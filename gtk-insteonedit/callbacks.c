/*
 * Copyright (c) 2015
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
   \file gtk-insteonedit/callbacks.c
   \brief Callbacks for gtk-insteonedit
   \author Tim Rightnour
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <event2/event.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "gnhast.h"
#include "common.h"
#include "confuse.h"
#include "confparser.h"
#include "gncoll.h"
#include "collcmd.h"
#include "genconn.h"
#include "insteon.h"

#include "callbacks.h"
#include "interface.h"
#include "support.h"
#include "gtk-insteonedit.h"

extern struct event_base *base;
extern GtkWidget *main_window;
extern insteon_devdata_t plminfo;
extern int working;
extern int getinfo_working;
extern int getld_working;

/**
   \brief gtk delete event handler
   \param widget wiget destroyed
   \param event event that happened
   \param data data, if any
*/

int delete_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	return FALSE;
}

/**
   \brief an exit callback
   \param widget widget that called me
   \param data user data
*/

void exit_cb(GtkWidget *widget, gpointer data)
{
	/* run the event base to cause the connection to drop */
	event_base_loop(base, EVLOOP_ONCE|EVLOOP_NONBLOCK);
	gtk_main_quit();
}

/**
   \brief a thing in the device list pane was selected
   \param widget widget selected
   \param data passed data
*/

void dd_listitem_selected(GtkWidget *widget, gpointer data)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	char *uid, im[16];
	device_t *dev;

	if (working)
		return;

	addr_to_string(im, plminfo.daddr);

	clear_info_link("");
	if (gtk_tree_selection_get_selected(GTK_TREE_SELECTION(widget), &model,
					    &iter)) {
		gtk_tree_model_get(model, &iter, LIST_UID_COL, &uid, -1);
		dev = find_device_byuid(uid);
		if (dev == NULL) {
			if (strcmp(uid, im)) {
				/* failball */
				g_free(uid);
				return;
			}
			/* this is the PLM */
			update_detail_plm();
			g_free(uid);
			return;
		}
		update_detail_dev(dev);
		g_free(uid);
	}
}

/**
   \brief The getinfo button was clicked
   \param button the button widget
   \param user_data pointer to the selected item
*/

void on_getinfo_activate(GtkButton *button, gpointer user_data)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkWidget *widget = (GtkWidget *)user_data;
	char *uid, im[16];
	device_t *dev;

	LOG(LOG_DEBUG, "on_getinfo_activate: working=%d gi_working=%d",
	    working, getinfo_working);

	if (working || getinfo_working)
		return;

	if (gtk_tree_selection_get_selected(GTK_TREE_SELECTION(widget), &model,
					    &iter)) {
		gtk_tree_model_get(model, &iter, LIST_UID_COL, &uid, -1);
		dev = find_device_byuid(uid);
		getinfo_dev(dev);
		g_free(uid);
	}
}

/**
   \brief The get linkdata button was clicked
   \param button the button widget
   \param user_data pointer to the selected item
*/

void on_ld_activate(GtkButton *button, gpointer user_data)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkWidget *widget = (GtkWidget *)user_data;
	char *uid, im[16];
	device_t *dev;

	LOG(LOG_DEBUG, "on_ld_activate: working=%d ld_working=%d",
	    working, getld_working);

	if (working || getld_working)
		return;

	if (gtk_tree_selection_get_selected(GTK_TREE_SELECTION(widget), &model,
					    &iter)) {
		gtk_tree_model_get(model, &iter, LIST_UID_COL, &uid, -1);
		dev = find_device_byuid(uid);
		get_aldb_dev(dev);
		g_free(uid);
	}
}

void on_new1_activate(GtkMenuItem *menuitem, gpointer user_data)
{

}


void on_open1_activate(GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

}


void
on_save1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

}


void
on_save_as1_activate                   (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

}


void
on_quit1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

}


void
on_cut1_activate                       (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

}


void
on_copy1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

}


void
on_paste1_activate                     (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

}


void
on_delete1_activate                    (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

}


void
on_about1_activate                     (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

}

