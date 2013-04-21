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

/* Globals */
FILE *logfile = NULL;
extern int debugmode;
cfg_t *cfg;
struct event_base *base;	/**< The primary event base */

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
	CFG_END(),
};

cfg_opt_t options[] = {
	CFG_SEC("network", network_opts, CFGF_NONE),
	CFG_SEC("device", device_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_END(),
};

/** \brief main, um, duh */

int main(int argc, char **argv)
{
	cfg_t *network;
	extern char *optarg;
	extern int optind;
	int ch;
	char *conffile = GNHASTD_CONFIG_FILE;

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

	init_devtable();
	init_argcomm();
	init_commands();

	/* Go forth and destroy */
	LOG(LOG_NOTICE, "Entering main network loop");
	event_base_dispatch(base);

	/* Close it all down */
	cfg_free(cfg);
	return 0;
}
