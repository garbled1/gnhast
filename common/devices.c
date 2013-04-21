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

/**
	\file devices.c
	\brief Device handling
	\author Tim Rightnour
*/

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <sys/rbtree.h>

#include "gnhast.h"
#include "common.h"

int nrofdevs;
rb_tree_t devices;
TAILQ_HEAD(, _device_t) alldevs = TAILQ_HEAD_INITIALIZER(alldevs);

static int compare_device_byuid(void *ctx, const void *a, const void *b);
static int compare_device_uidtokey(void *ctx, const void *a, const void *key);
static const rb_tree_ops_t device__alltree_ops = {
	.rbto_compare_nodes = compare_device_byuid,
	.rbto_compare_key = compare_device_uidtokey,
	.rbto_node_offset = offsetof(struct _device_t, rbn),
	.rbto_context = NULL
};

/** \brief Compare two devices by uid */

static int compare_device_byuid(void *ctx, const void *a, const void *b) 
{
	return strcmp(((device_t *)a)->uid, ((device_t *)b)->uid);
}

/** \brief Compare device uid to string */

static int compare_device_uidtokey(void *ctx, const void *a, const void *key)
{
	return strcmp(((device_t *)a)->uid, key);
}

/**
	\brief Search the devicetable for a device matching UID
	\param name name to look for
	\return device_t * if found, NULL if not
*/

device_t *find_device_byuid(char *uid)
{
	return rb_tree_find_node(&devices, uid);
}

/**
   \brief Add a wrapped device to this client
   \param dev device to add
   \param client client_t to add to
   \param rate rate of feed, if used
*/
void add_wrapped_device(device_t *dev, client_t *client, int rate)
{
	wrap_device_t *wrap = smalloc(wrap_device_t);

	wrap->dev = dev;
	wrap->rate = rate;
	if (TAILQ_EMPTY(&client->wdevices))
		    TAILQ_INIT(&client->wdevices);
	TAILQ_INSERT_TAIL(&client->wdevices, wrap, next);
}

/**
	\brief Insert a device into the TAILQ
	\note keeps the table sorted
	\return pointer to new device
*/

void insert_device(device_t *dev)
{
	/* put it in the rb tree */
	rb_tree_insert_node(&devices, dev);

	/* incr nrofdevs */
	nrofdevs++;

	/* throw it in the all device TAILQ */
	TAILQ_INSERT_TAIL(&alldevs, dev, next_all);
}


/** \brief Initialize the devicetable */

void init_devtable(void)
{
	/*XXX*/
	/* For now, nothing, later, read from a config file and initialize */
	nrofdevs = 0;
	rb_tree_init(&devices, &device__alltree_ops);
}
