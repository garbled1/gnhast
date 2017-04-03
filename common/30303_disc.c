/*
 * Copyright (c) 2017
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
   \file 30303_disc.c
   \author Tim Rightnour
   \brief Routines to discover devices using UDB broadcast to 30303

   When using this API:

   Call d30303_setup().  Arguments are a mac prefix string in the form
   00-00-00, and a hostname string.  The hostname string can also be an
   IP address.

   If you give a hostname, it needs to be resolvable via DNS.

   Scenario 1:  You know the IP and the mac prefix, not hostname
   d30303_setup("00-00-00", "10.10.10.10");
     You will get back a url of http://10.10.10.10

   2: You know the mac prefix only
   d30303_setup("00-00-00", NULL);
     You will get back a url of the IP

   3: You know the hostname (hostnames tend to be in all caps)
   d30303_setup(NULL, "THING");
     You will get back a url of the hostname.

   Problems:
   Lets say you have two insteon hubs.  Both will report a hostname of
   INSTEONHUB. The discovery will not know which to give you back.  You will
   need to change the hostname of one of the devices.  (assuming default
   configuration here)

   You know that a BWG spa controller has a hostname of BWGSPA, so you pass
   that, but it's not in DNS.  You will get back an unresolvable URL.

   For the second case, pass just the mac addr, you will get back an IP.
   If you have two spas (really?) this won't work, as they will both match.

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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <event2/dns.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/util.h>

#include "common.h"
#include "gnhast.h"
#include "gncoll.h"
#include "collcmd.h"
#include "genconn.h"
#include "30303_disc.h"

int d30303_fd = -1;
int d30303_port = 0;
int d30303_count = 0;
int d30303_done = 0;
d30303_resp_t d30303_list[MAX_D30303_DEVS];
struct event *d30303_ev; /* the discovery event */
d30303_dev_t *d30303_dev; /* you can find your device here */

extern struct event_base *base;
extern void d30303_found_cb(char *url, char *hostname);

/**
   \brief Called when a d30303 device is found
   \param url url of device
   \param hostname hostname of device
*/

void __attribute__((weak)) d30303_found_cb(char *url, char *hostname)
{
        return;
}


/*****
  UDP 30303 discovery stuff
*****/

/**
   \brief End the discovery routine, and fire everything up
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void cb_end_d30303(int fd, short what, void *arg)
{
	char *hn;
	int i, hn_match, mp_match, ip_match;
	size_t hn_len, mp_len;

	LOG(LOG_NOTICE, "Discovery ended, found %d devices", d30303_count);
	event_del(d30303_ev);
	event_free(d30303_ev);
	close(d30303_fd);
	d30303_done = 1;

	if (d30303_dev->hostname == NULL && d30303_dev->mac_prefix == NULL) {
		if (d30303_count == 1) { /* ok, we do */
			LOG(LOG_NOTICE, "Only found one UDP 30303 device, "
			    "using that one");
			d30303_dev->hostname = d30303_list[0].uc_hostname;
		} else {
			LOG(LOG_ERROR, "No hostname or mac prefix, and "
			    "too many UDP 30303 devices found, giving up.");
			generic_cb_shutdown(0, 0, NULL);
			return; /*NOTREACHED*/
		}
	}

	/* now check for mac prefixes? */

	if (d30303_dev->mac_prefix != NULL)
		mp_len = strlen(d30303_dev->mac_prefix);
	for (i=0; i < MAX_D30303_DEVS; i++) {
		hn_match = mp_match = 0;
		/* they can't both be 1, because of above check */
		if (d30303_dev->mac_prefix == NULL)
			mp_match = 1;
		if (d30303_dev->hostname == NULL)
			ip_match = 1;
		if (d30303_dev->mac_prefix != NULL &&
		    d30303_list[i].macaddr != NULL &&
		    strncasecmp(d30303_dev->mac_prefix,
				d30303_list[i].macaddr, mp_len) == 0)
			mp_match = 1;
		if (d30303_dev->hostname != NULL &&
		    d30303_list[i].uc_hostname != NULL &&
		    strcasecmp(d30303_dev->hostname,
			       d30303_list[i].uc_hostname) == 0)
			hn_match = 1;
		if (d30303_dev->hostname != NULL &&
		    d30303_list[i].ipaddr != NULL &&
		    strcasecmp(d30303_dev->hostname,
			       d30303_list[i].ipaddr) == 0)
			ip_match = 1;
		if (hn_match && mp_match) {
			LOG(LOG_DEBUG, "Found 30303 discovery record %d "
			    "matches method hp&mp.", i);
			hn_len = strlen(d30303_list[i].uc_hostname);
			/* alloc for hn + http://hn\0 */
			d30303_dev->url = safer_malloc(hn_len + 8);
			sprintf(d30303_dev->url, "http://%s",
				d30303_list[i].uc_hostname);
			break;
		} else if (ip_match && mp_match) {
			LOG(LOG_DEBUG, "Found 30303 discovery record %d "
			    "matches method ip&mp.", i);
			hn_len = strlen(d30303_list[i].ipaddr);
			d30303_dev->url = safer_malloc(hn_len + 9);
			sprintf(d30303_dev->url, "http://%s",
				d30303_list[i].ipaddr);
			break;
		}
	}

	if (d30303_dev->url == NULL) {
		LOG(LOG_ERROR, "Couldn't find a matching controller, punt");
		generic_cb_shutdown(0, 0, NULL);
		return; /*NOTREACHED*/
	}
	LOG(LOG_NOTICE, "Set connect URL to %s", d30303_dev->url);
	d30303_found_cb(d30303_dev->url, d30303_dev->hostname);
}

/**
   \brief bind udp to listen to 30303 discovery events
   \return fd
*/
int bind_d30303_recv(void)
{
	int sock_fd, f, flag=1;
	struct sockaddr_in sin;
	socklen_t slen;

	if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		LOG(LOG_ERROR, "Failed to bind discovery in socket()");
		return -1;
	}

	if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &flag,
		       sizeof(int)) < 0) {
		LOG(LOG_ERROR, "Could not set SO_REUSEADDR for discovery");
		return -1;
	}
	if (setsockopt(sock_fd, SOL_SOCKET, SO_BROADCAST, &flag,
		       sizeof(int)) < 0) {
		LOG(LOG_ERROR, "Could not set SO_BROADCAST for discovery");
		return -1;
	}
	if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEPORT, &flag,
		       sizeof(int)) < 0) {
		LOG(LOG_ERROR, "Could not set SO_REUSEPORT for discovery");
		return -1;
	}

	/* make it non-blocking */
	f = fcntl(sock_fd, F_GETFL);
	f |= O_NONBLOCK;
	fcntl(sock_fd, F_SETFL, f);

	/* Set IP, port */
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = 0; /* take any available port */

	if (bind(sock_fd, (struct sockaddr *)&sin,
		 sizeof(struct sockaddr)) < 0) {
		LOG(LOG_ERROR, "bind() for discovery port failed");
		return -1;
	} else {
		slen = sizeof(sin);
		getsockname(sock_fd, (struct sockaddr *)&sin, &slen);
		d30303_port = ntohs(sin.sin_port);
		LOG(LOG_NOTICE,
		    "Listening on port %d:udp for discovery events",
		    d30303_port);
	}

	return sock_fd;
}

/**
   \brief Watch for discovery messages
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void cb_d30303_read(int fd, short what, void *arg)
{
	char buf[2048], buf2[8];
	char *p, *r;
	int len, size;
	struct sockaddr_in cli_addr;

	size = sizeof(struct sockaddr);
	bzero(buf, sizeof(buf));
	len = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&cli_addr,
		       &size);

	LOG(LOG_DEBUG, "got %d bytes on udp:%d", len, d30303_port);
	d30303_count++;

	buf[len] = '\0';
	LOG(LOG_DEBUG, "Response #%d from: %s", d30303_count,
	    inet_ntoa(cli_addr.sin_addr));
	LOG(LOG_DEBUG, "%s", buf);

	if (d30303_count >= MAX_D30303_DEVS) {
		LOG(LOG_ERROR, "Too many UDP 30303 devices on your network!");
		LOG(LOG_ERROR, "Recompile and change MAX_D30303_DEVS!");
		return;
	}

	p = strchr(buf, '\n');
	if (p == NULL)
		return;

	*p = '\0';
	/* find the first space, that's the end of the hostname */
	r = strchr(buf, ' ');
	if (r != NULL)
		*r = '\0';

	d30303_list[d30303_count-1].ipaddr =
		strdup(inet_ntoa(cli_addr.sin_addr));
	d30303_list[d30303_count-1].sin_addr = cli_addr.sin_addr;
	d30303_list[d30303_count-1].uc_hostname = strdup(buf);
	*p++;
	/* now find the \r, and fix it */
	r = strchr(p, '\r');
	if (r != NULL)
		*r = '\0';
	d30303_list[d30303_count-1].macaddr = strdup(p);

	LOG(LOG_NOTICE, "Found DEV#%d named %s at %s macaddr %s",
	    d30303_count, d30303_list[d30303_count-1].uc_hostname,
	    d30303_list[d30303_count-1].ipaddr,
	    d30303_list[d30303_count-1].macaddr);
}

/**
   \brief Send a UDP 30303 discovery string
   \param fd the search fd
   \param what what happened?
   \param arg unused
*/

void cb_d30303_send(int fd, short what, void *arg)
{
	char *send = D30303_DSTRING;
	int len;
	struct sockaddr_in broadcast_addr;

	memset(&broadcast_addr, 0, sizeof(broadcast_addr));
	broadcast_addr.sin_family = AF_INET;
	broadcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	broadcast_addr.sin_port = htons(D30303_DPORT);

	len = sendto(fd, send, strlen(send), 0,
		     (struct sockaddr *)&broadcast_addr,
		     sizeof(struct sockaddr_in));
	LOG(LOG_DEBUG, "Sent UDP 30303 Discovery message");
}

/**
   \brief Setup the 30303 discovery
   \param mac_prefix Optional mac prefix to search for
   \param hostname Optional hostname to search for
 */

void d30303_setup(char *mac_prefix, char *hostname)
{
	struct event *timer_ev;
	struct timeval secs = { D30303_SCAN_TIMEOUT, 0 };

	/* alloc discovery device */
	d30303_dev = safer_malloc(sizeof(d30303_dev_t));
	if (mac_prefix != NULL)
		d30303_dev->mac_prefix = strdup(mac_prefix);
	else
		d30303_dev->mac_prefix = NULL;
	if (hostname != NULL)
		d30303_dev->hostname = strdup(hostname);
	else
		d30303_dev->hostname = NULL;

	/* build discovery event */
	d30303_fd = bind_d30303_recv();
	if (d30303_fd != -1) {
		d30303_ev = event_new(base, d30303_fd, EV_READ | EV_PERSIST,
				      cb_d30303_read, NULL);
		event_add(d30303_ev, NULL);
		event_base_once(base, d30303_fd, EV_WRITE|EV_TIMEOUT,
				cb_d30303_send, NULL, NULL);
		LOG(LOG_NOTICE, "Searching for UDP 30303 devices");
	} else
		LOG(LOG_ERROR, "Failed to setup discovery event");

	timer_ev = evtimer_new(base, cb_end_d30303, NULL);
	evtimer_add(timer_ev, &secs);
}
