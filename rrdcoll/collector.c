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
   \file rrdcoll/collector.c
   \author Tim Rightnour
   \brief RRDtool collector
   This collector connects to gnhastd, and generates rrd data.
   In addition, it relays min/max/avg to the gnhastd server. (does it?)

   NOGENCONN
   This collector does not use the generic connection routines.
*/

#include "config.h"

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
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <rrd.h>

#ifdef HAVE_BSD_STDLIB_H
#include <bsd/stdlib.h>
#endif

#include "common.h"
#include "gnhast.h"
#include "confuse.h"
#include "confparser.h"
#include "gncoll.h"
#include "collcmd.h"

void connect_event_cb(struct bufferevent *ev, short what, void *arg);
void connect_server_cb(int nada, short what, void *arg);
void rrd_update_dev(device_t *dev);

FILE *logfile;   /** our logfile */
extern int debugmode;
cfg_t *cfg, *gnhastd_c, *rrdcoll_c;
uint32_t loopnr; /**< \brief the number of loops we've made */
char *dumpconf = NULL;
int need_rereg = 0;
int secure = 0;
int usecache = 0;
time_t rrd_lastupd;
char *conntype[1]; /* unused, to satisfy libgnhast only */

#define RRDCOLL_CONFIG_FILE	"rrdcoll.conf"
#define RRDCOLL_LOG_FILE	"rrdcoll.log"
#define RRDCOLL_PID_FILE	"rrdcoll.pid"
#define COLLECTOR_NAME		"rrdcoll"

char *conffile = SYSCONFDIR "/" RRDCOLL_CONFIG_FILE;

/* debugging */
//_malloc_options = "AJ";

/** Need the argtable in scope, so we can generate proper commands
    for the server */
extern argtable_t argtable[];
extern TAILQ_HEAD(, _device_t) alldevs;
extern commands_t commands[];
extern int collector_instance;
extern struct bufferevent *gnhastd_bev;

/** The event base */
struct event_base *base;
struct evdns_base *dns_base;

typedef struct _connection_t {
	int port;
	int type;
	int lastcmd;
	char *host;
	char *server;
	struct bufferevent *bev;
	device_t *current_dev;
	time_t lastdata;
	SSL_CTX *ssl_ctx;
	SSL *ssl;
	int shutdown;
} connection_t;

/** The connection streams for our two connections */
connection_t *gnhastd_conn;
connection_t *rrdc_conn;

/* Configuration file setup */

extern cfg_opt_t device_opts[];

cfg_opt_t gnhastd_opts[] = {
	CFG_STR("hostname", "127.0.0.1", CFGF_NONE),
	CFG_INT("port", 2920, CFGF_NONE),
	CFG_INT("sslport", 2921, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t rrdcoll_opts[] = {
	CFG_STR_LIST("default_rrds", 0, CFGF_NODEFAULT),
	CFG_INT_CB("userrdcached", 0, CFGF_NONE, conf_parse_bool),
	CFG_STR("rrdc_hostname", "127.0.0.1", CFGF_NONE),
	CFG_INT("rrdc_port", 42217, CFGF_NONE),
	CFG_INT("instance", 1, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t rrd_rra_opts[] = {
	CFG_FLOAT("xff", 0.5, CFGF_NONE),
	CFG_INT("steps", 0, CFGF_NODEFAULT),
	CFG_INT("rows", 0, CFGF_NODEFAULT),
};

cfg_opt_t dev_opts[] = {
	CFG_STR("file", 0, CFGF_NODEFAULT),
	CFG_STR("ds", 0, CFGF_NODEFAULT),
	CFG_STR("type", "GAUGE", CFGF_NONE),
	CFG_INT("heartbeat", 60, CFGF_NONE),
	CFG_STR_LIST("rrds", 0, CFGF_NODEFAULT),
};

cfg_opt_t options[] = {
	CFG_SEC("gnhastd", gnhastd_opts, CFGF_NONE),
	CFG_SEC("rrdcoll", rrdcoll_opts, CFGF_NONE),
	CFG_SEC("device", device_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_SEC("dev", dev_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_SEC("rrdrra", rrd_rra_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_STR("logfile", RRDCOLL_LOG_FILE, CFGF_NONE),
	CFG_STR("pidfile", RRDCOLL_PID_FILE, CFGF_NONE),
	CFG_END(),
};

/**
        \brief Find the cfg entry for an rrdev by it's UID
        \param cfg config base
        \param uid uid char *
        \return the section we found it in
*/

cfg_t *find_rrddevconf_byuid(cfg_t *cfg, char *uid)
{
        int i;
        cfg_t *section;

        for (i=0; i < cfg_size(cfg, "dev"); i++) {
                section = cfg_getnsec(cfg, "dev", i);
                if (strcmp(uid, cfg_title(section)) == 0)
                        return section;
        }
        return NULL;
}


/**
   \brief generate a new rrddev config entry from a device
   \param cfg cfg_t base
   \param dev device
   \return new cfg_t section
*/

cfg_t *new_rrdconf_from_dev(cfg_t *cfg, device_t *dev)
{
	cfg_opt_t *option;
	cfg_t *devconf;
	cfg_t *rrdcoll;
	char buf[256];
	int i, j;

	option = cfg_getopt(cfg, "dev");
	cfg_setopt(cfg, option, dev->uid);
	devconf = find_rrddevconf_byuid(cfg, dev->uid);
	sprintf(buf, "%s.rrd", dev->uid);
	cfg_setstr(devconf, "file", buf);
	if (dev->subtype == SUBTYPE_COUNTER)
		cfg_setstr(devconf, "type", "COUNTER");
	else
		cfg_setstr(devconf, "type", "GAUGE");
	cfg_setstr(devconf, "ds", dev->rrdname);
	rrdcoll = cfg_getsec(cfg, "rrdcoll");
	j = cfg_size(rrdcoll, "default_rrds");
	for (i=0; i < j; i++)
		cfg_setnstr(devconf, "rrds",
			    cfg_getnstr(rrdcoll, "default_rrds", i), i);
}

/**
   \brief Handle a endldevs device command
   \param args The list of arguments
   \param arg void pointer to client_t of provider
*/

int cmd_endldevs(pargs_t *args, void *arg)
{
	device_t *dev;

	if (dumpconf == NULL)
		return 0;

	TAILQ_FOREACH(dev, &alldevs, next_all) {
		(void)new_rrdconf_from_dev(cfg, dev);
	}
	dump_conf(cfg, 0, dumpconf);
	exit(0);
}

/**
   \brief Handle a endlgrps device command
   \param args The list of arguments
   \param arg void pointer to client_t of provider
*/

int cmd_endlgrps(pargs_t *args, void *arg)
{
    return(0);
}


/**
   \brief Called when an upd command occurs
   \param dev device that got updated
   \param arg pointer to client_t
*/

void coll_upd_cb(device_t *dev, void *arg)
{
	rrd_update_dev(dev);
}

/**
   \brief Called when a chg command occurs
   \param dev device that got updated
   \param arg pointer to client_t
*/

void coll_chg_cb(device_t *dev, void *arg)
{
	return;
}

/*****
      RRD routines
*****/


/**
   \brief get an rra def
   \param title title of reference
*/
cfg_t *rrd_get_rradef(cfg_t *cfg, char *title)
{
	cfg_t *config;
	int i;

	for (i=0; i < cfg_size(cfg, "rrdrra"); i++) {
		config = cfg_getnsec(cfg, "rrdrra", i);
		if (strcmp(title, cfg_title(config)) == 0)
			return config;
	}
	return NULL;
}

/**
   \brief generate default rra sections
   \param cfg cfg_t base
*/

void rra_default_rras(cfg_t *cfg)
{
	cfg_t *rraconf, *rrdcoll;
	cfg_opt_t *rra;

	if (cfg_size(cfg, "rrdrra") > 1)
		return;

	rra = cfg_getopt(cfg, "rrdrra");

	cfg_setopt(cfg, rra, "day_full");
	rraconf = rrd_get_rradef(cfg, "day_full");
	cfg_setfloat(rraconf, "xff", 0.5);
	cfg_setint(rraconf, "steps", 1);
	cfg_setint(rraconf, "rows", 1440);

	cfg_setopt(cfg, rra, "day_hour");
	rraconf = rrd_get_rradef(cfg, "day_hour");
	cfg_setfloat(rraconf, "xff", 0.5);
	cfg_setint(rraconf, "steps", 60);
	cfg_setint(rraconf, "rows", 24);

	cfg_setopt(cfg, rra, "week_day");
	rraconf = rrd_get_rradef(cfg, "week_day");
	cfg_setfloat(rraconf, "xff", 0.5);
	cfg_setint(rraconf, "steps", 1440);
	cfg_setint(rraconf, "rows", 7);

	cfg_setopt(cfg, rra, "week_full");
	rraconf = rrd_get_rradef(cfg, "week_full");
	cfg_setfloat(rraconf, "xff", 0.5);
	cfg_setint(rraconf, "steps", 1);
	cfg_setint(rraconf, "rows", 10080);

	cfg_setopt(cfg, rra, "week_5min");
	rraconf = rrd_get_rradef(cfg, "week_5min");
	cfg_setfloat(rraconf, "xff", 0.5);
	cfg_setint(rraconf, "steps", 5);
	cfg_setint(rraconf, "rows", 2016);

	cfg_setopt(cfg, rra, "month_30min");
	rraconf = rrd_get_rradef(cfg, "month_30min");
	cfg_setfloat(rraconf, "xff", 0.5);
	cfg_setint(rraconf, "steps", 30);
	cfg_setint(rraconf, "rows", 1488);

	cfg_setopt(cfg, rra, "year_1hour");
	rraconf = rrd_get_rradef(cfg, "year_1hour");
	cfg_setfloat(rraconf, "xff", 0.5);
	cfg_setint(rraconf, "steps", 60);
	cfg_setint(rraconf, "rows", 8760);

	cfg_setopt(cfg, rra, "10year_1day");
	rraconf = rrd_get_rradef(cfg, "10year_1day");
	cfg_setfloat(rraconf, "xff", 0.5);
	cfg_setint(rraconf, "steps", 1440);
	cfg_setint(rraconf, "rows", 3650);

	rrdcoll = cfg_getsec(cfg, "rrdcoll");
	cfg_setnstr(rrdcoll, "default_rrds", "day_full", 0);
	cfg_setnstr(rrdcoll, "default_rrds", "day_hour", 1);
	cfg_setnstr(rrdcoll, "default_rrds", "week_day", 2);
	cfg_setnstr(rrdcoll, "default_rrds", "week_full", 3);
	cfg_setnstr(rrdcoll, "default_rrds", "week_5min", 4);
	cfg_setnstr(rrdcoll, "default_rrds", "month_30min", 5);
	cfg_setnstr(rrdcoll, "default_rrds", "year_1hour", 6);
	cfg_setnstr(rrdcoll, "default_rrds", "10year_1day", 7);
}

/**
   \brief Update an entry in the rrd for device dev
   \param dev
*/

void rrd_update_dev(device_t *dev)
{
	char *rrdparams[4];
	char buf[64];
	cfg_t *devconf;
	uint32_t u;
	double d;
	int64_t ll;
	struct evbuffer *send;
	extern int optind, opterr;

	devconf = find_rrddevconf_byuid(cfg, dev->uid);
	rrdparams[0] = "rrdupdate";
	rrdparams[1] = cfg_getstr(devconf, "file");

	switch (datatype_dev(dev)) {
	case DATATYPE_UINT:
		get_data_dev(dev, DATALOC_DATA, &u);
		sprintf(buf, "%jd:%d", (intmax_t)dev->last_upd, u);
		break;
	case DATATYPE_DOUBLE:
		get_data_dev(dev, DATALOC_DATA, &d);
		sprintf(buf, "%jd:%f", (intmax_t)dev->last_upd, d);
		break;
	case DATATYPE_LL:
		get_data_dev(dev, DATALOC_DATA, &ll);
		sprintf(buf, "%jd:%jd", (intmax_t)dev->last_upd, ll);
		break;
	}
	rrdparams[2] = buf;
	rrdparams[3] = NULL;

	if (usecache) {
		send = evbuffer_new();
		evbuffer_add_printf(send, "UPDATE %s %s\n", rrdparams[1],
				    buf);
		bufferevent_write_buffer(rrdc_conn->bev, send);
		evbuffer_free(send);
	} else {
		optind = opterr = 0;
		rrd_clear_error();
		rrd_update(3, rrdparams);
		if (rrd_test_error())
			LOG(LOG_ERROR, "%s", rrd_get_error());
		else
			rrd_lastupd = time(NULL); /* no error is happytime */
	}
}

/**
   \brief Request a list of devices from gnhastd
   \param conn connection_t
*/

void request_devlist(connection_t *conn)
{
	struct evbuffer *send;

	send = evbuffer_new();
	/* ask for type sensor only */
	evbuffer_add_printf(send, "ldevs %s:3\n", ARGNM(SC_DEVTYPE));
	bufferevent_write_buffer(gnhastd_conn->bev, send);
	evbuffer_free(send);
}

/**
   \brief Create rrd databases, if needed
   \param cfg Configure structure
*/

void rrd_rrdcreate(cfg_t *cfg)
{
	cfg_t *devconf, *rra;
	int i, j, k, ret, nparams, hb;
	char *filename, *ds, *uid;
	char **rrdparams;
	struct stat sb;
	extern int optind, opterr;
	struct evbuffer *send;

	for (i = 0; i < cfg_size(cfg, "dev"); i++) {
		devconf = cfg_getnsec(cfg, "dev", i);
		filename = cfg_getstr(devconf, "file");
		uid = (char *)cfg_title(devconf);
		hb = cfg_getint(devconf, "heartbeat");

		/* schedule a feed with the server */
		send = evbuffer_new();
		evbuffer_add_printf(send, "feed %s:%s %s:%d\n", ARGNM(SC_UID),
				    uid, ARGNM(SC_RATE), hb);
		bufferevent_write_buffer(gnhastd_conn->bev, send);
		evbuffer_free(send);

		ret = stat(filename, &sb);
		if (ret == 0)
			continue;
		/* otherwise, build a new rrd */
	        LOG(LOG_NOTICE, "Creating rrd:%s for uid %s", filename, uid);
		ds = cfg_getstr(devconf, "ds");
		if (uid == NULL || ds == NULL) {
			LOG(LOG_ERROR, "Need ds and title for dev entry!");
			continue;
		}
		nparams = (cfg_size(devconf, "rrds") * 3) + 7;
		rrdparams = safer_malloc(sizeof(char *) * nparams);
		rrdparams[0] = "rrdcreate";
		rrdparams[1] = "--step";
		rrdparams[2] = safer_malloc(16);
		sprintf(rrdparams[2], "%d", cfg_getint(devconf, "heartbeat"));
		rrdparams[3] = filename;
		rrdparams[4] = safer_malloc(64);
		sprintf(rrdparams[4], "DS:%s:%s:%d:U:U", ds,
			cfg_getstr(devconf, "type"),
			cfg_getint(devconf, "heartbeat"));
		for (j=5, k=0; k < cfg_size(devconf, "rrds"); k++) {
			rra = rrd_get_rradef(cfg, cfg_getnstr(devconf, "rrds", k));
			rrdparams[j] = safer_malloc(64);
			rrdparams[j+1] = safer_malloc(64);
			rrdparams[j+2] = safer_malloc(64);
			sprintf(rrdparams[j], "RRA:AVERAGE:%f:%d:%d",
				cfg_getfloat(rra, "xff"),
				cfg_getint(rra, "steps"),
				cfg_getint(rra, "rows"));
			sprintf(rrdparams[j+1], "RRA:MIN:%f:%d:%d",
				cfg_getfloat(rra, "xff"),
				cfg_getint(rra, "steps"),
				cfg_getint(rra, "rows"));
			sprintf(rrdparams[j+2], "RRA:MAX:%f:%d:%d",
				cfg_getfloat(rra, "xff"),
				cfg_getint(rra, "steps"),
				cfg_getint(rra, "rows"));
			j = j+3;
		}
		rrdparams[j] = "RRA:LAST:0.5:1:1:";
		rrdparams[j+1] = NULL;
		optind = 0;
		opterr = 0;
		rrd_clear_error();
		/* WARNING, rrd_create swaps parameters around on exit!! */
		rrd_create(nparams-1, rrdparams);
		if (rrd_test_error())
			LOG(LOG_ERROR, "%s", rrd_get_error());

		free(rrdparams[2]);
		for (k=4; k < j-1; k++)
			free(rrdparams[k]);
		free(rrdparams);
	}
}

/**
   \brief A read callback, got data from server
   \param in The bufferevent that fired
   \param arg optional arg
*/

void rrdc_read_cb(struct bufferevent *in, void *arg)
{
	char *data;
	struct evbuffer *evbuf;
	size_t len;
	connection_t *conn = (connection_t *)arg;

	/* loop as long as we have data to read */
	while (1) {
		evbuf = bufferevent_get_input(in);
		data = evbuffer_readln(evbuf, &len, EVBUFFER_EOL_CRLF);

		if (data == NULL || len < 1)
			return;

		rrd_lastupd = time(NULL); /* mark as connection OK */
		LOG(LOG_DEBUG, "Got data from %s: %s", conn->server, data);
		free(data);
	}
}

/*****
      General routines/gnhastd connection stuff
*****/

/**
   \brief Check if a collector is functioning properly
   \param conn connection_t of collector's gnhastd connection
   \return 1 if OK, 0 if broken
   \note There is no heartbeat in this collector, so lets say 5 minutes?
*/

int collector_is_ok(void)
{
	int update = 60;

	if ((time(NULL) - rrd_lastupd) < (update * 5))
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

	conn->bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
	if (strcmp(conn->server, "gnhastd") == 0) {
		bufferevent_setcb(conn->bev, gnhastd_read_cb, NULL,
				  connect_event_cb, conn);
		/* set this for the ping event */
		gnhastd_bev = conn->bev;
	} else
		bufferevent_setcb(conn->bev, rrdc_read_cb, NULL,
				  connect_event_cb, conn);
	bufferevent_enable(conn->bev, EV_READ|EV_WRITE);
	bufferevent_socket_connect_hostname(conn->bev, dns_base, AF_UNSPEC,
					    conn->host, conn->port);
	LOG(LOG_NOTICE, "Attempting to connect to %s @ %s:%d",
	    conn->server, conn->host, conn->port);

	if (need_rereg) {
		rrd_rrdcreate(cfg); /* ask for feeds */
		request_devlist(conn);
		gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);
	}
}

/**
   \brief A timer callback that initiates a new SSL connection
   \param nada used for file descriptor
   \param what why did we fire?
   \param arg pointer to connection_t
   \note also used to manually initiate a connection
*/

void ssl_connect_server_cb(int nada, short what, void *arg)
{
	connection_t *conn = (connection_t *)arg;

	conn->bev = bufferevent_openssl_socket_new(base, -1, conn->ssl,
	    BUFFEREVENT_SSL_CONNECTING, BEV_OPT_CLOSE_ON_FREE);
	bufferevent_setcb(conn->bev, gnhastd_read_cb, NULL,
			  connect_event_cb, conn);
	bufferevent_enable(conn->bev, EV_READ|EV_WRITE);
	bufferevent_socket_connect_hostname(conn->bev, dns_base, AF_UNSPEC,
					    conn->host, conn->port);
	LOG(LOG_NOTICE, "Attempting to connect to %s @ %s:%d",
	    conn->server, conn->host, conn->port);

	if (need_rereg) {
		rrd_rrdcreate(cfg); /* ask for feeds */
		request_devlist(conn);
		gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);
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
		LOG(LOG_NOTICE, "Connected to %s", conn->server);
		if (strcmp(conn->server, "gnhastd") == 0) {
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
				    conn->server, strerror(err));
			err = bufferevent_get_openssl_error(ev);
			if (err)
				LOG(LOG_FATAL,
				    "SSL Error: %s", ERR_error_string(err, NULL));
		}
		LOG(LOG_NOTICE, "Lost connection to %s, closing", conn->server);
		bufferevent_disable(ev, EV_READ|EV_WRITE);
		bufferevent_free(ev);

		if (!conn->shutdown) {
			/* we need to reconnect! */
			need_rereg = 1;
			if (secure && strcmp(conn->server, "gnhastd") == 0)
				tev = evtimer_new(base, ssl_connect_server_cb,
						  conn);
			else
				tev = evtimer_new(base, connect_server_cb,
						  conn);
			evtimer_add(tev, &secs); /* XXX leaks? */
			LOG(LOG_NOTICE, "Attempting reconnection to "
			    "conn->server @ %s:%d in %d seconds",
			    conn->host, conn->port, secs.tv_sec);
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
	opt = cfg_getopt(rrdcoll_c, "userrdcached");
	if (opt)
		cfg_opt_set_print_func(opt, conf_print_bool);
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
	if (rrdc_conn && rrdc_conn->bev) {
		bufferevent_disable(rrdc_conn->bev, EV_READ|EV_WRITE);
		bufferevent_free(rrdc_conn->bev);
	}
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
	struct event *ev;

	/* process command line arguments */
	while ((ch = getopt(argc, argv, "?c:dm:s")) != -1)
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
		case 's':
			secure = 1;
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
	rrd_lastupd = time(NULL);

	/* Initialize the argtable */
	init_argcomm();
	init_commands();

	/* Initialize the device table */
	init_devtable(cfg, 0);
	loopnr = 0;

	cfg = parse_conf(conffile);

	if (cfg_getstr(cfg, "logfile") != NULL)
		logfile = openlog(cfg_getstr(cfg, "logfile"));

	writepidfile(cfg_getstr(cfg, "pidfile"));

	/* Now, parse the details of connecting to the gnhastd server */

	if (cfg) {
		gnhastd_c = cfg_getsec(cfg, "gnhastd");
		if (!gnhastd_c)
			LOG(LOG_FATAL, "Error reading config file, gnhastd section");
	}
	gnhastd_conn = smalloc(connection_t);
	if (secure)
		gnhastd_conn->port = cfg_getint(gnhastd_c, "sslport");
	else
		gnhastd_conn->port = cfg_getint(gnhastd_c, "port");

	gnhastd_conn->host = cfg_getstr(gnhastd_c, "hostname");
	gnhastd_conn->server = strdup("gnhastd");

	if (secure) {
		/* Initialize the OpenSSL library */
		SSL_load_error_strings();
		SSL_library_init();
		/* We MUST have entropy, or else there's no point to crypto. */
		if (!RAND_poll())
			return -1;
		//gnhastd_conn->ssl_ctx = SSL_CTX_new(SSLv3_method());
		//SSL_CTX_use_certificate_chain_file(gnhastd_conn->ssl_ctx,
		//    "../gnhastd/cert");
		//gnhastd_conn->ssl = SSL_new(gnhastd_conn->ssl_ctx);
	}

	rrdcoll_c = cfg_getsec(cfg, "rrdcoll");
	if (rrdcoll_c == NULL)
		LOG(LOG_FATAL, "No rrdcoll section in config file");

	/* cheat, and directly call the timer callback
	   This sets up a connection to the server. */
	if (secure)
		ssl_connect_server_cb(0, 0, gnhastd_conn);
	else
		connect_server_cb(0, 0, gnhastd_conn);
	collector_instance = cfg_getint(rrdcoll_c, "instance");
	gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);

	if (cfg_getint(rrdcoll_c, "userrdcached")) {
		usecache = 1;
		rrdc_conn = smalloc(connection_t);
		rrdc_conn->host = cfg_getstr(rrdcoll_c, "rrdc_hostname");
		rrdc_conn->port = cfg_getint(rrdcoll_c, "rrdc_port");
		rrdc_conn->server = strdup("rrdcached");
		connect_server_cb(0, 0, rrdc_conn);
	}

	parse_devices(cfg);
	rra_default_rras(cfg);

	rrd_rrdcreate(cfg);

	request_devlist(gnhastd_conn);

	/* setup signal handlers */
	ev = evsignal_new(base, SIGHUP, cb_sighup, conffile);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGTERM, cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGINT, cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGQUIT, cb_sigterm, NULL);
	event_add(ev, NULL);

	/* go forth and destroy */
	event_base_dispatch(base);

	closelog();
	cfg_free(cfg);
	evdns_base_free(dns_base, 0);
	event_base_free(base);
	delete_pidfile();
	return(0);
}
