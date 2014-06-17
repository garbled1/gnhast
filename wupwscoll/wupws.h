#ifndef _WUPWS_H_
#define _WUPWS_H_

#include "collector.h"

#define CONN_TYPE_GNHASTD	2
#define CONN_TYPE_WUPWS		1

#define WUPWSCOLL_CONFIG_FILE	"wupwscoll.conf"
#define WUPWSCOLL_LOG_FILE	"wupwscoll.log"
#define WUPWSCOLL_PID_FILE	"wupwscoll.pid"

#define WUPWS_DEBUG_URL		"http://127.0.0.1"
#define WUPWS_DEBUG_PATH	"/debug"
#define WUPWS_DEBUG_RAPID	"/rapid"

#define WUPWS_URL		"http://weatherstation.wunderground.com"
#define WUPWS_PATH		"/weatherstation/updateweatherstation.php"

#define WUPWS_RAPID_URL		"http://rtupdate.wunderground.com"
#define WUPWS_RAPID_PATH	"/weatherstation/updateweatherstation.php"

#define WUPWS_PWS_URL		"http://www.pwsweather.com"
#define WUPWS_PWS_PATH		"/pwsupdate/pwsupdate.php"

#define CALCDATA_AVG	1
#define CALCDATA_DIFF	2

#define PWS_DEBUG		0
#define PWS_WUNDERGROUND	1
#define PWS_PWSWEATHER		2

struct calcdata {
	char *pwsdev;
	char *uid;
	double *data;
	int nrofdata;
	int vals;
};

typedef struct _http_ctx_t {
	struct evhttp_uri *uri;
	struct evhttp_connection *cn;
	struct evhttp_request *req;
	char *query;
} http_ctx_t;

void connect_event_cb(struct bufferevent *ev, short what, void *arg);
void connect_server_cb(int nada, short what, void *arg);
void wupws_connect(int fd, short what, void *arg);
int conf_parse_pwstype(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		       void *result);
void upd_calcdata(char *uid, double data);

#endif /*_WUPWS_H_*/
