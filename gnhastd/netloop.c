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

/**
	\file netloop.c
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <sys/rbtree.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/bufferevent_ssl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#include "confuse.h"
#include "common.h"
#include "gnhastd.h"
#include "commands.h"
#include "cmds.h"

extern cfg_t *cfg;
extern struct event_base *base;

static int setnonblock(int fd)
{
	int f;

	f = fcntl(fd, F_GETFL);
	f |= O_NONBLOCK;
	fcntl(fd, F_SETFL, f);
}

void buf_read_cb(struct bufferevent *in, void *arg)
{
	char *data;
	char **words, *cmdword;
	int numwords, i;
	pargs_t *args=NULL;
	struct evbuffer *evbuf;
	size_t len;

	/* loop as long as we have data to read */
	while (1) {
		evbuf = bufferevent_get_input(in);
		data = evbuffer_readln(evbuf, &len, EVBUFFER_EOL_CRLF);

		if (data == NULL || len < 1)
			return;

		LOG(LOG_DEBUG, "Got data %s", data);

		words = parse_netcommand(data, &numwords);

		if (words == NULL || words[0] == NULL)
			goto out;

		cmdword = strdup(words[0]);
		args = parse_command(words, numwords);
		parsed_command(cmdword, args, arg);
		free(cmdword);
	}

out:
	if (args) {
		for (i=0; args[i].cword != -1; i++)
			if (args[i].type == PTCHAR)
				free(args[i].arg.c);
		free(args);
		args=NULL;
	}
	free(data);
}

void buf_write_cb(struct bufferevent *out, void *arg)
{
/*
	struct evbuffer *reply;

	reply = evbuffer_new();
	evbuffer_add_printf(reply, "Got data %s\n", data);
	bufferevent_write_buffer(in, reply);
	evbuffer_free(reply);
*/
}

void buf_error_cb(struct bufferevent *ev, short what, void *arg)
{
	client_t *client = (client_t *)arg;
	device_t *dev;
	wrap_device_t *wrap;
	int err;

	if (what & BEV_EVENT_ERROR) {
		err = bufferevent_get_openssl_error(ev);
		if (err)
			LOG(LOG_ERROR,
			    "SSL Error: %s", ERR_error_string(err, NULL));
	}
	bufferevent_free(client->ev);
	if (client->tev)
		event_free(client->tev);
	close(client->fd);
	/* remove all the tailq entries on the device list */
	if (client->provider)
		while (TAILQ_FIRST(&client->devices) != NULL) {
			dev = TAILQ_FIRST(&client->devices);
			dev->collector = NULL;
			TAILQ_REMOVE(&client->devices, dev, next_client);
		}
	while (TAILQ_FIRST(&client->wdevices) != NULL) {
		wrap = TAILQ_FIRST(&client->wdevices);
		TAILQ_REMOVE(&client->wdevices, wrap, next);
		free(wrap);
	}
	free(client);
}

void accept_cb(struct evconnlistener *serv, int sock, struct sockaddr *sa,
	       int sa_len, void *arg)
{
	struct sockaddr_in *client_addr = (struct sockaddr_in *)sa;
	client_t *client;

	client = smalloc(client_t);
	client->fd = sock;

	client->ev = bufferevent_socket_new(base, sock,
            BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE);

	LOG(LOG_NOTICE, "Connection on insecure port from %s",
	    inet_ntoa(client_addr->sin_addr));

	bufferevent_setcb(client->ev, buf_read_cb, NULL,
			  buf_error_cb, client);
	bufferevent_enable(client->ev, EV_READ|EV_PERSIST);
}

void sslaccept_cb(struct evconnlistener *serv, int sock, struct sockaddr *sa,
	       int sa_len, void *arg)
{
	struct sockaddr_in *client_addr = (struct sockaddr_in *)sa;
	client_t *client;

	client = smalloc(client_t);
	client->fd = sock;

	client->srv_ctx = (SSL_CTX *)arg;
	client->cli_ctx = SSL_new(client->srv_ctx);

	client->ev = bufferevent_openssl_socket_new(base, sock,
	    client->cli_ctx, BUFFEREVENT_SSL_ACCEPTING,
            BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE);

	LOG(LOG_NOTICE, "Connection on secure port from %s",
	    inet_ntoa(client_addr->sin_addr));

	bufferevent_setcb(client->ev, buf_read_cb, NULL,
			  buf_error_cb, client);
	bufferevent_enable(client->ev, EV_READ|EV_PERSIST);
}

static SSL_CTX *evssl_init(void)
{
	SSL_CTX *server_ctx;
	cfg_t *network;

	/* Initialize the OpenSSL library */
	SSL_load_error_strings();
	SSL_library_init();
	/* We MUST have entropy, or else there's no point to crypto. */
	if (!RAND_poll())
		return NULL;

	if (cfg) {
		network = cfg_getsec(cfg, "network");

		if (!network)
			LOG(LOG_FATAL, "Error reading config file");
	}

	server_ctx = SSL_CTX_new(SSLv23_server_method());

	if (! SSL_CTX_use_certificate_chain_file(server_ctx, cfg_getstr(network, "certchain")) ||
	    ! SSL_CTX_use_PrivateKey_file(server_ctx, cfg_getstr(network, "privkey"), SSL_FILETYPE_PEM)) {
		LOG(LOG_FATAL, "Couldn't read certificate file %s or "
		    "privte key file %s.  To generate a key and "
		    "self-signed certficiate, run:\n"
		    "  openssl genrsa -out pkey 2048\n"
		    "  openssl req -new -key pkey -out cert.req\n"
		    "  openssl x509 -req -days 365 -in cert.req "
		    "-signkey pkey -out cert",
		    cfg_getstr(network, "certchain"),
		    cfg_getstr(network, "privkey"));
		return NULL;
	}
	SSL_CTX_set_options(server_ctx, SSL_OP_NO_SSLv2);

	return server_ctx;
}


void init_netloop(void)
{
	int lsock;
	cfg_t *network;
	struct sockaddr_in sin;
	struct event *accept_event;
	struct evconnlistener *listener, *listen_nossl;
	int reuse = 1;
	SSL_CTX *ctx;

	if (cfg) {
		network = cfg_getsec(cfg, "network");

		if (!network)
			LOG(LOG_FATAL, "Error reading config file");
	}

	ctx = evssl_init();
	if (ctx == NULL)
		LOG(LOG_FATAL, "Could not init openssl");

	/* setup socket */
	lsock = socket(PF_INET, SOCK_STREAM, 0);
	if (lsock < 0)
		LOG(LOG_FATAL, "Failed to create listener socket: %s", strerror(errno));

	/* set listening address */
	memset(&sin, 0, sizeof(listener));
	sin.sin_family = PF_INET;
	if (strcmp(cfg_getstr(network, "listen"), "0.0.0.0") == 0 ||
	    strcmp(cfg_getstr(network, "listen"), "INADDR_ANY") == 0 ||
	    strcmp(cfg_getstr(network, "listen"), "ANY") == 0)
		sin.sin_addr.s_addr = INADDR_ANY;
	else
		inet_aton(cfg_getstr(network, "listen"), &sin.sin_addr);
	sin.sin_port = htons(cfg_getint(network, "sslport"));

	listener = evconnlistener_new_bind(base, sslaccept_cb, (void *)ctx,
	    LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, 1024,
	    (struct sockaddr *)&sin, sizeof(sin));

	LOG(LOG_NOTICE, "Listening with SSL on %s:%d",
	    cfg_getstr(network, "listen"), cfg_getint(network, "sslport"));

	/* now listen on non-secure port */

	sin.sin_port = htons(cfg_getint(network, "port"));

	listen_nossl = evconnlistener_new_bind(base, accept_cb, NULL,
	    LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, 1024,
	    (struct sockaddr *)&sin, sizeof(sin));
	LOG(LOG_NOTICE, "Listening without SSL on %s:%d",
	    cfg_getstr(network, "listen"), cfg_getint(network, "port"));

	return;
}
