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
#include <inttypes.h>
#include <string.h>

#include "gnhast.h"
#include "common.h"
#include "http_func.h"

/** event base externs */
extern struct event_base *base;
extern struct evdns_base *dns_base;
extern struct evhttp_connection *http_cn;

static char *basic_authstring = NULL;
static int http_use_auth = HTTP_AUTH_NONE;

/**
   \brief Base64 encode function
   \param data_buf string to encode
   \param dataLength length of string to be encoded
   \param result provide a buffer here
   \param resultSize size of result buffer
   \note Stolen from:
   http://en.wikibooks.org/wiki/Algorithm_Implementation/Miscellaneous/Base64#C
*/

int base64_encode(const void* data_buf, size_t dataLength, char* result,
		  size_t resultSize)
{
	const char base64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	const uint8_t *data = (const uint8_t *)data_buf;
	size_t resultIndex = 0;
	size_t x;
	uint32_t n = 0;
	int padCount = dataLength % 3;
	uint8_t n0, n1, n2, n3;
 
	/* increment over the length of the string, three characters at a
	   time */
	for (x = 0; x < dataLength; x += 3) {
		/* these three 8-bit (ASCII) characters
		   become one 24-bit number */
		n = ((uint32_t)data[x]) << 16; 
		if ((x+1) < dataLength)
			n += ((uint32_t)data[x+1]) << 8;
 
		if ((x+2) < dataLength)
			n += data[x+2];
 
		/* this 24-bit number gets separated into four 6-bit numbers */
		n0 = (uint8_t)(n >> 18) & 63;
		n1 = (uint8_t)(n >> 12) & 63;
		n2 = (uint8_t)(n >> 6) & 63;
		n3 = (uint8_t)n & 63;
 
		/*
		 * if we have one byte available, then its encoding is spread
		 * out over two characters
		 */
		if (resultIndex >= resultSize)
			return 1;   /* indicate failure: buffer too small */
		result[resultIndex++] = base64chars[n0];
		if (resultIndex >= resultSize)
			return 1;   /* indicate failure: buffer too small */
		result[resultIndex++] = base64chars[n1];
 
		/*
		 * if we have only two bytes available, then their encoding is
		 * spread out over three chars
		 */
		if ((x+1) < dataLength) {
			if (resultIndex >= resultSize)
				return 1; /* failure: buffer too small */
			result[resultIndex++] = base64chars[n2];
		}
 
		/*
		 * if we have all three bytes available, then their encoding is spread
		 * out over four characters
		 */
		if ((x+2) < dataLength) {
			if (resultIndex >= resultSize)
				return 1;   /* failure: buffer too small */
			result[resultIndex++] = base64chars[n3];
		}
	}
 
	/*
	 * create and add padding that is required if we did not have a multiple of 3
	 * number of characters available
	 */
	if (padCount > 0) { 
		for (; padCount < 3; padCount++) { 
			if (resultIndex >= resultSize)
				return 1;   /* failure: buffer too small */
			result[resultIndex++] = '=';
		} 
	}
	if (resultIndex >= resultSize)
		return 1;   /* indicate failure: buffer too small */
	result[resultIndex] = 0;
	return 0;   /* indicate success */
}


/**
   \brief Set the username/password/authentication type
   \param username The username
   \param password The password
   \param authtype The authentication type
*/

void http_setup_auth(char *username, char *password, int authtype)
{
	char encoded_buf[1024];
	char authbuf[1024];
	char encoded_headerbuf[1024+7];

	if (authtype == HTTP_AUTH_NONE) {
		if (basic_authstring != NULL)
			free(basic_authstring);
		http_use_auth = authtype;
		return;
	}

	if (username == NULL || password == NULL)
		return; /* really? */

	if ((strlen(username) + strlen(password)) > 1024) {
		LOG(LOG_ERROR, "username/password combo too big!");
		return;
	}

	sprintf(authbuf, "%s:%s", username, password);
	if (base64_encode(authbuf, strlen(authbuf), encoded_buf, 1024)) {
		LOG(LOG_ERROR, "unable to bas64_encode username/password");
		return;
	}
	http_use_auth = authtype;
	sprintf(encoded_headerbuf, "Basic %s", encoded_buf);
	basic_authstring = strdup(encoded_headerbuf);
	LOG(LOG_DEBUG, "Authstring encoded to: %s", encoded_headerbuf);
}

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
	if (http_use_auth == HTTP_AUTH_BASIC && basic_authstring != NULL) {
		evhttp_add_header(req->output_headers, "Authorization",
				  basic_authstring);
		LOG(LOG_DEBUG, "Authstring: %s", basic_authstring);
	}
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
	if (http_use_auth == HTTP_AUTH_BASIC && basic_authstring != NULL)
		evhttp_add_header(req->output_headers, "Authorization",
				  basic_authstring);

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

