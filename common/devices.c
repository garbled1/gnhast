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
   \file devices.c
   \brief Device handling
   \author Tim Rightnour
*/

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/queue.h>
#ifdef HAVE_SYS_RBTREE_H
 #include <sys/rbtree.h>
#else
 #include "../linux/rbtree.h"
#endif

#include "gnhast.h"
#include "common.h"
#include "confparser.h"

int notimerupdate = 0;
int nrofdevs;
static rb_tree_t devices;
static rb_tree_t devgroups;
TAILQ_HEAD(, _device_t) alldevs = TAILQ_HEAD_INITIALIZER(alldevs);
TAILQ_HEAD(, _device_group_t) allgroups = TAILQ_HEAD_INITIALIZER(allgroups);

static int compare_device_byuid(void *ctx, const void *a, const void *b);
static int compare_device_uidtokey(void *ctx, const void *a, const void *key);
static const rb_tree_ops_t device__alltree_ops = {
	.rbto_compare_nodes = compare_device_byuid,
	.rbto_compare_key = compare_device_uidtokey,
	.rbto_node_offset = offsetof(struct _device_t, rbn),
	.rbto_context = NULL
};

static int compare_devgroup_byuid(void *ctx, const void *a, const void *b);
static int compare_devgroup_uidtokey(void *ctx, const void *a,
				     const void *key);
static const rb_tree_ops_t devgroup__alltree_ops = {
	.rbto_compare_nodes = compare_devgroup_byuid,
	.rbto_compare_key = compare_devgroup_uidtokey,
	.rbto_node_offset = offsetof(struct _device_group_t, rbn),
	.rbto_context = NULL
};

/** The device type name map table */
name_map_t devtype_map[] = {
	{DEVICE_NONE, "NONE"},
	{DEVICE_SWITCH, "switch"},
	{DEVICE_DIMMER, "dimmer"},
	{DEVICE_SENSOR, "sensor"},
	{DEVICE_TIMER, "timer"},
	{DEVICE_BLIND, "blind"},
};

name_map_t devproto_map[] = {
	{PROTO_NONE, "NONE"},
	{PROTO_INSTEON_V1, "insteon-v1"},
	{PROTO_INSTEON_V2, "insteon-v2"},
	{PROTO_INSTEON_V2CS, "insteon-v2cs"},
	{PROTO_SENSOR_OWFS, "sensor-owfs"},
	{PROTO_SENSOR_BRULTECH_GEM, "brultech-gem"},
	{PROTO_SENSOR_BRULTECH_ECM1240, "brultech-ecm1240"},
	{PROTO_SENSOR_WMR918, "wmr918"},
	{PROTO_SENSOR_AD2USB, "ad2usb"},
	{PROTO_SENSOR_ICADDY, "icaddy"},
	{PROTO_SENSOR_VENSTAR, "venstar"},
	{PROTO_CONTROL_URSTII, "urstii"},
};

name_map_t devsubtype_map[] = {
	{SUBTYPE_NONE, "NONE"},
	{SUBTYPE_SWITCH, "switch"},
	{SUBTYPE_OUTLET, "outlet"},
	{SUBTYPE_TEMP, "temp"},
	{SUBTYPE_HUMID, "humid"},
	{SUBTYPE_COUNTER, "counter"},
	{SUBTYPE_PRESSURE, "pressure"},
	{SUBTYPE_SPEED, "windspeed"},
	{SUBTYPE_DIR, "winddir"},
	{SUBTYPE_MOISTURE, "moisture"},
	{SUBTYPE_WETNESS, "wetness"},
	{SUBTYPE_HUB, "hub"},
	{SUBTYPE_LUX, "lux"},
	{SUBTYPE_VOLTAGE, "voltage"},
	{SUBTYPE_WATTSEC, "wattsec"},
	{SUBTYPE_WATT, "watt"},
	{SUBTYPE_AMPS, "amps"},
	{SUBTYPE_RAINRATE, "rainrate"},
	{SUBTYPE_WEATHER, "weather"},
	{SUBTYPE_ALARMSTATUS, "alarmstatus"},
	{SUBTYPE_NUMBER, "number"},
	{SUBTYPE_PERCENTAGE, "percentage"},
	{SUBTYPE_FLOWRATE, "flowrate"},
	{SUBTYPE_DISTANCE, "distance"},
	{SUBTYPE_VOLUME, "volume"},
	{SUBTYPE_TIMER, "timer"},
	{SUBTYPE_THMODE, "thmode"},
	{SUBTYPE_THSTATE, "thstate"},
	{SUBTYPE_SMNUMBER, "smnumber"},
	{SUBTYPE_COLLECTOR, "collector"},
	{SUBTYPE_BLIND, "blind"},
	{SUBTYPE_BOOL, "bool"},
};


/** \brief Compare two devices by uid */

static int compare_device_byuid(void *ctx, const void *a, const void *b) 
{
	return strcmp(((device_t *)a)->uid, ((device_t *)b)->uid);
}

/** \brief Compare device uid to string */

static int compare_device_uidtokey(void *ctx, const void *a, const void *key)
{
	return strcmp(((device_t *)a)->uid, key);
}

/** \brief Compare two device groups by uid */

static int compare_devgroup_byuid(void *ctx, const void *a, const void *b) 
{
	return strcmp(((device_group_t *)a)->uid, ((device_group_t *)b)->uid);
}

/** \brief Compare device group uid to string */

static int compare_devgroup_uidtokey(void *ctx, const void *a, const void *key)
{
	return strcmp(((device_group_t *)a)->uid, key);
}

/**
   \brief Search the devicetable for a device matching UID
   \param uid uid to look for
   \return device_t * if found, NULL if not
*/

device_t *find_device_byuid(char *uid)
{
	return rb_tree_find_node(&devices, uid);
}

/**
   \brief Search the device group table for a devgroup matching UID
   \param uid uid to look for
   \return device_t * if found, NULL if not
*/

device_group_t *find_devgroup_byuid(char *uid)
{
	return rb_tree_find_node(&devgroups, uid);
}

/**
   \brief Add a wrapped device to this client
   \param dev device to add
   \param client client_t to add to
   \param rate rate of feed, if used
   \param scale (temp) scale, if used
*/
void add_wrapped_device(device_t *dev, client_t *client, int rate, int scale)
{
	wrap_device_t *wrap = smalloc(wrap_device_t);

	wrap->dev = dev;
	wrap->rate = rate;
	wrap->scale = scale;
	if (TAILQ_EMPTY(&client->wdevices))
		    TAILQ_INIT(&client->wdevices);
	if (!(wrap->onq & DEVONQ_CLIENT)) {
		TAILQ_INSERT_TAIL(&client->wdevices, wrap, next);
		wrap->onq |= DEVONQ_CLIENT;
	}
}

/**
	\brief Insert a device into the TAILQ
	\note keeps the table sorted
	\return pointer to new device
*/

void insert_device(device_t *dev)
{
	/* put it in the rb tree */
	rb_tree_insert_node(&devices, dev);

	/* incr nrofdevs */
	nrofdevs++;

	/* throw it in the all device TAILQ */
	if (!(dev->onq & DEVONQ_ALL)) {
		TAILQ_INSERT_TAIL(&alldevs, dev, next_all);
		dev->onq |= DEVONQ_ALL;
	}
	/* we have no current data for this device */
	dev->flags |= DEVFLAG_NODATA;
}

/**
   \brief Create a new device group
   \param uid uid of group
   \return pointer to new group
*/

device_group_t *new_devgroup(char *uid)
{
	device_group_t *devgrp;

	devgrp = smalloc(device_group_t);
	devgrp->uid = strdup(uid);
	rb_tree_insert_node(&devgroups, devgrp);
	TAILQ_INIT(&devgrp->children);
	TAILQ_INIT(&devgrp->members);
	/* throw it in the all device group TAILQ */
	if (!(devgrp->onq & GROUPONQ_ALL)) {
		TAILQ_INSERT_TAIL(&allgroups, devgrp, next_all);
		devgrp->onq |= GROUPONQ_ALL;
	}
}

/**
   \brief Add a device to a group
   \param dev device to add
   \param devgrp device group to add to
   \note Uses a wrap device
*/
void add_dev_group(device_t *dev, device_group_t *devgrp)
{
	wrap_device_t *wrap = smalloc(wrap_device_t);

	wrap->dev = dev;
	TAILQ_INSERT_TAIL(&devgrp->members, wrap, next);
	wrap->onq |= WRAPONQ_NEXT;
	wrap->group = devgrp;
}

/**
   \brief Remove a device from a group
   \param dev device to remove
   \param devgrp device group to remove from
   \note basically nukes the wrapdev
*/

void remove_dev_group(device_t *dev, device_group_t *devgrp)
{
	wrap_device_t *wrap;

	TAILQ_FOREACH(wrap, &devgrp->members, next) {
		if (wrap->dev == dev) {
			TAILQ_REMOVE(&devgrp->members, wrap, next);
			free(wrap);
			return;
		}
	}
}

/**
   \brief Add a group to a group
   \param group1 group to add
   \param group2 group to add to
*/
void add_group_group(device_group_t *group1, device_group_t *group2)
{
	wrap_group_t *wrapg = smalloc(wrap_group_t);

	wrapg->group = group1;

	TAILQ_INSERT_TAIL(&group2->children, wrapg, nextg);
	wrapg->onq |= GROUPONQ_NEXT;
	wrapg->parent = group2;
	group1->subgroup++;
}

/**
   \brief remove a group from a group
   \param group1 group to remove
   \param group2 group to remove from
*/
void remove_group_group(device_group_t *group1, device_group_t *group2)
{
	wrap_group_t *wrapg;

	TAILQ_FOREACH(wrapg, &group2->children, nextg) {
		if (wrapg->group == group1) {
			wrapg->group->subgroup--;
			TAILQ_REMOVE(&group2->children, wrapg, nextg);
			free(wrapg);
			return;
		}
	}
}

/**
   \brief Is a device a member of a given group?
   \param dev dev to lookup
   \param devgrp device group to look in
   \return bool
*/

int dev_in_group(device_t *dev, device_group_t *devgrp)
{
	wrap_device_t *tmp;

	TAILQ_FOREACH(tmp, &devgrp->members, next)
		if (tmp->dev == dev)
			return 1;
	return 0;
}

/**
   \brief Is a group a member of a given group?
   \param grp grp to lookup
   \param devgrp device group to look in
   \return bool
*/

int group_in_group(device_group_t *grp, device_group_t *devgrp)
{
	wrap_group_t *tmp;

	TAILQ_FOREACH(tmp, &devgrp->children, nextg)
		if (tmp->group == grp)
			return 1;
	return 0;
}

/**
   \brief Initialize the devicetable and the group table
   \param readconf if true, read the device conf file
*/

void init_devtable(cfg_t *cfg, int readconf)
{
	device_t *dev;
	cfg_t *devconf;
	int i;

	nrofdevs = 0;
	rb_tree_init(&devices, &device__alltree_ops);
	rb_tree_init(&devgroups, &devgroup__alltree_ops);

	if (readconf == 0)
		return;

	for (i=0; i < cfg_size(cfg, "device"); i++) {
		devconf = cfg_getnsec(cfg, "device", i);
		dev = new_dev_from_conf(cfg, (char *)cfg_title(devconf));
		insert_device(dev);
		LOG(LOG_DEBUG, "Loaded device %s from config file", dev->uid);
	}

}

/**
   \brief get data from a device
   \param dev what device
   \param where where to store, see DATALOC_*
   \param pointer to data to be stored
*/

void get_data_dev(device_t *dev, int where, void *data)
{
	data_t *store;

	switch (where) {
	case DATALOC_DATA:
		store = &dev->data;
		break;
	case DATALOC_LAST:
		store = &dev->last;
		break;
	case DATALOC_MIN:
		store = &dev->min;
		break;
	case DATALOC_MAX:
		store = &dev->max;
		break;
	case DATALOC_AVG:
		store = &dev->avg;
		break;
	case DATALOC_LOWAT:
		store = &dev->lowat;
		break;
	case DATALOC_HIWAT:
		store = &dev->hiwat;
		break;
	case DATALOC_CHANGE:
		store = &dev->change;
	}

	switch (dev->type) {
	case DEVICE_SWITCH:
		*((uint8_t *)data) = store->state;
		break;
	case DEVICE_DIMMER:
		*((double *)data) = store->level;
		break;
	case DEVICE_SENSOR:
	case DEVICE_TIMER:
	{
		switch (dev->subtype) {
		case SUBTYPE_TEMP:
			*((double *)data) = store->temp;
			break;
		case SUBTYPE_HUMID:
		case SUBTYPE_PERCENTAGE:
			*((double *)data) = store->humid;
			break;
		case SUBTYPE_TIMER:
		case SUBTYPE_COUNTER:
			*((uint32_t *)data) = store->count;
			break;
		case SUBTYPE_PRESSURE:
			*((double *)data) = store->pressure;
			break;
		case SUBTYPE_SPEED:
			*((double *)data) = store->speed;
			break;
		case SUBTYPE_DIR:
			*((double *)data) = store->dir;
			break;
		case SUBTYPE_MOISTURE:
			*((double *)data) = store->moisture;
			break;
		case SUBTYPE_WETNESS:
			*((double *)data) = store->wetness;
			break;
		case SUBTYPE_LUX:
			*((double *)data) = store->lux;
			break;
		case SUBTYPE_WATTSEC:
			*((int64_t *)data) = store->wattsec;
			break;
		case SUBTYPE_VOLTAGE:
			*((double *)data) = store->volts;
			break;
		case SUBTYPE_WATT:
			*((double *)data) = store->watts;
			break;
		case SUBTYPE_AMPS:
			*((double *)data) = store->amps;
			break;
		case SUBTYPE_RAINRATE:
			*((double *)data) = store->rainrate;
			break;
		case SUBTYPE_SWITCH:
		case SUBTYPE_WEATHER:
		case SUBTYPE_ALARMSTATUS:
		case SUBTYPE_THMODE:
		case SUBTYPE_THSTATE:
		case SUBTYPE_SMNUMBER:
		case SUBTYPE_COLLECTOR:
		case SUBTYPE_BLIND:
			*((uint8_t *)data) = store->state;
			break;
		case SUBTYPE_NUMBER:
			*((int64_t *)data) = store->number;
			break;
		case SUBTYPE_FLOWRATE:
			*((double *)data) = store->flow;
			break;
		case SUBTYPE_DISTANCE:
			*((double *)data) = store->distance;
			break;
		case SUBTYPE_VOLUME:
			*((double *)data) = store->volume;
			break;
		}
		break;
	}
	}
}

/**
   \brief Store data in a device in the proper location
   \param dev what device
   \param where where to store, see DATALOC_*
   \param pointer to data
*/

void store_data_dev(device_t *dev, int where, void *data)
{
	data_t *store;

	switch (where) {
	case DATALOC_DATA:
		store = &dev->data;
		/* special handling, copy current to prev */
		memcpy(&dev->last, &dev->data, sizeof(data_t));
		dev->flags &= ~DEVFLAG_NODATA;
		break;
	case DATALOC_LAST:
		store = &dev->last;
		break;
	case DATALOC_MIN:
		store = &dev->min;
		break;
	case DATALOC_MAX:
		store = &dev->max;
		break;
	case DATALOC_AVG:
		store = &dev->avg;
		break;
	case DATALOC_LOWAT:
		store = &dev->lowat;
		break;
	case DATALOC_HIWAT:
		store = &dev->hiwat;
		break;
	case DATALOC_CHANGE:
		store = &dev->change;
	}

	switch (dev->type) {
	case DEVICE_SWITCH:
		store->state = *((uint8_t *)data);
		break;
	case DEVICE_DIMMER:
		store->level = *((double *)data);
		break;
	case DEVICE_SENSOR:
	case DEVICE_TIMER:
	case DEVICE_BLIND:
	{
		switch (dev->subtype) {
		case SUBTYPE_TEMP:
			store->temp = *((double *)data);
			break;
		case SUBTYPE_HUMID:
		case SUBTYPE_PERCENTAGE:
			store->humid = *((double *)data);
			break;
		case SUBTYPE_TIMER:
		case SUBTYPE_COUNTER:
			store->count = *((uint32_t *)data);
			break;
		case SUBTYPE_PRESSURE:
			store->pressure = *((double *)data);
			break;
		case SUBTYPE_SPEED:
			store->speed = *((double *)data);
			break;
		case SUBTYPE_DIR:
			store->dir = *((double *)data);
			break;
		case SUBTYPE_MOISTURE:
			store->moisture = *((double *)data);
			break;
		case SUBTYPE_WETNESS:
			store->wetness = *((double *)data);
			break;
		case SUBTYPE_LUX:
			store->lux = *((double *)data);
			break;
		case SUBTYPE_VOLTAGE:
			store->volts = *((double *)data);
			break;
		case SUBTYPE_WATTSEC:
			store->wattsec = *((int64_t *)data);
			break;
		case SUBTYPE_WATT:
			store->watts = *((double *)data);
			break;
		case SUBTYPE_AMPS:
			store->amps = *((double *)data);
			break;
		case SUBTYPE_RAINRATE:
			store->rainrate = *((double *)data);
			break;
		case SUBTYPE_SWITCH:
		case SUBTYPE_WEATHER:
		case SUBTYPE_ALARMSTATUS:
		case SUBTYPE_THMODE:
		case SUBTYPE_THSTATE:
		case SUBTYPE_SMNUMBER:
		case SUBTYPE_COLLECTOR:
		case SUBTYPE_BLIND:
			store->state = *((uint8_t *)data);
			break;
		case SUBTYPE_NUMBER:
			store->number = *((int64_t *)data);
			break;
		case SUBTYPE_VOLUME:
			store->volume = *((double *)data);
			break;
		case SUBTYPE_DISTANCE:
			store->distance = *((double *)data);
			break;
		case SUBTYPE_FLOWRATE:
			store->flow = *((double *)data);
			break;
		}
		break;
	}
	}
}

/**
   \brief Print data to a buf that must be freed
   \param dev device to print
   \param where data location to print
   \return char pointer to string that must be freed
*/

char *print_data_dev(device_t *dev, int where)
{
	char buf[256];
	double d=0.0;
	uint32_t u=0;
	int64_t ll=0;

	if (dev == NULL)
		return NULL;

	switch (datatype_dev(dev)) {
	case DATATYPE_UINT:
		get_data_dev(dev, where, &u);
		sprintf(buf, "%d", u);
		break;
	case DATATYPE_LL:
		get_data_dev(dev, where, &ll);
		sprintf(buf, "%jd", ll);
		break;
	case DATATYPE_DOUBLE:
	default:
		get_data_dev(dev, where, &d);
		sprintf(buf, "%f", d);
		break;
	}
	return strdup(buf);
}

/**
   \brief parse an hargs string and fixup hargs
   \param dev device
   \param data string to parse
*/

void parse_hargs(device_t *dev, char *data)
{
	char *p, *hold, *fhold;
	int j;

	if (data == NULL)
		return;

	/* Maybe they want to nuke the hargs?  ok. */
	if (strlen(data) < 1) {
		/* free the old hargs */
		for (j = 0; j < dev->nrofhargs; j++)
			free(dev->hargs[j]);
		if (dev->hargs)
			free(dev->hargs);
		dev->nrofhargs = 0;
		return;
	}

	fhold = hold = strdup(data);

	/* free the old hargs */
	for (j = 0; j < dev->nrofhargs; j++)
		free(dev->hargs[j]);
	if (dev->hargs)
		free(dev->hargs);

	/* count the arguments */
	for ((p = strtok(hold, ",")), j=0; p;
	     (p = strtok(NULL, ",")), j++);
	dev->nrofhargs = j;
	dev->hargs = safer_malloc(sizeof(char *) *
				  dev->nrofhargs);
	free(fhold);
	fhold = hold = strdup(data);
	for ((p = strtok(hold, ",")), j=0;
	     p && j < dev->nrofhargs;
	     (p = strtok(NULL, ",")), j++) {
		dev->hargs[j] = strdup(p);
	}
	free(fhold);
}

/**
   \brief Return the DATATYPE_ of a device
   \param dev device
   \return DATATYPE_*
*/

int datatype_dev(device_t *dev)
{
	if (dev->type == DEVICE_DIMMER)
		return DATATYPE_DOUBLE;
	switch (dev->subtype) {
	case SUBTYPE_SWITCH:
	case SUBTYPE_TIMER:
	case SUBTYPE_COUNTER:
	case SUBTYPE_OUTLET:
	case SUBTYPE_WEATHER:
	case SUBTYPE_ALARMSTATUS:
	case SUBTYPE_THMODE:
	case SUBTYPE_THSTATE:
	case SUBTYPE_SMNUMBER:
	case SUBTYPE_COLLECTOR:
	case SUBTYPE_BLIND:
		return DATATYPE_UINT;
		break;
	case SUBTYPE_WATTSEC:
	case SUBTYPE_NUMBER:
		return DATATYPE_LL;
		break;
	}
	return DATATYPE_DOUBLE;
}

/**
   \brief Is a device beyond the watermark?
   \param dev device
   \return 0 no, 1 above hiwat, -1 below lowat, 2 no watermark
   Check if we are spamming the handler.  If not, check only if we CROSSED
   a watermark.
*/

int device_watermark(device_t *dev)
{
	double lwd, hwd, dd, pdd;
	uint32_t lwu, hwu, du, pdu;
	int64_t lwj, hwj, dj, pdj;
	int spam = (dev->flags & DEVFLAG_SPAMHANDLER);

	if (datatype_dev(dev) == DATATYPE_UINT) {
		get_data_dev(dev, DATALOC_LOWAT, &lwu);
		get_data_dev(dev, DATALOC_HIWAT, &hwu);
		get_data_dev(dev, DATALOC_DATA, &du);
		get_data_dev(dev, DATALOC_LAST, &pdu);
		if (lwu == 0 && hwu == 0)
			return 2;
		if (spam) {
			if (du < lwu)
				return -1;
			if (du > hwu)
				return 1;
		} else {
		/* if we aren't spamming, make sure we actually CROSSED
		   the watermark */
			if (du < lwu && pdu > lwu)
				return -1;
			if (du > hwu && pdu < hwu)
				return 1;
		}
	} else if (datatype_dev(dev) == DATATYPE_LL) {
		get_data_dev(dev, DATALOC_LOWAT, &lwj);
		get_data_dev(dev, DATALOC_HIWAT, &hwj);
		get_data_dev(dev, DATALOC_DATA, &dj);
		get_data_dev(dev, DATALOC_LAST, &pdj);
		if (lwj == 0 && hwj == 0)
			return 2;
		if (spam) {
			if (dj < lwj)
				return -1;
			if (dj > hwj)
				return 1;
		} else {
			if (dj < lwj && pdj > lwj)
				return -1;
			if (dj > hwj && pdj < hwj)
				return 1;
		}
	} else {
		get_data_dev(dev, DATALOC_LOWAT, &lwd);
		get_data_dev(dev, DATALOC_HIWAT, &hwd);
		get_data_dev(dev, DATALOC_DATA, &dd);
		get_data_dev(dev, DATALOC_LAST, &pdd);
		if (lwd == 0.0 && hwd == 0.0)
			return 2;
		if (spam) {
			if (dd < lwd)
				return -1;
			if (dd > hwd)
				return 1;
		} else {
			if (dd < lwd && pdd > lwd)
				return -1;
			if (dd > hwd && pdd < hwd)
				return 1;
		}
	}
	return 0;
}

/**
   \brief timer device update handler
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void cb_timerdev_update(int fd, short what, void *arg)
{
	uint32_t count;
	device_t *dev;

	if (notimerupdate == 1)
		return;

	TAILQ_FOREACH(dev, &alldevs, next_all) {
		if (dev->type == DEVICE_TIMER &&
		    dev->subtype == SUBTYPE_TIMER) {
			get_data_dev(dev, DATALOC_DATA, &count);
			if (count) {
				count -= 1;
				store_data_dev(dev, DATALOC_DATA, &count);
			}
		}
	}
}
