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
   \brief Bidirectional collector for insteon devices
   \author Tim Rightnour
   \note Currently only supports i2 and i2cs devices
*/


#include <termios.h>
#include <stdio.h>
#include <err.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <math.h>
#include <sys/queue.h>
#include <event2/dns.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>

#include "gnhast.h"
#include "common.h"
#include "confparser.h"
#include "confuse.h"
#include "gncoll.h"
#include "collcmd.h"
#include "insteon.h"

extern int errno;
extern int debugmode;
extern TAILQ_HEAD(, _device_t) alldevs;
extern int nrofdevs;
extern char *conntype[];
extern commands_t commands[];

void connect_event_cb(struct bufferevent *ev, short what, void *arg);
void connect_server_cb(int nada, short what, void *arg);
void plm_query_all_devices(void);
void plm_query_grouped_devices(device_t *dev, uint group);

/* Configuration file details */

char *conffile = SYSCONFDIR "/" INSTEONCOLL_CONF_FILE;
int need_query = 0;
cfg_t *cfg, *icoll_c, *gnhastd_c;
extern cfg_opt_t device_opts[];

cfg_opt_t insteoncoll_opts[] = {
	CFG_STR("device", 0, CFGF_NODEFAULT),
	CFG_INT("rescan", 60, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t gnhastd_opts[] = {
	CFG_STR("hostname", "127.0.0.1", CFGF_NONE),
	CFG_INT("port", 2920, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t options[] = {
	CFG_SEC("gnhastd", gnhastd_opts, CFGF_NONE),
	CFG_SEC("insteoncoll", insteoncoll_opts, CFGF_NONE),
	CFG_SEC("device", device_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_STR("logfile", INSTEONCOLL_LOG_FILE, CFGF_NONE),
	CFG_STR("pidfile", INSTEONCOLL_PID_FILE, CFGF_NONE),
	CFG_END(),
};

FILE *logfile;

struct event_base *base;
struct evdns_base *dns_base;
connection_t *plm_conn;
connection_t *gnhastd_conn;
uint8_t plm_addr[3];
int need_rereg = 0;

extern SIMPLEQ_HEAD(fifohead, _cmdq_t) cmdfifo;


usage(void)
{
	(void)fprintf(stderr, "Usage %s: [-c <conffile>]\n",
		      getprogname());
	exit(1);
}

/**
   \brief Store and log data from a switch
   \param dev device to store data to
   \param s data to store
*/

void storelog_switch(device_t *dev, uint8_t s)
{
	uint8_t orig_s;

	LOG(LOG_DEBUG, "Storing data from %s %d", dev->loc, s);
	store_data_dev(dev, DATALOC_DATA, &s);
	get_data_dev(dev, DATALOC_LAST, &orig_s);
	if (s != orig_s)
		LOG(LOG_NOTICE, "Switch device %s changed "
		    "state from %d to %d", dev->uid, orig_s,s);
}

/**
   \brief Store and log data from a dimmer
   \param dev device to store data to
   \param s data to store
*/

void storelog_dimmer(device_t *dev, double d)
{
	double orig_d;

	LOG(LOG_DEBUG, "Storing data from %s %f", dev->loc, d);
	store_data_dev(dev, DATALOC_DATA, &d);
	get_data_dev(dev, DATALOC_LAST, &orig_d);
	if (d != orig_d)
		LOG(LOG_NOTICE, "Dimmer device %s changed "
		    "state from %f to %f", dev->uid, orig_d, d);
}


/**********************************************
	gnhastd handlers
**********************************************/

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
   \brief Handle a enldevs device command
   \param args The list of arguments
   \param arg void pointer to client_t of provider
*/

int cmd_endldevs(pargs_t *args, void *arg)
{
	return 0;
}

/**
   \brief Called when a switch chg command occurs
   \param dev device that got updated
   \param state new state (on/off)
   \param arg pointer to client_t
*/

void coll_chg_switch_cb(device_t *dev, int state, void *arg)
{
	if (state) {
		plm_switch_on(dev, 0xFF);
		LOG(LOG_NOTICE, "Got request to turn on switch %s", dev->uid);
	} else {
		plm_switch_off(dev);
		LOG(LOG_NOTICE, "Got request to turn off switch %s", dev->uid);
	}
}

/**
   \brief Called when a dimmer chg command occurs
   \param dev device that got updated
   \param level new dimmer level
   \param arg pointer to client_t
*/

void coll_chg_dimmer_cb(device_t *dev, double level, void *arg)
{
	double d;
	uint8_t u;

	if (level == 0.0) {
		plm_switch_off(dev);
		return;
	}

	d = rint(255.0 * level);
	u = (uint8_t)d;
	LOG(LOG_NOTICE, "Got dimmer request level=%f for device %s", level,
		dev->uid);
	plm_switch_on(dev, u);

	return;
}

/**************************************
	PLM Handling code
**************************************/


/**
   \brief Queue is empty callback
   \param arg pointer to connection_t
*/

void plm_queue_empty_cb(void *arg)
{
	connection_t *conn = (connection_t *)arg;

	if (need_query) {
		plm_enq_wait(10);
		plm_query_all_devices();
	}
	need_query = 0;
	return; /* do nothing */
}

/**
   \brief Rescan request callback
   \param fd unused
   \param what unused
   \param arg pointer to connection_t
*/
void plm_rescan(int fd, short what, void *arg)
{

	need_query++;
}


/**
   \brief Handle an all-linking completed command
   \param data recieved
*/

void plm_handle_alink_complete(uint8_t *data)
{
	char im[16];
	uint8_t devaddr[3];
	cmdq_t *cmd;

	cmd = SIMPLEQ_FIRST(&cmdfifo);

	memcpy(devaddr, data+4, 3);
	addr_to_string(im, devaddr);
	LOG(LOG_NOTICE, "ALINK Complete: dev: %s devcat: %0.2X subcat: %0.2X "
	    "Firmware: %0.2X Group: %0.2X Linktype: %0.2X",
	    im, data[7], data[8], data[9], data[3], data[2]);

	if (memcmp(devaddr, (cmd->cmd)+2, 3) == 0)
		plmcmdq_dequeue();
}

/**
   \brief Handle a std length recv
   \param fromaddr Who from?
   \param toaddr who to?
   \param flags message flags
   \param com1 command1
   \param com2 command2
*/

void plm_handle_stdrecv(uint8_t *fromaddr, uint8_t *toaddr, uint8_t flags,
			uint8_t com1, uint8_t com2, connection_t *conn)
{
	char fa[16], ta[16];
	device_t *dev;
	double d;
	uint8_t s, group;
	cmdq_t *cmd;
	int maybe_need_query = 0;

	addr_to_string(fa, fromaddr);
	addr_to_string(ta, toaddr);

	LOG(LOG_DEBUG, "StdMesg from:%s to %s cmd1,2:0x%0.2X,0x%0.2X "
	    "flags:0x%0.2X", fa, ta, com1, com2, flags);
	plmcmdq_check_recv(fromaddr, toaddr, com1, CMDQ_WAITDATA);

	dev = find_device_byuid(fa);
	if (dev == NULL) {
		LOG(LOG_ERROR, "Unknown device %s sent stdmsg", fa);
		return;
	}

	group = 0;
	/* do we have a broadcast to a group */
	if (flags & PLMFLAG_GROUP && flags & PLMFLAG_BROAD)
		group = toaddr[2]; /* low byte in send is group number */

	/* look for status requests, as they are wierd */
	cmd = SIMPLEQ_FIRST(&cmdfifo);
	if (cmd != NULL && cmd->cmd[6] == STDCMD_STATUSREQ) {
		d = (double)com2 / 255.0;
		if (dev->type == DEVICE_SWITCH) {
			s = (com2 > 0) ? 1 : 0;
			storelog_switch(dev, s);
		} else {
			storelog_dimmer(dev, d);
		}
		gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
		return;
	}

	switch (com1) {
	case STDCMD_GETVERS:
		LOG(LOG_DEBUG, "Got Version info from %s", fa);
		switch (com2) {
		case 0x00:
			dev->proto = PROTO_INSTEON_V1;
			break;
		case 0x01:
			dev->proto = PROTO_INSTEON_V2;
			break;
		case 0x02:
			dev->proto = PROTO_INSTEON_V2CS;
			break;
		case 0xFF:
			dev->proto = PROTO_INSTEON_V2CS;
			LOG(LOG_WARNING, "Device %s is i2cs not linked to PLM");
			break;
		}
		break;
	case STDCMD_PING:
		LOG(LOG_DEBUG, "Ping reply from %s", fa);
		plm_set_hops(dev, flags);
		need_query++;
		break;
	case STDCMD_ON:
		LOG(LOG_NOTICE, "Got ON from %s", fa);
	case STDCMD_FASTON:
		LOG(LOG_NOTICE, "Got FAST ON from %s", fa);
		if ((flags & PLMFLAG_GROUP) && com2 == 0) {
			d = 100.0; /* for now, lets assume this */
			s = 1;
		} else {
			d = (double)com2 / 255.0;
			s = com2;
		}
		if (dev->type == DEVICE_SWITCH)
			storelog_switch(dev, s);
		else
			storelog_dimmer(dev, d);
		gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
		/* schedule a query for it too */
		maybe_need_query++;
		break;
	case STDCMD_OFF:
		LOG(LOG_NOTICE, "Got OFF from %s", fa);
	case STDCMD_FASTOFF:
		LOG(LOG_NOTICE, "Got FAST OFF from %s", fa);
		if ((flags & PLMFLAG_GROUP) && com2 == 0) {
			d = 0.0; /* for now, lets assume this */
			s = 0x0;
		} else {
			d = (double)com2 / 255.0;
			s = com2;
		}
		if (dev->type == DEVICE_SWITCH)
			storelog_switch(dev, s);
		else
			storelog_dimmer(dev, d);
		gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
		/* schedule a query for it too */
		maybe_need_query++;
		break;
	case STDCMD_MANUALDIM:
		LOG(LOG_NOTICE, "Got Manual Dim Start from %s", fa);
		break;
	case STDCMD_MANUALDIMSTOP:
		LOG(LOG_NOTICE, "Got Manual Dim Stop from %s", fa);
		maybe_need_query++;
		break;
	}
	if (maybe_need_query) {
		plm_check_proper_delay(fromaddr);
		if (group)
			plm_query_grouped_devices(dev, group);
		else
			need_query++;
	}
}

/**
   \brief Handle an extended length recv
   \param fromaddr Who from?
   \param toaddr who to?
   \param flags message flags
   \param com1 command1
   \param com2 command2
*/

void plm_handle_extrecv(uint8_t *fromaddr, uint8_t *toaddr, uint8_t flags,
			uint8_t com1, uint8_t com2, uint8_t *ext,
			connection_t *conn)
{
	char fa[16], ta[16];
	device_t *dev;

	addr_to_string(fa, fromaddr);
	addr_to_string(ta, toaddr);

	LOG(LOG_DEBUG, "ExtMesg from:%s to %s cmd1,2:0x%0.2X,0x%0.2X "
	    "flags:0x%0.2X", fa, ta, com1, com2, flags);
	LOG(LOG_DEBUG, "ExtMesg data:0x%0.2X 0x%0.2X 0x%0.2X 0x%0.2X "
	    "0x%0.2X 0x%0.2X 0x%0.2X 0x%0.2X 0x%0.2X 0x%0.2X 0x%0.2X "
	    "0x%0.2X 0x%0.2X 0x%0.2X", ext[0], ext[1], ext[2], ext[3],
	    ext[4], ext[5], ext[6], ext[7], ext[8], ext[9], ext[10],
	    ext[11], ext[12], ext[13]);

	plmcmdq_check_recv(fromaddr, toaddr, com1, CMDQ_WAITEXT);

	dev = find_device_byuid(fa);
	if (dev == NULL) {
		LOG(LOG_ERROR, "Unknown device %s sent stdmsg", fa);
		return;
	}

	switch (com1) {
	case EXTCMD_RWALDB:
		if (plm_handle_aldb(dev, ext))
			plmcmdq_got_data(CMDQ_WAITALDB);
		break;
	}
}

/**
   \brief Ping all devices
*/

void plm_ping_all_devices(void)
{
	device_t *dev;

	TAILQ_FOREACH(dev, &alldevs, next_all)
		plm_enq_std(dev, STDCMD_PING, 0x00, CMDQ_WAITACKDATA);

	plm_enq_wait(5);

	TAILQ_FOREACH(dev, &alldevs, next_all) {
		plm_req_aldb(dev);
		plm_enq_wait(2);
	}
}

/**
   \brief Query all devices
*/
void plm_query_all_devices(void)
{
	device_t *dev;

	TAILQ_FOREACH(dev, &alldevs, next_all)
		plm_enq_std(dev, STDCMD_STATUSREQ, 0x00,
			    CMDQ_WAITACK|CMDQ_WAITANY);
}

/**
   \brief Query grouped devices
   \param dev device that might be leader of group
   \param group group number
*/
void plm_query_grouped_devices(device_t *dev, uint group)
{
	char gn[16];
	device_group_t *devgrp;
	wrap_device_t *wrap;

	sprintf(gn, "%s-%0.2X", dev->loc, group);
	devgrp = find_devgroup_byuid(gn);
	if (devgrp == NULL) {
		need_query++;
		return;
	}
	LOG(LOG_DEBUG, "Query device group: %s", gn);
	plm_enq_wait(10);
	TAILQ_FOREACH(wrap, &devgrp->members, next) {
		plm_enq_std(wrap->dev, STDCMD_STATUSREQ, 0x00,
			    CMDQ_WAITACK|CMDQ_WAITANY);
		LOG(LOG_DEBUG, "Query group member %s", wrap->dev->uid);
	}
	plm_enq_wait(1);
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
	device_t *dev;

	conn->bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
	if (conn->type == CONN_TYPE_GNHASTD)
		bufferevent_setcb(conn->bev, gnhastd_read_cb, NULL,
				  connect_event_cb, conn);

	bufferevent_enable(conn->bev, EV_READ|EV_WRITE);
	bufferevent_socket_connect_hostname(conn->bev, dns_base, AF_UNSPEC,
					    conn->host, conn->port);

	if (conn->type == CONN_TYPE_GNHASTD) {
		LOG(LOG_NOTICE, "Attempting to connect to %s @ %s:%d",
		    conntype[conn->type], conn->host, conn->port);
		if (need_rereg) {
			TAILQ_FOREACH(dev, &alldevs, next_all)
				if (dev->name != NULL)
					gn_register_device(dev, conn->bev);
			gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);
		}
		need_rereg = 0;
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
			evtimer_add(tev, &secs); /* XXX leak? */
			LOG(LOG_NOTICE, "Attempting reconnection to "
			    "%s @ %s:%d in %d seconds",
			    conntype[conn->type], conn->host, conn->port,
			    secs.tv_sec);
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
	unsigned int a, b, c;
	insteon_devdata_t *dd;

	for (i=0; i < cfg_size(cfg, "device"); i++) {
		devconf = cfg_getnsec(cfg, "device", i);
		dev = new_dev_from_conf(cfg, (char *)cfg_title(devconf));
		if (dev == NULL) {
			LOG(LOG_ERROR, "Couldn't find device conf for %s",
			    cfg_title(devconf));
			/* WTF?? */
			continue;
		}
		dd = smalloc(insteon_devdata_t);
		sscanf(dev->loc, "%x.%x.%x", &a, &b, &c);
		dd->daddr[0] = (uint8_t)a;
		dd->daddr[1] = (uint8_t)b;
		dd->daddr[2] = (uint8_t)c;
		dd->hopflag = 255;
		dev->localdata = dd;
		insert_device(dev);
		LOG(LOG_DEBUG, "Loaded device %s location %s from config file",
		    dev->uid, dev->loc);
		if (dev->name != NULL)
			gn_register_device(dev, gnhastd_conn->bev);
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
	bufferevent_free(plm_conn->bev);
	ev = evtimer_new(base, cb_shutdown, NULL);
	evtimer_add(ev, &secs);
}

/**
   \brief main
*/

int
main(int argc, char *argv[])
{
	int c, fd;
	struct ev_token_bucket_cfg *ratelim;
	struct timeval rate = { 1, 0 };
	struct timeval runq = { 0, 500 };
	struct timeval rescan = { 60, 0 };
	struct event *ev;

	while ((c = getopt(argc, argv, "c:d")) != -1)
		switch (c) {
		case 'c':
			conffile = optarg;
			break;
		case 'd':
			debugmode = 1;
			break;
		default:
			usage();
		}

	if (!debugmode)
		if (daemon(0, 0) == -1)
			LOG(LOG_FATAL, "Failed to daemonize: %s",
			    strerror(errno));

	/* Initialize the event system */
	base = event_base_new();
	dns_base = evdns_base_new(base, 1);

	/* Initialize the device table */
	init_devtable(cfg, 0);

	/* Initialize the argtable */
	init_argcomm();
	init_commands();

	/* Initialize the command fifo */
	SIMPLEQ_INIT(&cmdfifo);

	cfg = parse_conf(conffile);

	if (!debugmode)
		logfile = openlog(cfg_getstr(cfg, "logfile"));

	writepidfile(cfg_getstr(cfg, "pidfile"));

	if (cfg) {
		icoll_c = cfg_getsec(cfg, "insteoncoll");
		if (!icoll_c)
			LOG(LOG_FATAL, "Error reading config file, "
			    "insteoncoll section");
	}

	/* Now, parse the details of connecting to the gnhastd server */

	if (cfg) {
		gnhastd_c = cfg_getsec(cfg, "gnhastd");
		if (!gnhastd_c)
			LOG(LOG_FATAL, "Error reading config file, gnhastd "
			    "section");
	}

	/* read the serial device name from the conf file */
	if (cfg_getstr(icoll_c, "device") == NULL)
		LOG(LOG_FATAL, "Serial device not set in conf file");

	/* Connect to the PLM */

	fd = serial_connect(cfg_getstr(icoll_c, "device"), B19200,
			    CS8|CREAD|CLOCAL);

	plm_conn = smalloc(connection_t);
	plm_conn->bev = bufferevent_socket_new(base, fd,
					       BEV_OPT_CLOSE_ON_FREE);
	plm_conn->type = CONN_TYPE_PLM;
	bufferevent_setcb(plm_conn->bev, plm_readcb, NULL, serial_eventcb,
			  plm_conn);
	bufferevent_enable(plm_conn->bev, EV_READ|EV_WRITE);

	ratelim = ev_token_bucket_cfg_new(2400, 100, 25, 256, &rate);
	bufferevent_set_rate_limit(plm_conn->bev, ratelim);

	plm_getinfo();

	/* setup runq */
	ev = event_new(base, -1, EV_PERSIST, plm_runq, plm_conn);
	event_add(ev, &runq);

	ev = event_new(base, -1, EV_PERSIST, plm_rescan, plm_conn);
	rescan.tv_sec = cfg_getint(icoll_c, "rescan");
	event_add(ev, &rescan);

	/* Connect to gnhastd */

	gnhastd_conn = smalloc(connection_t);
	gnhastd_conn->port = cfg_getint(gnhastd_c, "port");
	gnhastd_conn->type = CONN_TYPE_GNHASTD;
	gnhastd_conn->host = cfg_getstr(gnhastd_c, "hostname");
	/* cheat, and directly call the timer callback
	   This sets up a connection to the server. */
	connect_server_cb(0, 0, gnhastd_conn);
	gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);

	/* setup signal handlers */
	ev = evsignal_new(base, SIGHUP, cb_sighup, conffile);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGTERM, cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGINT, cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGQUIT, cb_sigterm, NULL);
	event_add(ev, NULL);

	parse_devices(cfg);
	plm_ping_all_devices();

	/* loopit */
	event_base_dispatch(base);

	(void)close(fd);
	closelog();
	delete_pidfile();
	return 0;
}
