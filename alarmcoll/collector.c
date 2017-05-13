/*
 * Copyright (c) 2013, 2014, 2017
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
   \file alarmcoll/collector.c
   \author Tim Rightnour
   \brief A collector that can monitor devices and create alarms
*/

#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <event2/dns.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>

#include "common.h"
#include "gnhast.h"
#include "confuse.h"
#include "collcmd.h"
#include "genconn.h"
#include "gncoll.h"
#include "confparser.h"
#include "collector.h"
#include "csvparser.h"
#include "alarmcoll.h"

/** our logfile */
FILE *logfile;
char *dumpconf = NULL;
char *conffile = SYSCONFDIR "/" ALARMCOLL_CONFIG_FILE;;

/* Need the argtable in scope, so we can generate proper commands
   for the server */
extern argtable_t argtable[];

/** The event base */
struct event_base *base;
struct evdns_base *dns_base;

cfg_t *cfg, *gnhastd_c, *alarmcoll_c;

#define CONN_TYPE_GNHASTD       1
char *conntype[3] = {
	"none",
	"gnhastd",
};
connection_t *gnhastd_conn;
int need_rereg = 0;
extern int debugmode;
extern int collector_instance;

watch_t *watched;
int watched_items = 0;
time_t acoll_lastupd;

/* Example options setup */

extern cfg_opt_t device_opts[];

cfg_opt_t gnhastd_opts[] = {
	CFG_STR("hostname", "127.0.0.1", CFGF_NONE),
	CFG_INT("port", 2920, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t alarmcoll_opts[] = {
	CFG_INT("instance", 1, CFGF_NONE),
	CFG_INT("update", 60, CFGF_NONE),
	CFG_STR("alarmlist", SYSCONFDIR "/alarmlist.csv", CFGF_NONE),
	CFG_END(),
};

cfg_opt_t options[] = {
	CFG_SEC("gnhastd", gnhastd_opts, CFGF_NONE),
	CFG_SEC("device", device_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_SEC("alarmcoll", alarmcoll_opts, CFGF_NONE),
	CFG_STR("logfile", ALARMCOLL_LOG_FILE, CFGF_NONE),
	CFG_STR("pidfile", ALARMCOLL_PID_FILE, CFGF_NONE),
	CFG_END(),
};

void cb_sigterm(int fd, short what, void *arg);
void cb_shutdown(int fd, short what, void *arg);


/*** Collector specific code goes here ***/


/* Gnhastd connection type routines go here */

/**
   \brief Check if a collector is functioning properly
   \param conn connection_t of collector's gnhastd connection
   \return 1 if OK, 0 if broken
   Generally you set this up with some kind of last update check using
   time(2).  Compare it against the normal update rate, and if there haven't
   been updates in say, 4-6 cycles, return 0.
*/

int collector_is_ok(void)
{
        int update;

        update = cfg_getint(alarmcoll_c, "update");
        if ((time(NULL) - acoll_lastupd) < (update * 5))
                return(1);
        return(0);
}

/**
   \brief Called when a register command occurs
   \param dev device that got updated
   \param arg pointer to client_t
*/

void coll_register_cb(device_t *dev, void *arg)
{
	int i;

	for (i=0; i < watched_items; i++) {
		if (strcmp(dev->uid, watched[i].uid) == 0) {
			dev->localdata = &watched[i];
			return;
		}
	}
	LOG(LOG_WARNING, "Device we aren't watching registered: %s", dev->uid);
}

#define TEST_DATATYPE(test, dt)				\
	switch (dt) {					\
	case DATATYPE_DOUBLE:				\
		if (data.d test watch->val.d)		\
			fired = 1;			\
		break;					\
	case DATATYPE_UINT:				\
		if (data.ui test watch->val.ui)		\
			fired = 1;			\
		break;					\
	case DATATYPE_LL:				\
		if (data.ll test watch->val.ll)		\
			fired = 1;			\
		break;					\
	}

/**
   \brief Called when an upd command occurs
   \param dev device that got updated
   \param arg pointer to client_t
*/

void coll_upd_cb(device_t *dev, void *arg)
{
	data_t data;
	watch_t *watch;
	int datatype, fired, i;
	time_t now;

	/* the only thing we talk to is gnhastd */
	acoll_lastupd = time(NULL);
	get_data_dev(dev, DATALOC_DATA, &data);
	datatype = datatype_dev(dev);
	/* XXX brutal hack until I fix datatype of switches */
	if (dev->type == DEVICE_SWITCH || dev->subtype == SUBTYPE_SWITCH ||
	    dev->subtype == SUBTYPE_OUTLET ||
	    dev->subtype == SUBTYPE_COLLECTOR ||
	    dev->subtype == SUBTYPE_SMNUMBER ||
	    dev->subtype == SUBTYPE_ALARMSTATUS ||
	    dev->subtype == SUBTYPE_DAYLIGHT ||
	    dev->subtype == SUBTYPE_WEATHER)
		data.ui = (uint32_t)data.state;

	/* iterate top to bottom because of the ordering of things like
	   and and or */
	for (i=0; i < watched_items; i++) {
		if (strcmp(watched[i].uid, dev->uid) != 0)
			continue;
		watch = &watched[i];
		fired = 0;

		/* Simple tests first */

		if (QUERY_FLAG(watch->wtype, WTYPE_GT)) {
			TEST_DATATYPE(>, datatype);
		} else if (QUERY_FLAG(watch->wtype, WTYPE_LT)) {
			TEST_DATATYPE(<, datatype);
		} else if (QUERY_FLAG(watch->wtype, WTYPE_EQ)) {
			TEST_DATATYPE(==, datatype);
		} else if (QUERY_FLAG(watch->wtype, WTYPE_LTE)) {
			TEST_DATATYPE(<=, datatype);
		} else if (QUERY_FLAG(watch->wtype, WTYPE_GTE)) {
			TEST_DATATYPE(>=, datatype);
		} else if (QUERY_FLAG(watch->wtype, WTYPE_NE)) {
			TEST_DATATYPE(!=, datatype);
		} else if (QUERY_FLAG(watch->wtype, WTYPE_JITTER)) {
			now = time(NULL);
			TEST_DATATYPE(==, datatype);
			if (fired == 0) {
				watch->val = dev->data; /* copy */
				watch->lastchg = now; /* reset */
				fired = 0; /* unset alarm */
			} else if (fired &&
				   ((now - watch->lastchg) > watch->threshold)) {
				/* we should fire */
				fired = 1;
			} else { /* same but time is OK */
				fired = 0; /* no fire */
			}
			LOG(LOG_DEBUG, "Diff=%d data=%f last=%f",
			    now-watch->lastchg, dev->data.d, watch->val.d);
		}

		/* now lets dig through the complex tests */

		if (QUERY_FLAG(watch->wtype, WTYPE_OR)&&
		    watched[watch->comparison].fired)
			fired = 1;

		if (QUERY_FLAG(watch->wtype, WTYPE_AND)) {
			if (fired && watched[watch->comparison].fired)
				fired = 1;
			else
				fired = 0;
		}

		/* The result of the handler is irrelevant */
		if (QUERY_FLAG(watch->wtype, WTYPE_HANDLER) && fired) {
			if (watch->handler != NULL) {
				LOG(LOG_NOTICE, "Firing handler %s for dev %s"
				    " aluid %s", watch->handler, dev->uid,
				    watch->aluid);
				system(watch->handler);
			}
		}

		if (fired && !watch->fired) {
			LOG(LOG_DEBUG, "Alarm should FIRE for dev %s aluid %s",
			    dev->uid, watch->aluid);
			if (watch->sev)
				gn_setalarm(gnhastd_conn->bev, watch->aluid,
					    watch->msg, watch->sev,
					    watch->channel);
			watch->fired = 1;
		} else if (watch->fired && !fired) {
			LOG(LOG_DEBUG, "Alarm should CLEAR for dev %s "
			    "aluid %s", dev->uid, watch->aluid);
			gn_setalarm(gnhastd_conn->bev, watch->aluid,
				    watch->msg, 0, watch->channel);
			watch->fired = 0;
		}
	}
}


/**
   \brief connect to gnhast and establish feeds for the monitored devices
   XXX A little stupid in that it will ask for multiples...
*/

void alarmcoll_establish_feeds(void)
{
	int i, hb, subtype;
	char *uid;
	struct evbuffer *send;

	hb = cfg_getint(alarmcoll_c, "update");

	for (i = 0; i < watched_items; i++) {
		uid = watched[i].uid;
		if (uid == NULL)
			continue;

		/* clear the alarm */
		gn_setalarm(gnhastd_conn->bev, watched[i].aluid,
			    watched[i].msg, 0, watched[i].channel);

		/* schedule a feed with the server */
		send = evbuffer_new();
		/* ask for the reg first */
		evbuffer_add_printf(send, "ldevs %s:%s\n", ARGNM(SC_UID), uid);
		evbuffer_add_printf(send, "feed %s:%s %s:%d\n",
				    ARGNM(SC_UID), uid,
				    ARGNM(SC_RATE), hb);
		evbuffer_add_printf(send, "ask %s:%s\n",
				    ARGNM(SC_UID), uid);
		bufferevent_write_buffer(gnhastd_conn->bev, send);
		evbuffer_free(send);
	}
}

/**
   \brief An strtol that lets me check for other types
   \param nptr string to convert
   \param endptr stores the addr of first invalid char
   \param base number base (10, 16, etc)
   \param maxval Maxium acceptable value
   \return a maxint
   This works like strtoll and friends, but is used with the define below
   to make it into a strtou32.  Taken from:

   http://stackoverflow.com/questions/5745352/whats-the-equivalent-of-atoi-or-strtoul-for-uint32-t-and-other-stdint-types

*/

inline unsigned long long strtoullMax(const char *nptr, char **endptr,
            int base, unsigned long long maxval) {
	unsigned long long ret = strtoll(nptr, endptr, base);
	if (ret > maxval) {
		ret = maxval;
		errno = ERANGE;
	} else {
		if (ret == ULLONG_MAX && errno == ERANGE)
			ret = maxval;
	}
	return ret;
}

#define strtou32(NPTR, ENDPTR, BASE)			\
   strtoullMax(NPTR, ENDPTR, BASE, (uint32_t)-1)

/**
   \brief Parse the watch type
   \param field The field string
   \param row the current row
   \param numfields the number of fields in the row
   \return wtype
   Try to do the best we can with insane combinations.
*/

int parse_wtype(const char *field, int row, int numfields)
{
	int i, wtype=0;
	size_t len;

	len = strlen(field);
	if (len < 1)
		return 0;

	for (i=0; i < len; i++) {
		switch (field[i]) {
		case '>':
			if (QUERY_FLAG(wtype, WTYPE_LT))
				SET_FLAG(wtype, WTYPE_NE);
			else
				SET_FLAG(wtype, WTYPE_GT);
			break;
		case '<':
			if (QUERY_FLAG(wtype, WTYPE_GT))
				SET_FLAG(wtype, WTYPE_NE);
			else
				SET_FLAG(wtype, WTYPE_LT);
			break;
		case '=':
			if (QUERY_FLAG(wtype, WTYPE_LT)) {
				CLEAR_FLAG(wtype, WTYPE_LT);
				SET_FLAG(wtype, WTYPE_LTE);
			} else if (QUERY_FLAG(wtype, WTYPE_GT)) {
				CLEAR_FLAG(wtype, WTYPE_GT);
				SET_FLAG(wtype, WTYPE_GTE);
			} else
				SET_FLAG(wtype, WTYPE_EQ);
			break;
		case 'J':
			SET_FLAG(wtype, WTYPE_JITTER);
				if (numfields < 7) {
				LOG(LOG_WARNING, "Row %d is set to jitter but"
				    " no timespan set, assuming 1 hr.", row);
				watched[row].threshold = 3600;
			}
			break;
		case '&':
			if (QUERY_FLAG(wtype, WTYPE_OR)) {
				LOG(LOG_WARNING, "Setting AND and OR is"
				    " insane. row %d", row);
				break;
			}
			if (numfields < 8)
				LOG(LOG_WARNING, "Row %d has AND but no "
				    "comparison field found. Ignoring.", row);
			else
				SET_FLAG(wtype, WTYPE_AND);
			break;
		case '|':
			if (QUERY_FLAG(wtype, WTYPE_AND)) {
				LOG(LOG_WARNING, "Setting AND and OR is"
				    " insane. row %d", row);
				break;
			}
			if (numfields < 8)
				LOG(LOG_WARNING, "Row %d has OR but no "
				    "comparison field found. Ignoring.", row);
			else
				SET_FLAG(wtype, WTYPE_OR);
			break;
		case 'H':
			if (numfields < 9)
				LOG(LOG_WARNING, "Row %d has handler but no "
				    "handler field found. Ignoring.", row);
			else
				SET_FLAG(wtype, WTYPE_HANDLER);
			break;
		default:
			LOG(LOG_WARNING, "Unhandled watch type: %s row %d",
			    field, row);
			wtype += WTYPE_GT; /* just pick a thing */
			break;
		}
	}
	return wtype;
}

/**
   \brief Simple linear search of aluids in the watchlist
   \param aluid a string to search for
   \return row #, or -1 for fail
*/

int find_row_by_aluid(const char *aluid)
{
	int i;

	for (i=0; i < watched_items; i++)
		if (strcmp(watched[i].aluid, aluid) == 0)
			return i;
	return -1;
}

/**
   \brief Parse a csv file for our info
   \param csvname name of CSV to parse
   \return 0 if file not read, 1 for success
*/

int parse_csv(char *csvname)
{
	int rows, i, crow, alchan;
	char *p;
	CsvParser *csvparser;
	CsvRow *row;

	csvparser = CsvParser_new(csvname, ",", 0);

	/* count the rows */
	rows=0;
	while ((row = CsvParser_getRow(csvparser)) ) {
		rows++;
		CsvParser_destroy_row(row);
	}
	CsvParser_destroy(csvparser);
	watched_items = rows;
	if (rows == 0)
		return 0;

	/* do it all over again */
	csvparser = CsvParser_new(csvname, ",", 0);
	watched = safer_malloc(sizeof(watch_t) * rows);
	crow=-1;
	while ((row = CsvParser_getRow(csvparser)) ) {
		const char **rowFields = CsvParser_getFields(row);

		crow++;
		if (CsvParser_getNumFields(row) < 6) {
			LOG(LOG_WARNING, "Row %d of %s has too few fields",
			    crow, csvname);
			continue;
		}
		watched[crow].uid = strdup(rowFields[0]);
		watched[crow].wtype = parse_wtype(rowFields[1], crow,
						  CsvParser_getNumFields(row));
		watched[crow].sev = atoi(rowFields[2]);
		alchan = atoi(rowFields[3]);
		if (alchan > 31 || alchan < 0) {
			LOG(LOG_DEBUG, "Alarm channel out of range: %s row %d",
				  rowFields[3], crow);
			alchan = ACHAN_GENERIC;
		} else
			SET_FLAG(watched[crow].channel, alchan);
		watched[crow].aluid = strdup(rowFields[4]);
		watched[crow].msg = strdup(rowFields[5]);
		errno = 0;
		p = (char *)rowFields[6];
		watched[crow].val.ui = strtou32(rowFields[6], &p, 10);
		if (errno == ERANGE && watched[crow].val.ui == (uint32_t)-1) {
			/* it was a int64? */
			errno = 0;
			p = (char *)rowFields[6];
			watched[crow].val.ll = strtoll(rowFields[6], &p, 10);
			LOG(LOG_DEBUG, "%s is LL", rowFields[6]);
		}
		if (errno != 0 || rowFields[6] == p || *p != '\0') {
			/* must be a double? */
			watched[crow].val.d = atof(rowFields[6]);
			LOG(LOG_DEBUG, "%s is DOUBLE", rowFields[6]);
		} else
			LOG(LOG_DEBUG, "%s is UINT", rowFields[6]);

		/* now lets look for additional fields */
		if (QUERY_FLAG(watched[crow].wtype, WTYPE_JITTER) &&
		    CsvParser_getNumFields(row) > 7) {
			p = (char *)rowFields[7];
			watched[crow].threshold = strtou32(rowFields[7],&p,10);
			watched[crow].val.ui = 0; /* we use val to record */
		}
		if ((QUERY_FLAG(watched[crow].wtype, WTYPE_AND) ||
		     QUERY_FLAG(watched[crow].wtype, WTYPE_OR)) &&
		    CsvParser_getNumFields(row) > 8) {
			watched[crow].comparison =
				find_row_by_aluid(rowFields[8]);
			if (watched[crow].comparison == -1) {
				LOG(LOG_WARNING, "Cannot find matching aluid "
				    "for %s, ignoring comparison.",
				    rowFields[8]);
				CLEAR_FLAG(watched[crow].wtype, WTYPE_AND);
				CLEAR_FLAG(watched[crow].wtype, WTYPE_OR);
			}
		}
		if (QUERY_FLAG(watched[crow].wtype, WTYPE_HANDLER) &&
		    CsvParser_getNumFields(row) > 9) {
			watched[crow].handler = strdup(rowFields[9]);
		}

		watched[crow].lastchg = time(NULL); /* set to now */
		watched[crow].fired = 0; /* set initial state */
		CsvParser_destroy_row(row);
	}
	CsvParser_destroy(csvparser);
	return 1;
}

/**
   \brief Main itself
   \param argc count
   \param arvg vector
   \return int
*/

int main(int argc, char **argv)
{
	struct event *ev;
	extern char *optarg;
	extern int optind;
	int ch, port = -1;
	char *dumpconf = NULL;
	char *gnhastdserver = NULL;
	char *csvfile;

	/* process command line arguments */
	while ((ch = getopt(argc, argv, "?c:dm:")) != -1)
		switch (ch) {
		case 'c':	/* Set configfile */
			conffile = strdup(optarg);
			break;
		case 'd':	/* debugging mode */
			debugmode = 1;
			break;
		case 'm':	/* dump the conf file */
			dumpconf = strdup(optarg);
			break;
		default:
		case '?':	/* you blew it */
			(void)fprintf(stderr, "usage:\n%s [-c configfile]"
				      "[-m dumpconffile]\n",
				      getprogname());
			return(EXIT_FAILURE);
			/*NOTREACHED*/
			break;
		}

	if (!debugmode && dumpconf == NULL)
		if (daemon(0, 0) == -1)
			LOG(LOG_FATAL, "Failed to daemonize: %s",
			    strerror(errno));

	/* Initialize the event system */
	base = event_base_new();
	dns_base = evdns_base_new(base, 1);

	/* Initialize the argtable */
	init_argcomm();
	/* Initialize the command table */
	init_commands();
	/* Initialize the device table */
	init_devtable(cfg, 0);

	cfg = parse_conf(conffile);

	if (!debugmode && dumpconf == NULL)
		logfile = openlog(cfg_getstr(cfg, "logfile"));

	writepidfile(cfg_getstr(cfg, "pidfile"));

	/* Now, parse the details of connecting to the gnhastd server */

	if (cfg) {
		gnhastd_c = cfg_getsec(cfg, "gnhastd");
		if (!gnhastd_c)
			LOG(LOG_FATAL, "Error reading config file, "
			    "gnhastd section");
	}

	/* Now parse the collector specific config settings */
	if (cfg) {
		alarmcoll_c = cfg_getsec(cfg, "alarmcoll");
		if (!alarmcoll_c)
			LOG(LOG_FATAL, "Error reading config file, "
			    "alarmcoll section");
	}
	if (dumpconf != NULL) {
		LOG(LOG_NOTICE, "Dumping config file to %s and exiting",
		    dumpconf);
		dump_conf(cfg, 0, dumpconf);
		exit(0);
	}

	csvfile = cfg_getstr(alarmcoll_c, "alarmlist");
	if (!parse_csv(csvfile))
		LOG(LOG_FATAL, "Could not open alarm list %s", csvfile);

	gnhastd_conn = smalloc(connection_t);
	gnhastd_conn->cname = strdup(COLLECTOR_NAME);
	if (port != -1)
		gnhastd_conn->port = port;
	else
		gnhastd_conn->port = cfg_getint(gnhastd_c, "port");
	if (gnhastdserver != NULL)
		gnhastd_conn->host = gnhastdserver;
	else
		gnhastd_conn->host = cfg_getstr(gnhastd_c, "hostname");
	gnhastd_conn->type = CONN_TYPE_GNHASTD;
	/* cheat, and directly call the timer callback
	   This sets up a connection to the server. */
	generic_connect_server_cb(0, 0, gnhastd_conn);
	collector_instance = cfg_getint(alarmcoll_c, "instance");
	gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);

	/* setup signal handlers */
	ev = evsignal_new(base, SIGHUP, cb_sighup, conffile);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGTERM, generic_cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGINT, generic_cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGQUIT, generic_cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGUSR1, cb_sigusr1, NULL);
	event_add(ev, NULL);

	alarmcoll_establish_feeds();

	/* go forth and destroy */
	event_base_dispatch(base);

	/* Close out the log, and bail */
	closelog();
	delete_pidfile();
	return(0);
}

