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
#include <sys/queue.h>
#include <event2/dns.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>

#include "gnhast.h"
#include "common.h"
#include "confparser.h"
#include "genconn.h"
#include "confuse.h"
#include "insteon.h"

extern int errno;
extern int debugmode;
extern TAILQ_HEAD(, _device_t) alldevs;
extern int nrofdevs;
extern char *conntype[];
extern char *hubhtml_username;
extern char *hubhtml_password;
extern int plmtype;

/* Configuration file details */

char *conffile; /* placeholder */
char *dumpconf = NULL;
cfg_t *cfg, *cfg_idb;
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
int need_rereg = 0;
connection_t *plm_conn;
connection_t *gnhastd_conn;
uint8_t plm_addr[3];

/* our state of operation */
int mode;
#define MODE_NONE		0
#define MODE_GET_DEV_VERS	1
#define MODE_LINK_ALL_C		2
#define MODE_LINK_ALL_R		3
#define MODE_GET_DEV_INFO	4

char *listfile = NULL;
int nrofdevslist = 0;
extern SIMPLEQ_HEAD(fifohead, _cmdq_t) cmdfifo;
extern SIMPLEQ_HEAD(workhead, _workq_t) workfifo;

usage(void)
{
	(void)fprintf(stderr, "Usage:\n\n");
	(void)fprintf(stderr, "For Serial device:\n");
	(void)fprintf(stderr, "%s: [-m <dumpconffile>]"
		      " -f <listfile> -s <device>\n",
	    getprogname());
	(void)fprintf(stderr, "For Hub as PLM (port should be 9761):\n");
	(void)fprintf(stderr, "%s: [-m <dumpconffile>] -f <listfile>"
		      " -h <hostname of hub> -n <portnum>\n", getprogname());
	(void)fprintf(stderr, "For Hub as HTTP (port 25105):\n");
	(void)fprintf(stderr, "%s: [-m <dumpconffile>] -f <listfile>"
		      " -h <hostname of hub> -n <portnum>"
		      " -u <username> -P <password>\n", getprogname());
	exit(1);
}

void
parse_devlist(void)
{
	FILE *fp;
	char *buf, *lbuf;
	size_t len;
	int i, x;
	device_t *dev;
	insteon_devdata_t *dd;

	lbuf = NULL;
	fp = fopen(listfile, "r");
	while ((buf = fgetln(fp, &len)))
	       nrofdevslist++;
	rewind(fp);
	i = 0;

	while ((buf = fgetln(fp, &len))) {
		if (buf[len - 1] == '\n')
			buf[len - 1] = '\0';
		else {
			if ((lbuf = (char *)malloc(len + 1)) == NULL)
				err(1, NULL);
			memcpy(lbuf, buf, len);
			lbuf[len] = '\0';
			buf = lbuf;
		}
		dev = smalloc(device_t);
		dd = smalloc(insteon_devdata_t);
		x = strtol(buf, NULL, 16);
		dd->daddr[0] = ((x&0xff0000)>>16);
		dd->daddr[1] = ((x&0x00ff00)>>8);
		dd->daddr[2] = (x&0x0000ff);
		dd->hopflag = 255;
		dev->localdata = dd;
		dev->uid = safer_malloc(12);
		dev->loc = safer_malloc(12);
		addr_to_string(dev->uid, dd->daddr);
		addr_to_string(dev->loc, dd->daddr);
		insert_device(dev);
		LOG(LOG_NOTICE,"device from listfile #%d 0x%s %X.%X.%X", i,
		    dev->uid, dd->daddr[0], dd->daddr[1], dd->daddr[2]);
		i++;

		if (lbuf != NULL) {
			free(lbuf);
			lbuf = NULL;
		}
	}
	fclose(fp);
}

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
   \brief Request the engine versions
   \param conn the connection_t
*/

void plm_get_engine_versions(connection_t *conn)
{
	device_t *dev;

	TAILQ_FOREACH(dev, &alldevs, next_all)
		plm_enq_std(dev, STDCMD_GETVERS, 0x00, CMDQ_WAITACKDATA);
}


/**
   \brief Queue is empty callback
   \param arg pointer to connection_t
*/

void plm_queue_empty_cb(void *arg)
{
	connection_t *conn = (connection_t *)arg;
	device_t *dev;
	int done = 0, i, grpn;
	cfg_t *db;
	insteon_devdata_t *dd;

	switch (mode) {
	case MODE_GET_DEV_VERS:
		TAILQ_FOREACH(dev, &alldevs, next_all)
			if (dev->proto != PROTO_NONE)
				done++;
		if (done == nrofdevs) {
			/* set it to link as resp next */
			mode = MODE_LINK_ALL_C;
			TAILQ_FOREACH(dev, &alldevs, next_all) {
				plm_enq_std(dev, STDCMD_LINKMODE, 0x01,
					    CMDQ_WAITACK|CMDQ_WAITDATA);
				/* Link the PLM to the device as a master */
				LOG(LOG_NOTICE, "Link as master");
				plm_all_link(0x01, 0x75);
				//plm_all_link(0xFF, 0x75);
			}
		}
		break;
	case MODE_LINK_ALL_C:
		TAILQ_FOREACH(dev, &alldevs, next_all) {
			/* Now link the PLM as a responder on group 1 */
			LOG(LOG_NOTICE, "Link as responder on group 1");
			plm_all_link(0x00, 0x01);
			plm_enq_std(dev, STDCMD_LINKMODE, 0x01,
				    CMDQ_WAITACK|CMDQ_WAITDATA);
			dd = (insteon_devdata_t *)dev->localdata;
			db = find_db_entry(dd->devcat, dd->subcat);
			if (db == NULL)
				continue;
			/* assign ourselves to all the default groups */
			for (i=0; i < cfg_size(db, "defgroups"); i++) {
				grpn = cfg_getnint(db, "defgroups", i);
				LOG(LOG_NOTICE,
				    "Linking as responder to group %d", grpn);
				plm_all_link(0x00, grpn);
				plm_enq_std(dev, STDCMD_LINKMODE, grpn,
					    CMDQ_WAITACK|CMDQ_WAITDATA);
				plm_enq_std(dev, GRPCMD_ASSIGN_GROUP, grpn,
					    CMDQ_WAITACK|CMDQ_WAITDATA);
			}
		}
		mode = MODE_LINK_ALL_R;
		break;
	case MODE_LINK_ALL_R:
		TAILQ_FOREACH(dev, &alldevs, next_all)
			plm_enq_std(dev, STDCMD_PDATA_REQ, 0x00,
				    CMDQ_WAITACKEXT|CMDQ_WAITDATA);
		mode = MODE_GET_DEV_INFO;
		break;
	case MODE_GET_DEV_INFO:
		if (dumpconf != NULL) {
			dump_conf(cfg, 0, dumpconf);
			LOG(LOG_NOTICE, "Wrote conf file %s", dumpconf);
		}
		LOG(LOG_NOTICE, "Work complete, exiting");
		exit(0);
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
	device_t *dev;
	cmdq_t *cmd;
	cfg_t *db, *devconf;
	insteon_devdata_t *dd;

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
	/* get the devdata and store the subcat/cat */
	dd = (insteon_devdata_t *)dev->localdata;
	dd->devcat = data[7];
	dd->subcat = data[8];
	devconf = new_conf_from_dev(cfg, dev);
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

	switch (com1) {
	case STDCMD_GETVERS:
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
			LOG(LOG_WARNING, "Device %s is i2cs not linked to "
			    "PLM", fa);
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
   \brief main
*/

int
main(int argc, char *argv[])
{
	int timeout = 1, c, error, fd, groupnum = 1, i, nr, port = 9761;
	char *host = NULL;
	char *device = NULL, buf[256], head[5];
	char *idbfile = SYSCONFDIR "/" INSTEON_DB_FILE;
	char *conffile = SYSCONFDIR "/" INSTEONCOLL_CONF_FILE;
	struct termios tio;
	struct timeval runq = { 0, 5000 };
	struct timeval workq = { 1, 50 };
	struct event *ev;
	cfg_t *icoll;
	cfg_opt_t *a;

	while ((c = getopt(argc, argv, "df:h:m:n:s:U:P:")) != -1)
		switch (c) {
		case 'd':
			debugmode = 1;
			break;
		case 'f':
			listfile = strdup(optarg);
			break;
		case 'h':
			host = strdup(optarg);
			break;
		case 'n':
			port = atoi(optarg);
			break;
		case 'm':
			dumpconf = strdup(optarg);
			break;
		case 's':
			device = optarg;
			break;
		case 'U':
			hubhtml_username = strdup(optarg);
			break;
		case 'P':
			hubhtml_password = strdup(optarg);
			break;
		default:
			usage();
		}

	if ((device == NULL && host == NULL) || listfile == NULL)
		usage();

	/* Initialize the event system */
	base = event_base_new();
	dns_base = evdns_base_new(base, 1);

	/* Initialize the device table */
	init_devtable(cfg, 0);

	/* Initialize the command fifo */
	SIMPLEQ_INIT(&cmdfifo);
	SIMPLEQ_INIT(&workfifo);

	cfg = parse_conf(conffile);
	if (cfg == NULL)
		cfg = cfg_init(options, CFGF_NONE);

	parse_devlist();
	if (nrofdevslist == 0)
		LOG(LOG_FATAL, "No devices in listfile");

	cfg_idb = parse_insteondb(idbfile);
	if (cfg_idb == NULL)
		LOG(LOG_ERROR, "No Insteon DB file, cannot build usable conf");

	/* Connect to the PLM and save that in the conf */

	icoll = cfg_getsec(cfg, "insteoncoll");

	if (device != NULL) {
		plmtype = PLM_TYPE_SERIAL;
		fd = plmtype_connect(PLM_TYPE_SERIAL, device, NULL, -1);
		cfg_setstr(icoll, "device", device);
	} else if (port == 9761) {
		plmtype = PLM_TYPE_HUBPLM;
		fd = plmtype_connect(PLM_TYPE_HUBPLM, NULL, host, port);
		cfg_setstr(icoll, "hostname", host);
		cfg_setint(icoll, "hubport", port);
	} else {
		plmtype = PLM_TYPE_HUBHTTP;
		if (hubhtml_username == NULL || hubhtml_password == NULL)
			usage();
		fd = plmtype_connect(PLM_TYPE_HUBHTTP, NULL, host, port);
		cfg_setstr(icoll, "hostname", host);
		cfg_setint(icoll, "hubport", port);
		cfg_setstr(icoll, "httpuser", hubhtml_username);
		cfg_setstr(icoll, "httppass", hubhtml_password);
	}
	cfg_setint(icoll, "plmtype", plmtype);

	a = cfg_getopt(icoll, "plmtype");
	cfg_opt_set_print_func(a, conf_print_plmtype);

	plm_getinfo();
	plm_get_engine_versions(plm_conn);
	mode = MODE_GET_DEV_VERS;

	/* setup runq */
	ev = event_new(base, -1, EV_PERSIST, plm_runq, plm_conn);
	event_add(ev, &runq);
	ev = event_new(base, -1, EV_PERSIST, plm_run_workq, plm_conn);
	event_add(ev, &workq);

	/* loopit */
	event_base_dispatch(base);

	(void)close(fd);
	return 0;
}
