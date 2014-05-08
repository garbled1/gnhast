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
   \file http_func.c
   \author Tim Rightnour
   \brief Generic HTTP helper routines
   Requires a DNS base, and a struct evhttp_connection *http_cn
*/

#include <event2/dns.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/util.h>

#include "gnhast.h"
#include "common.h"
#include "http_func.h"

/** event base externs */
extern struct event_base *base;
extern struct evdns_base *dns_base;
extern struct evhttp_connection *http_cn;

/**
   \brief Callback to perform a GET from an http server
   \param fd unused
   \param what what happened?
   \param arg url suffix
*/

void cb_http_GET(int fd, short what, void *arg)
{
	http_get_t *getinfo = (http_get_t *)arg;
	struct evhttp_uri *uri;
	struct evhttp_request *req;
	device_t *dev;

	/* do precheck? */
	if (getinfo->precheck != NULL)
		if (getinfo->precheck(getinfo) != 0)
			return;

	if (getinfo->url_prefix == NULL) {
		LOG(LOG_ERROR, "url is NULL, punt");
		return;
	}

	if (getinfo->url_suffix == NULL)
		getinfo->url_suffix = "/";

	uri = evhttp_uri_parse(getinfo->url_prefix);
	if (uri == NULL) {
		LOG(LOG_ERROR, "Failed to parse URL: %s", getinfo->url_prefix);
		return;
	}

	evhttp_uri_set_port(uri, getinfo->http_port);
	LOG(LOG_DEBUG, "host: %s port: %d url: %s%s", evhttp_uri_get_host(uri),
	    evhttp_uri_get_port(uri), getinfo->url_prefix,
	    getinfo->url_suffix);

	if (http_cn == NULL)
		http_cn = evhttp_connection_base_new(base, dns_base,
						     evhttp_uri_get_host(uri),
						     evhttp_uri_get_port(uri));
	
	req = evhttp_request_new(getinfo->cb, NULL);
	evhttp_make_request(http_cn, req, EVHTTP_REQ_GET, getinfo->url_suffix);
	evhttp_add_header(req->output_headers, "Host",
			  evhttp_uri_get_host(uri));
	evhttp_uri_free(uri);
}

/**
   \brief POST data to an HTTP server
   \param url_suffix URL to POST to
   \param payload data to send
*/

void http_POST(char *url_prefix, char *url_suffix, int http_port,
	       char *payload, void (*cb)(struct evhttp_request *, void *))
{
	struct evhttp_uri *uri;
	struct evhttp_request *req;
	struct evbuffer *data;
	int i;
	char buf[256];

	if (url_prefix == NULL) {
		LOG(LOG_ERROR, "url_prefix is NULL, punt");
		return;
	}

	uri = evhttp_uri_parse(url_prefix);
	if (uri == NULL) {
		LOG(LOG_ERROR, "Failed to parse URL %s", url_prefix);
		return;
	}

	evhttp_uri_set_port(uri, http_port);
	LOG(LOG_DEBUG, "host: %s port: %d", evhttp_uri_get_host(uri),
	    evhttp_uri_get_port(uri));

	if (http_cn == NULL)
		http_cn = evhttp_connection_base_new(base, dns_base,
						     evhttp_uri_get_host(uri),
						     evhttp_uri_get_port(uri));
	
	req = evhttp_request_new(cb, NULL);
	if (url_suffix != NULL)
		evhttp_make_request(http_cn, req, EVHTTP_REQ_POST, url_suffix);
	else
		evhttp_make_request(http_cn, req, EVHTTP_REQ_POST, "/");

	evhttp_add_header(req->output_headers, "Host",
			  evhttp_uri_get_host(uri));
	evhttp_add_header(req->output_headers, "Content-Type",
			  "application/x-www-form-urlencoded");

	LOG(LOG_DEBUG, "POSTing to %s%s", url_prefix,
	    url_suffix ? url_suffix : "");
	if (payload != NULL) {
		sprintf(buf, "%d", strlen(payload));
		evhttp_add_header(req->output_headers, "Content-Length", buf);
		data = evhttp_request_get_output_buffer(req);
		evbuffer_add_printf(data, payload);
		LOG(LOG_DEBUG, "POST payload: %s", payload);
	}
	evhttp_uri_free(uri);
}

