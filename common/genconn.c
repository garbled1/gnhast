/*
 * Copyright (c) 2014
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
   \file genconn.c
   \brief Common generic connection handlers for collectors
   \author Tim Rightnour
   Requires a dns_base, base, need_rereg, gnhastd_conn, and conntype array
   all defined somewhere to function.
   Is only usable for single-connection to the server collectors.
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
#include "genconn.h"
#include "collcmd.h"
#include "gnhast.h"
#include "collector.h"

extern struct event_base *base;
extern struct evdns_base *dns_base;
extern char *conntype[];
extern int need_rereg;
extern connection_t *gnhastd_conn;
extern struct bufferevent *gnhastd_bev;

/*** Callbacks ***/
extern void genconn_connect_cb(int cevent, connection_t *conn);
extern int collector_is_ok();

/*** Weak Reference Stubs ***/

/**
   \brief Called when a connection event occurs (stub)
   \param cevent CEVENT saying what happened
   \param conn connection_t that something occurred on
*/

void __attribute__((weak)) genconn_connect_cb(int cevent, connection_t *conn)
{
	return;
}

/**
   \brief Check if a collector is functioning properly
   \param conn connection_t of collector's gnhastd connection
   \return 1 if OK, 0 if broken

   Each client can supply it's own OK check, and by overriding the one below,
   can perform a self-sanity check.  The generic gnhastd connection routine
   will auto-schedule it.
*/

int __attribute__((weak)) collector_is_ok(void)
{
	return(0); /* lie */
}

/**
   \brief A timer callback to send gnhastd imalive statements
   \param nada used for file descriptor
   \param what why did we fire?
   \param arg pointer to connection_t of gnhastd connection
*/

void generic_collector_health_cb(int nada, short what, void *arg)
{
	connection_t *conn = (connection_t *)arg;

	LOG(LOG_DEBUG, "Running self health check");
	if (collector_is_ok()) {
		gn_imalive(conn->bev);
		LOG(LOG_DEBUG, "I am ok");
	} else
		LOG(LOG_WARNING, "Collector is non functional");
}

/**
   \brief A timer callback that initiates a new connection
   \param nada used for file descriptor
   \param what why did we fire?
   \param arg pointer to connection_t
   \note also used to manually initiate a connection
*/

void generic_connect_server_cb(int nada, short what, void *arg)
{
	connection_t *conn = (connection_t *)arg;

	conn->bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
	bufferevent_setcb(conn->bev, gnhastd_read_cb, NULL,
			  generic_connect_event_cb, conn);

	bufferevent_enable(conn->bev, EV_READ|EV_WRITE);
	bufferevent_socket_connect_hostname(conn->bev, dns_base, AF_UNSPEC,
					    conn->host, conn->port);
	LOG(LOG_NOTICE, "Attempting to connect to %s @ %s:%d",
	     conntype[conn->type], conn->host, conn->port);

	/* set this for the ping event */
	gnhastd_bev = conn->bev;

	if (need_rereg) {
		gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);
	}
}

/**
   \brief Generic event callback used with connections
   \param ev The bufferevent that fired
   \param what why did it fire?
   \param arg pointer to connection_t;
*/

void generic_connect_event_cb(struct bufferevent *ev, short what, void *arg)
{
	int err;
	connection_t *conn = (connection_t *)arg;
	struct event *tev; /* timer event */
	struct timeval secs = { 30, 0 }; /* retry in 30 seconds */

	if (what & BEV_EVENT_CONNECTED) {
		LOG(LOG_NOTICE, "Connected to %s", conntype[conn->type]);
		conn->connected = 1;
		genconn_connect_cb(CEVENT_CONNECTED, conn);
		/* if it's a gnhastd connection, setup a health check */
		if (strcmp(conntype[conn->type], "gnhastd") == 0) {
			tev = event_new(base, -1, EV_PERSIST,
					generic_collector_health_cb, conn);
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
				    conntype[conn->type],
				    evdns_err_to_string(err));
		}
		LOG(LOG_NOTICE, "Lost connection to %s, closing",
		     conntype[conn->type]);
		bufferevent_disable(ev, EV_READ|EV_WRITE);
		bufferevent_free(ev);
		conn->connected = 0;
		genconn_connect_cb(CEVENT_DISCONNECTED, conn);

		if (!conn->shutdown) {
			/* we need to reconnect! */
			need_rereg = 1;
			tev = evtimer_new(base, generic_connect_server_cb,
					  conn);
			evtimer_add(tev, &secs); /* XXX leaks? */
			LOG(LOG_NOTICE, "Attempting reconnection to "
			    "conn->server @ %s:%d in %d seconds",
			    conn->host, conn->port, secs.tv_sec);
		} else if (conn->shutdown == 1) {
			genconn_connect_cb(CEVENT_SHUTDOWN, conn);
			event_base_loopexit(base, NULL);
		}
	}
}

/**
   \brief Shutdown timer
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void generic_cb_shutdown(int fd, short what, void *arg)
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

void generic_cb_sigterm(int fd, short what, void *arg)
{
	struct timeval secs = { 30, 0 };
	struct event *ev;

	LOG(LOG_NOTICE, "Recieved SIGTERM, shutting down");
	gnhastd_conn->shutdown = 1;
	if (gnhastd_conn->connected)
		gn_disconnect(gnhastd_conn->bev);
	ev = evtimer_new(base, generic_cb_shutdown, NULL);
	evtimer_add(ev, &secs);
}
