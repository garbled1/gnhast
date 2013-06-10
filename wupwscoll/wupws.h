#ifndef _WUPWS_H_
#define _WUPWS_H_

#define WUPWSCOLL_CONFIG_FILE	"wupwscoll.conf"
#define WUPWSCOLL_LOG_FILE	"wupwscoll.log"
#define WUPWSCOLL_PID_FILE	"wupwscoll.pid"
#define COLLECTOR_NAME		"wupwscoll"

#define WUPWS_DEBUG	"http://127.0.0.1/update"
#define WUPWS_DEBUG2	"http://127.0.0.1/rapid"
#define WUPWS_URL	"http://weatherstation.wunderground.com/weatherstation/updateweatherstation.php"
#define WUPWS_RAPID	"http://rtupdate.wunderground.com/weatherstation/updateweatherstation.php"
#define WUPWS_PWS	"http://www.pwsweather.com/pwsupdate/pwsupdate.php"

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

typedef struct _connection_t {
	int port;
	int type;
	char *host;
	char *server;
	struct bufferevent *bev;
	int shutdown;
} connection_t;

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
