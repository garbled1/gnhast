/*
 * Copyright (c) 2013, 2017
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
	\file netparser.c
 	\brief Network parsing engine
 	\author Tim Rightnour
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <inttypes.h>
#include <ctype.h>
#include <sys/queue.h>

#include "gnhast.h"
#include "commands.h"
#include "common.h"

extern commands_t commands[];
extern size_t commands_size;

/**
   \brief The argument to type mappings
   \note SC_'s are in commands.h
*/
argtable_t argtable[] = {
	{"uid",	SC_UID, PTCHAR},
	{"name", SC_NAME, PTCHAR},
	{"rate", SC_RATE, PTINT},
	{"rrdname", SC_RRDNAME, PTCHAR},
	{"devt", SC_DEVTYPE, PTINT},
	{"proto", SC_PROTO, PTINT},
	{"subt", SC_SUBTYPE, PTINT},
	{"switch", SC_SWITCH, PTINT},
	{"dimmer", SC_DIMMER, PTDOUBLE},
	{"temp", SC_TEMP, PTDOUBLE},
	{"humid", SC_HUMID, PTDOUBLE},
	{"lux", SC_LUX, PTDOUBLE},
	{"pres", SC_PRESSURE, PTDOUBLE},
	{"speed", SC_SPEED, PTDOUBLE},
	{"dir", SC_DIR, PTDOUBLE},
	{"count", SC_COUNT, PTUINT},
	{"wet", SC_WETNESS, PTDOUBLE},
	{"ph", SC_PH, PTDOUBLE},
	{"wsec", SC_WATTSEC, PTLL},
	{"volts", SC_VOLTAGE, PTDOUBLE},
	{"watt", SC_WATT, PTDOUBLE},
	{"amps", SC_AMPS, PTDOUBLE},
	{"rain", SC_RAINRATE, PTDOUBLE},
	{"client", SC_CLIENT, PTCHAR},
	{"scale", SC_SCALE, PTINT},
	{"weather", SC_WEATHER, PTINT},
	{"hiwat", SC_HIWAT, PTDOUBLE},
	{"lowat", SC_LOWAT, PTDOUBLE},
	{"handler", SC_HANDLER, PTCHAR},
	{"hargs", SC_HARGS, PTCHAR},
	{"flow", SC_FLOWRATE, PTDOUBLE},
	{"distance", SC_DISTANCE, PTDOUBLE},
	{"alarm", SC_ALARMSTATUS, PTINT},
	{"number", SC_NUMBER, PTLL},
	{"pct", SC_PERCENTAGE, PTDOUBLE},
	{"volume", SC_VOLUME, PTDOUBLE},
	{"timer", SC_TIMER, PTUINT},
	{"thmode", SC_THMODE, PTINT},
	{"thstate", SC_THSTATE, PTINT},
	{"smnum", SC_SMNUMBER, PTINT},
	{"glist", SC_GROUPLIST, PTCHAR},
	{"dlist", SC_DEVLIST, PTCHAR},
	{"collector", SC_COLLECTOR, PTINT},
	{"blind", SC_BLIND, PTINT},
	{"alsev", SC_ALSEV, PTINT},
	{"altext", SC_ALTEXT, PTCHAR},
	{"aluid", SC_ALUID, PTCHAR},
	{"trigger", SC_TRIGGER, PTUINT},
	{"orp", SC_ORP, PTDOUBLE},
	{"salinity", SC_SALINITY, PTDOUBLE},
	{"alchan", SC_ALCHAN, PTUINT},
	{"spamhandler", SC_SPAM, PTINT},
	{"daylight", SC_DAYLIGHT, PTINT},
	{"tristate", SC_TRISTATE, PTINT},
	{"moonph", SC_MOONPH, PTDOUBLE},
};

/** \brief size of the args table */ 
const size_t args_size = sizeof(argtable) / sizeof(argtable_t);


/**
	\brief compare two commands lexographically
	\param a first command
	\param b second command
	\return see strcmp()
*/

int compare_command(const void *a, const void *b)
{
	return strcmp(((commands_t *)a)->name, ((commands_t *)b)->name);
}


/**
	\brief compare two argtable members
	\param a first member
	\param b second member
	\return see strcmp()
*/


static int compare_argtable(const void *a, const void *b)
{
	return strcmp(((argtable_t *)a)->name, ((argtable_t *)b)->name);
}

/**
   \brief find an argument by id number
   \param id ID number to find
   \return entry number in argtable
*/

int find_arg_byid(int id)
{
	int i;

	for (i=0; i < args_size; i++)
		if (argtable[i].num == id)
			return(i);
	return(-1);
}

/**
   \brief Given a device, return the appropriate argument
   \param dev device
   \return entry number in argtable
*/
int find_arg_bydev(device_t *dev)
{
	int i;

	if (dev->type == DEVICE_DIMMER)
		return find_arg_byid(SC_DIMMER);
	switch (dev->subtype) {
	case SUBTYPE_TEMP: return find_arg_byid(SC_TEMP); break;
	case SUBTYPE_HUMID: return find_arg_byid(SC_HUMID); break;
	case SUBTYPE_LUX: return find_arg_byid(SC_LUX); break;
	case SUBTYPE_COUNTER: return find_arg_byid(SC_COUNT); break;
	case SUBTYPE_PRESSURE: return find_arg_byid(SC_PRESSURE); break;
	case SUBTYPE_SPEED: return find_arg_byid(SC_SPEED); break;
	case SUBTYPE_DIR: return find_arg_byid(SC_DIR); break;
	case SUBTYPE_SWITCH: return find_arg_byid(SC_SWITCH); break;
	case SUBTYPE_WETNESS: return find_arg_byid(SC_WETNESS); break;
	case SUBTYPE_PH: return find_arg_byid(SC_PH); break;
	case SUBTYPE_VOLTAGE: return find_arg_byid(SC_VOLTAGE); break;
	case SUBTYPE_WATTSEC: return find_arg_byid(SC_WATTSEC); break;
	case SUBTYPE_WATT: return find_arg_byid(SC_WATT); break;
	case SUBTYPE_AMPS: return find_arg_byid(SC_AMPS); break;
	case SUBTYPE_RAINRATE: return find_arg_byid(SC_RAINRATE); break;
	case SUBTYPE_WEATHER: return find_arg_byid(SC_WEATHER); break;
	case SUBTYPE_ALARMSTATUS: return find_arg_byid(SC_ALARMSTATUS); break;
	case SUBTYPE_NUMBER: return find_arg_byid(SC_NUMBER); break;
	case SUBTYPE_PERCENTAGE: return find_arg_byid(SC_PERCENTAGE); break;
	case SUBTYPE_FLOWRATE: return find_arg_byid(SC_FLOWRATE); break;
	case SUBTYPE_DISTANCE: return find_arg_byid(SC_DISTANCE); break;
	case SUBTYPE_VOLUME: return find_arg_byid(SC_VOLUME); break;
	case SUBTYPE_TIMER: return find_arg_byid(SC_TIMER); break;
	case SUBTYPE_THMODE: return find_arg_byid(SC_THMODE); break;
	case SUBTYPE_THSTATE: return find_arg_byid(SC_THSTATE); break;
	case SUBTYPE_SMNUMBER: return find_arg_byid(SC_SMNUMBER); break;
	case SUBTYPE_COLLECTOR: return find_arg_byid(SC_COLLECTOR); break;
	case SUBTYPE_BLIND: return find_arg_byid(SC_BLIND); break;
	case SUBTYPE_TRIGGER: return find_arg_byid(SC_TRIGGER); break;
	case SUBTYPE_ORP: return find_arg_byid(SC_ORP); break;
	case SUBTYPE_SALINITY: return find_arg_byid(SC_SALINITY); break;
	case SUBTYPE_DAYLIGHT: return find_arg_byid(SC_DAYLIGHT); break;
	case SUBTYPE_TRISTATE: return find_arg_byid(SC_TRISTATE); break;
	case SUBTYPE_MOONPH: return find_arg_byid(SC_MOONPH); break;
	}
	return -1;
}

/**
   \brief initialize the argument table
*/
void init_argcomm(void)
{
	qsort((char *)argtable, args_size, sizeof(argtable_t), compare_argtable);
}

/**
   \brief parse a command from the network
   \param words a list of words to parse
   \param count how many words
   \return a pargs_t of arguments
   \note if this returns NULL, the command was invalid.
   \note Frees words upon exit
*/

pargs_t *parse_command(char  **words, int count)
{
	argtable_t *asp, dummy;
	char *cp, *tmp, *p;
	size_t span;
	int i, numargs, cur;
	pargs_t *args;

	if (words == NULL || words[0] == NULL) {
		for (i=0; i < count && words[i] != NULL; i++)
			free(words[i]);
		if (words != NULL)
			free(words);
		return(NULL);
	}

	/* the command is words[0], ignore it */

	numargs = count;

	args = safer_malloc(sizeof(pargs_t)*numargs);
	cur = 0;
	for (i=1; i < count && words[i] != NULL && *words[i]; i++) {
		span = strcspn(words[i], ":");
		tmp = strchr(words[i], ':');
		if (tmp == NULL)
			continue;
		tmp++;
		cp = words[i];
		cp[span] = '\0';
		for (p=cp; *p; p++) {
			*p = tolower(*p);
		}

		dummy.name = cp;
		asp = (argtable_t *)bsearch((void *)&dummy, (void *)argtable,
						args_size, sizeof(argtable_t),
						compare_argtable);
		if (asp) {
			args[cur].cword = asp->num;
			args[cur].type = asp->type;
			switch (asp->type) {
			case PTDOUBLE:
				args[cur].arg.d = strtod(tmp, (char **)NULL);
				break;
			case PTFLOAT:
				args[cur].arg.f = strtof(tmp, (char **)NULL);
				break;
			case PTCHAR:
				args[cur].arg.c = strdup(tmp);
				break;
			case PTINT:
				args[cur].arg.i = atoi(tmp);
				break;
			case PTUINT:
				args[cur].arg.u =
					strtoul(tmp, (char **)NULL, 10);
				break;
			case PTLONG:
				args[cur].arg.l =
					strtol(tmp, (char **)NULL, 10);
			case PTLL:
				args[cur].arg.ll =
					strtoll(tmp, (char **)NULL, 10);
				break;
			}
			cur++;
		} else {
			LOG(LOG_ERROR, "Invalid command recieved: %s", words[i]);
		}
	}

	args[cur].cword = -1;
	for (i=0; i < count && words[i] != NULL; i++)
		free(words[i]);
	free(words);

	return(args);
}

/**
   \brief parse a string into a command
   \param buf string to parse
   \param arg_count how many words did we find?
   \return a list of words
*/

char **parse_netcommand(char *buf, int *arg_count)
{
	char **ret_str;
	char *buf2 = 0, *buf2_head = 0;
	int i = 0;

	if (!*buf)
		return(0);

	buf2 = (char *)safer_malloc(strlen(buf)+1);
	buf2_head = buf2;
	ret_str = (char **)safer_malloc(sizeof(char *) * 2);

	for (;;) {
		if (*buf == '\"') {
			buf++;

			while (*buf && *buf != '\"') {
				if (*buf == '\\') {
					buf++;

					if (*buf == '\"')
						*buf2++ = *buf++;
					else {
						buf--;
						*buf2++ = *buf++;
					}
				} else
					*buf2++ = *buf++;
			}

			if (*buf == '\"')
				buf++;
		}

		if (*buf == ' ' || !*buf) {
			*buf2 = '\0';
			buf2 = buf2_head;
			ret_str = (char **)realloc(ret_str, sizeof(char *) * (i + 1));
			ret_str[i] = strdup(buf2);
			buf2 = buf2_head;
			i++;

			if (!*buf) {
				/* Free temporary buffer */
				free(buf2);
				*arg_count = i;
				return(ret_str);
			} else {
				buf++;
				/* skip sequential whitespace */
				while (*buf == ' ' && *buf)
					*buf++;
			}
		} else {
			*buf2++ = *buf++;
		}
	}
	LOG(LOG_ERROR, "Got to end of parse_netcommand without return");
	return NULL;
}


/**
   \brief Find an argument in the args by it's ID
   \param args arguments to search
   \param id id to look for
   \return argument number if found, -1 if not
*/

int find_arg_by_id(pargs_t *args, int id)
{
	int i;

	for (i=0; args[i].cword != -1; i++)
	if (args[i].cword == id)
		return(i);
	return(-1);
}
