/*
 * Copyright (c) 2014, 2016
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
   \file astrocoll/collector.c
   \author Tim Rightnour
   \brief Astronomical Phenomenon Collector
*/

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <math.h>
#include <sys/socket.h>
#include <event2/dns.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/util.h>

#include "common.h"
#include "gnhast.h"
#include "confuse.h"
#include "confparser.h"
#include "gncoll.h"
#include "collcmd.h"
#include "genconn.h"
#include "astro.h"
#include "jsmn.h"
#include "http_func.h"
#include "jsmn_func.h"

char *conffile = SYSCONFDIR "/" ASTROCOLL_CONFIG_FILE;
FILE *logfile;   /** our logfile */
cfg_t *cfg, *gnhastd_c, *astro_c;
char *dumpconf = NULL;
int need_rereg = 0;
int notify_fd = -1;
int query_running = 0;

char *sunrise_sunset_url = SUNRISE_SUNSET_URL;
http_get_t *sunrise_sunset_get;
char *usno_url = USNO_URL;
http_get_t *usno_get;


time_t astro_lastupd;
int dl_cbarg[DAYL_DUSK_CIVIL_TWILIGHT+1];
int sunrise_offsets[SS_SOLAR_NOON+1]; /* used to calculate current point */
int usno_sun_offsets[USNO_SUN_SOLAR_NOON+1]; /* same */
int usno_moon_offsets[USNO_MOON_UPPER_TRANSIT+1];

/* Timer event list */
struct event *sun_ev[SS_SOLAR_NOON+1];
struct event *solar_noon_start_ev, *solar_noon_end_ev;
struct event *moon_ev[USNO_MOON_UPPER_TRANSIT+1];
struct event *lunar_noon_start_ev, *lunar_noon_end_ev;


/* debugging */
//_malloc_options = "AJ";

/** Need the argtable in scope, so we can generate proper commands
    for the server */
extern argtable_t argtable[];
extern TAILQ_HEAD(, _device_t) alldevs;
extern commands_t commands[];
extern int debugmode;
extern int notimerupdate;
extern int collector_instance;

/** The event base */
struct event_base *base;
struct evdns_base *dns_base;

#define CONN_TYPE_SUNRISE_SUNSET        1
#define CONN_TYPE_GNHASTD       	2
#define CONN_TYPE_USNO			3
char *conntype[4] = {
        "none",
        "sunrise-sunset",
        "gnhastd",
	"aa.usno.navy.mil",
};


void connect_event_cb(struct bufferevent *ev, short what, void *arg);
void cb_shutdown(int fd, short what, void *arg);
static int conf_parse_suntype(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		     void *result);
void update_mode(device_t *dev, uint8_t mode);
void ss_request_cb(struct evhttp_request *req, void *arg);
void delay_feedstart_cb(int fd, short what, void *arg);

/** The connection streams for our connection */
connection_t *gnhastd_conn;

/* Configuration file setup */

extern cfg_opt_t device_opts[];

cfg_opt_t gnhastd_opts[] = {
	CFG_STR("hostname", "127.0.0.1", CFGF_NONE),
	CFG_INT("port", 2920, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t astrocoll_opts[] = {
	CFG_INT("update", 21600, CFGF_NONE),
	CFG_INT_CB("sunmethod", SUNTYPE_SS, CFGF_NONE, conf_parse_suntype),

	CFG_STR("sunriseuid", "sunrise", CFGF_NONE),
	CFG_STR("sunrisename", "Sunrise", CFGF_NONE),
	CFG_STR("moonphaseuid", "moonphase", CFGF_NONE),
	CFG_STR("moonphasename", "Lunar Phase", CFGF_NONE),
	CFG_STR("moonriseuid", "moonrise", CFGF_NONE),
	CFG_STR("moonrisename", "Lunar Transit", CFGF_NONE),

	CFG_FLOAT("latitude", 0.0, CFGF_NODEFAULT),
	CFG_FLOAT("longitude", 0.0, CFGF_NODEFAULT),

	CFG_STR("solaruid", "", CFGF_NODEFAULT),
	CFG_FLOAT("dev-sunrise", 250.0, CFGF_NONE),
	CFG_FLOAT("dev-astro-twilight", 50.0, CFGF_NONE),
	CFG_FLOAT("dev-nautical-twilight", 100.0, CFGF_NONE),
	CFG_FLOAT("dev-civil-twilight", 200.0, CFGF_NONE),

	CFG_INT("instance", 1, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t options[] = {
	CFG_SEC("gnhastd", gnhastd_opts, CFGF_NONE),
	CFG_SEC("astrocoll", astrocoll_opts, CFGF_NONE),
	CFG_SEC("device", device_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_STR("logfile", ASTROCOLL_LOG_FILE, CFGF_NONE),
	CFG_STR("pidfile", ASTROCOLL_PID_FILE, CFGF_NONE),
	CFG_END(),
};


/*****
      Stubs
*****/

/**** None ****/


/**
   \brief Check if a collector is functioning properly
   \param conn connection_t of collector's gnhastd connection
   \return 1 if OK, 0 if broken
   \note if 5 updates pass with no data, bad bad.
*/

int collector_is_ok(void)
{
	int update;

	update = cfg_getint(astro_c, "update");
	if ((time(NULL) - astro_lastupd) < (update * 5))
		return(1);
	return(0);
}

/**
   \brief parse a sunmethod type
   \param cfg the config base
   \param opt the option we are parsing
   \param the value of the option
   \param result result of option parsing will be stored here
   \return success
*/

static int conf_parse_suntype(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		     void *result)
{
	if (strcasecmp(value, "sunrise-sunset") == 0)
		*(int *)result = SUNTYPE_SS;
	else if (strcmp(value, "0") == 0)
		*(int *)result = SUNTYPE_SS;
	else if (strcasecmp(value, "device") == 0)
		*(int *)result = SUNTYPE_DEV;
	else if (strcmp(value, "1") == 0)
		*(int *)result = SUNTYPE_DEV;
	else if (strcmp(value, "internal") == 0)
		*(int *)result = SUNTYPE_INT;
	else if (strcmp(value, "2") == 0)
		*(int *)result = SUNTYPE_INT;
	else if (strcmp(value, "usno") == 0)
		*(int *)result = SUNTYPE_USNO;
	else if (strcmp(value, "3") == 0)
		*(int *)result = SUNTYPE_USNO;
	else {
		cfg_error(cfg, "invalid suntype value for option '%s': %s",
		    cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}

/**
   \brief Used to print sunmethod values
   \param opt option structure
   \param index number of option to print
   \param fp passed FILE
*/

static void conf_print_suntype(cfg_opt_t *opt, unsigned int index, FILE *fp)
{
	switch (cfg_opt_getnint(opt, index)) {
	case SUNTYPE_SS:
		fprintf(fp, "sunrise-sunset");
		break;
	case SUNTYPE_DEV:
		fprintf(fp, "device");
		break;
	case SUNTYPE_INT:
		fprintf(fp, "internal");
		break;
	case SUNTYPE_USNO:
		fprintf(fp, "usno");
		break;
	}
}

/**** Device calculation routines ****/

/**
   \brief Print the time with offset
   \param offset
   \return char * of current time
*/

static inline char *print_time(time_t offset)
{
	time_t now;

	now = time(NULL) + offset;
	return asctime(localtime(&now));
}

/**
   \brief Callback to modify sunrise device
   \param fd fd
   \param what why did it fire?
   \param arg pointer to integer from DAYLIGHT_TYPES enum;
*/

void modify_sunrise_cb(int fd, short what, void *arg)
{
	struct event *evn;
	struct timeval secs = { 0, 0 };
	device_t *dev;
	int *cbarg = (int *)arg;

	dev = find_device_byuid(cfg_getstr(astro_c, "sunriseuid"));
	if (dev == NULL)
		LOG(LOG_FATAL, "Lost my internal sunrise device");

	LOG(LOG_NOTICE, "Setting %s to %d", dev->name, *cbarg);
	store_data_dev(dev, DATALOC_DATA, cbarg);
	gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
}

/**
   \brief Callback to modify sunrise device
   \param fd fd
   \param what why did it fire?
   \param arg pointer to integer from DAYLIGHT_TYPES enum;
*/

void modify_moonrise_cb(int fd, short what, void *arg)
{
	struct event *evn;
	struct timeval secs = { 0, 0 };
	device_t *dev;
	int *cbarg = (int *)arg;

	dev = find_device_byuid(cfg_getstr(astro_c, "moonriseuid"));
	if (dev == NULL)
		LOG(LOG_FATAL, "Lost my internal moonrise device");

	LOG(LOG_NOTICE, "Setting %s to %d", dev->name, *cbarg);
	store_data_dev(dev, DATALOC_DATA, cbarg);
	gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
}

/*****
  Sunrise-Sunset API Control Code
*****/

/**
   \brief convert a time into an event and schedule
   \param str Time as a string, format: 2015-05-21T12:14:17+00:00
   \param daylight DAYLIGHT_TYPES enum value
   \param field SS_TIMES enum value
*/

void convert_sunrise_data(char *str, int daylight, int field)
{
	time_t curtime, event_time;
	double difft;
	struct tm event_tm;
	struct timeval secs = { 0, 0 };

	curtime = time(NULL);

	memset(&event_tm, 0, sizeof(event_tm));
	strptime(str, "%FT%T%z", &event_tm);
	event_time = mktime_z(NULL, &event_tm);
	difft = difftime(event_time, curtime);
	sunrise_offsets[field] = (int)round(difft);
	LOG(LOG_DEBUG, "Field:%d Dayl:%d offset:%f str:%s",
	    field, daylight, difft, str);
	/* special handling for solar noon */
	if (field == SS_SOLAR_NOON && (difft - 1800.0) > 0.0) {
		secs.tv_sec = sunrise_offsets[field] - 1800;
		if (solar_noon_start_ev != NULL)
			event_free(solar_noon_start_ev);
		solar_noon_start_ev = evtimer_new(base, modify_sunrise_cb,
					  (void*)&dl_cbarg[DAYL_SOLAR_NOON]);
		event_add(solar_noon_start_ev, &secs);
		LOG(LOG_NOTICE, "Create solar noon start at %s",
		    print_time(secs.tv_sec));

		secs.tv_sec = sunrise_offsets[field] + 1800;
		if (solar_noon_end_ev != NULL)
			event_free(solar_noon_end_ev);
		solar_noon_end_ev = evtimer_new(base, modify_sunrise_cb,
				       (void*)&dl_cbarg[DAYL_DAY]);
		event_add(solar_noon_end_ev, &secs);
		LOG(LOG_NOTICE, "Create solar noon end at %s",
		    print_time(secs.tv_sec));
	}
	if (difft > 0.0 && field != SS_SOLAR_NOON) {
		secs.tv_sec = sunrise_offsets[field];
		if (sun_ev[field] != NULL)
			event_free(sun_ev[field]); /* clear old one */
		sun_ev[field] = evtimer_new(base, modify_sunrise_cb,
					    (void*)&dl_cbarg[daylight]);
		event_add(sun_ev[field], &secs);
		LOG(LOG_NOTICE, "Create event %d start at %s", daylight,
		    print_time(secs.tv_sec));
	}
}

#define JSMN_TEST_OR_FAIL(ret, str)			  \
	if (ret == -1) {				  \
		LOG(LOG_ERROR, "Couldn't parse %s", str); \
		goto ss_request_cb_out;			  \
      	}

/**
   \brief Callback for http request (sunrise/sunset)
   \param req request structure
   \param arg unused
*/

void ss_request_cb(struct evhttp_request *req, void *arg)
{
	struct evbuffer *data;
	size_t len;
	char *buf, *str, dbuf[256];
	jsmn_parser jp;
	jsmntok_t token[255];
	int jret, i, j;
	char *apiwords[SS_SOLAR_NOON+1] = {
		"astronomical_twilight_begin",
		"nautical_twilight_begin",
		"civil_twilight_begin",
		"sunrise",
		"sunset",
		"civil_twilight_end",
		"nautical_twilight_end",
		"astronomical_twilight_end",
		"solar_noon",
	};
	int apidayl[SS_SOLAR_NOON+1] = {
		DAYL_DAWN_ASTRO_TWILIGHT,
		DAYL_DAWN_NAUTICAL_TWILIGHT,
		DAYL_DAWN_CIVIL_TWILIGHT,
		DAYL_DAY,
		DAYL_DUSK_CIVIL_TWILIGHT,
		DAYL_DUSK_NAUTICAL_TWILIGHT,
		DAYL_DUSK_ASTRO_TWILIGHT,
		DAYL_NIGHT,
		DAYL_SOLAR_NOON,
	};

	if (req == NULL) {
		LOG(LOG_ERROR, "Got NULL req in ss_request_cb() ??");
		return;
	}

	switch (req->response_code) {
	case HTTP_OK: break;
	default:
		LOG(LOG_ERROR, "Sunrise Http request failure: %d", req->response_code);
		return;
		break;
	}
	jsmn_init(&jp);

	data = evhttp_request_get_input_buffer(req);
	len = evbuffer_get_length(data);
	LOG(LOG_DEBUG, "input buf len= %d", len);
	if (len == 0)
		return;

	buf = safer_malloc(len+1);
	if (evbuffer_copyout(data, buf, len) != len) {
		LOG(LOG_ERROR, "Failed to copyout %d bytes", len);
		goto ss_request_cb_out;
	}
	buf[len] = '\0'; /* just in case of stupid */
	LOG(LOG_DEBUG, "input buf: %s", buf);
	jret = jsmn_parse(&jp, buf, len, token, 255);
	if (jret < 0) {
		LOG(LOG_ERROR, "Failed to parse jsom string: %d", jret);
		/* Leave the data on the queue and punt for more */
		/* XXX NEED switch for token error */
		goto ss_request_cb_out;
	} else
		evbuffer_drain(data, len); /* toss it */

	/* flush the offsets */
	for (i=0; i < SS_ASTRO_END + 1; i++)
		sunrise_offsets[i] = -86401; /* over a day */

	/* Now start parsing */

	for (j=SS_ASTRO_BEGIN;  j <= SS_SOLAR_NOON; j++) {
		i = jtok_find_token_val(token, buf, apiwords[j], jret);
		JSMN_TEST_OR_FAIL(i, apiwords[j]);
		str = jtok_string(&token[i], buf);
		convert_sunrise_data(str, apidayl[j], j);
		free(str);
	}

	/* if we got this far everything is a-ok */
	astro_lastupd = time(NULL);

	/* lets do an upd, just for funsies */
	if (sunrise_offsets[SS_SOLAR_NOON] > -1800 &&
	    sunrise_offsets[SS_SOLAR_NOON] < 1800) {
		modify_sunrise_cb(0, 0, &dl_cbarg[DAYL_SOLAR_NOON]);
	} else {
		/* we skip solar noon here */
		for (j=SS_ASTRO_BEGIN;  j < SS_ASTRO_END; j++) {
			if (sunrise_offsets[j] >= 0) {
				LOG(LOG_DEBUG, "Sunrise: j=%d offset=%d",
				    j, sunrise_offsets[j]);
				if (j > 0)
					j--;
				else
					j = SS_ASTRO_END;
				LOG(LOG_DEBUG, "Sunrise: Picked: %s j=%d "
				    "apidayl=%d", apiwords[j], j, apidayl[j]);
				modify_sunrise_cb(0, 0, &dl_cbarg[apidayl[j]]);
				break;
			}
		}
	}


ss_request_cb_out:
	free(buf);
	return;
}


/*****
  USNO API Control Code
*****/

/**
   \brief Set the lunar phase
   \param fracillum as a string ("%83")
*/

void modify_moonphase(char *frac)
{
	device_t *dev;
	int i;
	double d;

	dev = find_device_byuid(cfg_getstr(astro_c, "moonphaseuid"));
	if (dev == NULL)
		LOG(LOG_FATAL, "Lost my internal moonphase device");

	if (frac == NULL)
		return;

	for (i=0; i < strlen(frac); i++)
		if (frac[i] == '%')
			frac[i] = '\0';

	d = atof(frac);
	d /= 100.0;

	LOG(LOG_NOTICE, "Setting %s to %f", dev->name, d);
	store_data_dev(dev, DATALOC_DATA, &d);
	gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
}



/**
   \brief convert a time into an event and schedule
   \param str Time as a string, format: 12:14
   \param daylight DAYLIGHT_TYPES enum value
   \param field USNO_TIMES enum value
*/

void convert_usno_data(char *str, int daylight, int field)
{
	time_t curtime, event_time;
	double difft;
	struct tm event_tm;
	struct timeval secs = { 0, 0 };

	curtime = time(NULL);

	memset(&event_tm, 0, sizeof(event_tm));
	strptime(str, "%F %R", &event_tm);
	event_time = mktime_z(NULL, &event_tm);
	difft = difftime(event_time, curtime);
	usno_sun_offsets[field] = (int)round(difft);
	LOG(LOG_DEBUG, "Field:%d Dayl:%d offset:%f str:%s",
	    field, daylight, difft, str);
	/* special handling for solar noon */
	if (field == USNO_SUN_SOLAR_NOON && (difft - 1800.0) > 0.0) {
		secs.tv_sec = usno_sun_offsets[field] - 1800;
		if (solar_noon_start_ev != NULL)
			event_free(solar_noon_start_ev);
		solar_noon_start_ev = evtimer_new(base, modify_sunrise_cb,
					  (void*)&dl_cbarg[DAYL_SOLAR_NOON]);
		event_add(solar_noon_start_ev, &secs);
		LOG(LOG_NOTICE, "Create solar noon start at %s",
		    print_time(secs.tv_sec));

		secs.tv_sec = usno_sun_offsets[field] + 1800;
		if (solar_noon_end_ev != NULL)
			event_free(solar_noon_end_ev);
		solar_noon_end_ev = evtimer_new(base, modify_sunrise_cb,
				       (void*)&dl_cbarg[DAYL_DAY]);
		event_add(solar_noon_end_ev, &secs);
		LOG(LOG_NOTICE, "Create solar noon end at %s",
		    print_time(secs.tv_sec));
	}
	if (difft > 0.0 && field != USNO_SUN_SOLAR_NOON) {
		secs.tv_sec = usno_sun_offsets[field];
		if (sun_ev[field] != NULL)
			event_free(sun_ev[field]); /* clear old one */
		sun_ev[field] = evtimer_new(base, modify_sunrise_cb,
					    (void*)&dl_cbarg[daylight]);
		event_add(sun_ev[field], &secs);
		LOG(LOG_NOTICE, "Create event %d start at %s", daylight,
		    print_time(secs.tv_sec));
	}
}

/**
   \brief convert a time into an event and schedule, for lunar
   \param str Time as a string, format: 12:14
   \param daylight DAYLIGHT_TYPES enum value
   \param field USNO_TIMES enum value
   \note The API is a little obnoxious about fracillum, in that it doesn't
   display if the closestphase is today.  Argh.
*/

void convert_usno_moondata(char *str, int daylight, int field)
{
	time_t curtime, event_time;
	double difft;
	struct tm event_tm;
	struct timeval secs = { 0, 0 };

	curtime = time(NULL);

	memset(&event_tm, 0, sizeof(event_tm));
	strptime(str, "%F %R", &event_tm);
	event_time = mktime_z(NULL, &event_tm);
	difft = difftime(event_time, curtime);
	usno_moon_offsets[field] = (int)round(difft);
	LOG(LOG_DEBUG, "Moon Field:%d Dayl:%d offset:%f str:%s",
	    field, daylight, difft, str);
	/* special handling for lunar noon */
	if (field == USNO_MOON_UPPER_TRANSIT && (difft - 1800.0) > 0.0) {
		secs.tv_sec = usno_moon_offsets[field] - 1800;
		if (lunar_noon_start_ev != NULL)
			event_free(lunar_noon_start_ev);
		lunar_noon_start_ev = evtimer_new(base, modify_moonrise_cb,
					   (void*)&dl_cbarg[DAYL_SOLAR_NOON]);
		event_add(lunar_noon_start_ev, &secs);
		LOG(LOG_NOTICE, "Create lunar noon start at %s",
		    print_time(secs.tv_sec));

		secs.tv_sec = usno_moon_offsets[field] + 1800;
		if (lunar_noon_end_ev != NULL)
			event_free(lunar_noon_end_ev);
		lunar_noon_end_ev = evtimer_new(base, modify_moonrise_cb,
				       (void*)&dl_cbarg[DAYL_DAY]);
		event_add(lunar_noon_end_ev, &secs);
		LOG(LOG_NOTICE, "Create lunar noon end at %s",
		    print_time(secs.tv_sec));
	}
	if (difft > 0.0 && field != USNO_MOON_UPPER_TRANSIT) {
		secs.tv_sec = usno_moon_offsets[field];
		if (moon_ev[field] != NULL)
			event_free(moon_ev[field]); /* clear old one */
		moon_ev[field] = evtimer_new(base, modify_moonrise_cb,
					     (void*)&dl_cbarg[daylight]);
		event_add(moon_ev[field], &secs);
		LOG(LOG_NOTICE, "Create lunar event %d start at %s", daylight,
		    print_time(secs.tv_sec));
	}
}

#define JSMN_TEST_OR_FAIL2(ret, str)			  \
	if (ret == -1) {				  \
		LOG(LOG_ERROR, "Couldn't parse %s", str); \
		goto usno_request_cb_out;		  \
      	}

/**
   \brief Callback for http request (usno)
   \param req request structure
   \param arg unused
*/

void usno_request_cb(struct evhttp_request *req, void *arg)
{
	struct evbuffer *data;
	size_t len;
	char *buf, *str, dbuf[256], datestr[256];
	jsmn_parser jp;
	jsmntok_t token[255];
	int jret, i, j, k, datamemb, year, month, day, sm;
	struct tm tms;
	char *apiwords[USNO_SUN_SOLAR_NOON+1] = {
		"BC", "R", "S", "EC", "U",
	};
	int apidayl[USNO_SUN_SOLAR_NOON+1] = {
		DAYL_DAWN_CIVIL_TWILIGHT,
		DAYL_DAY,
		DAYL_DUSK_CIVIL_TWILIGHT,
		DAYL_NIGHT,
		DAYL_SOLAR_NOON,
	};
	char *moonwords[USNO_MOON_UPPER_TRANSIT+1] = {
		"R", "S", "U",
	};
	int moondayl[USNO_MOON_UPPER_TRANSIT+1] = {
		DAYL_DAY,
		DAYL_NIGHT,
		DAYL_SOLAR_NOON,
	};


	if (req == NULL) {
		LOG(LOG_ERROR, "Got NULL req in usno_request_cb() ??");
		return;
	}

	switch (req->response_code) {
	case HTTP_OK: break;
	default:
		LOG(LOG_ERROR, "USNO Http request failure: %d", req->response_code);
		return;
		break;
	}
	jsmn_init(&jp);

	data = evhttp_request_get_input_buffer(req);
	len = evbuffer_get_length(data);
	LOG(LOG_DEBUG, "input buf len= %d", len);
	if (len == 0)
		return;

	buf = safer_malloc(len+1);
	if (evbuffer_copyout(data, buf, len) != len) {
		LOG(LOG_ERROR, "Failed to copyout %d bytes", len);
		goto usno_request_cb_out;
	}
	buf[len] = '\0'; /* just in case of stupid */
	LOG(LOG_DEBUG, "input buf: %s", buf);
	jret = jsmn_parse(&jp, buf, len, token, 255);
	if (jret < 0) {
		LOG(LOG_ERROR, "Failed to parse jsom string: %d", jret);
		/* Leave the data on the queue and punt for more */
		/* XXX NEED switch for token error */
		goto usno_request_cb_out;
	} else
		evbuffer_drain(data, len); /* toss it */

	/* flush the offsets */
	for (i=0; i <= USNO_SUN_SOLAR_NOON; i++)
		usno_sun_offsets[i] = -86401; /* over a day */
	for (i=0; i <= USNO_MOON_UPPER_TRANSIT; i++)
		usno_moon_offsets[i] = -86401; /* over a day */

	/* Now start parsing */

	/* first, locate the year/month/day items */

	i = jtok_find_token_val(token, buf, "year", jret);
	JSMN_TEST_OR_FAIL2(i, "year");
	year = jtok_int(&token[i], buf);
	i = jtok_find_token_val(token, buf, "month", jret);
	JSMN_TEST_OR_FAIL2(i, "month");
	month = jtok_int(&token[i], buf);
	i = jtok_find_token_val(token, buf, "day", jret);
	JSMN_TEST_OR_FAIL2(i, "day");
	day = jtok_int(&token[i], buf);
	LOG(LOG_DEBUG, "Date: %d-%d-%d", year, month, day);

	/* now find the sundata */

	/* if we are just using usno for lunar data, skip ahead */
	sm = cfg_getint(astro_c, "sunmethod");
	if (sm != SUNTYPE_USNO)
		goto usno_moon;

	datamemb = jtok_get_array_size(token, buf, "sundata", jret);
	if (datamemb == -1)
		goto usno_request_cb_out;

	for (j=USNO_SUN_CIVIL_BEGIN;  j <= USNO_SUN_SOLAR_NOON; j++) {
		for (k=0; k < datamemb; k++) {
			i = jtok_find_token_val_nth_array(token, buf, k, 
							  "sundata", "phen",
							  jret);
			JSMN_TEST_OR_FAIL2(i, "phen");
			str = jtok_string(&token[i], buf);
			if (strcmp(str, apiwords[j]) == 0) {
				LOG(LOG_DEBUG, "Found phen %s", apiwords[j]);
				i = jtok_find_token_val_nth_array(token, buf,
				    k, "sundata", "time", jret);
				JSMN_TEST_OR_FAIL2(i, "time");
				str = jtok_string(&token[i], buf);
				LOG(LOG_DEBUG, "Found time for phen %s : %s",
				    apiwords[j], str);
				sprintf(datestr, "%d-%d-%d %s", year, month,
					day, str);
				convert_usno_data(datestr, apidayl[j], j);
				free(str);
			} else
				free(str);
		}
	}

	/* we might have future data, lets see */
	datamemb = jtok_get_array_size(token, buf, "nextsundata", jret);
	if (datamemb == -1)
		goto usno_skip_nextsun;

	/* mktime auto-adjusts for insanity, so day+1 actually works */
	sprintf(datestr, "%d-%d-%d 00:00", year, month, day+1);
	strptime(datestr, "%F %R", &tms);
	mktime_z(NULL, &tms);
	strftime_z(NULL, dbuf, 256, "%F", &tms);
	LOG(LOG_DEBUG, "Day +1 == %s", dbuf);

	for (j=USNO_SUN_CIVIL_BEGIN;  j <= USNO_SUN_SOLAR_NOON; j++) {
		for (k=0; k < datamemb; k++) {
			i = jtok_find_token_val_nth_array(token, buf, k, 
			    "nextsundata", "phen", jret);
			JSMN_TEST_OR_FAIL2(i, "phen");
			str = jtok_string(&token[i], buf);
			if (strcmp(str, apiwords[j]) == 0) {
				LOG(LOG_DEBUG, "Found nextphen %s", apiwords[j]);
				i = jtok_find_token_val_nth_array(token, buf,
				    k, "sundata", "time", jret);
				JSMN_TEST_OR_FAIL2(i, "time");
				str = jtok_string(&token[i], buf);
				LOG(LOG_DEBUG, "Found nexttime for phen %s : %s",
				    apiwords[j], str);
				sprintf(datestr, "%s %s", dbuf, str);
				convert_usno_data(datestr, apidayl[j], j);
				free(str);
			} else
				free(str);
		}
	}

usno_skip_nextsun:
	/* if we got this far everything is a-ok */
	astro_lastupd = time(NULL);

	/* lets do an upd, just for funsies */
	if (usno_sun_offsets[USNO_SUN_SOLAR_NOON] > -1800 &&
	    usno_sun_offsets[USNO_SUN_SOLAR_NOON] < 1800) {
		modify_sunrise_cb(0, 0, &dl_cbarg[DAYL_SOLAR_NOON]);
	} else {
		/* we skip solar noon here */
		for (j=USNO_SUN_CIVIL_BEGIN;  j < USNO_SUN_CIVIL_END; j++) {
			if (usno_sun_offsets[j] >= 0) {
				LOG(LOG_DEBUG, "Sunrise: j=%d offset=%d",
				    j, usno_sun_offsets[j]);
				if (j > 0)
					j--;
				else
					j = USNO_SUN_CIVIL_END;
				LOG(LOG_DEBUG, "Sunrise: Picked: %s j=%d "
				    "apidayl=%d", apiwords[j], j, apidayl[j]);
				modify_sunrise_cb(0, 0, &dl_cbarg[apidayl[j]]);
				break;
			}
		}
	}
	LOG(LOG_DEBUG, "Hi");

usno_moon:

	/* Lets get the moon data! */

	datamemb = jtok_get_array_size(token, buf, "moondata", jret);
	if (datamemb == -1)
		goto usno_request_cb_out;

	for (j=USNO_MOONRISE;  j <= USNO_MOON_UPPER_TRANSIT; j++) {
		for (k=0; k < datamemb; k++) {
		i = jtok_find_token_val_nth_array(token, buf, k, 
						  "moondata", "phen", jret);
			JSMN_TEST_OR_FAIL2(i, "phen");
			str = jtok_string(&token[i], buf);
			if (strcmp(str, moonwords[j]) == 0) {
				LOG(LOG_DEBUG, "Found phen %s", moonwords[j]);
				i = jtok_find_token_val_nth_array(token, buf,
				    k, "moondata", "time", jret);
				JSMN_TEST_OR_FAIL2(i, "time");
				str = jtok_string(&token[i], buf);
				LOG(LOG_DEBUG, "Found time for phen %s : %s",
				    moonwords[j], str);
				sprintf(datestr, "%d-%d-%d %s", year, month,
					day, str);
				convert_usno_moondata(datestr, moondayl[j], j);
				free(str);
			} else
				free(str);
		}
	}
	/* if we got this far everything is a-ok */
	astro_lastupd = time(NULL);

	/* lets do an upd, just for funsies */
	if (usno_moon_offsets[USNO_MOON_UPPER_TRANSIT] > -1800 &&
	    usno_moon_offsets[USNO_MOON_UPPER_TRANSIT] < 1800) {
		modify_moonrise_cb(0, 0, &dl_cbarg[DAYL_SOLAR_NOON]);
	} else {
		/* we skip solar noon here */
		if (usno_moon_offsets[USNO_MOONRISE] <= 0 &&
		    usno_moon_offsets[USNO_MOONSET] <= 0 &&
		    usno_moon_offsets[USNO_MOONRISE] < 
		    usno_moon_offsets[USNO_MOONSET]) {
			LOG(LOG_DEBUG, "Moon is down");
			modify_moonrise_cb(0, 0, &dl_cbarg[DAYL_NIGHT]);
		} else if (usno_moon_offsets[USNO_MOONRISE] > 
			   usno_moon_offsets[USNO_MOONSET]) {
			LOG(LOG_DEBUG, "Moon is up");
			modify_moonrise_cb(0, 0, &dl_cbarg[DAYL_DAY]);
		} else {
			LOG(LOG_DEBUG, "Assuming moon down");
			modify_moonrise_cb(0, 0, &dl_cbarg[DAYL_NIGHT]);
		}
	}

	/* Locate the phase of the moon */
	i = jtok_find_token_val(token, buf, "fracillum", jret);
	JSMN_TEST_OR_FAIL2(i, "fracillum");
	str = jtok_string(&token[i], buf);
	modify_moonphase(str);
	free(str);


usno_request_cb_out:
	free(buf);
	return;
}


/**
   \brief Build the astro devices
*/

void build_devices(void)
{
	char *suid, *muid, *sname, *mname, *mtuid, *mtname;
	device_t *dev;

	suid = cfg_getstr(astro_c, "sunriseuid");
	sname = cfg_getstr(astro_c, "sunrisename");

	muid = cfg_getstr(astro_c, "moonphaseuid");
	mname = cfg_getstr(astro_c, "moonphasename");

	mtuid = cfg_getstr(astro_c, "moonriseuid");
	mtname = cfg_getstr(astro_c, "moonrisename");

	generic_build_device(cfg, suid, sname, "sunrise",
			     PROTO_CALCULATED,
			     DEVICE_SENSOR, SUBTYPE_DAYLIGHT, NULL, 0,
			     gnhastd_conn->bev);

	generic_build_device(cfg, muid, mname, "moonph",
			     PROTO_CALCULATED, DEVICE_SENSOR,
			     SUBTYPE_MOONPH, NULL, 0, gnhastd_conn->bev);

	generic_build_device(cfg, mtuid, mtname, "moonrise",
			     PROTO_CALCULATED,
			     DEVICE_SENSOR, SUBTYPE_DAYLIGHT, NULL, 0,
			     gnhastd_conn->bev);

	/* are we dumping the conf file? */
	if (dumpconf != NULL) {
		LOG(LOG_NOTICE, "Dumping config file to "
		    "%s and exiting", dumpconf);
		dump_conf(cfg, 0, dumpconf);
		exit(0);
	}
}

/**
   \brief Start the feed for the sunrise/sunset API
   We have to delay start the feeds, otherwise we could overwhelm the
   website.  Therefore this routine does a silly double jump thingie.
*/

void sunrise_sunset_startfeed(void)
{
	struct event *ev;
	struct timeval secs = { 0, 0 };
	int feed_delay, update;
	char url_suffix[512];

	if (sunrise_sunset_url == NULL)
		LOG(LOG_FATAL, "Sunrise-sunset URL is NULL");

	query_running = 1;

	update = cfg_getint(astro_c, "update");
	if (update < 3600) {
		update = 3600; /* 1 hour */
		cfg_setint(astro_c, "update", update);
		LOG(LOG_WARNING, "Update is too fast, adjusting to %d",
		    update);
	}
	feed_delay = 2;

	sprintf(url_suffix, "%slat=%f&lng=%f&date=today&formatted=0",
		SUNRISE_SUNSET_API,
		cfg_getfloat(astro_c, "latitude"),
		cfg_getfloat(astro_c, "longitude"));

	sunrise_sunset_get = smalloc(http_get_t);
	sunrise_sunset_get->url_prefix = sunrise_sunset_url;
	sunrise_sunset_get->url_suffix = strdup(url_suffix);
	sunrise_sunset_get->cb = ss_request_cb;
	sunrise_sunset_get->http_port = SUNRISE_SUNSET_PORT;
	sunrise_sunset_get->precheck = NULL;
	sunrise_sunset_get->http_cn = NULL;

	secs.tv_sec = feed_delay;
	ev = evtimer_new(base, delay_feedstart_cb, sunrise_sunset_get);
	event_add(ev, &secs);
}

/**
   \brief Start the feed for the usno API
   We have to delay start the feeds, otherwise we could overwhelm the
   website.  Therefore this routine does a silly double jump thingie.
*/

void usno_startfeed(void)
{
	struct event *ev;
	struct timeval secs = { 0, 0 };
	int feed_delay, update;
	char url_suffix[512];

	if (usno_url == NULL)
		LOG(LOG_FATAL, "USNO URL is NULL");

	query_running = 1;

	update = cfg_getint(astro_c, "update");
	if (update < 3600) {
		update = 3600; /* 1 hour */
		cfg_setint(astro_c, "update", update);
		LOG(LOG_WARNING, "Update is too fast, adjusting to %d",
		    update);
	}
	feed_delay = 5;

	sprintf(url_suffix, "%sdate=today&coords=%f,%f&tz=0",
		USNO_API,
		cfg_getfloat(astro_c, "latitude"),
		cfg_getfloat(astro_c, "longitude"));

	usno_get = smalloc(http_get_t);
	usno_get->url_prefix = usno_url;
	usno_get->url_suffix = strdup(url_suffix);
	usno_get->cb = usno_request_cb;
	usno_get->http_port = USNO_PORT;
	usno_get->precheck = NULL;
	usno_get->http_cn = NULL;

	secs.tv_sec = feed_delay;
	ev = evtimer_new(base, delay_feedstart_cb, usno_get);
	event_add(ev, &secs);
}


/**
   \brief Callback to start a feed
   \param fd fd
   \param what why did it fire?
   \param arg pointer to http_get_t;
*/

void delay_feedstart_cb(int fd, short what, void *arg)
{
	struct event *evn;
	struct timeval secs = { 0, 0 };
	http_get_t *get = (http_get_t *)arg;

	secs.tv_sec = cfg_getint(astro_c, "update");
	evn = event_new(base, -1, EV_PERSIST, cb_http_GET, get);
	event_add(evn, &secs);
	LOG(LOG_NOTICE, "Starting %s updates every %d seconds",
	    get->url_prefix, secs.tv_sec);

	/* and do one now */
	cb_http_GET(0, 0, get);
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
	int ch, fd, sm, i;
	char *buf;
	struct event *ev;
	struct timeval secs = {0, 0};

	/* process command line arguments */
	while ((ch = getopt(argc, argv, "?c:dm:")) != -1)
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
		default:
		case '?':	/* you blew it */
			(void)fprintf(stderr, "usage:\n%s [-c configfile]"
				      "[-m dumpconfigfile]\n", getprogname());
			return(EXIT_FAILURE);
			/*NOTREACHED*/
			break;
		}

	if (!debugmode && !dumpconf)
		if (daemon(0, 0) == -1)
			LOG(LOG_FATAL, "Failed to daemonize: %s",
			    strerror(errno));
	/* Initialize the event system */
	base = event_base_new();
	dns_base = evdns_base_new(base, 1);
	astro_lastupd = time(NULL);

	/* Initialize the argtable */
	init_argcomm();
	/* Initialize the command table */
	init_commands();
	/* Initialize the device table */
	init_devtable(cfg, 0);

	cfg = parse_conf(conffile);

	if (!debugmode)
		logfile = openlog(cfg_getstr(cfg, "logfile"));

	writepidfile(cfg_getstr(cfg, "pidfile"));

	/* First, parse the astro section */

	if (cfg) {
		astro_c = cfg_getsec(cfg, "astrocoll");
		if (!astro_c)
			LOG(LOG_FATAL, "Error reading config file, "
			    "astro section");
	}

	/* Now, parse the details of connecting to the gnhastd server */

	if (cfg) {
		gnhastd_c = cfg_getsec(cfg, "gnhastd");
		if (!gnhastd_c)
			LOG(LOG_FATAL, "Error reading config file, "
			    "gnhastd section");
	}

	/* Setup */
	gnhastd_conn = smalloc(connection_t);
	gnhastd_conn->cname = strdup(COLLECTOR_NAME);
	gnhastd_conn->port = cfg_getint(gnhastd_c, "port");
	gnhastd_conn->type = CONN_TYPE_GNHASTD;
	gnhastd_conn->host = cfg_getstr(gnhastd_c, "hostname");
	/* cheat, and directly call the timer callback
	   This sets up a connection to the server. */
	generic_connect_server_cb(0, 0, gnhastd_conn);
	collector_instance = cfg_getint(astro_c, "instance");
	gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);

	build_devices();
	LOG(LOG_DEBUG, "method = %d SS=%d", cfg_getint(astro_c, "sunmethod"), SUNTYPE_SS);

	/* Setup the callback arguments array */
	dl_cbarg[DAYL_NIGHT] = DAYL_NIGHT;
	dl_cbarg[DAYL_DAY] = DAYL_DAY;
	dl_cbarg[DAYL_SOLAR_NOON] = DAYL_SOLAR_NOON;
	dl_cbarg[DAYL_DAWN_ASTRO_TWILIGHT] = DAYL_DAWN_ASTRO_TWILIGHT;
	dl_cbarg[DAYL_DAWN_NAUTICAL_TWILIGHT] = DAYL_DAWN_NAUTICAL_TWILIGHT;
	dl_cbarg[DAYL_DAWN_CIVIL_TWILIGHT] = DAYL_DAWN_CIVIL_TWILIGHT;
	dl_cbarg[DAYL_DUSK_CIVIL_TWILIGHT] = DAYL_DUSK_CIVIL_TWILIGHT;
	dl_cbarg[DAYL_DUSK_NAUTICAL_TWILIGHT] = DAYL_DUSK_NAUTICAL_TWILIGHT;
	dl_cbarg[DAYL_DUSK_ASTRO_TWILIGHT] = DAYL_DUSK_ASTRO_TWILIGHT;

	/* initialize the events to NULL */
	for (i=0; i <= SS_SOLAR_NOON; i++)
		sun_ev[i] = NULL;
	for (i=0; i <= USNO_MOON_UPPER_TRANSIT; i++)
		moon_ev[i] = NULL;
	solar_noon_start_ev = solar_noon_end_ev = NULL;
	lunar_noon_start_ev = lunar_noon_end_ev = NULL;
		
	sm = cfg_getint(astro_c, "sunmethod");
	if (sm == SUNTYPE_SS)
		sunrise_sunset_startfeed();

	/* we do this either way, because moon */
	usno_startfeed();

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

	closelog();
	delete_pidfile();
	return(0);
}
