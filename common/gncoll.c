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
   \file gncoll.c
   \author Tim Rightnour
   \brief Generic collector routines
*/

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <time.h>
#include <event2/dns.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>

#include "common.h"
#include "gnhast.h"
#include "confuse.h"
#include "confparser.h"
#include "gncoll.h"

/* Need the argtable in scope, so we can generate proper commands
   for the server */
extern argtable_t argtable[];
extern char *dumpconf;

int collector_instance = 0;

/**
   \brief convert a temperature
   \param temp current temp value
   \param cur current scale
   \param new new scale
   \return scaled value
*/

double gn_scale_temp(double temp, int cur, int new)
{
	double d = temp;

	/* first, convert to Fahrenheit */
	switch (cur) {
	case TSCALE_K: d = KTOF(temp); break;
	case TSCALE_C: d = CTOF(temp); break;
	case TSCALE_R: d = RTOF(temp); break;
	}
	/* now, from F -> new scale */
	switch (new) {
	case TSCALE_K: return CTOK(FTOC(d)); break;
	case TSCALE_C: return FTOC(d); break;
	case TSCALE_R: return FTOR(d); break;
	}
	return d;
}

/**
   \brief convert a pressure reading
   \param press current pressure value
   \param cur current scale
   \param new new scale
   \return scaled value
*/

double gn_scale_pressure(double press, int cur, int new)
{
	double d = press;

	/* first, convert to millibars */
	switch (cur) {
	case BAROSCALE_IN: d = BARO_INTOMB(press); break;
	case BAROSCALE_MM: d = BARO_MMTOMB(press); break;
	}
	/* now from MB -> new scale */
	switch (new) {
	case BAROSCALE_IN: return BARO_MBTOIN(d); break;
	case BAROSCALE_MM: return BARO_MBTOMM(d); break;
	}
	return d;
}

/**
   \brief convert a speed reading
   \param speed current speed value
   \param cur current scale
   \param new new scale
   \return scaled value
*/

double gn_scale_speed(double speed, int cur, int new)
{
	double d = speed;

	/* first, convert to MPH */
	switch (cur) {
	case SPEED_KNOTS: d = KNOTSTOMPH(speed); break;
	case SPEED_MS: d = MSTOMPH(speed); break;
	case SPEED_KPH: d = KPHTOMPH(speed); break;
	}
	/* now, from MPH, to new speed */
	switch (new) {
	case SPEED_KNOTS: return MPHTOKNOTS(d); break;
	case SPEED_MS: return MPHTOMS(d); break;
	case SPEED_KPH: return MPHTOKPH(d); break;
	}
	return d;
}

/**
   \brief convert a length reading
   \param length current length value
   \param cur current scale
   \param new new scale
   \return scaled value
*/

double gn_scale_length(double length, int cur, int new)
{
	double d = length;

	/* first, convert to inches */
	switch (cur) {
	case LENGTH_MM: d = MMTOIN(length); break;
	}
	/* now, from inches, to new length */
	switch (new) {
	case LENGTH_MM: return INTOMM(d); break;
	}
	return d;
}

/**
   \brief convert a light reading
   \param light current light value
   \param cur current scale
   \param new new scale
   \return scaled value
*/

double gn_scale_light(double light, int cur, int new)
{
	double d = light;

	/* first, convert to LUX */
	switch (cur) {
	case LIGHT_WM2: d = LIGHT_WM2TOLUX(light); break;
	}
	/* now, from LUX -> new scale */
	switch (new) {
	case LIGHT_WM2: return LIGHT_LUXTOWM2(d); break;
	}
	return d;
}

/**
   \brief Maybe scale a device
   \param dev the device
   \param scale the new scale
   \param val the current value (avoids second call to get_data_dev)
   \return scaled value
   \note This only works on type double
*/

double gn_maybe_scale(device_t *dev, int scale, double val)
{
	double d;

	if (dev->scale == scale)
		return val;
		
	/* is this a device that is scalable? */
	switch (dev->subtype) {
	case SUBTYPE_TEMP:
		return gn_scale_temp(val, dev->scale, scale);
		break;
	case SUBTYPE_PRESSURE:
		return gn_scale_pressure(val, dev->scale, scale);
		break;
	case SUBTYPE_SPEED:
		return gn_scale_speed(val, dev->scale, scale);
		break;
	case SUBTYPE_RAINRATE:
		return gn_scale_length(val, dev->scale, scale);
		break;
	case SUBTYPE_LUX:
		return gn_scale_light(val, dev->scale, scale);
		break;
	default:
		return val;
		break;
	}
	return val;
}

/**
   \brief Modify a device's details
   \param dev The device to modify
   \param out the bufferevent we are scheduling on
*/
void gn_modify_device(device_t *dev, struct bufferevent *out)
{
	struct evbuffer *send;
	double d=0.0;
	uint32_t u=0;
	int64_t ll=0;
	int i;

	/* Verify sanity, is our device registerable? */
	if (dev->name == NULL || dev->uid == NULL) {
		LOG(LOG_ERROR, "Attempt to register unnamed device");
		return;
	}
	send = evbuffer_new();
	/* Command to modify is "mod", start with that */
	evbuffer_add_printf(send, "mod ");

	/* We use the ARGNM macro to generate the argument words so if they
	   change, we don't have to re-write all our code.
	*/
	evbuffer_add_printf(send, "%s:%s %s:\"%s\" ", ARGNM(SC_UID), dev->uid,
			    ARGNM(SC_NAME), dev->name);
	if (dev->rrdname)
		evbuffer_add_printf(send, "%s:%s ", ARGNM(SC_RRDNAME),
				    dev->rrdname);
	if (dev->scale)
		evbuffer_add_printf(send, "%s:%d ", ARGNM(SC_SCALE),
				    dev->scale);

	/* switch and do watermarks */
	switch (datatype_dev(dev)) {
	case DATATYPE_UINT:
		get_data_dev(dev, DATALOC_LOWAT, &u);
		evbuffer_add_printf(send, " %s:%d ", ARGNM(SC_LOWAT), u);
		get_data_dev(dev, DATALOC_HIWAT, &u);
		evbuffer_add_printf(send, "%s:%d ", ARGNM(SC_HIWAT), u);
		break;
	case DATATYPE_LL:
		get_data_dev(dev, DATALOC_LOWAT, &ll);
		evbuffer_add_printf(send, " %s:%jd ", ARGNM(SC_LOWAT), ll);
		get_data_dev(dev, DATALOC_HIWAT, &ll);
		evbuffer_add_printf(send, "%s:%jd", ARGNM(SC_HIWAT), ll);
		break;
	case DATATYPE_DOUBLE:
	default:
		get_data_dev(dev, DATALOC_LOWAT, &d);
		evbuffer_add_printf(send, " %s:%f ", ARGNM(SC_LOWAT), d);
		get_data_dev(dev, DATALOC_HIWAT, &d);
		evbuffer_add_printf(send, "%s:%f", ARGNM(SC_HIWAT), d);
		break;
	}

	if (dev->handler)
		evbuffer_add_printf(send, " %s:%s", ARGNM(SC_HANDLER),
				    dev->handler);

	if (dev->hargs && dev->nrofhargs > 0) {
		evbuffer_add_printf(send, " %s:", ARGNM(SC_HARGS));
		for (i = 0; i < dev->nrofhargs && dev->hargs[i] != NULL; i++) {
			if (i != 0)
				evbuffer_add_printf(send, ",");
			evbuffer_add_printf(send, "%s", dev->hargs[i]);
		}
	}
	evbuffer_add_printf(send, "\n");

	/* schedule the bufferevent write */
	
	bufferevent_write_buffer(out, send);
	bufferevent_enable(out, EV_READ|EV_WRITE);
	evbuffer_free(send);
}


/**
   \brief Register a device with the server
   \param dev The device to inform server about
   \param out the bufferevent we are scheduling on
*/

void gn_register_device(device_t *dev, struct bufferevent *out)
{
	struct evbuffer *send;

	/* Verify sanity, is our device registerable? */
	if (dev->name == NULL || dev->uid == NULL) {
		LOG(LOG_ERROR, "Attempt to register unnamed device");
		return;
	}
	if (dev->type == 0 || dev->subtype == 0 || dev->proto == 0) {
		LOG(LOG_ERROR, "Attempt to register device uid=%s without "
		    "type/subtype/proto", dev->uid);
		return;
	}

	send = evbuffer_new();
	/* Command to register is "reg", start with that */
	evbuffer_add_printf(send, "reg ");

	/* We use the ARGNM macro to generate the argument words so if they
	   change, we don't have to re-write all our code.
	*/
	evbuffer_add_printf(send, "%s:%s %s:\"%s\" ", ARGNM(SC_UID), dev->uid,
			    ARGNM(SC_NAME), dev->name);
	if (dev->rrdname)
		evbuffer_add_printf(send, "%s:%s ", ARGNM(SC_RRDNAME),
				    dev->rrdname);
	if (dev->scale)
		evbuffer_add_printf(send, "%s:%d ", ARGNM(SC_SCALE),
				    dev->scale);
	evbuffer_add_printf(send, "%s:%d %s:%d %s:%d\n", ARGNM(SC_DEVTYPE),
			    dev->type, ARGNM(SC_PROTO), dev->proto,
			    ARGNM(SC_SUBTYPE), dev->subtype);

	/* schedule the bufferevent write */
	
	bufferevent_write_buffer(out, send);
	bufferevent_enable(out, EV_READ|EV_WRITE);
	evbuffer_free(send);
}

/**
   \brief Register a device group with the server, only send name
   \param devgrp The device group to inform server about
   \param out the bufferevent we are scheduling on
*/

void gn_register_devgroup_nameonly(device_group_t *devgrp,
				   struct bufferevent *out)
{
	struct evbuffer *send;

	/* Verify sanity, is our device registerable? */
	if (devgrp->name == NULL || devgrp->uid == NULL) {
		LOG(LOG_ERROR, "Attempt to register unnamed device group");
		return;
	}

	send = evbuffer_new();
	/* Command to register groups is "regg", start with that */
	evbuffer_add_printf(send, "regg ");

	/* We use the ARGNM macro to generate the argument words so if they
	   change, we don't have to re-write all our code.
	*/
	evbuffer_add_printf(send, "%s:%s %s:\"%s\" ", ARGNM(SC_UID),
			    devgrp->uid, ARGNM(SC_NAME), devgrp->name);

	evbuffer_add_printf(send, "\n");

	/* schedule the bufferevent write */
	
	bufferevent_write_buffer(out, send);
	bufferevent_enable(out, EV_READ|EV_WRITE);
	evbuffer_free(send);
}

/**
   \brief Register a device group with the server
   \param devgrp The device group to inform server about
   \param out the bufferevent we are scheduling on
*/

void gn_register_devgroup(device_group_t *devgrp, struct bufferevent *out)
{
	struct evbuffer *send;
	wrap_device_t *wdev;
	wrap_group_t *wgrp;
	int first;

	/* Verify sanity, is our device registerable? */
	if (devgrp->name == NULL || devgrp->uid == NULL) {
		LOG(LOG_ERROR, "Attempt to register unnamed device group");
		return;
	}

	send = evbuffer_new();
	/* Command to register groups is "regg", start with that */
	evbuffer_add_printf(send, "regg ");

	/* We use the ARGNM macro to generate the argument words so if they
	   change, we don't have to re-write all our code.
	*/
	evbuffer_add_printf(send, "%s:%s %s:\"%s\" ", ARGNM(SC_UID),
			    devgrp->uid, ARGNM(SC_NAME), devgrp->name);

	first = 1;
	TAILQ_FOREACH(wgrp, &devgrp->children, nextg) {
		if (first) {
			evbuffer_add_printf(send, "%s:", ARGNM(SC_GROUPLIST));
			evbuffer_add_printf(send, "%s", wgrp->group->uid);
			first = 0;
		} else {
			evbuffer_add_printf(send, ",%s", wgrp->group->uid);
		}
	}
	evbuffer_add_printf(send, " ");

	first = 1;
	TAILQ_FOREACH(wdev, &devgrp->members, next) {
		if (first) {
			evbuffer_add_printf(send, "%s:", ARGNM(SC_DEVLIST));
			evbuffer_add_printf(send, "%s", wdev->dev->uid);
			first = 0;
		} else {
			evbuffer_add_printf(send, ",%s", wdev->dev->uid);
		}
	}
	evbuffer_add_printf(send, "\n");

	/* schedule the bufferevent write */
	
	bufferevent_write_buffer(out, send);
	bufferevent_enable(out, EV_READ|EV_WRITE);
	evbuffer_free(send);
}

/**
   \brief Tell the server a device's current value
   \param dev The device to inform server about
   \param out the bufferevent we are scheduling on
*/
	
void gn_update_device(device_t *dev, int what, struct bufferevent *out)
{
	struct evbuffer *send;
	double d=0.0;
	uint32_t u=0;
	int64_t ll=0;
	int scale, i;

	/* Verify device sanity first */
	if (dev->name == NULL || dev->uid == NULL) {
		LOG(LOG_ERROR, "Attempt to update unnamed device");
		return; 
	}
	if (dev->type == 0 || dev->subtype == 0 || dev->proto == 0) {
		LOG(LOG_ERROR, "Attempt to update device uid=%s without "
		    "type/subtype/proto", dev->uid);
		return;
	}

	scale = GNC_GET_SCALE(what);
	if (what & GNC_NOSCALE)
		scale = dev->scale; /* short circuit gn_maybe scale */
	send = evbuffer_new();

	/* special handling for cacti updates */
	if (what & GNC_UPD_CACTI) {
		evbuffer_add_printf(send, "%s:", dev->rrdname);
		if (datatype_dev(dev) == DATATYPE_UINT) {
			get_data_dev(dev, DATALOC_DATA, &u);
			evbuffer_add_printf(send, "%d\n", u);
		} else if (datatype_dev(dev) == DATATYPE_LL) {
			get_data_dev(dev, DATALOC_DATA, &ll);
			evbuffer_add_printf(send, "%jd\n", ll);
		} else {
			get_data_dev(dev, DATALOC_DATA, &d);
			evbuffer_add_printf(send, "%f\n",
					    gn_maybe_scale(dev, scale, d));
		}
		bufferevent_write_buffer(out, send);
		evbuffer_free(send);
		return;
	}
	/* The command to update is "upd" */
	evbuffer_add_printf(send, "upd ");

	/* fill in the details */
	evbuffer_add_printf(send, "%s:%s ", ARGNM(SC_UID), dev->uid);
	if (((what & GNC_UPD_NAME) || (what & GNC_UPD_FULL))
	    && dev->name != NULL)
		evbuffer_add_printf(send, "%s:\"%s\" ",  ARGNM(SC_NAME),
				    dev->name);
	if (((what & GNC_UPD_RRDNAME) || (what & GNC_UPD_FULL))
	    && dev->rrdname != NULL)
		evbuffer_add_printf(send, "%s:%s ",  ARGNM(SC_RRDNAME),
				    dev->rrdname);
	if (((what & GNC_UPD_HANDLER) || (what & GNC_UPD_FULL))
	    && dev->handler != NULL)
		evbuffer_add_printf(send, "%s:%s ",  ARGNM(SC_HANDLER),
				    dev->handler);
	if (((what & GNC_UPD_HARGS)  || (what & GNC_UPD_FULL))
	    && dev->nrofhargs > 0 && dev->hargs != NULL) {
		evbuffer_add_printf(send, "%s:",  ARGNM(SC_HARGS));
		evbuffer_add_printf(send, "\"%s\"", dev->hargs[0]);
		for (i=1; i < dev->nrofhargs; i++)
			evbuffer_add_printf(send, ",\"%s\"", dev->hargs[i]);
		evbuffer_add_printf(send, " ");
	}
	/* do everything else */
	if (what & GNC_UPD_FULL) {
		evbuffer_add_printf(send, "%s:%d %s:%d %s:%d ",
				    ARGNM(SC_DEVTYPE), dev->type,
				    ARGNM(SC_PROTO), dev->proto,
				    ARGNM(SC_SUBTYPE), dev->subtype);
		if (dev->scale)
			evbuffer_add_printf(send, "%s:%d ", ARGNM(SC_SCALE),
					    dev->scale);
	}

	/* switch and do watermarks and values */
	switch (datatype_dev(dev)) {
	case DATATYPE_UINT:
		get_data_dev(dev, DATALOC_DATA, &u);
		evbuffer_add_printf(send, "%s:%d", ARGDEV(dev), u);
		if ((what & GNC_UPD_WATER) || (what & GNC_UPD_FULL)) {
			get_data_dev(dev, DATALOC_LOWAT, &u);
			evbuffer_add_printf(send, " %s:%d ",
					    ARGNM(SC_LOWAT), u);
			get_data_dev(dev, DATALOC_HIWAT, &u);
			evbuffer_add_printf(send, "%s:%d ",
					    ARGNM(SC_HIWAT), u);
		}
		break;
	case DATATYPE_LL:
		get_data_dev(dev, DATALOC_DATA, &ll);
		evbuffer_add_printf(send, "%s:%jd", ARGDEV(dev), ll);
		if ((what & GNC_UPD_WATER) || (what & GNC_UPD_FULL)) {
			get_data_dev(dev, DATALOC_LOWAT, &ll);
			evbuffer_add_printf(send, " %s:%jd ",
					    ARGNM(SC_LOWAT), ll);
			get_data_dev(dev, DATALOC_HIWAT, &ll);
			evbuffer_add_printf(send, "%s:%jd",
					    ARGNM(SC_HIWAT), ll);
		}
		break;
	case DATATYPE_DOUBLE:
	default:
		get_data_dev(dev, DATALOC_DATA, &d);
		evbuffer_add_printf(send, "%s:%f", ARGDEV(dev),
				    gn_maybe_scale(dev, scale, d));
		if ((what & GNC_UPD_WATER) || (what & GNC_UPD_FULL)) {
			get_data_dev(dev, DATALOC_LOWAT, &d);
			evbuffer_add_printf(send, " %s:%f ",
					    ARGNM(SC_LOWAT), d);
			get_data_dev(dev, DATALOC_HIWAT, &d);
			evbuffer_add_printf(send, "%s:%f",
					    ARGNM(SC_HIWAT), d);
		}
		break;
	}
	evbuffer_add_printf(send, "\n");
	bufferevent_write_buffer(out, send);
	evbuffer_free(send);
}

/**
   \brief Request a clean disconnect from the server
   \param bev bufferevent connected to a gnhast server
*/

void gn_disconnect(struct bufferevent *bev)
{
	struct evbuffer *send;

	LOG(LOG_NOTICE, "Requesting disconnect from gnhastd");
	send = evbuffer_new();
	evbuffer_add_printf(send, "disconnect\n");
	bufferevent_write_buffer(bev, send);
	evbuffer_free(send);
}

/**
   \brief Register client name
   \param bev bufferevent connected to a gnhast server
   \param name client name
*/

void gn_client_name(struct bufferevent *bev, char *name)
{
	struct evbuffer *send;

	if (name == NULL)
		return;
	LOG(LOG_NOTICE, "Registering client name %s-%0.3d with gnhastd",
	    name, collector_instance);
	send = evbuffer_new();
	evbuffer_add_printf(send, "client client:%s-%0.3d\n", name,
		collector_instance);
	bufferevent_write_buffer(bev, send);
	evbuffer_free(send);
}

/**
   \brief Send a ping
   \param bev bufferevent connected to a gnhastd server
   \note I can think of no sane reason to ever do this from a collector
*/

void gn_ping(struct bufferevent *bev)
{
	struct evbuffer *send;

	send = evbuffer_new();
	evbuffer_add_printf(send, "ping\n");
	bufferevent_write_buffer(bev, send);
	evbuffer_free(send);
}

/**
   \brief Send an imalive
   \param bev bufferevent connected to a gnhastd server
*/

void gn_imalive(struct bufferevent *bev)
{
	struct evbuffer *send;

	send = evbuffer_new();
	evbuffer_add_printf(send, "imalive\n");
	bufferevent_write_buffer(bev, send);
	evbuffer_free(send);
}

/* note, we have no client send die command.  There is no feasible scenario
   where gnhastd is alive enough to process a death request, but broken enough
   to need one.  Nor do we actually want gnhastd being bonked by collectors.
*/

/**
   \brief Generic routine to build a simple device
   \param cfg base config structure
   \param uid UID of device
   \param name name of device
   \param rrdname rrdname of device
   \param proto protocol
   \param type device type
   \param subtype device subtype
   \param loc device locator string
   \param tscale tempscale, if used
   Checks for device in config, loads if found, otherwise creates.
   Fills in loc if missing.
*/

void generic_build_device(cfg_t *cfg, char *uid, char *name, char *rrdname,
			  int proto, int type, int subtype, char *loc,
			  int tscale, struct bufferevent *bev)
{
	device_t *dev;

	dev = new_dev_from_conf(cfg, uid);
	if (dev == NULL) {
		dev = smalloc(device_t);
		dev->uid = strdup(uid);
		if (dumpconf != NULL) {
			dev->name = strdup(name);
			dev->rrdname = strdup(rrdname);
		}
		dev->proto = proto;
		dev->type = type;
		dev->subtype = subtype;
		if (subtype == SUBTYPE_TEMP)
			dev->scale = tscale;
		if (dev->loc == NULL) {
			dev->loc = strdup(loc);
		}
		(void) new_conf_from_dev(cfg, dev);
	} else {
		dev->loc = strdup(loc);
	}
	insert_device(dev);
	if (dumpconf == NULL && dev->name != NULL)
		gn_register_device(dev, bev);
}
