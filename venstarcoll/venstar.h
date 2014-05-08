#ifndef _VENSTAR_H_
#define _VENSTAR_H_

#define VENSTARCOLL_CONFIG_FILE  "venstarcoll.conf"
#define VENSTARCOLL_LOG_FILE     "venstarcoll.log"
#define VENSTARCOLL_PID_FILE     "venstarcoll.pid"
#define COLLECTOR_NAME		"venstarcoll"

#define VENSTAR_HTTP_PORT	80

#define VEN_API		"/"
#define VEN_INFO	"/query/info"
#define VEN_SENSORS	"/query/sensors"
#define VEN_RUNTIMES	"/query/runtimes"
#define VEN_ALERTS	"/query/alerts"

#define VEN_FEEDS	4

#define VEN_CONTROL	"/control"
#define VEN_SETTINGS	"/settings"

#define TTYPE_RES	0
#define TTYPE_COM	1

typedef struct _connection_t {
        int port;
        int type;
        char *host;
        struct bufferevent *bev;
        int shutdown;
} connection_t;

#define QI_TYPE_STR	0
#define QI_TYPE_INT	1
#define QI_TYPE_BOOL	2
#define QI_TYPE_FLOAT	3

typedef struct _queryinfo_t {
	char *name;
	int type;
	int residential;
} queryinfo_t;

#endif /*_VENSTAR_H_*/
