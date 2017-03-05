#ifndef _ICADDY_H_
#define _ICADDY_H_

#include "collector.h"

#define ICADDYCOLL_CONFIG_FILE  "icaddycoll.conf"
#define ICADDYCOLL_LOG_FILE     "icaddycoll.log"
#define ICADDYCOLL_PID_FILE     "icaddycoll.pid"

#define ICADDY_MAC_PREFIX	"00-04-A3"
#define ICADDY_HTTP_PORT	80

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

#endif /* _ICADDY_H_ */
