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
extern int plmaldbmorerecords;
extern char *conntype[];

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
	CFG_INT("plmport", 9761, CFGF_NONE),
	CFG_INT("httpport", 25105, CFGF_NONE),
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
char *aldbfile = NULL;
int writealdb = 0;
int plmaldbdump = 0;
aldb_t *plmaldb;
int plmaldbsize = 0;
int nrofplmaldb = 0;

#define PLMALDBMALLOC		16

/* our state of operation */
int mode;
#define MODE_NONE		0
#define MODE_GET_DEV_VERS	1
#define MODE_LINK_ALL_C		2
#define MODE_LINK_ALL_R		3
#define MODE_GET_DEV_INFO	4

extern SIMPLEQ_HEAD(fifohead, _cmdq_t) cmdfifo;


usage(void)
{
	(void)fprintf(stderr, "Usage %s: -a <devaddr> -s <device> "
		      "[-f <aldbfile>]\n", getprogname());
	(void)fprintf(stderr, "Usage %s: -a <devaddr> -s <device> "
		      " -w -f <aldbfile>\n", getprogname());
	exit(1);
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
   \brief Print out the ALDB
   \param dev device to print aldb for
*/
void print_aldb(device_t *dev)
{
	int i;
	char da[16];
	insteon_devdata_t *dd = (insteon_devdata_t *)dev->localdata;

	addr_to_string(da, dd->daddr);
	LOG(LOG_NOTICE, "Dumping ALDB for device %s %d records", da,
	    dd->aldblen);
	for (i = 0; i < dd->aldblen && i < ALDB_MAXSIZE; i++) {
		addr_to_string(da, dd->aldb[i].devaddr);
		LOG(LOG_NOTICE, "Link %s GRP: %0.2X OnLvl: %0.2X Ramp: %0.2X "
		    "D3: %0.2X Flg: %0.2X", da, dd->aldb[i].group,
		    dd->aldb[i].ldata1, dd->aldb[i].ldata2,
		    dd->aldb[i].ldata3, dd->aldb[i].lflags);
	}
}

/**
   \brief Dump ALDB to a file
   \param dev device to dump aldb of
   \param filename filename to dump to
*/
void dump_aldb(device_t *dev, char *filename)
{
	int i;
	char da[16];
	insteon_devdata_t *dd = (insteon_devdata_t *)dev->localdata;
	FILE *f;

	f = fopen(filename, "w");
	if (f == NULL) {
		LOG(LOG_ERROR, "Cannot open %s for writing", filename);
		return;
	}

	addr_to_string(da, dd->daddr);
	LOG(LOG_NOTICE, "Dumping ALDB for device %s %d records to %s", da,
	    dd->aldblen, filename);

	for (i = 0; i < dd->aldblen && i < ALDB_MAXSIZE; i++) {
		addr_to_string(da, dd->aldb[i].devaddr);
		fprintf(f, "%s %0.2X %0.2X %0.2X %0.2X %0.2X\n",
			da, dd->aldb[i].group,
			dd->aldb[i].ldata1, dd->aldb[i].ldata2,
			dd->aldb[i].ldata3, dd->aldb[i].lflags);
	}
	fclose(f);
}

/**
   \brief Parse an ALDB record file created by dump_aldb
   \param dev device to load ALDB record into
   \param filename filename to parse
*/
void parse_aldbfile(device_t *dev, char *filename)
{
	int i, lines;
	insteon_devdata_t *dd = (insteon_devdata_t *)dev->localdata;
	FILE *fp;
	char *buf, *lbuf;
	size_t len;
	unsigned int a,b,c,d,e,f,g,h;

	fp = fopen(filename, "r");
	if (fp == NULL) {
		LOG(LOG_ERROR, "Cannot open %s for reading", filename);
		return;
	}
	lines = 0;
	while ((buf = fgetln(fp, &len)))
	       lines++;
	rewind(fp);
	i = 0;
	if (lines > ALDB_MAXSIZE)
		LOG(LOG_FATAL, "ALDB record file %s too big", filename);
	dd->aldblen = lines;

	lbuf = NULL;
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
		dd->aldb[i].addr = 0x0FFF - (i*8);
/*		sscanf(buf, "%X.%X.%X %X %X %X %X %X", &dd->aldb[i].devaddr[0],
		       &dd->aldb[i].devaddr[1], &dd->aldb[i].devaddr[2],
		       &dd->aldb[i].group, &dd->aldb[i].ldata1,
		       &dd->aldb[i].ldata2, &dd->aldb[i].ldata3,
		       &dd->aldb[i].lflags);*/
		sscanf(buf, "%X.%X.%X %X %X %X %X %X",
		       &a, &b, &c, &d, &e, &f, &g, &h);
		dd->aldb[i].devaddr[0] = a;
		dd->aldb[i].devaddr[1] = b;
		dd->aldb[i].devaddr[2] = c;
		dd->aldb[i].group = d;
		dd->aldb[i].ldata1 = e;
		dd->aldb[i].ldata2 = f;
		dd->aldb[i].ldata3 = g;
		dd->aldb[i].lflags = h;
		i++;

		if (lbuf != NULL) {
			free(lbuf);
			lbuf = NULL;
		}
	}
	fclose(fp);
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
			LOG(LOG_WARNING, "Device %s is i2cs not linked to PLM");
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
			print_aldb(dev);
			if (aldbfile != NULL && writealdb == 0)
				dump_aldb(dev, aldbfile);
			exit(0);
		}
		break;
	}
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
	int c, fd, x, port = 9761;
	char *device = NULL, *devaddr;
	char *host = NULL;
	char *idbfile = SYSCONFDIR "/" INSTEON_DB_FILE;
	char *conffile = SYSCONFDIR "/" INSTEONCOLL_CONF_FILE;
	struct ev_token_bucket_cfg *ratelim;
	struct timeval rate = { 1, 0 };
	struct timeval runq = { 0, 500 };
	struct event *ev;
	device_t *dev;
	insteon_devdata_t *dd;

	devaddr = NULL;
	while ((c = getopt(argc, argv, "a:df:h:n:ps:w")) != -1)
		switch (c) {
		case 'a':
			devaddr = strdup(optarg);
			break;
		case 'p':
			plmaldbdump = 1;
			break;
		case 'd':
			debugmode = 1;
			break;
		case 'f':
			aldbfile = strdup(optarg);
			break;
		case 'h':
			host = strdup(optarg);
			break;
		case 'n':
			port = atoi(optarg);
			break;
		case 's':
			device = optarg;
			break;
		case 'w':
			writealdb = 1;
			break;
		default:
			usage();
		}

	if ((device == NULL && host == NULL) ||
	    (devaddr == NULL && plmaldbdump == 0))
		usage();

	if (writealdb && aldbfile == NULL)
		usage();

	/* Initialize the device table */
	init_devtable(cfg, 0);

	/* convert the devaddr to a device */
	if (devaddr != NULL) {
		dev = smalloc(device_t);
		dd = smalloc(insteon_devdata_t);
		x = strtol(devaddr, NULL, 16);
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
		free(devaddr);
	}

	/* Initialize the command fifo */
	SIMPLEQ_INIT(&cmdfifo);

	cfg = parse_conf(conffile);

	cfg_idb = parse_insteondb(idbfile);
	if (cfg_idb == NULL)
		LOG(LOG_ERROR, "No Insteon DB file, cannot build usable conf");

	if (device != NULL) {
		fd = plmtype_connect(PLM_TYPE_SERIAL, device, NULL, -1);
	} else if (port == 9761) {
		fd = plmtype_connect(PLM_TYPE_HUBPLM, NULL, host, port);
	} else {
		fd = plmtype_connect(PLM_TYPE_HUBHTTP, NULL, host, port);
	}

	plm_getinfo();

	if (writealdb) {
		parse_aldbfile(dev, aldbfile);
		print_aldb(dev);
		plm_write_aldb(dev);
	}
	if (plmaldbdump)
		plm_getplm_aldb(0);
	else
		plm_req_aldb(dev);

	/* setup runq */
	ev = event_new(base, -1, EV_PERSIST, plm_runq, plm_conn);
	event_add(ev, &runq);

	/* loopit */
	event_base_dispatch(base);

	(void)close(fd);
	return 0;
}
