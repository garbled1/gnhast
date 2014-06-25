/*
 * Copyright (c) 2014
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
   \file callbacks.c
   \brief gtk+ callback handling code for gtk-gnhast
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
#include "callbacks.h"
#include "interface.h"
#include "support.h"
#include "gtk-gnhast.h"

extern struct event_base *base;
extern GtkWidget *main_window;
extern GtkTreeModel *iconview_model;
extern GtkTreeModel *devicetree_model;
extern GtkTreeModel *devicelist_model;
extern int conf_is_modified;
extern GtkTreeSelection *dd_select, *overview_select;
extern dd_field_t dd_fields[];
extern connection_t *gnhastd_conn;
extern TAILQ_HEAD(, _device_group_t) allgroups;
extern char *conffile;

static void register_child_groups(device_group_t *devgrp);


void on_new1_activate(GtkMenuItem *menuitem, gpointer user_data)
{

}

/**
   \brief Open new connection menu item selected
   \param menuitem menuitem widget
   \param user_data unused
*/

void on_open1_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	GtkWidget *dialog, *entry;
	int result, port;
	char *server, *buf;

	dialog = create_dialog1();

	result = gtk_dialog_run(GTK_DIALOG(dialog));
	if (result == GTK_RESPONSE_OK) {
		/* get the data, see what we can do about it */
		entry = lookup_widget(dialog, "dialog_server_entry");
		if (entry == NULL)
			return;
		server = (char *)gtk_entry_get_text(GTK_ENTRY(entry));

		if (strlen(server) < 1)
			return;

		entry = lookup_widget(dialog, "dialog_port_entry");
		if (entry == NULL)
			return;
		buf = (char *)gtk_entry_get_text(GTK_ENTRY(entry));
		port = atoi(buf);
		change_connection(server, port);
	}
	gtk_widget_destroy(dialog);
}

/**
   \brief register a child group
   \param devgrp group to look for children of
*/

static void register_child_groups(device_group_t *devgrp)
{
	wrap_group_t *wrapg;

	TAILQ_FOREACH(wrapg, &devgrp->children, nextg) {
		if (!(wrapg->group->onq & GROUPONQ_REG)) {
			register_child_groups(wrapg->group);
			LOG(LOG_DEBUG, "Registering group %s",
			    wrapg->group->uid);
			gn_register_devgroup(wrapg->group, gnhastd_conn->bev);
			wrapg->group->onq |= GROUPONQ_REG;
		}
	}
}

/**
   \brief Send the complete device group tree to gnhastd
   \param menuitem the menuitem selected
   \param user_data unused
*/

void on_save1_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	device_group_t *devgrp;

	/* We have to do leaf nodes first, otherwise, chaos. */
	TAILQ_FOREACH(devgrp, &allgroups, next_all) {
		register_child_groups(devgrp);
		/* Now register the parent */
		if (!(devgrp->onq & GROUPONQ_REG)) {
			LOG(LOG_DEBUG, "Registering group %s", devgrp->uid);
			gn_register_devgroup(devgrp, gnhastd_conn->bev);
			devgrp->onq |= GROUPONQ_REG;
		}
	}

	/* now clear all the registered flags */
	TAILQ_FOREACH(devgrp, &allgroups, next_all)
		devgrp->onq &= ~GROUPONQ_REG;
}

/**
   \brief Save the gtk-gnhast rc file
   \param menuitem the menuitem selected
   \param user_data unused
*/

void on_savecfg_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	dump_rcfile(conffile);
}

/**
   \brief Save the gtk-gnhast rc file
   \param menuitem the menuitem selected
   \param user_data unused
*/

void on_savedev_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	devconf_dump_cb();
}

/**
   \brief Save the gtk-gnhast rc file
   \param menuitem the menuitem selected
   \param user_data unused
*/

void on_savegrp_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	devgroupconf_dump_cb();
}

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
	cb_sigterm(0, 0, 0);
	/* run the event base to cause the connection to drop */
	event_base_loop(base, EVLOOP_ONCE|EVLOOP_NONBLOCK);
	gtk_main_quit();
}

/**
   \brief quit menu item callback
   \param menuitem menu item selected
   \param user_data user_data, if any
*/

void on_quit1_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	exit_cb(main_window, NULL);
}


void on_cut1_activate(GtkMenuItem *menuitem, gpointer user_data)
{

}


void on_copy1_activate(GtkMenuItem *menuitem, gpointer user_data)
{

}


void on_paste1_activate(GtkMenuItem *menuitem, gpointer user_data)
{

}


void on_delete1_activate(GtkMenuItem *menuitem, gpointer user_data)
{

}

/**
   \brief about menu clicked callback
   \param menuitem menuitem clicked
   \param pointer user data
*/

void on_about1_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	show_aboutdialog();
}


void on_enable_datastream1_activate(GtkMenuItem *menuitem, gpointer user_data)
{

}

/**
   \brief Request Devlist clicked callback
   \param menuitem menuitem clicked
   \param pointer user data
*/

void req_devlist_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	request_devlist();
}

/**
   \brief a thing in the device details pane was selected
   \param widget widget selected
   \param data passed data
*/

void dd_listitem_selected(GtkWidget *widget, gpointer data)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	char *uid;
	device_t *dev;
	device_group_t *devgrp;

	if (gtk_tree_selection_get_selected(GTK_TREE_SELECTION(widget), &model,
					    &iter)) {
		gtk_tree_model_get(model, &iter, UID_COLUMN, &uid, -1);
		switch (what_is_uid(uid)) {
		case UID_IS_GROUP:
			devgrp = find_devgroup_byuid(uid);
			update_dd_devgrp(devgrp);
			break;
		case UID_IS_DEV:
			dev = find_device_byuid(uid);
			update_dd_dev(dev);
			break;
		}
		g_free(uid);
	}
}

/**
   \brief The device details save button was clicked
   \param button the button widget
   \param user_data unused
*/

void on_dd_save_activate(GtkButton *button, gpointer user_data)
{
	device_t *dev;
	device_group_t *devgrp;
	char *uid, *data, *data2, *rrdname;
	double d;
	uint32_t u;
	int64_t ll;

	uid = (char *)gtk_entry_get_text(GTK_ENTRY(dd_fields[DD_UID].entry));

	switch (what_is_uid(uid)) {
	case UID_IS_GROUP:
		devgrp = find_devgroup_byuid(uid);

		/* we only update name */
		data = (char *)gtk_entry_get_text(GTK_ENTRY(dd_fields[DD_NAME].entry));
		if (strlen(data) < 1) {
			error_dialog(main_window, "Group must have a Name, "
				     "Not Saved.");
			return;
		}
		if (devgrp->name != NULL)
			free(devgrp->name);
		devgrp->name = strdup(data);

		/* Now, we need to tell gnhastd about the changes */
		gn_register_devgroup_nameonly(devgrp, gnhastd_conn->bev);

		update_devicetree_model(devicetree_model);
		return;
		break;
	case UID_IS_DEV:
		dev = find_device_byuid(uid);
		/* now, start replacing things */

		/* name */
		data = (char *)gtk_entry_get_text(GTK_ENTRY(dd_fields[DD_NAME].entry));
		if (strlen(data) < 1) {
			error_dialog(main_window, "Device must have a Name, "
				     "Not Saved.");
			return;
		}
		if (dev->name != NULL)
			free(dev->name);
		dev->name = strdup(data);

		/* rrdname */
		data = (char *)gtk_entry_get_text(GTK_ENTRY(dd_fields[DD_RRDNAME].entry));
		rrdname = mk_rrdname(data);
		/* check, but don't actually fail */
		if (strcmp(rrdname, data) != 0)
			error_dialog(main_window, "RRDName changed to :%s",
				     rrdname);
		if (dev->rrdname != NULL)
			free(dev->rrdname);
		dev->rrdname = rrdname;

		/* scale */
		/* XXX skipping for now */

		/* handler */
		data = (char *)gtk_entry_get_text(GTK_ENTRY(dd_fields[DD_HANDLER].entry));
		/* if handler is empty and new one is, skip this */
		if (strlen(data) > 0 ||
		    (strlen(data) == 0 && dev->handler != NULL)) {
			if (dev->handler != NULL)
				free(dev->handler);
			if (strlen(data) > 0)
				dev->handler = strdup(data);
		}

		/* hargs */
		data = (char *)gtk_entry_get_text(GTK_ENTRY(dd_fields[DD_HARGS].entry));
		if (strlen(data) > 0 ||
		    (strlen(data) == 0 && dev->nrofhargs > 1))
			parse_hargs(dev, data);

		/* lowat & hiwat */
		data = (char *)gtk_entry_get_text(GTK_ENTRY(dd_fields[DD_LOWAT].entry));
		data2 = (char *)gtk_entry_get_text(GTK_ENTRY(dd_fields[DD_HIWAT].entry));
		switch (datatype_dev(dev)) {
		case DATATYPE_UINT:
			u = strtoul(data, (char **)NULL, 10);
			store_data_dev(dev, DATALOC_LOWAT, &u);
			u = strtoul(data2, (char **)NULL, 10);
			store_data_dev(dev, DATALOC_HIWAT, &u);
			break;
		case DATATYPE_DOUBLE:
			d = atof(data);
			store_data_dev(dev, DATALOC_LOWAT, &d);
			d = atof(data2);
			store_data_dev(dev, DATALOC_HIWAT, &d);
			break;
		case DATATYPE_LL:
			ll = strtoll(data, (char **)NULL, 10);
			store_data_dev(dev, DATALOC_LOWAT, &ll);
			ll = strtoll(data2, (char **)NULL, 10);
			store_data_dev(dev, DATALOC_HIWAT, &ll);
			break;
		}

		/* Now, we need to tell gnhastd about the changes */
		gn_modify_device(dev, gnhastd_conn->bev);

		update_devicetree_model(devicetree_model);
		return;
		break;
	}

}

/**
   \brief The device details reset button was clicked
   \param button the button widget
   \param user_data unused
   \note we totally cheat here.
*/

void on_dd_reset_activate(GtkButton *button, gpointer user_data)
{
	dd_listitem_selected(GTK_WIDGET(dd_select), NULL);
}

/**
   \brief a thing in the list view pane was selected
   \param widget widget selected
   \param data passed data
*/

void overview_listitem_selected(GtkWidget *widget, gpointer data)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	char *uid;

	if (gtk_tree_selection_get_selected(GTK_TREE_SELECTION(widget), &model,
					    &iter)) {
		gtk_tree_model_get(model, &iter, UID_COLUMN, &uid, -1);
		update_iconview(uid);
		g_free(uid);
	}
}

/**
   \brief a thing in the iconview widget was activated
   \param widget widget selected
   \param data passed data
*/

void iconview_icon_activated(GtkIconView *widget, GtkTreePath *path,
			     gpointer data)
{
	GtkTreeIter iter;
	char *uid;

	if (gtk_tree_model_get_iter(GTK_TREE_MODEL(iconview_model), &iter,
				    path)) {
		gtk_tree_model_get(GTK_TREE_MODEL(iconview_model), &iter,
				   UID_COLUMN, &uid, -1);
		update_iconview(uid);
		g_free(uid);
	}
}

/**
   \brief We drag an icon from the iconview, set the uid up
   \param widget iconview widget
   \param drag_context the drag context?
   \param data the data we are saving off
   \param info dunno
   \param time dunno
   \param user_data user_data, if any
*/

void on_icon_drag_data_get(GtkWidget *widget, GdkDragContext *drag_context,
			   GtkSelectionData *data, guint info, guint time,
			   gpointer user_data)
{
	GList *selected;
	GtkTreePath *path;
	GtkTreeIter iter;
	char *uid;

	selected = gtk_icon_view_get_selected_items(GTK_ICON_VIEW(widget));
	path = (GtkTreePath *)selected->data;
	gtk_tree_model_get_iter(iconview_model, &iter, path);
	gtk_tree_model_get(GTK_TREE_MODEL(iconview_model), &iter,
			   UID_COLUMN, &uid, -1);

	gtk_selection_data_set(data, data->target, 8, (char *)uid,
			       strlen(uid));
	g_list_free(selected);
	g_free(uid);
}

/**
   \brief A drag ended up in the treeview
   \param widget treeview widget
   \param ctx the drag context?
   \param x x coord?
   \param y y coord?
   \param data the data we are bringing in
   \param info dunno
   \param time dunno
   \param user_data user_data, if any
*/

void treeview_on_drag_rec(GtkWidget *widget, GdkDragContext *ctx, int x, int y,
			  GtkSelectionData *data, guint info, guint time,
			  gpointer user_data)
{
	device_group_t *targetgrp, *druggrp;
	device_t *drugdev;
	GtkTreePath *path;
	GtkTreeIter iter, newiter;
	char *uid;
	int x1, y1;

	gtk_tree_view_convert_widget_to_bin_window_coords(GTK_TREE_VIEW(widget), x, y, &x1, &y1);

	gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), x1, y1, &path,
				      NULL, NULL, NULL);
	gtk_tree_model_get_iter(devicetree_model, &iter, path);
	gtk_tree_model_get(devicetree_model, &iter, UID_COLUMN, &uid, -1);

	/* target must be a group */
	targetgrp = find_devgroup_byuid(uid);
	if (targetgrp == NULL)
		goto out;

	/* did we get a device or a group? */
	druggrp = find_devgroup_byuid((char *)data->data);
	if (druggrp != NULL) {
		if (group_in_group(druggrp, targetgrp))
			goto out;
		add_group_devicetree_model(druggrp, devicetree_model, &iter);
		add_group_group(druggrp, targetgrp);
		conf_is_modified = 1;
	} else {
		drugdev = find_device_byuid((char *)data->data);
		if (drugdev == NULL)
			goto out;
		if (dev_in_group(drugdev, targetgrp))
			goto out;
		gtk_tree_store_append(GTK_TREE_STORE(devicetree_model),
				      &newiter, &iter);
		gtk_tree_store_set(GTK_TREE_STORE(devicetree_model), &newiter,
				   NAME_COLUMN, drugdev->name,
				   UID_COLUMN, drugdev->uid, -1);
		add_dev_group(drugdev, targetgrp);
		conf_is_modified = 1;
	}

out:
	g_free(uid);
	gtk_tree_path_free(path);
}

/**
   \brief Delete a group or device
   \param model the tree model
   \param iter the iter from the tree model
   \param uid the UID of the current device
*/

static gboolean delete_item_func(GtkTreeModel *model, GtkTreeIter *iter,
				 char *uid)
{
	GtkTreeIter parent;
	char *parentuid = NULL;
	device_t *dev;
	device_group_t *group, *pgroup;

	/* don't muck with all devices */
	if (strcmp(uid, INVALID_UID) == 0)
		return FALSE;

	if (gtk_tree_model_iter_parent(model, &parent, iter)) {
		gtk_tree_model_get(model, &parent, UID_COLUMN,
				   &parentuid, -1);
	}

	/* don't muck with the all devices list */
	if (parentuid != NULL && strcmp(parentuid, INVALID_UID) == 0)
		return FALSE;

	gtk_tree_store_remove(GTK_TREE_STORE(model), iter);
	if (parentuid != NULL)
		pgroup = find_devgroup_byuid(parentuid);
	if (pgroup != NULL && parentuid != NULL) {
		switch (what_is_uid(uid)) {
		case UID_IS_DEV:
			dev = find_device_byuid(uid);
			remove_dev_group(dev, pgroup);
			break;
		case UID_IS_GROUP:
			group = find_devgroup_byuid(uid);
			remove_group_group(group, pgroup);
			break;
		}
		free(parentuid);
	}
	return TRUE;
}

/**
   \brief Insert a group
   \param model the tree model
   \param iter the iter from the tree model
   \param uid the UID of the current device
   \note FREES uid !!
*/

static gboolean insert_item_func(GtkTreeModel *model, GtkTreeIter *iter,
				 char *uid)
{
	GtkTreeIter parent;
	device_group_t *group, *pgroup;
	GtkWidget *dialog, *gname, *guid;
	gint result;
	const char *newuid, *newname;

	/* don't muck with all devices */
	if (strcmp(uid, INVALID_UID) == 0)
		return FALSE;
	if (what_is_uid(uid) == UID_IS_DEV) {
		if (gtk_tree_model_iter_parent(model, &parent,
					       iter)) {
			free(uid);
			gtk_tree_model_get(model, &parent, UID_COLUMN,
					   &uid, -1);
			iter = &parent;
		}
	}
	/* now iter and uid should point to a group */
	dialog = create_insert_group_dialog();
	result = gtk_dialog_run(GTK_DIALOG(dialog));
	switch (result) {
	case GTK_RESPONSE_ACCEPT:
		gname = lookup_widget(dialog, "groupname");
		guid = lookup_widget(dialog, "groupuid");
		if (gname == NULL || guid == NULL)
			goto insert_fail;

		newuid = gtk_entry_get_text(GTK_ENTRY(guid));
		newname = gtk_entry_get_text(GTK_ENTRY(gname));
		if (strlen(newname) < 1 || strlen(newuid) < 1) {
			error_dialog(main_window,
				     "Require a name and UID");
			goto insert_fail;
		}

		if (what_is_uid((char *)newuid) != UID_IS_NULL) {
			error_dialog(main_window,
				     "UID %s is not unique",
				     newuid);
			goto insert_fail;
		}

		/* ok, we passed all the tests, create a group */
		pgroup = find_devgroup_byuid(uid);
		group = new_devgroup((char *)newuid);
		group->name = strdup((char *)newname);
		if (pgroup != NULL)
			add_group_group(group, pgroup);
		update_devicetree_model(devicetree_model);
		update_devicelist_model(devicelist_model);
		update_iconview(uid);
		gtk_widget_destroy(dialog);
		free(uid);
		return TRUE;
		break;
	case GTK_RESPONSE_REJECT:
	default:
		break;
	}
insert_fail:
	gtk_widget_destroy(dialog);
	free(uid);
	return FALSE;
}

/**
   \brief A key was pressed in the treeview
   \param widget treeview widget
   \param event what event?
   \param user_data some user data
*/

gboolean treeview_keypress_cb(GtkWidget *widget, GdkEvent *event,
			      gpointer user_data)
{
	GdkEventKey key = event->key;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeSelection *select;
	char *uid = NULL;
	gboolean ret;

	select = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
	if (gtk_tree_selection_get_selected(select, &model, &iter))
		gtk_tree_model_get(model, &iter, UID_COLUMN, &uid, -1);

	if (uid == NULL)
		return FALSE;

	switch (key.keyval) {
	case GDK_KEY_Delete:
		ret = delete_item_func(model, &iter, uid);
		free(uid);
		return ret;
		break;
	case GDK_KEY_Insert:
		return insert_item_func(model, &iter, uid);
		break;
	}
	return FALSE;
}

/**
   \brief Insert a new group menu item selected
   \param menuitem the menuitem that was selected
   \param user_data pointer to the treeview widget
*/

static void on_insert_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	GtkWidget *widget = GTK_WIDGET(user_data);
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeSelection *select;
	char *uid = NULL;

	select = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
	if (gtk_tree_selection_get_selected(select, &model, &iter))
		gtk_tree_model_get(model, &iter, UID_COLUMN, &uid, -1);

	if (uid == NULL)
		return;

	(void)insert_item_func(model, &iter, uid);
	return;
}

/**
   \brief delete a group or device menu item selected
   \param menuitem the menuitem that was selected
   \param user_data pointer to the treeview widget
*/

static void on_delete_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	GtkWidget *widget = GTK_WIDGET(user_data);
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeSelection *select;
	char *uid = NULL;

	select = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
	if (gtk_tree_selection_get_selected(select, &model, &iter))
		gtk_tree_model_get(model, &iter, UID_COLUMN, &uid, -1);

	if (uid == NULL)
		return;

	(void)delete_item_func(model, &iter, uid);
	free(uid);
	return;
}

/**
   \brief A mouse button was clicked in the treeview
   \param widget treeview widget
   \param event what event?
   \param user_data some user data
*/

gboolean treeview_buttonpress_cb(GtkWidget *widget, GdkEvent *event,
			      gpointer user_data)
{
	GtkWidget *menu;
	GtkWidget *delete, *insert;
	GdkEventButton *event_button;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeSelection *select;
	char *uid = NULL;

	/* Did the user hit button 3?  Otherwise, pass it on */
	if (event->type == GDK_BUTTON_PRESS) {
		event_button = (GdkEventButton *)event;
		if (event_button->button != 3)
			return FALSE;
	}

	select = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
	if (gtk_tree_selection_get_selected(select, &model, &iter))
		gtk_tree_model_get(model, &iter, UID_COLUMN, &uid, -1);

	if (uid == NULL)
		return FALSE;

	menu = gtk_menu_new();
	insert = gtk_menu_item_new_with_label("Insert Group");
	g_signal_connect((gpointer)insert, "activate",
			 G_CALLBACK(on_insert_activate), (gpointer)widget);

	delete = gtk_menu_item_new_with_label("Delete Device/Group");
	g_signal_connect((gpointer)delete, "activate",
			 G_CALLBACK(on_delete_activate), (gpointer)widget);

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), insert);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), delete);

	gtk_widget_show_all(menu);

	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
		       event_button->button, event_button->time);
	free(uid);
	return TRUE;
}
