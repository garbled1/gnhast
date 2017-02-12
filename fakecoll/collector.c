/*
 * Copyright (c) 2013, 2014
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
   \brief An example collector, that generates fake data to test
   \note You may/should use this as a skeleton to create a new collector
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
#include "confuse.h"
#include "collcmd.h"
#include "genconn.h"
#include "gncoll.h"
#include "collector.h"

/* There is a bunch of extraneous stuff in this example that really isn't
   needed to just get the fakecoll working, however, I've built it like a
   normal collector, so it can be easily copied and hacked up.
*/

/* internal stuffs here */
int loopmax = FAKECOLL_LOOPMAX;
int loopcur = 0;
device_t *deva, *devb, *devc;
int lie_about_ok = 0;

/** our logfile */
FILE *logfile;
char *dumpconf = NULL;
char *conffile = SYSCONFDIR "/" FAKECOLL_CONFIG_FILE;;

/* Need the argtable in scope, so we can generate proper commands
   for the server */
extern argtable_t argtable[];

/** The event base */
struct event_base *base;
struct evdns_base *dns_base;

cfg_t *cfg, *gnhastd_c, *fakecoll_c;

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

cfg_opt_t fakecoll_opts[] = {
	CFG_INT("instance", 1, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t options[] = {
	CFG_SEC("gnhastd", gnhastd_opts, CFGF_NONE),
	CFG_SEC("device", device_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_SEC("fakecoll", fakecoll_opts, CFGF_NONE),
	CFG_STR("logfile", FAKECOLL_LOG_FILE, CFGF_NONE),
	CFG_STR("pidfile", FAKECOLL_PID_FILE, CFGF_NONE),
	CFG_END(),
};

void cb_generate_chaff(int fd, short what, void *arg);
void cb_sigterm(int fd, short what, void *arg);
void cb_shutdown(int fd, short what, void *arg);


/*** Collector specific code goes here ***/

/**
   \brief random number generator, between x and y
   \param min smallest number
   \param max largest number
   \return The random number
*/

int rndm(int min, int max)
{
	int diff;

	diff = max - min + 1;
	if (max < 1 || diff < 1)
		return(min);

	return(random()%diff+min);
}

/**
   \brief random number generator, between x and y, float value
   \param min smallest number
   \param max largest number
   \return The random number
*/

float frndm(float min, float max)
{
	float diff;

	diff = max - min + 1.0;
	if (max < 1 || diff < 1)
		return(min);

	return(drand48()*diff+min);
}

/**
   \brief Generate chaff callback so we can test the engine
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void cb_generate_chaff(int fd, short what, void *arg)
{
	int j;
	uint8_t s, ss;
	double d, dd;
	struct timeval secs = { 0, 0 };
	struct event *timer_ev;

	loopcur++;

	j = rndm(0, 2);
	switch(j) {
	case 0:
		s = rndm(0, 1);
		store_data_dev(deva, DATALOC_DATA, &s);
		gn_update_device(deva, 0, gnhastd_conn->bev);
		get_data_dev(deva, DATALOC_LAST, &ss);
		LOG(LOG_NOTICE, "Storing on %s:%d last: %d",
		    deva->name, s, ss);
		break;
	case 1:
		d = frndm(60.0, 110.0);
		store_data_dev(devb, DATALOC_DATA, &d);
		get_data_dev(devb, DATALOC_LAST, &dd);
		gn_update_device(devb, 0, gnhastd_conn->bev);
		LOG(LOG_NOTICE, "Storing on %s:%f last: %f",
		    devb->name, d, dd);
		break;
	case 2:
		d = frndm(0.0, 100.0);
		store_data_dev(devc, DATALOC_DATA, &d);
		get_data_dev(devc, DATALOC_LAST, &dd);
		gn_update_device(devc, 0, gnhastd_conn->bev);
		LOG(LOG_NOTICE, "Storing on %s:%f last: %f",
		    devc->name, d, dd);
		break;
	}

	/* if loopmax is 0 or -1, go forever */
	if (loopcur >= loopmax && loopmax > 1) {
		generic_cb_sigterm(0, 0, NULL);
		return;
	}

	/* otherwise, schedule another one */

	/* sleep for 2-10 seconds */
	secs.tv_sec = rndm(2, 10);

	if (!lie_about_ok) {
		/* schedule ourselves as a timer */
		timer_ev = evtimer_new(base, cb_generate_chaff, NULL);
		evtimer_add(timer_ev, &secs);
	}
}

/**
   \brief Build the fake devices and setup the event loop
*/

void build_chaff_engine(void)
{
	struct timeval secs = { 0, 0 };
	struct event *timer_ev;

	/* Setup 3 fake devices */
	deva = smalloc(device_t);
	devb = smalloc(device_t);
	devc = smalloc(device_t);

	/* switch device */
	deva->name = deva->uid = "Switch";
	deva->proto = PROTO_INSTEON_V2;
	deva->type = DEVICE_SWITCH;
	deva->subtype = SUBTYPE_SWITCH;

	/* temperature sensor */
	devb->name = devb->uid = "TempSensor";
	devb->proto = PROTO_SENSOR_OWFS;
	devb->type = DEVICE_SENSOR;
	devb->subtype = SUBTYPE_TEMP;

	/* dimmer */
	devc->name = devc->uid = "Dimmer";
	devc->proto = PROTO_INSTEON_V1;
	devc->type = DEVICE_DIMMER;
	devc->subtype = SUBTYPE_OUTLET;

	/* tell the server about the three devices */
	gn_register_device(deva, gnhastd_conn->bev);
	gn_register_device(devb, gnhastd_conn->bev);
	gn_register_device(devc, gnhastd_conn->bev);

	/* sleep for 2-10 seconds */
	secs.tv_sec = rndm(2, 10);

	/* schedule a chaff timer */
	timer_ev = evtimer_new(base, cb_generate_chaff, NULL);
	evtimer_add(timer_ev, &secs);
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
	if (lie_about_ok)
		return(0);
	return(1); /* lie */
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
	while ((ch = getopt(argc, argv, "?c:dfl:s:p:")) != -1)
		switch (ch) {
		case 'c':	/* Set configfile */
			conffile = strdup(optarg);
			break;
		case 'd':	/* debugging mode */
			debugmode = 1;
			break;
		case 'f':	/* lie about being OK */
			lie_about_ok = 1;
			break;
		case 'l':
			loopmax = atoi(optarg);
			break;
		case 's':	/* set servername */
			gnhastdserver = strdup(optarg);
			break;
		case 'p':	/* portnum */
			port = atoi(optarg);
			break;
		default:
		case '?':	/* you blew it */
			(void)fprintf(stderr, "usage:\n%s [-c configfile]"
				      "[-l loops] [-s server] [-p port]\n",
				      getprogname());
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
		fakecoll_c = cfg_getsec(cfg, "fakecoll");
		if (!fakecoll_c)
			LOG(LOG_FATAL, "Error reading config file, "
			    "fakecoll section");
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
	collector_instance = cfg_getint(fakecoll_c, "instance");
	gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);

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

	/* spam the server */
	build_chaff_engine();

	/* go forth and destroy */
	event_base_dispatch(base);

	/* Close out the log, and bail */
	closelog();
	delete_pidfile();
	return(0);
}

