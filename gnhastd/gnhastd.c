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

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/queue.h>
#include <event2/event.h>

#include "confuse.h"
#include "config.h"
#include "gnhast.h"
#include "gnhastd.h"
#include "common.h"
#include "confparser.h"

/* Globals */
FILE *logfile = NULL;
extern int debugmode;
cfg_t *cfg;
char *dumpconf = NULL;
struct event_base *base;	/**< The primary event base */
extern TAILQ_HEAD(, _device_t) alldevs;
extern TAILQ_HEAD(, _client_t) clients;
extern TAILQ_HEAD(, _device_group_t) allgroups;


/* debugging */
/*_malloc_options = "AJ";*/

/* Configuration options for the server */

extern cfg_opt_t device_opts[];
extern cfg_opt_t device_group_opts[];

cfg_opt_t network_opts[] = {
	CFG_STR("listen", "127.0.0.1", CFGF_NONE),
	CFG_INT("sslport", 2921, CFGF_NONE),
	CFG_INT("port", 2920, CFGF_NONE),
	CFG_STR("certchain", "cert", CFGF_NONE),
	CFG_STR("privkey", "pkey", CFGF_NONE),
	CFG_INT_CB("usessl", 0, CFGF_NONE, conf_parse_bool),
	CFG_INT_CB("usenonssl", 1, CFGF_NONE, conf_parse_bool),
	CFG_END(),
};

cfg_opt_t options[] = {
	CFG_SEC("network", network_opts, CFGF_NONE),
	CFG_SEC("device", device_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_SEC("devgroup", device_group_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_STR("devconf", GNHASTD_DEVICE_FILE, CFGF_NONE),
	CFG_STR("devgroupconf", GNHASTD_DEVGROUP_FILE, CFGF_NONE),
	CFG_INT("devconf_update", 300, CFGF_NONE),
	CFG_INT("infodump", 600, CFGF_NONE),
	CFG_FUNC("include", cfg_include),
	CFG_STR("logfile", GNHASTD_LOG_FILE, CFGF_NONE),
	CFG_STR("pidfile", GNHASTD_PID_FILE, CFGF_NONE),
	CFG_END(),
};

/**
   \brief Timer callback to ping all collectors with associated devices
   \param nada used for file descriptor
   \param what why did we fire?
   \param arg unused
*/

void ping_clients_cb(int nada, short what, void *arg)
{
	client_t *client;

	TAILQ_FOREACH(client, &clients, next) {
		if (client->coll_dev != NULL) {
			LOG(LOG_DEBUG, "Sending ping to %s", client->name);
			gn_ping(client->ev);
		}
	}
}

/**
   \brief Timer callback to mark clients bad if they don't respond to pings
   \param nada used for file descriptor
   \param what why did we fire?
   \param arg unused
*/

void health_update_clients_cb(int nada, short what, void *arg)
{
	client_t *client;
	int i = COLLECTOR_BAD;

	TAILQ_FOREACH(client, &clients, next) {
		if (client->coll_dev != NULL &&
		    ((time(NULL) - client->lastupd) > HEALTH_CHECK_RATE * 3))
			store_data_dev(client->coll_dev, DATALOC_DATA, &i);
	}
}

/**
   \brief Timer callback to update the device conf file
   \param nada used for file descriptor
   \param what why did we fire?
   \param arg unused
*/

void devconf_dump_cb(int nada, short what, void *arg)
{
	device_t *dev;
	cfg_t *dc;
	char *p, *buf;
	int madebuf=0;

	p = cfg_getstr(cfg, "devconf");
	if (p == NULL)
		return;
	buf = NULL;
	if (strlen(p) >= 2) {
		if (p[0] == '.' && p[1] == '/')
			buf = cfg_getstr(cfg, "devconf");
		if (p[0] == '/')
			buf = cfg_getstr(cfg, "devconf");
	}
	/* ok, not an absolute/relative path, set it up */

	if (buf == NULL) {
		buf = safer_malloc(64 + strlen(p));
		sprintf(buf, "%s/%s", SYSCONFDIR, cfg_getstr(cfg, "devconf"));
		madebuf++;
	}

	TAILQ_FOREACH(dev, &alldevs, next_all) {
		dc = new_conf_from_dev(cfg, dev);
	}
	LOG(LOG_DEBUG, "Writing device conf file %s", buf);
	dump_conf(cfg, CONF_DUMP_DEVONLY, buf);
	if (madebuf)
		free(buf);
}

/**
   \brief Timer callback to update the device group conf file
   \param nada used for file descriptor
   \param what why did we fire?
   \param arg unused
*/

void devgroupconf_dump_cb(int nada, short what, void *arg)
{
	device_group_t *devgrp;
	cfg_t *dc;
	char *p, *buf;
	int madebuf=0;

	p = cfg_getstr(cfg, "devgroupconf");
	if (p == NULL)
		return;
	buf = NULL;
	if (strlen(p) >= 2) {
		if (p[0] == '.' && p[1] == '/')
			buf = cfg_getstr(cfg, "devgroupconf");
		if (p[0] == '/')
			buf = cfg_getstr(cfg, "devgroupconf");
	}
	/* ok, not an absolute/relative path, set it up */

	if (buf == NULL) {
		buf = safer_malloc(64 + strlen(p));
		sprintf(buf, "%s/%s", SYSCONFDIR,
			cfg_getstr(cfg, "devgroupconf"));
		madebuf++;
	}

	TAILQ_FOREACH(devgrp, &allgroups, next_all) {
		dc = new_conf_from_devgrp(cfg, devgrp);
	}
	LOG(LOG_DEBUG, "Writing device group conf file %s", buf);
	dump_conf(cfg, CONF_DUMP_GROUPONLY, buf);
	if (madebuf)
		free(buf);
}

/**
   \brief SIGTERM handler
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void cb_sigterm(int fd, short what, void *arg)
{
	LOG(LOG_NOTICE, "Recieved SIGTERM, shutting down");
	network_shutdown();
	event_base_loopexit(base, NULL);
}

/**
   \brief SIGINFO handler
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void cb_siginfo(int fd, short what, void *arg)
{
	client_t *client;
	wrap_device_t *wrap;
	device_t *dev;
	int i, w, d;

	i = 0;
	TAILQ_FOREACH(client, &clients, next)
		i++;
	LOG(LOG_NOTICE, "gnhastd reporting statistics:");
	LOG(LOG_NOTICE, "Number of clients connected: %d", i);
	TAILQ_FOREACH(client, &clients, next) {
		w = 0;
		d = 0;
		TAILQ_FOREACH(dev, &client->devices, next_client)
			d++;
		TAILQ_FOREACH(wrap, &client->wdevices, next)
			w++;
		LOG(LOG_NOTICE, "Client %s %s %s devices:%d wrapdevs:%d "
		    "updates:%d lastupd (seconds):%d",
		    client->provider ? "provider" : "reciever",
		    client->name ? client->name : "generic",
		    client->addr ? client->addr : "unknown",
		    d, w, client->updates,
		    (int)(time(NULL) - client->lastupd));
	}
	i = 0;
	TAILQ_FOREACH(dev, &alldevs, next_all)
		i++;
	LOG(LOG_NOTICE, "Total number of devices: %d", i);
	LOG(LOG_NOTICE, "End statistics");
}

/** \brief main, um, duh */

int main(int argc, char **argv)
{
	cfg_t *devgroups;
	extern char *optarg;
	extern int optind;
	int ch;
	char *conffile = SYSCONFDIR "/" GNHASTD_CONFIG_FILE;
	struct timeval secs = {0, 0};
	struct event *ev;
	pid_t pid;

	while ((ch = getopt(argc, argv, "?c:d")) != -1)
		switch (ch) {
		case 'c':	/* Set configfile */
			conffile = optarg;
			break;
		case 'd':
			debugmode = 1;
			break;
		case '?':	/* you blew it */
			(void)fprintf(stderr, "usage:\n%s [-c configfile]\n", getprogname());
			return(EXIT_FAILURE);
			/*NOTREACHED*/
			break;
		default:
			break;
		}

	if (!debugmode)
		if (daemon(0, 0) == -1)
			LOG(LOG_FATAL, "Failed to daemonize: %s",
			    strerror(errno));

	/* Parse the config file */
	cfg = parse_conf(conffile);
	if (cfg == NULL)
		LOG(LOG_FATAL, "Failed to parse config file");

	/* Fire up logging if not debugging */
	if (!debugmode)
		logfile = openlog(cfg_getstr(cfg, "logfile"));

	LOG(LOG_NOTICE, "Starting gnhastd version %s", VERSION);

	/* Initialize the event loop */
	base = event_base_new();

	/* Setup the network loop */
	init_netloop();

	init_devtable(cfg, 1);
	init_argcomm();
	init_commands();

	parse_devgroups(cfg);
	if (debugmode)
		print_group_table(1);

	/* schedule periodic rewrites of devices.conf */
	if (cfg_getint(cfg, "devconf_update") > 0) {
		secs.tv_sec = cfg_getint(cfg, "devconf_update");
		ev = event_new(base, -1, EV_PERSIST, devconf_dump_cb, NULL);
		event_add(ev, &secs);
		/* and one for devgroup conf */
		ev = event_new(base, -1, EV_PERSIST, devgroupconf_dump_cb,
			       NULL);
		event_add(ev, &secs);
	}

	/* schedule periodic statistic dumps to the log */
	if (cfg_getint(cfg, "infodump") > 0) {
		secs.tv_sec = cfg_getint(cfg, "infodump");
		ev = event_new(base, -1, EV_PERSIST, cb_siginfo, NULL);
		event_add(ev, &secs);
	}

	/* update timer devices */
	secs.tv_sec = 1;
	ev = event_new(base, -1, EV_PERSIST, cb_timerdev_update, NULL);
	event_add(ev, &secs);

	/* setup ping for all collectors */
	secs.tv_sec = HEALTH_CHECK_RATE;
	ev = event_new(base, -1, EV_PERSIST, ping_clients_cb, NULL);
	event_add(ev, &secs);

	/* setup collector health updates */
	secs.tv_sec = 3; /* 3 seconds should be fine */
	ev = event_new(base, -1, EV_PERSIST, health_update_clients_cb, NULL);
	event_add(ev, &secs);

	/* setup signal handlers */
	ev = evsignal_new(base, SIGHUP, cb_sighup, conffile);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGTERM, cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGINT, cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGQUIT, cb_sigterm, NULL);
	event_add(ev, NULL);
#ifdef SIGINFO
	ev = evsignal_new(base, SIGINFO, cb_siginfo, NULL);
	event_add(ev, NULL);
#endif
	ev = evsignal_new(base, SIGUSR1, cb_sigusr1, NULL);
	event_add(ev, NULL);
	signal(SIGPIPE, SIG_IGN); /* ignore sigpipe */

	pid = getpid();
	writepidfile(cfg_getstr(cfg, "pidfile"));

	/* Go forth and destroy */
	LOG(LOG_NOTICE, "Entering main network loop. Pid: %d", pid);
	event_base_dispatch(base);

	/* Close it all down */
	devconf_dump_cb(0, 0, 0);
	devgroupconf_dump_cb(0, 0, 0);
	cfg_free(cfg);
	closelog();
	return 0;
}
