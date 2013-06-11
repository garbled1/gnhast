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
   \author Tim Rightnour
   \brief Oregon Scientific WMR918/968 Collector
   Can be used directly attached, or with wx200d

   http://wx200.planetfall.com/wx200.txt
   http://www.netsky.org/WMR/Protocol.htm
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
#include "wmr918.h"
#include "confparser.h"
#include "gncoll.h"

void connect_event_cb(struct bufferevent *ev, short what, void *arg);
void wx200d_connect_event_cb(struct bufferevent *ev, short what, void *arg);
void connect_server_cb(int nada, short what, void *arg);
static int conf_parse_conntype(cfg_t *cfg, cfg_opt_t *opt, const char *value,
			       void *result);

FILE *logfile;   /** our logfile */
extern int debugmode;
cfg_t *cfg, *gnhastd_c, *wmr918_c;
char *dumpconf = NULL;
int need_rereg = 0;
int gotdata = 0;
char *uidprefix = "wmr918";

/** Need the argtable in scope, so we can generate proper commands
    for the server */
extern argtable_t argtable[];
extern TAILQ_HEAD(, _device_t) alldevs;

/** The event base */
struct event_base *base;
struct evdns_base *dns_base;

#define CONN_TYPE_WX200D       	1
#define CONN_TYPE_WMRSERIAL	2
#define CONN_TYPE_GNHASTD	3
char *conntype[4] = {
	"none",
	"wx200d",
	"wmr918_serial",
	"gnhastd",
};

typedef struct _connection_t {
	int port;
	int type;
	int tscale;
	char *host;
	struct bufferevent *bev;
	int shutdown;
} connection_t;

/** The connection streams for our three connections */
connection_t *gnhastd_conn, *wmr918serial_conn, *wx200d_conn;

/* Configuration file setup */

extern cfg_opt_t device_opts[];

cfg_opt_t wmr918_opts[] = {
	CFG_STR("hostname", "127.0.0.1", CFGF_NONE),
	CFG_INT("port", 9753, CFGF_NONE),
	CFG_STR("serialdev", 0, CFGF_NODEFAULT),
	CFG_INT_CB("conntype", CONN_TYPE_WX200D, CFGF_NONE,
		   conf_parse_conntype),
	CFG_INT_CB("tscale", TSCALE_F, CFGF_NONE, conf_parse_tscale),
	CFG_INT_CB("baroscale", BAROSCALE_MB, CFGF_NONE, conf_parse_baroscale),
	CFG_INT_CB("lengthscale", LENGTH_IN, CFGF_NONE, conf_parse_lscale),
	CFG_INT_CB("speedscale", SPEED_MPH, CFGF_NONE, conf_parse_speedscale),
	CFG_END(),
};

cfg_opt_t gnhastd_opts[] = {
	CFG_STR("hostname", "127.0.0.1", CFGF_NONE),
	CFG_INT("port", 2920, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t options[] = {
	CFG_SEC("wmr918", wmr918_opts, CFGF_NONE),
	CFG_SEC("gnhastd", gnhastd_opts, CFGF_NONE),
	CFG_SEC("device", device_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_STR("logfile", WMR918COLL_LOG_FILE, CFGF_NONE),
	CFG_STR("pidfile", WMR918COLL_PID_FILE, CFGF_NONE),
	CFG_END(),
};

/*****
      General stuff
*****/

/**
   \brief parse wmr connection type
*/

static int conf_parse_conntype(cfg_t *cfg, cfg_opt_t *opt, const char *value,
			      void *result)
{
	if (strcasecmp(value, "net") == 0)
		*(int *)result = CONN_TYPE_WX200D;
	else if (strcmp(value,"serial") == 0)
		*(int *)result = CONN_TYPE_WMRSERIAL;
	else {
		cfg_error(cfg, "invalid value for option '%s': %s",
		    cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}

/**
   \brief print wmr connection type
*/

static void conf_print_conntype(cfg_opt_t *opt, unsigned int index, FILE *fp)
{
	switch (cfg_opt_getnint(opt, index)) {
	case CONN_TYPE_WX200D:
		fprintf(fp, "net");
		break;
	case CONN_TYPE_WMRSERIAL:
		fprintf(fp, "serial");
		break;
	default:
		fprintf(fp, "net");
		break;
	}
}

/**
   \brief Create a new dev with subtype, value, and uid suffix
   \param subtype SUBTYPE_*
   \param val current reading
   \param suffix a uid suffix
   \return device_t of new dev
*/

device_t *new_dev(int subtype, double val, char *suffix)
{
	char buf[256];
	device_t *dev;

	sprintf(buf, "%s-%s", uidprefix, suffix);
	dev = new_dev_from_conf(cfg, buf);
	if (dev == NULL) {
		dev = smalloc(device_t);
		dev->uid = strdup(buf);
		dev->loc = strdup(buf);
		dev->type = DEVICE_SENSOR;
		dev->proto = PROTO_SENSOR_WMR918;
		dev->subtype = subtype;
		switch (subtype) {
		case SUBTYPE_TEMP:
			dev->scale = cfg_getint(wmr918_c, "tscale");
			break;
		case SUBTYPE_PRESSURE:
			dev->scale = cfg_getint(wmr918_c, "baroscale");
			break;
		case SUBTYPE_SPEED:
			dev->scale = cfg_getint(wmr918_c, "speedscale");
			break;
		case SUBTYPE_RAINRATE:
			dev->scale = cfg_getint(wmr918_c, "lengthscale");
			break;
		}
		(void)new_conf_from_dev(cfg, dev);
	}
	insert_device(dev);
	store_data_dev(dev, DATALOC_DATA, &val);
	if (dumpconf == NULL && dev->name != NULL) {
		gn_register_device(dev, gnhastd_conn->bev);
		gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
	}
	return dev;
}

/**
   \brief Update a device
   \param val current reading
   \param suffix a uid suffix
*/

void update_dev(double val, char *suffix)
{
	char buf[256];
	device_t *dev;

	sprintf(buf, "%s-%s", uidprefix, suffix);
	dev = find_device_byuid(buf);
	if (dev == NULL)
		LOG(LOG_FATAL, "Cannot find dev %s", buf);
	store_data_dev(dev, DATALOC_DATA, &val);
	if (dev->name)
		gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
}

/**
   \brief check if the conf is ready to dump, and do so if needed
*/

void maybe_dump_conf(void)
{
	int i;
	cfg_opt_t *a;
	cfg_t *section;

	if (dumpconf != NULL && gotdata == DGROUP_ALL) {
		LOG(LOG_NOTICE, "Dumping config file to %s and exiting",
		    dumpconf);
		for (i=0; i < cfg_size(cfg, "wmr918"); i++) {
			section = cfg_getnsec(cfg, "wmr918", i);
			a = cfg_getopt(section, "tscale");
			cfg_opt_set_print_func(a, conf_print_tscale);

			section = cfg_getnsec(cfg, "wmr918", i);
			a = cfg_getopt(section, "lengthscale");
			cfg_opt_set_print_func(a, conf_print_lscale);

			section = cfg_getnsec(cfg, "wmr918", i);
			a = cfg_getopt(section, "baroscale");
			cfg_opt_set_print_func(a, conf_print_baroscale);

			section = cfg_getnsec(cfg, "wmr918", i);
			a = cfg_getopt(section, "speedscale");
			cfg_opt_set_print_func(a, conf_print_speedscale);

			section = cfg_getnsec(cfg, "wmr918", i);
			a = cfg_getopt(section, "conntype");
			cfg_opt_set_print_func(a, conf_print_conntype);
		}

		dump_conf(cfg, 0, dumpconf);
		exit(0);
	}
}

/*****
      WX200 Functions
*****/

/**
   \brief Calculate the checksum byte for a wmr message
   \param data data
   \param len length of data
   \return checksum
*/
uint8_t wx2_calc_cs(uint8_t *data, int len)
{
	int i, sum;
	uint8_t ret;

	sum = 0;
	for (i=0; i < (len - 1); i++)
		sum += data[i];

	ret = sum % 256;

	return ret;
}

/**
   \brief Calculate wx200 style barometer reading
   \param d1 first byte
   \param d2 second byte
   \param d3 third byte
   \param format format byte
   \param sealevel 1 for sealevel, 0 for local
   \return barometric reading in double, in desired format
   \note this format is particularly insane.
*/

double wx2_calc_baro(uint8_t d1, uint8_t d2, uint8_t d3,
		     uint8_t format, int sealevel)
{
	double baro;
	int scale;

	if (sealevel) {
		baro = ((d1/16)*10+(d1%16))/10.0;
		baro += ((d2/16)*10+(d2%16))*10.0;
		baro += ((d3 & 0xF)%16) * 1000.0;
	} else {
		baro = ((d1/16)*10+(d1%16));
		baro += ((d2/16)+(d2%16))*100.0;
	}

	scale = cfg_getint(wmr918_c, "baroscale");
	LOG(LOG_DEBUG, "format = %X %X", format, format & 0x30);
	switch (format & 0x30) { /* bits 0 and 1 */
	case 0x00:  baro = BARO_INTOMB(baro); break;
	case 0x10:  baro = BARO_MMTOMB(baro); break;
	}
	switch (scale) {
	case BAROSCALE_IN: baro = BARO_MBTOIN(baro); break;
	case BAROSCALE_MM: baro = BARO_MBTOMM(baro); break;
	}
	return baro;
}

/**
   \brief Calculate wx200 style temp
   \param d1 first byte
   \param d2 second byte
   \param format format byte
   \return temp temperature in double, in desired format
*/
double wx2_calc_temp(uint8_t d1, uint8_t d2, uint8_t format)
{
	double temp;
	uint8_t x;
	int tscale;

	temp = ((d1 & 0xF)%16)/10.0 ;
	temp += (d1/16);

	x = d2 & 0x7;
	temp += x*10.0;
	if (d2 & 0x8)
		temp = 0.0-temp;

	tscale = cfg_getint(wmr918_c, "tscale");
	if (format & 0x4) {
		switch (tscale) {
		case TSCALE_C:
			temp = FTOC(temp);
			break;
		case TSCALE_K:
			temp = CTOK(FTOC(temp));
			break;
		}
	} else {
		switch (tscale) {
		case TSCALE_F:
			temp = CTOF(temp);
			break;
		case TSCALE_K:
			temp = CTOK(temp);
			break;
		}
	}
	return temp;
}

/**
   \brief handle a GROUP_TIME data chunk
   \param data data block
*/

void wx2_handle_time(uint8_t *data)
{
	LOG(LOG_DEBUG, "Time data: %0.2X:%0.2X:%0.2X", data[3],
	    data[2], data[1]);
	maybe_dump_conf();
}

/**
   \brief handle a GROUP_HUMID data chunk
   \param data data block
*/

void wx2_handle_humid(uint8_t *data)
{
	device_t *dev;
	char buf[256];
	double ih, oh;

	LOG(LOG_DEBUG, "Time data: %0.2X/%0.2X %0.2X:%0.2X:%0.2X",
	    data[5]-0x10, data[4], data[3], data[2], data[1]);

	ih = ((data[8]/16)*10+(data[8]%16));
	oh = ((data[20]/16)*10+(data[20]%16));
	LOG(LOG_DEBUG, "Indoor Humidity : %f%% Outdoor: %f%%",
	    ih, oh);

	if ((gotdata & DGROUP_HUMID) == 0) {
		(void)new_dev(SUBTYPE_HUMID, ih, "inhumid");
		(void)new_dev(SUBTYPE_HUMID, oh, "outhumid");
	} else {
		update_dev(ih, "inhumid");
		update_dev(oh, "outhumid");
	}
	maybe_dump_conf();
}


/**
   \brief handle a GROUP_TEMP data chunk
   \param data data block
*/

void wx2_handle_temp(uint8_t *data)
{
	double itemp, otemp;
	device_t *dev;
	char buf[256];

	itemp = wx2_calc_temp(data[1], data[2], data[15]);
	otemp = wx2_calc_temp(data[16], data[17], data[15]);
	LOG(LOG_DEBUG, "indoor= %f outdoor= %f", itemp, otemp);

	if ((gotdata & DGROUP_TEMP) == 0) {
		(void)new_dev(SUBTYPE_TEMP, itemp, "intemp");
		(void)new_dev(SUBTYPE_TEMP, otemp, "outtemp");
	} else {
		update_dev(itemp, "intemp");
		update_dev(otemp, "outtemp");
	}
	maybe_dump_conf();
}

/**
   \brief handle a GROUP_BARO data chunk
   \param data data block
*/

void wx2_handle_baro(uint8_t *data)
{
	double idew, odew, local, sealevel;
	int tscale;

	tscale = cfg_getint(wmr918_c, "tscale");

	local = wx2_calc_baro(data[1], data[2], 0, data[5], 0);
	sealevel = wx2_calc_baro(data[3], data[4], data[5], data[5], 1);

	LOG(LOG_DEBUG, "local %fmb sealevel %fmb", local, sealevel);

	idew = ((data[7]/16)*10+(data[7]%16));
	odew = ((data[18]/16)*10+(data[18]%16));
	switch (tscale) {
	case TSCALE_F:
		idew = CTOF(idew);
		odew = CTOF(odew);
		break;
	case TSCALE_K:
		idew = CTOK(idew);
		odew = CTOK(odew);
		break;
	}
	LOG(LOG_DEBUG, "indoor dewpt %f outdoor %f", idew, odew);

	if ((gotdata & DGROUP_BARO) == 0) {
		(void)new_dev(SUBTYPE_TEMP, idew, "indew");
		(void)new_dev(SUBTYPE_TEMP, odew, "outdew");
		(void)new_dev(SUBTYPE_PRESSURE, local, "localbaro");
		(void)new_dev(SUBTYPE_PRESSURE, sealevel, "sealevelbaro");
	} else {
		update_dev(idew, "indew");
		update_dev(odew, "outdew");
		update_dev(local, "localbaro");
		update_dev(sealevel, "sealevelbaro");
	}
	maybe_dump_conf();
}

/**
   \brief handle a GROUP_RAIN data chunk
   \param data data block
*/

void wx2_handle_rain(uint8_t *data)
{
	int lscale;
	double rate, yest, total;

	lscale = cfg_getint(wmr918_c, "lengthscale");

	rate = ((data[1]/16)+(data[1]%16));
	rate += (((data[2]& 0xF)/16)+((data[2] & 0xF)%16)) * 100.0;

	yest = ((data[3]/16)+(data[3]%16));
	yest += ((data[4]/16)+(data[4]%16)) * 100.0;

	total = ((data[5]/16)+(data[5]%16));
	total += ((data[6]/16)+(data[6]%16)) * 100.0;

	if (data[10] & 0x2 && lscale == LENGTH_MM) {
		rate = INTOMM(rate);
		yest = INTOMM(yest);
		total = INTOMM(total);
	} else if ((data[10] & 0x2) == 0 && lscale == LENGTH_IN) {
		rate = MMTOIN(rate);
		yest = MMTOIN(yest);
		total = MMTOIN(total);
	}

	LOG(LOG_DEBUG, "rate = %f, yest = %f, total = %f", rate, yest, total);
	if ((gotdata & DGROUP_RAIN) == 0) {
		(void)new_dev(SUBTYPE_RAINRATE, rate, "rain");
		(void)new_dev(SUBTYPE_RAINRATE, yest, "ydayrain");
		(void)new_dev(SUBTYPE_RAINRATE, total, "totalrain");
	} else {
		update_dev(rate, "rain");
		update_dev(yest, "ydayrain");
		update_dev(total, "totalrain");
	}
	maybe_dump_conf();
}

/**
   \brief handle a GROUP_WIND data chunk
   \param data data block
*/

void wx2_handle_wind(uint8_t *data)
{
	int sscale, tscale;
	double gust, gustdir, avg, avgdir, windchill;

	sscale = cfg_getint(wmr918_c, "speedscale");
	tscale = cfg_getint(wmr918_c, "tscale");

	LOG(LOG_DEBUG, "gs %X/%X, gdir %X/%X, as %X/%X, adir %X/%X, format %X"
	    " chill %X", data[1], data[2], data[2], data[3], data[4], data[5],
	    data[5], data[6], data[15], data[16]);

	gust = (((data[1]/16)*10)+(data[1]%16))/10.0;
	gust += ((data[2] & 0xF)%16) * 10.0;

	avg = (((data[4]/16)*10)+(data[4]%16))/10.0;
	avg += ((data[5] & 0xF)%16) * 10.0;

	gustdir = (data[2] & 0xF0)/16.0;
	gustdir += (((data[3]/16)*10)+(data[3]%16))*10.0;

	avgdir = (data[5] & 0xF0)/16.0;
	avgdir += (((data[6]/16)*10)+(data[6]%16))*10.0;

	windchill = ((data[16]/16)*10)+(data[16]%16);

	switch (tscale) {
	case TSCALE_F: windchill = CTOF(windchill); break;
	case TSCALE_K: windchill = CTOK(windchill); break;
	}

	switch (data[15] & 0xC0) { /* bits 2-3 */
	case 0x40:
		gust = KNOTSTOMPH(gust);
		avg = KNOTSTOMPH(avg);
		break;
	case 0x80:
		gust = MSTOMPH(gust);
		avg = MSTOMPH(avg);
		break;
	case 0xc0:
		gust = KPHTOMPH(gust);
		avg = KPHTOMPH(avg);
		break;
	}

	switch (sscale) {
	case SPEED_KPH:
		gust = MPHTOKPH(gust);
		avg = MPHTOKPH(avg);
		break;
	case SPEED_MS:
		gust = MPHTOMS(gust);
		avg = MPHTOMS(avg);
		break;
	case SPEED_KNOTS:
		gust = MPHTOKNOTS(gust);
		avg = MPHTOKNOTS(avg);
		break;
	}

	LOG(LOG_DEBUG, "gust= %f avg= %f gdir= %f adir= %f wc= %f", gust, avg,
	    gustdir, avgdir, windchill);

	if ((gotdata & DGROUP_WIND) == 0) {
		(void)new_dev(SUBTYPE_SPEED, gust, "windgust");
		(void)new_dev(SUBTYPE_SPEED, avg, "windavg");
		(void)new_dev(SUBTYPE_DIR, gustdir, "windgustdir");
		(void)new_dev(SUBTYPE_DIR, avgdir, "windavgdir");
		(void)new_dev(SUBTYPE_TEMP, windchill, "windchill");
	} else {
		update_dev(gust, "windgust");
		update_dev(avg, "windavg");
		update_dev(gustdir, "windgustdir");
		update_dev(avgdir, "windavgdir");
		update_dev(windchill, "windchill");
	}
	maybe_dump_conf();

}

/**
   \brief wx200 read callback
   \param in the bufferevent that fired
   \param arg the connection_t
   \note Used for both serial and network connection. (yay wx200d!)
*/

void wx2_buf_read_cb(struct bufferevent *in, void *arg)
{
	connection_t *conn = (connection_t *)arg;
	size_t len;
	uint8_t *data, cs;
	device_t *dev;
	struct evbuffer *evbuf;

	evbuf = bufferevent_get_input(in);
	data = evbuffer_pullup(evbuf, 1);

	while (data != NULL) {
		switch (data[0]) {
		case GROUP_HUMID:
			LOG(LOG_DEBUG, "Group 8F (humid) data recieved");
			data = evbuffer_pullup(evbuf, GROUP_HUMID_SIZE);
			if (data == NULL)
				return; /* too small, wait */
			cs = wx2_calc_cs(data, GROUP_HUMID_SIZE);
			if (cs == data[GROUP_HUMID_SIZE-1]) {
				wx2_handle_humid(data);
				gotdata |= DGROUP_HUMID;
			} else
				LOG(LOG_ERROR, "Group humid bad chksum,"
				    " discarding update!");
			evbuffer_drain(evbuf, GROUP_HUMID_SIZE);
			break;
		case GROUP_TEMP:
			LOG(LOG_DEBUG, "Group 9F (temp) data recieved");
			data = evbuffer_pullup(evbuf, GROUP_TEMP_SIZE);
			if (data == NULL)
				return; /* too small, wait */
			cs = wx2_calc_cs(data, GROUP_TEMP_SIZE);
			if (cs == data[GROUP_TEMP_SIZE-1]) {
				wx2_handle_temp(data);
				gotdata |= DGROUP_TEMP;
			} else
				LOG(LOG_ERROR, "Group temp bad chksum,"
				    " discarding update!");
			evbuffer_drain(evbuf, GROUP_TEMP_SIZE);
			break;
		case GROUP_BARO:
			LOG(LOG_DEBUG, "Group AF (baro) data recieved");
			data = evbuffer_pullup(evbuf, GROUP_BARO_SIZE);
			if (data == NULL)
				return; /* too small, wait */
			cs = wx2_calc_cs(data, GROUP_BARO_SIZE);
			if (cs == data[GROUP_BARO_SIZE-1]) {
				wx2_handle_baro(data);
				gotdata |= DGROUP_BARO;
			} else
				LOG(LOG_ERROR, "Group baro bad chksum,"
				    " discarding update!");
			evbuffer_drain(evbuf, GROUP_BARO_SIZE);
			break;
		case GROUP_RAIN:
			LOG(LOG_DEBUG, "Group BF (rain) data recieved");
			data = evbuffer_pullup(evbuf, GROUP_RAIN_SIZE);
			if (data == NULL)
				return; /* too small, wait */
			cs = wx2_calc_cs(data, GROUP_RAIN_SIZE);
			if (cs == data[GROUP_RAIN_SIZE-1]) {
				wx2_handle_rain(data);
				gotdata |= DGROUP_RAIN;
			} else
				LOG(LOG_ERROR, "Group rain bad chksum,"
				    " discarding update!");
			evbuffer_drain(evbuf, GROUP_RAIN_SIZE);
			break;
		case GROUP_WIND:
			LOG(LOG_DEBUG, "Group CF (wind) data recieved");
			data = evbuffer_pullup(evbuf, GROUP_WIND_SIZE);
			if (data == NULL)
				return; /* too small, wait */
			cs = wx2_calc_cs(data, GROUP_WIND_SIZE);
			if (cs == data[GROUP_WIND_SIZE-1]) {
				wx2_handle_wind(data);
				gotdata |= DGROUP_WIND;
			} else
				LOG(LOG_ERROR, "Group wind bad chksum,"
				    " discarding update!");
			evbuffer_drain(evbuf, GROUP_WIND_SIZE);
			break;
		case GROUP_TIME:
			LOG(LOG_DEBUG, "Group FF (time) data recieved");
			data = evbuffer_pullup(evbuf, GROUP_TIME_SIZE);
			if (data == NULL)
				return; /* too small, wait */
			cs = wx2_calc_cs(data, GROUP_TIME_SIZE);
			if (cs == data[GROUP_TIME_SIZE-1])
				wx2_handle_time(data);
			else
				LOG(LOG_ERROR, "Group time bad chksum,"
				    " discarding update!");
			evbuffer_drain(evbuf, GROUP_TIME_SIZE);
			break;
		}
		data = evbuffer_pullup(evbuf, 1);
	}
	return;
}

/**
   \brief Event callback used with wx200d network connections
   \param ev The bufferevent that fired
   \param what why did it fire?
   \param arg pointer to connection_t;
*/

void wx200d_connect_event_cb(struct bufferevent *ev, short what, void *arg)
{
	int err;
	connection_t *conn = (connection_t *)arg;

	if (what & BEV_EVENT_CONNECTED)
		LOG(LOG_DEBUG, "Connected to %s", conntype[conn->type]);
	else if (what & (BEV_EVENT_ERROR|BEV_EVENT_EOF)) {
		if (what & BEV_EVENT_ERROR) {
			err = bufferevent_socket_get_dns_error(ev);
			if (err)
				LOG(LOG_FATAL, "DNS Failure connecting to %s: %s",
				    conntype[conn->type], strerror(err));
		}
		LOG(LOG_DEBUG, "Lost connection to %s, closing",
		    conntype[conn->type]);
		bufferevent_free(ev);
		/* Retry immediately */
		connect_server_cb(0, 0, conn);
	}
}

/*****
      General routines/gnhastd connection stuff
*****/


/**
   \brief A read callback, got data from server
   \param in The bufferevent that fired
   \param arg optional arg
*/

void buf_read_cb(struct bufferevent *in, void *arg)
{
	char *data;
	struct evbuffer *input;
	size_t len;

	input = bufferevent_get_input(in);
	data = evbuffer_readln(input, &len, EVBUFFER_EOL_CRLF);
	if (len) {
		printf("Got data? %s\n", data);
		free(data);
	}
}

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
		bufferevent_setcb(conn->bev, buf_read_cb, NULL,
				  connect_event_cb, conn);
	else if (conn->type == CONN_TYPE_WX200D)
		bufferevent_setcb(conn->bev, wx2_buf_read_cb, NULL,
				  wx200d_connect_event_cb, conn);
	bufferevent_enable(conn->bev, EV_READ|EV_WRITE);
	bufferevent_socket_connect_hostname(conn->bev, dns_base, AF_UNSPEC,
					    conn->host, conn->port);
	if (conn->type == CONN_TYPE_GNHASTD) {
		LOG(LOG_NOTICE, "Attempting to connect to %s @ %s:%d",
		    conntype[conn->type], conn->host, conn->port);
		if (need_rereg)
			TAILQ_FOREACH(dev, &alldevs, next_all)
				if (dumpconf == NULL && dev->name != NULL)
					gn_register_device(dev, conn->bev);
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
	// XXX bufferevent_free(owserver_conn->bev);
	ev = evtimer_new(base, cb_shutdown, NULL);
	evtimer_add(ev, &secs);
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
	int ch, fd;
	char *buf;
	char *conffile = SYSCONFDIR "/" WMR918COLL_CONFIG_FILE;
	struct timeval secs = { 0, 0 };
	struct event *ev;

	/* process command line arguments */
	while ((ch = getopt(argc, argv, "?c:dm:u:")) != -1)
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
		default:
		case '?':	/* you blew it */
			(void)fprintf(stderr, "usage:\n%s [-c configfile]"
				      "[-m dumpconfigfile] [-u uidprefix]\n",
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
	/* Initialize the device table */
	init_devtable(cfg, 0);

	cfg = parse_conf(conffile);

	if (!debugmode)
		logfile = openlog(cfg_getstr(cfg, "logfile"));

	writepidfile(cfg_getstr(cfg, "pidfile"));

	/* First, parse the wmr918coll section */

	if (cfg) {
		wmr918_c = cfg_getsec(cfg, "wmr918");
		if (!wmr918_c)
			LOG(LOG_FATAL, "Error reading config file,"
			    " wmr918 section");
	}

	/* Now, parse the details of connecting to the gnhastd server */

	if (cfg) {
		gnhastd_c = cfg_getsec(cfg, "gnhastd");
		if (!gnhastd_c)
			LOG(LOG_FATAL, "Error reading config file,"
			    " gnhastd section");
	}
	gnhastd_conn = smalloc(connection_t);
	gnhastd_conn->port = cfg_getint(gnhastd_c, "port");
	gnhastd_conn->type = CONN_TYPE_GNHASTD;
	gnhastd_conn->host = cfg_getstr(gnhastd_c, "hostname");
	/* cheat, and directly call the timer callback
	   This sets up a connection to the server. */
	connect_server_cb(0, 0, gnhastd_conn);
	gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);

	if (cfg_getint(wmr918_c, "conntype") == CONN_TYPE_WX200D) {
		wx200d_conn = smalloc(connection_t);
		wx200d_conn->port = cfg_getint(wmr918_c, "port");
		wx200d_conn->type = CONN_TYPE_WX200D;
		wx200d_conn->host = cfg_getstr(wmr918_c, "hostname");
		wx200d_conn->tscale = cfg_getint(wmr918_c, "tscale");
		connect_server_cb(0, 0, wx200d_conn);
	} else if (cfg_getint(wmr918_c, "conntype") == CONN_TYPE_WMRSERIAL) {
		/* read the serial device name from the conf file */
		if (cfg_getstr(wmr918_c, "serialdev") == NULL)
			LOG(LOG_FATAL, "Serial device not set in conf file");

		/* Connect to the WMR918 */
		fd = serial_connect(cfg_getstr(wmr918_c, "serialdev"), B9600,
				    CS8|CREAD|CLOCAL);

		wmr918serial_conn = smalloc(connection_t);
		wmr918serial_conn->bev = bufferevent_socket_new(base, fd,
					    BEV_OPT_CLOSE_ON_FREE);
		wmr918serial_conn->type = CONN_TYPE_WMRSERIAL;
		bufferevent_setcb(wmr918serial_conn->bev, wx2_buf_read_cb,
				  NULL, serial_eventcb, wmr918serial_conn);
		bufferevent_enable(wmr918serial_conn->bev, EV_READ|EV_WRITE);
	} else {
		LOG(LOG_FATAL, "No connection type specified, punting");
	}

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

	/* go forth and destroy */
	event_base_dispatch(base);

	closelog();
	return(0);
}