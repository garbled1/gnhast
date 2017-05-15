/*
 * Copyright (c) 2015
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
   \file gtk-insteonedit/main.c
   \brief Main for gtk-insteonedit
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
#include "genconn.h"
#include "interface.h"
#include "support.h"
#include "insteon.h"
#include "gtk-insteonedit.h"

char *conffile = NULL;
FILE *logfile;   /** our logfile */
cfg_t *cfg, *gnhastd_c, *cfg_idb;
char *dumpconf = NULL;
int conf_is_modified = 0;
char *devfile = INSTEONCOLL_CONF_FILE;
connection_t *plm_conn;
uint8_t plm_addr[3];
aldb_t *plmaldb;
int plmaldbsize = 0;
int nrofplmaldb = 0;
#define PLMALDBMALLOC 16

/* stuff to shut up genconn */
int need_rereg = 0;
connection_t *gnhastd_conn;

extern TAILQ_HEAD(, _device_t) alldevs;
extern TAILQ_HEAD(, _device_group_t) allgroups;
extern int debugmode;
extern int notimerupdate;
extern SIMPLEQ_HEAD(fifohead, _cmdq_t) cmdfifo;
extern int plmaldbmorerecords;
extern insteon_devdata_t plminfo;
extern char *hubhtml_username;
extern char *hubhtml_password;
extern int plmtype;
extern struct event *hubhtml_bufget_ev;

extern GtkTreeModel *devicelist_model;
extern int working;
extern int getinfo_working;
extern int getld_working;
extern int plmaldbmorerecords;

/** The event base */
struct event_base *base;
struct evdns_base *dns_base;

GtkWidget *main_window;

/* Copied from insteoncoll/collector.c */
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
	CFG_INT("instance", 1, CFGF_NONE),
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

/* insteon db file */
cfg_opt_t insteonrec[] = {
	CFG_STR("name", 0, CFGF_NODEFAULT),
	CFG_INT("devcat", 0, CFGF_NONE),
	CFG_INT("subcat", 0, CFGF_NONE),
	CFG_INT_CB("subtype", 0, CFGF_NODEFAULT, conf_parse_subtype),
	CFG_INT_CB("type", 0, CFGF_NODEFAULT, conf_parse_type),
	CFG_INT_LIST("defgroups", "{1}", CFGF_NONE),
	CFG_END(),
};

cfg_opt_t insteondb[] = {
	CFG_SEC("model", insteonrec, CFGF_MULTI | CFGF_TITLE),
	CFG_END(),
};


/**
   \brief Find a db entry matching devcat/subcat
   \param devcat devcat
   \param subcat subcat
   \return config entry, NULL if not found
*/

cfg_t *find_db_entry(int devcat, int subcat)
{
	cfg_t *section;
	int i;

	for (i = 0; i < cfg_size(cfg_idb, "model"); i++) {
		section = cfg_getnsec(cfg_idb, "model", i);
		if (cfg_getint(section, "devcat") == devcat &&
		    cfg_getint(section, "subcat") == subcat)
			return section;
	}
	return NULL;
}

/**
   \brief Parse a config file
   \param filename filename to parse
   \return pointer to generated config structure
*/

cfg_t *parse_insteondb(const char *filename)
{
	cfg_t *cfg = cfg_init(insteondb, CFGF_NONE);

	switch(cfg_parse(cfg, filename)) {
	case CFG_FILE_ERROR:
		LOG(LOG_WARNING, "Config file %s could not be read: %s",
			filename, strerror(errno));
	case CFG_SUCCESS:
		break;
	case CFG_PARSE_ERROR:
		return 0;
	}

	return cfg;
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
		dd->proto = 0xFE; /* special value */
		dev->localdata = dd;
		insert_device(dev);
		LOG(LOG_DEBUG, "Loaded device %s location %s from config file",
		    dev->uid, dev->loc);
	}
}

/**
   \brief A GTK callback to see if we have all getld data yet
   \param user_data pointer to dev
*/
gboolean check_getld_status(gpointer user_data)
{
	device_t *dev = (device_t *)user_data;
	insteon_devdata_t *dd;

	if (dev == NULL) {
		if (plmaldbmorerecords == 0) {
			getld_working = GETLD_IDLE;
			working = 0;
			/* update the UI */
			update_aldb_devicelist_model(NULL);
			return 0;
		}
	} else {
		if (getld_working == GETLD_DONE) {
			getld_working = GETLD_IDLE;
			working = 0;
			/* update the UI */
			return 0;
		}
	}
	return 1;
}

/**
   \brief A GTK callback to see if we have all getinfo data yet
   \param user_data pointer to dev
*/
gboolean check_getinfo_status(gpointer user_data)
{
	device_t *dev = (device_t *)user_data;
	insteon_devdata_t *dd;

	if (dev == NULL) {
		getinfo_working = GETINFO_IDLE;
		working = 0;
		update_info_dev(NULL);
		return 0;
	}

	if (getinfo_working == GETINFO_UNLINKED) {
		getinfo_working = GETINFO_IDLE;
		working = 0;
		clear_info_link("UnLinked");
		return 0;
	}

	dd = (insteon_devdata_t *)dev->localdata;

	/* XXX add ramprate */
	if (dd->firmware != 0x00 && dd->proto != 0xFF) {
		getinfo_working = GETINFO_IDLE;
		working = 0;
		update_info_dev(dev);
		return 0;
	}

	return 1;
}

/**
   \brief Get the aldb for  a device
   \param dev device to gather data for
*/

void get_aldb_dev(device_t *dev)
{
	insteon_devdata_t *dd;

	if (getld_working)
		return;

	if (dev == NULL) {
		plm_getplm_aldb(0);
		getld_working = GETLD_WORKING;
	} else {
		dd = (insteon_devdata_t *)dev->localdata;
		if (dd->proto == 0xFE || dd->proto == 0xFF)
			return;
		plm_req_aldb(dev);
		getld_working = GETLD_WORKING;
	}
	g_timeout_add(250, check_getld_status, (gpointer)dev);
}

/**
   \brief Get detailed info about a device
   \param dev device to gather data for
*/

void getinfo_dev(device_t *dev)
{
	insteon_devdata_t *dd;

	if (getinfo_working)
		return;

	if (dev == NULL) {
		/* we are working on the plm, so, done! */
		(void)check_getinfo_status(NULL);
		return;
	}
	dd = (insteon_devdata_t *)dev->localdata;

	working = 1;

	if (dd->proto == 0xFE) {
		plm_enq_std(dev, STDCMD_GETVERS, 0x00, CMDQ_WAITACKDATA);
		getinfo_working = GETINFO_WORKING;
	}

	if (dd->firmware == 0x00) {
		plm_enq_std(dev, STDCMD_PDATA_REQ, 0x00, CMDQ_WAITEXT);
		getinfo_working = GETINFO_WORKING;
	}

	if (dd->ramprate == 0x00) {
		/* XXX todo */;
	}

	if (getinfo_working)
		g_timeout_add(250, check_getinfo_status, (gpointer)dev);
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

/**
   \brief A GTK callback to check for the reciept of the PLM info
   \param user_data user data
*/

gboolean check_plminfo(gpointer user_data)
{
	if (plminfo.daddr[0] == 0x0)
		return 1;
	else {
		/* we have plmdata */
		LOG(LOG_DEBUG, "Found PLMINFO");
		add_plm_devicelist_model(devicelist_model);
		working = 0;
		return 0;
	}
}

/**
   \brief A GTK callback to control the spinner
   \param user_data user data
*/

gboolean control_spinner(gpointer user_data)
{
	GtkWidget *spinner;

	spinner = lookup_widget(main_window, "spinner");
	if (spinner == NULL)
		return 1;

	if (working)
		gtk_spinner_start(GTK_SPINNER(spinner));
	else
		gtk_spinner_stop(GTK_SPINNER(spinner));

	return 1;
}

/**
   \brief Queue is empty callback
   \param arg pointer to connection_t
*/

void plm_queue_empty_cb(void *arg)
{
	connection_t *conn = (connection_t *)arg;

	return; /* for now */
}

/**** Actual code goes here ****/

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
	insteon_devdata_t *dd;

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
	dd = (insteon_devdata_t *)dev->localdata;

	switch (com1) {
	case STDCMD_GETVERS:
		switch (com2) {
		case 0x00:
			dev->proto = PROTO_INSTEON_V1;
			dd->proto = com2;
			break;
		case 0x01:
			dev->proto = PROTO_INSTEON_V2;
			break;
			dd->proto = com2;
		case 0x02:
			dev->proto = PROTO_INSTEON_V2CS;
			break;
			dd->proto = com2;
		case 0xFF:
			dev->proto = PROTO_INSTEON_V2CS;
			LOG(LOG_WARNING,
			    "Device %s is i2cs not linked to PLM", dev->uid);
			dd->proto = 0x02;
			if (getinfo_working)
				getinfo_working = GETINFO_UNLINKED;
			plmcmdq_flush(); /* just nuke the queue */
			break;
		}
		break;
	case STDCMD_PING:
		plm_set_hops(dev, flags);
		break;
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
	insteon_devdata_t *dd;

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
	dd = (insteon_devdata_t *)dev->localdata;

	switch (com1) {
	case EXTCMD_RWALDB:
		if (plm_handle_aldb(dev, ext)) {
			plmcmdq_got_data(CMDQ_WAITALDB);
			getld_working = GETLD_DONE;
			/*print_aldb(dev);
			if (aldbfile != NULL && writealdb == 0)
			dump_aldb(dev, aldbfile); */
			exit(0);
		}
		break;
	case STDCMD_PDATA_REQ:
		switch (com2) {
		case EXTCMD2_PDR_REQ:
			dd->productkey[0] = ext[1];
			dd->productkey[1] = ext[2];
			dd->devcat = ext[3];
			dd->subcat = ext[4];
			dd->firmware = ext[5];
			break;
		}
		break;
	}
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
	LOG(LOG_NOTICE, "ALINK Record: dev: %s Group: %0.2X LinkFlags: %0.2X "
	    "link1: %0.2X link2: %0.2X link3: %0.2X",
	    im, data[3], data[2], data[7], data[8], data[9]);

	/* allocate space */
	if (plmaldbsize == 0) {
		plmaldb = safer_malloc(sizeof(aldb_t) * PLMALDBMALLOC);
		plmaldbsize = PLMALDBMALLOC;
	} else if (plmaldbsize == (nrofplmaldb + 1)) {
		plmaldb = realloc(plmaldb, sizeof(aldb_t) *
				  (PLMALDBMALLOC + plmaldbsize));
		plmaldbsize += PLMALDBMALLOC;
	}
	plmaldb[nrofplmaldb].lflags = data[2];
	plmaldb[nrofplmaldb].group = data[3];
	plmaldb[nrofplmaldb].devaddr[0] = data[4];
	plmaldb[nrofplmaldb].devaddr[1] = data[5];
	plmaldb[nrofplmaldb].devaddr[2] = data[6];
	plmaldb[nrofplmaldb].ldata1 = data[7];
	plmaldb[nrofplmaldb].ldata2 = data[8];
	plmaldb[nrofplmaldb].ldata3 = data[9];
	nrofplmaldb++;

	if (plmaldbmorerecords == 0)
		LOG(LOG_NOTICE, "Got last record");
}

/**
   \brief Handle an all-linking completed command
   \param data data recieved
*/

void plm_handle_alink_complete(uint8_t *data)
{
	char im[16];
	uint8_t devaddr[3];
	device_t *dev;
	cmdq_t *cmd;
	cfg_t *db;

	cmd = SIMPLEQ_FIRST(&cmdfifo);

	memcpy(devaddr, data+4, 3);
	addr_to_string(im, devaddr);
	LOG(LOG_NOTICE, "ALINK Complete: dev: %s devcat: %0.2X subcat: %0.2X "
	    "Firmware: %0.2X Group: %0.2X Linktype: %0.2X",
	    im, data[7], data[8], data[9], data[3], data[2]);

	if (memcmp(devaddr, (cmd->cmd)+2, 3) == 0)
		plmcmdq_dequeue();

	if (data[7] == 0 && data[8] == 0)
		return; /* ignore this result */

	db = find_db_entry(data[7], data[8]);
	if (db == NULL) {
		LOG(LOG_WARNING, "Unknown devcat/subcat %0.2X/%0.2X.  Please "
		    "update %s", data[7], data[8], INSTEON_DB_FILE);
		return;
	}
	dev = find_device_byuid(im);
	if (dev == NULL) {
		LOG(LOG_ERROR, "Can't find device!  WTF?");
		return;
	}
	dev->name = strdup(cfg_getstr(db, "name"));
	dev->type = cfg_getint(db, "type");
	dev->subtype = cfg_getint(db, "subtype");
	(void)new_conf_from_dev(cfg, dev);
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
	char *logname = NULL;
	char *idbfile = SYSCONFDIR "/" INSTEON_DB_FILE;
	char *conffile = SYSCONFDIR "/" INSTEONCOLL_CONF_FILE;
	int ch, fd;
	char *host = NULL;
	struct timeval runq = { 0, 500 };
	struct event *ev;

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

	plminfo.daddr[0] = 0x0; /* initialize for use in callback check */

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

	/* Initialize the device table */
	init_devtable(cfg, 0);

	/* Initialize the command fifo */
	SIMPLEQ_INIT(&cmdfifo);

	cfg = parse_conf(conffile);
	cfg_idb = parse_insteondb(idbfile);
	if (cfg_idb == NULL)
		LOG(LOG_ERROR, "No Insteon DB file, cannot build usable conf");

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
			LOG(LOG_FATAL, "Error reading config file, "
			    "gnhastd section");
	}

	parse_devices(cfg);

	hubhtml_username = cfg_getstr(icoll_c, "httpuser");
	hubhtml_password = cfg_getstr(icoll_c, "httppass");
	plmtype = cfg_getint(icoll_c, "plmtype");

	/* Connect to the PLM */
	fd = plmtype_connect(plmtype, cfg_getstr(icoll_c, "device"),
			     cfg_getstr(icoll_c, "hostname"),
			     cfg_getint(icoll_c, "hubport"));

	plm_getinfo();
	working = 1;

	/* setup runq */
	ev = event_new(base, -1, EV_PERSIST, plm_runq, plm_conn);
	event_add(ev, &runq);

	main_window = create_window1();
	update_devicelist_model(devicelist_model);
	gtk_widget_show(main_window);


	gtk_signal_connect(GTK_OBJECT(main_window), "delete_event",
			   GTK_SIGNAL_FUNC(delete_event), NULL);
	gtk_signal_connect(GTK_OBJECT(main_window), "destroy",
			   G_CALLBACK(exit_cb), NULL);


	/* call the event2 loop 4 times a second */
	g_timeout_add(250, run_event_base, NULL);
	g_timeout_add(500, check_plminfo, NULL);
	g_timeout_add(333, control_spinner, NULL);

	gtk_main();
	return 0;
}
