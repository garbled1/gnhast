/* $Id$ */

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
#include <stdlib.h>
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
struct event_base *base;	/**< The primary event base */
extern TAILQ_HEAD(, _device_t) alldevs;

/* debugging */
_malloc_options = "AJ";

/* Configuration options for the server */

extern cfg_opt_t device_opts[];
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
	CFG_STR("devconf", "devices.conf", CFGF_NONE),
	CFG_INT("devconf_update", 300, CFGF_NONE),
	CFG_END(),
};

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

	TAILQ_FOREACH(dev, &alldevs, next_all) {
		dc = new_conf_from_dev(cfg, dev);
	}
	LOG(LOG_DEBUG, "Writing device conf file %s",
	    cfg_getstr(cfg, "devconf"));
	dump_conf(cfg, CONF_DUMP_DEVONLY, cfg_getstr(cfg, "devconf"));
}

/** \brief main, um, duh */

int main(int argc, char **argv)
{
	cfg_t *network;
	extern char *optarg;
	extern int optind;
	int ch;
	char *conffile = GNHASTD_CONFIG_FILE;
	struct timeval secs = {0, 0};
	struct event *ev;

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

	/* Fire up logging */
	//logfile = openlog("log");
	LOG(LOG_NOTICE, "Starting gnhastd version %s", VERSION);

	/* Parse the config file */
	cfg = parse_conf(conffile);

	/* Initialize the event loop */
	base = event_base_new();

	/* Setup the network loop */
	init_netloop();

	init_devtable(cfg, 1);
	init_argcomm();
	init_commands();

	/* schedule periodic rewrites of devices.conf */
	if (cfg_getint(cfg, "devconf_update") > 0) {
		secs.tv_sec = cfg_getint(cfg, "devconf_update");
		ev = event_new(base, -1, EV_PERSIST, devconf_dump_cb,
					NULL);
		event_add(ev, &secs);
	}

	/* Go forth and destroy */
	LOG(LOG_NOTICE, "Entering main network loop");
	event_base_dispatch(base);

	/* Close it all down */
	cfg_free(cfg);
	return 0;
}
