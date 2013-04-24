/* $Id$ */

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
#include <sys/rbtree.h>

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

#define GNHASTD_CONFIG_FILE	"gnhastd.conf"
#define GNHASTD_LOG_FILE	"gnhastd.log"

/* Basic device types */
enum DEV_TYPES {
	DEVICE_NONE,
	DEVICE_SWITCH,
	DEVICE_DIMMER,
	DEVICE_SENSOR,
};
#define DEVICE_MAX DEVICE_SENSOR

enum PROTO_TYPES {
	PROTO_NONE,
	PROTO_INSTEON_V1,
	PROTO_INSTEON_V2,
	PROTO_INSTEON_V2CS,
	PROTO_SENSOR_OWFS,
};
#define PROTO_MAX PROTO_SENSOR_OWFS

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
	SUBTYPE_MOISTURE,
	SUBTYPE_WETNESS,
	SUBTYPE_HUB,
	SUBTYPE_LUX,
	SUBTYPE_BOOL,
};
#define SUBTYPE_MAX SUBTYPE_BOOL
/* used in owsrv to limit which sensors we can talk about
   add new sensor types between these */
#define SUBTYPE_OWSRV_SENSOR_MIN SUBTYPE_TEMP
#define SUBTYPE_OWSRV_SENSOR_MAX SUBTYPE_LUX

struct _device_group_t;
struct _client_t;
struct _device_t;
struct _wrap_device_t;

/** The client type, used to store data for events */
typedef struct _client_t {
	int fd;		/**< \brief file descriptor */
	int provider;	/**< \brief is this client a device provider? */
	TAILQ_HEAD(, _device_t) devices;  /**< \brief linked list of devices it provides */
	TAILQ_HEAD(, _wrap_device_t) wdevices; /**< \brief linked list of non-provided devices */
	struct bufferevent *ev;	/**< \brief the bufferevent */
	struct event *tev;	/**< \brief a timer event */
	int timer_gcd;		/**< \brief current gcd of the timer */
	SSL_CTX *srv_ctx;	/**< \brief server context */
	SSL *cli_ctx;		/**< \brief client context */
} client_t;

/* matches struct device */
enum DATALOC_TYPES {
	DATALOC_DATA,
	DATALOC_MIN,
	DATALOC_MAX,
	DATALOC_AVG,
	DATALOC_LOWAT,
	DATALOC_HIWAT,
};

/* matches data_t */
enum DATATYPE_TYPES {
	DATATYPE_UINT,
	DATATYPE_DOUBLE,
};

/** data union */
typedef union _data_t {
	uint8_t state;	/**< \brief on or off */
	uint32_t count; /**< \brief counter */
	double temp;	/**< \brief Temperature */
	double humid;	/**< \brief Humidity */
	double lux;	/**< \brief Lux */
	double pressure;/**< \brief Pressure */
	double speed;   /**< \brief speed (wind) */
	double dir;     /**< \brief direction (wind) */
	double level;	/**< \brief level (for dimmers) */
	double wetness;	/**< \brief wetness */
	double moisture;/**< \brief moisture level */
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
	data_t data;		/**< \brief data is current */
	data_t min;		/**< \brief 24h min */
	data_t max;		/**< \brief 24h max */
	data_t avg;		/**< \brief 24h avg */
	data_t lowat;		/**< \brief low water mark */
	data_t hiwat;		/**< \brief high water mark */
	client_t *collector;	/**< \brief The collector that serves this data up */
	char *handler;		/**< \brief our external handler */
	void *localdata;	/**< \brief pointer to program-specific data */
	time_t last_upd;	/**< \brief time of last update */
	struct rb_node rbn;	/**< \brief red black node */
	TAILQ_ENTRY(_device_t) next_client;	/**< \brief Next device in client */
	TAILQ_ENTRY(_device_t) next_peer;	/**< \brief Next peer in devgroup */
	TAILQ_ENTRY(_device_t) next_all;	/**< \brief Next in global devlist */
} device_t;

/** A wrapper device structure */
typedef struct _wrap_device_t {
	int rate;
	device_t *dev;
	time_t last_fired;
	TAILQ_ENTRY(_wrap_device_t) next; /**< \brief next device */
} wrap_device_t;

/** A device group */
typedef struct _device_group_t {
	char *uid;		/**< \brief Unique group id */
	char *name;		/**< \brief group name */
	struct _device_group_t *parent;	/**< \brief parent devgroup */
	TAILQ_HEAD(, _device_t) members; /**< \brief group members, ties to next_peer in device_t */
	TAILQ_HEAD(, _device_group_t) children; /**< \brief child groups */
	TAILQ_ENTRY(_device_group_t) next;	/**< \brief my siblings */
} device_group_t;

#endif /* _GNHAST_H_ */
