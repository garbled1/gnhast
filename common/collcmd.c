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

extern int cmd_endldevs(pargs_t *args, void *arg);
extern void coll_upd_cb(device_t *dev, void *arg);
extern void coll_chg_switch_cb(device_t *dev, int state, void *arg);
extern void coll_chg_dimmer_cb(device_t *dev, double level, void *arg);

/** The command table */
commands_t commands[] = {
	{"chg", cmd_change, 0},
	{"reg", cmd_register, 0},
	{"endldevs", cmd_endldevs, 0},
	{"upd", cmd_update, 0},
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
		case SC_WEATHER:
			store_data_dev(dev, DATALOC_DATA, &args[i].arg.i);
			break;
		case SC_LUX:
		case SC_HUMID:
		case SC_TEMP:
		case SC_DIMMER:
		case SC_PRESSURE:
		case SC_SPEED:
		case SC_DIR:
		case SC_MOISTURE:
		case SC_WETNESS:
		case SC_VOLTAGE:
		case SC_WATT:
		case SC_AMPS:
			store_data_dev(dev, DATALOC_DATA, &args[i].arg.d);
			break;
		case SC_COUNT:
			store_data_dev(dev, DATALOC_DATA, &args[i].arg.u);
			break;
		case SC_WATTSEC:
			store_data_dev(dev, DATALOC_DATA, &args[i].arg.ll);
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
	if (dev->type == DEVICE_DIMMER)
		coll_chg_dimmer_cb(dev, level, arg);
	else if (dev->type == DEVICE_SWITCH)
		coll_chg_switch_cb(dev, state, arg);
	else {
		LOG(LOG_ERROR, "Unhandled dev type in cmd_change()");
		return(-1);
	}
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
