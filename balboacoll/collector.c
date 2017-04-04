/*
 * Copyright (c) 2013, 2014, 2017
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
   \brief Collector for talking to a balboa spa wifi model 50350
*/

#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <netdb.h>
#include <time.h>
#include <signal.h>
#include <event2/dns.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>

#include "common.h"
#include "gnhast.h"
#include "confuse.h"
#include "collcmd.h"
#include "genconn.h"
#include "gncoll.h"
#include "confparser.h"
#include "30303_disc.h"
#include "collector.h"
#include "crc.h"

/** our logfile */
FILE *logfile;
char *dumpconf = NULL;
char *conffile = SYSCONFDIR "/" BALBOACOLL_CONFIG_FILE;;

/* Need the argtable in scope, so we can generate proper commands
   for the server */
extern argtable_t argtable[];

/** The event base */
struct event_base *base;
struct evdns_base *dns_base;
cfg_t *cfg, *gnhastd_c, *balboacoll_c;
struct sockaddr_in bwg_addr;

#define CONN_TYPE_GNHASTD       1
#define CONN_TYPE_BALBOA	2
char *conntype[3] = {
	"none",
	"gnhastd",
	"balboa",
};
connection_t *gnhastd_conn;
connection_t *balboa_conn;

int need_rereg = 0;
int devices_ready = 0;
int nrof_pumps = 0;
int tscale = TSCALE_F;
uint8_t mac_suffix[3] = { 0x0, 0x0, 0x0 };
uint8_t prev_stat[23];
time_t spa_lastupd = NULL;
extern int debugmode;
extern int collector_instance;

/* the message types */
uint8_t mtypes[9][3] = {
	{ 0xFF, 0xAF, 0x13 }, /* BMTR_STATUS_UPDATE */
	{ 0x0A, 0xBF, 0x23 }, /* BMTR_FILTER_CONFIG */
	{ 0x0A, 0xBF, 0x04 }, /* BMTS_CONFIG_REQ */
	{ 0x0A, 0XBF, 0x94 }, /* BMTR_CONFIG_RESP */
	{ 0x0A, 0xBF, 0x22 }, /* BMTS_FILTER_REQ */
	{ 0x0A, 0xBF, 0x11 }, /* BMTS_CONTROL_REQ */
	{ 0x0A, 0xBF, 0x20 }, /* BMTS_SET_TEMP */
	{ 0x0A, 0xBF, 0x21 }, /* BMTS_SET_TIME */
	{ 0x0A, 0xBF, 0x92 }, /* BMTS_SET_WI	FI */
};

/* options setup */

extern cfg_opt_t device_opts[];

cfg_opt_t gnhastd_opts[] = {
	CFG_STR("hostname", "127.0.0.1", CFGF_NONE),
	CFG_INT("port", 2920, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t balboacoll_opts[] = {
	CFG_INT("instance", 1, CFGF_NONE),
	CFG_INT("pumps", 1, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t options[] = {
	CFG_SEC("gnhastd", gnhastd_opts, CFGF_NONE),
	CFG_SEC("device", device_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_SEC("balboacoll", balboacoll_opts, CFGF_NONE),
	CFG_STR("logfile", BALBOACOLL_LOG_FILE, CFGF_NONE),
	CFG_STR("pidfile", BALBOACOLL_PID_FILE, CFGF_NONE),
	CFG_END(),
};

void cb_sigterm(int fd, short what, void *arg);
void cb_shutdown(int fd, short what, void *arg);
void connect_server_cb(int nada, short what, void *arg);

/*** Collector specific code goes here ***/

/**
   \brief Called when the discovery event ends and has found a device
   \param url the url we found
   \param hostname the hostname of the device
   We will get back the IP in the url in the form of http://IP
*/

void d30303_found_cb(char *url, char *hostname)
{
	char *p;

	p = url+7;
	LOG(LOG_DEBUG, "Using IP: %s", p);
	inet_aton(p, &bwg_addr.sin_addr);
	bwg_addr.sin_port = htons(BALBOA_PORT);
	bwg_addr.sin_family = AF_INET;

	/* connect to what we found */
	balboa_conn = smalloc(connection_t);
	balboa_conn->port = BALBOA_PORT;
	balboa_conn->host = strdup(p);
	balboa_conn->type = CONN_TYPE_BALBOA;
	connect_server_cb(0,0, balboa_conn);
}

/**
 * Update the crc value with new data.
 *
 * \param crc      The current crc value.
 * \param data     Pointer to a buffer of \a data_len bytes.
 * \param data_len Number of bytes in the \a data buffer.
 * \return         The updated crc value.
 *****************************************************************************/
crc_t crc_update(crc_t crc, const void *data, size_t data_len)
{
    const unsigned char *d = (const unsigned char *)data;
    unsigned int i;
    bool bit;
    unsigned char c;

    while (data_len--) {
        c = *d++;
        for (i = 0; i < 8; i++) {
            bit = crc & 0x80;
            crc = (crc << 1) | ((c >> (7 - i)) & 0x01);
            if (bit) {
                crc ^= 0x07;
            }
        }
        crc &= 0xff;
    }
    return crc & 0xff;
}


/**
 * Calculate the final crc value.
 *
 * \param crc  The current crc value.
 * \return     The final crc value.
 *****************************************************************************/
crc_t crc_finalize(crc_t crc)
{
    unsigned int i;
    bool bit;

    for (i = 0; i < 8; i++) {
        bit = crc & 0x80;
        crc = (crc << 1) | 0x00;
        if (bit) {
            crc ^= 0x07;
        }
    }
    return (crc ^ 0x02) & 0xff;
}

/**
   \brief Calculate the checksum byte for a balboa message
   \param data data
   \param len length of data
   \return checksum
*/
uint8_t balboa_calc_cs(uint8_t *data, int len)
{
	crc_t crc;

	crc = crc_init();
	crc = crc_update(crc, data, len);
	crc = crc_finalize(crc);
	return (uint8_t)crc;
}

/**
   \brief Figure out which message we recieved
   \param data the data stream
   \param len the length
   \return BALBOA_MTYPES enum, -1 if not found
*/

int find_balboa_mtype(uint8_t *data, size_t len)
{
	int i;

	for (i=0; i < NROF_BMT; i++) {
		if (data[2] == mtypes[i][0] &&
		    data[3] == mtypes[i][1] &&
		    data[4] == mtypes[i][2])
			return i;
	}
	return -1;
}

/**
   \brief Send a config request
*/

void send_config_req(void)
{
	uint8_t data[8];

	data[0] = M_START;
	data[1] = 5;
	data[2] = mtypes[BMTS_CONFIG_REQ][0];
	data[3] = mtypes[BMTS_CONFIG_REQ][1];
	data[4] = mtypes[BMTS_CONFIG_REQ][2];
	data[5] = 0x77; /* chksum is easy here */
	data[6] = M_END;

	bufferevent_write(balboa_conn->bev, data, 7);
}

/**
   \brief Initialize devices
   \param pumps number of pumps
*/

void init_balboa_devs(int pumps)
{
	device_t *dev;
	char uid[256], name[256], macstr[16];
	char *buf2;
	int i;

	/* lets build some basic devices */

	if (mac_suffix[0] == 0x0 && mac_suffix[1] == 0x0 &&
	    mac_suffix[2] == 0x0)
		return;

	sprintf(macstr, "%0.2X%0.2X%0.2X", mac_suffix[0],
		mac_suffix[1], mac_suffix[2]);

	/* first, pumps */
	nrof_pumps = pumps;
	for (i=0; i < pumps; i++) {
		sprintf(uid, "%s-pump%0.2d", macstr, i);
		sprintf(name, "Spa Pump #%0.2d", i);
		generic_build_device(cfg, uid, name, NULL, PROTO_BALBOA,
				     DEVICE_SWITCH, SUBTYPE_TRISTATE, NULL,
				     0, balboa_conn->bev);
	}
	/* the circulation pump */
	sprintf(uid, "%s-circpump", macstr);
	sprintf(name, "Spa Circulation Pump");
	generic_build_device(cfg, uid, name, NULL, PROTO_BALBOA,
			     DEVICE_SWITCH, SUBTYPE_SWITCH, NULL,
			     0, balboa_conn->bev);
	/* the light */
	sprintf(uid, "%s-light", macstr);
	sprintf(name, "Spa Light");
	generic_build_device(cfg, uid, name, NULL, PROTO_BALBOA,
			     DEVICE_SWITCH, SUBTYPE_SWITCH, NULL,
			     0, balboa_conn->bev);
	/* The current temp */
	sprintf(uid, "%s-curtemp", macstr);
	sprintf(name, "Spa Curent Temp");
	generic_build_device(cfg, uid, name, NULL, PROTO_BALBOA,
			     DEVICE_SENSOR, SUBTYPE_TEMP, NULL,
			     TSCALE_F, balboa_conn->bev);
	dev = find_device_byuid(uid);
	if (dev != NULL) {
		tscale = dev->scale;
	}

	/* The settemp */
	sprintf(uid, "%s-settemp", macstr);
	sprintf(name, "Spa Set Temp");
	generic_build_device(cfg, uid, name, NULL, PROTO_BALBOA,
			     DEVICE_SENSOR, SUBTYPE_TEMP, NULL,
			     TSCALE_F, balboa_conn->bev);
	/* The heat mode */
	sprintf(uid, "%s-heatmode", macstr);
	sprintf(name, "Spa Heating Mode");
	generic_build_device(cfg, uid, name, NULL, PROTO_BALBOA,
			     DEVICE_SENSOR, SUBTYPE_TRISTATE, NULL,
			     0, balboa_conn->bev);
	/* The heat state */
	sprintf(uid, "%s-heatstate", macstr);
	sprintf(name, "Spa Heating State");
	generic_build_device(cfg, uid, name, NULL, PROTO_BALBOA,
			     DEVICE_SENSOR, SUBTYPE_TRISTATE, NULL,
			     0, balboa_conn->bev);
	/* The heat state */
	sprintf(uid, "%s-temprange", macstr);
	sprintf(name, "Spa Temperature Range");
	generic_build_device(cfg, uid, name, NULL, PROTO_BALBOA,
			     DEVICE_SENSOR, SUBTYPE_SWITCH, NULL,
			     0, balboa_conn->bev);
	devices_ready = 1;
	/* are we dumping the conf file? */
	if (dumpconf != NULL) {
		LOG(LOG_NOTICE, "Dumping config file to "
		    "%s and exiting", dumpconf);
		dump_conf(cfg, 0, dumpconf);
		exit(0);
	}

	/* initialize the prev register */
	memset(prev_stat, 0, 23);
}


/**
   \brief Parse a config response
   \param data The data
   \param len Length of data
   \return -1 if error

SZ 02 03 04   05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22
1E 0A BF 94   02 14 80 00 15 27 37 EF ED 00 00 00 00 00 00 00 00 00

23 24 25 26 27 28 29 CB
15 27 FF FF 37 EF ED 42

22,23,24 seems to be mac prefix,  27-29 suffix.  25/26 unk
8-13 also full macaddr.
I'm assuming 05 is the number of pumps.  no idea what 06 is.  Wild guesses
at this point.
*/

int parse_config_resp(uint8_t *data, size_t len)
{
	int i;

	/* initialize the mac_suffix */
	if (mac_suffix[0] == 0x0 && mac_suffix[1] == 0x0 &&
	    mac_suffix[2] == 0x0) {
		mac_suffix[0] = data[11];
		mac_suffix[1] = data[12];
		mac_suffix[2] = data[13];
	}

	spa_lastupd = time(NULL);

	LOG(LOG_DEBUG, "config bytes: %0.2X %0.2X %0.2X", data[5], data[6],
	    data[7]);

	/* we assume data[5] is number of pumps. Could be wrong. */
	if (!devices_ready)
		init_balboa_devs((int)data[5]);
	return 0;
}

/**
   \brief Parse a status update
   \param data The data
   \param len Length of data
   \return 1 if error

00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25
MS ML MT MT MT XX F1 CT HH MM F2  X  X  X F3 F4 PP  X CP LF  X  X  X  X  X ST
7E 1D FF AF 13  0  0 64  8 2D  0  0  1  0  0  4  0  0  0  0  0  0  0  0  0 64

26 27 28 29 30
 X  X  X CB ME
 0  0  0  6 7E
*/

int parse_status_update(uint8_t *data, size_t len)
{
	device_t *dev;
	char uid[256], macstr[16];
	int n_tscale = TSCALE_F;
	int upd = 0;
	int i;
	uint8_t val;
	double d;

	/* we got data so update this */
	spa_lastupd = time(NULL);

	/*
	  check if anything changed for quick exit.
	  Because the minute counter will update every minute, we still send
	  data once per minute.  This is intentional.
	*/
	if (memcmp(prev_stat, data+6, 23) == 0)
		return 0;

	sprintf(macstr, "%0.2X%0.2X%0.2X", mac_suffix[0],
		mac_suffix[1], mac_suffix[2]);

	/* figure out if our scale is correct still */
	if (data[14] & 0x01)
		n_tscale = TSCALE_C;
	if (n_tscale != tscale) {
		tscale = n_tscale;
		upd = GNC_UPD_FULL;
	}

	/* First curtemp */
	sprintf(uid, "%s-curtemp", macstr);
	dev = find_device_byuid(uid);
	if (dev == NULL) {
		LOG(LOG_ERROR, "Can't find curtemp");
		return 1;
	}
	d = (double)data[7];
	if (tscale)
		d /= 2.0;
	store_data_dev(dev, DATALOC_DATA, &d);
	if (upd)
		dev->scale = tscale;
	LOG(LOG_DEBUG, "CurTemp changed to %f", d);
	gn_update_device(dev, upd, balboa_conn->bev);

	/* Set Temp */
	sprintf(uid, "%s-settemp", macstr);
	dev = find_device_byuid(uid);
	if (dev == NULL) {
		LOG(LOG_ERROR, "Can't find settemp");
		return 1;
	}
	d = (double)data[25];
	if (tscale)
		d /= 2.0;
	store_data_dev(dev, DATALOC_DATA, &d);
	if (upd)
		dev->scale = tscale;
	LOG(LOG_DEBUG, "SetTemp changed to %f", d);
	gn_update_device(dev, upd, balboa_conn->bev);

	/* Heat Mode (flag 2) */
	sprintf(uid, "%s-heatmode", macstr);
	dev = find_device_byuid(uid);
	if (dev == NULL) {
		LOG(LOG_ERROR, "Can't find heatmode");
		return 1;
	}
	val = data[10] & 0x03;
	if (val == 3)
		val = 2;
	store_data_dev(dev, DATALOC_DATA, &val);
	LOG(LOG_DEBUG, "Heat Mode changed to %d", val);
	gn_update_device(dev, 0, balboa_conn->bev);

	/* Flag 4, heating, temp range */
	sprintf(uid, "%s-heatstate", macstr);
	dev = find_device_byuid(uid);
	if (dev == NULL) {
		LOG(LOG_ERROR, "Can't find heatstate");
		return 1;
	}
	val = (data[15] & 0x30)>>4;
	store_data_dev(dev, DATALOC_DATA, &val);
	LOG(LOG_DEBUG, "Heat State changed to %d", val);
	gn_update_device(dev, 0, balboa_conn->bev);

	sprintf(uid, "%s-temprange", macstr);
	dev = find_device_byuid(uid);
	if (dev == NULL) {
		LOG(LOG_ERROR, "Can't find temprange");
		return 1;
	}
	val = (data[15] & 0x04)>>2;
	store_data_dev(dev, DATALOC_DATA, &val);
	LOG(LOG_DEBUG, "Temp Range changed to %d", val);
	gn_update_device(dev, 0, balboa_conn->bev);

	/* pump status */
	for (i=0; i < nrof_pumps; i++) {
		sprintf(uid, "%s-pump%0.2d", macstr, i);
		dev = find_device_byuid(uid);
		if (dev == NULL) {
			LOG(LOG_ERROR, "Can't find pump %d", i);
			return 1;
		}
		val = (data[16]>>i) & 0x03;
		store_data_dev(dev, DATALOC_DATA, &val);
		LOG(LOG_DEBUG, "Pump %d changed to %d", i, val);
		gn_update_device(dev, 0, balboa_conn->bev);
	}

	/* Circulation pump */
	sprintf(uid, "%s-circpump", macstr);
	dev = find_device_byuid(uid);
	if (dev == NULL) {
		LOG(LOG_ERROR, "Can't find circpump");
		return 1;
	}
	if (data[18] == 0x02)
		val = 1;
	else
		val = 0;
	store_data_dev(dev, DATALOC_DATA, &val);
	LOG(LOG_DEBUG, "Circ Pump changed to %d", val);
	gn_update_device(dev, 0, balboa_conn->bev);

	/* Spa light */
	sprintf(uid, "%s-light", macstr);
	dev = find_device_byuid(uid);
	if (dev == NULL) {
		LOG(LOG_ERROR, "Can't find light");
		return 1;
	}
	if (data[19] == 0x03)
		val = 1;
	else
		val = 0;
	store_data_dev(dev, DATALOC_DATA, &val);
	LOG(LOG_DEBUG, "Light changed to %d", val);
	gn_update_device(dev, 0, balboa_conn->bev);

	/* Phew, done, now update the baseline */
	memcpy(prev_stat, data+6, 23);
	return 0;
}

/**
   \brief balboa read callback
   \param in the bufferevent that fired
   \param arg the connection_t
*/

void balboa_buf_read_cb(struct bufferevent *in, void *arg)
{
	connection_t *conn = (connection_t *)arg;
	size_t len, rlen;
	uint8_t *data, header[2], cs;
	device_t *dev;
	int i, j;
	struct evbuffer *evbuf;

	evbuf = bufferevent_get_input(in);
	len = evbuffer_get_length(evbuf);

	if (len < 2)
		return;  /* no size data yet */

	evbuffer_copyout(evbuf, header, 2);
	LOG(LOG_DEBUG, "Got MSG: %X len %d bytes", header[0], header[1]);
	if (header[0] == M_START)
		rlen = (size_t)header[1] + 2; /* checksum + msg end */
	else
		return; /* eh? */

	data = safer_malloc(rlen);
	evbuffer_remove(evbuf, data, rlen);
	cs = balboa_calc_cs(data+1, rlen-3);

	if (cs != data[rlen-2]) {
		LOG(LOG_ERROR, "Got bad checksum on message, discarding");
		free(data);
		return;
	}

#if 0
	for (j=0; j < rlen; j++)
		printf("%0.2X", data[j]);
	printf("\n");
#endif

	i = find_balboa_mtype(data, rlen);
	if (i == -1) {
		LOG(LOG_DEBUG, "Got unknown message type %0.2X%0.2X%0.2X"
		    " LEN:%d", data[2], data[3], data[4], data[1]);
		free(data);
		return;
	}
	switch (i) {
	case BMTR_CONFIG_RESP:
		parse_config_resp(data, rlen);
		for (j=0; j < rlen; j++)
			printf("%0.2X", data[j]);
		printf("\n");
		break;
	case BMTR_STATUS_UPDATE:
		parse_status_update(data, rlen);
		break;
	}

	free(data);
}

/**
   \brief Event callback used for balboa connection
   \param ev The bufferevent that fired
   \param what why did it fire?
   \param arg pointer to connection_t;
*/

void balboa_connect_event_cb(struct bufferevent *ev, short what, void *arg)
{
	int err;
	connection_t *conn = (connection_t *)arg;

	if (what & BEV_EVENT_CONNECTED) {
		LOG(LOG_DEBUG, "Connected to %s", conntype[conn->type]);
		send_config_req();
	} else if (what & (BEV_EVENT_ERROR|BEV_EVENT_EOF)) {
		if (what & BEV_EVENT_ERROR) {
			err = bufferevent_socket_get_dns_error(ev);
			if (err)
				LOG(LOG_FATAL, "DNS Failure connecting to "
				    "%s: %s", conntype[conn->type],
				    strerror(err));
		}
		LOG(LOG_DEBUG, "Lost connection to %s, closing",
		    conntype[conn->type]);
		bufferevent_disable(ev, EV_READ|EV_WRITE);
		bufferevent_free(ev);
		/* Retry immediately */
		connect_server_cb(0, 0, conn);
	}
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
	if (conn->type == CONN_TYPE_BALBOA)
		bufferevent_setcb(conn->bev, balboa_buf_read_cb, NULL,
				  balboa_connect_event_cb, conn);
	else
		return;
	bufferevent_enable(conn->bev, EV_READ|EV_WRITE);
	bufferevent_socket_connect(conn->bev, (struct sockaddr *)&bwg_addr,
				   sizeof(struct sockaddr_in));
	LOG(LOG_NOTICE, "Attempting to connect to %s @ %s:%d",
	    conntype[conn->type], inet_ntoa(bwg_addr.sin_addr), conn->port);
}

/* Gnhastd connection type routines go here */

/**
   \brief Check if a collector is functioning properly
   \param conn connection_t of collector's gnhastd connection
   \return 1 if OK, 0 if broken
   Generally you set this up with some kind of last update check using
   time(2).  Compare it against the normal update rate, and if there haven't
   been updates in say, 4-6 cycles, return 0.
*/

int collector_is_ok(void)
{
	return(1); /* lie */
}

/**
   \brief Main itself
   \param argc count
   \param arvg vector
   \return int
*/

int main(int argc, char **argv)
{
	struct event *ev;
	extern char *optarg;
	extern int optind;
	int ch, port = -1;
	char *gnhastdserver = NULL;
	char *pidfile;

	/* process command line arguments */
	while ((ch = getopt(argc, argv, "?c:dm:")) != -1)
		switch (ch) {
		case 'c':	/* Set configfile */
			conffile = strdup(optarg);
			break;
		case 'd':	/* debugging mode */
			debugmode = 1;
			break;
		case 'm':
			dumpconf = strdup(optarg);
			break;
		default:
		case '?':	/* you blew it */
			(void)fprintf(stderr, "usage:\n%s [-c configfile]"
				      "[-m dumpconfigfile]\n",
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

	cfg = parse_conf(conffile);

	if (!debugmode)
		logfile = openlog(cfg_getstr(cfg, "logfile"));

	pidfile = cfg_getstr(cfg, "pidfile");
	writepidfile(cfg_getstr(cfg, "pidfile"));

	/* Now, parse the details of connecting to the gnhastd server */

	if (cfg) {
		gnhastd_c = cfg_getsec(cfg, "gnhastd");
		if (!gnhastd_c)
			LOG(LOG_FATAL, "Error reading config file, "
			    "gnhastd section");
	}

	/* Now parse the collector specific config settings */
	if (cfg) {
		balboacoll_c = cfg_getsec(cfg, "balboacoll");
		if (!balboacoll_c)
			LOG(LOG_FATAL, "Error reading config file, "
			    "balboacoll section");
	}

	/* find the spa */
	d30303_setup(BALBOA_MAC_PREFIX, NULL);

	gnhastd_conn = smalloc(connection_t);
	gnhastd_conn->cname = strdup(COLLECTOR_NAME);
	if (port != -1)
		gnhastd_conn->port = port;
	else
		gnhastd_conn->port = cfg_getint(gnhastd_c, "port");
	if (gnhastdserver != NULL)
		gnhastd_conn->host = gnhastdserver;
	else
		gnhastd_conn->host = cfg_getstr(gnhastd_c, "hostname");
	gnhastd_conn->type = CONN_TYPE_GNHASTD;
	/* cheat, and directly call the timer callback
	   This sets up a connection to the server. */
	generic_connect_server_cb(0, 0, gnhastd_conn);
	collector_instance = cfg_getint(balboacoll_c, "instance");
	gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);

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

	/* go forth and destroy */
	event_base_dispatch(base);

	/* Close out the log, and bail */
	closelog();
	delete_pidfile();
	return(0);
}

