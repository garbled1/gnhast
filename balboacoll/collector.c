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
   \file balboacoll/collector.c
   \author Tim Rightnour
   \brief Collector for talking to a balboa spa wifi model 50350
   Much help from:
   https://gist.github.com/ccutrer/ba945ac2ff9508d9e151556b572f2503
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

#define GNHASTD_MIN_PROTO_VERS	0x11
extern int min_proto_version;

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
spaconfig_t spacfg;
uint8_t mac_suffix[3] = { 0x0, 0x0, 0x0 };
uint8_t prev_stat[23];
time_t spa_lastupd = NULL;
extern int debugmode;
extern int collector_instance;

/* the message types */
uint8_t mtypes[14][3] = {
	{ 0xFF, 0xAF, 0x13 }, /* BMTR_STATUS_UPDATE */
	{ 0x0A, 0xBF, 0x23 }, /* BMTR_FILTER_CONFIG */
	{ 0x0A, 0xBF, 0x04 }, /* BMTS_CONFIG_REQ */
	{ 0x0A, 0XBF, 0x94 }, /* BMTR_CONFIG_RESP */
	{ 0x0A, 0xBF, 0x22 }, /* BMTS_FILTER_REQ */
	{ 0x0A, 0xBF, 0x11 }, /* BMTS_CONTROL_REQ */
	{ 0x0A, 0xBF, 0x20 }, /* BMTS_SET_TEMP */
	{ 0x0A, 0xBF, 0x21 }, /* BMTS_SET_TIME */
	{ 0x0A, 0xBF, 0x92 }, /* BMTS_SET_WIFI */
	{ 0x0A, 0xBF, 0x22 }, /* BMTS_PANEL_REQ */
	{ 0x0A, 0XBF, 0x27 }, /* BMTS_SET_TSCALE */
	{ 0x0A, 0xBF, 0x2E }, /* BMTR_PANEL_RESP */
	{ 0x0A, 0xBF, 0x24 }, /* BMTR_PANEL_NOCLUE1 */
	{ 0x0A, 0XBF, 0x25 }, /* BMTR_PANEL_NOCLUE2 */
};
double tmin[2][2] = {
	{ 50.0, 10.0 }, /* low F, C */
	{ 80.0, 26.0 }, /* high F, C */
};
double tmax[2][2] = {
	{ 80.0, 26.0 }, /* low F, C */
	{ 104.0, 40.0 }, /* high F, C */
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

/*** Checksum code ***/

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

/*** Collector callbacks go here ***/

/**
   \brief Called when a switch chg command occurs
   \param dev device that got updated
   \param state new state (on/off)
   \param arg pointer to client_t

   The control mechanisim is a toggle button, so we have to potentially
   cycle through, not just set a value.
*/

void coll_chg_switch_cb(device_t *dev, int state, void *arg)
{
	char buf[16];
	int i, j, iter;
	uint8_t data[12];
	uint8_t curval, cs;

	if (dev->loc == NULL)
		return; /* not a changeable value */

	/* setup things that don't change */
	data[0] = M_START;
	data[1] = 7;
	data[2] = mtypes[BMTS_CONTROL_REQ][0];
	data[3] = mtypes[BMTS_CONTROL_REQ][1];
	data[4] = mtypes[BMTS_CONTROL_REQ][2];
	data[6] = 0x00; /* who knows? */
	data[8] = M_END;

	/* there are only a few, just iterate */

	/* The lights */
	if (strcmp(dev->loc, "l0") == 0 ||
	    strcmp(dev->loc, "l1") == 0) { /* light */
		get_data_dev(dev, DATALOC_DATA, &curval);
		if (curval == state)
			return;
		if (strcmp(dev->loc, "l0") == 0)
			data[5] = C_LIGHT1;
		else
			data[5] = C_LIGHT2;
		data[7] = cs = balboa_calc_cs(data+1, 6);
		bufferevent_write(balboa_conn->bev, data, 9);
		return;
	}
	/* The pumps */
	for (i=0; i < MAX_PUMPS; i++) {
		get_data_dev(dev, DATALOC_DATA, &curval);
		if (curval == state)
			return;
		/* this madness gives us the # of pushes to cycle around */
		for (iter=0; iter < 3; iter++)
			if (state == (curval+iter)%3)
				break;

		sprintf(buf, "p%0.2d", i);
		if (strcmp(dev->loc, buf) == 0) {
			for (j=0; j < iter; j++) {
				/* 4 is 0, 5 is 2, presume 6 is 3? */
				data[5] = C_PUMP1 + i;
				data[7] = cs = balboa_calc_cs(data+1, 6);
				bufferevent_write(balboa_conn->bev, data, 9);
			}
			return;
		}
	}
	/* Heatmode */
	if (strcmp(dev->loc, "hm") == 0) {
		get_data_dev(dev, DATALOC_DATA, &curval);
		if (curval == state)
			return;
		data[5] = C_HEATMODE;
		data[7] = cs = balboa_calc_cs(data+1, 6);
		bufferevent_write(balboa_conn->bev, data, 9);
		return;
	}
	/* TempRange */
	if (strcmp(dev->loc, "tr") == 0) {
		get_data_dev(dev, DATALOC_DATA, &curval);
		if (curval == state)
			return;
		data[5] = C_TEMPRANGE;
		data[7] = cs = balboa_calc_cs(data+1, 6);
		bufferevent_write(balboa_conn->bev, data, 9);
		return;
	}

	/* The AUX devices */
	if (strcmp(dev->loc, "a0") == 0 ||
	    strcmp(dev->loc, "a1") == 0) { /* Aux 1 & 2 */
		get_data_dev(dev, DATALOC_DATA, &curval);
		if (curval == state)
			return;
		if (strcmp(dev->loc, "a0") == 0)
			data[5] = C_AUX1;
		else
			data[5] = C_AUX2;
		data[7] = cs = balboa_calc_cs(data+1, 6);
		bufferevent_write(balboa_conn->bev, data, 9);
		return;
	}

	/* The mister */
	if (strcmp(dev->loc, "mi") == 0) {
		get_data_dev(dev, DATALOC_DATA, &curval);
		if (curval == state)
			return;
		data[5] = C_MISTER;
		data[7] = cs = balboa_calc_cs(data+1, 6);
		bufferevent_write(balboa_conn->bev, data, 9);
		return;
	}

	/* The blower (quad state) */
	if (strcmp(dev->loc, "bl") == 0) {
		get_data_dev(dev, DATALOC_DATA, &curval);
		if (curval == state)
			return;
		/* this madness gives us the # of pushes to cycle around */
		for (iter=0; iter < 4; iter++)
			if (state == (curval+iter)%4)
				break;
		for (j=0; j < iter; j++) {
			data[5] = C_BLOWER;
			data[7] = cs = balboa_calc_cs(data+1, 6);
			bufferevent_write(balboa_conn->bev, data, 9);
		}
		return;
	}
}

/**
   \brief Called when a temp chg command occurs
   \param dev device that got updated
   \param temp new set temp
   \param arg pointer to client_t
*/

void coll_chg_temp_cb(device_t *dev, double temp, void *arg)
{
	uint8_t val, data[8], cs;

	if (temp < tmin[spacfg.trange][spacfg.tscale] ||
	    temp > tmax[spacfg.trange][spacfg.tscale]) {
		LOG(LOG_ERROR, "Attempt to set out of bounds temp %f", temp);
		return;
	}
	val = (uint8_t)temp; /* close enough */
	if (spacfg.tscale == TSCALE_C)
		val *= 2;
	data[0] = M_START;
	data[1] = 6;
	data[2] = mtypes[BMTS_SET_TEMP][0];
	data[3] = mtypes[BMTS_SET_TEMP][1];
	data[4] = mtypes[BMTS_SET_TEMP][2];
	data[5] = val;
	data[6] = cs = balboa_calc_cs(data+1, 5);
	data[7] = M_END;
	bufferevent_write(balboa_conn->bev, data, 8);
	return;
}

/**
   \brief Called when a chg command occurs
   \param dev device that got updated
   \param arg pointer to client_t
*/
void coll_chg_cb(device_t *dev, void *arg)
{
	uint8_t state;
	double d;

	switch (dev->subtype) {
	case SUBTYPE_TRISTATE:
	case SUBTYPE_SWITCH:
		get_data_dev(dev, DATALOC_CHANGE, &state);
                coll_chg_switch_cb(dev, state, arg);
                break;
	case SUBTYPE_TEMP:
		get_data_dev(dev, DATALOC_CHANGE, &d);
		coll_chg_temp_cb(dev, d, arg);
		break;
	default:
		LOG(LOG_ERROR, "Got unhandled chg for subtype %d",
		    dev->subtype);
	}
	return;
}



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
   \brief Send a panel request
   \param ba byte 1
   \param bb byte 2

      0001020304 0506070809101112
0,1 - 7E0B0ABF2E 0A0001500000BF7E
2,0 - 7E1A0ABF24 64DC140042503230303047310451800C6B010A0200F97E
4,0 - 7E0E0ABF25 120432635068290341197E

*/

void send_panel_req(int ba, int bb)
{
	uint8_t data[10];

	data[0] = M_START;
	data[1] = 8;
	data[2] = mtypes[BMTS_PANEL_REQ][0];
	data[3] = mtypes[BMTS_PANEL_REQ][1];
	data[4] = mtypes[BMTS_PANEL_REQ][2];
	data[5] = (uint8_t)ba;
	data[6] = 0;
	data[7] = (uint8_t)bb;
	data[8] = balboa_calc_cs(data+1, 7);
	data[9] = M_END;

	bufferevent_write(balboa_conn->bev, data, 10);
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
   \brief Callback to just send a config req every so often as a keepalive
   \param fd ignored
   \param what ignored
   \param arg ignored
*/

void cb_get_spaconfig(int fd, short what, void *arg)
{
	send_config_req();
}

/**
   \brief Attempt to rescue the connection to the balboa device
   Sometimes the device seems to just stop talking to us. If it does so,
   try sending it something just to wake it up.
*/

void rescue_balboa_conn(void)
{
	send_config_req();
}



/**
   \brief Initialize devices
   \param pumps number of pumps
*/

void init_balboa_devs(void)
{
	device_t *dev;
	char uid[256], name[256], macstr[16], loc[16];
	char *buf2;
	char **tags;
	int i;

	/* lets build some basic devices */

	/* is our macsuffix set? */
	if (mac_suffix[0] == 0x0 && mac_suffix[1] == 0x0 &&
	    mac_suffix[2] == 0x0)
		return;

	/* If all three config elements are ready, we can build devs */
	if (spacfg.cfgdone[CFGDONE_CONF] == 0 ||
	    spacfg.cfgdone[CFGDONE_PANEL] == 0 ||
	    spacfg.cfgdone[CFGDONE_STATUS] == 0)
		return;

	sprintf(macstr, "%0.2X%0.2X%0.2X", mac_suffix[0],
		mac_suffix[1], mac_suffix[2]);

	tags = build_tags(6, "device_source", "spa",
			  "device_manufacturer", "balboa",
			  "device_model", "unset");
	/* first, pumps */
	for (i=0; i < MAX_PUMPS; i++) {
		if (spacfg.pump_array[i]) {
			sprintf(uid, "%s-pump%0.2d", macstr, i);
			sprintf(name, "Spa Pump #%0.2d", i);
			sprintf(loc, "p%0.2d", i);
			generic_build_device(cfg, uid, name, NULL,
					     PROTO_POOL,
					     DEVICE_SWITCH, SUBTYPE_TRISTATE,
					     loc, 0, tags, 6,
					     gnhastd_conn->bev);
		}
	}
	/* the circulation pump */
	if (spacfg.circpump) {
		sprintf(uid, "%s-circpump", macstr);
		sprintf(name, "Spa Circulation Pump");
		generic_build_device(cfg, uid, name, NULL, PROTO_POOL,
				     DEVICE_SWITCH, SUBTYPE_SWITCH, NULL,
				     0, tags, 6, gnhastd_conn->bev);
	}
	/* the lights */
	for (i=0; i < 2; i++) {
		if (spacfg.light_array[i]) {
			sprintf(uid, "%s-light%0.2d", macstr, i);
			sprintf(name, "Spa Light #%0.2d", i);
			sprintf(loc, "l%d", i);
			generic_build_device(cfg, uid, name, NULL,
					     PROTO_LIGHT, DEVICE_SWITCH,
					     SUBTYPE_SWITCH, loc, 0,
					     tags, 6, gnhastd_conn->bev);
		}
	}
	/* the Aux devices */
	for (i=0; i < 2; i++) {
		if (spacfg.aux_array[i]) {
			sprintf(uid, "%s-aux%0.2d", macstr, i);
			sprintf(name, "Spa Aux #%0.2d", i);
			sprintf(loc, "a%d", i);
			generic_build_device(cfg, uid, name, NULL,
					     PROTO_POOL, DEVICE_SWITCH,
					     SUBTYPE_SWITCH, loc, 0,
					     tags, 6, gnhastd_conn->bev);
		}
	}
	/* the blower, needs a new type XXX */
	/* the mister */
	if (spacfg.mister) {
		sprintf(uid, "%s-mister", macstr);
		sprintf(name, "Spa Mister");
		generic_build_device(cfg, uid, name, NULL, PROTO_POOL,
				     DEVICE_SWITCH, SUBTYPE_SWITCH, "mi", 0,
				     tags, 6, gnhastd_conn->bev);
	}
	/* The current temp */
	sprintf(uid, "%s-curtemp", macstr);
	sprintf(name, "Spa Curent Temp");
	generic_build_device(cfg, uid, name, NULL, PROTO_POOL,
			     DEVICE_SENSOR, SUBTYPE_TEMP, NULL,
			     spacfg.tscale, tags, 6, gnhastd_conn->bev);
	dev = find_device_byuid(uid);
	if (dev != NULL) {
		spacfg.tscale = dev->scale;
	}

	/* The settemp */
	sprintf(uid, "%s-settemp", macstr);
	sprintf(name, "Spa Set Temp");
	generic_build_device(cfg, uid, name, NULL, PROTO_SETTINGS,
			     DEVICE_SENSOR, SUBTYPE_TEMP, "st",
			     spacfg.tscale, tags, 6, gnhastd_conn->bev);

	/* The heat mode */
	sprintf(uid, "%s-heatmode", macstr);
	sprintf(name, "Spa Heating Mode");
	generic_build_device(cfg, uid, name, NULL, PROTO_SETTINGS,
			     DEVICE_SENSOR, SUBTYPE_TRISTATE, "hm",
			     0, tags, 6, gnhastd_conn->bev);
	/* The heat state */
	sprintf(uid, "%s-heatstate", macstr);
	sprintf(name, "Spa Heating State");
	generic_build_device(cfg, uid, name, NULL, PROTO_POOL,
			     DEVICE_SENSOR, SUBTYPE_TRISTATE, NULL,
			     0, tags, 6, gnhastd_conn->bev);
	/* The heat temperature range */
	sprintf(uid, "%s-temprange", macstr);
	sprintf(name, "Spa Temperature Range");
	generic_build_device(cfg, uid, name, NULL, PROTO_POOL,
			     DEVICE_SENSOR, SUBTYPE_SWITCH, "tr",
			     0, tags, 6, gnhastd_conn->bev);
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

05 - nrof pumps. Bitmask 2 bits per pump.
06 - P6xxxxP5
07 - L1xxxxL2

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

	LOG(LOG_DEBUG, "Got device config");

	if (!devices_ready) {
		spacfg.cfgdone[CFGDONE_CONF] = 1;
		init_balboa_devs();
	}
	return 0;
}

/**
   \brief Parse a panel config response
   \param data The data
   \param len Length of data
   \return -1 if error

SZ 02 03 04   05 06 07 08 09 10 CB
0B 0A BF 2E   0A 00 01 50 00 00 BF

05 - nrof pumps. Bitmask 2 bits per pump.
06 - P6xxxxP5
07 - L2xxxxL1
08 - CxxxxxBL - circpump, blower
09 - xxMIxxAA - mister, Aux2, Aux1

*/

int parse_panel_config_resp(uint8_t *data, size_t len)
{
	int i;

	spa_lastupd = time(NULL);

	LOG(LOG_DEBUG, "Got panel config");

	if (!devices_ready) {
		/* check the config bitmasks */
		if (data[5] & 0x03)
			spacfg.pump_array[0] = 1;
		if (data[5] & 0x0c)
			spacfg.pump_array[1] = 1;
		if (data[5] & 0x30)
			spacfg.pump_array[2] = 1;
		if (data[5] & 0xc0)
			spacfg.pump_array[3] = 1;
		if (data[6] & 0x03)
			spacfg.pump_array[4] = 1;
		if (data[6] & 0xc0)
			spacfg.pump_array[5] = 1;
		if (data[7] & 0x03)
			spacfg.light_array[0] = 1;
		if (data[7] & 0xc0)
			spacfg.light_array[1] = 1;
		if (data[8] & 0x03)
			spacfg.blower = 1;
		if (data[8] & 0x80)
			spacfg.circpump = 1;
		if (data[9] & 0x01)
			spacfg.aux_array[1] = 1;
		if (data[9] & 0x02)
			spacfg.aux_array[2] = 1;
		if (data[9] & 0x30)
			spacfg.mister = 1;
		/* we got the second cfg response */
		spacfg.cfgdone[CFGDONE_PANEL] = 1;
		init_balboa_devs();
	}
	return 0;
}

/**
   \brief Parse a status update
   \param data The data
   \param len Length of data
   \return 1 if error

00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25
MS ML MT MT MT XX F1 CT HH MM F2  X  X  X F3 F4 PP  X CP LF MB  X  X  X  X ST
7E 1D FF AF 13  0  0 64  8 2D  0  0  1  0  0  4  0  0  0  0  0  0  0  0  0 64

26 27 28 29 30
 X  X  X CB ME
 0  0  0  6 7E

20- mister/blower
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

	if (!devices_ready) {
		if (data[14] & 0x01)
			spacfg.tscale = TSCALE_C;
		else
			spacfg.tscale = TSCALE_F;

		spacfg.trange = (data[15] & 0x04)>>2;

		spacfg.cfgdone[CFGDONE_STATUS] = 1;
		init_balboa_devs();
		return 1;
	}

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
	if (n_tscale != spacfg.tscale) {
		spacfg.tscale = n_tscale;
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
	if (spacfg.tscale)
		d /= 2.0;
	store_data_dev(dev, DATALOC_DATA, &d);
	if (upd)
		dev->scale = spacfg.tscale;
	LOG(LOG_DEBUG, "CurTemp changed to %f", d);
	gn_update_device(dev, upd, gnhastd_conn->bev);

	/* Set Temp */
	sprintf(uid, "%s-settemp", macstr);
	dev = find_device_byuid(uid);
	if (dev == NULL) {
		LOG(LOG_ERROR, "Can't find settemp");
		return 1;
	}
	d = (double)data[25];
	if (spacfg.tscale)
		d /= 2.0;
	store_data_dev(dev, DATALOC_DATA, &d);
	if (upd)
		dev->scale = spacfg.tscale;
	LOG(LOG_DEBUG, "SetTemp changed to %f", d);
	gn_update_device(dev, upd, gnhastd_conn->bev);

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
	gn_update_device(dev, 0, gnhastd_conn->bev);

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
	gn_update_device(dev, 0, gnhastd_conn->bev);

	sprintf(uid, "%s-temprange", macstr);
	dev = find_device_byuid(uid);
	if (dev == NULL) {
		LOG(LOG_ERROR, "Can't find temprange");
		return 1;
	}
	val = (data[15] & 0x04)>>2;
	spacfg.trange = val; /* update trange here for speed */
	store_data_dev(dev, DATALOC_DATA, &val);
	LOG(LOG_DEBUG, "Temp Range changed to %d", val);
	gn_update_device(dev, 0, gnhastd_conn->bev);

	/* pump status */
	for (i=0; i < MAX_PUMPS; i++) {
		if (!spacfg.pump_array[i])
			continue;
		sprintf(uid, "%s-pump%0.2d", macstr, i);
		dev = find_device_byuid(uid);
		if (dev == NULL) {
			LOG(LOG_ERROR, "Can't find pump %d", i);
			return 1;
		}
		if (i < 4)
			val = (data[16]>>i) & 0x03;
		else
			val = (data[17]>>(i-4)) & 0x03;
		store_data_dev(dev, DATALOC_DATA, &val);
		LOG(LOG_DEBUG, "Pump %d changed to %d", i, val);
		gn_update_device(dev, 0, gnhastd_conn->bev);
	}

	/* Circulation pump */
	if (spacfg.circpump) {
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
		gn_update_device(dev, 0, gnhastd_conn->bev);
	}
	/* Spa lights */
	for (i=0; i<2; i++) {
		if (!spacfg.light_array[i])
			continue;
		sprintf(uid, "%s-light%0.2d", macstr, i);
		dev = find_device_byuid(uid);
		if (dev == NULL) {
			LOG(LOG_ERROR, "Can't find light %d", i);
			return 1;
		}
		if (data[19]>>i & 0x03)
			val = 1;
		else
			val = 0;
		store_data_dev(dev, DATALOC_DATA, &val);
		LOG(LOG_DEBUG, "Light #%d changed to %d", i, val);
		gn_update_device(dev, 0, gnhastd_conn->bev);
	}

	/* Mister */
	if (spacfg.mister) {
		sprintf(uid, "%s-mister", macstr);
		dev = find_device_byuid(uid);
		if (dev == NULL) {
			LOG(LOG_ERROR, "Can't find mister");
			return 1;
		}
		val = data[20] & 0x01;
		store_data_dev(dev, DATALOC_DATA, &val);
		LOG(LOG_DEBUG, "Mister changed to %d", val);
		gn_update_device(dev, 0, gnhastd_conn->bev);
	}

	/* AUX */
	for (i=0; i<2; i++) {
		if (!spacfg.aux_array[i])
			continue;
		sprintf(uid, "%s-aux%0.2d", macstr, i);
		dev = find_device_byuid(uid);
		if (dev == NULL) {
			LOG(LOG_ERROR, "Can't find aux %d", i);
			return 1;
		}
		if (i == 0)
			val = data[20] & 0x08;
		else
			val = data[20] & 0x10;
		store_data_dev(dev, DATALOC_DATA, &val);
		LOG(LOG_DEBUG, "Aux changed to %d", val);
		gn_update_device(dev, 0, gnhastd_conn->bev);
	}
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
	//LOG(LOG_DEBUG, "Got MSG: %X len %d bytes", header[0], header[1]);
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

	i = find_balboa_mtype(data, rlen);
	if (i == -1) {
		LOG(LOG_DEBUG, "Got unknown message type %0.2X%0.2X%0.2X"
		    " LEN:%d", data[2], data[3], data[4], data[1]);
#if 0
		for (j=0; j < rlen; j++)
			printf("%0.2X", data[j]);
		printf("\n");
#endif
		free(data);
		return;
	}
	switch (i) {
	case BMTR_CONFIG_RESP:
		parse_config_resp(data, rlen);
		break;
	case BMTR_STATUS_UPDATE:
		j = parse_status_update(data, rlen);
		break;
	case BMTR_PANEL_RESP:
		parse_panel_config_resp(data, rlen);
		break;
	}

	free(data);

	if ((time(NULL) - spa_lastupd) > 5 && j) {
		/* something went wrong.  re-ask for the config */
		send_config_req();
	}
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
		/* ask for the config */
		send_config_req();
		send_panel_req(0, 1);
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

   This device spams status updates about 4 per second (wow!)  So if we don't
   hear from it in about 15 seconds, it's super broke.
*/

int collector_is_ok(void)
{
	if ((time(NULL) - spa_lastupd) < 15)
                return(1);

	rescue_balboa_conn(); /* at least try */
        return(0);
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
	struct timeval secs = { 0, 0 };
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

	/* set min proto version */
	min_proto_version = GNHASTD_MIN_PROTO_VERS;

	/* Init the config array 0 */
	memset(&spacfg, 0, sizeof(spaconfig_t));

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
	gn_get_apiv(gnhastd_conn->bev);

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

	/* check the config every 50 minutes */
	secs.tv_sec = 3000;
	ev = event_new(base, -1, EV_PERSIST, cb_get_spaconfig, NULL);
	event_add(ev, &secs);

	/* go forth and destroy */
	event_base_dispatch(base);

	/* Close out the log, and bail */
	closelog();
	delete_pidfile();
	return(0);
}

