/*
 * Copyright (c) 2014
 *      Tim Rightnour.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of Tim Rightnour may not be used to endorse or promote 
 *    products derived from this software without specific prior written 
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TIM RIGHTNOUR ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL TIM RIGHTNOUR BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
   \file collector.c
   \author Tim Rightnour
   \brief IrrigationCaddy collector
*/

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <event2/dns.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/util.h>

#include "common.h"
#include "gnhast.h"
#include "confuse.h"
#include "confparser.h"
#include "gncoll.h"
#include "collcmd.h"
#include "icaddy.h"
#include "jsmn.h"

char *conffile = SYSCONFDIR "/" ICADDYCOLL_CONFIG_FILE;
FILE *logfile;   /** our logfile */
cfg_t *cfg, *gnhastd_c, *icaddy_c;
char *dumpconf = NULL;
int need_rereg = 0;
int discovery_fd = -1;
int discovery_port = 0;
int discovery_count = 0;
int discovery_done = 0;
int waiting = 0;
char *icaddy_url = NULL;
char *ichn;
int hasrain = 0;
icaddy_discovery_resp_t icaddy_list[MAX_ICADDY_DEVS];
struct event *disc_ev; /* the discovery event */
 
/* debugging */
//_malloc_options = "AJ";

/** Need the argtable in scope, so we can generate proper commands
    for the server */
extern argtable_t argtable[];
extern TAILQ_HEAD(, _device_t) alldevs;
extern commands_t commands[];
extern int debugmode;
extern int notimerupdate;

/** The event base */
struct event_base *base;
struct evdns_base *dns_base;
struct evhttp_connection *http_cn = NULL;

#define CONN_TYPE_ICADDY        1
#define CONN_TYPE_GNHASTD       2
char *conntype[3] = {
        "none",
        "IrrigationCaddy",
        "gnhastd",
};


/** The connection streams for our connection */
connection_t *gnhastd_conn;

/* Configuration file setup */

extern cfg_opt_t device_opts[];

cfg_opt_t gnhastd_opts[] = {
	CFG_STR("hostname", "127.0.0.1", CFGF_NONE),
	CFG_INT("port", 2920, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t icaddycoll_opts[] = {
	CFG_STR("hostname", 0, CFGF_NODEFAULT),
	CFG_INT("update", 60, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t options[] = {
	CFG_SEC("gnhastd", gnhastd_opts, CFGF_NONE),
	CFG_SEC("icaddycoll", icaddycoll_opts, CFGF_NONE),
	CFG_SEC("device", device_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_STR("logfile", ICADDYCOLL_LOG_FILE, CFGF_NONE),
	CFG_STR("pidfile", ICADDYCOLL_PID_FILE, CFGF_NONE),
	CFG_END(),
};


void connect_event_cb(struct bufferevent *ev, short what, void *arg);
void cb_shutdown(int fd, short what, void *arg);
void icaddy_connect(int fd, short what, void *arg);
void icaddy_POST(char *url_suffix, char *payload);

/*****
      Stubs
*****/

/**
   \brief Called when a switch chg command occurs
   \param dev device that got updated
   \param state new state (on/off)
   \param arg pointer to client_t
*/

void coll_chg_switch_cb(device_t *dev, int state, void *arg)
{
	if (dev->subtype == SUBTYPE_SWITCH) {
		/* we got a master enable request */
		LOG(LOG_NOTICE, "Got master %s request",
		    state ? "enable" : "disable");
		if (state == 0)
			icaddy_POST(ICJ_RUNOFF_URL, ICJ_RUNOFF_POST);
		else
			icaddy_POST(ICJ_RUNON_URL, ICJ_RUNON_POST);
		return;
	}
	return;
}

/**
   \brief Called when a number chg command occurs
   \param dev device that got updated
   \param num new number
   \param arg pointer to client_t
*/

void coll_chg_number_cb(device_t *dev, int64_t num, void *arg)
{
	char buf[256], loc[32], buf2[32];
	int i, found;
	uint32_t timer;
	device_t *zdev;

	LOG(LOG_DEBUG, "Change req: %s %s %ll", dev->name, dev->loc, num);
	if (strcasecmp(dev->loc, "pr") == 0) { /* prog running */
		if (num == 0) { /* stop program */
			/* requires a full stop/start action */
			icaddy_POST(ICJ_RUNOFF_URL, ICJ_RUNOFF_POST);
			icaddy_POST(ICJ_RUNON_URL, ICJ_RUNON_POST);
			return;
		}
		if (num > 4) {
			LOG(LOG_WARNING, "Got request to run prog %d, ignored",
			    num);
			return;
		}
		if (num >= 1 && num <= 3) {
			sprintf(buf, "pgmNum=%d&doProgram=1&runNow=true",
				(int)num);
			LOG(LOG_DEBUG, "Sending: %s", buf);
			icaddy_POST(NULL, buf);
			return;
		}
		/* the below search is obnoxious, but necc. due to ordering */
		if (num == 4) { /* special handling */
			sprintf(buf, "pgmNum=4&doProgram=1&runNow=true");
			for (i = 1; i <= 10; i++) {
				found = 0;
				TAILQ_FOREACH(zdev, &alldevs, next_all) {
					sprintf(loc, "z%0.2d", i);
					if (strcmp(zdev->loc, loc) == 0) {
						found = 1;
						break;
					}
				}
				if (!found) {
					LOG(LOG_WARNING, "Cannot find dev %s",
					    loc);
					return;
				}
				get_data_dev(zdev, DATALOC_DATA, &timer);
				if (timer > 0) {
					sprintf(buf2, "&z%ddurMin=%d", i,
						(timer/60) ? (timer/60) : 1);
					strcat(buf, buf2);
				}
			}
			waiting = 0;
			notimerupdate = 0;
			icaddy_POST(NULL, buf);
			return;
		}
	}
	if (strcasecmp(dev->loc, "zr") == 0) { /* zone running */
		if (num == 0) { /* stop the zone */
			icaddy_POST(ICJ_RUNOFF_URL, ICJ_STOPPROG_POST);
			return;
		}
	}
}

/**
   \brief Called when a timer chg command occurs
   \param dev device that got updated
   \param tstate new timer value
   \param arg pointer to client_t
*/

void coll_chg_timer_cb(device_t *dev, uint32_t tstate, void *arg)
{

	notimerupdate = 1;
	store_data_dev(dev, DATALOC_DATA, &tstate);
	return;
}

/**
   \brief Called when a dimmer chg command occurs
   \param dev device that got updated
   \param level new dimmer level
   \param arg pointer to client_t
*/

void coll_chg_dimmer_cb(device_t *dev, double level, void *arg)
{
	return;
}

/**
   \brief Handle a enldevs device command
   \param args The list of arguments
   \param arg void pointer to client_t of provider
*/

int cmd_endldevs(pargs_t *args, void *arg)
{
	return;
}

/**
   \brief Called when an upd command occurs
   \param dev device that got updated
   \param arg pointer to client_t
*/

void coll_upd_cb(device_t *dev, void *arg)
{
	return;
}

/**
   \brief Parse the config file for devices and load them
   \param cfg config base
*/
void parse_devices(cfg_t *cfg)
{
	device_t *dev;
	cfg_t *devconf;
	int i;

	for (i=0; i < cfg_size(cfg, "device"); i++) {
		devconf = cfg_getnsec(cfg, "device", i);
		dev = new_dev_from_conf(cfg, (char *)cfg_title(devconf));
		insert_device(dev);
		LOG(LOG_DEBUG, "Loaded device %s location %s from config file",
		    dev->uid, dev->loc);
		if (dumpconf == NULL && dev->name != NULL)
			gn_register_device(dev, gnhastd_conn->bev);
	}
}

/*****
  Icaddy Parsing Code
*****/

/**
   \brief Allocate a string for a token and return just that
   \param token token to parse
   \param buf buffer containing the parsed string
   \return nul terminated string containing data
   Must be a string.
   Must free return value
*/

char *jtok_string(jsmntok_t *token, char *buf)
{
	char *data;
	size_t len;

	if (token->type != JSMN_STRING)
		return NULL;
	len = token->end - token->start;
	if (len < 1)
		return NULL;
	data = safer_malloc(len+1);

	strncpy(data, buf+token->start, len);
	data[len] = '\0';
	return data;
}

/**
   \brief Return int value of token
   \param token token to parse
   \param buf buffer containing the parsed string
   \return zero if error, or value
   Cannot be array type
*/

int jtok_int(jsmntok_t *token, char *buf)
{
	char data[256];
	size_t len;

	if (token->type == JSMN_ARRAY)
		return 0;
	len = token->end - token->start;
	if (len < 1 || len > 255)
		return 0;

	strncpy(data, buf+token->start, len);
	data[len] = '\0';
	return atoi(data);
}

/**
   \brief Return bool value of token
   \param token token to parse
   \param buf buffer containing the parsed string
   \return zero if error, or value
   Cannot be array type
   if anything other than "true" returns false
*/

int jtok_bool(jsmntok_t *token, char *buf)
{
	char data[256];
	size_t len;

	if (token->type == JSMN_ARRAY)
		return 0;
	len = token->end - token->start;
	if (len < 1 || len > 255)
		return 0;

	strncpy(data, buf+token->start, len);
	data[len] = '\0';
	if (strcasecmp(data, "true") == 0)
		return 1;
	return 0;
}

/**
   \brief Find a specific token by string name
   \param tokens array of tokens
   \param buf parsed buffer
   \param match string to match
   \param maxtoken last token
*/

int jtok_find_token(jsmntok_t *tokens, char *buf, char *match, int maxtoken)
{
	int i;
	size_t len, mlen;

	if (match == NULL || buf == NULL)
		return -1;
	mlen = strlen(match);
	for (i=0; i<maxtoken; i++) {
		if (tokens[i].type != JSMN_STRING)
			continue;
		len = tokens[i].end - tokens[i].start;
		if (len != mlen)
			continue;
		if (strncmp(buf+tokens[i].start, match, len) == 0)
			return i;
	}
	return -1;
}

#define JSMN_TEST_OR_FAIL(ret, str) \
	if (ret == -1) { \
		LOG(LOG_ERROR, "Couldn't parse %s", str); \
		goto request_cb_out; \
      	}

/**
   \brief Callback for http request
   \param req request structure
   \param arg unused
*/

void request_cb(struct evhttp_request *req, void *arg)
{
	struct evbuffer *data;
	size_t len;
	char *buf, *str, dbuf[256];
	jsmn_parser jp;
	jsmntok_t token[255];
	int jret, i, j, nrofzones=0;
	device_t *dev;

	ichn = cfg_getstr(icaddy_c, "hostname");

	if (req == NULL) {
		LOG(LOG_ERROR, "Got NULL req in request_cb() ??");
		return;
	}

	switch (req->response_code) {
	case HTTP_OK: break;
	default:
		LOG(LOG_ERROR, "Http request failure: %d", req->response_code);
		return;
		break;
	}
	jsmn_init(&jp);

	data = evhttp_request_get_input_buffer(req);
	len = evbuffer_get_length(data);
	LOG(LOG_DEBUG, "input buf len= %d", len);
	if (len == 0)
		return;

	buf = safer_malloc(len+1);
	if (evbuffer_copyout(data, buf, len) != len) {
		LOG(LOG_ERROR, "Failed to copyout %d bytes", len);
		goto request_cb_out;
	}
	buf[len] = '\0'; /* just in case of stupid */
	LOG(LOG_DEBUG, "input buf: %s", buf);
	jret = jsmn_parse(&jp, buf, len, token, 255);
	if (jret < 0) {
		LOG(LOG_ERROR, "Failed to parse jsom string: %d", jret);
		/* Leave the data on the queue and punt for more */
		/* XXX NEED switch for token error */
		goto request_cb_out;
	} else
		evbuffer_drain(data, len); /* toss it */

	/* look for a status response */
	if (strcasecmp(req->uri, ICJ_STATUS) == 0) {
		int64_t curzone;
		uint32_t sec;

		LOG(LOG_DEBUG, "Got status message");

		/* which zone is running? */
		i = jtok_find_token(token, buf, "zoneNumber", jret);
		JSMN_TEST_OR_FAIL(i, "zoneNumber");
		curzone = jtok_int(&token[i+1], buf);
		sprintf(dbuf, "%s-zonerunning", ichn);
		dev = find_device_byuid(dbuf);
		if (dev == NULL) {
			LOG(LOG_ERROR, "Can't find dev for %s", dbuf);
			goto request_cb_out;
		}
		store_data_dev(dev, DATALOC_DATA, &curzone);
		gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
		if (curzone) {
			i = jtok_find_token(token, buf, "zoneSecLeft", jret);
			JSMN_TEST_OR_FAIL(i, "zoneSecLeft");
			sec = (uint32_t)jtok_int(&token[i+1], buf);
			sprintf(dbuf, "%s-zone%0.2d", ichn, curzone);
			dev = find_device_byuid(dbuf);
			if (dev == NULL) {
				LOG(LOG_ERROR, "Can't find dev for %s", dbuf);
				goto request_cb_out;
			}
			store_data_dev(dev, DATALOC_DATA, &sec);
			gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
			LOG(LOG_DEBUG, "Currently running zone %d, "
			    "time left: %u seconds", curzone, sec);
		}

		/* which program is running? */
		i = jtok_find_token(token, buf, "progNumber", jret);
		JSMN_TEST_OR_FAIL(i, "progNumber");
		curzone = jtok_int(&token[i+1], buf);
		sprintf(dbuf, "%s-progrunning", ichn);
		dev = find_device_byuid(dbuf);
		if (dev == NULL) {
			LOG(LOG_ERROR, "Can't find dev for %s", dbuf);
			goto request_cb_out;
		}
		store_data_dev(dev, DATALOC_DATA, &curzone);
		gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
		if (curzone) {
			i = jtok_find_token(token, buf, "progSecLeft", jret);
			JSMN_TEST_OR_FAIL(i, "progSecLeft");
			sec = (uint32_t)jtok_int(&token[i+1], buf);
			LOG(LOG_DEBUG, "Currently running program %d, "
			    "time left: %u seconds", curzone, sec);
			sprintf(dbuf, "%s-program%d", ichn, curzone);
			dev = find_device_byuid(dbuf);
			if (dev == NULL) {
				LOG(LOG_ERROR, "Can't find dev for %s", dbuf);
				goto request_cb_out;
			}
			store_data_dev(dev, DATALOC_DATA, &sec);
			gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
		}

		/* is the unit on? */
		i = jtok_find_token(token, buf, "allowRun", jret);
		JSMN_TEST_OR_FAIL(i, "allowRun");
		j = jtok_bool(&token[i+1], buf);
		sprintf(dbuf, "%s-run", ichn);
		dev = find_device_byuid(dbuf);
		if (dev == NULL) {
			LOG(LOG_ERROR, "Can't find dev for %s", dbuf);
			goto request_cb_out;
		}
		store_data_dev(dev, DATALOC_DATA, &j);
		gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
		LOG(LOG_DEBUG, "Icaddy is currently %s",
		    (j ? "enabled" : "disabled"));

		/* is it raining ? */
		if (hasrain) {
			i = jtok_find_token(token, buf, "isRaining", jret);
			JSMN_TEST_OR_FAIL(i, "isRaining");
			j = jtok_bool(&token[i+1], buf);
			sprintf(dbuf, "%s-rain", ichn);
			dev = find_device_byuid(dbuf);
			if (dev == NULL) {
				LOG(LOG_ERROR, "Can't find dev for %s", dbuf);
				goto request_cb_out;
			}
			store_data_dev(dev, DATALOC_DATA, &j);
			gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
		}

	/* Look for a settings response */
	} else if (strcasecmp(req->uri, ICJ_SETTINGS) == 0) {
		LOG(LOG_DEBUG, "Got settings message");
		i = jtok_find_token(token, buf, "icVersion", jret);
		if (i != -1) {
			str = jtok_string(&token[i+1], buf);
			LOG(LOG_NOTICE, "Firmware Revision: %s", str);
			free(str);
		}
		i = jtok_find_token(token, buf, "maxZones", jret);
		if (i != -1) {
			nrofzones = jtok_int(&token[i+1], buf);
			LOG(LOG_DEBUG, "Max zones %d", nrofzones);
		}
		i = jtok_find_token(token, buf, "zNames", jret);
		JSMN_TEST_OR_FAIL(i, "zNames");
		for (j=0; j < token[i+1].size; j++) {
			sprintf(dbuf, "%s-zone%0.2d", ichn, j+1);
			dev = find_device_byuid(dbuf);
			if (dev == NULL)
				dev = new_dev_from_conf(cfg, dbuf);
			if (dev == NULL) {
				LOG(LOG_DEBUG, "creating new dev: %s", dbuf);
				dev = smalloc(device_t);
				dev->uid = strdup(dbuf);
				if (dumpconf != NULL) {
					dev->name = jtok_string(&token[i+2+j],
								buf);
					if (dev->name == NULL) {
						sprintf(dbuf, "Unused %d",j+1);
						dev->name = strdup(dbuf);
					}
					sprintf(dbuf, "zone%0.2d", j+1);
					dev->rrdname = strdup(dbuf);
				}
				sprintf(dbuf, "z%0.2d", j+1);
				dev->loc = strdup(dbuf);
				dev->proto = PROTO_SENSOR_ICADDY;
				dev->type = DEVICE_TIMER;
				dev->subtype = SUBTYPE_TIMER;
				(void) new_conf_from_dev(cfg, dev);
			} else {
				sprintf(dbuf, "z%0.2d", j+1);
				dev->loc = strdup(dbuf);
			}
			insert_device(dev);
			if (dumpconf == NULL && dev->name != NULL)
				gn_register_device(dev, gnhastd_conn->bev);
		}
		/* just go ahead and create the devs for the programs now */
		for (j=0; j < 4; j++) {
			sprintf(dbuf, "%s-program%d", ichn, j+1);
			dev = find_device_byuid(dbuf);
			if (dev == NULL)
				dev = new_dev_from_conf(cfg, dbuf);
			if (dev == NULL) {
				dev = smalloc(device_t);
				dev->uid = strdup(dbuf);
				if (dumpconf != NULL) {
					sprintf(dbuf, "Program %d", j+1);
					dev->name = strdup(dbuf);
					sprintf(dbuf, "prog%0.2d", j+1);
					dev->rrdname = strdup(dbuf);
				}
				sprintf(dbuf, "p%0.2d", j+1);
				dev->loc = strdup(dbuf);
				dev->proto = PROTO_SENSOR_ICADDY;
				dev->type = DEVICE_TIMER;
				dev->subtype = SUBTYPE_TIMER;
				(void) new_conf_from_dev(cfg, dev);
			} else {
				sprintf(dbuf, "p%0.2d", j+1);
				dev->loc = strdup(dbuf);
			}
			insert_device(dev);
			if (dumpconf == NULL && dev->name != NULL)
				gn_register_device(dev, gnhastd_conn->bev);
		}
		i = jtok_find_token(token, buf, "useSensor1", jret);
		JSMN_TEST_OR_FAIL(i, "useSensor1");
		if (jtok_int(&token[i+1], buf) != 0) {
			hasrain = 1;
			sprintf(dbuf, "%s-rain", ichn);
			dev = find_device_byuid(dbuf);
			if (dev == NULL)
				dev = new_dev_from_conf(cfg, dbuf);
			if (dev == NULL) {
				dev = smalloc(device_t);
				dev->uid = strdup(dbuf);
				if (dumpconf != NULL) {
					sprintf(dbuf, "Rain Sensor");
					dev->name = strdup(dbuf);
					sprintf(dbuf, "rainsense");
					dev->rrdname = strdup(dbuf);
				}
				dev->proto = PROTO_SENSOR_ICADDY;
				dev->type = DEVICE_SENSOR;
				dev->subtype = SUBTYPE_SWITCH;
				(void) new_conf_from_dev(cfg, dev);
			}
			insert_device(dev);
			if (dumpconf == NULL && dev->name != NULL)
				gn_register_device(dev, gnhastd_conn->bev);
		}

		/* Main unit runnable device */
		sprintf(dbuf, "%s-run", ichn);
		dev = find_device_byuid(dbuf);
		if (dev == NULL)
			dev = new_dev_from_conf(cfg, dbuf);
		if (dev == NULL) {
			dev = smalloc(device_t);
			dev->uid = strdup(dbuf);
			if (dumpconf != NULL) {
				sprintf(dbuf, "Main Unit Enable");
				dev->name = strdup(dbuf);
				sprintf(dbuf, "unitenable");
				dev->rrdname = strdup(dbuf);
			}
			dev->proto = PROTO_SENSOR_ICADDY;
			dev->type = DEVICE_SWITCH;
			dev->subtype = SUBTYPE_SWITCH;
			(void) new_conf_from_dev(cfg, dev);
		}
		insert_device(dev);
		if (dumpconf == NULL && dev->name != NULL)
			gn_register_device(dev, gnhastd_conn->bev);

		/* cur program device */
		sprintf(dbuf, "%s-progrunning", ichn);
		dev = find_device_byuid(dbuf);
		if (dev == NULL)
			dev = new_dev_from_conf(cfg, dbuf);
		if (dev == NULL) {
			dev = smalloc(device_t);
			dev->uid = strdup(dbuf);
			if (dumpconf != NULL) {
				sprintf(dbuf, "Current Running Program");
				dev->name = strdup(dbuf);
				sprintf(dbuf, "progrunning");
				dev->rrdname = strdup(dbuf);
			}
			dev->proto = PROTO_SENSOR_ICADDY;
			dev->type = DEVICE_SENSOR;
			dev->subtype = SUBTYPE_NUMBER;
			if (dev->loc == NULL) {
				sprintf(dbuf, "pr");
				dev->loc = strdup(dbuf);
			}
			(void) new_conf_from_dev(cfg, dev);
		} else {
			sprintf(dbuf, "pr");
			dev->loc = strdup(dbuf);
		}
		insert_device(dev);
		if (dumpconf == NULL && dev->name != NULL)
			gn_register_device(dev, gnhastd_conn->bev);

		/* cur zone running */
		sprintf(dbuf, "%s-zonerunning", ichn);
		dev = find_device_byuid(dbuf);
		if (dev == NULL)
			dev = new_dev_from_conf(cfg, dbuf);
		if (dev == NULL) {
			dev = smalloc(device_t);
			dev->uid = strdup(dbuf);
			if (dumpconf != NULL) {
				sprintf(dbuf, "Current Running Zone");
				dev->name = strdup(dbuf);
				sprintf(dbuf, "zonerunning");
				dev->rrdname = strdup(dbuf);
			}
			sprintf(dbuf, "zr");
			dev->loc = strdup(dbuf);
			dev->proto = PROTO_SENSOR_ICADDY;
			dev->type = DEVICE_SENSOR;
			dev->subtype = SUBTYPE_NUMBER;
			(void) new_conf_from_dev(cfg, dev);
		} else {
			sprintf(dbuf, "zr");
			dev->loc = strdup(dbuf);
		}
		insert_device(dev);
		if (dumpconf == NULL && dev->name != NULL)
			gn_register_device(dev, gnhastd_conn->bev);


		/* are we dumping the conf file? */
		if (dumpconf != NULL) {
			LOG(LOG_NOTICE, "Dumping config file to "
			    "%s and exiting", dumpconf);
			dump_conf(cfg, 0, dumpconf);
			exit(0);
		}
	}

request_cb_out:
	free(buf);
	return;
}

/**
   \brief Callback to connect to the irrigation caddy and collect data
   \param fd unused
   \param what what happened?
   \param arg url suffix
*/

void icaddy_connect(int fd, short what, void *arg)
{
	char *url_suffix = (char *)arg;
	struct evhttp_uri *uri;
	struct evhttp_request *req;
	device_t *dev;

	if (strcmp(url_suffix, ICJ_STATUS) == 0 && notimerupdate) {
		waiting++;
		if (waiting > 3) { /* took too long, punt */
			waiting = 0;
			notimerupdate = 0;
		} else
			return;
	}

	if (icaddy_url == NULL) {
		LOG(LOG_ERROR, "icaddy_url is NULL, punt");
		cb_shutdown(0, 0, NULL);
		return;
	}

	if (url_suffix == NULL) {
		LOG(LOG_ERROR, "No URL suffix, what am I supposed to do?");
		return;
	}

	uri = evhttp_uri_parse(icaddy_url);
	if (uri == NULL) {
		LOG(LOG_ERROR, "Failed to parse URL");
		return;
	}

	evhttp_uri_set_port(uri, ICADDY_HTTP_PORT);
	LOG(LOG_DEBUG, "host: %s port: %d", evhttp_uri_get_host(uri),
	    evhttp_uri_get_port(uri));

	if (http_cn == NULL)
		http_cn = evhttp_connection_base_new(base, dns_base,
						     evhttp_uri_get_host(uri),
						     evhttp_uri_get_port(uri));
	
	req = evhttp_request_new(request_cb, NULL);
	evhttp_make_request(http_cn, req, EVHTTP_REQ_GET, url_suffix);
	evhttp_add_header(req->output_headers, "Host",
			  evhttp_uri_get_host(uri));
	evhttp_uri_free(uri);
}

/**
   \brief POST data to the icaddy
   \param url_suffix URL to POST to
   \param payload data to send
*/

void icaddy_POST(char *url_suffix, char *payload)
{
	struct evhttp_uri *uri;
	struct evhttp_request *req;
	struct evbuffer *data;
	int i;
	char buf[256];

	if (icaddy_url == NULL) {
		LOG(LOG_ERROR, "icaddy_url is NULL, punt");
		cb_shutdown(0, 0, NULL);
		return;
	}

	uri = evhttp_uri_parse(icaddy_url);
	if (uri == NULL) {
		LOG(LOG_ERROR, "Failed to parse URL");
		return;
	}

	evhttp_uri_set_port(uri, ICADDY_HTTP_PORT);
	LOG(LOG_DEBUG, "host: %s port: %d", evhttp_uri_get_host(uri),
	    evhttp_uri_get_port(uri));

	if (http_cn == NULL)
		http_cn = evhttp_connection_base_new(base, dns_base,
						     evhttp_uri_get_host(uri),
						     evhttp_uri_get_port(uri));
	
	req = evhttp_request_new(request_cb, NULL);
	if (url_suffix != NULL)
		evhttp_make_request(http_cn, req, EVHTTP_REQ_POST, url_suffix);
	else
		evhttp_make_request(http_cn, req, EVHTTP_REQ_POST, "/");

	evhttp_add_header(req->output_headers, "Host",
			  evhttp_uri_get_host(uri));
	evhttp_add_header(req->output_headers, "Content-Type",
			  "application/x-www-form-urlencoded");

	LOG(LOG_DEBUG, "POSTing to %s%s", icaddy_url,
	    url_suffix ? url_suffix : "");
	if (payload != NULL) {
		sprintf(buf, "%d", strlen(payload));
		evhttp_add_header(req->output_headers, "Content-Length", buf);
		data = evhttp_request_get_output_buffer(req);
		evbuffer_add_printf(data, payload);
		LOG(LOG_DEBUG, "POST payload: %s", payload);
	}
	evhttp_uri_free(uri);
}

/**
   \brief Start the feed
*/

void icaddy_startfeed(void)
{
	struct event *ev;
	struct timeval secs = { 0, 0 };

	secs.tv_sec = cfg_getint(icaddy_c, "update");
	ev = event_new(base, -1, EV_PERSIST, icaddy_connect, ICJ_STATUS);
	event_add(ev, &secs);
	LOG(LOG_NOTICE, "Starting feed timer updates every %d seconds",
	    secs.tv_sec);
	/* do one right now */
	icaddy_connect(0, 0, ICJ_SETTINGS);
	icaddy_connect(0, 0, ICJ_STATUS);
}


/*****
      General routines/gnhastd connection stuff
*****/

/**
   \brief A write callback, if we need to tell server something
   \param out The bufferevent that fired
   \param arg optional argument
*/

void buf_write_cb(struct bufferevent *out, void *arg)
{
	struct evbuffer *send;

	send = evbuffer_new();
	evbuffer_add_printf(send, "test\n");
	bufferevent_write_buffer(out, send);
	evbuffer_free(send);
}

/**
   \brief Error callback, close down connection
   \param ev The bufferevent that fired
   \param what why did it fire?
   \param arg set to the client structure, so we can free it out and close fd
*/

void buf_error_cb(struct bufferevent *ev, short what, void *arg)
{
	client_t *client = (client_t *)arg;

	bufferevent_free(client->ev);
	close(client->fd);
	free(client);
	exit(2);
}

/**
   \brief A timer callback that initiates a new connection
   \param nada used for file descriptor
   \param what why did we fire?
   \param arg pointer to connection_t
   \note also used to manually initiate a connection
*/

void connect_server_cb(int nada, short what, void *arg)
{
	connection_t *conn = (connection_t *)arg;

	conn->bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
	bufferevent_setcb(conn->bev, gnhastd_read_cb, NULL,
			  connect_event_cb, conn);

	bufferevent_enable(conn->bev, EV_READ|EV_WRITE);
	bufferevent_socket_connect_hostname(conn->bev, dns_base, AF_UNSPEC,
					    conn->host, conn->port);
	LOG(LOG_NOTICE, "Attempting to connect to %s @ %s:%d",
	    conntype[conn->type], conn->host, conn->port);

	if (need_rereg) {
		gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);
	}
}


/**
   \brief Event callback used with connections
   \param ev The bufferevent that fired
   \param what why did it fire?
   \param arg pointer to connection_t;
*/

void connect_event_cb(struct bufferevent *ev, short what, void *arg)
{
	int err;
	connection_t *conn = (connection_t *)arg;
	struct event *tev; /* timer event */
	struct timeval secs = { 30, 0 }; /* retry in 30 seconds */

	if (what & BEV_EVENT_CONNECTED) {
		LOG(LOG_NOTICE, "Connected to %s", conntype[conn->type]);
	} else if (what & (BEV_EVENT_ERROR|BEV_EVENT_EOF)) {
		if (what & BEV_EVENT_ERROR) {
			err = bufferevent_socket_get_dns_error(ev);
			if (err)
				LOG(LOG_FATAL,
				    "DNS Failure connecting to %s: %s",
				    conntype[conn->type], strerror(err));
		}
		LOG(LOG_NOTICE, "Lost connection to %s, closing",
		    conntype[conn->type]);
		bufferevent_free(ev);

		if (!conn->shutdown) {
			/* we need to reconnect! */
			need_rereg = 1;
			tev = evtimer_new(base, connect_server_cb, conn);
			evtimer_add(tev, &secs); /* XXX leaks? */
			LOG(LOG_NOTICE, "Attempting reconnection to "
			    "conn->server @ %s:%d in %d seconds",
			    conn->host, conn->port, secs.tv_sec);
		} else
			event_base_loopexit(base, NULL);
	}
}

/*****
  Signal handlers and related phizz
*****/

/**
   \brief Shutdown timer
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void cb_shutdown(int fd, short what, void *arg)
{
	LOG(LOG_WARNING, "Clean shutdown timed out, stopping");
	event_base_loopexit(base, NULL);
}

/**
   \brief A sigterm handler
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void cb_sigterm(int fd, short what, void *arg)
{
	struct timeval secs = { 30, 0 };
	struct event *ev;

	LOG(LOG_NOTICE, "Recieved SIGTERM, shutting down");
	gnhastd_conn->shutdown = 1;
	gn_disconnect(gnhastd_conn->bev);
	ev = evtimer_new(base, cb_shutdown, NULL);
	evtimer_add(ev, &secs);
}

/*****
  irrigationcaddy discovery stuff
*****/

/**
   \brief End the discovery routine, and fire everything up
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void cb_end_discovery(int fd, short what, void *arg)
{
	char *hn;
	int i;
	size_t hn_len;

	LOG(LOG_NOTICE, "Discovery ended, found %d devices", discovery_count);
	event_del(disc_ev);
	event_free(disc_ev);
	close(discovery_fd);
	discovery_done = 1;

	hn = cfg_getstr(icaddy_c, "hostname");
	if (hn == NULL) {  /* maybe we just pick one? */
		if (discovery_count == 1) { /* ok, we do */
			LOG(LOG_NOTICE, "Only found one IrrigationCaddy, "
			    "using that one");
			hn = icaddy_list[0].uc_hostname;
			cfg_setstr(icaddy_c, "hostname", hn);
			ichn = cfg_getstr(icaddy_c, "hostname");
		} else {
			LOG(LOG_ERROR, "No hostname set in conf file, and "
			    "too many IrrigationCaddys found, giving up.");
			cb_shutdown(0, 0, NULL);
			return; /*NOTREACHED*/
		}
	}

	if (hn == NULL) {
		LOG(LOG_ERROR, "Hostname still NULL, giving up");
		cb_shutdown(0, 0, NULL);
		return; /*NOTREACHED*/
	}

	for (i=0; i < MAX_ICADDY_DEVS; i++) {
		if (strcasecmp(hn, icaddy_list[i].uc_hostname) == 0) {
			LOG(LOG_DEBUG, "Found disc record %d matches by"
			    " hostname", i);
			hn_len = strlen(hn);
			/* alloc for hn + http://hn\0 */
			icaddy_url = safer_malloc(hn_len + 8);
			sprintf(icaddy_url, "http://%s", hn);
			break;
		} else if (strcasecmp(hn, icaddy_list[i].ipaddr) == 0) {
			hn_len = strlen(icaddy_list[i].ipaddr);
			icaddy_url = safer_malloc(hn_len + 9);
			sprintf(icaddy_url, "http://%s",
				icaddy_list[i].ipaddr);
			break;
		}
	}
	if (icaddy_url == NULL) {
		LOG(LOG_ERROR, "Couldn't find a matching controller, punt");
		cb_shutdown(0, 0, NULL);
		return; /*NOTREACHED*/
	}
	LOG(LOG_NOTICE, "Set connect URL to %s", icaddy_url);
	icaddy_startfeed();
}

/**
   \brief bind udp to listen to icaddy discovery events
   \return fd
*/
int bind_discovery_recv(void)
{
	int sock_fd, f, flag=1;
	struct sockaddr_in sin;
	socklen_t slen;

	if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		LOG(LOG_ERROR, "Failed to bind discovery in socket()");
		return -1;
	}

	if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &flag,
		       sizeof(int)) < 0) {
		LOG(LOG_ERROR, "Could not set SO_REUSEADDR for discovery");
		return -1;
	}
	if (setsockopt(sock_fd, SOL_SOCKET, SO_BROADCAST, &flag,
		       sizeof(int)) < 0) {
		LOG(LOG_ERROR, "Could not set SO_BROADCAST for discovery");
		return -1;
	}
	if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEPORT, &flag,
		       sizeof(int)) < 0) {
		LOG(LOG_ERROR, "Could not set SO_REUSEPORT for discovery");
		return -1;
	}

	/* make it non-blocking */
	f = fcntl(sock_fd, F_GETFL);
	f |= O_NONBLOCK;
	fcntl(sock_fd, F_SETFL, f);

	/* Set IP, port */
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = 0; /* take any available port */

	if (bind(sock_fd, (struct sockaddr *)&sin,
		 sizeof(struct sockaddr)) < 0) {
		LOG(LOG_ERROR, "bind() for discovery port failed");
		return -1;
	} else {
		slen = sizeof(sin);
		getsockname(sock_fd, (struct sockaddr *)&sin, &slen);
		discovery_port = ntohs(sin.sin_port);
		LOG(LOG_NOTICE,
		    "Listening on port %d:udp for discovery events",
		    discovery_port);
	}

	return sock_fd;
}

/**
   \brief Watch for discovery messages
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void cb_discovery_read(int fd, short what, void *arg)
{
	char buf[2048], buf2[8];
	char *p, *r;
	int len, size;
	struct sockaddr_in cli_addr;

	size = sizeof(struct sockaddr);
	bzero(buf, sizeof(buf));
	len = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&cli_addr,
		       &size);

	LOG(LOG_DEBUG, "got %d bytes on udp:%d", len, discovery_port);
	discovery_count++;

	buf[len] = '\0';
	LOG(LOG_DEBUG, "Response #%d from: %s", discovery_count,
	    inet_ntoa(cli_addr.sin_addr));
	LOG(LOG_DEBUG, "%s", buf);

	if (discovery_count >= MAX_ICADDY_DEVS) {
		LOG(LOG_ERROR, "Too many IrrigationCaddys on your network!");
		LOG(LOG_ERROR, "Recompile and change MAX_ICADDY_DEVS!");
		return;
	}

	p = strchr(buf, '\n');
	if (p == NULL)
		return;

	*p = '\0';
	/* find the first space, that's the end of the hostname */
	r = strchr(buf, ' ');
	if (r != NULL)
		*r = '\0';

	icaddy_list[discovery_count-1].ipaddr = inet_ntoa(cli_addr.sin_addr);
	icaddy_list[discovery_count-1].sin_addr = cli_addr.sin_addr;
	icaddy_list[discovery_count-1].uc_hostname = strdup(buf);
	*p++;
	/* now find the \r, and fix it */
	r = strchr(p, '\r');
	if (r != NULL)
		*r = '\0';
	icaddy_list[discovery_count-1].macaddr = strdup(p);

	LOG(LOG_NOTICE, "Found IC#%d named %s at %s macaddr %s",
	    discovery_count, icaddy_list[discovery_count-1].uc_hostname,
	    icaddy_list[discovery_count-1].ipaddr,
	    icaddy_list[discovery_count-1].macaddr);
}

/**
   \brief Send an icaddy discovery string
   \param fd the search fd
   \param what what happened?
   \param arg unused
*/

void cb_discovery_send(int fd, short what, void *arg)
{
	char *send = ICADDY_DSTRING;
	int len;
	struct sockaddr_in broadcast_addr;

	memset(&broadcast_addr, 0, sizeof(broadcast_addr));
	broadcast_addr.sin_family = AF_INET;
	broadcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	broadcast_addr.sin_port = htons(ICADDY_DPORT);

	len = sendto(fd, send, strlen(send), 0,
		     (struct sockaddr *)&broadcast_addr,
		     sizeof(struct sockaddr_in));
	LOG(LOG_DEBUG, "Sent Irrigation Caddy Discovery message");
}

/**
 */
void discovery_setup()
{
	struct event *timer_ev;
	struct timeval secs = { IC_SCAN_TIMEOUT, 0 };

	/* build discovery event */
	discovery_fd = bind_discovery_recv();
	if (discovery_fd != -1) {
		disc_ev = event_new(base, discovery_fd, EV_READ | EV_PERSIST,
				   cb_discovery_read, NULL);
		event_add(disc_ev, NULL);
		event_base_once(base, discovery_fd, EV_WRITE|EV_TIMEOUT,
				cb_discovery_send, NULL, NULL);
		LOG(LOG_NOTICE, "Searching for Irrigation Caddy devices");
	} else
		LOG(LOG_ERROR, "Failed to setup discovery event");

	timer_ev = evtimer_new(base, cb_end_discovery, NULL);
	evtimer_add(timer_ev, &secs);
}


/**
   \brief Main itself
   \param argc count
   \param arvg vector
   \return int
*/

int main(int argc, char **argv)
{
	extern char *optarg;
	extern int optind;
	int ch, fd;
	char *buf;
	struct event *ev;
	struct timeval secs = {0, 0};

	/* process command line arguments */
	while ((ch = getopt(argc, argv, "?c:dm:")) != -1)
		switch (ch) {
		case 'c':	/* Set configfile */
			conffile = optarg;
			break;
		case 'd':
			debugmode = 1;
			break;
		case 'm':
			dumpconf = strdup(optarg);
			break;
		default:
		case '?':	/* you blew it */
			(void)fprintf(stderr, "usage:\n%s [-c configfile]"
				      "[-m dumpconfigfile]\n", getprogname());
			return(EXIT_FAILURE);
			/*NOTREACHED*/
			break;
		}

	if (!debugmode)
		if (daemon(0, 0) == -1)
			LOG(LOG_FATAL, "Failed to daemonize: %s",
			    strerror(errno));
	/* Initialize the event system */
	base = event_base_new();
	dns_base = evdns_base_new(base, 1);

	/* Initialize the argtable */
	init_argcomm();
	/* Initialize the device table */
	init_devtable(cfg, 0);

	cfg = parse_conf(conffile);

	if (!debugmode)
		logfile = openlog(cfg_getstr(cfg, "logfile"));

	writepidfile(cfg_getstr(cfg, "pidfile"));

	/* First, parse the icaddy section */

	if (cfg) {
		icaddy_c = cfg_getsec(cfg, "icaddycoll");
		if (!icaddy_c)
			LOG(LOG_FATAL, "Error reading config file, icaddy section");
	}

	/* Now, parse the details of connecting to the gnhastd server */

	if (cfg) {
		gnhastd_c = cfg_getsec(cfg, "gnhastd");
		if (!gnhastd_c)
			LOG(LOG_FATAL, "Error reading config file, gnhastd section");
	}

	/* discover the icaddy */
	discovery_setup();

	gnhastd_conn = smalloc(connection_t);
	gnhastd_conn->port = cfg_getint(gnhastd_c, "port");
	gnhastd_conn->type = CONN_TYPE_GNHASTD;
	gnhastd_conn->host = cfg_getstr(gnhastd_c, "hostname");
	/* cheat, and directly call the timer callback
	   This sets up a connection to the server. */
	connect_server_cb(0, 0, gnhastd_conn);
	gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);

	parse_devices(cfg);

	/* setup signal handlers */
	ev = evsignal_new(base, SIGHUP, cb_sighup, conffile);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGTERM, cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGINT, cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGQUIT, cb_sigterm, NULL);
	event_add(ev, NULL);

	/* update timer devices */
	secs.tv_sec = 1;
	ev = event_new(base, -1, EV_PERSIST, cb_timerdev_update, NULL);
	event_add(ev, &secs);

	/* go forth and destroy */
	event_base_dispatch(base);

	closelog();
	delete_pidfile();
	return(0);
}
