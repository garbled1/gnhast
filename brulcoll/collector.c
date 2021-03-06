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

/**
   \file brulcoll/collector.c
   \author Tim Rightnour
   \brief Brultech collector
   This collector connects to a brultech GEM, and relays the data to gnhastd
   Currently, only GEM devices with ethernet are supported.
   There is basic support for reading an ecm1240, but only tested via
   GEM emulation, need code to actually setup the ecm and build devices.

   NOGENCONN
   This collector does not use the generic connection routines.
*/

/*#define WIZNET_DEBUG*/

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <termios.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <event2/dns.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/util.h>

#include "config.h"
#ifdef HAVE_BSD_STDLIB_H
#include <bsd/stdlib.h>
#endif
#include "common.h"
#include "gnhast.h"
#include "confuse.h"
#include "brultech.h"
#include "confparser.h"
#include "gncoll.h"
#include "collcmd.h"

void connect_event_cb(struct bufferevent *ev, short what, void *arg);
void brul_netconnect_event_cb(struct bufferevent *ev, short what, void *arg);
void connect_server_cb(int nada, short what, void *arg);
static int conf_parse_brul_model(cfg_t *cfg, cfg_opt_t *opt, const char *value,
				 void *result);
static int conf_parse_brul_conn(cfg_t *cfg, cfg_opt_t *opt, const char *value,
				void *result);
void cb_wiznet_send_direct(int fd, short what, void *arg);

char *conffile = SYSCONFDIR "/" BRULCOLL_CONFIG_FILE;
FILE *logfile;   /** our logfile */
extern int debugmode;
cfg_t *cfg, *gnhastd_c, *brultech_c, *brulcoll_c;
uint32_t loopnr; /**< \brief the number of loops we've made */
char *dumpconf = NULL;
int need_rereg = 0;
int indian = 1;
int tempscale = TSCALE_F;
time_t brul_lastupd;


/* stuff for gem/wiznet reset */
int gemconnattempts = 0;
int wiz_fd = -1;
char gemipaddrstr[16];
int gemipaddr[4];
wiznet_t wizconf; /**< \brief Wiznet conf string */
int wiznetfound = 0;
char *wizresetbuf;

bruldata_t bruldata[2];	/**< \brief Prev and current data, converted */
brulconf_t brulconf;

/** Need the argtable in scope, so we can generate proper commands
    for the server */
extern argtable_t argtable[];
extern TAILQ_HEAD(, _device_t) alldevs;
extern int collector_instance;
extern struct bufferevent *gnhastd_bev;

/** The event base */
struct event_base *base;
struct evdns_base *dns_base;

#define CONN_TYPE_BRUL     	1
#define CONN_TYPE_GNHASTD	2
char *conntype[3] = {
	"none",
	"brultechGEM",
	"gnhastd",
};

typedef struct _connection_t {
	int port;
	int type;
	int32_t tempbase;
	int mode;
	int pkttype;
	char *host;
	struct bufferevent *bev;
	device_t *current_dev;
	time_t lastdata;
	int shutdown;
} connection_t;

/** The connection streams for our two connections */
connection_t *gnhastd_conn, *brulnet_conn;

/* Configuration file setup */

extern cfg_opt_t device_opts[];

cfg_opt_t brultech_opts[] = {
	CFG_STR("hostname", "127.0.0.1", CFGF_NONE),
	CFG_INT("port", 80, CFGF_NONE),
	CFG_INT_CB("model", BRUL_MODEL_GEM, CFGF_NONE, conf_parse_brul_model),
	CFG_INT_CB("connection", BRUL_COMM_NET, CFGF_NONE,
		   conf_parse_brul_conn),
	CFG_STR("serialdev", "/dev/dty01", CFGF_NONE),
	CFG_INT("instance", 1, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t gnhastd_opts[] = {
	CFG_STR("hostname", "127.0.0.1", CFGF_NONE),
	CFG_INT("port", 2920, CFGF_NONE),
	CFG_END(),
};

cfg_opt_t brulcoll_opts[] = {
	CFG_INT_CB("tscale", TSCALE_F, CFGF_NONE, conf_parse_tscale),
	CFG_INT("update", 10, CFGF_NONE),
	CFG_INT("pkttype", BRUL_PKT_32NET, CFGF_NONE),	
	CFG_END(),
};

cfg_opt_t options[] = {
	CFG_SEC("brultech", brultech_opts, CFGF_NONE),
	CFG_SEC("gnhastd", gnhastd_opts, CFGF_NONE),
	CFG_SEC("brulcoll", brulcoll_opts, CFGF_NONE),
	CFG_SEC("device", device_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_STR("logfile", BRULCOLL_LOG_FILE, CFGF_NONE),
	CFG_STR("pidfile", BRULCOLL_PID_FILE, CFGF_NONE),
	CFG_END(),
};

/*****
      Brultech General Functions
*****/

/**
   \brief parse brultech model type
   \param cfg the config base
   \param opt the option we are parsing
   \param the value of the option
   \param result result of option parsing will be stored here
   \return success
*/

static int conf_parse_brul_model(cfg_t *cfg, cfg_opt_t *opt, const char *value,
				 void *result)
{
	if (strcasecmp(value, "gem") == 0)
		*(int *)result = BRUL_MODEL_GEM;
	else if (strcasecmp(value, "ecm1240") == 0)
		*(int *)result = BRUL_MODEL_ECM1240;
	else {
		cfg_error(cfg, "invalid value for option '%s': %s",
		    cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}

/**
   \brief Used to print brultech model
   \param opt option structure
   \param index number of option to print
   \param fp passed FILE
*/

static void conf_print_brul_model(cfg_opt_t *opt, unsigned int index, FILE *fp)
{
	switch (cfg_opt_getnint(opt, index)) {
	case BRUL_MODEL_GEM:
		fprintf(fp, "gem");
		break;
	case BRUL_MODEL_ECM1240:
		fprintf(fp, "ecm1240");
		break;
	default:
		fprintf(fp, "UNKNOWN-FIXME");
		break;
	}
}

/**
   \brief parse brultech connection type
   \param cfg the config base
   \param opt the option we are parsing
   \param the value of the option
   \param result result of option parsing will be stored here
   \return success
*/

static int conf_parse_brul_conn(cfg_t *cfg, cfg_opt_t *opt, const char *value,
				void *result)
{
	if (strcasecmp(value, "net") == 0)
		*(int *)result = BRUL_COMM_NET;
	else if (strcmp(value,"serial") == 0)
		*(int *)result = BRUL_COMM_SERIAL;
	else {
		cfg_error(cfg, "invalid value for option '%s': %s",
		    cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}

/**
   \brief Used to print brultech connection type
   \param opt option structure
   \param index number of option to print
   \param fp passed FILE
*/

static void conf_print_brul_conn(cfg_opt_t *opt, unsigned int index, FILE *fp)
{
	switch (cfg_opt_getnint(opt, index)) {
	case BRUL_COMM_SERIAL:
		fprintf(fp, "serial");
		break;
	case BRUL_COMM_NET:
	default:
		fprintf(fp, "net");
		break;
	}
}

/**
   \brief Setup the Brultech ECM1240
   \param conn the connection_t
   \note sets current_dev to NULL and requests a connection
*/

void brul_setup_ecm(connection_t *conn)
{
	struct evbuffer *send, *in;
	size_t len;

	LOG(LOG_DEBUG, "Setting up the ECM, at mode %d", conn->mode);
	send = evbuffer_new();
       
	switch (conn->mode) {
	case BRUL_MODE_NONE:
		evbuffer_add_printf(send, "%c", 0xFC);
		conn->mode = BRUL_MODE_WAITREADY;
		break;
	case BRUL_MODE_ECMREADY:
		evbuffer_add_printf(send, "TOG");
		conn->mode = BRUL_MODE_WAITTOG;
		break;
	case BRUL_MODE_ECMTOG:
		evbuffer_add_printf(send, "XTD");
		conn->mode = BRUL_MODE_ECMDATA;
		break;
	}
	/* flush the input buffer*/
	in = bufferevent_get_input(conn->bev);
	len = evbuffer_get_length(in);
	evbuffer_drain(in, len);

	bufferevent_write_buffer(conn->bev, send);
	evbuffer_free(send);
	LOG(LOG_DEBUG, "Setup, requesting mode %d", conn->mode);
}


/**
   \brief Setup the Brultech GEM
   \param conn the connection_t
   \note sets current_dev to NULL and requests a connection
*/

void brul_setup_gem(connection_t *conn)
{
	struct evbuffer *send, *in;
	size_t len;

	LOG(LOG_DEBUG, "Setting up the GEM, at mode %d", conn->mode);
	send = evbuffer_new();
       
	switch (conn->mode) {
	case BRUL_MODE_NONE:
		evbuffer_add_printf(send, "^^^SYSOFF\n");
		conn->mode = BRUL_MODE_RTOFF;
		break;
	case BRUL_MODE_RTOFF:
		evbuffer_add_printf(send, "^^^SYSPKT%0.2d\n", conn->pkttype);
		conn->mode = BRUL_MODE_PKT;
		break;
	case BRUL_MODE_PKT:
		evbuffer_add_printf(send, "^^^SYSIVL%0.3d\n",
				    cfg_getint(brulcoll_c, "update"));
		conn->mode = BRUL_MODE_IVL;
		break;
	case BRUL_MODE_IVL:
		evbuffer_add_printf(send, "^^^RQSSRN\n");
		conn->mode = BRUL_MODE_SRN;
		break;
	case BRUL_MODE_SRN:	
		if (conn->tempbase == BRUL_TEMP_F)
			evbuffer_add_printf(send, "^^^TMPDGF\n");
		else
			evbuffer_add_printf(send, "^^^TMPDGC\n");
		conn->mode = BRUL_MODE_TEMPTYPE;
		break;
	case BRUL_MODE_TEMPTYPE:
		evbuffer_add_printf(send, "^^^RQSTST\n");
		conn->mode = BRUL_MODE_TST;
		break;
	case BRUL_MODE_TST:
		evbuffer_add_printf(send, "^^^RQSPST\n");
		conn->mode = BRUL_MODE_PST;
		break;
	case BRUL_MODE_PST:
		evbuffer_add_printf(send, "^^^RQSCMX\n");
		conn->mode = BRUL_MODE_CMX;
		break;
	case BRUL_MODE_CMX:
		evbuffer_add_printf(send, "^^^SYS_ON\n");
		conn->mode = BRUL_MODE_RTON;
		break;
		/* once we are in on mode, the next packet will be ON,
		   followed by the actual data stream, so the ON will
		   switch the mode in the read handler */
	}
	/* flush the input buffer*/
	in = bufferevent_get_input(conn->bev);
	len = evbuffer_get_length(in);
	evbuffer_drain(in, len);

	bufferevent_write_buffer(conn->bev, send);
	evbuffer_free(send);
	LOG(LOG_DEBUG, "Setup, requesting mode %d", conn->mode);
}

/**
   \brief Handle a TST response
   \param data string to parse
   \param type handle tst or pst?
   \note 00010100 = temp 3, 5 enabled
*/

#define BRUL_HANDLE_TST	1
#define BRUL_HANDLE_PST 2

static void brul_handle_tstpst(char *data, int type)
{
	char *p, *buf;
	device_t *dev;
	int t;

	if (type == BRUL_HANDLE_TST)
		t = 8;
	else
		t = 4;

	for (p = data; t != 0 && *p != '\0' && *p != '\r' && *p != '\n';
	     *p++) {
		if (*p == '0') {
			t--;
			continue;
		}
		/* otherwise, this probe is enabled */
		if (type == BRUL_HANDLE_TST)
			brulconf.validtemp += 1<<t;
		else
			brulconf.validpulse += 1<<t;
		buf = safer_malloc(16);
		if (type == BRUL_HANDLE_TST) {
			sprintf(buf, "%0.8d-t%d", brulconf.serial, t);
			LOG(LOG_NOTICE, "Found GEM temp device %s", buf);
		} else {
			sprintf(buf, "%0.8d-p%d", brulconf.serial, t);
			LOG(LOG_NOTICE, "Found GEM pulse device %s", buf);
		}
		dev = new_dev_from_conf(cfg, buf);
		if (dev == NULL) {
			dev = smalloc(device_t);
			dev->uid = strdup(buf);
			dev->proto = PROTO_SENSOR_BRULTECH_GEM;
			dev->type = DEVICE_SENSOR;
			if (type == BRUL_HANDLE_TST) {
				dev->subtype = SUBTYPE_TEMP;
				dev->scale = tempscale;
			} else
				dev->subtype = SUBTYPE_COUNTER;
			(void)new_conf_from_dev(cfg, dev);
		}
		insert_device(dev);
       		if (dumpconf == NULL && dev->name != NULL)
			gn_register_device(dev, gnhastd_conn->bev);
		t--;
		free(buf);
	}
	return;
}

/**
   \brief build the main brultech devices
   \param channels number of channels to build
   \param amps number of amps devices to build
   \param proto protocol type
   Builds a wattsec and watts device for each channel, as well as a
   seconds counter, and a voltage device.
*/

static void brul_build_devices(int channels, int amps, int proto)
{
	int i;
	device_t *dev;
	char buf[64];

	/* first, the channels */
	for (i=0; i < channels; i++) {
		sprintf(buf, "%0.8d-c%0.2d", brulconf.serial, i);
		dev = new_dev_from_conf(cfg, buf);
		if (dev == NULL) {
			dev = smalloc(device_t);
			dev->uid = strdup(buf);
			if (dumpconf != NULL) {
				sprintf(buf, "brultech wattsec %d", i);
				dev->name = strdup(buf);
				sprintf(buf, "brulwsec%0.2d", i);
				dev->rrdname = strdup(buf);
			}
			dev->proto = proto;
			dev->type = DEVICE_SENSOR;
			dev->subtype = SUBTYPE_WATTSEC;
			(void) new_conf_from_dev(cfg, dev);
		}
		insert_device(dev);
       		if (dumpconf == NULL && dev->name != NULL)
			gn_register_device(dev, gnhastd_conn->bev);

		/* now build watts for this channel */
		sprintf(buf, "%0.8d-w%0.2d", brulconf.serial, i);
		dev = new_dev_from_conf(cfg, buf);
		if (dev == NULL) {
			dev = smalloc(device_t);
			dev->uid = strdup(buf);
			if (dumpconf != NULL) {
				sprintf(buf, "brultech watt %d", i);
				dev->name = strdup(buf);
				sprintf(buf, "brulwatt%0.2d", i);
				dev->rrdname = strdup(buf);
			}
			dev->proto = proto;
			dev->type = DEVICE_SENSOR;
			dev->subtype = SUBTYPE_WATT;
			(void)new_conf_from_dev(cfg, dev);
		}
		insert_device(dev);
       		if (dumpconf == NULL && dev->name != NULL)
			gn_register_device(dev, gnhastd_conn->bev);
	}

	/* amp devices */
	for (i=0; i < amps; i++) {
		sprintf(buf, "%0.8d-a%0.2d", brulconf.serial, i);
		dev = new_dev_from_conf(cfg, buf);
		if (dev == NULL) {
			dev = smalloc(device_t);
			dev->uid = strdup(buf);
			if (dumpconf != NULL) {
				sprintf(buf, "brultech amps %d", i);
				dev->name = strdup(buf);
				sprintf(buf, "brulamp%0.2d", i);
				dev->rrdname = strdup(buf);
			}
			dev->proto = proto;
			dev->type = DEVICE_SENSOR;
			dev->subtype = SUBTYPE_AMPS;
			(void)new_conf_from_dev(cfg, dev);
		}
		insert_device(dev);
       		if (dumpconf == NULL && dev->name != NULL)
			gn_register_device(dev, gnhastd_conn->bev);
	}

	/* Seconds counter */
	sprintf(buf, "%0.8d-sec", brulconf.serial);
	dev = new_dev_from_conf(cfg, buf);
	if (dev == NULL) {
		dev = smalloc(device_t);
		dev->uid = strdup(buf);
		if (dumpconf != NULL) {
			sprintf(buf, "brultech seconds counter");
			dev->name = strdup(buf);
			sprintf(buf, "brulsec");
			dev->rrdname = strdup(buf);
		}
		dev->proto = proto;
		dev->type = DEVICE_SENSOR;
		dev->subtype = SUBTYPE_COUNTER;
		(void)new_conf_from_dev(cfg, dev);
	}
	insert_device(dev);
	if (dumpconf == NULL && dev->name != NULL)
		gn_register_device(dev, gnhastd_conn->bev);

	/* voltage */
	sprintf(buf, "%0.8d-volt", brulconf.serial);
	dev = new_dev_from_conf(cfg, buf);
	if (dev == NULL) {
		dev = smalloc(device_t);
		dev->uid = strdup(buf);
		if (dumpconf != NULL) {
			sprintf(buf, "brultech voltage");
			dev->name = strdup(buf);
			sprintf(buf, "brulvolt");
			dev->rrdname = strdup(buf);
		}
		dev->proto = proto;
		dev->type = DEVICE_SENSOR;
		dev->subtype = SUBTYPE_VOLTAGE;
		(void)new_conf_from_dev(cfg, dev);
	}
	insert_device(dev);
	if (dumpconf == NULL && dev->name != NULL)
		gn_register_device(dev, gnhastd_conn->bev);

}
/**
   \brief Handle an ecm1240 packet
   \param in bufferevent that has data
   \param conn connection_t
*/

void brul_handle_ecm1240(struct bufferevent *in, connection_t *conn)
{
	ecm1240_t *ecmdata;
	size_t len;
	struct evbuffer *evbuf;
	int i;

	evbuf = bufferevent_get_input(in);
	ecmdata = smalloc(ecm1240_t);
	len = evbuffer_remove(evbuf, ecmdata, sizeof(ecm1240_t));
	if (len == -1)
		LOG(LOG_ERROR, "Got no data in brul_handle_ecm1240");
	LOG(LOG_DEBUG, "Read %d bytes of data", len);

	if (ecmdata->footer[0] != 0xFF || ecmdata->footer[1] != 0xFE) {
		LOG(LOG_ERROR, "Footer not found, misalignment of data!");
		LOG(LOG_DEBUG, "Footer: %X %X %X volts:%d", ecmdata->footer[0],
		    ecmdata->footer[1], ecmdata->checksum,
		    BETOH16(ecmdata->voltage));
		return;
	}
	/* copy cur to prev */
	memcpy(&bruldata[0], &bruldata[1], sizeof(bruldata_t));

	for (i=0; i<2; i++) {
		bruldata[1].channel[i] = CONV_WATTSEC(ecmdata->channel[i]);
		bruldata[1].polar[i] = CONV_WATTSEC(ecmdata->polar[i]);
	}
	bruldata[1].voltage = BETOH16(ecmdata->voltage) / 10.0;
	bruldata[1].seconds = CONV_THREE(ecmdata->seconds);
	bruldata[1].serial = LETOH16(ecmdata->serial);

	LOG(LOG_DEBUG, "V:%f sec:%d serial:%d", bruldata[1].voltage,
	    bruldata[1].seconds, bruldata[1].serial);

	for (i=2; i < 6; i++)
		bruldata[1].channel[i] = LETOH32(ecmdata->aux[i-2]);
	free(ecmdata);
}


/**
   \brief Handle a type 7 packet
   \param in bufferevent that has data
   \param conn connection_t
*/

void brul_handle_type7(struct bufferevent *in, connection_t *conn)
{
	gem_polar32_t *gemdata;
	size_t len;
	struct evbuffer *evbuf;
	int i;

	evbuf = bufferevent_get_input(in);
	gemdata = smalloc(gem_polar32_t);
	len = evbuffer_remove(evbuf, gemdata, sizeof(gem_polar32_t));
	if (len == -1)
		LOG(LOG_ERROR, "Got no data in brul_handle_type7");
	LOG(LOG_DEBUG, "Read %d bytes of data", len);

	if (gemdata->footer[0] != 0xFF || gemdata->footer[1] != 0xFE) {
		LOG(LOG_ERROR, "Footer not found, misalignment of data!");
		LOG(LOG_DEBUG, "Footer: %X %X %X volts:%d", gemdata->footer[0],
		    gemdata->footer[1], gemdata->checksum,
		    BETOH16(gemdata->voltage));
		return;
	}
	/* copy cur to prev */
	memcpy(&bruldata[0], &bruldata[1], sizeof(bruldata_t));
	for (i=0; i < brulconf.nrofchannels; i++) {
		bruldata[1].channel[i] = CONV_WATTSEC(gemdata->channel[i]);
		bruldata[1].polar[i] = CONV_WATTSEC(gemdata->polar[i]);
		/* printf("C%d=%jd   ", i, bruldata[1].channel[i]); */
	}
	/* printf("\n"); */
	bruldata[1].voltage = BETOH16(gemdata->voltage) / 10.0;
	bruldata[1].seconds = CONV_THREE(gemdata->seconds);
	bruldata[1].serial = BETOH16(gemdata->serial);

	LOG(LOG_DEBUG, "V:%f sec:%d serial:%d", bruldata[1].voltage,
	    bruldata[1].seconds, bruldata[1].serial);

	for (i=0; i<8; i++)
		if (brulconf.validtemp & (1<<i))
			bruldata[1].temp[i] = LETOH16(gemdata->temp[i]) / 2.0;
	for (i=0; i<4; i++)
		if (brulconf.validpulse & (1<<i))
			bruldata[1].pulse[i] = CONV_THREE(gemdata->pulse[i]);
	free(gemdata);
}

/**
   \brief Handle a type 5 packet
   \param in bufferevent that has data
   \param conn connection_t
*/

void brul_handle_type5(struct bufferevent *in, connection_t *conn)
{
	gem_polar48dt_t *gemdata;
	size_t len;
	struct evbuffer *evbuf;
	int i;

	evbuf = bufferevent_get_input(in);
	gemdata = smalloc(gem_polar48dt_t);
	len = evbuffer_remove(evbuf, gemdata, sizeof(gem_polar48dt_t));
	if (len == -1)
		LOG(LOG_ERROR, "Got no data in brul_handle_type5");
	LOG(LOG_DEBUG, "Read %d bytes of data", len);

	if (gemdata->footer[0] != 0xFF || gemdata->footer[1] != 0xFE) {
		LOG(LOG_ERROR, "Footer not found, misalignment of data!");
		LOG(LOG_DEBUG, "Footer: %X %X %X volts:%d", gemdata->footer[0],
		    gemdata->footer[1], gemdata->checksum,
		    BETOH16(gemdata->voltage));
		return;
	}
	/* copy cur to prev */
	memcpy(&bruldata[0], &bruldata[1], sizeof(bruldata_t));
	for (i=0; i < brulconf.nrofchannels; i++) {
		bruldata[1].channel[i] = CONV_WATTSEC(gemdata->channel[i]);
		bruldata[1].polar[i] = CONV_WATTSEC(gemdata->polar[i]);
		//printf("C%d=%jd   ", i, bruldata[1].channel[i]);
	}
	//printf("\n");
	bruldata[1].voltage = BETOH16(gemdata->voltage) / 10.0;
	bruldata[1].seconds = CONV_THREE(gemdata->seconds);
	bruldata[1].serial = BETOH16(gemdata->serial);

	LOG(LOG_DEBUG, "V:%f sec:%d serial:%d", bruldata[1].voltage,
	    bruldata[1].seconds, bruldata[1].serial);

	for (i=0; i<8; i++)
		if (brulconf.validtemp & (1<<i))
			bruldata[1].temp[i] = BETOH16(gemdata->temp[i]) / 2.0;
	for (i=0; i<4; i++)
		if (brulconf.validpulse & (1<<i))
			bruldata[1].pulse[i] = CONV_THREE(gemdata->pulse[i]);
	free(gemdata);
}


/**
   \brief Calculate all the devices and update
*/

void calc_devices(void)
{
	bruldata_t *cur = &bruldata[1];
	bruldata_t *prev = &bruldata[0];
	int sdelta, i;
	int64_t wdiff;
	double watts;
	device_t *dev;
	char uid[64];

	/* wattsec counters */
	for (i=0; i < brulconf.nrofchannels; i++) {
		sprintf(uid, "%0.8d-c%0.2d", brulconf.serial, i);
		dev = find_device_byuid(uid);
		if (dev == NULL) {
			LOG(LOG_ERROR, "Can't find dev for %s", uid);
			continue;
		}
		store_data_dev(dev, DATALOC_DATA, &cur->channel[i]);
		gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
	}

	/* temp sensors */
	for (i=0; i<8; i++)
		if (brulconf.validtemp & (1<<i)) {
			sprintf(uid, "%0.8d-t%d", brulconf.serial, i);
			dev = find_device_byuid(uid);
			if (dev == NULL) {
				LOG(LOG_ERROR, "Can't find dev for %s", uid);
				continue;
			}
			store_data_dev(dev, DATALOC_DATA, &cur->temp[i]);
			gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
		}

	/* pulse counters */
	for (i=0; i<4; i++)
		if (brulconf.validpulse & (1<<i)) {
			sprintf(uid, "%0.8d-p%d", brulconf.serial, i);
			dev = find_device_byuid(uid);
			if (dev == NULL) {
				LOG(LOG_ERROR, "Can't find dev for %s", uid);
				continue;
			}
			store_data_dev(dev, DATALOC_DATA, &cur->pulse[i]);
			gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
		}

	/* voltage */
	sprintf(uid, "%0.8d-volt", brulconf.serial);
	dev = find_device_byuid(uid);
	if (dev != NULL) {
		store_data_dev(dev, DATALOC_DATA, &cur->voltage);
		gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
	}

	sprintf(uid, "%0.8d-sec", brulconf.serial);
	dev = find_device_byuid(uid);
	if (dev != NULL) {
		store_data_dev(dev, DATALOC_DATA, &cur->seconds);
		gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
	}

	if (prev->seconds == 0)
		return; /* wait for the next pass */

	/* watts/period */

	if (prev->seconds > cur->seconds) {
		sdelta = MAX_THREE - prev->seconds;
		sdelta += cur->seconds;
	} else
		sdelta = cur->seconds - prev->seconds;

	for (i=0; i < brulconf.nrofchannels; i++) {
		sprintf(uid, "%0.8d-w%0.2d", brulconf.serial, i);
		dev = find_device_byuid(uid);
		if (dev == NULL) {
			LOG(LOG_ERROR, "Can't find dev for %s", uid);
			continue;
		}
		if (prev->channel[i] > cur->channel[i]) {
			wdiff = MAX_WSEC - prev->channel[i];
			wdiff += cur->channel[i];
		} else
			wdiff = cur->channel[i] - prev->channel[i];
		watts = (float)wdiff/(float)sdelta;
		store_data_dev(dev, DATALOC_DATA, &watts);
		gn_update_device(dev, GNC_NOSCALE, gnhastd_conn->bev);
	}
}

/**
   \brief brultech read callback
   \param in the bufferevent that fired
   \param arg the connection_t
*/

void brul_buf_read_cb(struct bufferevent *in, void *arg)
{
	connection_t *conn = (connection_t *)arg;
	struct evbuffer *evbuf;
	int32_t freedata=1;
	char *data;
	size_t len;
	threebyte_t *header;

	/* look for the response, then advance the mode by calling setup */
	if (conn->mode < BRUL_MODE_DATA) {
again:
		if (conn->mode != BRUL_MODE_TEMPTYPE) {
			evbuf = bufferevent_get_input(in);
			data = evbuffer_readln(evbuf, &len, EVBUFFER_EOL_CRLF);
			if (data == NULL || len < 1)
				return;
			LOG(LOG_DEBUG, "Got data %s", data);
		}
		switch (conn->mode) {
		case BRUL_MODE_RTOFF:
			if (strcmp(data, "OFF") == 0)
				brul_setup_gem(conn);
			else {
				LOG(LOG_ERROR, "Expected OFF, got %s", data);
				free(data);
				goto again;
			}
			break;
		case BRUL_MODE_PKT:
			if (strcmp(data, "PKT") == 0)
				brul_setup_gem(conn);
			else
				LOG(LOG_ERROR, "Expected PKT, got %s", data);
			break;
		case BRUL_MODE_IVL:
			if (strcmp(data, "IVL") == 0)
				brul_setup_gem(conn);
			else
				LOG(LOG_ERROR, "Expected IVL, got %s", data);
			break;
		case BRUL_MODE_SRN:
			brulconf.serial = atoi(data);
			brul_setup_gem(conn);
			break;
		case BRUL_MODE_TEMPTYPE:
			freedata=0;
			evbuf = bufferevent_get_input(in);
			data = evbuffer_pullup(evbuf, 1);
			if (data == NULL)
				return;
			if (strncmp(data, "F", 1) == 0 || strncmp(data, "C", 1) == 0)
				brul_setup_gem(conn);
			else
				LOG(LOG_ERROR, "Expected C/F, got %s", data);
			evbuffer_drain(evbuf, 1); /* discard the byte */
			break;
		case BRUL_MODE_TST:
			brulconf.validtemp = 0;
			brul_handle_tstpst(data, BRUL_HANDLE_TST);
			brul_setup_gem(conn);
			break;
		case BRUL_MODE_PST:
			brulconf.validpulse = 0;
			brul_handle_tstpst(data, BRUL_HANDLE_PST);
			brul_setup_gem(conn);
			break;
		case BRUL_MODE_CMX:
			brulconf.nrofchannels = atoi(data);
			brul_build_devices(brulconf.nrofchannels, 0,
					   PROTO_SENSOR_BRULTECH_GEM);
			if (dumpconf != NULL) {
				LOG(LOG_NOTICE, "Dumping config file to "
				    "%s and exiting", dumpconf);
				dump_conf(cfg, 0, dumpconf);
				exit(0);
			} else
				brul_setup_gem(conn);
			break;
		case BRUL_MODE_RTON:
			/* we see the on, and switch to data mode */
			if (strcmp(data, "_ON") == 0)
				conn->mode = BRUL_MODE_DATA;
			break;
		/* ECM1240 code */
		case BRUL_MODE_WAITTOG:
		case BRUL_MODE_WAITREADY:
			evbuf = bufferevent_get_input(in);
			data = evbuffer_pullup(evbuf, 1);
			if (data == NULL)
				return;
			if (data[0] == 0xFC &&
			    conn->mode == BRUL_MODE_WAITREADY)
				conn->mode = BRUL_MODE_ECMREADY;
			else if (data[0] == 0xFC &&
				 conn->mode == BRUL_MODE_WAITTOG)
				conn->mode = BRUL_MODE_ECMTOG;
			evbuffer_drain(evbuf, 1); /* discard the byte */
			break;
		}
		if (freedata)
			free(data);
		return;
	}
	/* if we get here, we have a data packet */
	evbuf = bufferevent_get_input(in);
rereadheader:
	header = (threebyte_t *)evbuffer_pullup(evbuf, 3);
	if (header == NULL)
		return; /* ??? */
	LOG(LOG_DEBUG, "header = %X %X %X", header->byte[0], header->byte[1],
	    header->byte[2]);
	if (header->byte[0] != 0xFE && header->byte[1] != 0xFF) {
		LOG(LOG_DEBUG, "Bad data, draining 1 byte");
		evbuffer_drain(evbuf, 1);
		goto rereadheader;
	}

	/* if we got this far, we have reasonable data, mark it as a lastupd */
	brul_lastupd = time(NULL);

	switch (header->byte[2]) {
	case BRUL_FMT_32POLAR:
		brul_handle_type7(in, conn);
		calc_devices();
		break;
	case BRUL_FMT_48POLAR:
		brul_handle_type5(in, conn);
		calc_devices();
		break;
	case BRUL_FMT_ECM1240:
		brul_handle_ecm1240(in, conn);
		break;
	}

	return;
}

/**
   \brief Event callback used with brultech net connections
   \param ev The bufferevent that fired
   \param what why did it fire?
   \param arg pointer to connection_t;
*/

void brul_netconnect_event_cb(struct bufferevent *ev, short what, void *arg)
{
	int err;
	connection_t *conn = (connection_t *)arg;

	if (what & BEV_EVENT_CONNECTED) {
		LOG(LOG_DEBUG, "Connected to %s", conntype[conn->type]);
	} else if (what & (BEV_EVENT_ERROR|BEV_EVENT_EOF)) {
		if (what & BEV_EVENT_ERROR) {
			err = bufferevent_socket_get_dns_error(ev);
			if (err)
				LOG(LOG_FATAL, "DNS Failure connecting to %s: %s",
				    conntype[conn->type], strerror(err));
		}
		LOG(LOG_DEBUG, "Lost connection to %s, closing",
		    conntype[conn->type]);
		bufferevent_disable(ev, EV_READ|EV_WRITE);
		bufferevent_free(ev);
		/* Retry immediately */
		connect_server_cb(0, 0, conn);
		brul_setup_gem(conn);
	}
}

/*****
      General routines/gnhastd connection stuff
*****/


/**
   \brief Check if a collector is functioning properly
   \param conn connection_t of collector's gnhastd connection
   \return 1 if OK, 0 if broken
   \note if 5 updates pass with no data, bad bad.
*/

int collector_is_ok(void)
{
	int update;

	update = cfg_getint(brulcoll_c, "update");
	if ((time(NULL) - brul_lastupd) < (update * 5))
		return(1);
	return(0);
}

/**
   \brief A timer callback to send gnhastd imalive statements
   \param nada used for file descriptor
   \param what why did we fire?
   \param arg pointer to connection_t of gnhastd connection
*/

void health_cb(int nada, short what, void *arg)
{
	connection_t *conn = (connection_t *)arg;

	if (collector_is_ok())
		gn_imalive(conn->bev);
	else
		LOG(LOG_WARNING, "Collector is non functional");
}

/**
   \brief A timer callback that initiates a new connection
   \param nada used for file descriptor
   \param what why did we fire?
   \param arg pointer to connection_t
   \note also used to manually initiate a connection
*/

void connect_server_cb(int nada, short what, void *arg)
{
	connection_t *conn = (connection_t *)arg;
	device_t *dev;

	conn->bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
	if (conn->type == CONN_TYPE_GNHASTD)
		bufferevent_setcb(conn->bev, gnhastd_read_cb, NULL,
				  connect_event_cb, conn);
	else if (conn->type == CONN_TYPE_BRUL)
		bufferevent_setcb(conn->bev, brul_buf_read_cb, NULL,
				  brul_netconnect_event_cb, conn);
	bufferevent_enable(conn->bev, EV_READ|EV_WRITE);
	bufferevent_socket_connect_hostname(conn->bev, dns_base, AF_UNSPEC,
					    conn->host, conn->port);
	if (conn->type == 1) {
		gemconnattempts++;
		LOG(LOG_NOTICE, "Attempting to connect to %s @ %s:%d retry:%d",
		    conntype[conn->type], conn->host, conn->port,
		    gemconnattempts);
		/* Can/should we reset the device? */
		if (cfg_getint(brultech_c, "model") == BRUL_MODEL_GEM &&
		    cfg_getint(brultech_c, "connection") == BRUL_COMM_NET &&
		    gemconnattempts > MAXGEMRETRY && wiznetfound == 1 &&
		    wiz_fd != -1) {
			LOG(LOG_WARNING, "Attempting wiznet reset");
			event_base_once(base, wiz_fd, EV_WRITE|EV_TIMEOUT,
				cb_wiznet_send_direct, wizresetbuf, NULL);
			gemconnattempts -= 5; /* give it a few more tries */
		}
	}
	if (conn->type == CONN_TYPE_GNHASTD) {
		LOG(LOG_NOTICE, "Attempting to connect to %s @ %s:%d",
		    conntype[conn->type], conn->host, conn->port);
		if (need_rereg) {
			TAILQ_FOREACH(dev, &alldevs, next_all)
				if (dumpconf == NULL && dev->name != NULL)
					gn_register_device(dev, conn->bev);
			gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);
		}
		need_rereg = 0;
		/* set this for the ping event */
		gnhastd_bev = conn->bev;
	}
}

/**
   \brief Event callback used with connections
   \param ev The bufferevent that fired
   \param what why did it fire?
   \param arg pointer to connection_t;
*/

void connect_event_cb(struct bufferevent *ev, short what, void *arg)
{
	int err;
	connection_t *conn = (connection_t *)arg;
	struct event *tev; /* timer event */
	struct timeval secs = { 60, 0 }; /* retry in 60 seconds */

	if (what & BEV_EVENT_CONNECTED) {
		LOG(LOG_NOTICE, "Connected to %s", conntype[conn->type]);
		if (conn->type == CONN_TYPE_BRUL) /* gem */
			gemconnattempts = 0;
		if (conn->type == CONN_TYPE_GNHASTD) {
			tev = event_new(base, -1, EV_PERSIST, health_cb, conn);
			secs.tv_sec = HEALTH_CHECK_RATE;
			evtimer_add(tev, &secs);
			LOG(LOG_NOTICE, "Setting up self-health checks every "
			    "%d seconds", secs.tv_sec);
			
		}
	} else if (what & (BEV_EVENT_ERROR|BEV_EVENT_EOF)) {
		if (what & BEV_EVENT_ERROR) {
			err = bufferevent_socket_get_dns_error(ev);
			if (err)
				LOG(LOG_FATAL,
				    "DNS Failure connecting to %s: %s",
				    conntype[conn->type], strerror(err));
		}
		LOG(LOG_NOTICE, "Lost connection to %s, closing",
		    conntype[conn->type]);
		bufferevent_disable(ev, EV_READ|EV_WRITE);
		bufferevent_free(ev);

		if (!conn->shutdown) {
			/* we need to reconnect! */
			need_rereg = 1;
			tev = evtimer_new(base, connect_server_cb, conn);
			evtimer_add(tev, &secs); /* XXX	leaks? */
			LOG(LOG_NOTICE, "Attempting reconnection to "
			    "%s @ %s:%d in %d seconds",
			    conntype[conn->type], conn->host, conn->port,
			    secs.tv_sec);
		} else
			event_base_loopexit(base, NULL);
	}
}


/**
   \brief Parse the config file for devices and load them
   \param cfg config base
*/
void parse_devices(cfg_t *cfg)
{
	device_t *dev;
	cfg_t *devconf;
	cfg_opt_t *opt;
	int i;

	for (i=0; i < cfg_size(cfg, "device"); i++) {
		devconf = cfg_getnsec(cfg, "device", i);
		dev = new_dev_from_conf(cfg, (char *)cfg_title(devconf));
		insert_device(dev);
		LOG(LOG_DEBUG, "Loaded device %s location %s from config file",
		    dev->uid, dev->loc);
		if (dumpconf == NULL && dev->name != NULL)
			gn_register_device(dev, gnhastd_conn->bev);
	}
	/* setup the print functions */
	opt = cfg_getopt(brultech_c, "model");
	if (opt)
		cfg_opt_set_print_func(opt, conf_print_brul_model);
	opt = cfg_getopt(brultech_c, "connection");
	if (opt)
		cfg_opt_set_print_func(opt, conf_print_brul_conn);
	opt = cfg_getopt(brulcoll_c, "tscale");
	if (opt)
		cfg_opt_set_print_func(opt, conf_print_tscale);

}

/**
   \brief Shutdown timer
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void cb_shutdown(int fd, short what, void *arg)
{
	LOG(LOG_WARNING, "Clean shutdown timed out, stopping");
	event_base_loopexit(base, NULL);
}

/**
   \brief A sigterm handler
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void cb_sigterm(int fd, short what, void *arg)
{
	struct timeval secs = { 30, 0 };
	struct event *ev;

	LOG(LOG_NOTICE, "Recieved SIGTERM, shutting down");
	gnhastd_conn->shutdown = 1;
	gn_disconnect(gnhastd_conn->bev);
	if (brulnet_conn->bev) {
		bufferevent_disable(brulnet_conn->bev, EV_READ|EV_WRITE);
		bufferevent_free(brulnet_conn->bev);
	}
	ev = evtimer_new(base, cb_shutdown, NULL);
	evtimer_add(ev, &secs);
}

/* WIZNET code */

/**
   \brief Watch for wiznet messages
   \param fd unused
   \param what what happened?
   \param arg unused
*/

void cb_wiznet_read(int fd, short what, void *arg)
{
	char buf[1024], buf2[8];
	int len, size;
	struct sockaddr_in cli_addr;
	wiznet_t wizc;

	size = sizeof(struct sockaddr);
	bzero(buf, sizeof(buf));
	len = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&cli_addr,
		       &size);

	LOG(LOG_DEBUG, "got %d bytes on udp:5001", len);

	if (len == sizeof(wiznet_t) + 4) { /* we got a FIND resp */
		strncpy(buf2, buf, 4);
		buf2[5] = '\0';
		if (strncmp("IMIN", buf, 4) != 0) {
			LOG(LOG_WARNING, "Got odd response from wiznet "
			    "device: %s", buf2);
			return;
		}
		memcpy(&wizc, &buf[4], sizeof(wiznet_t));
		LOG(LOG_NOTICE, "Got wiznet response from IP %d.%d.%d.%d",
		    wizc.ipaddr[0], wizc.ipaddr[1], wizc.ipaddr[2],
		    wizc.ipaddr[3]);
		LOG(LOG_NOTICE, "Wiznet FW version %d.%d", wizc.fwver[0],
		    wizc.fwver[1]);
		/* is this the wiznet device matching our GEM? */
		if (wizc.ipaddr[0] == gemipaddr[0] &&
		    wizc.ipaddr[1] == gemipaddr[1] &&
		    wizc.ipaddr[2] == gemipaddr[2] &&
		    wizc.ipaddr[3] == gemipaddr[3]) {
			memcpy(&wizconf, &wizc, sizeof(wiznet_t));
			LOG(LOG_NOTICE, "Found matching wiznet device");
			wiznetfound = 1;
			wizresetbuf = safer_malloc(sizeof(wiznet_t) + 4);
			wizresetbuf[0] = 'S';
			wizresetbuf[1] = 'E';
			wizresetbuf[2] = 'T';
			wizresetbuf[3] = 'T';
			memcpy(&wizresetbuf[4], &wizconf, sizeof(wiznet_t));
		}
		return;
	}
	if (len == 4) { /* we got a SETC response */
		strncpy(buf2, buf, 4);
		buf2[5] = '\0';
		if (strncmp("SETC", buf2, 4) != 0) {
			LOG(LOG_WARNING, "Got odd response from wiznet "
			    "device: %s", buf2);
			return;
		}
		LOG(LOG_NOTICE, "Wiznet device has been reset.");
		return;
	}
}

/**
   \brief Send data to the wiznet device
   \param fd the wiznet fd
   \param what what happened?
   \param arg char * of data to send
*/

void cb_wiznet_send(int fd, short what, void *arg)
{
	char *buf = (char *)arg;
	int len;
	struct sockaddr_in broadcast_addr;

	if (buf == NULL)
		return;

	memset(&broadcast_addr, 0, sizeof(broadcast_addr));
	broadcast_addr.sin_family = AF_INET;
	broadcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	broadcast_addr.sin_port = htons(WIZNET_PORT);

	len = sendto(fd, buf, strlen(buf), 0,
		     (struct sockaddr *)&broadcast_addr,
		     sizeof(struct sockaddr_in));
	LOG(LOG_DEBUG, "Send msg len %d to udp:1460");
}

/**
   \brief Send data to the wiznet device directly (no broadcast)
   \param fd the wiznet fd
   \param what what happened?
   \param arg char * of data to send
*/

void cb_wiznet_send_direct(int fd, short what, void *arg)
{
	char *buf = (char *)arg;
	int len;
	struct sockaddr_in wiznet_addr;

	if (buf == NULL)
		return;

	memset(&wiznet_addr, 0, sizeof(wiznet_addr));
	wiznet_addr.sin_family = AF_INET;
	wiznet_addr.sin_addr.s_addr = inet_addr(gemipaddrstr);
	wiznet_addr.sin_port = htons(WIZNET_PORT);

	len = sendto(fd, buf, strlen(buf), 0,
		     (struct sockaddr *)&wiznet_addr,
		     sizeof(struct sockaddr_in));
	LOG(LOG_DEBUG, "Send msg len %d to udp:1460");
}

/**
   \brief bind udp 5001 to listen to wiznet events
   \return fd
*/
int bind_wiznet_recv(void)
{
	int sock_fd, f, flag=1;
	struct sockaddr_in sin;

	if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		LOG(LOG_ERROR, "Failed to bind port 5001:udp in socket()");
		return -1;
	}

	if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &flag,
		       sizeof(int)) < 0) {
		LOG(LOG_ERROR, "Could not set SO_REUSEADDR on port 5001:udp");
		return -1;
	}
	if (setsockopt(sock_fd, SOL_SOCKET, SO_BROADCAST, &flag,
		       sizeof(int)) < 0) {
		LOG(LOG_ERROR, "Could not set SO_BROADCAST on port 5001:udp");
		return -1;
	}
	if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEPORT, &flag,
		       sizeof(int)) < 0) {
		LOG(LOG_ERROR, "Could not set SO_REUSEPORT on port 5001:udp");
		return -1;
	}

	/* make it non-blocking */
	f = fcntl(sock_fd, F_GETFL);
	f |= O_NONBLOCK;
	fcntl(sock_fd, F_SETFL, f);

	/* Set IP, port */
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(WIZNET_LISTEN);

	if (bind(sock_fd, (struct sockaddr *)&sin,
		 sizeof(struct sockaddr)) < 0) {
		LOG(LOG_ERROR, "bind() to port 5001:udp failed");
		return -1;
	} else
		LOG(LOG_NOTICE,
		    "Listening on port 5001:udp for wiznet events");

	return sock_fd;
}

/**
   \brief Setup the wiznet device handler
*/

void wiznet_setup()
{
	const char *s = NULL;
	int errcode;
	struct evutil_addrinfo hints, *answer = NULL;
	struct event *wiz_ev;

	memset(&hints, 0, sizeof(struct evutil_addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	errcode = evutil_getaddrinfo(brulnet_conn->host, NULL,
				&hints, &answer);

	if (errcode) {
		LOG(LOG_ERROR, "GEM DNS lookup failed. Modify config to "
		    "hardcode IP instead of using a hostname.");
		return;
	}
	if (answer == NULL)
		return;

	if (answer->ai_family != AF_INET)
		return;


	struct sockaddr_in *sin = (struct sockaddr_in *)answer->ai_addr;
	s = evutil_inet_ntop(AF_INET, &sin->sin_addr, gemipaddrstr, 16);
	if (s == NULL)
		return;

	LOG(LOG_NOTICE, "DNS lookup returned for GEM IP: %s", gemipaddrstr);
	sscanf(gemipaddrstr, "%d.%d.%d.%d", &gemipaddr[0], &gemipaddr[1],
	       &gemipaddr[2], &gemipaddr[3]);

	/* build wiznet event */
	wiz_fd = bind_wiznet_recv();
	if (wiz_fd != -1) {
		wiz_ev = event_new(base, wiz_fd, EV_READ | EV_PERSIST,
				   cb_wiznet_read, NULL);
		event_add(wiz_ev, NULL);
		event_base_once(base, wiz_fd, EV_WRITE|EV_TIMEOUT,
				cb_wiznet_send,	"FIND", NULL);
	} else
		LOG(LOG_ERROR, "Failed to setup wiznet event");
}

/**
   \brief Main itself
   \param argc count
   \param arvg vector
   \return int
*/

int main(int argc, char **argv)
{
	extern char *optarg;
	extern int optind;
	int ch, fd;
	char *buf;
	struct event *ev;

	/* process command line arguments */
	while ((ch = getopt(argc, argv, "?c:dm:")) != -1)
		switch (ch) {
		case 'c':	/* Set configfile */
			conffile = optarg;
			break;
		case 'd':
			debugmode = 1;
			break;
		case 'm':
			dumpconf = strdup(optarg);
			break;
		default:
		case '?':	/* you blew it */
			(void)fprintf(stderr, "usage:\n%s [-c configfile]"
				      "[-m dumpconfigfile]\n", getprogname());
			return(EXIT_FAILURE);
			/*NOTREACHED*/
			break;
		}

	if (!debugmode)
		if (daemon(0, 0) == -1)
			LOG(LOG_FATAL, "Failed to daemonize: %s",
			    strerror(errno));
	/* Initialize the event system */
	base = event_base_new();
	dns_base = evdns_base_new(base, 1);

	/* Initialize the argtable */
	init_argcomm();
	/* Initialize the command table */
	init_commands();
	/* Initialize the device table */
	init_devtable(cfg, 0);
	loopnr = 0;
	brul_lastupd = time(NULL);

	cfg = parse_conf(conffile);

	if (!debugmode)
		logfile = openlog(cfg_getstr(cfg, "logfile"));

	writepidfile(cfg_getstr(cfg, "pidfile"));

	/* First, parse the brulcoll section */

	if (cfg) {
		brulcoll_c = cfg_getsec(cfg, "brulcoll");
		if (!brulcoll_c)
			LOG(LOG_FATAL, "Error reading config file, "
			    "brulcoll section");
	}

	/* Now, parse the details of connecting to the gnhastd server */

	if (cfg) {
		gnhastd_c = cfg_getsec(cfg, "gnhastd");
		if (!gnhastd_c)
			LOG(LOG_FATAL, "Error reading config file, "
			    "gnhastd section");
	}

	/* Finally, parse how to connect to the brultech */

	if (cfg) {
		brultech_c = cfg_getsec(cfg, "brultech");
		if (!brultech_c)
			LOG(LOG_FATAL, "Error reading config file, "
			    "brultech section");
	}
	gnhastd_conn = smalloc(connection_t);
	gnhastd_conn->port = cfg_getint(gnhastd_c, "port");
	gnhastd_conn->type = CONN_TYPE_GNHASTD;
	gnhastd_conn->host = cfg_getstr(gnhastd_c, "hostname");
	/* cheat, and directly call the timer callback
	   This sets up a connection to the server. */
	connect_server_cb(0, 0, gnhastd_conn);
	collector_instance = cfg_getint(brultech_c, "instance");
	gn_client_name(gnhastd_conn->bev, COLLECTOR_NAME);
	
	brulnet_conn = smalloc(connection_t);
	switch (cfg_getint(brulcoll_c, "tscale")) {
	case TSCALE_C:
		tempscale = TSCALE_C;
		brulnet_conn->tempbase = BRUL_TEMP_C;
		break;
	default:
	case TSCALE_F:
		tempscale = TSCALE_F;
		brulnet_conn->tempbase = BRUL_TEMP_F;
		break;
	}
	brulnet_conn->mode = BRUL_MODE_NONE;
	brulnet_conn->pkttype = cfg_getint(brulcoll_c, "pkttype");
	if (cfg_getint(brultech_c, "connection") == BRUL_COMM_NET) {
		brulnet_conn->port = cfg_getint(brultech_c, "port");
		brulnet_conn->type = CONN_TYPE_BRUL;
		brulnet_conn->host = cfg_getstr(brultech_c, "hostname");
#ifndef WIZNET_DEBUG
		connect_server_cb(0, 0, brulnet_conn);
#endif
	} else if (cfg_getint(brultech_c, "connection") == BRUL_COMM_SERIAL) {
		if (cfg_getstr(brultech_c, "serialdev") == NULL)
			LOG(LOG_FATAL, "Serial device not set in conf file");
		fd = serial_connect(cfg_getstr(brultech_c, "serialdev"),
				    B19200, CS8|CREAD|CLOCAL);
		brulnet_conn->bev = bufferevent_socket_new(base, fd,
					    BEV_OPT_CLOSE_ON_FREE);
		brulnet_conn->type = CONN_TYPE_BRUL;
		bufferevent_setcb(brulnet_conn->bev, brul_buf_read_cb,
				  NULL, serial_eventcb, brulnet_conn);
		bufferevent_enable(brulnet_conn->bev, EV_READ|EV_WRITE);
	} else {
		LOG(LOG_FATAL, "No connection type specified, punting");
	}

	/* if it's a GEM, do a DNS lookup on the hostname */
	if (cfg_getint(brultech_c, "model") == BRUL_MODEL_GEM &&
	    cfg_getint(brultech_c, "connection") == BRUL_COMM_NET) {
		wiznet_setup();
	}
#ifndef WIZNET_DEBUG
	if (cfg_getint(brultech_c, "model") == BRUL_MODEL_GEM)
		brul_setup_gem(brulnet_conn);
	else if (cfg_getint(brultech_c, "model") == BRUL_MODEL_ECM1240)
		brul_setup_ecm(brulnet_conn);
	else
		LOG(LOG_FATAL, "No model specified, punting");
#endif
	parse_devices(cfg);

	/* setup signal handlers */
	ev = evsignal_new(base, SIGHUP, cb_sighup, conffile);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGTERM, cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGINT, cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGQUIT, cb_sigterm, NULL);
	event_add(ev, NULL);
	ev = evsignal_new(base, SIGUSR1, cb_sigusr1, NULL);
	event_add(ev, NULL);

	/* go forth and destroy */
	event_base_dispatch(base);

	closelog();
	delete_pidfile();
	return(0);
}
