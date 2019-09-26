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
   \file owsrvcoll/collector.c
   \author Tim Rightnour
   \brief One Wire Server collector
   This collector connects to an owserver, and relays the data to gnhastd

   NOGENCONN
   This collector does not use the generic connection routines.
*/

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <event2/dns.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>

#include "config.h"
#ifdef HAVE_BSD_STDLIB_H
#include <bsd/stdlib.h>
#endif
#include "common.h"
#include "gnhast.h"
#include "confuse.h"
#include "collcmd.h"
#include "owsrv.h"
#include "confparser.h"
#include "gncoll.h"

void connect_event_cb(struct bufferevent *ev, short what, void *arg);
void ows_connect_event_cb(struct bufferevent *ev, short what, void *arg);
void connect_server_cb(int nada, short what, void *arg);
void ows_timer_cb(int nada, short what, void *arg);

char *conffile = SYSCONFDIR "/" OWSRVCOLL_CONFIG_FILE;
FILE *logfile;   /** our logfile */
extern int debugmode;
cfg_t *cfg, *gnhastd_c, *owserver_c, *owsrvcoll_c;
uint32_t loopnr; /**< \brief the number of loops we've made */
char *dumpconf = NULL;
int need_rereg = 0;
int timer_pending = 0;
int persistent = 0;
int tempscale = 0;
time_t owsrv_lastupd;

/** Need the argtable in scope, so we can generate proper commands
    for the server */
extern argtable_t argtable[];
extern TAILQ_HEAD(, _device_t) alldevs;
extern int collector_instance;
extern struct bufferevent *gnhastd_bev;

/** The event base */
struct event_base *base;
struct evdns_base *dns_base;

#define CONN_TYPE_OWSRV		1
#define CONN_TYPE_GNHASTD	2
char *conntype[3] = {
	"none",
	"owserver",
	"gnhastd",
};

typedef struct _connection_t {
	int port;
	int type;
	int32_t owbase;
	int lastcmd;
	char *host;
	struct bufferevent *bev;
	device_t *current_dev;
	time_t lastdata;
	int shutdown;
} connection_t;

/** The connection streams for our two connections */
connection_t *gnhastd_conn, *owserver_conn;

/* Configuration file setup */

extern cfg_opt_t device_opts[];

cfg_opt_t owserver_opts[] = {
	CFG_STR("hostname", "127.0.0.1", CFGF_NONE),
	CFG_INT("port", 4304, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t gnhastd_opts[] = {
	CFG_STR("hostname", "127.0.0.1", CFGF_NONE),
	CFG_INT("port", 2920, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t owsrvcoll_opts[] = {
	CFG_INT_CB("tscale", TSCALE_F, CFGF_NONE, conf_parse_tscale),
	CFG_INT("update", 60, CFGF_NONE),
	CFG_INT("rescan", 15, CFGF_NONE),
	CFG_INT("instance", 1, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t options[] = {
	CFG_SEC("owserver", owserver_opts, CFGF_NONE),
	CFG_SEC("gnhastd", gnhastd_opts, CFGF_NONE),
	CFG_SEC("owsrvcoll", owsrvcoll_opts, CFGF_NONE),
	CFG_SEC("device", device_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_STR("logfile", OWSRVCOLL_LOG_FILE, CFGF_NONE),
	CFG_STR("pidfile", OWSRVCOLL_PID_FILE, CFGF_NONE),
	CFG_END(),
};

/*****
      OWS Functions
*****/

/**
   \brief Schedule a device read from the owserver
   \param dev the dev we wish to read
   \param conn the server connection
*/

void ows_schedule_devread(device_t *dev, connection_t *conn)
{
	struct server_msg sendmsg;
	char *buf;
	size_t sz;
	struct event *tev;
	struct timeval secs = { 0, 0};


	LOG(LOG_DEBUG, "Scheduling read for device %s", dev->uid);
schedtop:
	switch (dev->subtype) {
	case SUBTYPE_TEMP:
		sz = 13; /* 13 = /temperature + NUL */
		sz += strlen(dev->loc);
		buf = safer_malloc(sz);
		snprintf(buf, strlen(dev->loc) + 13, "%s/temperature",
			 dev->loc);
		break;
	case SUBTYPE_HUMID:
		sz = 10; /* 10 = /humidity + NUL */
		sz += strlen(dev->loc);
		if (dev->localdata != NULL) {
			sz += strlen((char *)dev->localdata) + 1; /*+1 for / */
			buf = safer_malloc(sz);
			snprintf(buf, sz, "%s/%s/humidity",
				 dev->loc, (char *)dev->localdata);
		} else {
			buf = safer_malloc(sz);
			snprintf(buf, sz, "%s/humidity", dev->loc);
		}
		break;
	case SUBTYPE_LUX:
		sz = 13; /* 13 = /illuminance + NUL */
		sz += strlen(dev->loc);
		if (dev->localdata == NULL) {
			sz += 8; /* 8 = S3-R1-A + / */
			buf = safer_malloc(sz);
			snprintf(buf, sz, "%s/S3-R1-A/illuminance", dev->loc);
		} else {
			sz += strlen((char *)dev->localdata) + 1; /*+1 for / */
			buf = safer_malloc(sz);
			snprintf(buf, sz, "%s/%s/illuminance",
				 dev->loc, (char *)dev->localdata);
		}
		break;
	case SUBTYPE_PRESSURE:
		sz = 10; /* 10 = /pressure + NUL */
		sz += strlen(dev->loc);
		if (dev->localdata == NULL) {
			sz += 8; /* 8 = B1-R1-A + / */
			buf = safer_malloc(sz);
			snprintf(buf, sz, "%s/B1-R1-A/pressure", dev->loc);
		} else {
			sz += strlen((char *)dev->localdata) + 1; /*+1 for / */
			buf = safer_malloc(sz);
			if (((char *)dev->localdata)[0] == 's') {
				/* moisture type */
				snprintf(buf, sz, "%s/moisture/%s", dev->loc,
					 (char *)dev->localdata);
			} else
				snprintf(buf, sz, "%s/%s/pressure",
					 dev->loc, (char *)dev->localdata);
		}
		break;
	case SUBTYPE_WETNESS:
		sz = 10; /* /moisture + NUL */
		sz += strlen(dev->loc);
		if (dev->localdata == NULL) {
			LOG(LOG_ERROR, "Must have multimodel info for "
			    "leaf wetness type %s", dev->uid);
			return;
		} else {
			sz += strlen((char *)dev->localdata) + 1; /*+1 for / */
			buf = safer_malloc(sz);
			snprintf(buf, sz, "%s/moisture/%s",
				 dev->loc, (char *)dev->localdata);
		}
		break;
	case SUBTYPE_COUNTER:
		sz = 1; /* 1 = NUL */
		sz += strlen(dev->loc);
		if (dev->localdata == NULL) {
			LOG(LOG_ERROR, "Must have multimodel info for counter "
			    "type %s", dev->uid);
			return;
		} else {
			sz += strlen((char *)dev->localdata) + 1; /*+1 for / */
			buf = safer_malloc(sz);
			snprintf(buf, sz, "%s/%s",
				 dev->loc, (char *)dev->localdata);
		}
		break;
	case SUBTYPE_NONE: /* skip type NONE */
		goto schedcbout;
	default:
		LOG(LOG_ERROR, "I don't know how to handle sensor type %d",
		    dev->subtype);
		goto schedcbout;
		break;
	}
	LOG(LOG_DEBUG, "Sending '%s' size=%d", buf, sz);
	sendmsg.version = 0;
	sendmsg.payload = htonl(sz);
	sendmsg.type = htonl(OWSM_READ);
	conn->lastcmd = OWSM_READ;
	conn->current_dev = dev;
	sendmsg.control_flags = htonl(conn->owbase);
	sendmsg.size = htonl(65536);
	sendmsg.offset = 0; /* unused? */
	/* connect to owserver again */
	if (!persistent)
		connect_server_cb(0, 0, conn);
	bufferevent_write(conn->bev, &sendmsg, sizeof(struct server_msg));
	bufferevent_write(conn->bev, buf, sz);
	free(buf);
	return;

/* Jump here when something goes wrong, so a rescan gets scheduled */
schedcbout:

	/* we schedule a dirall here, rather than call directly, to avoid
	   a speedloop if the last device is type NONE */
	dev = TAILQ_NEXT(dev, next_all);
	if (dev == NULL) {
		/* start over from the top */
		loopnr++;
		/* Time for a dirall ? */
		LOG(LOG_DEBUG, "Loop #%d rescan at %d l%r = %d", loopnr,
		    cfg_getint(owsrvcoll_c, "rescan"),
		    loopnr % cfg_getint(owsrvcoll_c, "rescan"));
		if (loopnr % cfg_getint(owsrvcoll_c, "rescan") == 0)
			conn->current_dev = NULL;
		else
			conn->current_dev = TAILQ_FIRST(&alldevs);
		if (!timer_pending) {
			secs.tv_sec = cfg_getint(owsrvcoll_c, "update");
			tev = evtimer_new(base, ows_timer_cb, conn);
			/* XXX this leaks, doesn't it? */
			evtimer_add(tev, &secs);
			timer_pending++;
		}
	} else {
		conn->current_dev = dev;
		goto schedtop; /* and try again */
	}
	return;
}

/**
   \brief Schedule a DIRALL command with owserver
   \param conn the connection_t
   \note sets current_dev to NULL and requests a connection
*/

void ows_schedule_dirall(connection_t *conn)
{
	char *buf;
	struct server_msg msg;

	LOG(LOG_DEBUG, "Scheduling DIRALL");
	conn->current_dev = NULL;
	buf = safer_malloc(2);
	buf[0] = '/'; buf[1] = '\0';
	msg.version = 0;
	msg.payload = htonl(strlen(buf)+1); /* +1 for the NUL */
	msg.type = htonl(OWSM_DIRALL);
	conn->lastcmd = OWSM_DIRALL;
	msg.control_flags = htonl(conn->owbase);
	msg.size = 0; /* unused */
	msg.offset = 0; /* unused? */
	if (!persistent)
		connect_server_cb(0, 0, conn);
	bufferevent_write(conn->bev, &msg, sizeof(struct server_msg));
	bufferevent_write(conn->bev, buf, strlen(buf)+1);
}

/**
   \brief handling code for a DIRALL response
   \param conn connection_t of connection
   \param buf response from owserver
*/

void ows_handle_dirall(connection_t *conn, char *buf)
{
	char *p;
	device_t *dev;

	for ((p = strtok(buf, ",")); p; (p = strtok(NULL, ","))) {
		if (p[0] == '/')
			*p++;
		dev = find_device_byuid(p);
		if (dev != NULL) {
			LOG(LOG_DEBUG, "Ignoring known device %s", p);
			continue;
			/* We saw the device, we know about it, move along */
		}
		/* if we are here, this is a new device! */
		LOG(LOG_NOTICE, "Found new owfs device %s", p);

		/*Look up name in config file */
		dev = new_dev_from_conf(cfg, p);
		if (dev == NULL) {
			dev = smalloc(device_t);
			dev->uid = strdup(p);
			dev->loc = strdup(p); /* for now, all we can do */
			dev->type = DEVICE_SENSOR;
			dev->proto = PROTO_SENSOR_OWFS;
			if (strncmp(p, "10.", 3) == 0 ||
			    strncmp(p, "28.", 3) == 0) {
				dev->subtype = SUBTYPE_TEMP;
				dev->scale = tempscale;
			}
			if (strncmp(p, "1D.", 3) == 0)
				dev->subtype = SUBTYPE_NONE;
			if (strncmp(p, "EF.", 3) == 0) {
				LOG(LOG_WARNING, "OW device family EF is not "
				    "auto-determinable, needs config entry "
				    "for uid %s\n", p);
				dev->subtype = SUBTYPE_NONE;
			}
			if (strncmp(p, "26.", 3) == 0) {
				LOG(LOG_WARNING, "OW device family 26 is not "
				    "auto-determinable, needs config entry "
				    "for uid %s\n", p);
				/* set it to humid for now */
				dev->subtype = SUBTYPE_NONE;
			}
			(void)new_conf_from_dev(cfg, dev);
		}
		insert_device(dev);
       		if (dumpconf == NULL && dev->name != NULL)
			gn_register_device(dev, gnhastd_conn->bev);
	}
	if (dumpconf != NULL) {
		LOG(LOG_NOTICE, "Dumping config file to %s and exiting",
		    dumpconf);
		dump_conf(cfg, 0, dumpconf);
		exit(0);
	}
}

/**
   \brief owserver read callback
   \param in the bufferevent that fired
   \param arg the connection_t
*/

void ows_buf_read_cb(struct bufferevent *in, void *arg)
{
	connection_t *conn = (connection_t *)arg;
	struct client_msg msg;
	struct event *tev;
	struct timeval secs = { 0, 0};
	int32_t itmp;
	int datagood = 1;
	size_t len;
	char *buf;
	device_t *dev;

	len = bufferevent_read(in, &msg, sizeof(struct client_msg));
	if (len < sizeof(struct client_msg)) {
		LOG(LOG_ERROR, "Short read of client_msg from %s",
		    conntype[conn->type]);
		goto readcbout;
	}
	if (msg.ret < 0)
		LOG(LOG_ERROR, "Got bad return code from %s: %d."
		    " curuid: %s",
		    conntype[conn->type], ntohl(msg.ret),
		    conn->current_dev ? conn->current_dev->uid : "none");

	itmp = (int32_t)ntohl(msg.control_flags);
	if (itmp & OWFLAG_PERSIST)
		persistent = 1;
	LOG(LOG_DEBUG, "Control flags == 0x%X", itmp);

	itmp = (int32_t)ntohl(msg.payload);
	msg.payload = itmp;
	/* we really don't care about the rest */

	if (msg.payload == -1 && msg.ret == 0) {
		LOG(LOG_DEBUG, "Got ping");
		return; /* we got a ping packet, too slow! */
	}

	if (msg.payload < 1) {
		LOG(LOG_ERROR, "Got message with no payload from %s",
		    conntype[conn->type]);
		goto readcbout;
	}
	/* grab the data */
	buf = safer_malloc(msg.payload+1);
	len = bufferevent_read(in, buf, msg.payload);
	buf[len] = '\0'; /* add trailing NUL */
	LOG(LOG_DEBUG, "Got data len=%d payload=%d buf:%s", len, msg.payload, buf);
	if (len < msg.payload) {
		LOG(LOG_ERROR, "Short read of payload from %s. Expected %d got %d",
		    conntype[conn->type], msg.payload, len);
		goto readcbout;
	}

	conn->lastdata = time(NULL);

	/* did we issue a dirall? */
	if (conn->lastcmd == OWSM_DIRALL) {
		ows_handle_dirall(conn, buf);
		dev = TAILQ_FIRST(&alldevs);
		if (dev == NULL)
			goto readcbout;
		/* we need to schedule a READ, this will happen at the end */
		conn->current_dev = dev;
	} else if (conn->lastcmd == OWSM_READ) {
		/* I bet we got some data! */
		if (strlen(buf) < 1) {
			LOG(LOG_ERROR, "No data from an OWSM_READ");
			goto readcbout;
		}
		dev = conn->current_dev;
		if (dev == NULL) {
			LOG(LOG_ERROR, "Got null current_dev in OWSM_READ");
			goto readcbout;
		}
		switch (dev->subtype) {
		case SUBTYPE_TEMP:
			dev->data.temp = strtod(buf, (char **)NULL);
			LOG(LOG_DEBUG, "Updating uid:%s with temp:%f",
			    dev->uid, dev->data.temp);
			break;
		case SUBTYPE_HUMID:
			dev->data.humid = strtod(buf, (char **)NULL);
			LOG(LOG_DEBUG, "Updating uid:%s with humid:%f",
			    dev->uid, dev->data.humid);
			break;
		case SUBTYPE_LUX:
			/* this sensor seems to return "1" alot */
			dev->data.lux = strtod(buf, (char **)NULL);
			LOG(LOG_DEBUG, "Updating uid:%s with lux:%f buf=%s",
			    dev->uid, dev->data.lux, buf);
			break;
		case SUBTYPE_PRESSURE:
			dev->data.pressure = strtod(buf, (char **)NULL);
			LOG(LOG_DEBUG, "Updating uid:%s with pressure:%f",
			    dev->uid, dev->data.pressure);
			break;
		case SUBTYPE_COUNTER:
			dev->data.count = strtoul(buf, (char **)NULL, 10);
			LOG(LOG_DEBUG, "Updating uid:%s with count:%d",
			    dev->uid, dev->data.count);
		}
		dev->last_upd = time(NULL);
		if (dev->name && datagood)
			gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
	}
	/* if we got here, things are happy */
	owsrv_lastupd = time(NULL);

	free(buf);

/* Jump here when something goes wrong, so a rescan gets scheduled */
readcbout:
	if (conn->current_dev != NULL)
		dev = TAILQ_NEXT(conn->current_dev, next_all);
	else
		dev = NULL;
	if (dev == NULL) {
		/* start over from the top */
		loopnr++;
		/* Time for a dirall ? */
		LOG(LOG_DEBUG, "Loop #%d rescan at %d l%r = %d", loopnr,
		    cfg_getint(owsrvcoll_c, "rescan"),
		    loopnr % cfg_getint(owsrvcoll_c, "rescan"));
		if (loopnr % cfg_getint(owsrvcoll_c, "rescan") == 0)
			conn->current_dev = NULL;
		else
			conn->current_dev = TAILQ_FIRST(&alldevs);
		if (!timer_pending) {
			secs.tv_sec = cfg_getint(owsrvcoll_c, "update");
			tev = evtimer_new(base, ows_timer_cb, conn);
			/* XXX this leaks, doesn't it? */
			evtimer_add(tev, &secs);
			timer_pending++;
		}
	} else
		ows_schedule_devread(dev, conn);
	return;
}


/**
   \brief Timer callback to init a scheduled owserver read/dirall
   \param nada used for file descriptor
   \param what why did we fire?
   \param arg pointer to connection_t
*/

void ows_timer_cb(int nada, short what, void *arg)
{
	connection_t *conn = (connection_t *)arg;

	LOG(LOG_DEBUG, "Calling ows_timer_cb");
	timer_pending = 0;
	if (conn->current_dev == NULL)
		ows_schedule_dirall(conn);
	else
		ows_schedule_devread(conn->current_dev, conn);
}

/**
   \brief Timer callback to check if we have stalled on owserver
   \param nada used for file descriptor
   \param what why did we fire?
   \param arg pointer to connection_t
*/
void ows_watchdog_cb(int nada, short what, void *arg)
{
	connection_t *conn = (connection_t *)arg;
	time_t now;

	now = time(NULL);
	if ((conn->lastdata + (cfg_getint(owsrvcoll_c, "update") * 3)) < now) {
		/* we haven't fired in too long! schedule a dirall */
		LOG(LOG_NOTICE, "Watchdog detected no updates, rescheduling DIRALL, timer_pending = %d", timer_pending);
		ows_schedule_dirall(conn);
	}
}

/**
   \brief Event callback used with ows connections
   \param ev The bufferevent that fired
   \param what why did it fire?
   \param arg pointer to connection_t;
*/

void ows_connect_event_cb(struct bufferevent *ev, short what, void *arg)
{
	int err;
	connection_t *conn = (connection_t *)arg;

	if (what & BEV_EVENT_CONNECTED)
		LOG(LOG_DEBUG, "Connected to %s", conntype[conn->type]);
	else if (what & (BEV_EVENT_ERROR|BEV_EVENT_EOF)) {
		if (what & BEV_EVENT_ERROR) {
			err = bufferevent_socket_get_dns_error(ev);
			if (err)
				LOG(LOG_FATAL, "DNS Failure connecting to %s: %s",
				    conntype[conn->type], strerror(err));
		}
		//if (persistent)
			LOG(LOG_DEBUG, "Lost connection to %s, closing",
			    conntype[conn->type]);
		persistent = 0;
		bufferevent_disable(ev, EV_READ|EV_WRITE);
		bufferevent_free(ev);
	}
}

/*****
      General routines/gnhastd connection stuff
*****/

/**
   \brief Check if a collector is functioning properly
   \param conn connection_t of collector's gnhastd connection
   \return 1 if OK, 0 if broken
   \note if 5 updates pass with no data, bad bad.
*/

int collector_is_ok(void)
{
	int update;

	update = cfg_getint(owsrvcoll_c, "update");
	if ((time(NULL) - owsrv_lastupd) < (update * 5))
		return(1);
	return(0);
}

/**
   \brief A timer callback to send gnhastd imalive statements
   \param nada used for file descriptor
   \param what why did we fire?
   \param arg pointer to connection_t of gnhastd connection
*/

void health_cb(int nada, short what, void *arg)
{
	connection_t *conn = (connection_t *)arg;

	if (collector_is_ok())
		gn_imalive(conn->bev);
	else
		LOG(LOG_WARNING, "Collector is non functional");
}

/**
   \brief Error callback, close down connection
   \param ev The bufferevent that fired
   \param what why did it fire?
   \param arg set to the client structure, so we can free it out and close fd
*/

void buf_error_cb(struct bufferevent *ev, short what, void *arg)
{
	client_t *client = (client_t *)arg;

	bufferevent_disable(client->ev, EV_READ|EV_WRITE);
	bufferevent_free(client->ev);
	close(client->fd);
	free(client);
	exit(2);
}

/**
   \brief A timer callback that initiates a new connection
   \param nada used for file descriptor
   \param what why did we fire?
   \param arg pointer to connection_t
   \note also used to manually initiate a connection
*/

void connect_server_cb(int nada, short what, void *arg)
{
	connection_t *conn = (connection_t *)arg;
	device_t *dev;

	conn->bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
	if (conn->type == CONN_TYPE_GNHASTD)
		bufferevent_setcb(conn->bev, gnhastd_read_cb, NULL,
				  connect_event_cb, conn);
	else if (conn->type == CONN_TYPE_OWSRV)
		bufferevent_setcb(conn->bev, ows_buf_read_cb, NULL,
				  ows_connect_event_cb, conn);
	bufferevent_enable(conn->bev, EV_READ|EV_WRITE);
	bufferevent_socket_connect_hostname(conn->bev, dns_base, AF_UNSPEC,
					    conn->host, conn->port);
	if (conn->type == CONN_TYPE_GNHASTD) {
		LOG(LOG_NOTICE, "Attempting to connect to %s @ %s:%d",
		    conntype[conn->type], conn->host, conn->port);
		if (need_rereg) {
			TAILQ_FOREACH(dev, &alldevs, next_all)
				if (dumpconf == NULL && dev->name != NULL)
					gn_register_device(dev, conn->bev);
			gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);
		}
		need_rereg = 0;
		/* set this for the ping event */
		gnhastd_bev = conn->bev;
	}
}

/**
   \brief Event callback used with connections
   \param ev The bufferevent that fired
   \param what why did it fire?
   \param arg pointer to connection_t;
*/

void connect_event_cb(struct bufferevent *ev, short what, void *arg)
{
	int err;
	connection_t *conn = (connection_t *)arg;
	struct event *tev; /* timer event */
	struct timeval secs = { 30, 0 }; /* retry in 30 seconds */

	if (what & BEV_EVENT_CONNECTED) {
		LOG(LOG_NOTICE, "Connected to %s", conntype[conn->type]);
		if (conn->type == CONN_TYPE_GNHASTD) {
			tev = event_new(base, -1, EV_PERSIST, health_cb, conn);
			secs.tv_sec = HEALTH_CHECK_RATE;
			evtimer_add(tev, &secs);
			LOG(LOG_NOTICE, "Setting up self-health checks every "
			    "%d seconds", secs.tv_sec);
		}
	} else if (what & (BEV_EVENT_ERROR|BEV_EVENT_EOF)) {
		if (what & BEV_EVENT_ERROR) {
			err = bufferevent_socket_get_dns_error(ev);
			if (err)
				LOG(LOG_FATAL,
				    "DNS Failure connecting to %s: %s",
				    conntype[conn->type], strerror(err));
		}
		LOG(LOG_NOTICE, "Lost connection to %s, closing",
		    conntype[conn->type]);
		bufferevent_disable(ev, EV_READ|EV_WRITE);
		bufferevent_free(ev);

		if (!conn->shutdown) {
			/* we need to reconnect! */
			need_rereg = 1;
			tev = evtimer_new(base, connect_server_cb, conn);
			evtimer_add(tev, &secs); /* XXX leak? */
			LOG(LOG_NOTICE, "Attempting reconnection to "
			    "%s @ %s:%d in %d seconds",
			    conntype[conn->type], conn->host, conn->port,
			    secs.tv_sec);
		} else 
			event_base_loopexit(base, NULL);
	}
}

/**
   \brief Parse the config file for devices and load them
   \param cfg config base
*/
void parse_devices(cfg_t *cfg)
{
	device_t *dev;
	cfg_t *devconf;
	cfg_opt_t *opt;
	int i;

	for (i=0; i < cfg_size(cfg, "device"); i++) {
		devconf = cfg_getnsec(cfg, "device", i);
		dev = new_dev_from_conf(cfg, (char *)cfg_title(devconf));
		insert_device(dev);
		LOG(LOG_DEBUG, "Loaded device %s location %s from config file",
		    dev->uid, dev->loc);
		if (dumpconf == NULL && dev->name != NULL)
			gn_register_device(dev, gnhastd_conn->bev);
	}

	/* setup print functions */
	opt = cfg_getopt(owsrvcoll_c, "tscale");
	if (opt)
		cfg_opt_set_print_func(opt, conf_print_tscale);
}

/**
   \brief Shutdown timer
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void cb_shutdown(int fd, short what, void *arg)
{
	LOG(LOG_WARNING, "Clean shutdown timed out, stopping");
	event_base_loopexit(base, NULL);
}

/**
   \brief A sigterm handler
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void cb_sigterm(int fd, short what, void *arg)
{
	struct timeval secs = { 30, 0 };
	struct event *ev;

	LOG(LOG_NOTICE, "Recieved SIGTERM, shutting down");
	gnhastd_conn->shutdown = 1;
	gn_disconnect(gnhastd_conn->bev);
	bufferevent_disable(owserver_conn->bev, EV_READ|EV_WRITE);
	bufferevent_free(owserver_conn->bev);
	ev = evtimer_new(base, cb_shutdown, NULL);
	evtimer_add(ev, &secs);
}

/**
   \brief Main itself
   \param argc count
   \param arvg vector
   \return int
*/

int main(int argc, char **argv)
{
	extern char *optarg;
	extern int optind;
	int ch;
	char *buf;
	struct timeval secs = { 0, 0 };
	struct event *ev;

	/* process command line arguments */
	while ((ch = getopt(argc, argv, "?c:dm:")) != -1)
		switch (ch) {
		case 'c':	/* Set configfile */
			conffile = optarg;
			break;
		case 'd':
			debugmode = 1;
			break;
		case 'm':
			dumpconf = strdup(optarg);
			break;
		default:
		case '?':	/* you blew it */
			(void)fprintf(stderr, "usage:\n%s [-c configfile]"
				      "[-m dumpconfigfile]\n", getprogname());
			return(EXIT_FAILURE);
			/*NOTREACHED*/
			break;
		}

	if (!debugmode)
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
	loopnr = 0;
	owsrv_lastupd = time(NULL);

	cfg = parse_conf(conffile);

	if (!debugmode)
		logfile = openlog(cfg_getstr(cfg, "logfile"));

	writepidfile(cfg_getstr(cfg, "pidfile"));

	/* First, parse the owsrvcoll section */

	if (cfg) {
		owsrvcoll_c = cfg_getsec(cfg, "owsrvcoll");
		if (!owsrvcoll_c)
			LOG(LOG_FATAL, "Error reading config file, owsrvcoll section");
	}

	/* Now, parse the details of connecting to the gnhastd server */

	if (cfg) {
		gnhastd_c = cfg_getsec(cfg, "gnhastd");
		if (!gnhastd_c)
			LOG(LOG_FATAL, "Error reading config file, gnhastd section");
	}
	gnhastd_conn = smalloc(connection_t);
	gnhastd_conn->port = cfg_getint(gnhastd_c, "port");
	gnhastd_conn->type = CONN_TYPE_GNHASTD;
	gnhastd_conn->host = cfg_getstr(gnhastd_c, "hostname");
	/* cheat, and directly call the timer callback
	   This sets up a connection to the server. */
	connect_server_cb(0, 0, gnhastd_conn);
	collector_instance = cfg_getint(owsrvcoll_c, "instance");
	gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);

	/* Finally, parse how to connect to the owserver */

	if (cfg) {
		owserver_c = cfg_getsec(cfg, "owserver");
		if (!owserver_c)
			LOG(LOG_FATAL, "Error reading config file, owserver section");
	}
	owserver_conn = smalloc(connection_t);
	owserver_conn->port = cfg_getint(owserver_c, "port");
	owserver_conn->type = CONN_TYPE_OWSRV;
	owserver_conn->host = cfg_getstr(owserver_c, "hostname");
	switch (cfg_getint(owsrvcoll_c, "tscale")) {
	case TSCALE_C:
		owserver_conn->owbase = OWFLAG_TEMP_C;
		tempscale = TSCALE_C;
		break;
	case TSCALE_K:
		owserver_conn->owbase = OWFLAG_TEMP_K;
		tempscale = TSCALE_K;
		break;
	case TSCALE_R:
		owserver_conn->owbase = OWFLAG_TEMP_R;
		tempscale = TSCALE_R;
		break;
	default:
	case TSCALE_F:
		owserver_conn->owbase = OWFLAG_TEMP_F;
		tempscale = TSCALE_F;
		break;
	}
	owserver_conn->owbase |= OWFLAG_PERSIST;
	ows_schedule_dirall(owserver_conn);

	/* Schedule a watchdog timer for the owserver */

	secs.tv_sec = cfg_getint(owsrvcoll_c, "update");
	ev = event_new(base, -1, EV_PERSIST, ows_watchdog_cb, owserver_conn);
	event_add(ev, &secs);

	/* setup signal handlers */
	ev = evsignal_new(base, SIGHUP, cb_sighup, conffile);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGTERM, cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGINT, cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGQUIT, cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGUSR1, cb_sigusr1, NULL);
	event_add(ev, NULL);

	parse_devices(cfg);

	/* go forth and destroy */
	event_base_dispatch(base);

	closelog();
	delete_pidfile();
	return(0);
}
