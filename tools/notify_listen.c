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
   \file ssdp_scan.c
   \author Tim Rightnour
   \brief Simple SSDP NOTIFY listener
*/

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <termios.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <event2/dns.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/util.h>

#include "common.h"
#include "ssdp.h"
#include "confuse.h"

extern int debugmode;

FILE *logfile;   /** our logfile */
cfg_t *cfg;

/* SSDP stuffs */
int ssdp_fd = -1;
int records = 0;
extern int ssdp_portnum;
char *sfield = NULL;

/** The event base */
struct event_base *base;
struct evdns_base *dns_base;

/*
  Stub
*/
cfg_t *parse_conf(const char *filename)
{
	return NULL;
}

/*****
      SSDP Setup
*****/

/**
   \brief Watch for NOTIFY messages
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void cb_notify_read(int fd, short what, void *arg)
{
	char buf[2048], buf2[8], *result;
	int len, size;
	struct sockaddr_in cli_addr;

	size = sizeof(struct sockaddr);
	bzero(buf, sizeof(buf));
	len = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&cli_addr,
		       &size);

	LOG(LOG_DEBUG, "got %d bytes on udp:%d", len, ssdp_portnum);

	buf[len] = '\0';
	records++;
	printf("Response #%d from: %s\n", records,
	       inet_ntoa(cli_addr.sin_addr));
	if (sfield == NULL) {
		printf("%s\n", buf);
		return;
	}
	result = find_ssdp_field(buf, sfield);
	if (result == NULL) {
		printf("Field %s not in reply\n", sfield);
		if (debugmode)
			printf("%s\n", buf);	
	
	} else {
		printf("%s: %s\n", sfield, result);
		free(result);
	}
}

/**
   \brief Setup an ssdp listener
*/

void notify_setup()
{
	struct event *ssdp_ev;

	/* build ssdp event */
	ssdp_fd = bind_notify_recv();
	if (ssdp_fd == -1) {
		LOG(LOG_ERROR, "Failed to setup ssdp reciever event");
		exit(1);
	}

	ssdp_ev = event_new(base, ssdp_fd, EV_READ | EV_PERSIST,
			   cb_notify_read, NULL);
	event_add(ssdp_ev, NULL);
}

/**
   \brief Shutdown timer
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void cb_shutdown(int fd, short what, void *arg)
{
	LOG(LOG_NOTICE, "Scan complete, found %d records", records);
	event_base_loopexit(base, NULL);
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
	int ch, fd, timeout = 0;
	char *buf;
	struct event *ev;
	struct timeval secs = { 0, 0 };

	/* process command line arguments */
	while ((ch = getopt(argc, argv, "?ds:t:")) != -1)
		switch (ch) {
		case 'd':
			debugmode = 1;
			break;
		case 's':
			sfield = strdup(optarg);
			break;
		case 't':
			timeout = atoi(optarg);
			break;
		default:
		case '?':	/* you blew it */
			(void)fprintf(stderr, "usage:\n%s [-s search_string]"
				      "[-t timeout]\n", getprogname());
			return(EXIT_FAILURE);
			/*NOTREACHED*/
			break;
		}

	/* Initialize the event system */
	base = event_base_new();

	if (timeout) {
		secs.tv_sec = timeout;
		ev = evtimer_new(base, cb_shutdown, NULL);
		evtimer_add(ev, &secs);
	}

	notify_setup();

	/* go forth and destroy */
	event_base_dispatch(base);
	return(0);
}
