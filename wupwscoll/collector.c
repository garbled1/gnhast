/*
 * Copyright (c) 2013
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
   \brief Weather Underground PWS collector
   This collector connects to gnhastd, and updates a PWS with
   data from selected sensors.
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
#include "wupws.h"

FILE *logfile;   /** our logfile */
cfg_t *cfg, *gnhastd_c, *wupws_c;
char *dumpconf = NULL;
int need_rereg = 0;
struct calcdata *cdata;
int dataslots = 0;
 
/* debugging */
//_malloc_options = "AJ";

/** Need the argtable in scope, so we can generate proper commands
    for the server */
extern argtable_t argtable[];
extern TAILQ_HEAD(, _device_t) alldevs;
extern commands_t commands[];
extern int debugmode;

/** The event base */
struct event_base *base;
struct evdns_base *dns_base;
struct evhttp_connection *http_cn = NULL;

/** The connection streams for our connection */
connection_t *gnhastd_conn;

/* Configuration file setup */

extern cfg_opt_t device_opts[];

cfg_opt_t gnhastd_opts[] = {
	CFG_STR("hostname", "127.0.0.1", CFGF_NONE),
	CFG_INT("port", 2920, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t wupwscoll_opts[] = {
	CFG_STR("pwsid", 0, CFGF_NODEFAULT),
	CFG_STR("pwspasswd", 0, CFGF_NODEFAULT),
	CFG_INT("update", 60, CFGF_NONE),
	CFG_INT_CB("rapidfire", 0, CFGF_NONE, conf_parse_bool),
	CFG_INT_CB("pwstype", PWS_WUNDERGROUND, CFGF_NONE, conf_parse_pwstype),
	CFG_END(),
};

cfg_opt_t pwsdev_opts[] = {
	CFG_STR("uid", 0, CFGF_NODEFAULT),
	CFG_INT_CB("subtype", 0, CFGF_NONE, conf_parse_subtype),
	CFG_INT("calculate", 0, CFGF_NONE),
	CFG_INT("accumulate", 0, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t options[] = {
	CFG_SEC("gnhastd", gnhastd_opts, CFGF_NONE),
	CFG_SEC("wupwscoll", wupwscoll_opts, CFGF_NONE),
	CFG_SEC("pwsdev", pwsdev_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_SEC("device", device_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_STR("logfile", WUPWSCOLL_LOG_FILE, CFGF_NONE),
	CFG_STR("pidfile", WUPWSCOLL_PID_FILE, CFGF_NONE),
	CFG_END(),
};

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
	double data;

	get_data_dev(dev, DATALOC_DATA, &data);
	upd_calcdata(dev->uid, data);
}

/*****
  wupws stuff
*****/

/**
   \brief parse a pws weather server type
*/
int conf_parse_pwstype(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		       void *result)
{
	if (strcasecmp(value, "wunderground") == 0)
		*(int *)result = PWS_WUNDERGROUND;
	else if (strcasecmp(value, "weatherunderground") == 0)
		*(int *)result = PWS_WUNDERGROUND;
	else if (strcasecmp(value, "pwsweather") == 0)
		*(int *)result = PWS_PWSWEATHER;
	else if (strcasecmp(value, "debug") == 0)
		*(int *)result = PWS_DEBUG;
	else {
		cfg_error(cfg, "invalid pwstype value for option '%s': %s",
		    cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}

/**
   \brief initialize the calculated data array
*/

void create_calcdata(void)
{
	cfg_t *pwsdev;
	int i, j;
	char *uid;

	for (i = 0, j = 0; i < cfg_size(cfg, "pwsdev"); i++) {
		pwsdev = cfg_getnsec(cfg, "pwsdev", i);
		uid = cfg_getstr(pwsdev, "uid");
		if (uid == NULL)
			continue;
		if (cfg_getint(pwsdev, "calculate") < 1)
			continue;
		j++;
	}
	dataslots = j;
	cdata = safer_malloc(sizeof(struct calcdata) * j);
	for (i = 0, j = 0; i < cfg_size(cfg, "pwsdev"); i++) {
		pwsdev = cfg_getnsec(cfg, "pwsdev", i);
		uid = cfg_getstr(pwsdev, "uid");
		if (uid == NULL)
			continue;
		if (cfg_getint(pwsdev, "calculate") < 1)
			continue;
		cdata[j].nrofdata = cfg_getint(pwsdev, "calculate") /
			cfg_getint(wupws_c, "update");
		cdata[j].pwsdev = strdup(cfg_title(pwsdev));
		cdata[j].uid = strdup(uid);
		cdata[j].data = safer_malloc(sizeof(double) *
					     cdata[j].nrofdata);
		cdata[j].vals = 0;
		j++;
	}
}

/**
   \brief update a calculated data with data
   \param uid uid of device
   \param data date to enter
*/

void upd_calcdata(char *uid, double data)
{
	int i;
	/*int j;*/

	for (i=0; i < dataslots; i++) {
		if (strcmp(cdata[i].uid, uid) == 0) {
			LOG(LOG_DEBUG, "Updating calcdata slot %d with data "
			    "from uid:%s %f", i, uid, data);
			memmove(&cdata[i].data[0], &cdata[i].data[1],
				sizeof(double) * (cdata[i].nrofdata - 1));
			cdata[i].data[cdata[i].nrofdata - 1] = data;
			cdata[i].vals++;
#if 0
			for (j=0; j<cdata[i].nrofdata; j++)
				LOG(LOG_DEBUG, "data field %d %f", j,
				    cdata[i].data[j]);
#endif
		}
	}
}

/**
   \brief get calculated data
   \param pwsdev pwsdev name of calcdata slot
   \param type data type to get CALCDATA_*
   \param cur current data
   \return data as double
*/

double get_calcdata(const char *pwsdev, int type, double cur)
{
	int i, slot;
	double d;

	slot = -1;
	for (i=0; i < dataslots; i++)
		if (strcmp(pwsdev, cdata[i].pwsdev) == 0)
			slot = i;
	if (slot == -1)
		return cur;
	if (cdata[slot].vals < cdata[slot].nrofdata)
		return cur;
	d = 0.0;
	if (type == CALCDATA_AVG) {
		for (i=0; i < cdata[slot].nrofdata; i++)
			d += cdata[slot].data[i];
		LOG(LOG_DEBUG, "get_calcdata: avg: %f cur:%f nrof:%d",
		    d / cdata[slot].nrofdata, cur, cdata[slot].nrofdata);
		return d / (double)cdata[slot].nrofdata;
	} else if (type == CALCDATA_DIFF) {
		LOG(LOG_DEBUG, "get_calcdata: diff: %f cur: %f",
		    cur - cdata[slot].data[0], cur);
		return cur - cdata[slot].data[0];
	}
	return cur;
}

/**
   \brief connect to gnhast and establish feeds for the PWS
*/

void wupws_establish_feeds(void)
{
	cfg_t *pwsdev;
	int i, hb, subtype, scale;
	char *uid;
	struct evbuffer *send;

	hb = cfg_getint(wupws_c, "update");

	for (i = 0; i < cfg_size(cfg, "pwsdev"); i++) {
		pwsdev = cfg_getnsec(cfg, "pwsdev", i);
		uid = cfg_getstr(pwsdev, "uid");
		if (uid == NULL)
			continue;
		if (cfg_getint(pwsdev, "calculate") > 0)
			continue; /* no feed for calculated values */
		subtype = cfg_getint(pwsdev, "subtype");
		scale = 0;
		switch (subtype) {
		case SUBTYPE_TEMP: scale = TSCALE_F; break;
		case SUBTYPE_PRESSURE: scale = BAROSCALE_IN; break;
		case SUBTYPE_SPEED: scale = SPEED_MPH; break;
		case SUBTYPE_RAINRATE: scale = LENGTH_IN; break;
		case SUBTYPE_LUX: scale = LIGHT_WM2; break;
		}

		/* schedule a feed with the server */
		send = evbuffer_new();
		evbuffer_add_printf(send, "ldevs %s:%s\n", ARGNM(SC_UID), uid);
		if (scale) {
			evbuffer_add_printf(send, "feed %s:%s %s:%d %s:%d\n",
					    ARGNM(SC_UID), uid,
					    ARGNM(SC_RATE), hb,
					    ARGNM(SC_SCALE), scale);
			evbuffer_add_printf(send, "ask %s:%s %s:%d\n",
					    ARGNM(SC_UID), uid,
					    ARGNM(SC_SCALE), scale);
		} else {
			evbuffer_add_printf(send, "feed %s:%s %s:%d\n",
					    ARGNM(SC_UID), uid,
					    ARGNM(SC_RATE), hb);
			evbuffer_add_printf(send, "ask %s:%s\n",
					    ARGNM(SC_UID), uid);
		}
		bufferevent_write_buffer(gnhastd_conn->bev, send);
		evbuffer_free(send);
	}
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
	char *buf, *result1, *result2;

	switch (req->response_code) {
	case HTTP_OK: break;
	default:
		LOG(LOG_ERROR, "Http request failure: %d", req->response_code);
		break;
	}
	data = evhttp_request_get_input_buffer(req);
	len = evbuffer_get_length(data);
	LOG(LOG_DEBUG, "input buf len= %d", len);
	buf = evbuffer_pullup(data, len);
	result1 = result2 = NULL;
	if (buf) {
		LOG(LOG_DEBUG, "input buf: %s", buf);
		result1 = strcasestr(buf, "success");
		result2 = strcasestr(buf, "logged");
	}
	if (result1 == NULL && result2 == NULL) {
		LOG(LOG_ERROR, "Data not sent! Check password/ID");
		if (buf)
			LOG(LOG_ERROR, "Message from server follows:\n%s", buf);
	}
	evbuffer_drain(data, len); /* toss it */
}

/**
   \brief Callback to connect to weather underground and send data
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void wupws_connect(int fd, short what, void *arg)
{
	struct evhttp_uri *uri;
	struct evhttp_request *req;
	int i;
	char *query, *uid, buf[256];
	struct tm *utc;
	time_t rawtime;
	cfg_t *pwsdev;
	device_t *dev;
	double data;

	//uri = evhttp_uri_new();

	switch (cfg_getint(wupws_c, "pwstype")) {
	case PWS_WUNDERGROUND:
		if (cfg_getint(wupws_c, "rapidfire") == 1)
			uri = evhttp_uri_parse(WUPWS_RAPID);
		else
			uri = evhttp_uri_parse(WUPWS_URL);
		break;
	case PWS_PWSWEATHER:
		uri = evhttp_uri_parse(WUPWS_PWS);
		break;
	case PWS_DEBUG: /* no breaking people's stuff */
	default:
		if (cfg_getint(wupws_c, "rapidfire") == 1)
			uri = evhttp_uri_parse(WUPWS_DEBUG2);
		else
			uri = evhttp_uri_parse(WUPWS_DEBUG);
		break;
	}

	if (uri == NULL) {
		LOG(LOG_ERROR, "Failed to parse URL");
		return;
	}

	query = safer_malloc(4096); /* alloc a big buffer */

	sprintf(query, "%sID=%s&PASSWORD=%s&dateutc=",
		cfg_getint(wupws_c, "pwstype") == PWS_WUNDERGROUND ?
		"action=updateraw&" : "", cfg_getstr(wupws_c, "pwsid"),
		cfg_getstr(wupws_c, "pwspasswd"));

	time(&rawtime);
	utc = gmtime(&rawtime);
	strftime(buf, 256, "%F+%H%%3A%M%%3A%S", utc);
	strcat(query, buf);
	for (i = 0; i < cfg_size(cfg, "pwsdev"); i++) {
		pwsdev = cfg_getnsec(cfg, "pwsdev", i);
		uid = cfg_getstr(pwsdev, "uid");
		if (uid == NULL)
			continue;
		dev = find_device_byuid(uid);
		if (dev == NULL)
			continue;
		/* we rely on the server doing conversion for us */
		get_data_dev(dev, DATALOC_DATA, &data);
		if (cfg_getint(pwsdev, "calculate") > 0)
			if (cfg_getint(pwsdev, "accumulate"))
				data = get_calcdata(cfg_title(pwsdev),
						    CALCDATA_DIFF, data);
			else
				data = get_calcdata(cfg_title(pwsdev),
						    CALCDATA_AVG, data);
		sprintf(buf, "&%s=%0.3f", cfg_title(pwsdev), data);
		strcat(query, buf);
	}
	sprintf(buf, "&softwaretype=gnhast/%s", VERSION);
	strcat(query, buf);
	if (cfg_getint(wupws_c, "pwstype") == PWS_PWSWEATHER) {
		sprintf(buf, "&action=updateraw");
		strcat(query, buf);
		if (cfg_getint(wupws_c, "rapidfire") == 1) {
			sprintf(buf, "&realtime=1&rtfreq=%d",
				cfg_getint(wupws_c, "update"));
			strcat(query, buf);
		}
	}
	LOG(LOG_DEBUG, "Generated PWS String:\n%s", query);
	if (evhttp_uri_set_query(uri, query) != 0)
		LOG(LOG_ERROR, "Bad query string: %s", query);

	evhttp_uri_set_port(uri, 80);

	LOG(LOG_DEBUG, "host: %s port: %d", evhttp_uri_get_host(uri),
	    evhttp_uri_get_port(uri));

	if (http_cn == NULL)
		http_cn = evhttp_connection_base_new(base, dns_base,
						     evhttp_uri_get_host(uri),
						     evhttp_uri_get_port(uri));
	
	req = evhttp_request_new(request_cb, NULL);
	evhttp_make_request(http_cn, req, EVHTTP_REQ_GET,
			    evhttp_uri_get_query(uri));
	evhttp_add_header(req->output_headers, "Host",
			  evhttp_uri_get_host(uri));
	free(query);
	evhttp_uri_free(uri);
}

/**
   \brief Callback to start the feed
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void wupws_startfeed(int fd, short what, void *arg)
{
	struct event *ev;
	struct timeval secs = { 0, 0 };

	/* We need to slightly offset the update callback, so the feeds
	   arrive just before it fires */

	secs.tv_sec = cfg_getint(wupws_c, "update");
	ev = event_new(base, -1, EV_PERSIST, wupws_connect, NULL);
	event_add(ev, &secs);
	LOG(LOG_NOTICE, "Starting feed timer updates every %d seconds",
	    secs.tv_sec);
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
	    conn->server, conn->host, conn->port);

	if (need_rereg) {
		wupws_establish_feeds(); /* ask for feeds */
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
		LOG(LOG_NOTICE, "Connected to %s", conn->server);
	} else if (what & (BEV_EVENT_ERROR|BEV_EVENT_EOF)) {
		if (what & BEV_EVENT_ERROR) {
			err = bufferevent_socket_get_dns_error(ev);
			if (err)
				LOG(LOG_FATAL,
				    "DNS Failure connecting to %s: %s",
				    conn->server, strerror(err));
		}
		LOG(LOG_NOTICE, "Lost connection to %s, closing", conn->server);
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
		//if (dumpconf == NULL && dev->name != NULL)
		//	gn_register_device(dev, gnhastd_conn->bev);
	}
}

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
	int ch;
	char *conffile = SYSCONFDIR "/" WUPWSCOLL_CONFIG_FILE;
	struct event *ev;
	struct timeval secs = { 1, 0 };

	/* process command line arguments */
	while ((ch = getopt(argc, argv, "?c:dm:s")) != -1)
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
	init_commands();

	/* Initialize the device table */
	init_devtable(cfg, 0);
	need_rereg = 1;

	cfg = parse_conf(conffile);

	if (!debugmode && cfg_getstr(cfg, "logfile") != NULL)
		logfile = openlog(cfg_getstr(cfg, "logfile"));

	writepidfile(cfg_getstr(cfg, "pidfile"));

	/* Now, parse the details of connecting to the gnhastd server */

	if (cfg) {
		gnhastd_c = cfg_getsec(cfg, "gnhastd");
		if (!gnhastd_c)
			LOG(LOG_FATAL, "Error reading config file, gnhastd section");
	}
	gnhastd_conn = smalloc(connection_t);
	gnhastd_conn->port = cfg_getint(gnhastd_c, "port");
	gnhastd_conn->host = cfg_getstr(gnhastd_c, "hostname");
	gnhastd_conn->server = strdup("gnhastd");

	wupws_c = cfg_getsec(cfg, "wupwscoll");
	if (cfg_getstr(wupws_c, "pwsid") == NULL ||
	    cfg_getstr(wupws_c, "pwspasswd") == NULL)
		LOG(LOG_FATAL, "pwsid or pwspasswd not set in conf file");

	/* flip to rapid fire if update is less than 60 seconds */
	if (cfg_getint(wupws_c, "pwstype") == PWS_WUNDERGROUND &&
	    cfg_getint(wupws_c, "update") < 60)
		cfg_setint(wupws_c, "rapidfire", 1);

	create_calcdata();
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

	/* cheat, and directly call the timer callback
	   This sets up a connection to the server. */
	connect_server_cb(0, 0, gnhastd_conn);
	gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);

	ev = evtimer_new(base, wupws_startfeed, NULL);
	evtimer_add(ev, &secs);

	/* go forth and destroy */
	event_base_dispatch(base);

	closelog();
	cfg_free(cfg);
	evdns_base_free(dns_base, 0);
	event_base_free(base);
	return(0);
}
