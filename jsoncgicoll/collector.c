/*
 * Copyright (c) 2013, 2014, 2016
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
   \brief A cgi backend that generates json data of device status
*/

#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <time.h>
#include <signal.h>
#include <event2/dns.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>

#include "common.h"
#include "gnhast.h"
#include "confparser.h"
#include "confuse.h"
#include "collcmd.h"
#include "genconn.h"
#include "gncoll.h"
#include "collector.h"

/* internal stuffs here */


/** our logfile */
FILE *logfile;
char *dumpconf = NULL;
char *conffile = SYSCONFDIR "/" JSONCGICOLL_CONFIG_FILE;

/* Need the argtable in scope, so we can generate proper commands
   for the server */
extern argtable_t argtable[];
extern TAILQ_HEAD(, _device_t) alldevs;
extern TAILQ_HEAD(, _alarm_t) alarms;

/** The event base */
struct event_base *base;
struct evdns_base *dns_base;

cfg_t *cfg, *gnhastd_c, *jsoncgicoll_c;

#define CONN_TYPE_GNHASTD       1
char *conntype[3] = {
        "none",
        "gnhastd",
};
connection_t *gnhastd_conn;
int need_rereg = 0;
extern int debugmode;
extern int collector_instance;

/* Example options setup */

extern cfg_opt_t device_opts[];

cfg_opt_t gnhastd_opts[] = {
	CFG_STR("hostname", "127.0.0.1", CFGF_NONE),
	CFG_INT("port", 2920, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t jsoncgicoll_opts[] = {
	CFG_INT("instance", 1, CFGF_NONE),
	CFG_INT("update", 60, CFGF_NONE),
	CFG_INT_CB("tscale", TSCALE_F, CFGF_NONE, conf_parse_tscale),
	CFG_INT_CB("baroscale", BAROSCALE_MB, CFGF_NONE, conf_parse_baroscale),
	CFG_INT_CB("lengthscale", LENGTH_IN, CFGF_NONE, conf_parse_lscale),
	CFG_INT_CB("speedscale", SPEED_MPH, CFGF_NONE, conf_parse_speedscale),
	CFG_INT_CB("lightscale", LIGHT_LUX, CFGF_NONE, conf_parse_lightscale),
	CFG_END(),
};

cfg_opt_t options[] = {
	CFG_SEC("gnhastd", gnhastd_opts, CFGF_NONE),
	CFG_SEC("device", device_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_SEC("jsoncgicoll", jsoncgicoll_opts, CFGF_NONE),
	CFG_STR("logfile", JSONCGICOLL_LOG_FILE, CFGF_NONE),
	CFG_STR("pidfile", JSONCGICOLL_PID_FILE, CFGF_NONE),
	CFG_END(),
};

void cb_sigterm(int fd, short what, void *arg);
void cb_shutdown(int fd, short what, void *arg);
void jsoncoll_establish_feeds(void);
void json_dump_all(int fd, short what, void *arg);

/*****
      Callbacks
*****/

/**
   \brief Called when a connection event occurs (stub)
   \param cevent CEVENT saying what happened
   \param conn connection_t that something occurred on
*/

void genconn_connect_cb(int cevent, connection_t *conn)
{
	if (cevent == CEVENT_CONNECTED)
		jsoncoll_establish_feeds();
}


/**
   \brief Called when an upd command occurs
   \param dev device that got updated
   \param arg pointer to client_t
   \note this handles the cfeed devices
*/

void coll_upd_cb(device_t *dev, void *arg)
{
	char *buf;

	if (dev->type == DEVICE_SWITCH || dev->type == DEVICE_DIMMER ||
	    dev->subtype == SUBTYPE_SWITCH) {
		printf("data: [ ");
		printf("{\"uid\" : \"%s\", ", dev->uid);
		printf("\"type\" : \"%d\", ", dev->type);
		printf("\"subt\" : \"%d\", ", dev->subtype);
		buf = print_data_dev(dev, DATALOC_DATA);
		if (buf == NULL)
			printf("\"value\" : \"\" } ");
		else {
			printf("\"value\" : \"%s\" } ", buf);
			free(buf);
		}
		printf(" ]\n\n");
		fflush(stdout);
	}
}

/**
   \brief Called when an alarm command occurs
   \param alarm alarm that got updated
   \param aluid alarm UID
   \param arg pointer to client_t
*/

void coll_alarm_cb(alarm_t *alarm, char *aluid, void *arg)
{
	if (alarm == NULL) {
		/* we got a clearing event */
		printf("data: [ ");
		printf("{\"aluid\" : \"%s\", ", aluid);
		printf("\"altext\" : \"\", \"alsev\" : \"0\", ");
		printf("\"alchan\" : \"%u\" } ]\n\n", ALL_FLAGS_SET);
		fflush(stdout);
		return;
	}
	printf("data: [ ");
	printf("{\"aluid\" : \"%s\", ", alarm->aluid);
	printf("\"altext\" : \"%s\", ", alarm->altext);
	printf("\"alsev\" : \"%d\", ", alarm->alsev);
	printf("\"alchan\" : \"%u\" } ]\n\n", alarm->alchan);
	fflush(stdout);
}

/**
   \brief Handle a endldevs device command
   \param args The list of arguments
   \param arg void pointer to client_t of provider
   \note now we want to establish feeds for everything
*/

int cmd_endldevs(pargs_t *args, void *arg)
{
        device_t *dev;
	int update, scale;
	struct evbuffer *send;
	struct event *ev;
	struct timeval secs = { 1, 0 };

	update = cfg_getint(jsoncgicoll_c, "update");

	TAILQ_FOREACH(dev, &alldevs, next_all) {
		scale = 0;
		switch (dev->subtype) {
		case SUBTYPE_TEMP:
			scale = cfg_getint(jsoncgicoll_c, "tscale");
			break;
		case SUBTYPE_PRESSURE:
			scale = cfg_getint(jsoncgicoll_c, "baroscale");
			break;
		case SUBTYPE_SPEED:
			scale = cfg_getint(jsoncgicoll_c, "speedscale");
			break;
		case SUBTYPE_RAINRATE:
			scale = cfg_getint(jsoncgicoll_c, "lengthscale");
			break;
		case SUBTYPE_LUX:
			scale = cfg_getint(jsoncgicoll_c, "lightscale");
			break;
		}
		/* schedule a feed with the server */
		send = evbuffer_new();

		if ((dev->type == DEVICE_SWITCH || dev->type == DEVICE_DIMMER
		     || dev->subtype == SUBTYPE_SWITCH) &&
		    dev->subtype != SUBTYPE_COLLECTOR) {
			/* do a cfeed instead */
			evbuffer_add_printf(send, "cfeed %s:%s\n",
					    ARGNM(SC_UID), dev->uid);
			evbuffer_add_printf(send, "ask %s:%s\n",
					    ARGNM(SC_UID), dev->uid);
		} else {
			if (scale) {
				evbuffer_add_printf(send,
						    "feed %s:%s %s:%d %s:%d\n",
						    ARGNM(SC_UID), dev->uid,
						    ARGNM(SC_RATE), update,
						    ARGNM(SC_SCALE), scale);
				evbuffer_add_printf(send, "ask %s:%s %s:%d\n",
						    ARGNM(SC_UID), dev->uid,
						    ARGNM(SC_SCALE), scale);
			} else {
				evbuffer_add_printf(send, "feed %s:%s %s:%d\n",
						    ARGNM(SC_UID), dev->uid,
						    ARGNM(SC_RATE), update);
				evbuffer_add_printf(send, "ask %s:%s\n",
						    ARGNM(SC_UID), dev->uid);
			}
		}
		bufferevent_write_buffer(gnhastd_conn->bev, send);
		evbuffer_free(send);
	}
	secs.tv_sec = 2; /* do one right away */
	ev = evtimer_new(base, json_dump_all, NULL);
	evtimer_add(ev, &secs);

	/* start the repeating json spew timer */
	secs.tv_sec = cfg_getint(jsoncgicoll_c, "update");
	ev = event_new(base, -1, EV_PERSIST, json_dump_all, NULL);
	evtimer_add(ev, &secs);
}


/*** Collector specific code goes here ***/

/**
   \brief Ask for list of all devices, connect to alarms
*/

void jsoncoll_establish_feeds(void)
{
	struct evbuffer *send;

	send = evbuffer_new();
	evbuffer_add_printf(send, "ldevs\n");
	evbuffer_add_printf(send, "listenalarms alsev:1 alchan:%u\n",
			    ALL_FLAGS_SET);
	evbuffer_add_printf(send, "dumpalarms\n");
	bufferevent_write_buffer(gnhastd_conn->bev, send);
	evbuffer_free(send);
}


/**
   \brief Dump everything as JSON data
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void json_dump_all(int fd, short what, void *arg)
{
	device_t *dev;
	char *buf;
	int count=0;

	printf("data: [\n");
	TAILQ_FOREACH(dev, &alldevs, next_all) {
		if (count != 0)
			printf("data: , {");
		else
			printf("data: {");
		printf("\"uid\" : \"%s\", ", dev->uid);
		printf("\"type\" : \"%d\", ", dev->type);
		printf("\"subt\" : \"%d\", ", dev->subtype);
		buf = print_data_dev(dev, DATALOC_DATA);
		if (buf == NULL)
			printf("\"value\" : \"\" }\n");
		else {
			printf("\"value\" : \"%s\" }\n", buf);
			free(buf);
		}
		count++;
	}
	printf("data: ]\n\n");
	fflush(stdout);
}

/* Gnhastd connection type routines go here */

/**
   \brief Check if a collector is functioning properly
   \param conn connection_t of collector's gnhastd connection
   \return 1 if OK, 0 if broken
   Generally you set this up with some kind of last update check using
   time(2).  Compare it against the normal update rate, and if there haven't
   been updates in say, 4-6 cycles, return 0.
*/

int collector_is_ok(void)
{
	return(1); /* we never want a restart */
}

/**
   \brief Main itself
   \param argc count
   \param arvg vector
   \return int
*/

int main(int argc, char **argv)
{
	struct event *ev;
	extern char *optarg;
	extern int optind;
	int ch, port = -1;
	char *gnhastdserver = NULL;

	/* process command line arguments */
	while ((ch = getopt(argc, argv, "?c:d")) != -1)
		switch (ch) {
		case 'c':	/* Set configfile */
			conffile = strdup(optarg);
			break;
		case 'd':	/* debugging mode */
			debugmode = 1;
			break;
		default:
		case '?':	/* you blew it */
			(void)fprintf(stderr, "usage:\n%s [-c configfile]\n",
				      getprogname());
			return(EXIT_FAILURE);
			/*NOTREACHED*/
			break;
		}

	/* Initialize the event system */
	base = event_base_new();
	dns_base = evdns_base_new(base, 1);

	/* Initialize the argtable */
	init_argcomm();
	/* Initialize the command table */
	init_commands();
	/* Initialize the device table */
	init_devtable(cfg, 0);

	cfg = parse_conf(conffile);

	if (!debugmode)
		logfile = openlog(cfg_getstr(cfg, "logfile"));

	writepidfile(cfg_getstr(cfg, "pidfile"));

	/* Now, parse the details of connecting to the gnhastd server */

	if (cfg) {
		gnhastd_c = cfg_getsec(cfg, "gnhastd");
		if (!gnhastd_c)
			LOG(LOG_FATAL, "Error reading config file, "
			    "gnhastd section");
	}

	/* Now parse the collector specific config settings */
	if (cfg) {
		jsoncgicoll_c = cfg_getsec(cfg, "jsoncgicoll");
		if (!jsoncgicoll_c)
			LOG(LOG_FATAL, "Error reading config file, "
			    "jsoncgicoll section");
	}

	gnhastd_conn = smalloc(connection_t);
	gnhastd_conn->cname = strdup(COLLECTOR_NAME);
	if (port != -1)
		gnhastd_conn->port = port;
	else
		gnhastd_conn->port = cfg_getint(gnhastd_c, "port");
	if (gnhastdserver != NULL)
		gnhastd_conn->host = gnhastdserver;
	else
		gnhastd_conn->host = cfg_getstr(gnhastd_c, "hostname");
	gnhastd_conn->type = CONN_TYPE_GNHASTD;
	/* cheat, and directly call the timer callback
	   This sets up a connection to the server. */
	generic_connect_server_cb(0, 0, gnhastd_conn);
	collector_instance = getpid(); /* we need a unique instance */
	//gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);

	/* setup signal handlers */

	/* these two are to catch when the user reloads or goes away */
	ev = evsignal_new(base, SIGHUP, generic_cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGPIPE, generic_cb_sigterm, NULL);
	event_add(ev, NULL);

	ev = evsignal_new(base, SIGTERM, generic_cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGINT, generic_cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGQUIT, generic_cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGUSR1, cb_sigusr1, NULL);
	event_add(ev, NULL);

	/* print the opening header */
	printf("Content-type: text/event-stream\n");
	printf("Cache-Control: no-cache\n\n");


	/* go forth and destroy */
	event_base_dispatch(base);

	/* Close out the log, and bail */
	closelog();
	delete_pidfile();
	return(0);
}

