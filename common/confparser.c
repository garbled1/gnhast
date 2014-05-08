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
   \file confparser.c
   \brief Contains generic routines to parse the config files
   \author Tim Rightnour
*/

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>  
#include <stdlib.h>
#include <math.h>

#include "confuse.h"
#include "config.h"
#include "common.h"
#include "gnhast.h"
#include "confparser.h"

/* Externs */
extern cfg_opt_t options[];

static int conf_parse_proto(cfg_t *cfg, cfg_opt_t *opt, const char *value,
			      void *result);

cfg_opt_t device_opts[] = {
	CFG_STR("name", 0, CFGF_NODEFAULT),
	CFG_STR("loc", 0, CFGF_NODEFAULT),
	CFG_STR("rrdname", 0, CFGF_NODEFAULT),
	CFG_INT_CB("subtype", 0, CFGF_NODEFAULT, conf_parse_subtype),
	CFG_INT_CB("type", 0, CFGF_NODEFAULT, conf_parse_type),
	CFG_INT_CB("proto", 0, CFGF_NODEFAULT, conf_parse_proto),
	CFG_INT_CB("tscale", 0, CFGF_NONE, conf_parse_tscale),
	CFG_INT_CB("baroscale", 0, CFGF_NONE, conf_parse_baroscale),
	CFG_INT_CB("speedscale", 0, CFGF_NONE, conf_parse_speedscale),
	CFG_INT_CB("lengthscale", 0, CFGF_NONE, conf_parse_lscale),
	CFG_INT_CB("lightscale", 0, CFGF_NONE, conf_parse_lightscale),
	CFG_STR("multimodel", 0, CFGF_NODEFAULT),
	CFG_STR("handler", 0, CFGF_NODEFAULT),
	CFG_STR_LIST("hargs", 0, CFGF_NODEFAULT),
	CFG_INT_CB("spamhandler", 0, CFGF_NONE, conf_parse_bool),
	CFG_FLOAT("hiwat", 0.0, CFGF_NONE),
	CFG_FLOAT("lowat", 0.0, CFGF_NONE),
	CFG_END(),
};

/**
   \brief Validate that a port option is valid
*/
int conf_validate_port(cfg_t *cfg, cfg_opt_t *opt)
{
	int value = cfg_opt_getnint(opt, 0);

	if (value <= 0) {
		LOG(LOG_ERROR, "Invalid port %d in section %s:",
		    value, cfg_name(cfg));
		return -1;
	}
	return 0;
}

/**
   \brief Validate an rrdname is valid
*/
int conf_validate_rrdname(cfg_t *cfg, cfg_opt_t *opt)
{
	char *value = cfg_opt_getnstr(opt, 0);
	char *p;
	int i;

	if (strlen(value) > 19) {
		LOG(LOG_ERROR, "rrdname %s is longer than 19 chars in "
		    "section %s", value, cfg_name(cfg));
		return -1;
	}
	for (p=value, i=0; i < strlen(value); *p++, i++) {
		if (isalpha(*p) || isdigit(*p) || *p == '_')
			continue;
		else {
			LOG(LOG_ERROR, "Invalid char %c in rrdname %s, "
			    "section %s", *p, value, cfg_name(cfg));
			return -1;
		}
	}
	return 0;
}

/**
   \brief parse a bool
*/
int conf_parse_bool(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		    void *result)
{
	if (strcasecmp(value, "yes") == 0)
		*(int *)result = 1;
	else if (strcasecmp(value, "true") == 0)
		*(int *)result = 1;
	else if (strcasecmp(value, "1") == 0)
		*(int *)result = 1;
	else if (strcasecmp(value, "no") == 0)
		*(int *)result = 0;
	else if (strcasecmp(value, "false") == 0)
		*(int *)result = 0;
	else if (strcasecmp(value, "0") == 0)
		*(int *)result = 0;
	else {
		cfg_error(cfg, "invalid bool value for option '%s': %s",
		    cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}

/**
   \brief parse a protocol
*/
static int conf_parse_proto(cfg_t *cfg, cfg_opt_t *opt, const char *value,
			      void *result)
{
	if (strcmp(value, "insteon-v1") == 0)
		*(int *)result = PROTO_INSTEON_V1;
	else if (strcmp(value, "insteon-v2") == 0)
		*(int *)result = PROTO_INSTEON_V2;
	else if (strcmp(value, "insteon-v2cs") == 0)
		*(int *)result = PROTO_INSTEON_V2CS;
	else if (strcmp(value, "sensor-owfs") == 0)
		*(int *)result = PROTO_SENSOR_OWFS;
	else if (strcmp(value, "brultech-gem") == 0)
		*(int *)result = PROTO_SENSOR_BRULTECH_GEM;
	else if (strcmp(value, "brultech-ecm1240") == 0)
		*(int *)result = PROTO_SENSOR_BRULTECH_ECM1240;
	else if (strcmp(value, "wmr918") == 0)
		*(int *)result = PROTO_SENSOR_WMR918;
	else if (strcmp(value, "ad2usb") == 0)
		*(int *)result = PROTO_SENSOR_AD2USB;
	else if (strcmp(value, "icaddy") == 0)
		*(int *)result = PROTO_SENSOR_ICADDY;
	else if (strcmp(value, "venstar") == 0)
		*(int *)result = PROTO_SENSOR_VENSTAR;
	else {
		cfg_error(cfg, "invalid value for option '%s': %s",
		    cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}

/**
   \brief Used to print protocols
   \param opt option structure
   \param index number of option to print
   \param fp passed FILE
*/

static void conf_print_proto(cfg_opt_t *opt, unsigned int index, FILE *fp)
{
	switch (cfg_opt_getnint(opt, index)) {
	case PROTO_NONE:
		fprintf(fp, "NONE");
		break;
	case PROTO_INSTEON_V1:
		fprintf(fp, "insteon-v1");
		break;
	case PROTO_INSTEON_V2:
		fprintf(fp, "insteon-v2");
		break;
	case PROTO_INSTEON_V2CS:
		fprintf(fp, "insteon-v2cs");
		break;
	case PROTO_SENSOR_OWFS:
		fprintf(fp, "sensor-owfs");
		break;
	case PROTO_SENSOR_BRULTECH_GEM:
		fprintf(fp, "brultech-gem");
		break;
	case PROTO_SENSOR_BRULTECH_ECM1240:
		fprintf(fp, "brultech-ecm1240");
		break;
	case PROTO_SENSOR_WMR918:
		fprintf(fp, "wmr918");
		break;
	case PROTO_SENSOR_AD2USB:
		fprintf(fp, "ad2usb");
		break;
	case PROTO_SENSOR_ICADDY:
		fprintf(fp, "icaddy");
		break;
	case PROTO_SENSOR_VENSTAR:
		fprintf(fp, "venstar");
		break;
	default:
		fprintf(fp, "NONE");
		break;
	}
}

/**
   \brief parse a type
*/
int conf_parse_type(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		    void *result)
{
	if (strcmp(value,"switch") == 0)
		*(int *)result = DEVICE_SWITCH;
	else if (strcmp(value,"dimmer") == 0)
		*(int *)result = DEVICE_DIMMER;
	else if (strcmp(value,"sensor") == 0)
		*(int *)result = DEVICE_SENSOR;
	else if (strcmp(value,"timer") == 0)
		*(int *)result = DEVICE_TIMER;
	else {
		cfg_error(cfg, "invalid value for option '%s': %s",
		    cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}

/**
   \brief Used to print bool values
   \param opt option structure
   \param index number of option to print
   \param fp passed FILE
*/

static void conf_print_bool(cfg_opt_t *opt, unsigned int index, FILE *fp)
{
	switch (cfg_opt_getnint(opt, index)) {
	case 0:
		fprintf(fp, "no");
		break;
	case 1:
		fprintf(fp, "yes");
		break;
	default:
		fprintf(fp, "no");
		break;
	}
}

/**
   \brief Used to print types
   \param opt option structure
   \param index number of option to print
   \param fp passed FILE
*/

static void conf_print_type(cfg_opt_t *opt, unsigned int index, FILE *fp)
{
	switch (cfg_opt_getnint(opt, index)) {
	case DEVICE_NONE:
		fprintf(fp, "NONE");
		break;
	case DEVICE_SWITCH:
		fprintf(fp, "switch");
		break;
	case DEVICE_DIMMER:
		fprintf(fp, "dimmer");
		break;
	case DEVICE_SENSOR:
		fprintf(fp, "sensor");
		break;
	case DEVICE_TIMER:
		fprintf(fp, "timer");
		break;
	default:
		fprintf(fp, "NONE");
		break;
	}
}

/**
   \brief parse a subtype by name
*/

int conf_parse_subtype(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		       void *result)
{
	if (strcmp(value, "switch") == 0)
		*(int *)result = SUBTYPE_SWITCH;
	else if (strcmp(value, "outlet") == 0)
		*(int *)result = SUBTYPE_OUTLET;
	else if (strcmp(value, "temp") == 0)
		*(int *)result = SUBTYPE_TEMP;
	else if (strcmp(value, "humid") == 0)
		*(int *)result = SUBTYPE_HUMID;
	else if (strcmp(value, "lux") == 0)
		*(int *)result = SUBTYPE_LUX;
	else if (strcmp(value, "bool") == 0)
		*(int *)result = SUBTYPE_BOOL;
	else if (strcmp(value, "counter") == 0)
		*(int *)result = SUBTYPE_COUNTER;
	else if (strcmp(value, "pressure") == 0)
		*(int *)result = SUBTYPE_PRESSURE;
	else if (strcmp(value, "windspeed") == 0)
		*(int *)result = SUBTYPE_SPEED;
	else if (strcmp(value, "winddir") == 0)
		*(int *)result = SUBTYPE_DIR;
	else if (strcmp(value, "moisture") == 0)
		*(int *)result = SUBTYPE_MOISTURE;
	else if (strcmp(value, "wetness") == 0)
		*(int *)result = SUBTYPE_WETNESS;
	else if (strcmp(value, "hub") == 0)
		*(int *)result = SUBTYPE_HUB;
	else if (strcmp(value, "voltage") == 0)
		*(int *)result = SUBTYPE_VOLTAGE;
	else if (strcmp(value, "wattsec") == 0)
		*(int *)result = SUBTYPE_WATTSEC;
	else if (strcmp(value, "watt") == 0)
		*(int *)result = SUBTYPE_WATT;
	else if (strcmp(value, "amps") == 0)
		*(int *)result = SUBTYPE_AMPS;
	else if (strcmp(value, "rainrate") == 0)
		*(int *)result = SUBTYPE_RAINRATE;
	else if (strcmp(value, "weather") == 0)
		*(int *)result = SUBTYPE_WEATHER;
	else if (strcmp(value, "alarmstatus") == 0)
		*(int *)result = SUBTYPE_ALARMSTATUS;
	else if (strcmp(value, "number") == 0)
		*(int *)result = SUBTYPE_NUMBER;
	else if (strcmp(value, "percentage") == 0)
		*(int *)result = SUBTYPE_PERCENTAGE;
	else if (strcmp(value, "flowrate") == 0)
		*(int *)result = SUBTYPE_FLOWRATE;
	else if (strcmp(value, "distance") == 0)
		*(int *)result = SUBTYPE_DISTANCE;
	else if (strcmp(value, "volume") == 0)
		*(int *)result = SUBTYPE_VOLUME;
	else if (strcmp(value, "timer") == 0)
		*(int *)result = SUBTYPE_TIMER;
	else if (strcmp(value, "thmode") == 0)
		*(int *)result = SUBTYPE_THMODE;
	else if (strcmp(value, "thstate") == 0)
		*(int *)result = SUBTYPE_THSTATE;
	else if (strcmp(value, "smnumber") == 0)
		*(int *)result = SUBTYPE_SMNUMBER;
	else {
		cfg_error(cfg, "invalid value for option '%s': %s",
		    cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}


/**
	\brief Used to print subtypes
	\param opt option structure
	\param index number of option to print
	\param fp passed FILE
*/

void conf_print_subtype(cfg_opt_t *opt, unsigned int index, FILE *fp)
{
	switch (cfg_opt_getnint(opt, index)) {
	case SUBTYPE_NONE:
		fprintf(fp, "NONE");
		break;
	case SUBTYPE_SWITCH:
		fprintf(fp, "switch");
		break;
	case SUBTYPE_OUTLET:
		fprintf(fp, "outlet");
		break;
	case SUBTYPE_TEMP:
		fprintf(fp, "temp");
		break;
	case SUBTYPE_HUMID:
		fprintf(fp, "humid");
		break;
	case SUBTYPE_LUX:
		fprintf(fp, "lux");
		break;
	case SUBTYPE_BOOL:
		fprintf(fp, "bool");
		break;
	case SUBTYPE_COUNTER:
		fprintf(fp, "counter");
		break;
	case SUBTYPE_PRESSURE:
		fprintf(fp, "pressure");
		break;
	case SUBTYPE_SPEED:
		fprintf(fp, "windspeed");
		break;
	case SUBTYPE_DIR:
		fprintf(fp, "winddir");
		break;
	case SUBTYPE_MOISTURE:
		fprintf(fp, "moisture");
		break;
	case SUBTYPE_WETNESS:
		fprintf(fp, "wetness");
		break;
	case SUBTYPE_HUB:
		fprintf(fp, "hub");
		break;
	case SUBTYPE_VOLTAGE:
		fprintf(fp, "voltage");
		break;
	case SUBTYPE_WATTSEC:
		fprintf(fp, "wattsec");
		break;
	case SUBTYPE_WATT:
		fprintf(fp, "watt");
		break;
	case SUBTYPE_AMPS:
		fprintf(fp, "amps");
		break;
	case SUBTYPE_RAINRATE:
		fprintf(fp, "rainrate");
		break;
	case SUBTYPE_WEATHER:
		fprintf(fp, "weather");
		break;
	case SUBTYPE_ALARMSTATUS:
		fprintf(fp, "alarmstatus");
		break;
	case SUBTYPE_NUMBER:
		fprintf(fp, "number");
		break;
	case SUBTYPE_PERCENTAGE:
		fprintf(fp, "percentage");
		break;
	case SUBTYPE_FLOWRATE:
		fprintf(fp, "flowrate");
		break;
	case SUBTYPE_DISTANCE:
		fprintf(fp, "distance");
		break;
	case SUBTYPE_VOLUME:
		fprintf(fp, "volume");
		break;
	case SUBTYPE_TIMER:
		fprintf(fp, "timer");
		break;
	case SUBTYPE_THMODE:
		fprintf(fp, "thmode");
		break;
	case SUBTYPE_THSTATE:
		fprintf(fp, "thstate");
		break;
	case SUBTYPE_SMNUMBER:
		fprintf(fp, "smnumber");
		break;
	default:
		fprintf(fp, "NONE");
		break;
	}
}

/**
   \brief parse a temperature scale
*/
int conf_parse_tscale(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		    void *result)
{
	if (strcasecmp(value, "F") == 0)
		*(int *)result = TSCALE_F;
	else if (strcasecmp(value, "FAHRENHEIT") == 0)
		*(int *)result = TSCALE_F;
	else if (strcasecmp(value, "C") == 0)
		*(int *)result = TSCALE_C;
	else if (strcasecmp(value, "CELCIUS") == 0)
		*(int *)result = TSCALE_C;
	else if (strcasecmp(value, "K") == 0)
		*(int *)result = TSCALE_K;
	else if (strcasecmp(value, "KELVIN") == 0)
		*(int *)result = TSCALE_K;
	else if (strcasecmp(value, "R") == 0)
		*(int *)result = TSCALE_R;
	else if (strcasecmp(value, "RANKINE") == 0)
		*(int *)result = TSCALE_R;
	else {
		cfg_error(cfg, "invalid temp scale value for option '%s': %s",
		    cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}

/**
   \brief Used to print temp scale values
   \param opt option structure
   \param index number of option to print
   \param fp passed FILE
*/

void conf_print_tscale(cfg_opt_t *opt, unsigned int index, FILE *fp)
{
	switch (cfg_opt_getnint(opt, index)) {
	case TSCALE_F:
		fprintf(fp, "F");
		break;
	case TSCALE_C:
		fprintf(fp, "C");
		break;
	case TSCALE_K:
		fprintf(fp, "K");
		break;
	default:
		fprintf(fp, "C");
		break;
	}
}


/**
   \brief parse a speed scale
*/
int conf_parse_speedscale(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		    void *result)
{
	if (strcasecmp(value, "MPH") == 0)
		*(int *)result = SPEED_MPH;
	else if (strcasecmp(value, "MS") == 0)
		*(int *)result = SPEED_MS;
	else if (strcasecmp(value, "KPH") == 0)
		*(int *)result = SPEED_KPH;
	else if (strcasecmp(value, "KNOTS") == 0)
		*(int *)result = SPEED_KNOTS;
	else {
		cfg_error(cfg, "invalid speed scale value for option '%s': %s",
		    cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}

/**
   \brief Used to print speed scale values
   \param opt option structure
   \param index number of option to print
   \param fp passed FILE
*/

void conf_print_speedscale(cfg_opt_t *opt, unsigned int index, FILE *fp)
{
	switch (cfg_opt_getnint(opt, index)) {
	case SPEED_MS:
		fprintf(fp, "ms");
		break;
	case SPEED_KPH:
		fprintf(fp, "kph");
		break;
	case SPEED_KNOTS:
		fprintf(fp, "knots");
		break;
	default:
		fprintf(fp, "mph");
		break;
	}
}

/**
   \brief parse a length scale
*/
int conf_parse_lscale(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		    void *result)
{
	if (strcasecmp(value, "IN") == 0)
		*(int *)result = LENGTH_IN;
	else if (strcasecmp(value, "INCHES") == 0)
		*(int *)result = LENGTH_IN;
	else if (strcasecmp(value, "MM") == 0)
		*(int *)result = LENGTH_MM;
	else if (strcasecmp(value, "MILLIMETER") == 0)
		*(int *)result = LENGTH_MM;
	else {
		cfg_error(cfg, "invalid len scale value for option '%s': %s",
		    cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}

/**
   \brief Used to print temp scale values
   \param opt option structure
   \param index number of option to print
   \param fp passed FILE
*/

void conf_print_lscale(cfg_opt_t *opt, unsigned int index, FILE *fp)
{
	switch (cfg_opt_getnint(opt, index)) {
	case LENGTH_IN:
		fprintf(fp, "in");
		break;
	case LENGTH_MM:
		fprintf(fp, "mm");
		break;
	default:
		fprintf(fp, "in");
		break;
	}
}

/**
   \brief parse a barometer scale
*/
int conf_parse_baroscale(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		    void *result)
{
	if (strcasecmp(value, "IN") == 0)
		*(int *)result = BAROSCALE_IN;
	else if (strcasecmp(value, "INCHES") == 0)
		*(int *)result = BAROSCALE_IN;
	else if (strcasecmp(value, "MM") == 0)
		*(int *)result = BAROSCALE_MM;
	else if (strcasecmp(value, "MB") == 0)
		*(int *)result = BAROSCALE_MB;
	else if (strcasecmp(value, "HPA") == 0)
		*(int *)result = BAROSCALE_MB;
	else {
		cfg_error(cfg, "invalid baro scale value for option '%s': %s",
		    cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}

/**
   \brief Used to print barometer scale values
   \param opt option structure
   \param index number of option to print
   \param fp passed FILE
*/

void conf_print_baroscale(cfg_opt_t *opt, unsigned int index, FILE *fp)
{
	switch (cfg_opt_getnint(opt, index)) {
	case BAROSCALE_IN:
		fprintf(fp, "in");
		break;
	case BAROSCALE_MM:
		fprintf(fp, "mm");
		break;
	case BAROSCALE_MB:
		fprintf(fp, "mb");
		break;
	default:
		fprintf(fp, "mb");
		break;
	}
}

/**
   \brief parse a light scale
*/
int conf_parse_lightscale(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		    void *result)
{
	if (strcasecmp(value, "WM2") == 0)
		*(int *)result = LIGHT_WM2;
	else if (strcasecmp(value, "W/M2") == 0)
		*(int *)result = LIGHT_WM2;
	else if (strcasecmp(value, "W/M^2") == 0)
		*(int *)result = LIGHT_WM2;
	else if (strcasecmp(value, "LUX") == 0)
		*(int *)result = LIGHT_LUX;
	else {
		cfg_error(cfg, "invalid light scale value for option '%s': %s",
		    cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}

/**
   \brief Used to print light scale values
   \param opt option structure
   \param index number of option to print
   \param fp passed FILE
*/

void conf_print_lightscale(cfg_opt_t *opt, unsigned int index, FILE *fp)
{
	switch (cfg_opt_getnint(opt, index)) {
	case LIGHT_WM2:
		fprintf(fp, "wm2");
		break;
	case LIGHT_LUX:
		fprintf(fp, "lux");
		break;
	default:
		fprintf(fp, "lux");
		break;
	}
}

/*****
      General routines
*****/


/**
	\brief Find the cfg entry for a device by it's UID
	\param cfg config base
	\param uid uid char *
	\return the section we found it in
*/

cfg_t *find_devconf_byuid(cfg_t *cfg, char *uid)
{
	int i;
	cfg_t *section;

	for (i=0; i < cfg_size(cfg, "device"); i++) {
		section = cfg_getnsec(cfg, "device", i);
		if (strcmp(uid, cfg_title(section)) == 0)
			return section;
	}
	return NULL;
}

/**
   \brief Allocate a new device by looking it up in the conf file
   \param cfg config structure to use
   \param uid UID to lookup
   \return allocated device, or NULL if not found
*/

device_t *new_dev_from_conf(cfg_t *cfg, char *uid)
{
	device_t *dev;
	cfg_t *devconf;
	double d;
	uint32_t u;
	int i;

	devconf = find_devconf_byuid(cfg, uid);
	if (devconf == NULL)
		return NULL;

	/* otherwise, start loading */
	dev = smalloc(device_t);
	dev->uid = strdup(uid);
	if (cfg_getstr(devconf, "loc") != NULL)
		dev->loc = strdup(cfg_getstr(devconf, "loc"));
	else
		dev->loc = strdup(uid);
	if (cfg_getstr(devconf, "name") != NULL)
		dev->name = strdup(cfg_getstr(devconf, "name"));
	if (cfg_getstr(devconf, "rrdname") != NULL)
		dev->rrdname = strdup(cfg_getstr(devconf, "rrdname"));
	if (cfg_getstr(devconf, "handler") != NULL)
		dev->handler = strdup(cfg_getstr(devconf, "handler"));
	if (cfg_size(devconf, "hargs") > 0) {
		dev->nrofhargs = cfg_size(devconf, "hargs");
		dev->hargs = safer_malloc(sizeof(char *) * dev->nrofhargs);
		for (i = 0; i < cfg_size(devconf, "hargs"); i++)
			dev->hargs[i] = strdup(cfg_getnstr(devconf, "hargs", i));
	}
	dev->proto = cfg_getint(devconf, "proto");
	dev->type = cfg_getint(devconf, "type");
	dev->subtype = cfg_getint(devconf, "subtype");
	switch (dev->subtype) {
	case SUBTYPE_TEMP:
		dev->scale = cfg_getint(devconf, "tscale");
		break;
	case SUBTYPE_SPEED:
		dev->scale = cfg_getint(devconf, "speedscale");
		break;
	case SUBTYPE_PRESSURE:
		dev->scale = cfg_getint(devconf, "baroscale");
		break;
	case SUBTYPE_RAINRATE:
		dev->scale = cfg_getint(devconf, "lengthscale");
		break;
	case SUBTYPE_LUX:
		dev->scale = cfg_getint(devconf, "lightscale");
		break;
	}
	if (cfg_getstr(devconf, "multimodel") != NULL &&
	    (dev->subtype == SUBTYPE_HUMID ||
	     dev->subtype == SUBTYPE_LUX ||
	     dev->subtype == SUBTYPE_PRESSURE ||
	     dev->subtype == SUBTYPE_COUNTER))
		dev->localdata = (void *)strdup(cfg_getstr(devconf, "multimodel"));
	if (datatype_dev(dev) == DATATYPE_UINT) {
		u = (uint32_t)lrint(cfg_getfloat(devconf, "lowat"));
		store_data_dev(dev, DATALOC_LOWAT, &u);
		u = (uint32_t)lrint(cfg_getfloat(devconf, "hiwat"));
		store_data_dev(dev, DATALOC_HIWAT, &u);
	} else {
		d = cfg_getfloat(devconf, "lowat");
		store_data_dev(dev, DATALOC_LOWAT, &d);
		d = cfg_getfloat(devconf, "hiwat");
		store_data_dev(dev, DATALOC_HIWAT, &d);
	}
	if (cfg_getint(devconf, "spamhandler") > 0)
		dev->flags |= DEVFLAG_SPAMHANDLER;

	return dev;
}

/**
   \brief generate a new config entry from a device
   \param cfg cfg_t base
   \param dev device
   \return new cfg_t section
*/

cfg_t *new_conf_from_dev(cfg_t *cfg, device_t *dev)
{
	cfg_opt_t *option;
	cfg_t *devconf;
	double d=0;
	uint32_t u=0;
	int i;

	devconf = find_devconf_byuid(cfg, dev->uid);
	if (devconf == NULL) {
		option = cfg_getopt(cfg, "device");
		cfg_setopt(cfg, option, dev->uid);
		devconf = find_devconf_byuid(cfg, dev->uid);
	}
	if (devconf == NULL)
		return NULL;
	if (dev->loc != NULL)
		cfg_setstr(devconf, "loc", dev->loc);
	if (dev->name != NULL)
		cfg_setstr(devconf, "name", dev->name);
	if (dev->rrdname != NULL)
		cfg_setstr(devconf, "rrdname", dev->rrdname);
	if (dev->handler != NULL)
		cfg_setstr(devconf, "handler", dev->handler);
	if (dev->nrofhargs > 0 && dev->hargs != NULL)
		for (i = 0; i < dev->nrofhargs; i++)
			cfg_setnstr(devconf, "hargs", dev->hargs[i], i);

	if (dev->subtype)
		cfg_setint(devconf, "subtype", dev->subtype);
	if (dev->subtype && dev->scale) {
		switch (dev->subtype) {
		case SUBTYPE_TEMP:
			cfg_setint(devconf, "tscale", dev->scale);
			break;
		case SUBTYPE_SPEED:
			cfg_setint(devconf, "speedscale", dev->scale);
			break;
		case SUBTYPE_PRESSURE:
			cfg_setint(devconf, "baroscale", dev->scale);
			break;
		case SUBTYPE_RAINRATE:
			cfg_setint(devconf, "lengthscale", dev->scale);
			break;
		case SUBTYPE_LUX:
			cfg_setint(devconf, "lightscale", dev->scale);
			break;
		}
	}
	if (dev->type)
		cfg_setint(devconf, "type", dev->type);
	if (dev->proto)
		cfg_setint(devconf, "proto", dev->proto);
	if (datatype_dev(dev) == DATATYPE_UINT) {
		get_data_dev(dev, DATALOC_LOWAT, &u);
		cfg_setfloat(devconf, "lowat", (double)u);
		get_data_dev(dev, DATALOC_HIWAT, &u);
		cfg_setfloat(devconf, "hiwat", (double)u);
	} else {
		get_data_dev(dev, DATALOC_LOWAT, &d);
		cfg_setfloat(devconf, "lowat", d);
		get_data_dev(dev, DATALOC_HIWAT, &d);
		cfg_setfloat(devconf, "hiwat", d);
	}

	return devconf;
}

/**
	\brief Dump the current config file out
	\param cfg the config pointer
	\param flags print flags CONF_DUMP_XXX
	\param filename filename to dump to
*/

cfg_t *dump_conf(cfg_t *cfg, int flags, const char *filename)
{
	FILE *fp;
	cfg_opt_t *a;
	cfg_t *section;
	int i;
	time_t t;

	LOG(LOG_NOTICE, "Rewriting configuration file");
	t = time(NULL);
	fp = fopen(filename, "w");
	fprintf(fp, "# Config file for %s\n", getprogname());
	fprintf(fp, "# Generated on %s", ctime(&t));
	fprintf(fp, "#\n\n");
	for (i=0; i < cfg_size(cfg, "device"); i++) {
		section = cfg_getnsec(cfg, "device", i);
		a = cfg_getopt(section, "subtype");
		cfg_opt_set_print_func(a, conf_print_subtype);
		a = cfg_getopt(section, "type");
		cfg_opt_set_print_func(a, conf_print_type);
		a = cfg_getopt(section, "proto");
		cfg_opt_set_print_func(a, conf_print_proto);
		a = cfg_getopt(section, "spamhandler");
		cfg_opt_set_print_func(a, conf_print_bool);
		a = cfg_getopt(section, "tscale");
		cfg_opt_set_print_func(a, conf_print_tscale);
		a = cfg_getopt(section, "baroscale");
		cfg_opt_set_print_func(a, conf_print_baroscale);
		a = cfg_getopt(section, "lengthscale");
		cfg_opt_set_print_func(a, conf_print_lscale);
		a = cfg_getopt(section, "speedscale");
		cfg_opt_set_print_func(a, conf_print_speedscale);
		a = cfg_getopt(section, "lightscale");
		cfg_opt_set_print_func(a, conf_print_lightscale);
		if (flags & CONF_DUMP_DEVONLY) {
			fprintf(fp, "device \"%s\" {\n", cfg_title(section));
			cfg_print_indent(section, fp, 2);
			fprintf(fp, "}\n");
		}
	}
	if (!(flags & CONF_DUMP_DEVONLY))
		cfg_print(cfg, fp);
	fclose(fp);
}

/**
	\brief Parse a config file
	\param filename filename to parse
	\return pointer to generated config structure
	\note expects "options" to be set with the base of the cfg_opt_t structure
*/

cfg_t *parse_conf(const char *filename)
{
	cfg_t *cfg = cfg_init(options, CFGF_NONE);

	cfg_set_validate_func(cfg, "device|rrdname", conf_validate_rrdname);

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
