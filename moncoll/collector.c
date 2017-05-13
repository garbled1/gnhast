/*
 * Copyright (c) 2013, 2014, 2015
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
   \file moncoll/collector.c
   \author Tim Rightnour
   \brief A collector to monitor and restart other collectors
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
#include "confparser.h"
#include "collector.h"

/* internal stuffs here */

#define FEED_RATE	5 /* 5 seconds should be good */
#define PID_RATE	60 /* reread pid files every 60 seconds */
#define RESTART_RATE	10 /* 10 seconds for restart runs */
int restart_event_running = 0;
uint32_t alchan = 0; /* alarm channel (set in main) */
extern int errno;

/** our logfile */
FILE *logfile;
char *dumpconf = NULL;
char *conffile = SYSCONFDIR "/" MONCOLL_CONFIG_FILE;;

/* Need the argtable in scope, so we can generate proper commands
   for the server */
extern argtable_t argtable[];

/** The event base */
struct event_base *base;
struct evdns_base *dns_base;

cfg_t *cfg, *gnhastd_c, *moncoll_c;

#define CONN_TYPE_GNHASTD       1
char *conntype[3] = {
        "none",
        "gnhastd",
};
connection_t *gnhastd_conn;
int need_rereg = 0;

struct _moncol_t;
typedef struct _moncol_t {
	char *name;
	char *uid;
	char *path;
	char *args;
	char *pidfile;
	pid_t pid;
	int instance;
	int alive;
	int kill_attempts;
	int passes;
	int restarts;
	TAILQ_ENTRY(_moncol_t) next;
} moncol_t;

TAILQ_HEAD(, _moncol_t) collectors = TAILQ_HEAD_INITIALIZER(collectors);

extern int debugmode;
extern int collector_instance;

/* Options setup */

extern cfg_opt_t device_opts[];

cfg_opt_t gnhastd_opts[] = {
	CFG_STR("hostname", "127.0.0.1", CFGF_NONE),
	CFG_INT("port", 2920, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t monitored_coll[] = {
	CFG_STR("name", "", CFGF_NODEFAULT),
	CFG_STR("uid", "", CFGF_NODEFAULT),
	CFG_STR("coll_path", "", CFGF_NODEFAULT),
	CFG_STR("coll_args", "", CFGF_NODEFAULT),
	CFG_STR("pidfile", "", CFGF_NODEFAULT),
	CFG_INT("instance", 1, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t moncoll_opts[] = {
	CFG_INT("instance", 1, CFGF_NONE),
	CFG_SEC("monitored", monitored_coll, CFGF_MULTI | CFGF_TITLE),
	CFG_END(),
};

cfg_opt_t options[] = {
	CFG_SEC("gnhastd", gnhastd_opts, CFGF_NONE),
	CFG_SEC("device", device_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_SEC("moncoll", moncoll_opts, CFGF_NONE),
	CFG_STR("logfile", MONCOLL_LOG_FILE, CFGF_NONE),
	CFG_STR("pidfile", MONCOLL_PID_FILE, CFGF_NONE),
	CFG_END(),
};


void cb_checkpids(int fd, short what, void *arg);

/*** Collector specific code goes here ***/

/**
   \brief Called when an upd command occurs
   \param dev device that got updated
   \param arg pointer to client_t
*/

void coll_upd_cb(device_t *dev, void *arg)
{
	uint8_t data;
	int found = 0;
	moncol_t *mon;

	get_data_dev(dev, DATALOC_DATA, &data);

	/* this is lame, but how many will we really be monitoring? */
	TAILQ_FOREACH(mon, &collectors, next) {
		if (strcmp(dev->uid, mon->uid) == 0) {
			found = 1;
			mon->alive = data;
			if (!data)
				LOG(LOG_NOTICE, "Gnhastd says collector "
				    "%s is dead!", mon->name);
			break;
		}
	}

	if (!found)
		LOG(LOG_ERROR, "Got data for unmonitored collector %s",
		    dev->uid);
}

/**
   \brief Parse the config file for monitored collectors
   \param cfg moncoll_opts section of cfg structure
   Builds the table of collectors to be monitored, and establishes a feed
   with gnhastd for each.
*/

void setup_monitors(cfg_t *cfg)
{
	cfg_t *mconf;
	moncol_t *mon;
	int i;
	struct evbuffer *send;
	char cbuf[512];

	LOG(LOG_NOTICE, "Starting monitor setup");
	for (i = 0; i < cfg_size(cfg, "monitored"); i++) {
		mconf = cfg_getnsec(cfg, "monitored", i);
		mon = smalloc(moncol_t);
		mon->name = cfg_getstr(mconf, "name");
		mon->uid = cfg_getstr(mconf, "uid");
		mon->path = cfg_getstr(mconf, "coll_path");
		mon->args = cfg_getstr(mconf, "coll_args");
		mon->pidfile = cfg_getstr(mconf, "pidfile");
		mon->pid = -1;
		mon->alive = -1;
		mon->kill_attempts = 0;
		mon->passes = 0;
		mon->restarts = 0;
		mon->instance = cfg_getint(mconf, "instance");
		TAILQ_INSERT_TAIL(&collectors, mon, next);

		send = evbuffer_new();
		evbuffer_add_printf(send, "ldevs %s:%s\n",
				    ARGNM(SC_UID), mon->uid);
		evbuffer_add_printf(send, "feed %s:%s %s:%d\n",
				    ARGNM(SC_UID), mon->uid,
				    ARGNM(SC_RATE), FEED_RATE);
		bufferevent_write_buffer(gnhastd_conn->bev, send);
		evbuffer_free(send);
		/* send a clearing event for monitored collectors */
		snprintf(cbuf, 512, "MONC-%d_%s-%d",
			 collector_instance, mon->name, mon->instance);
		gn_setalarm(gnhastd_conn->bev, cbuf, "nada", 0, alchan);
		LOG(LOG_NOTICE, "Setup monitoring for %s", mon->name);
	}
}

/**
   \brief Callback to kill or restart collectors
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void cb_restart_collectors(int fd, short what, void *arg)
{
	moncol_t *mon;
	struct event *ev;
	struct timeval secs = { 0, 0 };
	char cbuf[512], dbuf[512];

	TAILQ_FOREACH(mon, &collectors, next) {
		mon->passes++;
		LOG(LOG_DEBUG, "%s pass:%d restarts:%d", mon->name,
		    mon->passes, mon->restarts);
		if (mon->alive) {
			if (mon->passes > 60) {
				mon->passes = 0;
				mon->restarts = 0;
			}
			if (mon->passes > 10 && mon->restarts == 0) {/*unset */
				snprintf(cbuf, 512, "MONC-%d_%s-%d",
					 collector_instance, mon->name,
					 mon->instance);
				gn_setalarm(gnhastd_conn->bev, cbuf, "nada",
					    0, alchan);
			}
			continue;
		}
		/* If not alive, try to kill it */
		if (mon->pid < 1 || (
			    kill(mon->pid, SIGTERM) && errno == ESRCH)) {
			mon->restarts++;
			/* no such pid, restart it */
			mon->kill_attempts = 0;
			LOG(LOG_NOTICE, "Restarting down collector %s",
			    mon->name);
			if (mon->args == NULL || strlen(mon->args) < 1)
				snprintf(cbuf, 512, "%s", mon->path);
			else
				snprintf(cbuf, 512, "%s %s", mon->path,
					 mon->args);
			if (system(cbuf)) {
				LOG(LOG_ERROR, "Couldn't restart collector %s"
				    " with cmd %s", mon->name, cbuf);
				LOG(LOG_ERROR, "Errno %d: %s", errno,
				    strerror(errno));
			} else {
				/* it's up, schedule a reread of the pidfile */
				secs.tv_sec = 5; /* wait 5 seconds for start */
				ev = evtimer_new(base, cb_checkpids, NULL);
				evtimer_add(ev, &secs);
				mon->alive = 1; /* for now */
			}
		} else { /* sent it a kill, hopefully this works */
			LOG(LOG_NOTICE, "Killed collector %s", mon->name);
			/* on the next pass, we will automatically attempt
			   a restart, because the pid will be gone */
			mon->kill_attempts++;
			if (mon->kill_attempts > 5) {
				LOG(LOG_NOTICE, "Sending SIGKILL to %s",
				    mon->name);
				kill(mon->pid, SIGKILL);
			}
		}
		if (mon->passes > 10) {
			snprintf(cbuf, 512, "MONC-%d_%s-%d",
				 collector_instance, mon->name, mon->instance);
			snprintf(dbuf, 512, "Collector %s is restarting too "
				"fast", mon->uid);
			if (mon->restarts >= 5)
				gn_setalarm(gnhastd_conn->bev, cbuf, dbuf,
					    mon->restarts*5, alchan);
		}
	}
}

/**
   \brief Callback to read all the pids out of pidfiles and check them
   \param fd unused
   \param what what happened?
   \param arg unused
   We start the restart event in here, so the pids have been read once.
*/

void cb_checkpids(int fd, short what, void *arg)
{
	moncol_t *mon;
	FILE *f;
	char p[128];
	size_t s;
	struct timeval secs = { 0, 0 };
	struct event *ev;

	TAILQ_FOREACH(mon, &collectors, next) {
		f = fopen(mon->pidfile, "r");
		if (f == NULL) {
			LOG(LOG_ERROR, "Pidfile %s for collector %s not found",
			    mon->pidfile, mon->name);
			continue;
		}
		s = fread(p, 1, 127, f);
		p[s] = '\0';
		LOG(LOG_DEBUG, "PID for %s is %s", mon->name, p);
		mon->pid = atol(p);
		fclose(f);
		if (kill(mon->pid, 0) && errno == ESRCH) {
			mon->alive = 0;
			LOG(LOG_NOTICE, "Pid %ld for %s not running",
			    mon->pid, mon->name);
		}
	}

	if (restart_event_running)
		return;

	LOG(LOG_NOTICE, "Starting restart handler");
	secs.tv_sec = RESTART_RATE;
	ev = event_new(base, -1, EV_PERSIST, cb_restart_collectors, NULL);
	event_add(ev, &secs);
	restart_event_running = 1;
}

/* Gnhastd connection type routines go here */

/**
   \brief Check if a collector is functioning properly
   \param conn connection_t of collector's gnhastd connection
   \return 1 if OK, 0 if broken
   \note Who watches the watchmen? Nobody.
*/

int collector_is_ok(void)
{
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
	struct timeval secs = { 0, 0 };

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

	if (!debugmode)
		if (daemon(0, 0) == -1)
			LOG(LOG_FATAL, "Failed to daemonize: %s",
			    strerror(errno));

	SET_FLAG(alchan, ACHAN_GNHAST);

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
		moncoll_c = cfg_getsec(cfg, "moncoll");
		if (!moncoll_c)
			LOG(LOG_FATAL, "Error reading config file, "
			    "moncoll section");
	}

	gnhastd_conn = smalloc(connection_t);
	gnhastd_conn->cname = strdup(COLLECTOR_NAME);
	gnhastd_conn->port = cfg_getint(gnhastd_c, "port");
	gnhastd_conn->host = cfg_getstr(gnhastd_c, "hostname");
	gnhastd_conn->type = CONN_TYPE_GNHASTD;
	/* cheat, and directly call the timer callback
	   This sets up a connection to the server. */
	generic_connect_server_cb(0, 0, gnhastd_conn);
	collector_instance = cfg_getint(moncoll_c, "instance");
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

	/* setup the collection */

	setup_monitors(moncoll_c);
	secs.tv_sec = PID_RATE;
	ev = event_new(base, -1, EV_PERSIST, cb_checkpids, NULL);
	event_add(ev, &secs);
	cb_checkpids(0, 0, NULL); /* run one right now */
	/* we setup the restart routine inside the pid check */

	/* go forth and destroy */
	event_base_dispatch(base);

	/* Close out the log, and bail */
	closelog();
	delete_pidfile();
	return(0);
}

