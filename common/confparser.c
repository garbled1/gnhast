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
extern TAILQ_HEAD(, _device_t) alldevs;
extern TAILQ_HEAD(, _device_group_t) allgroups;
extern name_map_t devtype_map[];
extern name_map_t devproto_map[];
extern name_map_t devsubtype_map[];

static int conf_parse_proto(cfg_t *cfg, cfg_opt_t *opt, const char *value,
			      void *result);
static void print_group(device_group_t *devgrp, int devs, int indent);

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
	CFG_INT_CB("salinescale", 0, CFGF_NONE, conf_parse_salinescale),
	CFG_STR("multimodel", 0, CFGF_NODEFAULT),
	CFG_STR("handler", 0, CFGF_NODEFAULT),
	CFG_STR_LIST("hargs", 0, CFGF_NODEFAULT),
	CFG_STR_LIST("tags", 0, CFGF_NODEFAULT),
	CFG_INT_CB("spamhandler", 0, CFGF_NONE, conf_parse_spamhandler),
	CFG_FLOAT("hiwat", 0.0, CFGF_NONE),
	CFG_FLOAT("lowat", 0.0, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t device_group_opts[] = {
	CFG_STR("name", 0, CFGF_NODEFAULT),
	CFG_STR_LIST("devices", 0, CFGF_NODEFAULT),
	CFG_STR_LIST("devgroups", 0, CFGF_NODEFAULT),
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
   \param cfg the config base
   \param opt the option we are parsing
   \param the value of the option
   \param result result of option parsing will be stored here
   \return success
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
   \brief parse a spamhandler
   \param cfg the config base
   \param opt the option we are parsing
   \param the value of the option
   \param result result of option parsing will be stored here
   \return success
   \note For result:  0=no 1=yes 2=onchange
*/
int conf_parse_spamhandler(cfg_t *cfg, cfg_opt_t *opt, const char *value,
			   void *result)
{
	if (strcasecmp(value, "yes") == 0)
		*(int *)result = 1;
	else if (strcasecmp(value, "true") == 0)
		*(int *)result = 1;
	else if (strcasecmp(value, "1") == 0)
		*(int *)result = 1;
	else if (strcasecmp(value, "spam") == 0)
		*(int *)result = 1;
	else if (strcasecmp(value, "no") == 0)
		*(int *)result = 0;
	else if (strcasecmp(value, "false") == 0)
		*(int *)result = 0;
	else if (strcasecmp(value, "0") == 0)
		*(int *)result = 0;
	else if (strcasecmp(value, "2") == 0)
		*(int *)result = 2;
	else if (strcasecmp(value, "change") == 0)
		*(int *)result = 2;
	else if (strcasecmp(value, "onchange") == 0)
		*(int *)result = 2;
	else {
		cfg_error(cfg, "invalid spamhandler value for option '%s': %s",
		    cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}

/**
   \brief parse a protocol
   \param cfg the config base
   \param opt the option we are parsing
   \param the value of the option
   \param result result of option parsing will be stored here
   \return success
*/

static int conf_parse_proto(cfg_t *cfg, cfg_opt_t *opt, const char *value,
			      void *result)
{
	int i;

	for (i=0; i < NROF_PROTOS; i++) {
		if (strcmp(value, devproto_map[i].name) == 0) {
			*(int *)result = devproto_map[i].id;
			return 0;
		}
	}

	cfg_error(cfg, "invalid value for option '%s': %s",
		  cfg_opt_name(opt), value);
	return -1;
}

/**
   \brief Used to print protocols
   \param opt option structure
   \param index number of option to print
   \param fp passed FILE
*/

static void conf_print_proto(cfg_opt_t *opt, unsigned int index, FILE *fp)
{
	int i;

	i = cfg_opt_getnint(opt, index);
	if (i > 0 && i < NROF_PROTOS) {
		fprintf(fp, "%s", devproto_map[i].name);
		return;
	}
	fprintf(fp, "%s", devproto_map[PROTO_NONE].name);
}

/**
   \brief parse a type
   \param cfg the config base
   \param opt the option we are parsing
   \param the value of the option
   \param result result of option parsing will be stored here
   \return success
*/

int conf_parse_type(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		    void *result)
{
	int i;

	for (i=0; i < NROF_TYPES; i++) {
		if (strcmp(value, devtype_map[i].name) == 0) {
			*(int *)result = devtype_map[i].id;
			return 0;
		}
	}

	cfg_error(cfg, "invalid value for option '%s': %s",
		  cfg_opt_name(opt), value);
	return -1;
}

/**
   \brief Used to print bool values
   \param opt option structure
   \param index number of option to print
   \param fp passed FILE
*/

void conf_print_bool(cfg_opt_t *opt, unsigned int index, FILE *fp)
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
   \brief Used to print spamhandler values
   \param opt option structure
   \param index number of option to print
   \param fp passed FILE
*/

void conf_print_spamhandler(cfg_opt_t *opt, unsigned int index, FILE *fp)
{
	switch (cfg_opt_getnint(opt, index)) {
	case 0:
		fprintf(fp, "no");
		break;
	case 1:
		fprintf(fp, "yes");
		break;
	case 2:
		fprintf(fp, "onchange");
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
	int i;

	i = cfg_opt_getnint(opt, index);
	if (i > 0 && i < NROF_TYPES) {
		fprintf(fp, "%s", devtype_map[i].name);
		return;
	}
	fprintf(fp, "%s", devtype_map[DEVICE_NONE].name);
}

/**
   \brief parse a subtype by name
   \param cfg the config base
   \param opt the option we are parsing
   \param the value of the option
   \param result result of option parsing will be stored here
   \return success
*/

int conf_parse_subtype(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		       void *result)
{
	int i;

	for (i=0; i < NROF_SUBTYPES; i++) {
		if (strcmp(value, devsubtype_map[i].name) == 0) {
			*(int *)result = devsubtype_map[i].id;
			return 0;
		}
	}

	cfg_error(cfg, "invalid value for option '%s': %s",
		  cfg_opt_name(opt), value);
	return -1;
}

/**
	\brief Used to print subtypes
	\param opt option structure
	\param index number of option to print
	\param fp passed FILE
*/

void conf_print_subtype(cfg_opt_t *opt, unsigned int index, FILE *fp)
{
	int i;

	i = cfg_opt_getnint(opt, index);
	if (i > 0 && i < NROF_SUBTYPES) {
		fprintf(fp, "%s", devsubtype_map[i].name);
		return;
	}
	fprintf(fp, "%s", devsubtype_map[SUBTYPE_NONE].name);
}

/**
   \brief parse a temperature scale
   \param cfg the config base
   \param opt the option we are parsing
   \param the value of the option
   \param result result of option parsing will be stored here
   \return success
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
   \param cfg the config base
   \param opt the option we are parsing
   \param the value of the option
   \param result result of option parsing will be stored here
   \return success
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
   \param cfg the config base
   \param opt the option we are parsing
   \param the value of the option
   \param result result of option parsing will be stored here
   \return success
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
   \param cfg the config base
   \param opt the option we are parsing
   \param the value of the option
   \param result result of option parsing will be stored here
   \return success
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
	else if (strcasecmp(value, "CB") == 0)
		*(int *)result = BAROSCALE_CB;
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
	case BAROSCALE_CB:
		fprintf(fp, "cb");
		break;
	default:
		fprintf(fp, "mb");
		break;
	}
}

/**
   \brief parse a light scale
   \param cfg the config base
   \param opt the option we are parsing
   \param the value of the option
   \param result result of option parsing will be stored here
   \return success
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

/**
   \brief parse a salinity scale
   \param cfg the config base
   \param opt the option we are parsing
   \param the value of the option
   \param result result of option parsing will be stored here
   \return success
*/

int conf_parse_salinescale(cfg_t *cfg, cfg_opt_t *opt, const char *value,
			  void *result)
{
	if (strcasecmp(value, "PPT") == 0)
		*(int *)result = SALINITY_PPT;
	else if (strcasecmp(value, "SG") == 0)
		*(int *)result = SALINITY_SG;
	else if (strcasecmp(value, "SALINITY_COND") == 0)
		*(int *)result = SALINITY_COND;
	else if (strcasecmp(value, "MS") == 0)
		*(int *)result = SALINITY_COND;
	else {
		cfg_error(cfg,
			  "invalid salinity scale value for option '%s': %s",
			  cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}

/**
   \brief Used to print salinity scale values
   \param opt option structure
   \param index number of option to print
   \param fp passed FILE
*/

void conf_print_salinescale(cfg_opt_t *opt, unsigned int index, FILE *fp)
{
	switch (cfg_opt_getnint(opt, index)) {
	case SALINITY_PPT:
		fprintf(fp, "ppt");
		break;
	case SALINITY_SG:
		fprintf(fp, "sg");
		break;
	case SALINITY_COND:
		fprintf(fp, "ms");
		break;
	default:
		fprintf(fp, "ppt");
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
	\brief Find the cfg entry for a device group by it's UID
	\param cfg config base
	\param uid uid char *
	\return the section we found it in
*/

cfg_t *find_devgrpconf_byuid(cfg_t *cfg, char *uid)
{
	int i;
	cfg_t *section;

	for (i=0; i < cfg_size(cfg, "devgroup"); i++) {
		section = cfg_getnsec(cfg, "devgroup", i);
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
	int64_t ll;
	int i, tn;

	devconf = find_devconf_byuid(cfg, uid);
	if (devconf == NULL)
		return NULL;

	/* If the device exists, re-use it, otherwise, build a new one */
	dev = find_device_byuid(uid);
	if (dev == NULL) {
		dev = smalloc(device_t);
		dev->uid = strdup(uid);
		TAILQ_INIT(&dev->watchers);
	}

	/* now load the device with data from the conf file */
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
	if (cfg_size(devconf, "tags") > 0) {
		tn = dev->nroftags = cfg_size(devconf, "tags");
		if (dev->nroftags % 2 == 1)
			dev->nroftags += 1;
		dev->tags = safer_malloc(sizeof(char *) * tn);
		for (i = 0; i < cfg_size(devconf, "tags"); i++)
			dev->tags[i] = strdup(cfg_getnstr(devconf, "tags", i));
		if (tn % 2 == 1)
			dev->tags[i+1] = "";
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
	switch (datatype_dev(dev)) {
	case DATATYPE_UINT:
		u = (uint32_t)lrint(cfg_getfloat(devconf, "lowat"));
		store_data_dev(dev, DATALOC_LOWAT, &u);
		u = (uint32_t)lrint(cfg_getfloat(devconf, "hiwat"));
		store_data_dev(dev, DATALOC_HIWAT, &u);
		break;
	case DATATYPE_LL:
		ll = (int64_t)llrint(cfg_getfloat(devconf, "lowat"));
		store_data_dev(dev, DATALOC_LOWAT, &ll);
		ll = (int64_t)llrint(cfg_getfloat(devconf, "hiwat"));
		store_data_dev(dev, DATALOC_HIWAT, &ll);
		break;
	case DATATYPE_DOUBLE:
		d = cfg_getfloat(devconf, "lowat");
		store_data_dev(dev, DATALOC_LOWAT, &d);
		d = cfg_getfloat(devconf, "hiwat");
		store_data_dev(dev, DATALOC_HIWAT, &d);
		break;
	}
	switch (cfg_getint(devconf, "spamhandler")) {
	case 1:
		SET_FLAG(dev->flags, DEVFLAG_SPAMHANDLER);
		break;
	case 2:
		SET_FLAG(dev->flags, DEVFLAG_CHANGEHANDLER);
	}

	if (TAILQ_EMPTY(&dev->watchers))
		TAILQ_INIT(&dev->watchers);

	return dev;
}

/**
   \brief generate a new config entry from a device group
   \param cfg cfg_t base
   \param devgrp device group
   \return new cfg_t section
*/

cfg_t *new_conf_from_devgrp(cfg_t *cfg, device_group_t *devgrp)
{
	cfg_opt_t *option;
	cfg_t *devconf;
	int i;
	wrap_device_t *wdev;
	wrap_group_t *wgrp;

	devconf = find_devgrpconf_byuid(cfg, devgrp->uid);
	if (devconf == NULL) {
		option = cfg_getopt(cfg, "devgroup");
		cfg_setopt(cfg, option, devgrp->uid);
		devconf = find_devgrpconf_byuid(cfg, devgrp->uid);
	}
	if (devconf == NULL)
		return NULL;
	if (devgrp->name != NULL)
		cfg_setstr(devconf, "name", devgrp->name);

	i=0;
	TAILQ_FOREACH(wdev, &devgrp->members, next) {
		cfg_setnstr(devconf, "devices", wdev->dev->uid, i);
		i++;
	}

	i=0;
	TAILQ_FOREACH(wgrp, &devgrp->children, nextg) {
		cfg_setnstr(devconf, "devgroups", wgrp->group->uid, i);
		i++;
	}
	return devconf;
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
	int64_t ll=0;
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
	if (dev->nroftags > 0 && dev->tags != NULL)
		for (i = 0; i < dev->nroftags; i++)
			cfg_setnstr(devconf, "tags", dev->tags[i], i);
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
	switch (datatype_dev(dev)) {
	case DATATYPE_UINT:
		get_data_dev(dev, DATALOC_LOWAT, &u);
		cfg_setfloat(devconf, "lowat", (double)u);
		get_data_dev(dev, DATALOC_HIWAT, &u);
		cfg_setfloat(devconf, "hiwat", (double)u);
		break;
	case DATATYPE_LL:
		get_data_dev(dev, DATALOC_LOWAT, &ll);
		cfg_setfloat(devconf, "lowat", (double)ll);
		get_data_dev(dev, DATALOC_HIWAT, &ll);
		cfg_setfloat(devconf, "hiwat", (double)ll);
		break;
	case DATATYPE_DOUBLE:
		get_data_dev(dev, DATALOC_LOWAT, &d);
		cfg_setfloat(devconf, "lowat", d);
		get_data_dev(dev, DATALOC_HIWAT, &d);
		cfg_setfloat(devconf, "hiwat", d);
		break;
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
	device_t *dev;

	if (QUERY_FLAG(flags, CONF_DUMP_DEVONLY))
		LOG(LOG_NOTICE, "Rewriting device configuration file");
	else if (QUERY_FLAG(flags, CONF_DUMP_GROUPONLY))
		LOG(LOG_NOTICE, "Rewriting group configuration file");
	else
		LOG(LOG_NOTICE, "Rewriting configuration file");

	t = time(NULL);
	fp = fopen(filename, "w");
	if (fp == NULL) {
		LOG(LOG_ERROR, "Could not open %s for writing", filename);
		return;
	}
	/* Make sure all changes make it in */
	TAILQ_FOREACH(dev, &alldevs, next_all) {
		if (QUERY_FLAG(flags, CONF_DUMP_NOCOLLECTOR) &&
		    dev->subtype != SUBTYPE_COLLECTOR)
			(void)new_conf_from_dev(cfg, dev);
	}

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
		cfg_opt_set_print_func(a, conf_print_spamhandler);
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
		a = cfg_getopt(section, "salinescale");
		cfg_opt_set_print_func(a, conf_print_salinescale);
		if (QUERY_FLAG(flags, CONF_DUMP_DEVONLY) &&
		    (QUERY_FLAG(flags, CONF_DUMP_NOCOLLECTOR) &&
		     cfg_getint(section, "subtype") != SUBTYPE_COLLECTOR)) {
			fprintf(fp, "device \"%s\" {\n", cfg_title(section));
			cfg_print_indent(section, fp, 2);
			fprintf(fp, "}\n");
		}
	}
	for (i=0; i < cfg_size(cfg, "devgroup"); i++) {
		section = cfg_getnsec(cfg, "devgroup", i);
		if (QUERY_FLAG(flags, CONF_DUMP_GROUPONLY)) {
			fprintf(fp, "devgroup \"%s\" {\n", cfg_title(section));
			cfg_print_indent(section, fp, 2);
			fprintf(fp, "}\n");
		}
	}
	if (!QUERY_FLAG(flags, CONF_DUMP_DEVONLY) &&
	    !QUERY_FLAG(flags, CONF_DUMP_GROUPONLY))
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
	cfg_t *cfg;

	cfg = cfg_init(options, CFGF_NONE);

	cfg_set_validate_func(cfg, "device|rrdname", conf_validate_rrdname);

	switch(cfg_parse(cfg, filename)) {
	case CFG_FILE_ERROR:
		LOG(LOG_WARNING, "Config file %s could not be read: %s",
			filename, strerror(errno));
		return cfg;
	case CFG_SUCCESS:
		LOG(LOG_DEBUG, "Read config file %s", filename);
		break;
	case CFG_PARSE_ERROR:
		return NULL;
	}

	return cfg;
}

/**
   \brief Parse the devgroups
*/

void parse_devgroups(cfg_t *cfg)
{
	device_t *dev;
	device_group_t *grp, *addgrp;
	cfg_t *devgrp;
	int i, j, ngrps, ndevs;
	char *dname, *gname;

	/* pass 1, find the groups and add devices */
	for (i=0; i < cfg_size(cfg, "devgroup"); i++) {
		devgrp = cfg_getnsec(cfg, "devgroup", i);
		grp = new_devgroup((char *)cfg_title(devgrp));
		grp->name = strdup(cfg_getstr(devgrp, "name"));
		ndevs = cfg_size(devgrp, "devices");
		for (j=0; j < ndevs; j++) {
			dname = cfg_getnstr(devgrp, "devices", j);
			dev = find_device_byuid(dname);
			if (dev == NULL)
				LOG(LOG_ERROR, "Device %s listed in group %s "
				    "does not exist!", dname,
				    cfg_title(devgrp));
			else
				add_dev_group(dev, grp);
		}
		LOG(LOG_DEBUG, "Loaded group %s from config file", grp->uid);
	}

	/* pass 2, now that groups exist, chain the groups */
	for (i=0; i < cfg_size(cfg, "devgroup"); i++) {
		devgrp = cfg_getnsec(cfg, "devgroup", i);
		grp = find_devgroup_byuid((char *)cfg_title(devgrp));
		if (grp == NULL) {
			LOG(LOG_ERROR, "Group %s not found in pass 2",
			    cfg_title(devgrp));
			continue;
		}
		ngrps = cfg_size(devgrp, "devgroups");
		for (j=0; j < ngrps; j++) {
			gname = cfg_getnstr(devgrp, "devgroups", j);
			addgrp = find_devgroup_byuid(gname);
			if (addgrp == NULL) {
				LOG(LOG_ERROR, "Child devgrp %s not found "
				    "in pass 2", gname);
				continue;
			} else
				add_group_group(addgrp, grp);
		}
	}
}

/**
   \brief Debug print a group
   \param devgrp Group to print
   \param devs if 1, print devices
   \param indent indent level
*/

static void print_group(device_group_t *devgrp, int devs, int indent)
{
	wrap_device_t *wdev;
	device_group_t *cgrp;
	wrap_group_t *wgrp;

	LOG(LOG_DEBUG, "%*sGroup uid:%s name:%s",
	    indent, "", devgrp->uid, devgrp->name);
	if (devs) {
		TAILQ_FOREACH(wdev, &devgrp->members, next) {
			LOG(LOG_DEBUG, "%*s Member device uid:%s",
			    indent, "", wdev->dev->uid);
		}
	}
	TAILQ_FOREACH(wgrp, &devgrp->children, nextg)
		print_group(wgrp->group, devs, indent+1);
}

/**
   \brief Debug print the device group table
   \param devs if 1, print devices
*/

void print_group_table(int devs)
{
	device_group_t *devgrp;

	LOG(LOG_DEBUG, "Dumping group table");
	TAILQ_FOREACH(devgrp, &allgroups, next_all) {
		/* Look for head nodes */
		if (!devgrp->subgroup) {
			LOG(LOG_DEBUG, "Top group uid:%s name:%s",
			    devgrp->uid, devgrp->name);
			print_group(devgrp, devs, 1);
		}
	}
	LOG(LOG_DEBUG, "End of groups");
}
