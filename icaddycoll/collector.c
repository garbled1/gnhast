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
#include "genconn.h"
#include "collector.h"
#include "icaddy.h"
#include "jsmn.h"
#include "jsmn_func.h"
#include "http_func.h"

char *conffile = SYSCONFDIR "/" ICADDYCOLL_CONFIG_FILE;
FILE *logfile;   /** our logfile */
cfg_t *cfg, *gnhastd_c, *icaddy_c;
char *dumpconf = NULL;
int need_rereg = 0;
int discovery_fd = -1;
int discovery_port = 0;
int discovery_count = 0;
int discovery_done = 0;
int waiting = 0; /* are we waiting for more zone updates from gnhastd? */
char *icaddy_url = NULL;
char *ichn;
int hasrain = 0;
icaddy_discovery_resp_t icaddy_list[MAX_ICADDY_DEVS];
struct event *disc_ev; /* the discovery event */
time_t icaddy_lastupd;
http_get_t *status_get;
http_get_t *settings_get;
 
/* debugging */
//_malloc_options = "AJ";

/** Need the argtable in scope, so we can generate proper commands
    for the server */
extern argtable_t argtable[];
extern TAILQ_HEAD(, _device_t) alldevs;
extern commands_t commands[];
extern int debugmode;
extern int notimerupdate;
extern int collector_instance;

/** The event base */
struct event_base *base;
struct evdns_base *dns_base;

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
	CFG_INT("instance", 1, CFGF_NONE),
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

void cb_shutdown(int fd, short what, void *arg);
void request_cb(struct evhttp_request *req, void *arg);

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
	/* we got a master enable request */
	LOG(LOG_NOTICE, "Got master %s request",
	    state ? "enable" : "disable");
	if (state == 0)
		http_POST(icaddy_url, ICJ_RUNOFF_URL, ICADDY_HTTP_PORT,
			  ICJ_RUNOFF_POST, request_cb);
	else
		http_POST(icaddy_url, ICJ_RUNON_URL, ICADDY_HTTP_PORT,
			  ICJ_RUNON_POST, request_cb);
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
			http_POST(icaddy_url, ICJ_RUNOFF_URL, ICADDY_HTTP_PORT,
				  ICJ_RUNOFF_POST, request_cb);
			http_POST(icaddy_url, ICJ_RUNON_URL, ICADDY_HTTP_PORT,
				  ICJ_RUNON_POST, request_cb);
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
			http_POST(icaddy_url, "/", ICADDY_HTTP_PORT, buf,
				  request_cb);
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
			http_POST(icaddy_url, "/", ICADDY_HTTP_PORT, buf,
				  request_cb);
			return;
		}
	}
	if (strcasecmp(dev->loc, "zr") == 0) { /* zone running */
		if (num == 0) { /* stop the zone */
			http_POST(icaddy_url, ICJ_RUNOFF_URL, ICADDY_HTTP_PORT,
				  ICJ_STOPPROG_POST, request_cb);
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
   \brief Called when a chg command occurs
   \param dev device that got updated
   \param arg pointer to client_t
*/

void coll_chg_cb(device_t *dev, void *arg)
{
	uint8_t state;
	int64_t ll;
	uint32_t tstate;

	switch (dev->subtype) {
	case SUBTYPE_SWITCH:
		get_data_dev(dev, DATALOC_CHANGE, &state);
		coll_chg_switch_cb(dev, state, arg);
		break;
	case SUBTYPE_NUMBER:
		get_data_dev(dev, DATALOC_CHANGE, &ll);
		coll_chg_number_cb(dev, ll, arg);
		break;
	case SUBTYPE_TIMER:
		get_data_dev(dev, DATALOC_CHANGE, &tstate);
		coll_chg_timer_cb(dev, tstate, arg);
		break;
	default:
		LOG(LOG_ERROR, "Got unhandled chg for subtype %d",
		    dev->subtype);
	}
	return;
}

/**
   \brief Check if a collector is functioning properly
   \param conn connection_t of collector's gnhastd connection
   \return 1 if OK, 0 if broken
   \note if 5 updates pass with no data, bad bad.
*/

int collector_is_ok(void)
{
	int update;

	update = cfg_getint(icaddy_c, "update");
	if ((time(NULL) - icaddy_lastupd) < (update * 5))
		return(1);
	return(0);
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
		if (req->evcon != NULL)
			evhttp_connection_free(req->evcon);
		return;
		break;
	}
	jsmn_init(&jp);

	data = evhttp_request_get_input_buffer(req);
	len = evbuffer_get_length(data);
	LOG(LOG_DEBUG, "input buf len= %d", len);
	if (len == 0) {
		if (req->evcon != NULL)
			evhttp_connection_free(req->evcon);
		return;
	}

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
	/* if we got here, things are sane, so mark lastupd */
	icaddy_lastupd = time(NULL);

request_cb_out:
	free(buf);
	if (req->evcon != NULL)
		evhttp_connection_free(req->evcon);
	return;
}

/**
   \brief http_get precheck for status updates
   \param getinfo http_get_t structure of get
   \return -1 if precheck fails
 */

int precheck_status(http_get_t *getinfo)
{
	if (strcmp(getinfo->url_suffix, ICJ_STATUS) == 0 && notimerupdate) {
		waiting++;
		if (waiting > 3) { /* took too long, punt */
			waiting = 0;
			notimerupdate = 0;
		} else
			return -1;
	}
	return 0;
}

/**
   \brief Start the feed
*/

void icaddy_startfeed(char *url_prefix)
{
	struct event *ev;
	struct timeval secs = { 0, 0 };

	status_get = smalloc(http_get_t);
	status_get->url_prefix = url_prefix;
	status_get->url_suffix = ICJ_STATUS;
	status_get->cb = request_cb;
	status_get->http_port = ICADDY_HTTP_PORT;
	status_get->precheck = precheck_status;
	status_get->http_cn = NULL;

	settings_get = smalloc(http_get_t);
	settings_get->url_prefix = url_prefix;
	settings_get->url_suffix = ICJ_SETTINGS;
	settings_get->cb = request_cb;
	settings_get->http_port = ICADDY_HTTP_PORT;
	settings_get->precheck = NULL;
	settings_get->http_cn = NULL;

	secs.tv_sec = cfg_getint(icaddy_c, "update");
	ev = event_new(base, -1, EV_PERSIST, cb_http_GET, status_get);
	event_add(ev, &secs);
	LOG(LOG_NOTICE, "Starting feed timer updates every %d seconds",
	    secs.tv_sec);

	/* do one right now */
	cb_http_GET(0, 0, settings_get);
	cb_http_GET(0, 0, status_get);
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
			generic_cb_shutdown(0, 0, NULL);
			return; /*NOTREACHED*/
		}
	}

	if (hn == NULL) {
		LOG(LOG_ERROR, "Hostname still NULL, giving up");
		generic_cb_shutdown(0, 0, NULL);
		return; /*NOTREACHED*/
	}

	for (i=0; i < MAX_ICADDY_DEVS; i++) {
		if (icaddy_list[i].uc_hostname != NULL &&
		    strcasecmp(hn, icaddy_list[i].uc_hostname) == 0) {
			LOG(LOG_DEBUG, "Found disc record %d matches by"
			    " hostname", i);
			hn_len = strlen(hn);
			/* alloc for hn + http://hn\0 */
			icaddy_url = safer_malloc(hn_len + 8);
			sprintf(icaddy_url, "http://%s", hn);
			break;
		} else if (icaddy_list[i].ipaddr != NULL &&
			   strcasecmp(hn, icaddy_list[i].ipaddr) == 0) {
			hn_len = strlen(icaddy_list[i].ipaddr);
			icaddy_url = safer_malloc(hn_len + 9);
			sprintf(icaddy_url, "http://%s",
				icaddy_list[i].ipaddr);
			break;
		}
	}
	if (icaddy_url == NULL) {
		LOG(LOG_ERROR, "Couldn't find a matching controller, punt");
		generic_cb_shutdown(0, 0, NULL);
		return; /*NOTREACHED*/
	}
	LOG(LOG_NOTICE, "Set connect URL to %s", icaddy_url);
	icaddy_startfeed(icaddy_url);
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
	/* Initialize the command table */
	init_commands();
	/* Initialize the device table */
	init_devtable(cfg, 0);
	icaddy_lastupd = time(NULL);

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
	generic_connect_server_cb(0, 0, gnhastd_conn);
	collector_instance = cfg_getint(icaddy_c, "instance");
	gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);

	parse_devices(cfg);

	/* setup signal handlers */
	ev = evsignal_new(base, SIGHUP, cb_sighup, conffile);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGTERM, generic_cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGINT, generic_cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGQUIT, generic_cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGUSR1, cb_sigusr1, NULL);
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
