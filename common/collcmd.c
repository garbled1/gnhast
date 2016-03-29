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
   \file collcmd.c
   \brief Common command handlers for collectors
   \author Tim Rightnour

   These routines are called by collectors.  NEVER by gnhastd itself.
   Therefore, arg is a pointer to the collector's internal connection_t type,
   and is probably useless to us.
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
#include <event2/dns.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/bufferevent_ssl.h>

#include "common.h"
#include "gnhast.h"
#include "gncoll.h"
#include "collcmd.h"
#include "confuse.h"

extern int cmd_endldevs(pargs_t *args, void *arg);
extern int cmd_endlgrps(pargs_t *args, void *arg);
extern int cmd_ping(pargs_t *args, void *arg);
extern int cmd_imalive(pargs_t *args, void *arg);
extern int cmd_die(pargs_t *args, void *arg);
extern void coll_upd_cb(device_t *dev, void *arg);
extern void coll_chg_switch_cb(device_t *dev, int state, void *arg);
extern void coll_chg_dimmer_cb(device_t *dev, double level, void *arg);
extern void coll_chg_timer_cb(device_t *dev, uint32_t tstate, void *arg);
extern void coll_chg_number_cb(device_t *dev, int64_t num, void *arg);

/* Bring these in scope so mod can dump a conf file */
extern char *conffile;
extern cfg_t *cfg;
extern struct bufferevent *gnhastd_bev;

/** The command table */
commands_t commands[] = {
	{"chg", cmd_change, 0},
	{"reg", cmd_register, 0},
	{"regg", cmd_register_group, 0},
	{"endldevs", cmd_endldevs, 0},
	{"endlgrps", cmd_endlgrps, 0},
	{"ping", cmd_ping, 0},
	{"imalive", cmd_imalive, 0},
	{"die", cmd_die, 0},
	{"upd", cmd_update, 0},
	{"mod", cmd_modify, 0},
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

/*** Weak Reference Stubs ***/

/**
   \brief Called when an upd command occurs (stub)
   \param dev device that got updated
   \param arg pointer to connection_t
*/

void __attribute__((weak)) coll_upd_cb(device_t *dev, void *arg)
{
	return;
}

/**
   \brief Called when an chg command occurs (stub)
   \param dev device that got updated
   \param arg pointer to connection_t
*/

void __attribute__((weak)) coll_chg_cb(device_t *dev, void *arg)
{
	return;
}

/**
   \brief Handle a enldevs device command (stub)
   \param args The list of arguments
   \param arg void pointer to connection_t
*/

int __attribute__((weak)) cmd_endldevs(pargs_t *args, void *arg)
{
	return;
}

/**
   \brief Handle a endlgrps device command (stub)
   \param args The list of arguments
   \param arg void pointer to connection_t
*/

int __attribute__((weak)) cmd_endlgrps(pargs_t *args, void *arg)
{
	return;
}

/**
   \brief Handle a ping request (stub)
   \param arg void pointer to connection_t
*/

int __attribute__((weak)) cmd_ping(pargs_t *args, void *arg)
{
	if (collector_is_ok() && gnhastd_bev != NULL)
		gn_imalive(gnhastd_bev);
	return(0);
}

/**
   \brief Handle an imalive request (stub)
   \param arg void pointer to connection_t
*/

int __attribute__((weak)) cmd_imalive(pargs_t *args, void *arg)
{
	return(0);
}

/**
   \brief Handle a die request (stub)
   \param arg void pointer to connection_t
*/

int __attribute__((weak)) cmd_die(pargs_t *args, void *arg)
{
	return(0);
}

/***** Full blown commands *****/

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
	device_t *dev;

	for (i=0; args[i].cword != -1; i++) {
		switch (args[i].cword) {
		case SC_UID:
			uid = args[i].arg.c;
			break;
		case SC_NAME:
			name = args[i].arg.c;
			break;
		case SC_RRDNAME:
			rrdname = args[i].arg.c;
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
		return -1; /* MUST have UID */
	}

	LOG(LOG_DEBUG, "Register device: uid=%s name=%s rrd=%s type=%d proto=%d subtype=%d",
	    uid, (name) ? name : "NULL", (rrdname) ? rrdname : "NULL",
	    devtype, proto, subtype);

	dev = find_device_byuid(uid);
	if (dev == NULL) {
		LOG(LOG_DEBUG, "Creating new device for uid %s", uid);
		if (subtype == 0 || devtype == 0 || proto == 0 ||
		    name == NULL) {
			LOG(LOG_ERROR, "Attempt to register new device"
			    " without full specifications");
			return(-1);
		}
		if (rrdname == NULL)
			rrdname = strndup(name, 19);
		dev = smalloc(device_t);
		dev->uid = strdup(uid);
		new = 1;
	} else
		LOG(LOG_DEBUG, "Updating existing device uid:%s", uid);
	if (dev->name != NULL)
		free(dev->name);
	dev->name = strdup(name);
	if (dev->rrdname != NULL)
		free(dev->rrdname);
	if (rrdname != NULL)
		dev->rrdname = strndup(rrdname, 19);
	dev->type = devtype;
	dev->proto = proto;
	dev->subtype = subtype;
	(void)time(&dev->last_upd);

	if (new)
		insert_device(dev);

	return(0);
}

/**
	\brief Handle a register group command
	\param args The list of arguments
	\param arg void pointer to client_t of provider
*/

int cmd_register_group(pargs_t *args, void *arg)
{
	int i, new=0;
	char *uid=NULL, *name=NULL, *grouplist=NULL, *devlist=NULL;
	device_t *dev;
	char *tmpbuf, *p;
	device_group_t *devgrp, *cgrp;

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
			LOG(LOG_ERROR, "Attempt to register new device without"
			    " name");
			return(-1);
		}
		devgrp = new_devgroup(uid);
		devgrp->uid = uid;
	} else
		LOG(LOG_DEBUG, "Updating existing device group uid:%s", uid);

	devgrp->name = name;

	if (grouplist != NULL) {
		tmpbuf = grouplist;
		for (p = strtok(tmpbuf, ","); p; p = strtok(NULL, ",")) {
			cgrp = find_devgroup_byuid(p);
			if (cgrp == NULL)
				LOG(LOG_ERROR, "Cannot find child group %s "
				    "while attempting to register group %s",
				    p, uid);
			else if (!group_in_group(cgrp, devgrp)){
				add_group_group(cgrp, devgrp);
				LOG(LOG_DEBUG, "Adding child group %s to "
				    "group %s", cgrp->uid, uid);
			}
		}
		free(grouplist);
	}

	if (devlist != NULL) {
		tmpbuf = devlist;
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
		free(devlist);
	}

	return(0);
}

/**
	\brief Handle a update device command
	\param args The list of arguments
	\param arg void pointer to client_t of provider
*/

int cmd_update(pargs_t *args, void *arg)
{
	int i, j;
	device_t *dev;
	char *uid=NULL;
	char *p, *hold, *fhold;
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
		case SC_VOLUME:
		case SC_DISTANCE:
			store_data_dev(dev, DATALOC_DATA, &args[i].arg.d);
			break;
		case SC_TIMER:
		case SC_COUNT:
			store_data_dev(dev, DATALOC_DATA, &args[i].arg.u);
			break;
		case SC_WATTSEC:
		case SC_NUMBER:
			store_data_dev(dev, DATALOC_DATA, &args[i].arg.ll);
			break;
		case SC_HANDLER:
			if (dev->handler != NULL)
				free(dev->handler);
			dev->handler = strdup(args[i].arg.c);
			break;
		case SC_HARGS:
			fhold = hold = strdup(args[i].arg.c);
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
			fhold = hold = strdup(args[i].arg.c);
			for ((p = strtok(hold, ",")), j=0;
			     p && j < dev->nrofhargs;
			     (p = strtok(NULL, ",")), j++) {
				dev->hargs[j] = strdup(p);
			}
			free(fhold);
			LOG(LOG_NOTICE, "Handler args uid:%s changed to %s,"
			    " %d arguments", dev->uid, args[i].arg.c,
			    dev->nrofhargs);
			break;
		}
	}

	(void)time(&dev->last_upd);
	coll_upd_cb(dev, arg);
	return(0);
}

/**
	\brief Handle a change device command
	\param args The list of arguments
	\param arg void pointer to client_t of provider
*/

int cmd_change(pargs_t *args, void *arg)
{
	int i, state;
	uint32_t tstate;
	int64_t num;
	device_t *dev;
	char *uid=NULL;
	client_t *client = (client_t *)arg;
	double level;

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
			state = args[i].arg.i;
			if (state < 0)
				state = 0;
			if (state > 1)
				state = 1;
			if (dev->type != DEVICE_SWITCH) {
				LOG(LOG_WARNING, "Request to change switch "
				    "data on non-switch device %s", dev->uid);
				return(-1);
			}
			store_data_dev(dev, DATALOC_CHANGE, &state);
			break;
		case SC_DIMMER:
			level = args[i].arg.d;
			if (level < 0.0)
				level = 0.0;
			if (level > 1.0)
				level = 1.0;
			if (dev->type != DEVICE_DIMMER) {
				LOG(LOG_WARNING, "Request to change level "
				    "on non-dimmer device %s type=%d",
				    dev->uid, dev->type);
				return(-1);
			}
			store_data_dev(dev, DATALOC_CHANGE, &level);
			break;
		case SC_WEATHER:
		case SC_THMODE:
		case SC_THSTATE:
		case SC_SMNUMBER:
		case SC_ALARMSTATUS:
		case SC_COLLECTOR:
		case SC_BLIND:
			store_data_dev(dev, DATALOC_CHANGE, &args[i].arg.i);
			state = args[i].arg.i; /* XXX */
			break;
		case SC_LUX:
                case SC_HUMID:
                case SC_TEMP:
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
			store_data_dev(dev, DATALOC_CHANGE, &args[i].arg.d);
			break;
		case SC_COUNT:
		case SC_TIMER:
			store_data_dev(dev, DATALOC_CHANGE, &args[i].arg.u);
			tstate = args[i].arg.u; /* XXX */
			break;
		case SC_WATTSEC:
		case SC_NUMBER:
			store_data_dev(dev, DATALOC_CHANGE, &args[i].arg.ll);
			num = args[i].arg.ll; /* XXX */
			break;
		case SC_UID:
			break;
		default:
			LOG(LOG_WARNING, "Got an unhandled device type/subtype"
			    " in cmd_change() for device %s", dev->uid);
			return(-1);
			break;
		}
	}
	coll_chg_cb(dev, arg); /* new generic cb routine */
	return(0);
}

/**
   \brief Handle a modify device command
   \param args The list of arguments
   \param arg void pointer to client_t of provider
   We modify the device internally, and rewrite the conf
*/

int cmd_modify(pargs_t *args, void *arg)
{
	int i, j;
	double d;
	uint32_t u;
	device_t *dev;
	char *uid=NULL, *p, *hold;
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
		LOG(LOG_ERROR, "modify without UID");
		return(-1);
	}
	dev = find_device_byuid(uid);
	if (!dev) {
		LOG(LOG_ERROR, "UID:%s doesn't exist", uid);
		return(-1);
	}

	for (i=0; args[i].cword != -1; i++) {
		switch (args[i].cword) {
		case SC_NAME:
			free(dev->name);
			dev->name = strdup(args[i].arg.c);
			LOG(LOG_NOTICE, "Changing uid:%s name to %s",
			    dev->uid, dev->name);
			break;
		case SC_RRDNAME:
			free(dev->rrdname);
			dev->rrdname = strdup(args[i].arg.c);
			LOG(LOG_NOTICE, "Changing uid:%s rrdname to %s",
			    dev->uid, dev->rrdname);
			break;
		}
	}
	/* force a device conf rewrite */
	dump_conf(cfg, 0, conffile);
	return(0);
}

/**
   \brief A read callback, got data from server
   \param in The bufferevent that fired
   \param arg optional arg
*/

void gnhastd_read_cb(struct bufferevent *in, void *arg)
{
	char *data;
	char **words, *cmdword;
	int numwords, i;
	pargs_t *args=NULL;
	struct evbuffer *evbuf;
	size_t len;

	/* loop as long as we have data to read */
	while (1) {
		evbuf = bufferevent_get_input(in);
		data = evbuffer_readln(evbuf, &len, EVBUFFER_EOL_CRLF);

		if (data == NULL || len < 1)
			return;

		LOG(LOG_DEBUG, "Got data %s", data);

		words = parse_netcommand(data, &numwords);

		if (words == NULL || words[0] == NULL) {
			free(data);
			goto out;
		}

		cmdword = strdup(words[0]);
		args = parse_command(words, numwords);
		parsed_command(cmdword, args, arg);
		free(cmdword);
		free(data);
		if (args) {
			for (i=0; args[i].cword != -1; i++)
				if (args[i].type == PTCHAR)
					free(args[i].arg.c);
			free(args);
			args=NULL;
		}
	}

out:
	if (args) {
		for (i=0; args[i].cword != -1; i++)
			if (args[i].type == PTCHAR)
				free(args[i].arg.c);
		free(args);
		args=NULL;
	}
}
