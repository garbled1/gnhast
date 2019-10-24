/*
 * Copyright (c) 2013
 *	  Tim Rightnour.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 * 3. The name of Tim Rightnour may not be used to endorse or promote 
 *	products derived from this software without specific prior written 
 *	permission.
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
   \file serial_common.c
   \author Tim Rightnour
   \brief Serial device common functions
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/queue.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <termios.h>
#include <unistd.h>

#include "common.h"

/**
   \brief convert baud to printable int
   \param baud
   \return int

   I'll skip some of the useless ones, just because it's annoying.
*/
int get_baud(speed_t speed)
{
    switch (speed) {
    case B0:
	return 0;
    case B300:
	return 300;
    case B2400:
	return 2400;
    case B4800:
	return 4800;
    case B9600:
	return 9600;
    case B19200:
	return 19200;
    case B38400:
	return 38400;
    case B57600:
	return 57600;
    case B115200:
	return 115200;
    case B230400:
	return 230400;
    default:
	return -1;
    }
}

/**
   \brief connect to a serial device
   \param devnode path to device node
   \param speed speed of conneciton
   \param cflags control flags
*/

int serial_connect(char *devnode, speed_t speed, tcflag_t cflags)
{
	struct termios tio;
	int sfd;

	if ((sfd = open(devnode, O_RDWR|O_EXCL|O_NONBLOCK)) == -1)
		LOG(LOG_FATAL, "Cannot open `%s'", devnode);

	if (tcgetattr(sfd, &tio) == -1)
		LOG(LOG_FATAL, "Cannot getattr for `%s'", devnode);

	tio.c_iflag = 0;
	tio.c_oflag = 0;
	tio.c_cflag = cflags;
	tio.c_lflag = 0;
	cfsetispeed(&tio, speed);
	cfsetospeed(&tio, speed);

	if (tcsetattr(sfd, TCSANOW, &tio) == -1)
		LOG(LOG_FATAL, "Cannot setattr for `%s'", devnode);

	LOG(LOG_NOTICE, "Connected to serial device %s at %dbps", devnode,
	    get_baud(speed));

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
