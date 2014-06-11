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
   \brief Venstar T5800/T5900/T6800/T6900 collector
   Documentation for this is available at:
   http://developer.venstar.com/index.html
   Written for version 3 of the API.
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
#include "venstar.h"
#include "jsmn.h"
#include "ssdp.h"
#include "http_func.h"
#include "jsmn_func.h"

char *conffile = SYSCONFDIR "/" VENSTARCOLL_CONFIG_FILE;
FILE *logfile;   /** our logfile */
cfg_t *cfg, *gnhastd_c, *venstar_c;
char *dumpconf = NULL;
int need_rereg = 0;
int notify_fd = -1;
int query_running = 0;
char *venstar_url = NULL;
int max_age = 300;
char macaddr[16];
int tempscale = TSCALE_F;
struct event *disc_ev; /* the discovery event */
http_get_t *queryinfo_get;
http_get_t *querysensors_get;
http_get_t *queryruntimes_get;
http_get_t *queryalerts_get;
 
/* debugging */
//_malloc_options = "AJ";

/** Need the argtable in scope, so we can generate proper commands
    for the server */
extern argtable_t argtable[];
extern TAILQ_HEAD(, _device_t) alldevs;
extern commands_t commands[];
extern int debugmode;
extern int notimerupdate;
extern int ssdp_portnum;

/** The event base */
struct event_base *base;
struct evdns_base *dns_base;
struct evhttp_connection *http_cn = NULL;

#define CONN_TYPE_VENSTAR        1
#define CONN_TYPE_GNHASTD       2
char *conntype[3] = {
        "none",
        "Venstar",
        "gnhastd",
};


void connect_event_cb(struct bufferevent *ev, short what, void *arg);
void cb_shutdown(int fd, short what, void *arg);
int conf_parse_ttype(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		     void *result);
void update_mode(device_t *dev, uint8_t mode);
void request_cb(struct evhttp_request *req, void *arg);

/** The connection streams for our connection */
connection_t *gnhastd_conn;

/* The query/info map */

queryinfo_t queryinfo[] = {
	{ "mode", QI_TYPE_INT, TTYPE_RES },
	{ "state", QI_TYPE_INT, TTYPE_RES },
	{ "fan", QI_TYPE_BOOL, TTYPE_RES },
	{ "fanstate", QI_TYPE_BOOL, TTYPE_RES },
	{ "tempunits", QI_TYPE_BOOL, TTYPE_RES },
	{ "schedule", QI_TYPE_BOOL, TTYPE_RES },
	{ "schedulepart", QI_TYPE_INT, TTYPE_RES },
	{ "away", QI_TYPE_BOOL, TTYPE_RES },
	{ "holiday", QI_TYPE_BOOL, TTYPE_COM },
	{ "override", QI_TYPE_BOOL, TTYPE_COM },
	{ "overridetime", QI_TYPE_INT, TTYPE_COM },
	{ "forceunocc", QI_TYPE_BOOL, TTYPE_COM },
	{ "spacetemp", QI_TYPE_FLOAT, TTYPE_RES },
	{ "heattemp", QI_TYPE_FLOAT, TTYPE_RES },
	{ "cooltemp", QI_TYPE_FLOAT, TTYPE_RES },
	{ "cooltempmin", QI_TYPE_FLOAT, TTYPE_RES },
	{ "cooltempmax", QI_TYPE_FLOAT, TTYPE_RES },
	{ "heattempmin", QI_TYPE_FLOAT, TTYPE_RES },
	{ "heattempmax", QI_TYPE_FLOAT, TTYPE_RES },
	{ "setpointdelta", QI_TYPE_FLOAT, TTYPE_RES },
	{ "hum", QI_TYPE_FLOAT, TTYPE_RES },
	{ "availablemodes", QI_TYPE_INT, TTYPE_RES },
	{ NULL, 0, 0 },
};

/* Configuration file setup */

extern cfg_opt_t device_opts[];

cfg_opt_t gnhastd_opts[] = {
	CFG_STR("hostname", "127.0.0.1", CFGF_NONE),
	CFG_INT("port", 2920, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t venstarcoll_opts[] = {
	CFG_STR("name", "Thermostat", CFGF_NONE),
	CFG_INT("update", 60, CFGF_NONE),
	CFG_INT_CB("ttype", TTYPE_RES, CFGF_NONE, conf_parse_ttype),
	CFG_STR("tscale", "F", CFGF_NONE),
	CFG_END(),
};

cfg_opt_t options[] = {
	CFG_SEC("gnhastd", gnhastd_opts, CFGF_NONE),
	CFG_SEC("venstarcoll", venstarcoll_opts, CFGF_NONE),
	CFG_SEC("device", device_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_STR("logfile", VENSTARCOLL_LOG_FILE, CFGF_NONE),
	CFG_STR("pidfile", VENSTARCOLL_PID_FILE, CFGF_NONE),
	CFG_END(),
};


/*****
      Stubs
*****/

/**
   \brief Called when a chg command occurs
   \param dev device that got updated
   \param arg pointer to client_t
*/

void coll_chg_cb(device_t *dev, void *arg)
{
	uint8_t c;
	double d;
	char buf[512];

	LOG(LOG_DEBUG, "Got change request for dev %s", dev->uid);

	if (dev->subtype == SUBTYPE_THMODE) {
		get_data_dev(dev, DATALOC_CHANGE, &c);
		LOG(LOG_NOTICE, "Got request to change mode to %d", c);
		update_mode(dev, c);
		return;
	}
	if (strcmp(dev->loc, "heattemp") == 0) {
		get_data_dev(dev, DATALOC_CHANGE, &d);
		LOG(LOG_NOTICE, "Got request to change heattemp to %0.1f", d);
		sprintf(buf, "heattemp=%0.1f", d);
		http_POST(venstar_url, VEN_CONTROL, VENSTAR_HTTP_PORT, buf,
			  request_cb);
		return;
	}
	if (strcmp(dev->loc, "cooltemp") == 0) {
		get_data_dev(dev, DATALOC_CHANGE, &d);
		LOG(LOG_NOTICE, "Got request to change cooltemp to %0.1f", d);
		sprintf(buf, "cooltemp=%0.1f", d);
		http_POST(venstar_url, VEN_CONTROL, VENSTAR_HTTP_PORT, buf,
			  request_cb);
		return;
	}
	if (strcmp(dev->loc, "fan") == 0) {
		get_data_dev(dev, DATALOC_CHANGE, &c);
		LOG(LOG_NOTICE, "Got request to change fan to %d", c);
		sprintf(buf, "fan=%d", c);
		http_POST(venstar_url, VEN_CONTROL, VENSTAR_HTTP_PORT, buf,
			  request_cb);
		return;
	}
	if (strcmp(dev->loc, "away") == 0) {
		get_data_dev(dev, DATALOC_CHANGE, &c);
		LOG(LOG_NOTICE, "Got request to change away to %d", c);
		sprintf(buf, "away=%d", c);
		http_POST(venstar_url, VEN_SETTINGS, VENSTAR_HTTP_PORT, buf,
			  request_cb);
		return;
	}
	if (strcmp(dev->loc, "schedule") == 0) {
		get_data_dev(dev, DATALOC_CHANGE, &c);
		LOG(LOG_NOTICE, "Got request to change schedule to %d", c);
		sprintf(buf, "schedule=%d", c);
		http_POST(venstar_url, VEN_SETTINGS, VENSTAR_HTTP_PORT, buf,
			  request_cb);
		return;
	}
	return;
}

/**
   \brief Handle a endldevs device command
   \param args The list of arguments
   \param arg void pointer to client_t of provider
*/

int cmd_endldevs(pargs_t *args, void *arg)
{
	return;
}

/**
   \brief Handle a endlgrps device command
   \param args The list of arguments
   \param arg void pointer to client_t of provider
*/

int cmd_endlgrps(pargs_t *args, void *arg)
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
	return;
}

/**
   \brief parse a thermostat type
*/
int conf_parse_ttype(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		     void *result)
{
	if (strcasecmp(value, "residential") == 0)
		*(int *)result = TTYPE_RES;
	else if (strcmp(value, "0") == 0)
		*(int *)result = TTYPE_RES;
	else if (strcasecmp(value, "commercial") == 0)
		*(int *)result = TTYPE_COM;
	else if (strcmp(value, "1") == 0)
		*(int *)result = TTYPE_COM;
	else {
		cfg_error(cfg, "invalid ttype value for option '%s': %s",
		    cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}

/*****
  Venstar Control Code
*****/

#define GDATA_OR_FAIL(dev, buf, fmt, arg, loc, val)		\
	sprintf(buf, fmt, arg);					\
	dev = find_device_byuid(buf);				\
	if (dev == NULL) {					\
		LOG(LOG_ERROR, "Can't find dev for %s", buf);	\
		return;						\
	}							\
	get_data_dev(dev, loc, &val);

/**
   \brief Update the mode of the thermostat
   \param dev mode device
   \param mode new mode
*/

void update_mode(device_t *dev, uint8_t mode)
{
	double ctemp, htemp, delta;
	device_t *zdev;
	char dbuf[256], buf[512];
	
	GDATA_OR_FAIL(zdev, dbuf, "%s-cooltemp", macaddr,
		      DATALOC_DATA, ctemp);
	GDATA_OR_FAIL(zdev, dbuf, "%s-heattemp", macaddr,
		      DATALOC_DATA, htemp);
	if (mode == THERMMODE_OFF) {
		/* this is easy */
		sprintf(buf, "mode=%d&heattemp=%0.1f&cooltemp=%0.1f",
			mode, htemp, ctemp);
		http_POST(venstar_url, VEN_CONTROL, VENSTAR_HTTP_PORT, buf,
			  request_cb);
		return;
	}
	if (mode == THERMMODE_AUTO) {
		/* little more complex */
		GDATA_OR_FAIL(zdev, dbuf, "%s-setpointdelta", macaddr,
		      DATALOC_DATA, delta);
		if ((htemp + delta) > ctemp) {
			LOG(LOG_ERROR, "Heattemp of %0.1f + delta %0.1f is > "
			    "cooltemp %0.1f", htemp, delta, ctemp);
			return;
		}
		sprintf(buf, "mode=%d&heattemp=%0.1f&cooltemp=%0.1f",
			mode, htemp, ctemp);
		http_POST(venstar_url, VEN_CONTROL, VENSTAR_HTTP_PORT, buf,
			  request_cb);
		return;
	}
	if (mode == THERMMODE_HEAT) {
		sprintf(buf, "mode=%d&heattemp=%0.1f",
			mode, htemp);
		http_POST(venstar_url, VEN_CONTROL, VENSTAR_HTTP_PORT, buf,
			  request_cb);
		return;
	}
	if (mode == THERMMODE_COOL) {
		sprintf(buf, "mode=%d&cooltemp=%0.1f",
			mode, ctemp);
		http_POST(venstar_url, VEN_CONTROL, VENSTAR_HTTP_PORT, buf,
			  request_cb);
		return;
	}
}


/*****
  Venstar Parsing Code
*****/

#define JSMN_TEST_OR_FAIL(ret, str)			  \
	if (ret == -1) {				  \
		LOG(LOG_ERROR, "Couldn't parse %s", str); \
		goto request_cb_out;			  \
      	}

/**
   \brief Callback for http request
   \param req request structure
   \param arg unused
*/

void request_cb(struct evhttp_request *req, void *arg)
{
	struct evbuffer *data;
	struct event *ev;
	struct timeval secs = { 0, 0 };
	size_t len;
	char *buf, *str, dbuf[256];
	jsmn_parser jp;
	jsmntok_t token[255];
	int jret, qi, i, j, val_i;
	double val_d;
	device_t *dev, *zdev;

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

	/* look for a Query / Info */
	if (strcasecmp(req->uri, VEN_INFO) == 0) {
		LOG(LOG_DEBUG, "Got Query/Info message");

		/* Is this the right thermostat? */
		i = jtok_find_token_val(token, buf, "name", jret);
		JSMN_TEST_OR_FAIL(i, "name");
		str = jtok_string(&token[i], buf);
		if (strcasecmp(str, cfg_getstr(venstar_c, "name")) != 0) {
			LOG(LOG_WARNING, "Got message for wrong thermostat: "
			    "%s expected %s", str,
			    cfg_getstr(venstar_c, "name"));
			free(str);
			goto request_cb_out;
		}
		free(str);

		/* check to see if tempscale changed? */

		i = jtok_find_token_val(token, buf, "tempunits", jret);
		JSMN_TEST_OR_FAIL(i, "tempunits");
		val_i = jtok_int(&token[i], buf);
		if (val_i != tempscale) { /* oh god, it changed */
			LOG(LOG_WARNING, "Tempscale changed! Re-registering "
			    "all devices!");
			tempscale = val_i;
			TAILQ_FOREACH(zdev, &alldevs, next_all) {
				if (zdev->subtype == SUBTYPE_TEMP) {
					zdev->scale = tempscale;
					gn_register_device(dev,
							   gnhastd_conn->bev);
				}
			}
		}

		/* loop through the query fields */
		for (qi = 0; queryinfo[qi].name != NULL; qi++) {
			if (cfg_getint(venstar_c, "ttype") !=
			    queryinfo[qi].residential)
				continue;
			i = jtok_find_token_val(token, buf,
						queryinfo[qi].name, jret);
			if (i == -1) {
				LOG(LOG_WARNING, "Couldn't find token %s",
				    queryinfo[qi].name);
				continue;
			}
			sprintf(dbuf, "%s-%s", macaddr, queryinfo[qi].name);
			dev = find_device_byuid(dbuf);
			if (dev == NULL) {
				LOG(LOG_ERROR, "Can't find dev for %s", dbuf);
				continue;
			}
			switch (queryinfo[qi].type) {
			case QI_TYPE_BOOL:
			case QI_TYPE_INT:
				val_i = jtok_int(&token[i], buf);
				store_data_dev(dev, DATALOC_DATA, &val_i);
				break;
			case QI_TYPE_FLOAT:
				val_d = jtok_double(&token[i], buf);
				store_data_dev(dev, DATALOC_DATA, &val_d);
				break;
			}
			gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
		}
	} else if (strcasecmp(req->uri, VEN_SENSORS) == 0) {
		LOG(LOG_DEBUG, "Got query/sensors message");

		/* we only care about the Outdoor sensor */
		i = jtok_find_token_val_nth_array(token, buf, 1, "sensors",
						  "temp", jret);
		JSMN_TEST_OR_FAIL(i, "name");
		val_d = jtok_double(&token[i], buf);
		LOG(LOG_DEBUG, "Outdoor sensor reports %f", val_d);
		sprintf(dbuf, "%s-outdoortemp", macaddr);
		dev = find_device_byuid(dbuf);
		if (dev == NULL) {
			LOG(LOG_ERROR, "Can't find dev for %s", dbuf);
				return;
		}
		store_data_dev(dev, DATALOC_DATA, &val_d);
		gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
	} else if (strcasecmp(req->uri, VEN_ALERTS) == 0) {
		LOG(LOG_DEBUG, "Got query/alerts message");

		for (qi=0; qi < 3; qi++) {
			i = jtok_find_token_val_nth_array(token, buf, qi,
							  "alerts", "name",
							  jret);
			if (i == -1)
				continue;
			str = jtok_string(&token[i], buf);
			if (strcmp(str, "Air Filter") == 0) {
				sprintf(dbuf, "%s-filteralarm", macaddr);
			} else if (strcmp(str, "UV Lamp") == 0) {
				sprintf(dbuf, "%s-uvlampalarm", macaddr);
			} else if (strcmp(str, "Service") == 0) {
				sprintf(dbuf, "%s-servicealarm", macaddr);
			} else {
				LOG(LOG_ERROR, "Unknown alert type %s", str);
				free(str);
				continue;
			}
			free(str);
			dev = find_device_byuid(dbuf);
			if (dev == NULL) {
				LOG(LOG_ERROR, "Can't find dev for %s", dbuf);
				continue;
			}
			i = jtok_find_token_val_nth_array(token, buf, qi,
							  "alerts", "active",
							  jret);
			if (i == -1)
				continue;
			val_i = jtok_bool(&token[i], buf);
			store_data_dev(dev, DATALOC_DATA, &val_i);
			gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
		}
	} else if (strcasecmp(req->uri, VEN_CONTROL) == 0 ||
		strcasecmp(req->uri, VEN_SETTINGS) == 0) {
		LOG(LOG_DEBUG, "Got Control/Settings response message");

		/* Is this the right thermostat? */
		i = jtok_find_token_val(token, buf, "success", jret);
		if (i == -1) {
			LOG(LOG_ERROR, "Attempt to change resulted in error");
			i = jtok_find_token_val(token, buf, "reason", jret);
			JSMN_TEST_OR_FAIL(i, "reason");
			str = jtok_string(&token[i], buf);
			LOG(LOG_ERROR, "Reason for error: %s", str);
			free(str);
			goto request_cb_out;
		}
		LOG(LOG_DEBUG, "Change successful");
		/* otherwise it worked, immediately schedule a query */
		secs.tv_sec = 2;
		ev = evtimer_new(base, cb_http_GET, queryinfo_get);
		event_add(ev, &secs);
	}

request_cb_out:
	free(buf);
	return;
}

/**
   \brief Build the venstar devices
   \param commercial  1 for commercial, 0 for residential
*/

void build_devices(int commercial)
{
	char uid[64], name[256], *tname;
	device_t *dev;

	if (macaddr == NULL || macaddr[0] == '\0')
		LOG(LOG_FATAL, "macaddr of device not known.  Perhaps "
		    "discovery failed?");

	tname = cfg_getstr(venstar_c, "name");

	sprintf(uid, "%s-mode", macaddr);
	sprintf(name, "%s - Current thermostat mode", tname);
	generic_build_device(cfg, uid, name, "thermmode", PROTO_SENSOR_VENSTAR,
			     DEVICE_SWITCH, SUBTYPE_THMODE, "mode", 0,
			     gnhastd_conn->bev);

	sprintf(uid, "%s-state", macaddr);
	sprintf(name, "%s - Current thermostat state", tname);
	generic_build_device(cfg, uid, name, "thermstate",
			     PROTO_SENSOR_VENSTAR, DEVICE_SWITCH,
			     SUBTYPE_THSTATE, "state", 0, gnhastd_conn->bev);

	sprintf(uid, "%s-fan", macaddr);
	sprintf(name, "%s - Current fan setting", tname);
	generic_build_device(cfg, uid, name, "fan", PROTO_SENSOR_VENSTAR,
			     DEVICE_SWITCH, SUBTYPE_SWITCH, "fan", 0,
			     gnhastd_conn->bev);

	sprintf(uid, "%s-fanstate", macaddr);
	sprintf(name, "%s - Current fan state", tname);
	generic_build_device(cfg, uid, name, "fanstate", PROTO_SENSOR_VENSTAR,
			     DEVICE_SWITCH, SUBTYPE_SWITCH, "fanstate", 0,
			     gnhastd_conn->bev);

	sprintf(uid, "%s-tempunits", macaddr);
	sprintf(name, "%s - Temperature Units", tname);
	generic_build_device(cfg, uid, name, "tempunits", PROTO_SENSOR_VENSTAR,
			     DEVICE_SWITCH, SUBTYPE_SWITCH, "tempunits", 0,
			     gnhastd_conn->bev);

	sprintf(uid, "%s-schedule", macaddr);
	sprintf(name, "%s - Current schedule state", tname);
	generic_build_device(cfg, uid, name, "schedule", PROTO_SENSOR_VENSTAR,
			     DEVICE_SWITCH, SUBTYPE_SWITCH, "schedule", 0,
			     gnhastd_conn->bev);

	sprintf(uid, "%s-schedulepart", macaddr);
	sprintf(name, "%s - Current schedule part", tname);
	generic_build_device(cfg, uid, name, "schedulepart",
			     PROTO_SENSOR_VENSTAR,
			     DEVICE_SWITCH, SUBTYPE_SMNUMBER, "schedulepart",
			     0, gnhastd_conn->bev);

	/* the funny ordering here is just done to match the documentation */
	if (!commercial) {
		sprintf(uid, "%s-away", macaddr);
		sprintf(name, "%s - Current away state", tname);
		generic_build_device(cfg, uid, name, "away",
				     PROTO_SENSOR_VENSTAR,
				     DEVICE_SWITCH, SUBTYPE_SWITCH, "away",
				     0, gnhastd_conn->bev);
	} else { /* commercial unit */
		sprintf(uid, "%s-holiday", macaddr);
		sprintf(name, "%s - Current holiday state", tname);
		generic_build_device(cfg, uid, name, "holiday",
				     PROTO_SENSOR_VENSTAR, DEVICE_SWITCH,
				     SUBTYPE_SWITCH, "holiday", 0,
				     gnhastd_conn->bev);

		sprintf(uid, "%s-override", macaddr);
		sprintf(name, "%s - Current override state", tname);
		generic_build_device(cfg, uid, name, "override",
				     PROTO_SENSOR_VENSTAR, DEVICE_SWITCH,
				     SUBTYPE_SWITCH, "override", 0,
				     gnhastd_conn->bev);

		sprintf(uid, "%s-overridetime", macaddr);
		sprintf(name, "%s - Time left in override", tname);
		generic_build_device(cfg, uid, name, "overridetime",
				     PROTO_SENSOR_VENSTAR, DEVICE_TIMER,
				     SUBTYPE_TIMER, "overridetime", 0,
				     gnhastd_conn->bev);

		sprintf(uid, "%s-forceunocc", macaddr);
		sprintf(name, "%s - Current Force Unoccupied state", tname);
		generic_build_device(cfg, uid, name, "forceunocc",
				     PROTO_SENSOR_VENSTAR, DEVICE_SWITCH,
				     SUBTYPE_SWITCH, "forceunocc", 0,
				     gnhastd_conn->bev);
	}

	sprintf(uid, "%s-spacetemp", macaddr);
	sprintf(name, "%s - Current space temperature", tname);
	generic_build_device(cfg, uid, name, "spacetemp", PROTO_SENSOR_VENSTAR,
			     DEVICE_SENSOR, SUBTYPE_TEMP, "spacetemp",
			     tempscale, gnhastd_conn->bev);

	sprintf(uid, "%s-heattemp", macaddr);
	sprintf(name, "%s - Current heat to temperature", tname);
	generic_build_device(cfg, uid, name, "heattemp", PROTO_SENSOR_VENSTAR,
			     DEVICE_SENSOR, SUBTYPE_TEMP, "heattemp",
			     tempscale, gnhastd_conn->bev);

	sprintf(uid, "%s-cooltemp", macaddr);
	sprintf(name, "%s - Current cool to temperature", tname);
	generic_build_device(cfg, uid, name, "cooltemp", PROTO_SENSOR_VENSTAR,
			     DEVICE_SENSOR, SUBTYPE_TEMP, "cooltemp",
			     tempscale, gnhastd_conn->bev);

	sprintf(uid, "%s-cooltempmin", macaddr);
	sprintf(name, "%s - Minimum cool to temperature", tname);
	generic_build_device(cfg, uid, name, "cooltempmin", PROTO_SENSOR_VENSTAR,
			     DEVICE_SENSOR, SUBTYPE_TEMP, "cooltempmin",
			     tempscale, gnhastd_conn->bev);

	sprintf(uid, "%s-cooltempmax", macaddr);
	sprintf(name, "%s - Maximum cool to temperature", tname);
	generic_build_device(cfg, uid, name, "cooltempmax",
			     PROTO_SENSOR_VENSTAR,
			     DEVICE_SENSOR, SUBTYPE_TEMP, "cooltempmax",
			     tempscale, gnhastd_conn->bev);

	sprintf(uid, "%s-heattempmin", macaddr);
	sprintf(name, "%s - Minimum heat to temperature", tname);
	generic_build_device(cfg, uid, name, "cooltempmin",
			     PROTO_SENSOR_VENSTAR,
			     DEVICE_SENSOR, SUBTYPE_TEMP, "heattempmin",
			     tempscale, gnhastd_conn->bev);

	sprintf(uid, "%s-heattempmax", macaddr);
	sprintf(name, "%s - Maximum heat to temperature", tname);
	generic_build_device(cfg, uid, name, "heattempmax",
			     PROTO_SENSOR_VENSTAR,
			     DEVICE_SENSOR, SUBTYPE_TEMP, "heattempmax",
			     tempscale, gnhastd_conn->bev);

	sprintf(uid, "%s-setpointdelta", macaddr);
	sprintf(name, "%s - Minimum heat/cool temperature difference", tname);
	generic_build_device(cfg, uid, name, "setpointdelta",
			     PROTO_SENSOR_VENSTAR,
			     DEVICE_SENSOR, SUBTYPE_TEMP, "setpointdelta",
			     tempscale, gnhastd_conn->bev);

	sprintf(uid, "%s-hum", macaddr);
	sprintf(name, "%s - Current Humidity", tname);
	generic_build_device(cfg, uid, name, "hum", PROTO_SENSOR_VENSTAR,
			     DEVICE_SENSOR, SUBTYPE_HUMID, "hum", 0,
			     gnhastd_conn->bev);

	sprintf(uid, "%s-availablemodes", macaddr);
	sprintf(name, "%s - Available Thermostat Modes", tname);
	generic_build_device(cfg, uid, name, "availablemodes",
			     PROTO_SENSOR_VENSTAR,
			     DEVICE_SENSOR, SUBTYPE_SMNUMBER,
			     "availablemodes", 0, gnhastd_conn->bev);

	sprintf(uid, "%s-outdoortemp", macaddr);
	sprintf(name, "%s - Current outdoor temperature", tname);
	generic_build_device(cfg, uid, name, "outdoortemp",
			     PROTO_SENSOR_VENSTAR,
			     DEVICE_SENSOR, SUBTYPE_TEMP, "outdoortemp",
			     tempscale, gnhastd_conn->bev);


	sprintf(uid, "%s-filteralarm", macaddr);
	sprintf(name, "%s - Service Air Filter Alarm", tname);
	generic_build_device(cfg, uid, name, "filteralarm",
			     PROTO_SENSOR_VENSTAR,
			     DEVICE_SENSOR, SUBTYPE_SWITCH, "Air Filter",
			     0, gnhastd_conn->bev);

	sprintf(uid, "%s-uvlampalarm", macaddr);
	sprintf(name, "%s - Service UV Lamp Alarm", tname);
	generic_build_device(cfg, uid, name, "uvlampalarm",
			     PROTO_SENSOR_VENSTAR,
			     DEVICE_SENSOR, SUBTYPE_SWITCH, "UV Lamp",
			     0, gnhastd_conn->bev);

	sprintf(uid, "%s-servicealarm", macaddr);
	sprintf(name, "%s - Service Alarm", tname);
	generic_build_device(cfg, uid, name, "servicealarm",
			     PROTO_SENSOR_VENSTAR,
			     DEVICE_SENSOR, SUBTYPE_SWITCH, "Service",
			     0, gnhastd_conn->bev);

	/* XXX Unsure how to store runtimes just yet.  yesterday only maybe? */

	/* are we dumping the conf file? */
	if (dumpconf != NULL) {
		LOG(LOG_NOTICE, "Dumping config file to "
		    "%s and exiting", dumpconf);
		dump_conf(cfg, 0, dumpconf);
		exit(0);
	}
}

/**
   \brief Callback to start a feed
   \param fd fd
   \param what why did it fire?
   \param arg pointer to http_get_t;
*/

void delay_feedstart_cb(int fd, short what, void *arg)
{
	struct event *evn;
	struct timeval secs = { 0, 0 };
	http_get_t *get = (http_get_t *)arg;

	secs.tv_sec = cfg_getint(venstar_c, "update");
	evn = event_new(base, -1, EV_PERSIST, cb_http_GET, get);
	event_add(evn, &secs);
	LOG(LOG_NOTICE, "Starting %s updates every %d seconds",
	    get->url_suffix, secs.tv_sec);

	/* and do one now */
	cb_http_GET(0, 0, get);
}

/**
   \brief Start the feed
   We have to delay start the feeds, otherwise we could overwhelm the
   Venstar unit.  Therefore this routine does a silly double jump thingie.
*/

void venstar_startfeed(void)
{
	struct event *ev;
	struct timeval secs = { 0, 0 };
	int feed_delay, update;

	build_devices(cfg_getint(venstar_c, "ttype"));

	if (venstar_url == NULL)
		LOG(LOG_FATAL, "Venstar URL is NULL, discovery failed!");

	query_running = 1;

	feed_delay = cfg_getint(venstar_c, "update") / VEN_FEEDS;
	if (feed_delay < 1) {
		update = VEN_FEEDS * 2;
		feed_delay = 2;
		cfg_setint(venstar_c, "update", update);
		LOG(LOG_WARNING, "Update is too fast, adjusting to %d",
		    update);
	}

	queryinfo_get = smalloc(http_get_t);
	queryinfo_get->url_prefix = venstar_url;
	queryinfo_get->url_suffix = VEN_INFO;
	queryinfo_get->cb = request_cb;
	queryinfo_get->http_port = VENSTAR_HTTP_PORT;
	queryinfo_get->precheck = NULL;

	secs.tv_sec = feed_delay;
	ev = evtimer_new(base, delay_feedstart_cb, queryinfo_get);
	event_add(ev, &secs);

	querysensors_get = smalloc(http_get_t);
	querysensors_get->url_prefix = venstar_url;
	querysensors_get->url_suffix = VEN_SENSORS;
	querysensors_get->cb = request_cb;
	querysensors_get->http_port = VENSTAR_HTTP_PORT;
	querysensors_get->precheck = NULL;

	secs.tv_sec = feed_delay * 2;
	ev = evtimer_new(base, delay_feedstart_cb, querysensors_get);
	event_add(ev, &secs);

	queryalerts_get = smalloc(http_get_t);
	queryalerts_get->url_prefix = venstar_url;
	queryalerts_get->url_suffix = VEN_ALERTS;
	queryalerts_get->cb = request_cb;
	queryalerts_get->http_port = VENSTAR_HTTP_PORT;
	queryalerts_get->precheck = NULL;

	secs.tv_sec = feed_delay * 3;
	ev = evtimer_new(base, delay_feedstart_cb, queryalerts_get);
	event_add(ev, &secs);
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
  Venstar discovery stuff
*****/

/**
   \brief Watch for NOTIFY messages
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void cb_notify_read(int fd, short what, void *arg)
{
	char buf[2048], buf2[8], *result;
	char maddr[6][3];
	char *p, *q;
	int len, size, i;
	struct sockaddr_in cli_addr;

	size = sizeof(struct sockaddr);
	bzero(buf, sizeof(buf));
	len = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&cli_addr,
		       &size);

	LOG(LOG_DEBUG, "got %d bytes on udp:%d", len, ssdp_portnum);
	buf[len] = '\0';

	result = find_ssdp_field(buf, "NT:");
	if (result == NULL)
		return;

	if (strcasecmp(result, "colortouch:ecp") != 0) /* not a venstar */
		return;
	free(result);

	result = find_ssdp_field(buf, "USN:");
	if (result == NULL)
		return; /* wtf? */
	p = strstr(result, "name:");
	if (p == NULL) {
		LOG(LOG_ERROR, "Cannot find name in USN: %s", result);
		free(result);
		return;
	}
	p += 5; /* skip name */
	q = strchr(p, ':');
	if (q == NULL) {
		LOG(LOG_ERROR, "Cannot find terminating : in name %s", p);
		free(result);
		return;
	}
	*q = '\0'; /* null terminate */
	if (strcasecmp(p, cfg_getstr(venstar_c, "name")) != 0) {
		/* wrong one */
		free(result);
		return;
	}

	/* if we got this far, we have the correct venstar unit. Now
	   save off some useful data */
	free(result);

	result = find_ssdp_field(buf, "Location:");
	if (result == NULL) {
		LOG(LOG_ERROR, "Couldn't find location field!");
		return;
	}
	if (venstar_url != NULL) {
		if (strcmp(venstar_url, result) != 0) {
			free(venstar_url);
			venstar_url = result;
			LOG(LOG_NOTICE, "Venstar changed location to %s",
			    venstar_url);
		} else
			free(result);
	} else { /* venstar_url is null */
		venstar_url = result;
		LOG(LOG_NOTICE, "Venstar is at: %s", venstar_url);
	}

	result = find_ssdp_field(buf, "Cache-Control:");
	if (result == NULL) {
		LOG(LOG_ERROR, "Couldn't find cache-control, assuming 300");
		goto cbnr_out;
	}
	p = strchr(result, '=');
	if (p == NULL) {
		LOG(LOG_ERROR, "Badly formed cache-control line");
		free(result);
		goto cbnr_out;
	}
	p += 1; /* skip = */
	i = atoi(p);
	if (i != max_age) {
		max_age = i;
		LOG(LOG_NOTICE, "Set max-age to %d", max_age);
	}
	free(result);

	/* the following checks only need be run once */
	if (query_running)
		goto cbnr_out;

	/* Find the macaddr */
	result = find_ssdp_field(buf, "USN:");
	if (result == NULL) {
		LOG(LOG_ERROR, "Couldn't find mac address!");
		goto cbnr_out;
	}
	p = strstr(result, ":name:");
	if (p == NULL) {
		free(result);
		goto cbnr_out;
	}
	*p = '\0';
	result += 4;
	sscanf(result, "%2s:%2s:%2s:%2s:%2s:%2s", maddr[0], maddr[1], maddr[2],
	       maddr[3], maddr[4], maddr[5]);
	sprintf(macaddr, "%s%s%s%s%s%s", maddr[0], maddr[1], maddr[2],
		maddr[3], maddr[4], maddr[5]);
	LOG(LOG_DEBUG, "Macaddr = %s", macaddr);
	free(result);

	/* find the thermostat type */
	result = find_ssdp_field(buf, "USN:");
	if (result == NULL) {
		LOG(LOG_ERROR, "Couldn't find mac address!");
		goto cbnr_out;
	}
	p = strstr(result, ":type:");
	if (p == NULL) {
		free(result);
		goto cbnr_out;
	}
	p += 6; /* :type: */
	if (strcasecmp(p, "residential") == 0)
		cfg_setint(venstar_c, "ttype", TTYPE_RES);
	else if (strcasecmp(p, "commercial") == 0)
		cfg_setint(venstar_c, "ttype", TTYPE_COM);
	else
		LOG(LOG_ERROR, "Unknown themostat type: %s", p);
	LOG(LOG_DEBUG, "Thermostat type %s == %d", p,
	    cfg_getint(venstar_c, "ttype"));
	free(result);

	/* done */

cbnr_out:
	if (query_running)
		return;

	/* otherwise, do initial setup */

	venstar_startfeed();
}

/**
   \brief Perform device discovery (passively via NOTIFY)
 */
void discovery_setup()
{

	/* build discovery event */
	notify_fd = bind_notify_recv();
	if (notify_fd != -1) {
		disc_ev = event_new(base, notify_fd, EV_READ | EV_PERSIST,
				   cb_notify_read, NULL);
		event_add(disc_ev, NULL);
		LOG(LOG_NOTICE, "Waiting for NOTIFY from Venstar");
	} else
		LOG(LOG_FATAL, "Failed to setup discovery event");
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

	if (!debugmode && !dumpconf)
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

	/* First, parse the venstar section */

	if (cfg) {
		venstar_c = cfg_getsec(cfg, "venstarcoll");
		if (!venstar_c)
			LOG(LOG_FATAL, "Error reading config file, venstar section");
	}

	/* Now, parse the details of connecting to the gnhastd server */

	if (cfg) {
		gnhastd_c = cfg_getsec(cfg, "gnhastd");
		if (!gnhastd_c)
			LOG(LOG_FATAL, "Error reading config file, gnhastd section");
	}

	buf = cfg_getstr(venstar_c, "tscale");
	switch (*buf) {
	case 'C':
		tempscale = TSCALE_C;
		break;
	default:
	case 'F':
		tempscale = TSCALE_F;
	}

	/* discover the venstar */
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

	/* go forth and destroy */
	event_base_dispatch(base);

	closelog();
	delete_pidfile();
	return(0);
}
