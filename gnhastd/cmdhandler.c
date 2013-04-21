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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/queue.h>

#include "gnhast.h"
#include "common.h"
#include "commands.h"
#include "cmds.h"
#include "gncoll.h"

extern TAILQ_HEAD(, _device_t) alldevs;

/**
	\file cmdhandler.c
	\brief Common command handlers
	\author Tim Rightnour
*/

extern struct event_base *base;

/** The command table */
commands_t commands[] = {
	{"reg", cmd_register, 0},
	{"upd", cmd_update, 0},
	{"feed", cmd_feed, 0},
	{"ldevs", cmd_list_devices, 0},
	{"ask", cmd_ask_device, 0},
	{"cactiask", cmd_cactiask_device, 0},
};

/** The size of the command table */
const size_t commands_size = sizeof(commands) / sizeof(commands_t);


/**
	\brief Initialize the commands table
*/

void init_commands(void)
{
	qsort((char *)commands, commands_size, sizeof(commands_t),
	    compare_command);
}


/**
	\brief Handle a command from the network
	\param command command to execute
	\param args arguments to command
*/

int parsed_command(char *command, pargs_t *args, void *arg)
{
	commands_t *asp, dummy;
	char *cp;
	int ret;

	for (cp=command; *cp; cp++)
		*cp = tolower(*cp);

	dummy.name = command;
	asp = (commands_t *)bsearch((void *)&dummy, (void *)commands,
	    commands_size, sizeof(commands_t), compare_command);

	if (asp) {
		ret = asp->func(args, arg);
		return(ret);
	} else {
		return(-1); /* command not found */
	}
}

/* ================================ */
/* Handlers */

/**
	\brief Handle a update device command
	\param args The list of arguments
	\param arg void pointer to client_t of provider
*/

int cmd_update(pargs_t *args, void *arg)
{
	int i;
	device_t *dev;
	char *uid=NULL;
	client_t *client = (client_t *)arg;

	/* loop through the args and find the UID */
	for (i=0; args[i].cword != -1; i++) {
		switch (args[i].cword) {
		case SC_UID:
			uid = args[i].arg.c;
			break;
		}
	}
	if (!uid) {
		LOG(LOG_ERROR, "update without UID");
		return(-1);
	}
	dev = find_device_byuid(uid);
	if (!dev) {
		LOG(LOG_ERROR, "UID:%s doesn't exist", uid);
		return(-1);
	}

	/* Ok, we got one, now lets update it's data */

	for (i=0; args[i].cword != -1; i++) {
		switch (args[i].cword) {
		case SC_SWITCH:
			if (dev->type != DEVICE_SWITCH)
				LOG(LOG_ERROR, "Updating switch value on "
				    "non-switch device %s/%s", dev->uid,
				    dev->name);
			else
				dev->data.state = args[i].arg.i;
			break;
		case SC_TEMP:
			if (dev->type != DEVICE_SENSOR ||
			    dev->subtype != SUBTYPE_TEMP)
				LOG(LOG_ERROR, "Updating temp value on "
				    "non-temp device %s/%s", dev->uid,
				    dev->name);
			else
				dev->data.temp = args[i].arg.d;
			break;
		case SC_HUMID:
			if (dev->type != DEVICE_SENSOR ||
			    dev->subtype != SUBTYPE_HUMID)
				LOG(LOG_ERROR, "Updating humid value on "
				    "non-humid device %s/%s", dev->uid,
				    dev->name);
			else
				dev->data.humid = args[i].arg.d;
			break;
		case SC_LUX:
			if (dev->type != DEVICE_SENSOR ||
			    dev->subtype != SUBTYPE_LUX)
				LOG(LOG_ERROR, "Updating lux value on non-lux "
				    "device %s/%s", dev->uid, dev->name);
			else
				dev->data.lux = args[i].arg.d;
			break;
		case SC_DIMMER:
			if (dev->type != DEVICE_DIMMER)
				LOG(LOG_ERROR, "Updating dimmer level value "
				    "on non-dimmer device %s/%s",
				    dev->uid, dev->name);
			else
				dev->data.level = args[i].arg.d;
			break;
		case SC_PRESSURE:
			if (dev->type != DEVICE_SENSOR ||
			    dev->subtype != SUBTYPE_PRESSURE)
				LOG(LOG_ERROR, "Updating pressure value on "
				    "non-pressure device %s/%s", dev->uid,
				    dev->name);
			else
				dev->data.pressure = args[i].arg.d;
			break;
		case SC_SPEED:
			if (dev->type != DEVICE_SENSOR ||
			    dev->subtype != SUBTYPE_SPEED)
				LOG(LOG_ERROR, "Updating speed value on "
				    "non-speed device %s/%s", dev->uid,
				    dev->name);
			else
				dev->data.speed = args[i].arg.d;
			break;
		case SC_DIR:
			if (dev->type != DEVICE_SENSOR ||
			    dev->subtype != SUBTYPE_DIR)
				LOG(LOG_ERROR, "Updating direction value on "
				    "non-direction device %s/%s", dev->uid,
				    dev->name);
			else
				dev->data.dir = args[i].arg.d;
			break;
		case SC_COUNT:
			if (dev->subtype != SUBTYPE_COUNTER)
				LOG(LOG_ERROR, "Updating count value on "
				    "non-count device %s/%s", dev->uid,
				    dev->name);
			else
				dev->data.count = args[i].arg.u;
			break;
		}
	}
	(void)time(&dev->last_upd);
	return(0);
}


/**
	\brief Handle a register device command
	\param args The list of arguments
	\param arg void pointer to client_t of provider
*/

int cmd_register(pargs_t *args, void *arg)
{
	int i, new=0;
	uint8_t devtype=0, proto=0, subtype=0;
	char *uid=NULL, *name=NULL, *rrdname=NULL;
	device_t *dev, *tdev;
	client_t *client = (client_t *)arg;

	for (i=0; args[i].cword != -1; i++) {
		switch (args[i].cword) {
		case SC_UID:
			uid = strdup(args[i].arg.c);
			break;
		case SC_NAME:
			name = strdup(args[i].arg.c);
			break;
		case SC_RRDNAME:
			rrdname = strndup(args[i].arg.c, 19);
			break;
		case SC_DEVTYPE:
			devtype = (uint8_t)args[i].arg.i;
			break;
		case SC_PROTO:
			proto = (uint8_t)args[i].arg.i;
			break;
		case SC_SUBTYPE:
			subtype = (uint8_t)args[i].arg.i;
			break;
		}
	}

	if (uid == NULL) {
		LOG(LOG_ERROR, "Got register command without UID");
		return(-1); /* MUST have UID */
	}

	LOG(LOG_DEBUG, "Register device: uid=%s name=%s rrd=%s type=%d proto=%d subtype=%d",
	    uid, (name) ? name : "NULL", (rrdname) ? rrdname : "NULL",
	    devtype, proto, subtype);

	dev = find_device_byuid(uid);
	if (dev == NULL) {
		LOG(LOG_DEBUG, "Creating new device for uid %s", uid);
		if (subtype == 0 || devtype == 0 || proto == 0 || name == NULL) {
			LOG(LOG_ERROR, "Attempt to register new device without full specifications");
			return(-1);
		}
		if (rrdname == NULL)
			rrdname = strndup(name, 19);
		dev = smalloc(device_t);
		dev->uid = uid;
		new = 1;
	} else
		LOG(LOG_DEBUG, "Updating existing device uid:%s", uid);
	dev->name = name;
	dev->rrdname = rrdname;
	dev->type = devtype;
	dev->proto = proto;
	dev->subtype = subtype;
	(void)time(&dev->last_upd);
	if (dev->collector != NULL && dev->collector != client)
		LOG(LOG_ERROR, "Device uid:%s has a collector, but a new one registered it!", dev->uid);
	dev->collector = client;

	/* now, update the client_t */
	if (TAILQ_EMPTY(&client->devices)) {
		TAILQ_INIT(&client->devices);
		client->provider = 1; /* this client is a provider */
	}
	TAILQ_INSERT_TAIL(&client->devices, dev, next_client);
	dev->collector = client;
	
	if (new)
		insert_device(dev);

	return(0);
}

/**
   \brief Timer callback to update the feed request
   \param nada used for file descriptor
   \param what why did we fire?
   \param arg void pointer to client_t of connection
*/

void feeddata_cb(int nada, short what, void *arg)
{
	client_t *client = (client_t *)arg;
	device_t *dev;
	wrap_device_t *wrap;
	time_t now, diff;

	(void)time(&now);
	TAILQ_FOREACH(wrap, &client->wdevices, next) {
		diff = now - wrap->last_fired;
		if (diff / wrap->rate >= 1) { /* this dev is ready */
			gn_update_device(wrap->dev, GNC_UPD_RRDNAME,
					 client->ev);
			wrap->last_fired = now;
		}
	}
}

/**
	\brief Handle a feed device command
	\param args The list of arguments
	\param arg void pointer to client_t of provider
*/

int cmd_feed(pargs_t *args, void *arg)
{
	int i, rate=60, lc;
	char *uid=NULL;
	struct timeval secs = {0, 0};
	device_t *dev;
	wrap_device_t *wrap;
	client_t *client = (client_t *)arg;

	for (i=0; args[i].cword != -1; i++) {
		switch (args[i].cword) {
		case SC_UID:
			uid = args[i].arg.c;
			break;
		case SC_RATE:
			rate = args[i].arg.i;
			break;
		}
	}
	secs.tv_sec = rate;
	dev = find_device_byuid(uid);
	if (dev == NULL)
		return -1;
	add_wrapped_device(dev, client, rate);
	if (client->tev == NULL || !event_initialized(client->tev)) {
		client->tev = event_new(base, -1, EV_PERSIST, feeddata_cb,
					client);
		event_add(client->tev, &secs);
		client->timer_gcd = rate;
	} else {
		TAILQ_FOREACH(wrap, &client->wdevices, next) {
			lc = gcd(wrap->rate, client->timer_gcd);
			client->timer_gcd = lc;
		}
		secs.tv_sec = client->timer_gcd;
		event_add(client->tev, &secs); /* update with new lcm */
	}
	return 0;
}

/**
	\brief Handle a list devices command
	\param args The list of arguments
	\param arg void pointer to client_t of connection
*/

int cmd_list_devices(pargs_t *args, void *arg)
{
	int i, devtype, proto, subtype;
	device_t *dev;
	char *uid = NULL;
	client_t *client = (client_t *)arg;
	struct evbuffer *send;

	devtype = proto = subtype = 0;

	/* the arguments to this command are used as qualifiers for the
	   device list search */
	for (i=0; args[i].cword != -1; i++) {
		switch (args[i].cword) {
		case SC_UID:
			uid = args[i].arg.c;
			break;
		case SC_DEVTYPE:
			devtype = args[i].arg.i;
			break;
		case SC_PROTO:
			proto = args[i].arg.i;
			break;
		case SC_SUBTYPE:
			subtype = args[i].arg.i;
			break;
		}
	}
	TAILQ_FOREACH(dev, &alldevs, next_all) {
		if (uid && strcmp(uid, dev->uid) != 0)
			continue;
		if (devtype && devtype != dev->type)
			continue;
		if (proto && proto != dev->proto)
			continue;
		if (subtype && subtype != dev->subtype)
			continue;
		gn_register_device(dev, client->ev);
	}
	send = evbuffer_new();
	evbuffer_add_printf(send, "endldevs\n");
	bufferevent_write_buffer(client->ev, send);
	evbuffer_free(send);
	return 0;
}

/**
	\brief Handle a ask device command
	\param args The list of arguments
	\param arg void pointer to client_t of connection
*/

int cmd_ask_device(pargs_t *args, void *arg)
{
	int i;
	device_t *dev;
	char *uid = NULL;
	client_t *client = (client_t *)arg;

	/* the arguments to this command are used as qualifiers for the
	   device list search */
	for (i=0; args[i].cword != -1; i++) {
		if (args[i].cword == SC_UID) {
			uid = args[i].arg.c;
			dev = find_device_byuid(uid);
			if (dev == NULL)
				continue;
			gn_update_device(dev, GNC_UPD_NAME|GNC_UPD_RRDNAME,
					 client->ev);
		}
	}
	return 0;
}

/**
	\brief Handle a ask device command
	\param args The list of arguments
	\param arg void pointer to client_t of connection
*/

int cmd_cactiask_device(pargs_t *args, void *arg)
{
	int i;
	device_t *dev;
	char *uid = NULL;
	client_t *client = (client_t *)arg;

	/* the arguments to this command are used as qualifiers for the
	   device list search */
	for (i=0; args[i].cword != -1; i++) {
		if (args[i].cword == SC_UID) {
			uid = args[i].arg.c;
			dev = find_device_byuid(uid);
			if (dev == NULL)
				continue;
			gn_update_device(dev, GNC_UPD_CACTI, client->ev);
		}
	}
	return 0;
}
