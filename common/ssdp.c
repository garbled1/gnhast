/*
 * Copyright (c) 2014
 *	  Tim Rightnour.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 * 3. The name of Tim Rightnour may not be used to endorse or promote 
 *	products derived from this software without specific prior written 
 *	permission.
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
   \file common.c
   \author Tim Rightnour
   \brief UPnP SSDP Discovery functions
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <event2/dns.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/util.h>

#include "common.h"
#include "ssdp.h"

extern int debugmode;
int ssdp_portnum;

char *ssdp_fmt_str =	"M-SEARCH * HTTP/1.1\r\n" \
			"Host: " SSDP_MULTICAST ":" SSDP_PORT_STR "\r\n" \
			"Man: ssdp:discover\r\n" \
			"ST: %s\r\n"					\
			"MX: " SSDP_WAIT_STR "\r\n"			\
			"USER-AGENT: " PACKAGE_NAME "/" PACKAGE_VERSION "" \
			"\r\n";


/**
   \brief Send an M-SEARCH request to the broadcast addr
   \param fd the search fd
   \param what what happened?
   \param arg char * of ST field
*/

void cb_ssdp_msearch_send(int fd, short what, void *arg)
{
	char *stfield = (char *)arg;
	char ssdp_send[1024];
	int len;
	struct sockaddr_in broadcast_addr;

	if (stfield == NULL)
		return;

	memset(&broadcast_addr, 0, sizeof(broadcast_addr));
	broadcast_addr.sin_family = AF_INET;
	broadcast_addr.sin_addr.s_addr = inet_addr(SSDP_MULTICAST);
	broadcast_addr.sin_port = htons(SSDP_PORT);
	sprintf(ssdp_send, ssdp_fmt_str, stfield);

	len = sendto(fd, ssdp_send, strlen(ssdp_send), 0,
		     (struct sockaddr *)&broadcast_addr,
		     sizeof(struct sockaddr_in));
	LOG(LOG_DEBUG, "Sent SSDP ST:%s", stfield);
}

/**
   \brief bind udp to listen to SSDP events
   \return fd
*/
int bind_ssdp_recv(void)
{
	int sock_fd, f, flag=1;
	struct sockaddr_in sin;
	socklen_t slen;

	if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		LOG(LOG_ERROR, "Failed to bind SSDP in socket()");
		return -1;
	}

	if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &flag,
		       sizeof(int)) < 0) {
		LOG(LOG_ERROR, "Could not set SO_REUSEADDR for SSDP");
		return -1;
	}
	if (setsockopt(sock_fd, SOL_SOCKET, SO_BROADCAST, &flag,
		       sizeof(int)) < 0) {
		LOG(LOG_ERROR, "Could not set SO_BROADCAST for SSDP");
		return -1;
	}
	if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEPORT, &flag,
		       sizeof(int)) < 0) {
		LOG(LOG_ERROR, "Could not set SO_REUSEPORT for SSDP");
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
		LOG(LOG_ERROR, "bind() for SSDP port failed");
		return -1;
	} else {
		slen = sizeof(sin);
		getsockname(sock_fd, (struct sockaddr *)&sin, &slen);
		ssdp_portnum = ntohs(sin.sin_port);
		LOG(LOG_NOTICE,
		    "Listening on port %d:udp for SSDP events", ssdp_portnum);
	}

	return sock_fd;
}


/**
   \brief bind udp to listen to NOTIFY events
   \return fd
*/
int bind_notify_recv(void)
{
	int sock_fd, f, flag=1;
	struct sockaddr_in sin;
	struct ip_mreq mreq;
	socklen_t slen;

	if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		LOG(LOG_ERROR, "Failed to bind SSDP in socket()");
		return -1;
	}

	if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &flag,
		       sizeof(int)) < 0) {
		LOG(LOG_ERROR, "Could not set SO_REUSEADDR for SSDP");
		return -1;
	}
	if (setsockopt(sock_fd, SOL_SOCKET, SO_BROADCAST, &flag,
		       sizeof(int)) < 0) {
		LOG(LOG_ERROR, "Could not set SO_BROADCAST for SSDP");
		return -1;
	}
	if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEPORT, &flag,
		       sizeof(int)) < 0) {
		LOG(LOG_ERROR, "Could not set SO_REUSEPORT for SSDP");
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
	sin.sin_port = htons(SSDP_PORT);

	if (bind(sock_fd, (struct sockaddr *)&sin,
		 sizeof(struct sockaddr)) < 0) {
		LOG(LOG_ERROR, "bind() for SSDP port failed");
		return -1;
	} else {
		mreq.imr_multiaddr.s_addr = inet_addr(SSDP_MULTICAST);
		mreq.imr_interface.s_addr = htonl(INADDR_ANY);
		if (setsockopt(sock_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
			       &mreq, sizeof(mreq)) < 0) {
			LOG(LOG_ERROR, "Could not set membership on socket");
			return -1;
		}
		slen = sizeof(sin);
		getsockname(sock_fd, (struct sockaddr *)&sin, &slen);
		ssdp_portnum = ntohs(sin.sin_port);
		LOG(LOG_NOTICE, "Listening on port %d:udp for NOTIFY events",
		    ssdp_portnum);
	}

	return sock_fd;
}

/**
   \brief Parse a SSDP/NOTIFY buffer, looking for a specific field
   \param buf Buffer to parse
   \param field field to look for in buffer
   \return malloc'd copy of field data
   Returns NULL if not found
   Caller is responsible for freeing returned buffer
*/

char *find_ssdp_field(char *buf, char *field)
{
	char *p, *begin, *end, *ret;
	size_t len;

	p = strcasestr(buf, field);
	if (p == NULL)
		return NULL;

	end = strchr(p, '\r');
	if (end == NULL)
		end = strchr(p, '\n');
	if (end == NULL)
		end = strchr(p, '\0');
	if (end == NULL)
		return NULL;

	begin = strchr(p, ':'); /* find first : */
	if (begin == NULL)
		return NULL;
	begin += 2; /* skip the space and the : */

	len = end - begin;

	ret = safer_malloc(len+1);
	strncpy(ret, begin, len);
	ret[len] = '\0'; /* just in case */

	return ret;
}
