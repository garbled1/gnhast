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
#include <math.h>
/* #define QUEUEDEBUG 1 */
#include <sys/queue.h>

#include "gnhast.h"
#include "common.h"
#include "commands.h"
#include "cmds.h"
#include "gncoll.h"
#include "gnhastd.h"

extern TAILQ_HEAD(, _device_t) alldevs;
extern TAILQ_HEAD(, _device_group_t) allgroups;
extern TAILQ_HEAD(, _client_t) clients;
extern TAILQ_HEAD(, _alarm_t) alarms;

/**
   \file cmdhandler.c
   \brief Common command handlers
   \author Tim Rightnour
*/

extern struct event_base *base;
extern argtable_t argtable[];

/* When adding new commands, update the following files:
   gnhastd/cmds.h
   common/gncoll.c  (if we want to make a shortcut cmd for clients)
*/

/** The command table */
commands_t commands[] = {
	{"chg", cmd_change, 0},
	{"reg", cmd_register, 0},
	{"regg", cmd_register_group, 0},
	{"upd", cmd_update, 0},
	{"mod", cmd_modify, 0},
	{"feed", cmd_feed, 0},
	{"cfeed", cmd_cfeed, 0},
	{"ldevs", cmd_list_devices, 0},
	{"lgrps", cmd_list_groups, 0},
	{"ask", cmd_ask_device, 0},
	{"askf", cmd_ask_full_device, 0},
	{"cactiask", cmd_cactiask_device, 0},
	{"disconnect", cmd_disconnect, 0},
	{"client", cmd_client, 0},
	{"ping", cmd_ping, 0},
	{"imalive", cmd_imalive, 0},
	{"setalarm", cmd_setalarm, 0},
	{"listenalarms", cmd_listen_alarms, 0},
	{"dumpalarms", cmd_dump_alarms, 0},
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
	int hadnodata = 0;
	device_t *dev;
	char *uid=NULL;
	client_t *client = (client_t *)arg;
	wrap_client_t *dwatch;

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

	/* check if the device was unknown at the time */
	if (QUERY_FLAG(dev->flags, DEVFLAG_NODATA))
		hadnodata = 1;

	/* Ok, we got one, now lets update it's data */

	for (i=0; args[i].cword != -1; i++) {
		switch (args[i].cword) {
		case SC_SWITCH:
		case SC_WEATHER:
		case SC_ALARMSTATUS:
		case SC_THMODE:
		case SC_THSTATE:
		case SC_SMNUMBER:
		case SC_COLLECTOR:
		case SC_BLIND:
			store_data_dev(dev, DATALOC_DATA, &args[i].arg.i);
			break;
		case SC_LUX:
		case SC_HUMID:
		case SC_TEMP:
		case SC_DIMMER:
		case SC_PRESSURE:
		case SC_SPEED:
		case SC_DIR:
		case SC_PH:
		case SC_WETNESS:
		case SC_VOLTAGE:
		case SC_WATT:
		case SC_AMPS:
		case SC_PERCENTAGE:
		case SC_FLOWRATE:
		case SC_DISTANCE:
		case SC_VOLUME:
		case SC_ORP:
		case SC_SALINITY:
			store_data_dev(dev, DATALOC_DATA, &args[i].arg.d);
			break;
		case SC_COUNT:
		case SC_TIMER:
		case SC_TRIGGER:
			store_data_dev(dev, DATALOC_DATA, &args[i].arg.u);
			break;
		case SC_WATTSEC:
		case SC_NUMBER:
			store_data_dev(dev, DATALOC_DATA, &args[i].arg.ll);
			break;
		}
	}
	(void)time(&dev->last_upd);
	client->updates++;

	/* Always run handler on first update */
	if (dev->handler != NULL && (device_watermark(dev) != 0 || hadnodata))
		run_handler_dev(dev);

	/* look for clients watching us, and update them */
	TAILQ_FOREACH(dwatch, &dev->watchers, next) {
		gn_update_device(dev, GNC_UPD_RRDNAME, dwatch->client->ev);
		dwatch->client->sentdata++;
	}

	return(0);
}

/**
   \brief Handle a change device command
   \param args The list of arguments
   \param arg void pointer to client_t of provider
*/

int cmd_change(pargs_t *args, void *arg)
{
	int i;
	device_t *dev;
	char *uid=NULL;
	client_t *client = (client_t *)arg;
	struct evbuffer *send;

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

	send = evbuffer_new();
	/* The command to change is "chg" */
	evbuffer_add_printf(send, "chg ");

	/* fill in the details */
	evbuffer_add_printf(send, "%s:%s", ARGNM(SC_UID), dev->uid);

	for (i=0; args[i].cword != -1; i++) {
		switch (args[i].cword) {
		case SC_WEATHER:
		case SC_SWITCH:
		case SC_ALARMSTATUS:
		case SC_THMODE:
		case SC_THSTATE:
		case SC_SMNUMBER:
		case SC_COLLECTOR:
		case SC_BLIND:
			evbuffer_add_printf(send, " %s:%d",
					    ARGNM(args[i].cword),
					    args[i].arg.i);
			break;
		case SC_LUX:
		case SC_HUMID:
		case SC_TEMP:
		case SC_DIMMER:
		case SC_PRESSURE:
		case SC_SPEED:
		case SC_DIR:
		case SC_PH:
		case SC_WETNESS:
		case SC_VOLTAGE:
		case SC_WATT:
		case SC_AMPS:
		case SC_PERCENTAGE:
		case SC_FLOWRATE:
		case SC_DISTANCE:
		case SC_VOLUME:
		case SC_ORP:
		case SC_SALINITY:
			evbuffer_add_printf(send, " %s:%f",
					    ARGNM(args[i].cword),
					    args[i].arg.d);
			store_data_dev(dev, DATALOC_DATA, &args[i].arg.d);
			break;
		case SC_COUNT:
		case SC_TIMER:
		case SC_TRIGGER:
			evbuffer_add_printf(send, " %s:%d",
					    ARGNM(args[i].cword),
					    args[i].arg.u);
			break;
		case SC_WATTSEC:
		case SC_NUMBER:
			evbuffer_add_printf(send, " %s:%jd",
					    ARGNM(args[i].cword),
					    args[i].arg.ll);
			break;
		}
	}
	if (dev->collector == NULL) {
		LOG(LOG_ERROR, "Got chg for uid:%s, but no collector",
		    dev->uid);
		return(-1);
	}
	/* and send it on it's way */
	evbuffer_add_printf(send, "\n");
	bufferevent_write_buffer(dev->collector->ev, send);

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
	uint8_t devtype=0, proto=0, subtype=0, scale=0;
	char *uid=NULL, *name=NULL, *rrdname=NULL;
	device_t *dev;
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
		case SC_SCALE:
			scale = (uint8_t)args[i].arg.i;
			break;
		}
	}

	if (uid == NULL) {
		LOG(LOG_ERROR, "Got register command without UID");
		return(-1); /* MUST have UID */
	}

	LOG(LOG_DEBUG, "Register device: uid=%s name=%s rrd=%s type=%d "
	    "proto=%d subtype=%d",
	    uid, (name) ? name : "NULL", (rrdname) ? rrdname : "NULL",
	    devtype, proto, subtype);

	dev = find_device_byuid(uid);
	if (dev == NULL) {
		LOG(LOG_DEBUG, "Creating new device for uid %s", uid);
		if (subtype == 0 || devtype == 0 || proto == 0 ||
		    name == NULL) {
			LOG(LOG_ERROR, "Attempt to register new device "
			    "without full specifications");
			return(-1);
		}
		if (rrdname == NULL)
			rrdname = mk_rrdname(name);
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
	dev->scale = scale;
	(void)time(&dev->last_upd);
	if (dev->collector != NULL && dev->collector != client)
		LOG(LOG_ERROR, "Device uid:%s has a collector, but a new "
		    "one registered it!", dev->uid);
	dev->collector = client;

	if (TAILQ_EMPTY(&dev->watchers) || new)
		TAILQ_INIT(&dev->watchers);

	/* now, update the client_t */
	if (TAILQ_EMPTY(&client->devices)) {
		TAILQ_INIT(&client->devices);
		client->provider = 1; /* this client is a provider */
	}
	if (!(dev->onq & DEVONQ_CLIENT)) {
		TAILQ_INSERT_TAIL(&client->devices, dev, next_client);
		dev->onq |= DEVONQ_CLIENT;
	}
	dev->collector = client;
	
	if (new)
		insert_device(dev);

	return(0);
}

/**
	\brief Handle a register device group command
	\param args The list of arguments
	\param arg void pointer to client_t of provider
*/

int cmd_register_group(pargs_t *args, void *arg)
{
	int i, found;
	char *uid=NULL, *name=NULL, *grouplist=NULL, *devlist=NULL;
	char *tmpbuf, *p;
	device_group_t *devgrp, *cgrp;
	device_t *dev;
	wrap_group_t *wrapg;
	wrap_device_t *wrap;
	client_t *client = (client_t *)arg;

	for (i=0; args[i].cword != -1; i++) {
		switch (args[i].cword) {
		case SC_UID:
			uid = strdup(args[i].arg.c);
			break;
		case SC_NAME:
			name = strdup(args[i].arg.c);
			break;
		case SC_GROUPLIST:
			grouplist = strdup(args[i].arg.c);
			break;
		case SC_DEVLIST:
			devlist = strdup(args[i].arg.c);
			break;
		}
	}

	if (uid == NULL) {
		LOG(LOG_ERROR, "Got register group command without UID");
		return(-1); /* MUST have UID */
	}

	LOG(LOG_DEBUG, "Register group: uid=%s name=%s grouplist=%s "
	    "devlist=%s", uid, (name) ? name : "NULL",
	    (grouplist) ? grouplist : "NULL", (devlist) ? devlist : "NULL");

	devgrp = find_devgroup_byuid(uid);
	if (devgrp == NULL) {
		LOG(LOG_DEBUG, "Creating new device group for uid %s", uid);
		if (name == NULL) {
			LOG(LOG_ERROR, "Attempt to register new group without"
			    " name");
			return(-1);
		}
		devgrp = new_devgroup(uid);
	} else
		LOG(LOG_DEBUG, "Updating existing device group uid:%s", uid);

	if (name != NULL)
		devgrp->name = name;

	if (grouplist != NULL) {
		tmpbuf = strdup(grouplist);
		for (p = strtok(tmpbuf, ","); p; p = strtok(NULL, ",")) {
			cgrp = find_devgroup_byuid(p);
			if (cgrp == NULL)
				LOG(LOG_ERROR, "Cannot find child group %s "
				    "while attempting to register group %s",
				    p, uid);
			else if (!group_in_group(cgrp, devgrp)){
				add_group_group(cgrp, devgrp);
				LOG(LOG_NOTICE, "Adding child group %s to "
				    "group %s", cgrp->uid, uid);
			}
		}
		free(tmpbuf);
		/* Now look for groups that no longer belong */
		TAILQ_FOREACH(wrapg, &devgrp->children, nextg) {
			LOG(LOG_DEBUG, "Working on: %s", wrapg->group->uid);
			found = 0;
			tmpbuf = strdup(grouplist);
			for (p = strtok(tmpbuf, ","); p;
			     p = strtok(NULL, ",")) {
				if (strcmp(p, wrapg->group->uid) == 0) {
					found = 1;
					continue;
				}
			}
			if (!found) {
				LOG(LOG_NOTICE, "removing group %s from group "
				    "%s", wrapg->group->uid, devgrp->uid);
				remove_group_group(wrapg->group, devgrp);
			}
			free(tmpbuf);
		}
		free(grouplist);
	}

	if (devlist != NULL) {
		tmpbuf = strdup(devlist);
		for (p = strtok(tmpbuf, ","); p; p = strtok(NULL, ",")) {
			dev = find_device_byuid(p);
			if (dev == NULL)
				LOG(LOG_ERROR, "Cannot find child device %s "
				    "while attempting to register group %s",
				    p, uid);
			else if (!dev_in_group(dev, devgrp)) {
				add_dev_group(dev, devgrp);
				LOG(LOG_DEBUG, "Adding child device %s to "
				    "group %s", dev->uid, uid);
			}
		}
		free(tmpbuf);
		TAILQ_FOREACH(wrap, &devgrp->members, next) {
			found = 0;
			tmpbuf = strdup(devlist);
			for (p = strtok(tmpbuf, ","); p;
			     p = strtok(NULL, ",")) {
				if ((strlen(p) == strlen(wrap->dev->uid)) &&
				    strcmp(p, wrap->dev->uid) == 0) {
					found = 1;
					continue;
				}
			}
			if (!found) {
				LOG(LOG_DEBUG, "removing device %s from group "
				    "%s", wrap->dev->uid, devgrp->uid);
				remove_dev_group(wrap->dev, devgrp);
			}
			free(tmpbuf);
		}
		free(devlist);
	}

	return(0);
}

/**
   \brief Handle a modify device command
   \param args The list of arguments
   \param arg void pointer to client_t of provider
   We modify the device internally, rewrite the conf, and then forward the
   change to any collectors for that device.
   We do not need to send hiwat/lowat/handler stuff to the clients, because
   while thier config files may list it, they do not forward that info to
   the server.
*/

int cmd_modify(pargs_t *args, void *arg)
{
	int i, j;
	double d;
	uint32_t u;
	int64_t ll;
	device_t *dev;
	char *uid=NULL;
	client_t *client = (client_t *)arg;
	struct evbuffer *send;

	/* loop through the args and find the UID */
	for (i=0; args[i].cword != -1; i++) {
		switch (args[i].cword) {
		case SC_UID:
			uid = args[i].arg.c;
			break;
		}
	}
	if (!uid) {
		LOG(LOG_ERROR, "modify without UID");
		return(-1);
	}
	dev = find_device_byuid(uid);
	if (!dev) {
		LOG(LOG_ERROR, "UID:%s doesn't exist", uid);
		return(-1);
	}

	send = evbuffer_new();
	/* The command to modify is "mod" */
	evbuffer_add_printf(send, "mod ");

	/* fill in the details */
	evbuffer_add_printf(send, "%s:%s", ARGNM(SC_UID), dev->uid);

	for (i=0; args[i].cword != -1; i++) {
		switch (args[i].cword) {
		case SC_NAME:
			free(dev->name);
			dev->name = strdup(args[i].arg.c);
			evbuffer_add_printf(send, " %s:\"%s\"",
					    ARGNM(args[i].cword),
					    args[i].arg.c);
			LOG(LOG_NOTICE, "Changing uid:%s name to %s",
			    dev->uid, dev->name);
			break;
		case SC_RRDNAME:
			free(dev->rrdname);
			dev->rrdname = strdup(args[i].arg.c);
			evbuffer_add_printf(send, " %s:\"%s\"",
					    ARGNM(args[i].cword),
					    args[i].arg.c);
			LOG(LOG_NOTICE, "Changing uid:%s rrdname to %s",
			    dev->uid, dev->rrdname);
			break;
		case SC_HANDLER:
			if (dev->handler != NULL)
				free(dev->handler);
			dev->handler = strdup(args[i].arg.c);
			LOG(LOG_NOTICE, "Changing uid:%s handler to %s",
			    dev->uid, dev->handler);
			break;
		case SC_HIWAT:
		case SC_LOWAT:
			if (args[i].cword == SC_HIWAT)
				j = DATALOC_HIWAT;
			else
				j = DATALOC_LOWAT;
			switch (datatype_dev(dev)) {
			case DATATYPE_UINT:
				u = (uint32_t)lrint(args[i].arg.d);
				store_data_dev(dev, j, &u);
				LOG(LOG_NOTICE, "Changing uid:%s %s to %ud",
				    dev->uid,
				    (j == DATALOC_HIWAT) ? "high watermark" :
				    "low watermark", u);
				break;
			case DATATYPE_LL:
				ll = (int64_t)llrint(args[i].arg.d);
				store_data_dev(dev, j, &ll);
				LOG(LOG_NOTICE, "Changing uid:%s %s to %lld",
				    dev->uid,
				    (j == DATALOC_HIWAT) ? "high watermark" :
				    "low watermark", ll);
				break;
			case DATATYPE_DOUBLE:
				d = args[i].arg.d;
				store_data_dev(dev, j, &d);
				LOG(LOG_NOTICE, "Changing uid:%s %s to %f",
				    dev->uid,
				    (j == DATALOC_HIWAT) ? "high watermark" :
				    "low watermark", d);
				break;
			}
			break;
		case SC_HARGS:
			parse_hargs(dev, args[i].arg.c);
			LOG(LOG_NOTICE, "Handler args uid:%s changed to %s,"
			    " %d arguments", dev->uid, args[i].arg.c,
			    dev->nrofhargs);
			break;
		case SC_SPAM:
			LOG(LOG_NOTICE, "Spamhandler for uid:%s changed to %d",
			    dev->uid, args[i].arg.i);
			switch (args[i].arg.i) {
			case 0:
				CLEAR_FLAG(dev->flags, DEVFLAG_SPAMHANDLER);
				CLEAR_FLAG(dev->flags, DEVFLAG_CHANGEHANDLER);
				break;
			case 1:
				SET_FLAG(dev->flags, DEVFLAG_SPAMHANDLER);
				CLEAR_FLAG(dev->flags, DEVFLAG_CHANGEHANDLER);
				break;
			case 2:
				CLEAR_FLAG(dev->flags, DEVFLAG_SPAMHANDLER);
				SET_FLAG(dev->flags, DEVFLAG_CHANGEHANDLER);
				break;
			default:
				LOG(LOG_ERROR, "Invalid spamhandler "
				    "argument %d", args[i].arg.i);
				break;
			}
			break;
		}
	}
	/* force a device conf rewrite */
	devconf_dump_cb(0, 0, 0);
	/* XXX send to wrapped devices? */
	if (dev->collector == NULL) {
		LOG(LOG_WARNING, "Got mod for uid:%s, but no collector",
		    dev->uid);
		return(0);
	}
	/* and send it on it's way */
	evbuffer_add_printf(send, "\n");
	bufferevent_write_buffer(dev->collector->ev, send);

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
	wrap_device_t *wrap;
	time_t now, diff;

	(void)time(&now);
	TAILQ_FOREACH(wrap, &client->wdevices, next) {
		diff = now - wrap->last_fired;
		if (diff / wrap->rate >= 1) { /* this dev is ready */
			gn_update_device(wrap->dev, GNC_UPD_RRDNAME |
					 GNC_UPD_SCALE(wrap->scale),
					 client->ev);
			wrap->last_fired = now;
			client->sentdata++;
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
	int i, rate=60, lc, scale;
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
	if (uid == NULL)
		return -1;
	dev = find_device_byuid(uid);
	if (dev == NULL)
		return -1;

	/* check for scale arg */
	scale = dev->scale;
	for (i=0; args[i].cword != -1; i++) {
		switch (args[i].cword) {
		case SC_SCALE:
			scale = args[i].arg.i;
			break;
		}
	}

	add_wrapped_device(dev, client, rate, scale);
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
	client->feeds++;

	return 0;
}

/**
   \brief Handle a cfeed device command
   \param args The list of arguments
   \param arg void pointer to client_t of provider
   \note This command will request immediate updates when a device is updated
   by a provider. No scaling is provided here.  sorry.
*/

int cmd_cfeed(pargs_t *args, void *arg)
{
	int i;
	char *uid=NULL;
	device_t *dev;
	client_t *client = (client_t *)arg;

	for (i=0; args[i].cword != -1; i++) {
		switch (args[i].cword) {
		case SC_UID:
			uid = args[i].arg.c;
			break;
		}
	}
	if (uid == NULL)
		return -1;
	dev = find_device_byuid(uid);
	if (dev == NULL)
		return -1;

	add_wrapped_client(client, dev);

	return 0;
}

/**
   \brief Handle a client command
   \param args The list of arguments
   \param arg void pointer to client_t of provider

   We setup a device to track each provider
*/

int cmd_client(pargs_t *args, void *arg)
{
	int i;
	char *cli=NULL;
	char uid[512], buf[512], cliaddr[256];
	client_t *client = (client_t *)arg;
	device_t *dev;

	for (i=0; args[i].cword != -1; i++) {
		switch (args[i].cword) {
		case SC_CLIENT:
			cli = args[i].arg.c;
			break;
		}
	}

	if (cli == NULL)
		return -1;
	client->name = strdup(cli);
	sprintf(cliaddr, "%s", client->host ? client->host : "unknown");
	LOG(LOG_NOTICE, "Client %s registered as %s", cliaddr, cli);

	/* don't create tracking devices for handlers */
	if (strcmp(cli, "handler") == 0)
		return 0;

	/* now, build a tracking device for it */
	(void)sprintf(uid, "%s-%s", cli, cliaddr);
	dev = find_device_byuid(uid);
	if (dev == NULL) {
		LOG(LOG_NOTICE, "Creating handling device for collector with"
		    " uid:%s", uid);
		dev = smalloc(device_t);
		dev->uid = strdup(uid);
		sprintf(buf, "Collector %s from host %s", cli, cliaddr);
		dev->name = strdup(buf);
		sprintf(buf, "coll_%s_%s", cli, cliaddr);
		dev->rrdname = mk_rrdname(buf);
		dev->type = DEVICE_SWITCH;
		dev->proto = PROTO_COLLECTOR;
		dev->subtype = SUBTYPE_COLLECTOR;
		dev->collector = client; /* point to self */
		client->coll_dev = dev; /* client_t points to this dev */
		TAILQ_INIT(&dev->watchers);
		insert_device(dev);
	} else {
		/* we have an existing device that has re-connected */
		LOG(LOG_NOTICE, "Re-connecting handler device %s to client %s",
		    uid, client->name);
		client->coll_dev = dev;
	}

	/* mark this collector as functional */
	i = COLLECTOR_OK;
	store_data_dev(dev, DATALOC_DATA, &i);

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
   \brief Handle a list groups command
   \param args The list of arguments
   \param arg void pointer to client_t of connection
*/

int cmd_list_groups(pargs_t *args, void *arg)
{
	int i;
	device_group_t *devgrp;
	char *uid = NULL;
	char *name = NULL;
	client_t *client = (client_t *)arg;
	struct evbuffer *send;


	/* the arguments to this command are used as qualifiers for the
	   device list search */
	for (i=0; args[i].cword != -1; i++) {
		switch (args[i].cword) {
		case SC_UID:
			uid = args[i].arg.c;
			break;
		case SC_NAME:
			name = args[i].arg.c;
			break;
		}
	}
	TAILQ_FOREACH(devgrp, &allgroups, next_all) {
		if (uid && strcmp(uid, devgrp->uid) != 0)
			continue;
		if (name && strcmp(uid, devgrp->name) != 0)
			continue;
		gn_register_devgroup(devgrp, client->ev);
	}
	send = evbuffer_new();
	evbuffer_add_printf(send, "endlgrps\n");
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
	int i, scale, what;
	device_t *dev;
	char *uid = NULL;
	client_t *client = (client_t *)arg;

	what = GNC_UPD_NAME|GNC_UPD_RRDNAME;

	/* first, find the device */
	dev = NULL;
	for (i=0; args[i].cword != -1; i++) {
		if (args[i].cword == SC_UID) {
			uid = args[i].arg.c;
			dev = find_device_byuid(uid);
		}
	}
	if (dev == NULL)
		return 1;

	/* preset the scale to the device's native scale */
	scale = dev->scale;

	/* check for scale/flag arguments */
	for (i=0; args[i].cword != -1; i++)
		switch (args[i].cword) {
		case SC_SCALE: scale = args[i].arg.i; break;
		case SC_HIWAT:
		case SC_LOWAT: what |= GNC_UPD_WATER; break;
		case SC_HANDLER: what |= GNC_UPD_HANDLER; break;
		case SC_HARGS: what |= GNC_UPD_HARGS; break;
		}

	gn_update_device(dev, what|GNC_UPD_SCALE(scale), client->ev);
	client->sentdata++;

	return 0;
}

/**
   \brief Handle a ask device command
   \param args The list of arguments
   \param arg void pointer to client_t of connection
*/

int cmd_ask_full_device(pargs_t *args, void *arg)
{
	int i;
	device_t *dev;
	char *uid = NULL;
	client_t *client = (client_t *)arg;

	for (i=0; args[i].cword != -1; i++) {
		if (args[i].cword == SC_UID) {
			uid = args[i].arg.c;
			dev = find_device_byuid(uid);
			if (dev == NULL)
				continue;
			gn_update_device(dev, GNC_UPD_FULL, client->ev);
			client->sentdata++;
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
	int i, scale;
	device_t *dev;
	char *uid = NULL;
	client_t *client = (client_t *)arg;

	/* check for uid argument first */
	for (i=0; args[i].cword != -1; i++)
		if (args[i].cword == SC_UID) {
			uid = args[i].arg.c;
			dev = find_device_byuid(uid);
			if (dev == NULL)
				return 0;
		}
	/* now setup scale */
	scale = dev->scale;
	for (i=0; args[i].cword != -1; i++)
		if (args[i].cword == SC_SCALE)
			scale = args[i].arg.i;

	gn_update_device(dev, GNC_UPD_CACTI|GNC_UPD_SCALE(scale), client->ev);
	client->sentdata++;

	return 0;
}

/**
   \brief Close a connection down
   \param args The list of arguments (ignored)
   \param arg void pointer to client_t of connection
*/

int cmd_disconnect(pargs_t *args, void *arg)
{
	client_t *client = (client_t *)arg;
	int i;

	/* set close on empty, set watermark, then enable the write callback */
	client->close_on_empty = 1;
	bufferevent_setwatermark(client->ev, EV_WRITE, 0, 0);
	bufferevent_setcb(client->ev, buf_read_cb, buf_write_cb,
			  buf_error_cb, client);
	bufferevent_enable(client->ev, EV_READ|EV_WRITE);
	LOG(LOG_NOTICE, "Client %s from %s requested disconnect",
	    client->name ? client->name : "generic",
	    client->addr ? client->addr : "unknown");

	/* mark this collector as non-functional */
	i = COLLECTOR_BAD;
	if (client->coll_dev != NULL)
		store_data_dev(client->coll_dev, DATALOC_DATA, &i);

	return 0;
}

/**
   \brief Got a ping. (WHY?!)
   \param args The list of arguments (ignored)
   \param arg void pointer to client_t of connection
*/

int cmd_ping(pargs_t *args, void *arg)
{
	client_t *client = (client_t *)arg;

	LOG(LOG_NOTICE, "Client %s from %s pinged us",
	    client->name ? client->name : "generic",
	    client->addr ? client->addr : "unknown");

	return 0;
}

/**
   \brief Got an imalive.  The lastupd is handled in netloop.c:buf_read_cb()
   \param args The list of arguments (ignored)
   \param arg void pointer to client_t of connection
*/

int cmd_imalive(pargs_t *args, void *arg)
{
	client_t *client = (client_t *)arg;
	static uint8_t i = COLLECTOR_OK;

	LOG(LOG_DEBUG, "Client %s from %s is alive",
	    client->name ? client->name : "generic",
	    client->addr ? client->addr : "unknown");
	if (client->coll_dev != NULL)
		store_data_dev(client->coll_dev, DATALOC_DATA, &i);

	return 0;
}


/**
   \brief Got a setalarm
   \param args The list of arguments
   \param arg void pointer to client_t of connection
*/

int cmd_setalarm(pargs_t *args, void *arg)
{
	client_t *client = (client_t *)arg;
	client_t *tell;
	alarm_t *alarm;
	char *aluid = NULL;
	char *altext = NULL;
	int alsev = -1;
	uint32_t alchan = 1; /* generic channel */
	int i;

	LOG(LOG_DEBUG, "Got alarm set request");

	for (i=0; args[i].cword != -1; i++)
		switch (args[i].cword) {
		case SC_ALUID: aluid = args[i].arg.c; break;
		case SC_ALTEXT: altext = args[i].arg.c; break;
		case SC_ALSEV: alsev = args[i].arg.i; break;
		case SC_ALCHAN: alchan = args[i].arg.u; break;
		}

	if (aluid == NULL) {
		LOG(LOG_WARNING, "Got no aluid in setalarm, ignoring");
		return 0;
	}

	alarm = update_alarm(aluid, altext, alsev, alchan);

	if (alarm == NULL) { /* the alarm has been unset, tell everyone */
		TAILQ_FOREACH(tell, &clients, next) {
			if (tell->alarmwatch > 0) {
				/* send a clearing message */
				gn_setalarm(tell->ev, aluid, "", 0,
					    ALL_FLAGS_SET);
			}
		}
	} else {
		TAILQ_FOREACH(tell, &clients, next)
			if (tell->alarmwatch <= alarm->alsev &&
			    tell->alarmwatch != 0 &&
			    CHECK_MASK(tell->alchan, alarm->alchan))
				gn_setalarm(tell->ev, alarm->aluid,
					    alarm->altext, alarm->alsev,
					    alarm->alchan);
	}

	return 0;
}


/**
   \brief Got a request to listen to alarms
   \param args The list of arguments
   \param arg void pointer to client_t of connection
   \note if called with no args, unlistens
*/

int cmd_listen_alarms(pargs_t *args, void *arg)
{
	client_t *client = (client_t *)arg;
	int alsev = 0; /* assume 0 if no arg */
	uint32_t alchan = ALL_FLAGS_SET;
	int i;

	for (i=0; args[i].cword != -1; i++)
		switch (args[i].cword) {
		case SC_ALSEV: alsev = args[i].arg.i; break;
		case SC_ALCHAN: alchan = args[i].arg.u; break;
		}

	LOG(LOG_NOTICE, "Client %s from %s asked to listen to alarm sev %d",
	    client->name ? client->name : "generic",
	    client->addr ? client->addr : "unknown", alsev);

	client->alarmwatch = alsev;
	client->alchan = alchan;

	return 0;
}

/**
   \brief Got a request to dump all alarms
   \param args The list of arguments
   \param arg void pointer to client_t of connection
*/

int cmd_dump_alarms(pargs_t *args, void *arg)
{
	client_t *client = (client_t *)arg;
	client_t *tell;
	alarm_t *alarm;
	int alsev = 1; /* assume 1 if no arg */
	uint32_t alchan = ALL_FLAGS_SET; /* assume if no arg */
	char *aluid = NULL;
	int i;

	for (i=0; args[i].cword != -1; i++)
		switch (args[i].cword) {
		case SC_ALSEV: alsev = args[i].arg.i; break;
		case SC_ALUID: aluid = args[i].arg.c; break;
		case SC_ALCHAN: alchan = args[i].arg.u; break;
		}

	if (aluid == NULL) { /* dump everything */
		TAILQ_FOREACH(alarm, &alarms, next) {
			LOG(LOG_DEBUG, "alarm: %s %s %d %u", alarm->aluid,
			    alarm->altext, alarm->alsev,
			    alarm->alchan);
			if (alarm->alsev >= alsev &&
			    CHECK_MASK(alarm->alchan, alchan))
				gn_setalarm(client->ev, alarm->aluid,
					    alarm->altext, alarm->alsev,
					    alarm->alchan);
		}
	} else {
		TAILQ_FOREACH(alarm, &alarms, next) {
			if (alarm->alsev >= alsev &&
			    strcmp(alarm->aluid, aluid) == 0 &&
			    CHECK_MASK(alarm->alchan, alchan)) {
				gn_setalarm(client->ev, alarm->aluid,
					    alarm->altext, alarm->alsev,
					    alarm->alchan);
			}
		}
	}

	return 0;
}
