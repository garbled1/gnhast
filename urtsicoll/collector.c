/*
 * Copyright (c) 2016
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
   \brief Somfy URTSI collector

   The URTSI is odd, in that you can send commands to it, but there is
   no reading from it.  It's basically a write-only device.  Additionally,
   because these motors have no concept of position, only up/down/stop, and
   limit switch stops, You can't actually know where they are at any given
   moment.  Therefore, in gnhast, you cannot ask about a usrti device.  The
   return would be pure conjecture.
*/

#include <termios.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <event2/dns.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>

#include "common.h"
#include "gnhast.h"
#include "confuse.h"
#include "confparser.h"
#include "collcmd.h"
#include "genconn.h"
#include "gncoll.h"
#include "urtsi.h"

void connect_event_cb(struct bufferevent *ev, short what, void *arg);
void connect_server_cb(int nada, short what, void *arg);

char *conffile = SYSCONFDIR "/" URTSICOLL_CONFIG_FILE;
FILE *logfile;   /** our logfile */
extern int debugmode;
cfg_t *cfg, *gnhastd_c, *urtsi_c;
char *dumpconf = NULL;
int need_rereg = 0;
int first_update = 0;
int gotdata = 0;
char *uidprefix = "urtsi";
time_t urtsi_lastupd;

/** Need the argtable in scope, so we can generate proper commands
    for the server */
extern argtable_t argtable[];
extern TAILQ_HEAD(, _device_t) alldevs;
extern int collector_instance;

/** The event base */
struct event_base *base;
struct evdns_base *dns_base;

char *conntype[5] = {
	"none",
	"urtsi",
	"gnhastd",
};

/** The connection streams for our two connections */
connection_t *gnhastd_conn, *urtsi_conn;

/* Configuration file setup */

extern cfg_opt_t device_opts[];

cfg_opt_t urtsi_opts[] = {
	CFG_STR("serialdev", 0, CFGF_NODEFAULT),
	CFG_STR("usrti_address", "01", CFGF_NONE),
	CFG_INT("instance", 1, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t gnhastd_opts[] = {
	CFG_STR("hostname", "127.0.0.1", CFGF_NONE),
	CFG_INT("port", 2920, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t options[] = {
	CFG_SEC("urtsi", urtsi_opts, CFGF_NONE),
	CFG_SEC("gnhastd", gnhastd_opts, CFGF_NONE),
	CFG_SEC("device", device_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_STR("logfile", URTSICOLL_LOG_FILE, CFGF_NONE),
	CFG_STR("pidfile", URTSICOLL_PID_FILE, CFGF_NONE),
	CFG_END(),
};


/**********************************************
	gnhastd handlers
**********************************************/

/**
   \brief Check if a collector is functioning properly
   \param conn connection_t of collector's gnhastd connection
   \return 1 if OK, 0 if broken
   \note No way to do this. Assume OK.
*/

int collector_is_ok(void)
{
	return(1);
}


/**
   \brief Called when a switch chg command occurs
   \param dev device that got updated
   \param arg pointer to client_t
*/

void coll_chg_cb(device_t *dev, void *arg)
{
	client_t *cli = (client_t *)arg;
	char buf[8];
	char *addr;
	uint8_t state;
	struct evbuffer *send;

	get_data_dev(dev, DATALOC_CHANGE, &state);
	switch (state) {
	case BLIND_UP: sprintf(buf, "U"); break;
	case BLIND_DOWN: sprintf(buf, "D"); break;
	case BLIND_STOP: sprintf(buf, "S"); break;
	default: sprintf(buf, "S"); break;
	}

	addr = cfg_getstr(urtsi_c, "urtsi_address");

	LOG(LOG_DEBUG, "Request to change motor %s to %s", dev->loc, buf);

	send = evbuffer_new();
	if (addr == NULL) {
		LOG(LOG_WARNING, "No URTSI address in cfg file, assuming 01");
		evbuffer_add_printf(send, "01%s%s\r", dev->loc, buf);
	} else
		evbuffer_add_printf(send, "%s%s%s\r", addr, dev->loc, buf);
	bufferevent_write_buffer(urtsi_conn->bev, send);
	evbuffer_free(send);	
}

/*****
      General stuff
*****/

/**
   \brief Create a new motor dev for motor #mot
   \param mot motor number to control
   \return device_t of new dev
*/

device_t *new_motor_dev(int mot)
{
	char buf[256], buf2[256];
	device_t *dev;
	int val = BLIND_STOP;

	if (mot >= 16)
		mot = 0; /* 16 is 0, rest are sane HEX */
	sprintf(buf, "%s-%0.2X", uidprefix, mot);
	sprintf(buf2, "%0.2X", mot);
	dev = new_dev_from_conf(cfg, buf);
	if (dev == NULL) {
		dev = smalloc(device_t);
		dev->uid = strdup(buf);
		dev->loc = strdup(buf2);
		dev->type = DEVICE_BLIND;
		dev->proto = PROTO_CONTROL_URTSI;
		dev->subtype = SUBTYPE_BLIND;
		(void)new_conf_from_dev(cfg, dev);
	}
	insert_device(dev);
	store_data_dev(dev, DATALOC_DATA, &val);
	if (dumpconf == NULL && dev->name != NULL && gnhastd_conn != NULL) {
		gn_register_device(dev, gnhastd_conn->bev);
		gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
	}
	return dev;
}

/**
   \brief create an initial conf file
   \param motors number of motors
*/

void maybe_dump_conf(int motors)
{
	int i;
	char buf[16], buf2[256];
	device_t *dev;

	for (i=0; i < motors; i++) {
		sprintf(buf, "blind%d", i+1);
		sprintf(buf2, "%s-%s", uidprefix, buf);
		dev = find_device_byuid(buf2);
		if (dev == NULL)
			dev = new_motor_dev(i);
	}

	if (dumpconf != NULL) {
		LOG(LOG_NOTICE, "Dumping config file to %s and exiting",
		    dumpconf);
		dump_conf(cfg, 0, dumpconf);
		exit(0);
	}
}

/*****
      URTSI Functions
*****/

/* basically, none, it's write only */

/**
   \brief ad2usb read callback
   \param in the bufferevent that fired
   \param arg the connection_t
   \note In theory, this should NEVER get called?
*/

void urtsi_buf_read_cb(struct bufferevent *in, void *arg)
{
        connection_t *conn = (connection_t *)arg;
        size_t len;
        char *data;
	struct evbuffer *evbuf;

        LOG(LOG_DEBUG, "enter read_cb");

        urtsi_lastupd = time(NULL);

        /* loop as long as we have data to read */
        while (1) {
                evbuf = bufferevent_get_input(in);
                data = evbuffer_readln(evbuf, &len, EVBUFFER_EOL_CRLF);

                if (data == NULL || len < 1)
                        break;

                LOG(LOG_DEBUG, "Got data from URTSI: %s", data);
                free(data);
        }
}

/*****
      General routines/gnhastd connection stuff
*****/

/**
   \brief Parse the config file for devices and load them
   \param cfg config base
*/
void parse_devices(cfg_t *cfg)
{
	device_t *dev;
	cfg_t *devconf;
	cfg_opt_t *opt;
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
	int ch, fd, motors=0;
	char *buf;
	struct timeval secs = { 0, 0 };
	struct event *ev;

	/* process command line arguments */
	while ((ch = getopt(argc, argv, "?c:dm:u:M:")) != -1)
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
		case 'u':
			uidprefix = strdup(optarg);
			break;
		case 'M':
			motors = atoi(optarg);
			break;
		default:
		case '?':	/* you blew it */
			(void)fprintf(stderr, "usage:\n%s [-c configfile]"
				      "[-m dumpconfigfile] [-u uidprefix]"
				      "[-M motors]\n",
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

	/* set for first update */
	first_update = 1;
	urtsi_lastupd = time(NULL);

	cfg = parse_conf(conffile);

	if (!debugmode)
		logfile = openlog(cfg_getstr(cfg, "logfile"));

	writepidfile(cfg_getstr(cfg, "pidfile"));

	/* First, parse the urtsicoll section */

	if (cfg) {
		urtsi_c = cfg_getsec(cfg, "urtsi");
		if (!urtsi_c)
			LOG(LOG_FATAL, "Error reading config file,"
			    " urtsi section");
	}

	/* Now, parse the details of connecting to the gnhastd server */

	if (cfg) {
		gnhastd_c = cfg_getsec(cfg, "gnhastd");
		if (!gnhastd_c)
			LOG(LOG_FATAL, "Error reading config file,"
			    " gnhastd section");
	}

	if (motors != 0)
		maybe_dump_conf(motors);

	gnhastd_conn = smalloc(connection_t);
	gnhastd_conn->cname = strdup(COLLECTOR_NAME);
	gnhastd_conn->port = cfg_getint(gnhastd_c, "port");
	gnhastd_conn->type = CONN_TYPE_GNHASTD;
	gnhastd_conn->host = cfg_getstr(gnhastd_c, "hostname");
	/* cheat, and directly call the timer callback
	   This sets up a connection to the server. */
	generic_connect_server_cb(0, 0, gnhastd_conn);
	collector_instance = cfg_getint(urtsi_c, "instance");
	gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);

	/* read the serial device name from the conf file */
	if (cfg_getstr(urtsi_c, "serialdev") == NULL)
		LOG(LOG_FATAL, "Serial device not set in conf file");

	/* Connect to the URTSI */
	fd = serial_connect(cfg_getstr(urtsi_c, "serialdev"), B9600,
			    CS8|CREAD|CLOCAL);

	urtsi_conn = smalloc(connection_t);
	urtsi_conn->cname = strdup(COLLECTOR_NAME);
	urtsi_conn->bev = bufferevent_socket_new(base, fd,
						  BEV_OPT_CLOSE_ON_FREE);
	urtsi_conn->type = CONN_TYPE_URTSI;
	bufferevent_setcb(urtsi_conn->bev, urtsi_buf_read_cb,
			  NULL, serial_eventcb, urtsi_conn);
	bufferevent_enable(urtsi_conn->bev, EV_READ|EV_WRITE);

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

	parse_devices(cfg);

	/* go forth and destroy */
	event_base_dispatch(base);
	delete_pidfile();
	closelog();
	return(0);
}
