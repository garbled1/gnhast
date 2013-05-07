/* $Id$ */

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


#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>

#include "common.h"
#include "gnhast.h"
#include "confuse.h"
#include "gncoll.h"

/**
	\file collector.c
	\author Tim Rightnour
	\brief An example collector, that generates fake data to test
	\note You may use this as a skeleton to create a new collector
*/

/** our logfile */
FILE *logfile;

/** Need the argtable in scope, so we can generate proper commands for the server */
extern argtable_t argtable[];

/** The event base */
struct event_base *base;

cfg_t *cfg;

/* empty options to satisfy confparser */
cfg_opt_t options[] = {
	CFG_END(),
};


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
	\brief A simple timer callback that stops the event loop
	\param nada used for file descriptor
	\param what why did we fire?
	\param arg optional argument
*/

void timer_cb(int nada, short what, void *arg)
{
	event_loopbreak();
}

/**
	\brief random number generator, between x and y
	\param min smallest number
	\param max largest number
	\return The random number
*/

int rndm(int min, int max)
{
	int diff;

	diff = max - min + 1;
	if (max < 1 || diff < 1)
		return(min);

	return(random()%diff+min);
}

/**
	\brief random number generator, between x and y, float value
	\param min smallest number
	\param max largest number
	\return The random number
*/

float frndm(float min, float max)
{
	float diff;

	diff = max - min + 1.0;
	if (max < 1 || diff < 1)
		return(min);

	return(drand48()*diff+min);
}

/**
	\brief Quick routine to generate lots of fake events
	\param out bufferevent to schedule to
*/

void generate_chaff(struct bufferevent *out)
{
	device_t *deva, *devb, *devc;
	int i, j;
	struct timeval secs = { 0, 0 };

	/* Setup 3 fake devices */
	deva = smalloc(device_t);
	devb = smalloc(device_t);
	devc = smalloc(device_t);

	/* switch device */
	deva->name = deva->uid = "Switch";
	deva->proto = PROTO_INSTEON_V2;
	deva->type = DEVICE_SWITCH;
	deva->subtype = SUBTYPE_SWITCH;

	/* temperature sensor */
	devb->name = devb->uid = "TempSensor";
	devb->proto = PROTO_SENSOR_OWFS;
	devb->type = DEVICE_SENSOR;
	devb->subtype = SUBTYPE_TEMP;

	/* dimmer */
	devc->name = devc->uid = "Dimmer";
	devc->proto = PROTO_INSTEON_V1;
	devc->type = DEVICE_DIMMER;
	devc->subtype = SUBTYPE_OUTLET;

	/* tell the server about the three devices */
	gn_register_device(deva, out);
	gn_register_device(devb, out);
	gn_register_device(devc, out);

	/* Generate 50 device updates */
	for (i=0; i < 50; i++) {
		j = rndm(0, 2);
		switch(j) {
		case 0:
			deva->data.state = rndm(0,1);
			gn_update_device(deva, 0, out);
			break;
		case 1:
			devb->data.temp = frndm(60.0, 110.0);
			gn_update_device(devb, 0, out);
			break;
		case 2:
			devc->data.level = frndm(0.0, 100.0);
			gn_update_device(devc, 0, out);
			break;
		}
		/* sleep for 2-10 seconds */
		secs.tv_sec = rndm(2, 10);
		/* this timer will stop the eventloop and let us generate a new event */
		event_base_loopexit(base, &secs);
		/* fire the event engine up */
		event_base_dispatch(base);
	}
}

int main(int argc, char **argv)
{
        struct sockaddr_in serv_addr;
        struct hostent *server;
	client_t *srv;
	extern char *optarg;
	extern int optind;
	int ch, port = 2920;
	char *conffile = "none"; /* XXX Fixme */
	char *gnhastdserver = "127.0.0.1";

	/* Initialize the event system */
	base = event_base_new();

	/* Initialize the argtable */
	init_argcomm();

	/* process command line arguments */
	while ((ch = getopt(argc, argv, "?c:s:p:")) != -1)
		switch (ch) {
		case 'c':	/* Set configfile */
			conffile = optarg;
			break;
		case 's':	/* set servername */
			gnhastdserver = optarg;
			break;
		case 'p':	/* portnum */
			port = atoi(optarg);
			break;
		default:
		case '?':	/* you blew it */
			(void)fprintf(stderr, "usage:\n%s [-c configfile]"
			    "[-s server] [-p port]\n", getprogname());
			return(EXIT_FAILURE);
			/*NOTREACHED*/
			break;
		}


	/* Setup our socket */
	srv = smalloc(client_t);

        srv->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (srv->fd < 0)
		LOG(LOG_FATAL, "Failed to create socket: %s", strerror(errno));

        server = gethostbyname(gnhastdserver); /* XXX */
       	if (!server)
		LOG(LOG_FATAL, "Failed lookup of servername");
 
        serv_addr.sin_family = AF_INET;
        memcpy((char *) &(serv_addr.sin_addr.s_addr), (char *)(server->h_addr), server->h_length);
        serv_addr.sin_port = htons(port);
 
        if (connect(srv->fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
		LOG(LOG_FATAL, "Failed to connect to server:%s", strerror(errno));

	/* Build the bufferevent, realistically, all we need here is the event callback */
	srv->ev = bufferevent_socket_new(base, srv->fd, 0);
	bufferevent_setcb(srv->ev, buf_read_cb, NULL, buf_error_cb, srv);
	bufferevent_enable(srv->ev, EV_READ|EV_WRITE);

	/* spam the server */
	generate_chaff(srv->ev);

	/* Close out the fd, and bail */
        close(srv->fd); 
	return(0);
}

