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

	if (dev->proto == PROTO_INSTEON_V2CS) {
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
