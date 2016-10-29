/*
 * Copyright (c) 2013
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

#ifndef _GNHAST_H_
#define _GNHAST_H_
/* need queue.h for the devices */
#include <sys/queue.h>

#ifdef HAVE_SYS_RBTREE_H
 #include <sys/rbtree.h>
#else
 #include "../linux/rbtree.h"
#endif

/* For the timeval */
#include <sys/time.h>

/* for client_t */
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/bufferevent_ssl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>

/* General defines */

#define HEALTH_CHECK_RATE	60

/* Basic device types */
enum DEV_TYPES {
	DEVICE_NONE,
	DEVICE_SWITCH,
	DEVICE_DIMMER,
	DEVICE_SENSOR,
	DEVICE_TIMER,
	DEVICE_BLIND, /* can go up or down, but cannot be queried */
	NROF_TYPES,
};
#define DEVICE_MAX DEVICE_BLIND
/* a type blind should always return BLIND_STOP, for consistency */

enum PROTO_TYPES {
	PROTO_NONE,
	PROTO_INSTEON_V1,
	PROTO_INSTEON_V2,
	PROTO_INSTEON_V2CS,
	PROTO_SENSOR_OWFS,
	PROTO_SENSOR_BRULTECH_GEM,
	PROTO_SENSOR_BRULTECH_ECM1240,
	PROTO_SENSOR_WMR918,
	PROTO_SENSOR_AD2USB,
	PROTO_SENSOR_ICADDY,
	PROTO_SENSOR_VENSTAR,
	PROTO_CONTROL_URTSI,
	PROTO_COLLECTOR,
	PROTO_CAMERA_AXIS,
	PROTO_TUXEDO,
	PROTO_NEPTUNE_APEX,
	NROF_PROTOS,
};
#define PROTO_MAX PROTO_NEPTUNE_APEX

/***
    The following files must be updated when adding a subtype:
    common/netparser.c
    common/devices.c
    common/confparser.c (if scaled type, like pressure)
    common/gncoll.c (if scaled type, like pressure)
    gnhastd/cmdhandler.c
    common/commands.h (SC_ instances are here)
    common/collcmd.c
    Note, you must also update the SC_ instances.
***/

enum SUBTYPE_TYPES {
	SUBTYPE_NONE,
	SUBTYPE_SWITCH,
	SUBTYPE_OUTLET,
	SUBTYPE_TEMP,
	SUBTYPE_HUMID,
	SUBTYPE_COUNTER,
	SUBTYPE_PRESSURE,
	SUBTYPE_SPEED,
	SUBTYPE_DIR,
	SUBTYPE_PH, /* stored in ph */
	SUBTYPE_WETNESS,
	SUBTYPE_HUB,
	SUBTYPE_LUX,
	SUBTYPE_VOLTAGE,
	SUBTYPE_WATTSEC,
	SUBTYPE_WATT,
	SUBTYPE_AMPS,
	SUBTYPE_RAINRATE,
	SUBTYPE_WEATHER, /* stored in state */
	SUBTYPE_ALARMSTATUS, /* stored in state */
	SUBTYPE_NUMBER,
	SUBTYPE_PERCENTAGE, /* stored in humid */
	SUBTYPE_FLOWRATE,
	SUBTYPE_DISTANCE, 
	SUBTYPE_VOLUME,
	SUBTYPE_TIMER, /* stored in count, used as countdown to zero */
	SUBTYPE_THMODE, /* stored in state */
	SUBTYPE_THSTATE, /* stored in state */
	SUBTYPE_SMNUMBER, /* 8bit number stored in state */
	SUBTYPE_BLIND, /* stored in state, see BLIND_* */
	SUBTYPE_COLLECTOR, /* stored in state */
	SUBTYPE_TRIGGER, /* momentary switch (ui) */
	SUBTYPE_ORP, /* Oxidation Redux Potential (d) */
	SUBTYPE_SALINITY, /* Salinity (d) */
	SUBTYPE_BOOL,
	NROF_SUBTYPES,
};
#define SUBTYPE_MAX SUBTYPE_BOOL
/* used in owsrv to limit which sensors we can talk about
   add new sensor types between these */
#define SUBTYPE_OWSRV_SENSOR_MIN SUBTYPE_TEMP
#define SUBTYPE_OWSRV_SENSOR_MAX SUBTYPE_LUX

enum BLIND_TYPES {
	BLIND_UP,
	BLIND_DOWN,
	BLIND_STOP,
};

enum COLLECTOR_TYPES {
	COLLECTOR_BAD,
	COLLECTOR_OK,
};

enum TSCALE_TYPES {
	TSCALE_F,
	TSCALE_C,
	TSCALE_K,
	TSCALE_R,
};

enum BAROSCALE_TYPES {
	BAROSCALE_IN,
	BAROSCALE_MM,
	BAROSCALE_MB,
	BAROSCALE_CB,
};

enum LENGTH_TYPES {
	LENGTH_IN,
	LENGTH_MM,
};

enum SPEED_TYPES {
	SPEED_MPH,
	SPEED_KNOTS,
	SPEED_MS,
	SPEED_KPH,
};

enum LIGHT_TYPES {
	LIGHT_LUX,
	LIGHT_WM2,
};

enum WEATHER_TYPES {
	WEATHER_SUNNY,
	WEATHER_PARTCLOUD,
	WEATHER_CLOUDY,
	WEATHER_RAINY,
};

enum ALARMSTATUS_TYPES {
	ALARM_READY,
	ALARM_STAY,
	ALARM_NIGHTSTAY,
	ALARM_INSTANTMAX,
	ALARM_AWAY,
	ALARM_FAULT,
};

enum THERMOSTAT_MODES {
	THERMMODE_OFF,
	THERMMODE_HEAT,
	THERMMODE_COOL,
	THERMMODE_AUTO,
};

enum THERMOSTAT_STATE_TYPES {
	THERMSTATE_IDLE,
	THERMSTATE_HEATING,
	THERMSTATE_COOLING,
	THERMSTATE_LOCKOUT,
	THERMSTATE_ERROR,
};

enum SALINITY_TYPES {
	SALINITY_PPT,
	SALINITY_SG,
	SALINITY_COND, /* mS */
};

struct _device_group_t;
struct _client_t;
struct _device_t;
struct _wrap_client_t;
struct _wrap_device_t;
struct _wrap_group_t;

enum NAME_MAP_LIST {
	NAME_MAP_PROTO,
	NAME_MAP_TYPE,
	NAME_MAP_SUBTYPE,
};

typedef struct _name_map_t {
	int id;
	char *name;
} name_map_t;

/** The client type, used to store data for events */
typedef struct _client_t {
	int fd;		/**< \brief file descriptor */
	int provider;	/**< \brief is this client a device provider? */
	pid_t pid;	/**< \brief pid, if handler */
	TAILQ_HEAD(, _device_t) devices;  /**< \brief linked list of devices it provides */
	TAILQ_HEAD(, _wrap_device_t) wdevices; /**< \brief linked list of non-provided devices */
	struct bufferevent *ev;	/**< \brief the bufferevent */
	struct event *tev;	/**< \brief a timer event */
	int timer_gcd;		/**< \brief current gcd of the timer */
	SSL_CTX *srv_ctx;	/**< \brief server context */
	SSL *cli_ctx;		/**< \brief client context */
	int close_on_empty;	/**< \brief close this connection on empty */
	char *name;		/**< \brief Name of client */
	char *addr;		/**< \brief addr:port of client */
	char *host;		/**< \brief just the host */
	int port;		/**< \brief just the port */
	uint32_t updates;	/**< \brief updates recieved from this cli */
	uint32_t feeds;		/**< \brief feeds for this cli */
	uint32_t watched;	/**< \brief watched device count */
	uint32_t sentdata;	/**< \brief data sent to this cli */
	time_t lastupd;		/**< \brief last time we were talked to */
	int alarmwatch;		/**< \brief min sev of alarms we want, 0 disables */
	uint32_t alchan;	/**< \brief alarm channels we watch */
	struct _device_t *coll_dev;	/**< \brief the dev for the collector itself */
	TAILQ_ENTRY(_client_t) next; /**< \brief next client on list */
} client_t;

/* matches struct device */
enum DATALOC_TYPES {
	DATALOC_DATA,
	DATALOC_LAST,
	DATALOC_MIN,
	DATALOC_MAX,
	DATALOC_AVG,
	DATALOC_LOWAT,
	DATALOC_HIWAT,
	DATALOC_CHANGE,
};

/* matches data_t */
enum DATATYPE_TYPES {
	DATATYPE_UINT,
	DATATYPE_DOUBLE,
	DATATYPE_LL,
};

/* device queue flags */

#define DEVONQ_CLIENT	(1<<1)
#define DEVONQ_ALL	(1<<2)
#define WRAPONQ_NEXT	(1<<1)
#define GROUPONQ_NEXT	(1<<1)
#define GROUPONQ_ALL	(1<<2)
#define GROUPONQ_REG	(1<<3)	/**< \brief temp flag to record registration */

/* device flags */

#define DEVFLAG_SPAMHANDLER	(1<<1)	/**< \brief do we spam the handler? */
#define DEVFLAG_NODATA		(1<<2)	/**< \brief device has no cur data */
#define DEVFLAG_CHANGEHANDLER	(1<<3)	/**< \brief fire when device changes */

/* Flags (new method) for alarm channels See common.h SET_FLAG macros */

#define ACHAN_GENERIC	0	/**< \brief Generic default channel */
#define ACHAN_POWER	1	/**< \brief Power related */
#define ACHAN_LIGHTS	2	/**< \brief Lights and switches */
#define ACHAN_SECURE	3	/**< \brief Security/alarm panel */
#define ACHAN_WEATHER	4	/**< \brief Weather related */
#define ACHAN_AC	5	/**< \brief Air conditioning */
#define ACHAN_YARD	6	/**< \brief Lawn/yard */
#define ACHAN_GNHAST	7	/**< \brief Gnhast/collectors */
#define ACHAN_SYSTEM	8	/**< \brief Underlying OS stuff */
#define ACHAN_EMERG	9	/**< \brief Emergency Alerts */
#define ACHAN_MESSAGING	10	/**< \brief Text messaging facility */
/* Define new ones here */
#define ACHAN_USER1	24	/**< \brief User defined channel */
#define ACHAN_USER2	25	/**< \brief User defined channel */
#define ACHAN_USER3	26	/**< \brief User defined channel */
#define ACHAN_USER4	27	/**< \brief User defined channel */
#define ACHAN_USER5	28	/**< \brief User defined channel */
#define ACHAN_USER6	29	/**< \brief User defined channel */
#define ACHAN_USER7	30	/**< \brief User defined channel */
#define ACHAN_USER8	31	/**< \brief User defined channel */


/** data union */
typedef union _data_t {
	uint8_t state;	/**< \brief on or off */
	uint32_t count; /**< \brief counter */
	int64_t wattsec;/**< \brief wattseconds */
	double temp;	/**< \brief Temperature */
	double humid;	/**< \brief Humidity */
	double lux;	/**< \brief Lux */
	double pressure;/**< \brief Pressure */
	double speed;   /**< \brief speed (wind) */
	double dir;     /**< \brief direction (wind) */
	double level;	/**< \brief level (for dimmers) */
	double wetness;	/**< \brief wetness */
	double ph;	/**< \brief pH */
	double volts;	/**< \brief voltage */
	double watts;	/**< \brief watts */
	double amps;	/**< \brief amps */
	double rainrate;/**< \brief rain rate */
	double distance;/**< \brief distance */
	double volume;	/**< \brief volume */
	double flow;	/**< \brief flow */
	int64_t number;	/**< \brief generic number */
	double d;	/**< \brief Generic double */
	uint32_t ui;	/**< \brief Generic uint32 */
	int64_t ll;	/**< \brief Generic int64 */
} data_t;

/** The device structure */
typedef struct _device_t {
	char *uid;		/**< \brief Unique Identifier */
	char *loc;		/**< \brief Locator */
	char *name;		/**< \brief Friendly Name */
	char *rrdname;		/**< \brief Name for rrd */
	uint8_t proto;		/**< \brief protocol */
	uint8_t type;		/**< \brief Type */
	uint8_t subtype;	/**< \brief sub-type */
	uint8_t scale;		/**< \brief scale (temp/baro/etc) */
	data_t data;		/**< \brief current data */
	data_t last;		/**< \brief previous data */
	data_t min;		/**< \brief 24h min */
	data_t max;		/**< \brief 24h max */
	data_t avg;		/**< \brief 24h avg */
	data_t lowat;		/**< \brief low water mark */
	data_t hiwat;		/**< \brief high water mark */
	data_t change;		/**< \brief data requested to be changed */
	client_t *collector;	/**< \brief The collector that serves this data up */
	char *handler;		/**< \brief our external handler */
	char **hargs;		/**< \brief handler arguments */
	int nrofhargs;		/**< \brief number of handler arguments */
	void *localdata;	/**< \brief pointer to program-specific data */
	time_t last_upd;	/**< \brief time of last update */
	struct rb_node rbn;	/**< \brief red black node for dev->uid */
	uint32_t onq;		/**< \brief I am on a queue */
	uint32_t flags;		/**< \brief DEVFLAG_* */
	TAILQ_ENTRY(_device_t) next_client;	/**< \brief Next device in client */
	TAILQ_ENTRY(_device_t) next_all;	/**< \brief Next in global devlist */
	TAILQ_HEAD(, _wrap_client_t) watchers;  /**< \brief linked list of clients watching this device */
} device_t;

/** A wrapper client structure */
typedef struct _wrap_client_t {
	client_t *client;	/**< \brief wrapped client */
	TAILQ_ENTRY(_wrap_client_t) next; /**< \brief next client */
} wrap_client_t;

/** A wrapper device structure */
typedef struct _wrap_device_t {
	device_t *dev;		/**< \brief wrapped device */
	int rate;		/**< \brief rate of fire */
	int scale;		/**< \brief data scale (temp/baro/etc) */
	struct _device_group_t *group; /**< \brief parent group */
	time_t last_fired;	/**< \brief last time fired */
	uint32_t onq;		/**< \brief I am on a queue */
	TAILQ_ENTRY(_wrap_device_t) next; /**< \brief next device */
} wrap_device_t;

/** A wrapper device group structure */
typedef struct _wrap_group_t {
	struct _device_group_t *group;	/**< \brief wrapped device group */
	struct _device_group_t *parent;	/**< \brief parent group */
	uint32_t onq;		/**< \brief I am on a queue */
	TAILQ_ENTRY(_wrap_group_t) nextg; /**< \brief next group */
} wrap_group_t;

/** A device group */
typedef struct _device_group_t {
	char *uid;		/**< \brief Unique group id */
	char *name;		/**< \brief group name */
	struct rb_node rbn;	/**< \brief red black node */
	int subgroup;		/**< \brief is a child of another group */
	uint32_t onq;		/**< \brief I am on a queue */
	TAILQ_HEAD(, _wrap_device_t) members; /**< \brief group members */
	TAILQ_HEAD(, _wrap_group_t) children; /**< \brief child groups */
	TAILQ_ENTRY(_device_group_t) next_all;  /**< \brief next in global */
} device_group_t;

/** An alarm */
typedef struct _alarm_t {
	char *aluid;	/**< \brief alarm uid */
	char *altext;	/**< \brief alarm text */
	int alsev;	/**< \brief alarm sev, 0 clears alarm */
	uint32_t alchan;	/**< \brief alarm channel (BITFLAG) */
	TAILQ_ENTRY(_alarm_t) next; /**< \brief next in global alarm list */
} alarm_t;

#endif /* _GNHAST_H_ */
