#ifndef _HTTP_FUNC_H_
#define _HTTP_FUNC_H_

struct _http_get_t;

typedef struct _http_get_t {
	char *url_prefix;
	char *url_suffix;
	void (*cb)(struct evhttp_request *, void *);
	int http_port;
	int (*precheck)(struct _http_get_t *);
} http_get_t;

void cb_http_GET(int fd, short what, void *arg);
void http_POST(char *url_prefix, char *url_suffix, int http_port,
	       char *payload, void (*cb)(struct evhttp_request *, void *));


#endif /*_HTTP_FUNC_H_*/
