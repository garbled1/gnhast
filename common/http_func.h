#ifndef _HTTP_FUNC_H_
#define _HTTP_FUNC_H_

#define HTTP_AUTH_NONE	0
#define HTTP_AUTH_BASIC	1

struct _http_get_t;

typedef struct _http_get_t {
	char *url_prefix;
	char *url_suffix;
	void (*cb)(struct evhttp_request *, void *);
	int http_port;
	int (*precheck)(struct _http_get_t *);
	struct evhttp_connection *http_cn;
} http_get_t;

int base64_encode(const void* data_buf, size_t dataLength, char* result,
		  size_t resultSize);
void http_setup_auth(char *username, char *password, int authtype);
void cb_http_GET(int fd, short what, void *arg);
void http_POST(char *url_prefix, char *url_suffix, int http_port,
	       char *payload, void (*cb)(struct evhttp_request *, void *));


#endif /*_HTTP_FUNC_H_*/
