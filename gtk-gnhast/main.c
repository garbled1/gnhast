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
   \file main.c
   \brief Main for gtk-gnhast
   \author Tim Rightnour
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <pwd.h>
#include <sys/queue.h>
#include <event2/dns.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/util.h>
#include <gtk/gtk.h>

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif
#include "gnhast.h"
#include "common.h"
#include "confuse.h"
#include "confparser.h"
#include "gncoll.h"
#include "collcmd.h"
#include "interface.h"
#include "support.h"
#include "genconn.h"
#include "gtk-gnhast.h"

char *conffile = NULL;
FILE *logfile;   /** our logfile */
cfg_t *cfg, *gnhastd_c;
char *dumpconf = NULL;
int need_rereg = 0;
int feed_running = 0;
int conf_is_modified = 0;
char *devfile = GNHASTD_DEVICE_FILE;
char *groupfile = GNHASTD_DEVGROUP_FILE;
char *homedir;

/** Need the argtable in scope, so we can generate proper commands
    for the server */
extern argtable_t argtable[];
extern TAILQ_HEAD(, _device_t) alldevs;
extern TAILQ_HEAD(, _device_group_t) allgroups;
extern commands_t commands[];
extern int debugmode;
extern int notimerupdate;

extern GtkTreeModel *devicetree_model;
extern GtkTreeModel *devicelist_model;

/** The event base */
struct event_base *base;
struct evdns_base *dns_base;

#define CONN_TYPE_GNHASTD       1
char *conntype[3] = {
        "none",
        "gnhastd",
};

GtkWidget *main_window;
connection_t *gnhastd_conn;

extern cfg_opt_t device_opts[];
extern cfg_opt_t device_group_opts[];
cfg_opt_t gnhastd_opts[] = {
	CFG_STR("hostname", "127.0.0.1", CFGF_NONE),
	CFG_INT("port", 2920, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t options[] = {
	CFG_SEC("gnhastd", gnhastd_opts, CFGF_NONE),
	CFG_SEC("device", device_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_SEC("devgroup", device_group_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_END(),
};

void connect_event_cb(struct bufferevent *ev, short what, void *arg);
void establish_feeds(void);

/***** Stubs *****/

/**
   \brief Called when an upd command occurs
   \param dev device that got updated
   \param arg pointer to client_t
*/

void coll_upd_cb(device_t *dev, void *arg)
{
	update_all_vals_dev(dev);
	return;
}

/**
   \brief Handle a enldevs device command
   \param args The list of arguments
   \param arg void pointer to client_t of provider
*/

int cmd_endldevs(pargs_t *args, void *arg)
{
	struct evbuffer *send;

	send = evbuffer_new();
	evbuffer_add_printf(send, "lgrps\n");
	bufferevent_write_buffer(gnhastd_conn->bev, send);
	evbuffer_free(send);

	return;
}

/**
   \brief Handle a endlgrps device command
   \param args The list of arguments
   \param arg void pointer to client_t of provider
*/

int cmd_endlgrps(pargs_t *args, void *arg)
{
	TLOG(LOG_DEBUG, "Building devicetree model");
	update_devicetree_model(devicetree_model);
	TLOG(LOG_DEBUG, "Building devicelist model");
	update_devicelist_model(devicelist_model);
	if (!feed_running) {
		TLOG(LOG_DEBUG, "Establishing feeds at %d second intervals",
		     FEED_RATE);
		establish_feeds();
		feed_running = 1;
	}
	return;
}

/*****
      General routines/gnhastd connection stuff
*****/

/**
   \brief Attempt to connect to gnhastd
   \param server hostname of gnhastd server
   \param port port number
*/

void attempt_connect(char *server, int port)
{
	gnhastd_conn = smalloc(connection_t);
	gnhastd_conn->port = port;
	gnhastd_conn->type = CONN_TYPE_GNHASTD;
	gnhastd_conn->host = server;
	/* cheat, and directly call the timer callback
	   This sets up a connection to the server. */
	generic_connect_server_cb(0, 0, gnhastd_conn);
	gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);
}

/**
   \brief Disconnect and connect to new server
   \param server hostname of gnhastd server
   \param port port number
*/
void change_connection(char *server, int port)
{
	gnhastd_conn->shutdown = 2; /* 1 will loopexit */
	gn_disconnect(gnhastd_conn->bev);

	/* run the evloop twice, make sure it's done */
	event_base_loop(base, EVLOOP_ONCE|EVLOOP_NONBLOCK);
	sleep(1);
	event_base_loop(base, EVLOOP_ONCE|EVLOOP_NONBLOCK);

	/* fill in new details and reconnect */
	gnhastd_conn->port = port;
	gnhastd_conn->type = CONN_TYPE_GNHASTD;
	gnhastd_conn->host = server;
	/* cheat, and directly call the timer callback
	   This sets up a connection to the server. */
	generic_connect_server_cb(0, 0, gnhastd_conn);
	gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);
	event_base_loop(base, EVLOOP_ONCE|EVLOOP_NONBLOCK);

	cfg_setstr(gnhastd_c, "hostname", server);
	cfg_setint(gnhastd_c, "port", port);

	request_devlist();
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
	TLOG(LOG_WARNING, "Clean shutdown timed out, stopping");
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

	TLOG(LOG_NOTICE, "Recieved SIGTERM, shutting down");
	gnhastd_conn->shutdown = 1;
	gn_disconnect(gnhastd_conn->bev);
	ev = evtimer_new(base, cb_shutdown, NULL);
	evtimer_add(ev, &secs);
}

/**
   \brief A GTK callback to run the libevent2 event loop
   \param user_data user data
*/

gboolean run_event_base(gpointer user_data)
{

	/* spin the event loop once to check for data */
	event_base_loop(base, EVLOOP_ONCE|EVLOOP_NONBLOCK);
	return 1;
}

/**** Actual code goes here ****/

/**
   \brief Dump the current config file out
   \param filename filename to dump to
*/

void dump_rcfile(const char *filename)
{
	cfg_t *section;
	FILE *fp;
	time_t t;

	t = time(NULL);
        fp = fopen(filename, "w");
	if (fp == NULL) {
		TLOG(LOG_ERROR, "Could not open %s for writing", filename);
		return;
	}
        fprintf(fp, "# Config file for %s\n", getprogname());
        fprintf(fp, "# Generated on %s", ctime(&t));
        fprintf(fp, "#\n\n");
	section = cfg_getsec(cfg, "gnhastd");
	fprintf(fp, "gnhastd {\n");
	cfg_print_indent(section, fp, 2);
	fprintf(fp, "}\n");
	fclose(fp);
	TLOG(LOG_NOTICE, "Writing gtk-gnhast rc file %s", filename);
}


/**
   \brief Write the device conf file
*/

void devconf_dump_cb(void)
{
	device_t *dev;
	cfg_t *dc;
	char *buf;

	buf = safer_malloc(strlen(homedir) +
			   strlen(GNHASTD_DEVICE_FILE) + 2);
	sprintf(buf, "%s/%s", homedir, GNHASTD_DEVICE_FILE);

	TAILQ_FOREACH(dev, &alldevs, next_all) {
		dc = new_conf_from_dev(cfg, dev);
	}
	TLOG(LOG_NOTICE, "Writing device conf file %s", buf);
	dump_conf(cfg, CONF_DUMP_DEVONLY, buf);
	free(buf);
}

/**
   \brief Timer callback to update the device group conf file
   \param nada used for file descriptor
   \param what why did we fire?
   \param arg unused
*/

void devgroupconf_dump_cb(void)
{
	device_group_t *devgrp;
	cfg_t *dc;
	char *buf;

	buf = safer_malloc(strlen(homedir) +
			   strlen(GNHASTD_DEVGROUP_FILE) + 2);
	sprintf(buf, "%s/%s", homedir, GNHASTD_DEVGROUP_FILE);

	TAILQ_FOREACH(devgrp, &allgroups, next_all) {
		dc = new_conf_from_devgrp(cfg, devgrp);
	}
	TLOG(LOG_NOTICE, "Writing device group conf file %s", buf);
	dump_conf(cfg, CONF_DUMP_GROUPONLY, buf);
	free(buf);
}

/**
   \brief Request a list of devices from gnhastd
   \param conn connection_t
*/

void request_devlist(void)
{
	struct evbuffer *send;

	send = evbuffer_new();
	evbuffer_add_printf(send, "ldevs\n");
	bufferevent_write_buffer(gnhastd_conn->bev, send);
	evbuffer_free(send);
}

/**
   \brief Request full details of a device
   \param dev device
*/

void request_full_device(device_t *dev)
{
	struct evbuffer *send;

	if (dev == NULL || dev->uid == NULL)
		return;

	send = evbuffer_new();
	evbuffer_add_printf(send, "askf uid:%s\n", dev->uid);
	bufferevent_write_buffer(gnhastd_conn->bev, send);
	evbuffer_free(send);
}

/**
   \brief Establish all the device data feeds
*/
void establish_feeds(void)
{
	struct evbuffer *send;
	device_t *dev;

	TAILQ_FOREACH(dev, &alldevs, next_all) {
		send = evbuffer_new();
		evbuffer_add_printf(send, "feed %s:%s %s:%d\n", ARGNM(SC_UID),
				    dev->uid, ARGNM(SC_RATE), FEED_RATE);
		bufferevent_write_buffer(gnhastd_conn->bev, send);
		evbuffer_free(send);
		request_full_device(dev);
	}
}


/**
   \brief main itself
   \param argc count
   \param arvg vector
   \return int
*/

int main (int argc, char **argv)
{
	extern char *optarg;
	extern int optind;
	char *username, *path, *logname = NULL;
	struct passwd *userinfo;
	int ch;

	/* process command line arguments */
	while ((ch = getopt(argc, argv, "?c:dl:")) != -1)
		switch (ch) {
		case 'c':	/* Set configfile */
			conffile = strdup(optarg);
			break;
		case 'd':
			debugmode = 1;
			break;
		case 'l':
			logname = strdup(optarg);
			break;
		default:
		case '?':	/* you blew it */
			(void)fprintf(stderr, "usage:\n%s [-c configfile]"
				      " [-l logfile]\n",
				      getprogname());
			return(EXIT_FAILURE);
			/*NOTREACHED*/
			break;
		}

	if (logname != NULL)
		logfile = openlog(logname);

	gtk_set_locale();
	gtk_init(&argc, &argv);

	add_pixmap_directory(PACKAGE_DATA_DIR "/" PACKAGE "/pixmaps");
	if (debugmode)
		add_pixmap_directory("./data");

	/* Initialize the event system */
	base = event_base_new();
	dns_base = evdns_base_new(base, 1);

	/* Initialize the argtable */
	init_argcomm();
	init_commands();

	/* Initialize the device table */
	init_devtable(cfg, 0);

	username = getlogin();
	userinfo = getpwnam(username);

	homedir = userinfo->pw_dir;
	if (conffile == NULL) {
		conffile = safer_malloc(strlen(userinfo->pw_dir) +
					strlen(GTK_GNHAST_CONFIG_FILE) + 2);
		sprintf(conffile, "%s/%s", userinfo->pw_dir,
			GTK_GNHAST_CONFIG_FILE);
	}

	cfg = parse_conf(conffile);
	/* Now, parse the details of connecting to the gnhastd server */
	if (cfg) {
		gnhastd_c = cfg_getsec(cfg, "gnhastd");
		if (!gnhastd_c)
			LOG(LOG_FATAL, "Error reading config file, "
			    "gnhastd section");
	}

	attempt_connect(cfg_getstr(gnhastd_c, "hostname"),
			cfg_getint(gnhastd_c, "port"));

	main_window = create_window1();
	gtk_widget_show(main_window);

	gtk_signal_connect(GTK_OBJECT(main_window), "delete_event",
			   GTK_SIGNAL_FUNC(delete_event), NULL);
	gtk_signal_connect(GTK_OBJECT(main_window), "destroy",
			   G_CALLBACK(exit_cb), NULL);


	request_devlist();

	/* call the event2 loop 4 times a second */
	g_timeout_add(250, run_event_base, NULL);

	gtk_main();
	return 0;
}
