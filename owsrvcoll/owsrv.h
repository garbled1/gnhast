#ifndef _OWSRV_H_
#define _OWSRV_H_

#define OWSRVCOLL_CONFIG_FILE	"owsrvcoll.conf"
#define OWSRVCOLL_LOG_FILE	"owsrvcoll.log"
#define OWSRVCOLL_PID_FILE	"owsrvcoll.pid"
#define COLLECTOR_NAME		"owsrvcoll"

#define OWSRV_BASE_F	0
#define OWSRV_BASE_C	1
#define OWSRV_BASE_K	2
#define OWSRV_BASE_R	3

/* Cobbled together from owfs docs, owtap, etc */

enum OWS_MESSAGE_TYPES {
	OWSM_ERROR,
	OWSM_NOP,
	OWSM_READ,
	OWSM_WRITE,
	OWSM_DIR,
	OWSM_SIZE, /* unused */
	OWSM_PRESENT,
	OWSM_DIRALL,
	OWSM_GET,
	OWSM_DIRALLSLASH,
	OWSM_GETSLASH,
};

/* message to owserver */
struct server_msg {
	int32_t version;
	int32_t payload;
	int32_t type;
	int32_t control_flags;
	int32_t size;
	int32_t offset;
};

/* message to client */
struct client_msg {
	int32_t version;
	int32_t payload;
	int32_t ret;
	int32_t control_flags;
	int32_t size;
	int32_t offset;
};

#define OWFLAG_TEMP_C	0
#define OWFLAG_TEMP_F	(1<<16)
#define OWFLAG_TEMP_K	(1<<17)
#define OWFLAG_TEMP_R	(OWFLAG_TEMP_F&OWFLAG_TEMP_K)
#define OWFLAG_PERSIST	(1<<2)

#endif /*_OWSRV_H_*/
