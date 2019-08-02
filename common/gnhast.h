/*
 * Copyright (c) 2013, 2016, 2017
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
    \file gnhast.h
    \author Tim Rightnour
    \brief The main defines and structures for everything
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
/* Bump this whenever you add a new command, type, subtype, or proto */
#define GNHASTD_PROTO_VERS	0x12

/** Basic device types */
/** \note a type blind should always return BLIND_STOP, for consistency */
enum DEV_TYPES {
    DEVICE_NONE,	/**< No device, don't use */
    DEVICE_SWITCH,	/**< A switch on/off device */
    DEVICE_DIMMER,	/**< A dimmer */
    DEVICE_SENSOR,	/**< A device that is read-only */
    DEVICE_TIMER,	/**< A timer */
    DEVICE_BLIND, 	/**< can go up or down, but cannot be queried */
    NROF_TYPES,		/**< Used for looping, never as a type */
};
/* Update below when adding new device types */
#define DEVICE_MAX DEVICE_BLIND

/**
   The protocol types, used mainly for collectors

   \note
    The following files must be changed when adding a protocol:
    common/devices.c
    py-gnhast/gnhast/gnhast.py
*/
enum PROTO_TYPES {
    PROTO_NONE, /**< 0 No protocol */
    PROTO_INSTEON_V1, /**< 1 A version 1 insteon device */
    PROTO_INSTEON_V2, /**< 2 A version 2 insteon device */
    PROTO_INSTEON_V2CS, /**< 3 A version 2 insteon device with checksum */
    PROTO_SENSOR_OWFS, /**< 4 An OWFS presented device */
    PROTO_SENSOR_BRULTECH_GEM, /**< 5 A brultech GEM */
    PROTO_SENSOR_BRULTECH_ECM1240, /**< 6 A brultech ECM1240 */
    PROTO_SENSOR_WMR918, /**< 7 A WMR918 weather station */
    PROTO_SENSOR_AD2USB, /**< 8 An ad2usb ademco alarm integration */
    PROTO_SENSOR_ICADDY, /**< 9 Irrigation caddy */
    PROTO_SENSOR_VENSTAR, /**< 10 Venstar Thermostat */
    PROTO_CONTROL_URTSI, /**< 11 Somfy URTSI blind controller */
    PROTO_COLLECTOR, /**< 12 A generic collector */
    PROTO_CAMERA_AXIS, /**< 13 An AXIS camera */
    PROTO_TUXEDO, /**< 14 Honeywell Tuxedo Touch */
    PROTO_NEPTUNE_APEX, /**< 15 Neptune Apex aquarium controller */
    PROTO_CALCULATED, /**< 16 for calculated values */
    PROTO_BALBOA, /**< 17 A balboa spa wifi controller */
    PROTO_GENERIC, /**< 18 A Generic catch-all */
    PROTO_UNUSED1,
    PROTO_UNUSED2, /*20*/
    PROTO_UNUSED3,
    PROTO_UNUSED4,
    PROTO_UNUSED5,
    PROTO_UNUSED6,
    PROTO_UNUSED7,
    PROTO_UNUSED8,
    PROTO_UNUSED9,
    PROTO_UNUSED10, /*28*/
    PROTO_UNUSED11,
    PROTO_UNUSED12,
    PROTO_UNUSED13,
    PROTO_UNUSED14, /*32*/
    /* At this point, starting over, with use based protocols */
    PROTO_MAINS_SWITCH, /**< 33 A mains switch/outlet/otherwise */
    PROTO_WEATHER, /**< 34 Weather data */
    PROTO_SENSOR, /**< 35 Any random sensor */
    PROTO_SENSOR_INDOOR, /**< 36 A sensor for indoor data collection */
    PROTO_SENSOR_OUTDOOR, /**< 37 A sensor for outdoor data collection */
    PROTO_SENSOR_ELEC, /**< 38 A sensor that monitors electronics */
    PROTO_POWERUSE, /**< 39 Power usage data */
    PROTO_IRRIGATION, /**< 40 Irrigation data */
    PROTO_ENTRY, /**< 41 entry/egress, like a door, window, etc */
    PROTO_ALARM, /**< 42 An alarm of some kind */
    PROTO_BATTERY, /**< 43 Battery monitor */
    PROTO_CAMERA, /**< 44 A camera */
    PROTO_AQUARIUM, /**< 45 Aquarium stuff */
    PROTO_SOLAR, /**< 46 Solar power */
    PROTO_POOL, /**< 47 Pools, spas */
    PROTO_WATERHEATER, /**< 48 Water heaters */
    PROTO_LIGHT, /**< 49 Lights (bulbs, leds, whatever) */
    PROTO_AV, /**< 50 Audio/video equipment */
    PROTO_THERMOSTAT, /**< 51 Thermostat data */
    PROTO_SETTINGS, /**< 52 Device settings/config */
    PROTO_BLIND, /**< 53 Blinds, shutters, etc */
    NROF_PROTOS,
};
/* Update when adding a new protocol */
#define PROTO_MAX PROTO_BLIND

/**
   Subtypes

   \note
    The following files must be updated when adding a subtype:
    common/netparser.c
    common/devices.c
    common/confparser.c (if scaled type, like pressure)
    common/gncoll.c (if scaled type, like pressure)
    gnhastd/cmdhandler.c
    common/commands.h (SC_ instances are here)
    common/collcmd.c
    Note, you must also update the SC_ instances.
    GNHASTD_PROTO_VERS (up above)

   \todo Change how these are stored internally
*/

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
	SUBTYPE_DAYLIGHT, /* daylight (8 bit number stored in state) */
	SUBTYPE_MOONPH, /* lunar phase (d) */
	SUBTYPE_TRISTATE, /* tri-state device 0,1,2 stored in state) */
	SUBTYPE_BOOL, /* Never actually use this one, just for submax */
	NROF_SUBTYPES,
};
#define SUBTYPE_MAX SUBTYPE_BOOL
/* used in owsrv to limit which sensors we can talk about
   add new sensor types between these */
#define SUBTYPE_OWSRV_SENSOR_MIN SUBTYPE_TEMP
#define SUBTYPE_OWSRV_SENSOR_MAX SUBTYPE_LUX

/** Different daylight/night defines */

enum DAYLIGHT_TYPES {
    DAYL_NIGHT, /*0*/			/**< Night */
    DAYL_DAY, /*1*/			/**< Day */
    DAYL_SOLAR_NOON, /*2*/		/**< Solar noon*/
    DAYL_DAWN_ASTRO_TWILIGHT, /*3*/	/**< Astronomical Twilight (dawn) */
    DAYL_DAWN_NAUTICAL_TWILIGHT, /*4*/	/**< Nautical Twilight (dawn) */
    DAYL_DAWN_CIVIL_TWILIGHT, /*5*/	/**< Civil Twilight (dawn) */
    DAYL_DUSK_CIVIL_TWILIGHT, /*6*/	/**< Civil Twilight (dusk) */
    DAYL_DUSK_NAUTICAL_TWILIGHT, /*7*/	/**< Nautical Twilight (dusk) */
    DAYL_DUSK_ASTRO_TWILIGHT, /*8*/	/**< Astronomical Twilight (dusk) */
};

/** Blind commands, up, down, stop */

enum BLIND_TYPES {
    BLIND_UP,	/**< Up */
    BLIND_DOWN,	/**< Down */
    BLIND_STOP,	/**< Stop */
};

/** Collector status */
enum COLLECTOR_TYPES {
    COLLECTOR_BAD,	/**< Collector is broken */
    COLLECTOR_OK,	/**< Collector is ok */
};

/** Temperature scales */
enum TSCALE_TYPES {
    TSCALE_F, /**< Farenheit */
    TSCALE_C, /**< Celcius */
    TSCALE_K, /**< Kelvin */
    TSCALE_R, /**< Rankine */
};

/** Barometer scales */
enum BAROSCALE_TYPES {
    BAROSCALE_IN, /**< Inches (Hg)*/
    BAROSCALE_MM, /**< Millimeters (Hg) */
    BAROSCALE_MB, /**< Millibars */
    BAROSCALE_CB, /**< Centibars */
};

/** Length */
enum LENGTH_TYPES {
    LENGTH_IN, /**< Inches */
    LENGTH_MM, /**< Millimeters */
};

/** Speed */
enum SPEED_TYPES {
    SPEED_MPH, /**< Miles per Hour */
    SPEED_KNOTS, /**< Knots */
    SPEED_MS, /**< Meters per second */
    SPEED_KPH, /**< Kilometers per hour */
};

/** Light */
enum LIGHT_TYPES {
    LIGHT_LUX, /**< Lux */
    LIGHT_WM2, /**< Watts per square meter */
};

/** Weather types, used for WMR918 weather station */
enum WEATHER_TYPES {
    WEATHER_SUNNY,	/**< Sunny */
    WEATHER_PARTCLOUD,	/**< Partly cloudy */
    WEATHER_CLOUDY,	/**< Cloudy */
    WEATHER_RAINY,	/**< Rainy */
};

/** Ademco alarm states */
enum ALARMSTATUS_TYPES {
    ALARM_READY,	/**< Alarm Ready */
    ALARM_STAY,		/**< Stay */
    ALARM_NIGHTSTAY,	/**< Stay/Night */
    ALARM_INSTANTMAX,	/**< INSTANTMAX (high alert) */
    ALARM_AWAY,		/**< AWAY */
    ALARM_FAULT,	/**< A Fault */
};

/** Thermostat modes */
enum THERMOSTAT_MODES {
    THERMMODE_OFF,	/**< Off */
    THERMMODE_HEAT,	/**< Heat */
    THERMMODE_COOL,	/**< Cool */
    THERMMODE_AUTO,	/**< Automatic */
};

/** Thermostat states */
enum THERMOSTAT_STATE_TYPES {
    THERMSTATE_IDLE,	/**< Idle */
    THERMSTATE_HEATING,	/**< Heating */
    THERMSTATE_COOLING,	/**< Cooling */
    THERMSTATE_LOCKOUT,	/**< Panel Lockout*/
    THERMSTATE_ERROR,	/**< Broken */
};

/** Salinity */
enum SALINITY_TYPES {
    SALINITY_PPT, /**< Parts per thousand */
    SALINITY_SG, /**< Specific gravity */
    SALINITY_COND, /**< Conductivity mS */
};

struct _device_group_t;
struct _client_t;
struct _device_t;
struct _wrap_client_t;
struct _wrap_device_t;
struct _wrap_group_t;

/** Used in name_map_t to map names */
enum NAME_MAP_LIST {
    NAME_MAP_PROTO,	/**< Map of prototypes */
    NAME_MAP_TYPE,	/**< Map of types */
    NAME_MAP_SUBTYPE,	/**< Map of subtypes */
};

/** \brief Map a name to a type/subtype/prototype */
typedef struct _name_map_t {
    int id;	/**< \brief the id one of NAME_MAP_LIST */
    char *name;	/**< \brief The string that is mapped */
} name_map_t;

/** \brief The client type, used to store data for events */
typedef struct _client_t {
    int fd;		/**< \brief file descriptor */
    int provider;	/**< \brief is this client a device provider? */
    pid_t pid;		/**< \brief pid, if handler */
    TAILQ_HEAD(, _device_t) devices;  /**< \brief linked list of devices it provides */
    TAILQ_HEAD(, _wrap_device_t) wdevices; /**< \brief linked list of non-provided devices */
    struct bufferevent *ev;	/**< \brief the bufferevent */
    struct event *tev;	/**< \brief a timer event */
    int timer_gcd;	/**< \brief current gcd of the timer */
    SSL_CTX *srv_ctx;	/**< \brief server context */
    SSL *cli_ctx;	/**< \brief client context */
    int close_on_empty;	/**< \brief close this connection on empty */
    char *name;		/**< \brief Name of client */
    char *addr;		/**< \brief addr:port of client */
    char *host;		/**< \brief just the host */
    int port;		/**< \brief just the port */
    uint32_t updates;	/**< \brief updates recieved from this cli */
    uint32_t feeds;	/**< \brief feeds for this cli */
    uint32_t watched;	/**< \brief watched device count */
    uint32_t sentdata;	/**< \brief data sent to this cli */
    time_t lastupd;	/**< \brief last time we were talked to */
    int alarmwatch;	/**< \brief min sev of alarms we want, 0 disables */
    uint32_t alchan;	/**< \brief alarm channels we watch */
    struct _device_t *coll_dev;	/**< \brief the dev for the collector itself */
    TAILQ_ENTRY(_client_t) next; /**< \brief next client on list */
} client_t;

/** matches struct device data_t types*/
enum DATALOC_TYPES {
    DATALOC_DATA, /**< The current Data */
    DATALOC_LAST, /**< The previous data */
    DATALOC_LOWAT, /**< A low watermark */
    DATALOC_HIWAT, /**< A high watermark */
    DATALOC_CHANGE, /**< used to request a change to a device's data */
};

/** matches data_t
    \todo Need uint8_t type!!
*/
enum DATATYPE_TYPES {
    DATATYPE_UINT, /**< Unsigned int */
    DATATYPE_DOUBLE, /**< double */
    DATATYPE_LL, /**< uint64_t */
};

/* device queue flags */

#define DEVONQ_CLIENT	(1<<1)	/**< Device is on a client queue*/
#define DEVONQ_ALL	(1<<2)	/**< Device is on all queues*/
#define WRAPONQ_NEXT	(1<<1)	/**< Next wrapdev */
#define GROUPONQ_NEXT	(1<<1)	/**< next groupq */
#define GROUPONQ_ALL	(1<<2)	/**< all group queues*/
#define GROUPONQ_REG	(1<<3)	/**< \brief temp flag to record registration */

/* device flags (new method) */

#define DEVFLAG_SPAMHANDLER	0  /**< \brief do we spam the handler? */
#define DEVFLAG_NODATA		1  /**< \brief device has no cur data */
#define DEVFLAG_CHANGEHANDLER	2  /**< \brief fire when device changes */

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


/**
   \brief The data storage union
   \todo Simplify into 4 types
*/
typedef union _data_t {
    uint8_t state;	/**< \brief on or off */
    uint32_t count;	/**< \brief counter */
    int64_t wattsec;	/**< \brief wattseconds */
    double temp;	/**< \brief Temperature */
    double humid;	/**< \brief Humidity */
    double lux;		/**< \brief Lux */
    double pressure;	/**< \brief Pressure */
    double speed;	/**< \brief speed (wind) */
    double dir;		/**< \brief direction (wind) */
    double level;	/**< \brief level (for dimmers) */
    double wetness;	/**< \brief wetness */
    double ph;		/**< \brief pH */
    double volts;	/**< \brief voltage */
    double watts;	/**< \brief watts */
    double amps;	/**< \brief amps */
    double rainrate;	/**< \brief rain rate */
    double distance;	/**< \brief distance */
    double volume;	/**< \brief volume */
    double flow;	/**< \brief flow */
    int64_t number;	/**< \brief generic number */
    double d;		/**< \brief Generic double */
    uint32_t ui;	/**< \brief Generic uint32 */
    int64_t ll;		/**< \brief Generic int64 */
} data_t;

/** \brief The device structure */
typedef struct _device_t {
    char *uid;		/**< \brief Unique Identifier */
    char *loc;		/**< \brief Locator */
    char *name;		/**< \brief Friendly Name */
    char *rrdname;	/**< \brief Name for rrd */
    uint8_t proto;	/**< \brief protocol */
    uint8_t type;	/**< \brief Type */
    uint8_t subtype;	/**< \brief sub-type */
    uint8_t scale;	/**< \brief scale (temp/baro/etc) */
    data_t data;	/**< \brief current data */
    data_t last;	/**< \brief previous data */
    data_t lowat;	/**< \brief low water mark */
    data_t hiwat;	/**< \brief high water mark */
    data_t change;	/**< \brief data requested to be changed */
    client_t *collector;/**< \brief The collector that serves this data up */
    char *handler;	/**< \brief our external handler */
    char **hargs;	/**< \brief handler arguments */
    int nrofhargs;	/**< \brief number of handler arguments */
    char **tags;        /**< \brief tags */
    int nroftags;	/**< \brief number of tags */
    void *localdata;	/**< \brief pointer to program-specific data */
    time_t last_upd;	/**< \brief time of last update */
    struct rb_node rbn;	/**< \brief red black node for dev->uid */
    uint32_t onq;	/**< \brief I am on a queue */
    uint32_t flags;	/**< \brief DEVFLAG_* */
    TAILQ_ENTRY(_device_t) next_client;	/**< \brief Next device in client */
    TAILQ_ENTRY(_device_t) next_all;	/**< \brief Next in global devlist */
    TAILQ_HEAD(, _wrap_client_t) watchers;  /**< \brief linked list of clients watching this device */
} device_t;

/** \brief A wrapper client structure */
typedef struct _wrap_client_t {
    client_t *client;	/**< \brief wrapped client */
    TAILQ_ENTRY(_wrap_client_t) next; /**< \brief next client */
} wrap_client_t;

/** \brief A wrapper device structure */
typedef struct _wrap_device_t {
    device_t *dev;	/**< \brief wrapped device */
    int rate;		/**< \brief rate of fire */
    int scale;		/**< \brief data scale (temp/baro/etc) */
    struct _device_group_t *group; /**< \brief parent group */
    time_t last_fired;	/**< \brief last time fired */
    uint32_t onq;	/**< \brief I am on a queue */
    TAILQ_ENTRY(_wrap_device_t) next; /**< \brief next device */
} wrap_device_t;

/** \brief A wrapper device group structure */
typedef struct _wrap_group_t {
    struct _device_group_t *group;	/**< \brief wrapped device group */
    struct _device_group_t *parent;	/**< \brief parent group */
    uint32_t onq;			/**< \brief I am on a queue */
    TAILQ_ENTRY(_wrap_group_t) nextg;	/**< \brief next group */
} wrap_group_t;

/** \brief A device group */
typedef struct _device_group_t {
    char *uid;			/**< \brief Unique group id */
    char *name;			/**< \brief group name */
    struct rb_node rbn;		/**< \brief red black node */
    int subgroup;		/**< \brief is a child of another group */
    uint32_t onq;		/**< \brief I am on a queue */
    TAILQ_HEAD(, _wrap_device_t) members; /**< \brief group members */
    TAILQ_HEAD(, _wrap_group_t) children; /**< \brief child groups */
    TAILQ_ENTRY(_device_group_t) next_all; /**< \brief next in global */
} device_group_t;

/** \brief An alarm */
typedef struct _alarm_t {
    char *aluid;	/**< \brief alarm uid */
    char *altext;	/**< \brief alarm text */
    int alsev;		/**< \brief alarm sev, 0 clears alarm */
    uint32_t alchan;	/**< \brief alarm channel (BITFLAG) */
    TAILQ_ENTRY(_alarm_t) next; /**< \brief next in global alarm list */
} alarm_t;

#endif /* _GNHAST_H_ */
