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
#include "genconn.h"
#include "collcmd.h"
#include "insteon.h"

extern int errno;
extern int debugmode;
extern TAILQ_HEAD(, _device_t) alldevs;
extern int nrofdevs;
extern char *conntype[];
extern commands_t commands[];
extern char *hubhtml_username;
extern char *hubhtml_password;
extern int plmtype;
extern struct event *hubhtml_bufget_ev;

void plm_query_all_devices(void);
void plm_query_grouped_devices(device_t *dev, uint group);
void verify_aldb_group_links(device_t *dev);

/* Configuration file details */

char *conffile = SYSCONFDIR "/" INSTEONCOLL_CONF_FILE;
int need_query = 0;
cfg_t *cfg, *icoll_c, *gnhastd_c;
extern cfg_opt_t device_opts[];

cfg_opt_t insteoncoll_opts[] = {
	CFG_STR("device", 0, CFGF_NODEFAULT),
	CFG_INT("rescan", 60, CFGF_NONE),
	CFG_INT_CB("plmtype", PLM_TYPE_SERIAL, CFGF_NONE, conf_parse_plmtype),
	CFG_STR("hostname", "insteon-hub", CFGF_NONE),
	CFG_INT("hubport", 9761, CFGF_NONE),
	CFG_STR("httppass", "password", CFGF_NONE),
	CFG_STR("httpuser", "admin", CFGF_NONE),
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
char *dumpconf = NULL;
time_t plm_lastupd;

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
   \brief Check if a collector is functioning properly
   \param conn connection_t of collector's gnhastd connection
   \return 1 if OK, 0 if broken
*/

int collector_is_ok(void)
{
	if ((time(NULL) - plm_lastupd) < (cfg_getint(icoll_c, "rescan") * 5))
		return(1);
	return(0);
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

/**
   \brief Called when a chg command occurs
   \param dev device that got updated
   \param arg pointer to client_t
*/

void coll_chg_cb(device_t *dev, void *arg)
{
	double d;
	uint8_t s;

	if (dev->subtype == SUBTYPE_SWITCH && dev->type == DEVICE_SWITCH) {
		get_data_dev(dev, DATALOC_CHANGE, &s);
		coll_chg_switch_cb(dev, s, arg);
	} else if (dev->type == DEVICE_DIMMER) {
		get_data_dev(dev, DATALOC_CHANGE, &d);
		coll_chg_dimmer_cb(dev, d, arg);
	}
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
   \brief Handle an aldb record recieved command
   \param data data recieved
*/

void plm_handle_aldb_record_resp(uint8_t *data)
{
	char im[16];
	uint8_t devaddr[3];

	memcpy(devaddr, data+4, 3);
	addr_to_string(im, devaddr);
	LOG(LOG_DEBUG, "ALINK Record: dev: %s link1: %0.2X link2: %0.2X "
	    "link3: %0.2X Group: %0.2X LinkFlags: %0.2X",
	    im, data[7], data[8], data[9], data[3], data[2]);
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
			uint8_t com1, uint8_t com2)
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

	/* we got a response from the plm, so update the last time */
	plm_lastupd = time(NULL);

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
			uint8_t com1, uint8_t com2, uint8_t *ext)
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
		if (plm_handle_aldb(dev, ext)) {
			plmcmdq_got_data(CMDQ_WAITALDB);
			/* Check the aldb for missing links! */
			verify_aldb_group_links(dev);
		}
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
   \brief Look for aldb backlinks to PLM for all groups, add if needed
   \param dev device to investigate
*/

void verify_aldb_group_links(device_t *dev)
{
	insteon_devdata_t *dd;
	int i, j, foundrec;

	if (dev == NULL || dev->localdata == NULL)
		return;

	dd = (insteon_devdata_t *)dev->localdata;

	for (i=0; i < dd->aldblen; i++) {
		foundrec = 0;
		for (j=0; j < dd->aldblen; j++) {
			/* check if the group has a backlink as a resp to us */
			if (dd->aldb[i].group == dd->aldb[j].group &&
			    dd->aldb[j].devaddr[0] == plm_addr[0] &&
			    dd->aldb[j].devaddr[1] == plm_addr[1] &&
			    dd->aldb[j].devaddr[2] == plm_addr[2] &&
			    !ALINK_IS_MASTER(dd->aldb[j].lflags))
				foundrec++;
		}
		if (!foundrec) {
			/* we are not linked to this group! add one as
			   a responder */
			LOG(LOG_NOTICE, "Found unlinked group %X on %s, "
			    "linking", dd->aldb[i].group, dev->uid);
			plm_all_link(0x00, dd->aldb[i].group);
			plm_enq_std(dev, STDCMD_LINKMODE, dd->aldb[i].group,
				    CMDQ_WAITACK|CMDQ_WAITDATA);
			plm_enq_std(dev, GRPCMD_ASSIGN_GROUP,
				    dd->aldb[i].group,
				    CMDQ_WAITACK|CMDQ_WAITDATA);
		}
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
	if (plm_conn != NULL)
		bufferevent_free(plm_conn->bev);
	ev = evtimer_new(base, generic_cb_shutdown, NULL);
	evtimer_add(ev, &secs);

	if (plmtype == PLM_TYPE_HUBHTTP)
		event_del(hubhtml_bufget_ev);
}

/**
   \brief main
*/

int
main(int argc, char *argv[])
{
	int c, fd;
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

	hubhtml_username = cfg_getstr(icoll_c, "httpuser");
	hubhtml_password = cfg_getstr(icoll_c, "httppass");
	plmtype = cfg_getint(icoll_c, "plmtype");

	/* Connect to the PLM */
	fd = plmtype_connect(plmtype, cfg_getstr(icoll_c, "device"),
			     cfg_getstr(icoll_c, "hostname"),
			     cfg_getint(icoll_c, "hubport"));

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
	ev = evsignal_new(base, SIGUSR1, cb_sigusr1, NULL);
	event_add(ev, NULL);

	parse_devices(cfg);
	plm_ping_all_devices();

	/* loopit */
	event_base_dispatch(base);

	if (fd != -1)
	    (void)close(fd);
	closelog();
	delete_pidfile();
	return 0;
}
