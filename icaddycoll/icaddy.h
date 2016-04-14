#ifndef _ICADDY_H_
#define _ICADDY_H_

#include "collector.h"

#define ICADDYCOLL_CONFIG_FILE  "icaddycoll.conf"
#define ICADDYCOLL_LOG_FILE     "icaddycoll.log"
#define ICADDYCOLL_PID_FILE     "icaddycoll.pid"

#define ICADDY_DPORT		30303
#define ICADDY_DSTRING		"Discovery: Who is out there?"

#define ICADDY_HTTP_PORT	80

/* really?  you have more than 8?  recompile. sheesh */
#define MAX_ICADDY_DEVS		8
#define IC_SCAN_TIMEOUT		5 /* seconds */

#define ICJ_SETTINGS	"/settingsVars.json"
#define ICJ_STATUS	"/status.json"

#define ICJ_RUNON_URL	"/runSprinklers.htm"
#define ICJ_RUNON_POST	"run=run"
#define ICJ_RUNOFF_URL	"/stopSprinklers.htm"
#define ICJ_RUNOFF_POST	"stop=off"
#define ICJ_STOPPROG_POST	"stop=active"
#define ICJ_INDEX_URL	"/index.htm"

typedef struct _http_ctx_t {
        struct evhttp_uri *uri;
        struct evhttp_connection *cn;
        struct evhttp_request *req;
        char *query;
} http_ctx_t;

typedef struct _icaddy_discovery_resp_t {
	char *ipaddr;
	struct in_addr sin_addr;
	char *uc_hostname;
	char *macaddr;
} icaddy_discovery_resp_t;


#endif /* _ICADDY_H_ */
