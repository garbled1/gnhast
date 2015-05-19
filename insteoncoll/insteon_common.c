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

#include <termios.h>
#include <stdio.h>
#include <err.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#ifdef __linux__
 #include "../linux/queue.h"
 #include "../linux/time.h"
#else
 #include <sys/queue.h>
#endif
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>

#include "gnhast.h"
#include "common.h"
#include "confparser.h"
#include "confuse.h"
#include "genconn.h"
#include "confparser.h"
#include "gncoll.h"
#include "collcmd.h"
#include "insteon.h"

extern int errno;
extern int debugmode;
extern TAILQ_HEAD(, _device_t) alldevs;
extern int nrofdevs;
extern FILE *logfile;
extern struct event_base *base;
extern struct evdns_base *dns_base;
extern connection_t *plm_conn, *hubplm_conn, *hubhttp_conn;
extern connection_t *gnhastd_conn;
extern uint8_t plm_addr[3];
extern int need_rereg;
extern char *dumpconf;

int plmaldbmorerecords = 0;

SIMPLEQ_HEAD(fifohead, _cmdq_t) cmdfifo;
char *conntype[5] = {
	"none",
	"plm",
	"gnhastd",
	"hubplm",
	"hubhttp",
};

/**
   \brief parse insteon connection type
*/

int conf_parse_plmtype(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		       void *result)
{
	if (strcasecmp(value, "serial") == 0)
		*(int *)result = CONN_TYPE_PLM;
	else if (strcasecmp(value, "plm") == 0)
		*(int *)result = CONN_TYPE_PLM;
	else if (strcmp(value, "hubplm") == 0)
		*(int *)result = CONN_TYPE_HUBPLM;
	else if (strcmp(value, "hubhttp") == 0)
		*(int *)result = CONN_TYPE_HUBHTTP;
	else if (strcmp(value, "http") == 0)
		*(int *)result = CONN_TYPE_HUBHTTP;
	else {
		cfg_error(cfg, "invalid value for option '%s': %s",
		    cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}


/**
   \brief print insteon connection type
*/

void conf_print_plmtype(cfg_opt_t *opt, unsigned int index, FILE *fp)
{
	switch (cfg_opt_getnint(opt, index)) {
	case CONN_TYPE_PLM:
		fprintf(fp, "serial");
		break;
	case CONN_TYPE_HUBPLM:
		fprintf(fp, "hubplm");
		break;
	case CONN_TYPE_HUBHTTP:
		fprintf(fp, "hubhttp");
		break;
	default:
		fprintf(fp, "serial");
		break;
	}
}

/**
   \brief Event callback used with insteon hub-as-plm network connections
   \param ev The bufferevent that fired
   \param what why did it fire?
   \param arg pointer to connection_t;
*/

void hubplm_connect_event_cb(struct bufferevent *ev, short what, void *arg)
{
	int err;
	connection_t *conn = (connection_t *)arg;

	if (what & BEV_EVENT_CONNECTED)
		LOG(LOG_DEBUG, "Connected to %s", conntype[conn->type]);
	else if (what & (BEV_EVENT_ERROR|BEV_EVENT_EOF)) {
		if (what & BEV_EVENT_ERROR) {
			err = bufferevent_socket_get_dns_error(ev);
			if (err)
				LOG(LOG_FATAL, "DNS Failure connecting to %s: %s",
				    conntype[conn->type], strerror(err));
		}
		LOG(LOG_DEBUG, "Lost connection to %s, closing",
		    conntype[conn->type]);
		bufferevent_free(ev);
		/* Retry immediately */
		connect_server_cb(0, 0, conn);
	}
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
	else if (conn->type == CONN_TYPE_HUBPLM)
		bufferevent_setcb(conn->bev, plm_readcb, NULL,
				  hubplm_connect_event_cb, conn);
	bufferevent_enable(conn->bev, EV_READ|EV_WRITE);
	bufferevent_socket_connect_hostname(conn->bev, dns_base, AF_UNSPEC,
					    conn->host, conn->port);
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
	struct timeval secs = { 30, 0 }; /* retry in 30 seconds */

	if (what & BEV_EVENT_CONNECTED) {
		LOG(LOG_NOTICE, "Connected to %s", conntype[conn->type]);
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
		bufferevent_free(ev);

		if (!conn->shutdown) {
			/* we need to reconnect! */
			need_rereg = 1;
			tev = evtimer_new(base, connect_server_cb, conn);
			evtimer_add(tev, &secs); /* XXX leak? */
			LOG(LOG_NOTICE, "Attempting reconnection to "
			    "%s @ %s:%d in %d seconds",
			    conntype[conn->type], conn->host, conn->port,
			    secs.tv_sec);
		} else 
			event_base_loopexit(base, NULL);
	}
}

/**
   \brief Connect to a plm of given type
   \param plmtype type to connect to PLM_TYPE_*
   \param device serial device name
   \param host hostname
   \param port portnum
   \return fd
*/

int plmtype_connect(int plmtype, char *device, char *host, int portnum)
{
	int fd = -1;
	struct ev_token_bucket_cfg *ratelim;
	struct timeval rate = { 1, 0 };

	switch (plmtype) {
	case PLM_TYPE_SERIAL:
		if (device == NULL)
		    LOG(LOG_FATAL, "No serial device set.");
		fd = serial_connect(device, B19200,
				    CS8|CREAD|CLOCAL);

		plm_conn = smalloc(connection_t);
		plm_conn->bev = bufferevent_socket_new(base, fd,
						       BEV_OPT_CLOSE_ON_FREE);
		plm_conn->type = CONN_TYPE_PLM;
		bufferevent_setcb(plm_conn->bev, plm_readcb, NULL,
				  serial_eventcb, plm_conn);
		bufferevent_enable(plm_conn->bev, EV_READ|EV_WRITE);

		ratelim = ev_token_bucket_cfg_new(2400, 100, 25, 256, &rate);
		bufferevent_set_rate_limit(plm_conn->bev, ratelim);
		break;
	case PLM_TYPE_HUBPLM:
		if (host == NULL)
		    LOG(LOG_FATAL, "No hostname given");
		/* Warning, totally untested */
		hubplm_conn = smalloc(connection_t);
		hubplm_conn->port = portnum;
		hubplm_conn->type = CONN_TYPE_HUBPLM;
		hubplm_conn->host = host;
		connect_server_cb(0, 0, hubplm_conn);
		break;
	case PLM_TYPE_HUBHTTP:
		LOG(LOG_FATAL, "HTTP Not supported (yet)");
		break;
	}
	return fd;
}

/******************************************************
	Command Queue Routines
******************************************************/

/**
   \brief get number of hops for message
   \param dev device to get hops for
   \return hop flags
*/
int plm_get_hops(device_t *dev)
{
	insteon_devdata_t *dd = (insteon_devdata_t *)dev->localdata;

	if (dd->hopflag == 255)
		return PLMFLAGSET_STD3HOPS;

	return dd->hopflag;
}

/**
   \brief set the ideal number of hops for a device
   \param dev device to set hops for
   \param flag flag data from device message
*/
void plm_set_hops(device_t *dev, uint8_t flag)
{
	int hopsleft = 3;
	insteon_devdata_t *dd = (insteon_devdata_t *)dev->localdata;

	if (dd->hopflag != 255)
		return;

	if (flag & PLMFLAG_REMHOP1)
		hopsleft -= 2;
	if (flag & PLMFLAG_REMHOP2)
		hopsleft -= 1;

	switch (hopsleft) {
	case 0:
		dd->hopflag = 0;
		break;
	case 1:
		dd->hopflag = PLMFLAG_REMHOP1|PLMFLAG_MAXHOP1;
		break;
	case 2:
		dd->hopflag = PLMFLAG_REMHOP2|PLMFLAG_MAXHOP2;
		break;
	case 3:
	default:
		dd->hopflag = PLMFLAGSET_STD3HOPS;
		break;
	}
	LOG(LOG_DEBUG, "Setting device %s to maxhops=%d", dev->loc, hopsleft);
}

/**
   \brief Enqueue a standard command
   \param dev device to enqueue for
   \param com1 command 1
   \param com2 command 2
   \note Some commands, like status, must be sent as std, not cs style.
*/
void plm_enq_std(device_t *dev, uint8_t com1, uint8_t com2, uint8_t waitflags)
{
	cmdq_t *cmd;
	insteon_devdata_t *dd;

	if (dev->proto == PROTO_INSTEON_V2CS && com1 != STDCMD_STATUSREQ) {
		plm_enq_stdcs(dev, com1, com2, waitflags);
		return;
	}

	cmd = smalloc(cmdq_t);
	dd = (insteon_devdata_t *)dev->localdata;
	cmd->cmd[0] = PLM_START;
	cmd->cmd[1] = PLM_SEND;
	cmd->cmd[2] = dd->daddr[0];
	cmd->cmd[3] = dd->daddr[1];
	cmd->cmd[4] = dd->daddr[2];
	cmd->cmd[5] = plm_get_hops(dev);
	cmd->cmd[6] = com1;
	cmd->cmd[7] = com2;
	cmd->msglen = 8;
	cmd->sendcount = 0;
	cmd->wait = waitflags;
	cmd->state = waitflags|CMDQ_WAITSEND;
	SIMPLEQ_INSERT_TAIL(&cmdfifo, cmd, entries);
}

/**
   \brief Enqueue an extended command
   \param dev device to enqueue for
   \param com1 command 1
   \param com2 command 2
   \param data D1-D14
   \param waitflags waitflags
*/
void plm_enq_ext(device_t *dev, uint8_t com1, uint8_t com2, uint8_t *data,
		 uint8_t waitflags)
{
	cmdq_t *cmd;
	insteon_devdata_t *dd;

	cmd = smalloc(cmdq_t);
	dd = (insteon_devdata_t *)dev->localdata;
	cmd->cmd[0] = PLM_START;
	cmd->cmd[1] = PLM_SEND;
	cmd->cmd[2] = dd->daddr[0];
	cmd->cmd[3] = dd->daddr[1];
	cmd->cmd[4] = dd->daddr[2];
	cmd->cmd[5] = plm_get_hops(dev);
	cmd->cmd[5] |= PLMFLAG_EXT;
	cmd->cmd[6] = com1;
	cmd->cmd[7] = com2;
	memcpy((cmd->cmd)+8, data, 14);
	if (dev->proto == PROTO_INSTEON_V2CS)
		cmd->cmd[21] = plm_calc_cs(com1, com2, (cmd->cmd)+8);
	cmd->msglen = 22;
	cmd->sendcount = 0;
	cmd->wait = waitflags;
	cmd->state = waitflags|CMDQ_WAITSEND;
	SIMPLEQ_INSERT_TAIL(&cmdfifo, cmd, entries);
}

/**
   \brief Calculate the checksum byte for a i2cs message
   \param com1 command 1
   \param com2 command 2
   \param data D1-D14
   \return checksum
*/
uint8_t plm_calc_cs(uint8_t com1, uint8_t com2, uint8_t *data)
{
	int i, sum;
	uint8_t ret;

	sum = com1 + com2;
	for (i=0; i<14; i++)
		sum += data[i];

	ret = sum % 256;

	return (~ret + 1);
}

/**
   \brief Enqueue a standard command in i2cs format
   \param dev device to enqueue for
   \param com1 command 1
   \param com2 command 2
*/
void plm_enq_stdcs(device_t *dev, uint8_t com1, uint8_t com2,
		   uint8_t waitflags)
{
	cmdq_t *cmd;
	insteon_devdata_t *dd;

	cmd = smalloc(cmdq_t);
	dd = (insteon_devdata_t *)dev->localdata;
	cmd->cmd[0] = PLM_START;
	cmd->cmd[1] = PLM_SEND;
	cmd->cmd[2] = dd->daddr[0];
	cmd->cmd[3] = dd->daddr[1];
	cmd->cmd[4] = dd->daddr[2];
	cmd->cmd[5] = plm_get_hops(dev);
	cmd->cmd[5] |= PLMFLAG_EXT;
	cmd->cmd[6] = com1;
	cmd->cmd[7] = com2;
	/* smalloc is a zeroing malloc, so D1-14 are 0 */
	cmd->cmd[21] = plm_calc_cs(com1, com2, (cmd->cmd)+8);
	cmd->msglen = 22;
	cmd->sendcount = 0;
	cmd->wait = waitflags;
	cmd->state = waitflags|CMDQ_WAITSEND;
	SIMPLEQ_INSERT_TAIL(&cmdfifo, cmd, entries);
}

/**
   \brief queue up a wait, to give the plm a second to breathe
   \param howlong  how many queue cycles to wait
*/
void plm_enq_wait(int howlong)
{
	cmdq_t *cmd;

	cmd = smalloc(cmdq_t);
	cmd->cmd[0] = CMDQ_NOPWAIT;
	cmd->sendcount = howlong;
	SIMPLEQ_INSERT_TAIL(&cmdfifo, cmd, entries);
}

void plm_print_cmd(cmdq_t *cmd)
{
	LOG(LOG_DEBUG, "Command: Sendcount=%d msglen=%d state=%d wait=%d\n"
	    "%0.2X %0.2X %0.2X %0.2X %0.2X %0.2X %0.2X %0.2X %0.2X %0.2X "
	    "%0.2X %0.2X %0.2X %0.2X %0.2X %0.2X %0.2X %0.2X %0.2X %0.2X "
	    "%0.2X %0.2X %0.2X %0.2X %0.2X",
	    cmd->sendcount, cmd->msglen, cmd->state, cmd->wait,
	    cmd->cmd[0], cmd->cmd[1], cmd->cmd[2], cmd->cmd[3], cmd->cmd[4],
	    cmd->cmd[5], cmd->cmd[6], cmd->cmd[7], cmd->cmd[8], cmd->cmd[9],
	    cmd->cmd[10], cmd->cmd[11], cmd->cmd[12], cmd->cmd[13],
	    cmd->cmd[14], cmd->cmd[15], cmd->cmd[16], cmd->cmd[17],
	    cmd->cmd[18], cmd->cmd[19], cmd->cmd[20], cmd->cmd[21],
	    cmd->cmd[22], cmd->cmd[23], cmd->cmd[24]);
}

/**
   \brief Condense the run queue
*/

void plm_condense_runq(void)
{
	cmdq_t *cmd, *chk, *tmp;

	if (SIMPLEQ_EMPTY(&cmdfifo))
		return;
	cmd = SIMPLEQ_FIRST(&cmdfifo);
	if (cmd == NULL)
		return;

	/* don't dequeue ourselves, and don't dequeue waits */
	SIMPLEQ_FOREACH_SAFE(chk, &cmdfifo, entries, tmp)
		if (chk != cmd && chk->cmd[0] != CMDQ_NOPWAIT &&
		    memcmp(cmd->cmd, chk->cmd, 25) == 0) {
			LOG(LOG_DEBUG, "Removing duplicate cmd entry");
			plm_print_cmd(chk);
			LOG(LOG_DEBUG, "Duplicate OF:");
			plm_print_cmd(cmd);
			SIMPLEQ_REMOVE(&cmdfifo, chk, _cmdq_t, entries);
			free(chk);
		}
	SIMPLEQ_FOREACH_SAFE(chk, &cmdfifo, entries, tmp)
		if (chk != cmd && memcmp(cmd->cmd, chk->cmd, 8) == 0 &&
		    cmd->cmd[1] == PLM_SEND) {
			LOG(LOG_DEBUG, "Removing early command entry");
			SIMPLEQ_REMOVE(&cmdfifo, cmd, _cmdq_t, entries);
			free(cmd);
			return;
		}
}

void plm_check_proper_delay(uint8_t *devaddr)
{
	cmdq_t *delay, *chk, *tmp, *cmd;

	if (SIMPLEQ_EMPTY(&cmdfifo))
		return;

	delay = NULL;
	SIMPLEQ_FOREACH_SAFE(chk, &cmdfifo, entries, tmp)
		if (chk->cmd[0] == CMDQ_NOPWAIT) {
			delay = chk;
			break;
		}
	SIMPLEQ_FOREACH_SAFE(chk, &cmdfifo, entries, tmp) {
		if (chk->cmd[1] == PLM_SEND && chk->cmd[2] == devaddr[0] &&
		    chk->cmd[3] == devaddr[1] && chk->cmd[4] == devaddr[2]) {
			/* we are sending to this device */
			if (delay == NULL) {
				LOG(LOG_DEBUG, "Adding delay to head");
				cmd = smalloc(cmdq_t);
				cmd->cmd[0] = CMDQ_NOPWAIT;
				cmd->sendcount = 10;
				SIMPLEQ_INSERT_HEAD(&cmdfifo, cmd, entries);
			} else {
				LOG(LOG_DEBUG, "Extending first delay");
				delay->sendcount = 10;
			}
			break;
		}
	}
}

/**
   \brief Run the queue, see if anything is ready
   \param fd unused
   \param what unused
   \param arg pointer to connection_t
*/

void plm_runq(int fd, short what, void *arg)
{
	cmdq_t *cmd;
	connection_t *conn = (connection_t *)arg;
	struct timespec tp, chk, qsec = { 3 , 500000000L };
	struct timespec alink = { 5, 0 };
	struct timespec aldb = { 10, 0 };
	char dsend[256];

	//LOG(LOG_DEBUG, "Queue runner entered");
	plm_condense_runq();
again:
	if (SIMPLEQ_EMPTY(&cmdfifo)) {
		plm_queue_empty_cb(conn);
		return;
	}

	cmd = SIMPLEQ_FIRST(&cmdfifo);
	if (cmd == NULL)
		return;

	if (cmd->cmd[0] == CMDQ_NOPWAIT) {
		LOG(LOG_DEBUG, "Runqueue sleeping");
		/* we have a sleep request */
		if (cmd->sendcount <= 0) {
			SIMPLEQ_REMOVE_HEAD(&cmdfifo, entries);
			free(cmd);
			return;
		} else {
			cmd->sendcount -= 1;
			return;
		}
	}

	if (!cmd->state || (cmd->state && (cmd->sendcount > CMDQ_MAX_SEND))) {
		/* dequeue */
		LOG(LOG_DEBUG, "dequeueing current cmd");
		SIMPLEQ_REMOVE_HEAD(&cmdfifo, entries);
		free(cmd);
		goto again;
	}
	if ((cmd->state & CMDQ_WAITSEND) && cmd->msglen > 0) {
		/* send it */
		cmd->sendcount++;
		cmd->state &= ~CMDQ_WAITSEND;
		clock_gettime(CLOCK_MONOTONIC, &cmd->tp);
		bufferevent_write(conn->bev, cmd->cmd, cmd->msglen);
		LOG(LOG_DEBUG, "Running a qevent to:%0.2X.%0.2X.%0.2X "
		    "cmd:%0.2x/%0.2x state/w:%0.2x/%0.2x",
		    cmd->cmd[2], cmd->cmd[3], cmd->cmd[4],
		    cmd->cmd[6], cmd->cmd[7], cmd->state, cmd->wait);
#if 1
		int i;

		sprintf(dsend, "DATA:");
		for (i=0; i < cmd->msglen; i++)
			sprintf(dsend, "%s%0.2x ", dsend, cmd->cmd[i]);
		LOG(LOG_DEBUG, dsend);
#endif
		return;
	}
	if (cmd->state) {
		/* check if too much time has passed */
		clock_gettime(CLOCK_MONOTONIC, &tp);
		if (cmd->state & CMDQ_WAITALINK)
			timespecadd(&cmd->tp, &alink, &chk);
		else if (cmd->state & CMDQ_WAITALDB)
			timespecadd(&cmd->tp, &aldb, &chk);
		else
			timespecadd(&cmd->tp, &qsec, &chk);
		if (timespeccmp(&tp, &chk, >)) {
			/* if current time is greater than run + wait */
			if (cmd->sendcount >= CMDQ_MAX_SEND) {
				/* deallocate and punt */
				LOG(LOG_DEBUG, "Deallocating qevent");
				SIMPLEQ_REMOVE_HEAD(&cmdfifo, entries);
				free(cmd);
				goto again;
			}
			/* otherwise, resend */
			LOG(LOG_DEBUG, "Retrying command");
			cmd->state = cmd->wait|CMDQ_WAITSEND;
			goto again;
		} else
			return;
	}
}

/**
   \brief Retry the current command
*/
void plmcmdq_retry_cur(void)
{
	cmdq_t *cmd;

	cmd = SIMPLEQ_FIRST(&cmdfifo);
	if (cmd == NULL)
		return;

	cmd->state = cmd->wait | CMDQ_WAITSEND;
	cmd->sendcount++;
}

/**
   \brief Mark a cmd with what kind of data we got
   \param whatkind CMDQ_WAIT*
*/
void plmcmdq_got_data(int whatkind)
{
	cmdq_t *cmd;

	cmd = SIMPLEQ_FIRST(&cmdfifo);
	if (cmd == NULL)
		return;

	cmd->state &= ~whatkind;
	clock_gettime(CLOCK_MONOTONIC, &cmd->tp);
}

/**
   \brief Check for same command on a cmdq
   \param data data returned to plm
*/

void plmcmdq_check_ack(char *data)
{
	cmdq_t *cmd;
	uint8_t ackbit;

	cmd = SIMPLEQ_FIRST(&cmdfifo);
	if (cmd == NULL)
		return;

	switch (cmd->cmd[1]) {
	case PLM_ALINK_START:
		ackbit = data[4];
		break;
	case PLM_SEND:
		if (cmd->msglen > 20)
			ackbit = data[22];
		else
			ackbit = data[8];
		break;
	}

	if (cmd->msglen < 8)
		goto ackdata;

	if (!(cmd->cmd[1] == PLM_SEND || cmd->cmd[1] == PLM_SEND_X10))
		goto ackdata;

	if (memcmp(data, cmd->cmd, 5) == 0 &&
	    memcmp(data+6, (cmd->cmd)+6 , 2) == 0)
		goto ackdata;

	return;

	ackdata:
	if (ackbit == PLMCMD_ACK) {
		cmd->state &= ~CMDQ_WAITACK;
		clock_gettime(CLOCK_MONOTONIC, &cmd->tp);
	} else
		plmcmdq_retry_cur();
}

/**
   \brief Dequeue the current command
*/
void plmcmdq_dequeue(void)
{
	if (SIMPLEQ_FIRST(&cmdfifo) != NULL)
		SIMPLEQ_REMOVE_HEAD(&cmdfifo, entries);
}

/**
   \brief Check for same command on a recv
   \param fromaddr msg from
   \param toaddr msg to
   \param cmd1 command1
   \param whatkind CMDQ_WAIT*
   \return bool
*/

void plmcmdq_check_recv(char *fromaddr, char *toaddr, uint8_t cmd1,
			int whatkind)
{
	cmdq_t *cmd;

	cmd = SIMPLEQ_FIRST(&cmdfifo);
	if (cmd == NULL)
		return;

/*	LOG(LOG_DEBUG, "data: %0.2X.%0.2X.%0.2X queue: %0.2X.%0.2X.%0.2X "
	    "datacmd: %0.2X queuecmd: %0.2X",
	    fromaddr[0], fromaddr[1], fromaddr[2],
	    cmd->cmd[2], cmd->cmd[3], cmd->cmd[4],
	    cmd1, cmd->cmd[6]);
*/
	if (memcmp(fromaddr, (cmd->cmd)+2, 3) == 0 &&
	    cmd1 == cmd->cmd[6] &&
	    memcmp(toaddr, plm_addr, 3) == 0)
		plmcmdq_got_data(whatkind);

	/* without cmd, for any */
	if (memcmp(fromaddr, (cmd->cmd)+2, 3) == 0 &&
	    memcmp(toaddr, plm_addr, 3) == 0)
		plmcmdq_got_data(CMDQ_WAITANY);

	return;
}


/*****************************************************
	PLM Commands
*****************************************************/

/**
   \brief Ask the PLM for it's info
*/
void plm_getinfo(void)
{
	cmdq_t *cmd;

	cmd = smalloc(cmdq_t);
	cmd->cmd[0] = PLM_START;
	cmd->cmd[1] = PLM_GETINFO;
	cmd->msglen = 2;
	cmd->sendcount = 0;
	cmd->wait = CMDQ_WAITACK;
	cmd->state = CMDQ_WAITACK|CMDQ_WAITSEND;
	SIMPLEQ_INSERT_TAIL(&cmdfifo, cmd, entries);
}

/**
   \brief Ask the PLM for the ALDB records
   \param fl 0==first 1==last
*/
void plm_getplm_aldb(int fl)
{
	cmdq_t *cmd;

	LOG(LOG_DEBUG, "Asking for aldb record %d", fl);
	cmd = smalloc(cmdq_t);
	cmd->cmd[0] = PLM_START;
	if (fl == 0){
		cmd->cmd[1] = PLM_ALINK_GETFIRST;
		plmaldbmorerecords = 1;
	} else
		cmd->cmd[1] = PLM_ALINK_GETNEXT;
	cmd->msglen = 2;
	cmd->sendcount = 0;
	cmd->wait = CMDQ_WAITACK;
	cmd->state = CMDQ_WAITACK|CMDQ_WAITSEND;
	SIMPLEQ_INSERT_TAIL(&cmdfifo, cmd, entries);
}

/**
   \brief Put the PLM into all link mode
   \param linkcode Type of all link
   \param group group number
*/
void plm_all_link(uint8_t linkcode, uint8_t group)
{
	cmdq_t *cmd;

	cmd = smalloc(cmdq_t);
	cmd->cmd[0] = PLM_START;
	cmd->cmd[1] = PLM_ALINK_START;
	cmd->cmd[2] = linkcode;
	cmd->cmd[3] = group;
	cmd->msglen = 4;
	cmd->sendcount = 0;
	cmd->wait = CMDQ_WAITACKDATA|CMDQ_WAITALINK;
	cmd->state = CMDQ_WAITACK|CMDQ_WAITALINK|CMDQ_WAITSEND;
	SIMPLEQ_INSERT_TAIL(&cmdfifo, cmd, entries);
}

/**
   \brief Request the engine versions
   \param conn the connection_t
*/

void plm_req_aldb(device_t *dev)
{
	char data[14];

	/* set to all zeros to get a dump */
	memset(data, 0, 14);
	plm_enq_ext(dev, EXTCMD_RWALDB, 0x00, data,
		    CMDQ_WAITACK|CMDQ_WAITDATA|CMDQ_WAITALDB);
}

/**
   \brief Turn a switch ON
   \param dev device to turn on
   \param level new level
*/
void plm_switch_on(device_t *dev, uint8_t level)
{
	plm_enq_std(dev, STDCMD_ON, level, CMDQ_WAITACKDATA);
}

/**
   \brief Turn a switch OFF
   \param dev device to turn off
   \param level new level
*/
void plm_switch_off(device_t *dev)
{
	plm_enq_std(dev, STDCMD_OFF, 0, CMDQ_WAITACKDATA);
}


/*********************************************************
	PLM Handlers
*********************************************************/


/**
   \brief Handle a getinfo command
   \param data recieved
*/

void plm_handle_getinfo(uint8_t *data)
{
	char im[16];

	if (data[8] != PLMCMD_ACK) {
		LOG(LOG_ERROR, "PLM Getinfo failed!");
		return;
	}
	memcpy(plm_addr, data+2, 3);
	addr_to_string(im, plm_addr);
	LOG(LOG_NOTICE, "PLM address: %s devcat: %0.2X subcat: %0.2X "
	    "Firmware: %0.2X", im, data[5], data[6], data[7]);
	plmcmdq_dequeue();
}

/**
   \brief Write an aldb record
   \param dev device with record to write
   \param recno record number to write
*/

void plm_write_aldb_record(device_t *dev, int recno)
{
	insteon_devdata_t *dd = (insteon_devdata_t *)dev->localdata;
	uint8_t data[14];

	if (recno > ALDB_MAXSIZE) {
		LOG(LOG_ERROR, "Request to write record number > ALDB_MAXSIZE");
		return;
	}
	memset(data, 0, 14);
	/* rewrite the record address, just in case it's all 0's */
	dd->aldb[recno].addr = 0x0FFF - (8 * recno);
	data[1] = 0x02; /* write aldb record */
	data[2] = ((dd->aldb[recno].addr&0xFF00)>>8);
	data[3] = (dd->aldb[recno].addr&0x00FF);
	data[4] = 0x08; /* 8 bytes of record */
	data[5] = dd->aldb[recno].lflags;
	data[6] = dd->aldb[recno].group;
	data[7] = dd->aldb[recno].devaddr[0];
	data[8] = dd->aldb[recno].devaddr[1];
	data[9] = dd->aldb[recno].devaddr[2];
	data[10] = dd->aldb[recno].ldata1;
	data[11] = dd->aldb[recno].ldata2;
	data[12] = dd->aldb[recno].ldata3;

	LOG(LOG_DEBUG, "Sending write ALDB request for recno %d", recno);
	plm_enq_ext(dev, EXTCMD_RWALDB, 0x00, data,
		    CMDQ_WAITACK|CMDQ_WAITDATA);
}

/**
   \brief Write the entire ALDB
   \param dev device to write aldb of
*/
void plm_write_aldb(device_t *dev)
{
	insteon_devdata_t *dd = (insteon_devdata_t *)dev->localdata;
	int i;

	for (i=0; i < dd->aldblen; i++)
		plm_write_aldb_record(dev, i);
}

/**
   \brief handle an aldb record
   \param dev device that got an aldb record
   \param data extended data D1-D14
   \return 1 if last record
*/
int plm_handle_aldb(device_t *dev, char *data)
{
	int i, recno;
	aldb_t rec;
	insteon_devdata_t *dd = (insteon_devdata_t *)dev->localdata;
	device_group_t *devgrp;
	char gn[16], ln[16];
	device_t *link;

	memset(&rec, 0, sizeof(aldb_t));
	rec.addr = data[3] | (data[2]<<8);
	rec.lflags = data[5];
	rec.group = data[6];
	rec.devaddr[0] = data[7];
	rec.devaddr[1] = data[8];
	rec.devaddr[2] = data[9];
	rec.ldata1 = data[10];
	rec.ldata2 = data[11];
	rec.ldata3 = data[12];

	recno = (0x0FFF - rec.addr) / 8;
	if (recno >= ALDB_MAXSIZE) {
		LOG(LOG_ERROR,"ALDB too large, need to increase ALDB_MAXSIZE");
		return 1;
	}
	memcpy(&dd->aldb[recno], &rec, sizeof(aldb_t));

	/* Build a device group? */
	if (rec.lflags & ALDBLINK_MASTER) {
		sprintf(gn, "%s-%0.2X", dev->loc, rec.group);
		addr_to_string(ln, rec.devaddr);
		devgrp = find_devgroup_byuid(gn);
		if (devgrp == NULL) {
			LOG(LOG_DEBUG, "Building new group %s", gn);
			devgrp = new_devgroup(gn);
			devgrp->name = strdup(gn); /* for now */
		}
		/* add self to group */
		if (!dev_in_group(dev, devgrp))
			add_dev_group(dev, devgrp);
		link = find_device_byuid(ln);
		if (link != NULL) {
			if (!dev_in_group(link, devgrp))
				add_dev_group(link, devgrp);
		}
	}

	for (i=0; i < ALDB_MAXSIZE; i++)
		if (dd->aldb[i].addr == 0) {
			dd->aldblen = i;
			break;
		}
	/* final record seems to be all zeros */
	if (data[5] == 0 && data[6] == 0 && data[7] == 0 && data[8] == 0 &&
	    data[9] == 0 && data[10] == 0 && data[11] == 0 && data[12] == 0)
		return 1;
	return 0;
}



/*********************************************************
	Event Stuff
*********************************************************/

/**
   \brief A general callback for PLM reads
   \param bev bufferevent
   \param arg pointer to connection_t
*/

void plm_readcb(struct bufferevent *bev, void *arg)
{
	connection_t *conn = (connection_t *)arg;
	struct evbuffer *evbuf;
	char *plmhead, *data;
	uint8_t toaddr[3], fromaddr[3], extdata[14], ackbit;
	cmdq_t *cmd;

	evbuf = bufferevent_get_input(bev);

moredata:
	plmhead = evbuffer_pullup(evbuf, 2);
	if (plmhead == NULL)
		return; /* need more data, so wait */
	LOG(LOG_DEBUG, "Pullup: 0x%0.2X 0x%0.2X", plmhead[0], plmhead[1]);

	if (plmhead[0] != PLM_START) {
		if (plmhead[0] == PLMCMD_NAK)
			LOG(LOG_ERROR, "Previous cmd got NAK'd");
		else
			LOG(LOG_ERROR, "Got funny data from PLM: "
			    "0x%0.2X 0x%0.2X", plmhead[0], plmhead[1]);
		evbuffer_drain(evbuf, 1); /* XXX */
		goto moredata; /* ? I guess. */
	}

	cmd = SIMPLEQ_FIRST(&cmdfifo);

	/* Look at the command, and figure out what to do about it */
	switch (plmhead[1]) {
	case PLM_SEND: /* confirmation of send */
		data = evbuffer_pullup(evbuf, 9);
		if (data == NULL)
			return;
		if (data[5] & PLMFLAG_EXT) {
			ackbit = data[22];
			data = evbuffer_pullup(evbuf, 23);
			if (data == NULL)
				return;
		} else
			ackbit = data[8];
		if (ackbit == PLMCMD_ACK)
			LOG(LOG_DEBUG, "PLM Command Success: %0.2X", data[1]);
		else { /* we got a bad retcode? */
			LOG(LOG_ERROR, "PLM Command failed, 0x%0.2X", data[1]);
			LOG(LOG_DEBUG, "Data: %0.2X %0.2X %0.2X %0.2X "
			    "%0.2X %0.2X %0.2X %0.2X %0.2X",
			    data[0], data[1], data[2], data[3], data[4],
			    data[5], data[6], data[7], data[8], data[9]);
			if (data[5] & PLMFLAG_EXT)
				LOG(LOG_DEBUG, "EXT: "
				    "%0.2X %0.2X %0.2X %0.2X %0.2X "
				    "%0.2X %0.2X %0.2X %0.2X %0.2X "
				    "%0.2X %0.2X %0.2X",
				    data[10], data[11], data[12], data[13],
				    data[14], data[15], data[16], data[17],
				    data[18], data[19], data[20], data[21],
				    data[22]);
		}
		plmcmdq_check_ack(data);
		if (data[5] & PLMFLAG_EXT)
			evbuffer_drain(evbuf, 23);
		else
			evbuffer_drain(evbuf, 9); /* discard echo */
		break;
	case PLM_RECV_STD: /* we have a std msg */
		data = evbuffer_pullup(evbuf, 11);
		if (data == NULL)
			return;
		evbuffer_remove(evbuf, data, 11);
		memcpy(fromaddr, data+2, 3);
		memcpy(toaddr, data+5, 3);
		plm_handle_stdrecv(fromaddr, toaddr, data[8], data[9],
				   data[10], conn);
		break;
	case PLM_RECV_EXT:
		data = evbuffer_pullup(evbuf, 25);
		if (data == NULL)
			return;
		evbuffer_remove(evbuf, data, 25);
		memcpy(fromaddr, data+2, 3);
		memcpy(toaddr, data+5, 3);
		memcpy(extdata, data+11, 14);
		plm_handle_extrecv(fromaddr, toaddr, data[8], data[9],
				   data[10], extdata, conn);
		break;
	case PLM_RECV_X10:
		data = evbuffer_pullup(evbuf, 4);
		if (data == NULL)
			return;
		LOG(LOG_DEBUG, "Got X10 mesage: %0.2X %0.2X", data[2],
		    data[3]);
		evbuffer_remove(evbuf, data, 4);
		break;
	case PLM_SEND_X10: /* not supported yet, but, umm, maybe? */
		data = evbuffer_pullup(evbuf, 5);
		if (data == NULL)
			return;
		LOG(LOG_DEBUG, "Sent X10 mesage: %0.2X %0.2X", data[2],
		    data[3]);
		evbuffer_remove(evbuf, data, 5);
		break;
	case PLM_GETINFO:
		data = evbuffer_pullup(evbuf, 9);
		if (data == NULL)
			return;
		evbuffer_remove(evbuf, data, 9);
		plm_handle_getinfo(data);
		break;
	case PLM_ALINK_COMPLETE:
		data = evbuffer_pullup(evbuf, 10);
		if (data == NULL)
			return;
		evbuffer_remove(evbuf, data, 10);
		plm_handle_alink_complete(data);
		break;
	case PLM_ALINK_CFAILREP:
		data = evbuffer_pullup(evbuf, 7);
		if (data == NULL)
			return;
		LOG(LOG_WARNING, "Got all-link failure for device %X.%X.%X"
		    " group %X", data[4], data[5], data[6], data[3]);
		evbuffer_remove(evbuf, data, 7);
		break;
	case PLM_ALINK_CSTATUSREP:
		data = evbuffer_pullup(evbuf, 3);
		if (data == NULL)
			return;
		LOG(LOG_NOTICE, "All link cleanup status: %X", data[2]);
		evbuffer_remove(evbuf, data, 3);
		break;
	case PLM_ALINK_START:
		data = evbuffer_pullup(evbuf, 5);
		if (data == NULL)
			return;
		plmcmdq_check_ack(data);
		LOG(LOG_NOTICE, "Starting all link code:%X group:%X",
		    data[2], data[3]);
		evbuffer_remove(evbuf, data, 5);
		break;
	case PLM_ALINK_CANCEL:
		data = evbuffer_pullup(evbuf, 3);
		if (data == NULL)
			return;
		LOG(LOG_NOTICE, "All link cancelled");
		evbuffer_remove(evbuf, data, 3);
		break;
	case PLM_ALINK_SEND:
		data = evbuffer_pullup(evbuf, 6);
		if (data == NULL)
			return;
		LOG(LOG_NOTICE, "Sent all-link command %X/%X to group %X"
		    " Status: %X", data[3], data[4], data[2], data[5]);
		evbuffer_remove(evbuf, data, 6);
		break;
	case PLM_ALINK_GETLASTRECORD:
		data = evbuffer_pullup(evbuf, 3);
		if (data == NULL)
			return;
		LOG(LOG_DEBUG, "Sent get last record");
		evbuffer_remove(evbuf, data, 3);
		break;
	case PLM_ALINK_GETFIRST:
	case PLM_ALINK_GETNEXT:
		data = evbuffer_pullup(evbuf, 3);
		if (data == NULL)
			return;
		if (data[1] == cmd->cmd[1])
			plmcmdq_dequeue(); /* dequeue */
		if (data[2] == PLMCMD_ACK) /* more records in db */
			plm_getplm_aldb(1); /* get next record */
		else if (data[2] == PLMCMD_NAK) { /* last one */
			plmaldbmorerecords = 0;
			LOG(LOG_NOTICE, "Last PLM ALDB record");
		}
		evbuffer_remove(evbuf, data, 3);
		break;
	case PLM_ALINK_RECORD:
		data = evbuffer_pullup(evbuf, 10);
		if (data == NULL)
			return;
		evbuffer_remove(evbuf, data, 10);
		plm_handle_aldb_record_resp(data);
		break;
	case PLM_BUTTONEVENT:
		data = evbuffer_pullup(evbuf, 3);
		if (data == NULL)
			return;
		LOG(LOG_DEBUG, "Got button press event 0x%X", data[2]);
		evbuffer_remove(evbuf, data, 3);
		break;
	case PLM_RESET: /* seriously??! */
		data = evbuffer_pullup(evbuf, 2);
		if (data == NULL)
			return;
		LOG(LOG_ERROR, "PLM Has been manually reset!!");
		evbuffer_remove(evbuf, data, 2);
		break;
	case PLM_SETHOST: /* ?? */
		data = evbuffer_pullup(evbuf, 6);
		if (data == NULL)
			return;
		LOG(LOG_ERROR, "PLM Sent Set Host Device Category?");
		evbuffer_remove(evbuf, data, 6);
		break;
	case PLM_FULL_RESET:
		data = evbuffer_pullup(evbuf, 3);
		if (data == NULL)
			return;
		LOG(LOG_ERROR, "PLM Has been full reset!!");
		evbuffer_remove(evbuf, data, 3);
		break;
	case PLM_SET_ACK1:
		data = evbuffer_pullup(evbuf, 4);
		if (data == NULL)
			return;
		LOG(LOG_NOTICE, "Set Ack1 bit?");
		evbuffer_remove(evbuf, data, 4);
		break;
	case PLM_SET_NAK1:
		data = evbuffer_pullup(evbuf, 4);
		if (data == NULL)
			return;
		LOG(LOG_NOTICE, "Set NAK1 bit?");
		evbuffer_remove(evbuf, data, 4);
		break;
	case PLM_SET_ACK2:
		data = evbuffer_pullup(evbuf, 5);
		if (data == NULL)
			return;
		LOG(LOG_NOTICE, "Set Ack2 bit?");
		evbuffer_remove(evbuf, data, 5);
		break;
	case PLM_SETCONF:
		data = evbuffer_pullup(evbuf, 4);
		if (data == NULL)
			return;
		LOG(LOG_NOTICE, "Set PLM Config bits to %X", data[2]);
		evbuffer_remove(evbuf, data, 4);
		break;
	case PLM_LEDON:
		data = evbuffer_pullup(evbuf, 3);
		if (data == NULL)
			return;
		LOG(LOG_ERROR, "Set PLM LED On");
		evbuffer_remove(evbuf, data, 3);
		break;
	case PLM_LEDOFF:
		data = evbuffer_pullup(evbuf, 3);
		if (data == NULL)
			return;
		LOG(LOG_ERROR, "Set PLM LED Off");
		evbuffer_remove(evbuf, data, 3);
		break;
	case PLM_ALINK_MODIFY: /* unhandled */
		data = evbuffer_pullup(evbuf, 12);
		if (data == NULL)
			return;
		LOG(LOG_ERROR, "Sent modify all-link record");
		evbuffer_remove(evbuf, data, 12);
		break;
	case PLM_SLEEP:
		data = evbuffer_pullup(evbuf, 5);
		if (data == NULL)
			return;
		LOG(LOG_WARNING, "PLM set to SLEEP mode");
		evbuffer_remove(evbuf, data, 5);
		break;
	default:
		data = evbuffer_pullup(evbuf, 2);
		if (data == NULL)
			return;
		LOG(LOG_ERROR, "PLM Sending unknown response: %X %X",
		    data[0], data[1]);
		LOG(LOG_ERROR, "Draining buffer and ignoring");
		evbuffer_remove(evbuf, data, 2);
		break;
	}
}
