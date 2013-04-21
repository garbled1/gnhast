/* $Id$ */

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

/** Need the argtable in scope, so we can generate proper commands
    for the server */
extern argtable_t argtable[];

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
	evbuffer_add_printf(send, "%s:%d %s:%d %s:%d\n", ARGNM(SC_DEVTYPE),
			    dev->type, ARGNM(SC_PROTO), dev->proto,
			    ARGNM(SC_SUBTYPE), dev->subtype);

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

	/* The command to update is "upd" */
	send = evbuffer_new();

	/* special handling for cacti updates */
	if (what & GNC_UPD_CACTI) {
		evbuffer_add_printf(send, "%s:", dev->rrdname);
		if (dev->type == DEVICE_DIMMER)
			evbuffer_add_printf(send, "%f\n", dev->data.level);
		switch (dev->subtype) {
		case SUBTYPE_TEMP:
			evbuffer_add_printf(send, "%f\n", dev->data.temp);
			break;
		case SUBTYPE_HUMID:
			evbuffer_add_printf(send, "%f\n", dev->data.humid);
			break;
		case SUBTYPE_LUX:
			evbuffer_add_printf(send, "%f\n", dev->data.lux);
			break;
		case SUBTYPE_COUNTER:
			evbuffer_add_printf(send, "%d\n", dev->data.count);
			break;
		case SUBTYPE_PRESSURE:
			evbuffer_add_printf(send, "%f\n", dev->data.pressure);
			break;
		case SUBTYPE_SPEED:
			evbuffer_add_printf(send, "%f\n", dev->data.speed);
			break;
		case SUBTYPE_DIR:
			evbuffer_add_printf(send, "%f\n", dev->data.dir);
			break;
		case SUBTYPE_SWITCH:
			evbuffer_add_printf(send, "%d\n", dev->data.state);
			break;
		}
		bufferevent_write_buffer(out, send);
		evbuffer_free(send);
		return;
	}
	evbuffer_add_printf(send, "upd ");

	/* fill in the details */
	evbuffer_add_printf(send, "%s:%s ", ARGNM(SC_UID), dev->uid);
	if (what & GNC_UPD_NAME)
		evbuffer_add_printf(send, "%s:\"%s\" ",  ARGNM(SC_NAME),
				    dev->name);
	if (what & GNC_UPD_RRDNAME)
		evbuffer_add_printf(send, "%s:%s ",  ARGNM(SC_RRDNAME),
				    dev->rrdname);

	if (dev->type == DEVICE_DIMMER)
		evbuffer_add_printf(send, "%s:%f\n", ARGNM(SC_DIMMER),
				    dev->data.level);
	switch (dev->subtype) {
	case SUBTYPE_TEMP:
		evbuffer_add_printf(send, "%s:%f\n", ARGNM(SC_TEMP),
				    dev->data.temp);
		break;
	case SUBTYPE_HUMID:
		evbuffer_add_printf(send, "%s:%f\n", ARGNM(SC_HUMID),
				    dev->data.humid);
		break;
	case SUBTYPE_LUX:
		evbuffer_add_printf(send, "%s:%f\n", ARGNM(SC_LUX),
				    dev->data.lux);
		break;
	case SUBTYPE_COUNTER:
		evbuffer_add_printf(send, "%s:%d\n", ARGNM(SC_COUNT),
				    dev->data.count);
		break;
	case SUBTYPE_PRESSURE:
		evbuffer_add_printf(send, "%s:%f\n", ARGNM(SC_PRESSURE),
				    dev->data.pressure);
		break;
	case SUBTYPE_SPEED:
		evbuffer_add_printf(send, "%s:%f\n", ARGNM(SC_SPEED),
				    dev->data.speed);
		break;
	case SUBTYPE_DIR:
		evbuffer_add_printf(send, "%s:%f\n", ARGNM(SC_DIR),
				    dev->data.dir);
		break;
	case SUBTYPE_SWITCH:
		evbuffer_add_printf(send, "%s:%d\n", ARGNM(SC_SWITCH),
				    dev->data.state);
		break;
	}
	bufferevent_write_buffer(out, send);
	evbuffer_free(send);
}
