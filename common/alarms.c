/*
 * Copyright (c) 2016
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
   \file alarms.c
   \brief Alarm handling
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

extern void coll_alarm_cb(alarm_t *alarm, char *aluid, void *arg);

TAILQ_HEAD(, _alarm_t) alarms = TAILQ_HEAD_INITIALIZER(alarms);

/* STUB */

/**
   \brief Called when an alarm command occurs (stub)
   \param alarm alarm that got updated/created
   \param arg pointer to connection_t
*/

void __attribute__((weak)) coll_alarm_cb(alarm_t *alarm, char *aluid,
					 void *arg)
{
	return;
}

/**
   \brief Search the alarmtable for an alarm maching aluid
   \param aluid alarm uid to look for
   \return alarm_t * if found, NULL if not
   \note  Slow, for now, don't see this as being a big deal
*/

alarm_t *find_alarm_by_aluid(char *aluid)
{
	alarm_t *alarm;

	TAILQ_FOREACH(alarm, &alarms, next)
		if (strcmp(aluid, alarm->aluid) == 0)
			return alarm;
	return NULL;
}

/**
   \brief Fully update an alarm
   \param aluid UID of alarm to update
   \param altext body of text for alarm
   \param alsev severity of alarm. 0 deletes alarm
   \param alchan alarm channel bitfield
   \return pointer to alarm, or NULL if nuked/bad/etc
   \note  Creates an alarm if none exists.  Updates existing if it does,
   nukes it if severity is set to zero.
*/

alarm_t *update_alarm(char *aluid, char *altext, int alsev, uint32_t alchan)
{
	alarm_t *alarm;

	alarm = find_alarm_by_aluid(aluid);

	/* possibly a clearing event for an alarm we don't have */
	if (alarm == NULL && alsev == 0)
		return NULL;

	if (alarm == NULL) { /* create a new alarm */
		if (aluid == NULL || altext == NULL) {
			LOG(LOG_WARNING,
			    "NULL aluid or altext in update_alarm");
			return NULL;
		}
		alarm = smalloc(alarm_t);
		alarm->aluid = strdup(aluid);
		alarm->alsev = alsev;
		alarm->altext = strdup(altext);
		alarm->alchan = alchan;
		TAILQ_INSERT_TAIL(&alarms, alarm, next);

		return alarm;
	}
	/* ok, we have an alarm.  do we nuke? */
	if (alsev == 0) {
		TAILQ_REMOVE(&alarms, alarm, next);
		if (alarm->aluid != NULL)
			free(alarm->aluid);
		if (alarm->altext != NULL)
			free(alarm->altext);
		free(alarm);

		return NULL;
	}
	/* guess not, let's update the text and sev */
	if (alarm->altext != NULL && altext != NULL) {
		free(alarm->altext);
		alarm->altext = strdup(altext);
	}
	if (alsev != -1)
		alarm->alsev = alsev;
	if (alchan < 1)
		SET_FLAG(alchan, ACHAN_GENERIC);
	alarm->alchan = alchan;

	return alarm;
}

/**
   \brief Handle an alarm command
   \param args The list of arguments
   \param arg void pointer to client_t of provider
*/

int cmd_alarm(pargs_t *args, void *arg)
{
	int i;
	char *aluid = NULL;
	char *altext = NULL;
	int alsev = 0;
	int alchan = 1; /* generic */
	client_t *client = (client_t *)arg;
	alarm_t *alarm;

	/* loop through the args and find the UID */
	for (i=0; args[i].cword != -1; i++) {
		switch (args[i].cword) {
		case SC_ALUID:
			aluid = args[i].arg.c;
			break;
		}
	}
	if (!aluid) {
		LOG(LOG_ERROR, "alarm without ALUID");
		return(-1);
	}

	/* Ok, we got one, now lets update it's data */

	for (i=0; args[i].cword != -1; i++) {
		switch (args[i].cword) {
		case SC_ALSEV:
			alsev = args[i].arg.i;
			break;
		case SC_ALTEXT:
			altext = args[i].arg.c;
			break;
		case SC_ALCHAN:
			alchan = args[i].arg.u;
			break;
		default:
			LOG(LOG_WARNING, "Got an unhandled argument"
			    " in cmd_alarm() for alarm %s", aluid);
			break;
		}
	}
	alarm = update_alarm(aluid, altext, alsev, alchan);
	coll_alarm_cb(alarm, aluid, arg); /* generic cb routine */
	return(0);
}
