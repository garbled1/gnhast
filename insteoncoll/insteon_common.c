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
#include <sys/queue.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>

#include "gnhast.h"
#include "common.h"
#include "confparser.h"
#include "confuse.h"
#include "insteon.h"

extern int errno;
extern int debugmode;
extern TAILQ_HEAD(, _device_t) alldevs;
extern int nrofdevs;
extern FILE *logfile;
extern struct event_base *base;
extern connection_t *plm_conn;
extern uint8_t plm_addr[3];

SIMPLEQ_HEAD(fifohead, _cmdq_t) cmdfifo;
char *conntype[3] = {
	"none",
	"plm",
	"gnhastd",
};

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
	int hop;
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
*/
void plm_enq_std(device_t *dev, uint8_t com1, uint8_t com2, uint8_t waitflags)
{
	cmdq_t *cmd;
	insteon_devdata_t *dd;
	uint8_t *daddr;

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
   \brief Run the queue, see if anything is ready
   \param fd unused
   \param what unused
   \param arg pointer to connection_t
*/

void plm_runq(int fd, short what, void *arg)
{
	cmdq_t *cmd;
	connection_t *conn = (connection_t *)arg;
	struct timespec tp, chk, qsec = { 2 , 500000000L };
	struct timespec alink = { 5, 0 };
	int i;

	//LOG(LOG_DEBUG, "Queue runner entered");
again:
	if (SIMPLEQ_EMPTY(&cmdfifo)) {
		plm_queue_empty_cb(conn);
		return;
	}

	cmd = SIMPLEQ_FIRST(&cmdfifo);
	if (cmd == NULL)
		return;

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
#if 0
		printf("SENDING: ");
		for (i=0; i < cmd->msglen; i++)
			printf("%0.2x ", cmd->cmd[i]);
		printf("\n");
#endif
		return;
	}
	if (cmd->state) {
		/* check if too much time has passed */
		clock_gettime(CLOCK_MONOTONIC, &tp);
		if (cmd->state & CMDQ_WAITALINK)
			timespecadd(&cmd->tp, &alink, &chk);
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
   \brief Handle a std length recv
   \param fromaddr Who from?
   \param toaddr who to?
   \param flags message flags
   \param com1 command1
   \param com2 command2
*/

void plm_handle_stdrecv(uint8_t *fromaddr, uint8_t *toaddr, uint8_t flags,
			uint8_t com1, uint8_t com2, connection_t *conn)
{
	char fa[16], ta[16];
	device_t *dev;

	addr_to_string(fa, fromaddr);
	addr_to_string(ta, toaddr);

	LOG(LOG_DEBUG, "StdMesg from:%s to %s cmd1,2:0x%0.2X,0x%0.2X "
	    "flags:0x%0.2X", fa, ta, com1, com2, flags);
	plmcmdq_check_recv(fromaddr, toaddr, com1, CMDQ_WAITDATA);

	dev = find_device_byuid(fa);
	if (dev == NULL) {
		LOG(LOG_ERROR, "Unknown device %s sent stdmsg", fa);
		return;
	}

	switch (com1) {
	case STDCMD_GETVERS:
		switch (com2) {
		case 0x00:
			dev->proto = PROTO_INSTEON_V1;
			break;
		case 0x01:
			dev->proto = PROTO_INSTEON_V2;
			break;
		case 0x02:
			dev->proto = PROTO_INSTEON_V2CS;
			break;
		case 0xFF:
			dev->proto = PROTO_INSTEON_V2CS;
			LOG(LOG_WARNING, "Device %s is i2cs not linked to PLM");
			break;
		}
		break;
	case STDCMD_PING:
		plm_set_hops(dev, flags);
		break;
	}
}

/**
   \brief Handle an extended length recv
   \param fromaddr Who from?
   \param toaddr who to?
   \param flags message flags
   \param com1 command1
   \param com2 command2
*/

void plm_handle_extrecv(uint8_t *fromaddr, uint8_t *toaddr, uint8_t flags,
			uint8_t com1, uint8_t com2, uint8_t *ext,
			connection_t *conn)
{
	char fa[16], ta[16];

	addr_to_string(fa, fromaddr);
	addr_to_string(ta, toaddr);

	LOG(LOG_DEBUG, "ExtMesg from:%s to %s cmd1,2:0x%0.2X,0x%0.2X "
	    "flags:0x%0.2X", fa, ta, com1, com2, flags);
	LOG(LOG_DEBUG, "ExtMesg data:0x%0.2X 0x%0.2X 0x%0.2X 0x%0.2X "
	    "0x%0.2X 0x%0.2X 0x%0.2X 0x%0.2X 0x%0.2X 0x%0.2X 0x%0.2X "
	    "0x%0.2X 0x%0.2X 0x%0.2X", ext[0], ext[1], ext[2], ext[3],
	    ext[4], ext[5], ext[6], ext[7], ext[8], ext[9], ext[10],
	    ext[11], ext[12], ext[13]);
	plmcmdq_check_recv(fromaddr, toaddr, com1, CMDQ_WAITEXT);
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
		LOG(LOG_ERROR, "Got funny data from PLM: 0x%0.2X 0x%0.2X",
		    plmhead[0], plmhead[1]);
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
				    data[21]);
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
	case PLM_ALINK_START:
		data = evbuffer_pullup(evbuf, 5);
		if (data == NULL)
			return;
		plmcmdq_check_ack(data);
		evbuffer_remove(evbuf, data, 5);
		break;
	}

}

/**
   \brief connect to a serial device
   \param devnode path to device node
   \param speed speed of conneciton
   \param cflags control flags
*/

int serial_connect(char *devnode, int speed, int cflags)
{
	struct termios tio;
	int sfd;

	if ((sfd = open(devnode, O_RDWR|O_EXCL|O_NONBLOCK)) == -1)
		LOG(LOG_FATAL, "Cannot open `%s'", devnode);

	if (tcgetattr(sfd, &tio) == -1)
		LOG(LOG_FATAL, "Cannot getattr for `%s'", devnode);

	cfsetispeed(&tio, speed);
	cfsetospeed(&tio, speed);
	tio.c_iflag = 0;
	tio.c_oflag = 0;
	tio.c_cflag = cflags;
	tio.c_lflag = 0;

	if (tcsetattr(sfd, TCSANOW, &tio) == -1)
		LOG(LOG_FATAL, "Cannot setattr for `%s'", devnode);

	return sfd;
}

/**
   \brief General eventcb for a serial device
   \param bev bufferevent
   \param events what happened
   \param arg pointer to connection_t
*/

void serial_eventcb(struct bufferevent *bev, short events, void *arg)
{
	if (events & (BEV_EVENT_ERROR|BEV_EVENT_EOF)) {
		LOG(LOG_ERROR, "Got an error on serial port.  Now what?");
	}
}
