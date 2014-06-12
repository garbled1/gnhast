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
   \file script_handler.c
   \brief Launches external handlers in response to events
   \author Tim Rightnour
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/queue.h>
#include <sys/socket.h>

#include "gnhast.h"
#include "gnhastd.h"
#include "common.h"
#include "commands.h"
#include "cmds.h"
#include "gncoll.h"

extern TAILQ_HEAD(, _device_t) alldevs;
extern TAILQ_HEAD(, _client_t) clients;
extern struct event_base *base;

/**
   \brief Given a device, launch it's handler
   \param dev device to run handler for
*/

void run_handler_dev(device_t *dev)
{
	int sv[2], i;
	pid_t child;
	char **cmd;
	client_t *client;

	/* sanity checks */

	if (dev->handler == NULL) {
		LOG(LOG_ERROR, "Attempt to call handler for device w/o handler");
		return;
	}
	if (access(dev->handler, X_OK)) {
		LOG(LOG_ERROR, "Handler %s does not exist or is not"
		    " executable", dev->handler);
		return;
	}

	if (socketpair(PF_LOCAL, SOCK_STREAM|SOCK_NONBLOCK, 0, sv) != 0)
		LOG(LOG_FATAL, "Socketpair Failed: %s", strerror(errno));

	child = fork();

	switch (child) {
	case -1:
		LOG(LOG_ERROR, "Fork failed: %s", strerror(errno));
		return;
		break;
	case 0:	/* we are the child */
		/* setup the ones we want */
		if (dup2(sv[1], STDIN_FILENO) != STDIN_FILENO)
			bailout();
		if (dup2(sv[1], STDOUT_FILENO) != STDOUT_FILENO)
			bailout();
		if (dup2(sv[1], STDERR_FILENO) != STDERR_FILENO)
			bailout();
		/* close the other end */
		if (close(sv[0]) != 0)
			bailout();

		/* Now setup args for the handler */
		cmd = calloc(3 + dev->nrofhargs, sizeof(char *));
		cmd[0] = strdup(dev->handler);
		cmd[1] = strdup(dev->uid);
		for (i = 0; i < dev->nrofhargs; i++)
			cmd[2 + i] = strdup(dev->hargs[i]);
		cmd[2+i] = (char *)0;
		execvp(dev->handler, cmd);
		bailout(); /* if we got here, execvp failed */
	} /* end switch, now we are back in parent */
	if (close(sv[1]) != 0)
		LOG(LOG_ERROR, "close of pair failed: %s", strerror(errno));

	/* now we hook it up, just like it was a tcp connection */

	client = smalloc(client_t);
	client->fd = sv[0];
	client->name = strdup("handler");
	client->addr = strdup(dev->handler);
	client->pid = child;

	client->ev = bufferevent_socket_new(base, sv[0],
            BEV_OPT_CLOSE_ON_FREE);

	LOG(LOG_NOTICE, "Running handler \"%s %s\"", dev->handler, dev->uid);

	bufferevent_setcb(client->ev, buf_read_cb, NULL,
			  buf_error_cb, client);
	bufferevent_enable(client->ev, EV_READ|EV_PERSIST);
	TAILQ_INSERT_TAIL(&clients, client, next);
	/* now, tell the client about the change */
	gn_update_device(dev, GNC_UPD_NAME|GNC_UPD_RRDNAME, client->ev);
}
