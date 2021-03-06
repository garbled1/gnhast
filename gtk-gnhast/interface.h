/**
   \file gtk-gnhast/interface.h
   \author Tim Rightnour
   \brief Prototypes for the widgets
*/
/*
 * DO NOT EDIT THIS FILE - it is generated by Glade.
 */

void update_devicetree_model(GtkTreeModel *model);
void update_devicelist_model(GtkTreeModel *model);
void add_group_devicetree_model(device_group_t *devgrp,	GtkTreeModel *model,
				GtkTreeIter *level);
void log_status(char *context, char *msg, ...);
void TLOG(int level, char *msg, ...);
GtkWidget *create_window1(void);
GtkWidget *create_insert_group_dialog(void);
void error_dialog(GtkWidget *parent, char *msg, ...);
GtkWidget* create_dialog1(void);
void show_aboutdialog1(void);
void update_dd_dev(device_t *dev);
void update_dd_devgrp(device_group_t *devgrp);
void update_all_vals_dev(device_t *dev);
void update_iconview(char *uid);
