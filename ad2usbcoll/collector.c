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
   \file collector.c
   \author Tim Rightnour
   \brief NUTech AD2USB collector
*/

#include <termios.h>
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

#include "common.h"
#include "gnhast.h"
#include "confuse.h"
#include "confparser.h"
#include "collcmd.h"
#include "gncoll.h"
#include "ad2usb.h"

void connect_event_cb(struct bufferevent *ev, short what, void *arg);
void connect_server_cb(int nada, short what, void *arg);

char *conffile = SYSCONFDIR "/" AD2USBCOLL_CONFIG_FILE;
FILE *logfile;   /** our logfile */
extern int debugmode;
cfg_t *cfg, *gnhastd_c, *ad2usb_c;
char *dumpconf = NULL;
int need_rereg = 0;
int first_update = 0;
int gotdata = 0;
char *uidprefix = "ad2usb";
int *zonefaults;

/** Need the argtable in scope, so we can generate proper commands
    for the server */
extern argtable_t argtable[];
extern TAILQ_HEAD(, _device_t) alldevs;
extern const char *alarmtext[];

/** The event base */
struct event_base *base;
struct evdns_base *dns_base;

char *extratype[4] = {
	"none",
	"exp",
	"rel",
};

char *conntype[5] = {
	"none",
	"ad2usb",
	"gnhastd",
};

typedef struct _connection_t {
	int port;
	int type;
	char *host;
	struct bufferevent *bev;
	int shutdown;
} connection_t;

/** The connection streams for our two connections */
connection_t *gnhastd_conn, *ad2usb_conn;

/* Configuration file setup */

extern cfg_opt_t device_opts[];

cfg_opt_t ad2usb_opts[] = {
	CFG_STR("serialdev", 0, CFGF_NODEFAULT),
	CFG_INT("zones", 8, CFGF_NONE),
	CFG_INT_CB("logfaults", 0, CFGF_NONE, conf_parse_bool),
	CFG_INT("seccode", 0, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t gnhastd_opts[] = {
	CFG_STR("hostname", "127.0.0.1", CFGF_NONE),
	CFG_INT("port", 2920, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t options[] = {
	CFG_SEC("ad2usb", ad2usb_opts, CFGF_NONE),
	CFG_SEC("gnhastd", gnhastd_opts, CFGF_NONE),
	CFG_SEC("device", device_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_STR("logfile", AD2USBCOLL_LOG_FILE, CFGF_NONE),
	CFG_STR("pidfile", AD2USBCOLL_PID_FILE, CFGF_NONE),
	CFG_END(),
};


/**********************************************
	gnhastd handlers
**********************************************/

/**
   \brief Called when an upd command occurs
   \param dev device that got updated
   \param arg pointer to client_t
*/

void coll_upd_cb(device_t *dev, void *arg)
{
	return;
}

/**
   \brief Handle a enldevs device command
   \param args The list of arguments
   \param arg void pointer to client_t of provider
*/

int cmd_endldevs(pargs_t *args, void *arg)
{
	return 0;
}

/**
   \brief Called when a switch chg command occurs
   \param dev device that got updated
   \param state new state (on/off)
   \param arg pointer to client_t
*/

void coll_chg_cb(device_t *dev, void *arg)
{
	client_t *cli = (client_t *)arg;
	char buf[256];
	int code, button=0;
	uint8_t state;
	struct evbuffer *send;

	get_data_dev(dev, DATALOC_CHANGE, &state);

	code = cfg_getint(ad2usb_c, "seccode");
	if (code == 0) {
		LOG(LOG_NOTICE, "No seccode in cfg file, cannot change "
		    "alarm state");
		return;
	}

	sprintf(buf, "%s-%s", uidprefix, "status");
	if (strcmp(buf, dev->uid) != 0) {
		LOG(LOG_ERROR, "Can only change alarm state, ignoring change");
		return;
	}
	switch (state) {
	case ALARM_READY: button = 1; break;
	case ALARM_STAY: button = 3; break;
	case ALARM_NIGHTSTAY: button = 33; break;
	case ALARM_INSTANTMAX: button = 7; break;
	case ALARM_AWAY: button = 2; break;
	case ALARM_FAULT:
	default:
		LOG(LOG_ERROR, "Request to change alarmstate to invalid "
		    "state: %d", state);
		return;
		break;
	}
	if (button < 1)
		return;
	send = evbuffer_new();
	evbuffer_add_printf(send, "%0.4d%d", code, button);
	bufferevent_write_buffer(ad2usb_conn->bev, send);
	evbuffer_free(send);	
}

/*****
      General stuff
*****/

/**
   \brief Create a new integer dev with subtype, value, and uid suffix
   \param subtype SUBTYPE_*
   \param val current reading
   \param suffix a uid suffix
   \return device_t of new dev
*/

device_t *new_int_dev(int subtype, int val, char *suffix)
{
	char buf[256];
	device_t *dev;

	sprintf(buf, "%s-%s", uidprefix, suffix);
	dev = new_dev_from_conf(cfg, buf);
	if (dev == NULL) {
		dev = smalloc(device_t);
		dev->uid = strdup(buf);
		dev->loc = strdup(buf);
		dev->type = DEVICE_SENSOR;
		dev->proto = PROTO_SENSOR_AD2USB;
		dev->subtype = subtype;
		(void)new_conf_from_dev(cfg, dev);
	}
	insert_device(dev);
	store_data_dev(dev, DATALOC_DATA, &val);
	if (dumpconf == NULL && dev->name != NULL && gnhastd_conn != NULL) {
		gn_register_device(dev, gnhastd_conn->bev);
		gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
	}
	return dev;
}

/**
   \brief Maybe we log a status change?
   \param dev dev that fired
   \param val new value
*/

void maybe_log_status(device_t *dev, int val)
{
	if (!cfg_getint(ad2usb_c, "logfaults"))
		return;
	if (strstr(dev->uid, "lowbat") != NULL) {
		LOG(LOG_NOTICE, "uid:%s %s %s", dev->uid, dev->name,
		    val ? "Failure" : "OK");
		return;
	}
	if (strstr(dev->uid, "acpower") != NULL) {
		LOG(LOG_NOTICE, "uid:%s %s %s", dev->uid, dev->name,
		    val ? "On" : "Off");
		return;
	}
	if (dev->subtype == SUBTYPE_SWITCH)
		LOG(LOG_NOTICE, "uid:%s %s %s", dev->uid, dev->name,
		    val ? "faulted" : "clear");
	if (dev->subtype == SUBTYPE_ALARMSTATUS)
		LOG(LOG_NOTICE, "%s status changed to %s", dev->name,
		    alarmtext[val]);
}


/**
   \brief Update a integer device
   \param val current reading
   \param suffix a uid suffix
   \param send send the update
*/

void update_int_dev(int val, char *suffix, int send)
{
	char buf[256];
	device_t *dev;
	uint8_t prevval;

	sprintf(buf, "%s-%s", uidprefix, suffix);
	dev = find_device_byuid(buf);
	if (dev == NULL)
		LOG(LOG_FATAL, "Cannot find dev %s", buf);
	get_data_dev(dev, DATALOC_DATA, &prevval);
	store_data_dev(dev, DATALOC_DATA, &val);
	if (dev->name && (prevval != val || first_update) && send) {
		LOG(LOG_DEBUG, "updating %s with status %d", dev->uid, val);
		gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
		maybe_log_status(dev, val);
	}
}


/**
   \brief create an initial conf file
   \param zones number of zones
*/

void maybe_dump_conf(int zones)
{
	int i;
	char buf[16], buf2[256];
	device_t *dev;

	for (i=0; i < zones; i++) {
		sprintf(buf, "zone%d", i+1);
		sprintf(buf2, "%s-%s", uidprefix, buf);
		dev = find_device_byuid(buf2);
		if (dev == NULL)
			dev = new_int_dev(SUBTYPE_SWITCH, 0, buf);
	}
	sprintf(buf, "status");
	sprintf(buf2, "%s-%s", uidprefix, buf);
	dev = find_device_byuid(buf2);
	if (dev == NULL)
		dev = new_int_dev(SUBTYPE_ALARMSTATUS, 0, buf);

	sprintf(buf, "lowbat");
	sprintf(buf2, "%s-%s", uidprefix, buf);
	dev = find_device_byuid(buf2);
	if (dev == NULL)
		dev = new_int_dev(SUBTYPE_SWITCH, 0, buf);

	sprintf(buf, "acpower");
	sprintf(buf2, "%s-%s", uidprefix, buf);
	dev = find_device_byuid(buf2);
	if (dev == NULL)
		dev = new_int_dev(SUBTYPE_SWITCH, 1, buf);

	sprintf(buf, "fire");
	sprintf(buf2, "%s-%s", uidprefix, buf);
	dev = find_device_byuid(buf2);
	if (dev == NULL)
		dev = new_int_dev(SUBTYPE_SWITCH, 1, buf);

	if (dumpconf != NULL) {
		LOG(LOG_NOTICE, "Dumping config file to %s and exiting",
		    dumpconf);
		dump_conf(cfg, 0, dumpconf);
		exit(0);
	}
}

/*****
      AD2USB Functions
*****/


/**
   \brief deal with an exp/rel output message
   \param data the data buffer
   \param len len of data
*/

void ad2usb_handle_exp(char *data, size_t len)
{
	int i, val, addr, input, type;
	char *p, *tmp, buf[32], buf2[256];
	device_t *dev;

	if (len < 10) {
		LOG(LOG_ERROR, "Malformed EXP/REL msg, skipping: %s", data);
		return;
	}

	p = strtok(data, ":"); /* gives us !EXP */
	if (p != NULL && strcmp(p, "!EXP"))
		type = 1;
	else if (p != NULL && strcmp(p, "!REL"))
		type = 2;

	p = strtok(NULL, ":"); /* gives us addr,input,val */
	if (p == NULL || type == 0) {
		LOG(LOG_ERROR, "Malformed EXP/REL msg, skipping: %s", data);
		return;
	}

	sscanf(p, "%d,%d,%d", &addr, &input, &val);

	sprintf(buf, "%s-%0.2d-%0.2d", extratype[type], addr, input);
	sprintf(buf2, "%s-%s", uidprefix, buf);
	dev = find_device_byuid(buf2);
	if (dev == NULL) {
		LOG(LOG_NOTICE, "No device for uid:%s, building", buf2);
		dev = new_int_dev(SUBTYPE_SWITCH, val, buf);
		LOG(LOG_NOTICE, "Rewriting config file %s", conffile);
		dump_conf(cfg, 0, conffile);
	}
	update_int_dev(val, buf, 1);
}


/**
   \brief deal with an rfx output message
   \param data the data buffer
   \param len len of data
*/

void ad2usb_handle_rfx(char *data, size_t len)
{
	int i, val, serial;
	char *p, *tmp, buf[32], buf2[256];
	device_t *dev;
	uint8_t msg, bitmask;

	if (len != 15) {
		LOG(LOG_ERROR, "Malformed RFX msg, skipping: %s", data);
		return;
	}
		
	strtok(data, ":"); /* gives us !RFX */
	p = strtok(NULL, ":"); /* gives us serial,message */
	if (p == NULL) {
		LOG(LOG_ERROR, "Malformed RFX msg, skipping: %s", data);
		return;
	}

	sscanf(p, "%d,%hhx", &serial, &msg);

	for (i=1; i < 5; i++) {
		sprintf(buf, "%0.7d-l%d", serial, i);
		sprintf(buf2, "%s-%s", uidprefix, buf);
		dev = find_device_byuid(buf2);
		if (dev == NULL) {
			LOG(LOG_WARNING, "Ignoring unknown RFX device signal"
			    " serial# %0.7d, loop %d", serial, i);
			continue;
		}
		/* wierd setup for the loops, so we case it */
		switch (i) {
		case 1: bitmask = (1<<7); break;
		case 2: bitmask = (1<<5); break;
		case 3: bitmask = (1<<4); break;
		case 4: bitmask = (1<<6); break;
		}
		if (msg & bitmask)
			update_int_dev(1, buf, 1); /* faulted */
		else
			update_int_dev(0, buf, 1);
	}
	sprintf(buf, "%0.7d-lowbat", serial);
	sprintf(buf2, "%s-%s", uidprefix, buf);
	dev = find_device_byuid(buf2);
	if (dev == NULL) {
		LOG(LOG_WARNING, "No lowbat device for RFX serial# %0.7d,"
		    " skipping", serial);
		return;
	}
	if (msg & (1<<1))
		update_int_dev(1, buf, 1); /* low battery */
	else
		update_int_dev(0, buf, 1); /* battery good */
}


/**
   \brief deal with a standard panel output message
   \param data the data buffer
   \param len len of data
*/

void ad2usb_handle_std(char *data, size_t len)
{
	struct evbuffer *send;
	int fault=0, val, zones, i, zf;
	char *p, *tmp, buf[16], buf2[256];
	device_t *dev;

	/* check for multifault so we don't clock zone 8 */
	tmp = strcasestr(data, "Hit * for faults");

	/* get section 1 */
	p = strtok(data, ",");
	if (strlen(p) < 22) {
		LOG(LOG_WARNING, "malformed section 1: %s", p);
		return;
	}
	if (p[1] == '1')
		update_int_dev(ALARM_READY, "status", 1);
	else if (p[2] == '1')
		update_int_dev(ALARM_AWAY, "status", 1);
	else if (p[3] == '1')
		update_int_dev(ALARM_STAY, "status", 1);
	else if (p[13] == '1')
		update_int_dev(ALARM_INSTANTMAX, "status", 1);
	else if (p[16] == '1')
		update_int_dev(ALARM_NIGHTSTAY, "status", 1);
	if (p[1] == '0' && p[2] == '0' && p[3] == '0' &&
	    p[13] == '0' && p[16] == '0' && tmp == NULL) {
		fault = 1;
		update_int_dev(ALARM_FAULT, "status", 1);
	}

	if (p[8] == '1')
		update_int_dev(1, "acpower", 1);
	else if (p[8] == '0')
		update_int_dev(0, "acpower", 1);

	if (p[12] == '1')
		update_int_dev(1, "lowbat", 1);
	else if (p[12] == '0')
		update_int_dev(0, "lowbat", 1);

	if (p[12] == '1')
		update_int_dev(1, "fire", 1);
	else if (p[12] == '0')
		update_int_dev(0, "fire", 1);

	/* ok, get the next section */
	p = strtok(NULL, ",");
	sscanf(p, "%d", &val);
	if (fault) {
		/* count the number of currently faulted zones */
		zones = cfg_getint(ad2usb_c, "zones");
		for (i=0, zf=0; i < zones; i++)
			if (zonefaults[i] > 0)
				zf++;

		LOG(LOG_DEBUG, "Fault status, sec2 data: %s", p);
		sprintf(buf, "zone%d", val);
		if (val == 8 && strstr(data, "FAULT ") != NULL) {
			; /* 8's are a pain, look for the word FAULT */
		} else {
			update_int_dev(1, buf, 1);
			zonefaults[val-1] = zf+3; /* 2 + zf + 1 for cur */
		}
	} else if (tmp == NULL) { /* clear all faults */
		LOG(LOG_DEBUG, "Clearing all faults");
		zones = cfg_getint(ad2usb_c, "zones");

		for (i=0; i < zones; i++) {
			zonefaults[i] = 0;
			sprintf(buf, "zone%d", i+1);
			sprintf(buf2, "%s-%s", uidprefix, buf);
			dev = find_device_byuid(buf2);
			if (dev != NULL)
				update_int_dev(0, buf, 1);
		}
	}

	/* can we even do anything sane with section 3? */
	p = strtok(NULL, ",");
	/* and now, section 4 */
	p = strtok(NULL, ",");
	tmp = strcasestr(p, "Hit * for faults");
	/* need to deal with multiple faults here */
	send = evbuffer_new();
	if (tmp != NULL) {
		evbuffer_add_printf(send, "*\n");
		bufferevent_write_buffer(ad2usb_conn->bev, send);
		evbuffer_free(send);
	}
	if (fault) { /* force it to spam us so we can find the fault clears */
		evbuffer_add_printf(send, "\n");
		bufferevent_write_buffer(ad2usb_conn->bev, send);
		evbuffer_free(send);
	}
}


/**
   \brief ad2usb read callback
   \param in the bufferevent that fired
   \param arg the connection_t
*/

void ad2usb_buf_read_cb(struct bufferevent *in, void *arg)
{
	connection_t *conn = (connection_t *)arg;
	size_t len;
	char *data, buf[16], buf2[256];
	device_t *dev;
	int i, j, zones, madeonepass=0;
	struct evbuffer *evbuf;

	LOG(LOG_DEBUG, "enter read_cb");

	/* loop as long as we have data to read */
	while (1) {
		evbuf = bufferevent_get_input(in);
		data = evbuffer_readln(evbuf, &len, EVBUFFER_EOL_CRLF);

		if (data == NULL || len < 1)
			break;

		madeonepass++;

		LOG(LOG_DEBUG, "Got data from AD2USB: %s", data);

		if (len > 2 && data[0] == '[')
			ad2usb_handle_std(data, len);
		else if (strstr(data, "!RFX:") != NULL)
			ad2usb_handle_rfx(data, len);
		else if (strstr(data, "!EXP:") != NULL ||
			 strstr(data, "!REL:") != NULL)
			ad2usb_handle_exp(data, len);

		free(data);
	}
	if (!madeonepass)
		return;
	LOG(LOG_DEBUG, "zonefault counts");

	/* decrement the zone fault counter */
	zones = cfg_getint(ad2usb_c, "zones");
	for (i=0; i < zones; i++) {
		j = zonefaults[i];
		if (zonefaults[i] > 0) {
			LOG(LOG_DEBUG, "decrement fc %d to %d", i+1, zonefaults[i]-1);
			zonefaults[i]--;
		}
		if (zonefaults[i] == 0 && j > 0) {
			sprintf(buf, "zone%d", i+1);
			sprintf(buf2, "%s-%s", uidprefix, buf);
			LOG(LOG_DEBUG, "Clearing %s from faultcounter", buf);
			dev = find_device_byuid(buf2);
			if (dev != NULL)
				update_int_dev(0, buf, 1);
		}
	}

	/* clear the first update counter */
	first_update = 0;

	return;
}

/*****
      General routines/gnhastd connection stuff
*****/


/**
   \brief A read callback, got data from server
   \param in The bufferevent that fired
   \param arg optional arg
*/

void buf_read_cb(struct bufferevent *in, void *arg)
{
	char *data;
	struct evbuffer *input;
	size_t len;

	input = bufferevent_get_input(in);
	data = evbuffer_readln(input, &len, EVBUFFER_EOL_CRLF);
	if (len) {
		printf("Got data? %s\n", data);
		free(data);
	}
}

/**
   \brief A write callback, if we need to tell server something
   \param out The bufferevent that fired
   \param arg optional argument
*/

void buf_write_cb(struct bufferevent *out, void *arg)
{
	struct evbuffer *send;

	send = evbuffer_new();
	evbuffer_add_printf(send, "test\n");
	bufferevent_write_buffer(out, send);
	evbuffer_free(send);
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
	bufferevent_enable(conn->bev, EV_READ|EV_WRITE);
	bufferevent_socket_connect_hostname(conn->bev, dns_base, AF_UNSPEC,
					    conn->host, conn->port);
	if (conn->type == CONN_TYPE_GNHASTD) {
		LOG(LOG_NOTICE, "Attempting to connect to %s @ %s:%d",
		    conntype[conn->type], conn->host, conn->port);
		if (need_rereg) {
			first_update=1;
			TAILQ_FOREACH(dev, &alldevs, next_all)
				if (dumpconf == NULL && dev->name != NULL)
					gn_register_device(dev, conn->bev);
			gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);
		}
		need_rereg = 0;
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
	// XXX bufferevent_free(owserver_conn->bev);
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
	int ch, fd, zones=0;
	char *buf;
	struct timeval secs = { 0, 0 };
	struct event *ev;

	/* process command line arguments */
	while ((ch = getopt(argc, argv, "?c:dm:u:z:")) != -1)
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
		case 'u':
			uidprefix = strdup(optarg);
			break;
		case 'z':
			zones = atoi(optarg);
			break;
		default:
		case '?':	/* you blew it */
			(void)fprintf(stderr, "usage:\n%s [-c configfile]"
				      "[-m dumpconfigfile] [-u uidprefix]"
				      "[-z zones]\n",
				      getprogname());
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
	/* Initialize the device table */
	init_devtable(cfg, 0);

	/* set for first update */
	first_update = 1;

	cfg = parse_conf(conffile);

	if (!debugmode)
		logfile = openlog(cfg_getstr(cfg, "logfile"));

	writepidfile(cfg_getstr(cfg, "pidfile"));

	/* First, parse the ad2usbcoll section */

	if (cfg) {
		ad2usb_c = cfg_getsec(cfg, "ad2usb");
		if (!ad2usb_c)
			LOG(LOG_FATAL, "Error reading config file,"
			    " ad2usb section");
	}

	/* Now, parse the details of connecting to the gnhastd server */

	if (cfg) {
		gnhastd_c = cfg_getsec(cfg, "gnhastd");
		if (!gnhastd_c)
			LOG(LOG_FATAL, "Error reading config file,"
			    " gnhastd section");
	}

	if (zones == 0)
		zones = cfg_getint(ad2usb_c, "zones");
	maybe_dump_conf(zones);
	zonefaults = safer_malloc(sizeof(int) * zones);

	gnhastd_conn = smalloc(connection_t);
	gnhastd_conn->port = cfg_getint(gnhastd_c, "port");
	gnhastd_conn->type = CONN_TYPE_GNHASTD;
	gnhastd_conn->host = cfg_getstr(gnhastd_c, "hostname");
	/* cheat, and directly call the timer callback
	   This sets up a connection to the server. */
	connect_server_cb(0, 0, gnhastd_conn);
	gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);

	/* read the serial device name from the conf file */
	if (cfg_getstr(ad2usb_c, "serialdev") == NULL)
		LOG(LOG_FATAL, "Serial device not set in conf file");

	/* Connect to the AD2USB */
	fd = serial_connect(cfg_getstr(ad2usb_c, "serialdev"), B115200,
			    CS8|CREAD|CLOCAL);

	ad2usb_conn = smalloc(connection_t);
	ad2usb_conn->bev = bufferevent_socket_new(base, fd,
						  BEV_OPT_CLOSE_ON_FREE);
	ad2usb_conn->type = CONN_TYPE_AD2USB;
	bufferevent_setcb(ad2usb_conn->bev, ad2usb_buf_read_cb,
			  NULL, serial_eventcb, ad2usb_conn);
	bufferevent_enable(ad2usb_conn->bev, EV_READ|EV_WRITE);

	/* setup signal handlers */
	ev = evsignal_new(base, SIGHUP, cb_sighup, conffile);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGTERM, cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGINT, cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGQUIT, cb_sigterm, NULL);
	event_add(ev, NULL);

	parse_devices(cfg);

	/* go forth and destroy */
	event_base_dispatch(base);
	delete_pidfile();
	closelog();
	return(0);
}
