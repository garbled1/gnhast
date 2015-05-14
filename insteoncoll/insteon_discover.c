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

/* Configuration file details */

char *dumpconf = NULL;
cfg_t *cfg, *cfg_idb;
extern cfg_opt_t device_opts[];

cfg_opt_t insteoncoll_opts[] = {
	CFG_STR("device", 0, CFGF_NODEFAULT),
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
connection_t *plm_conn;
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


usage(void)
{
	(void)fprintf(stderr, "Usage %s: [-m <dumpconffile>]"
		      " -f <listfile> -s <device>\n",
	    getprogname());
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
	int done = 0;

	switch (mode) {
	case MODE_GET_DEV_VERS:
		TAILQ_FOREACH(dev, &alldevs, next_all)
			if (dev->proto != PROTO_NONE)
				done++;
		if (done == nrofdevs) {
			mode = MODE_LINK_ALL_C;
			TAILQ_FOREACH(dev, &alldevs, next_all) {
				plm_enq_std(dev, STDCMD_LINKMODE, 0x01,
					    CMDQ_WAITACK|CMDQ_WAITDATA);
				plm_all_link(0x01, 0x75);
				//plm_all_link(0xFF, 0x75);
			}
		}
		break;
	case MODE_LINK_ALL_C:
		TAILQ_FOREACH(dev, &alldevs, next_all) {
			plm_all_link(0x00, 0x01);
			//plm_all_link(0xFF, 0x76);
			plm_enq_std(dev, STDCMD_LINKMODE, 0x01,
				    CMDQ_WAITACK|CMDQ_WAITDATA);
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
			uint8_t com1, uint8_t com2, connection_t *conn)
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
			uint8_t com1, uint8_t com2, uint8_t *ext,
			connection_t *conn)
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
	int timeout = 1, c, error, fd, groupnum = 1, i, nr;
	char *device = NULL, buf[256], head[5];
	char *idbfile = SYSCONFDIR "/" INSTEON_DB_FILE;
	char *conffile = SYSCONFDIR "/" INSTEONCOLL_CONF_FILE;
	struct termios tio;
	struct ev_token_bucket_cfg *ratelim;
	struct timeval rate = { 1, 0 };
	struct timeval runq = { 0, 500 };
	struct event *ev;
	cfg_t *icoll;

	while ((c = getopt(argc, argv, "df:m:s:")) != -1)
		switch (c) {
		case 'd':
			debugmode = 1;
			break;
		case 'f':
			listfile = strdup(optarg);
			break;
		case 'm':
			dumpconf = strdup(optarg);
			break;
		case 's':
			device = optarg;
			break;
		default:
			usage();
		}

	if (device == NULL || listfile == NULL)
		usage();

	/* Initialize the device table */
	init_devtable(cfg, 0);

	/* Initialize the command fifo */
	SIMPLEQ_INIT(&cmdfifo);

	cfg = parse_conf(conffile);
	if (cfg == NULL)
		cfg = cfg_init(options, CFGF_NONE);

	parse_devlist();
	if (nrofdevslist == 0)
		LOG(LOG_FATAL, "No devices in listfile");

	cfg_idb = parse_insteondb(idbfile);
	if (cfg_idb == NULL)
		LOG(LOG_ERROR, "No Insteon DB file, cannot build usable conf");

	/* write the serial device name to the conf file */
	icoll = cfg_getsec(cfg, "insteoncoll");
	if (cfg_getstr(icoll, "device") == NULL)
		cfg_setstr(icoll, "device", device);

	fd = serial_connect(device, B19200, CS8|CREAD|CLOCAL);

	base = event_base_new();
	plm_conn = smalloc(connection_t);
	plm_conn->bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
	plm_conn->type = CONN_TYPE_PLM;
	bufferevent_setcb(plm_conn->bev, plm_readcb, NULL, serial_eventcb,
			  plm_conn);
	bufferevent_enable(plm_conn->bev, EV_READ|EV_WRITE);

	ratelim = ev_token_bucket_cfg_new(2400, 100, 25, 256, &rate);
	bufferevent_set_rate_limit(plm_conn->bev, ratelim);

	plm_getinfo();
	plm_get_engine_versions(plm_conn);
	mode = MODE_GET_DEV_VERS;

	/* setup runq */
	ev = event_new(base, -1, EV_PERSIST, plm_runq, plm_conn);
	event_add(ev, &runq);

	/* loopit */
	event_base_dispatch(base);

	(void)close(fd);
	return 0;
}
