#ifndef _GTK_INSTEONEDIT_
#define _GTK_INSTEONEDIT_

#define INSTEONCOLL_CONF_FILE   "insteoncoll.conf"

enum LIST_MODEL_COLUMNS {
        LIST_UID_COL,
        LIST_NUM_COLS,
};

enum ALDB_MODEL_COLUMNS {
	GROUP_COL,
	DEVID_COL,
	LD1_COL,
	LD2_COL,
	LD3_COL,
	INUSE_COL,
	MASTER_COL,
	EDIT_COL,
	DELETE_COL,
	ALDB_NUM_COLS,
};

#define GETINFO_IDLE		0
#define GETINFO_WORKING		1
#define GETINFO_UNLINKED	2

#define GETLD_IDLE		0
#define GETLD_WORKING		1
#define GETLD_DONE		2

/* main */
void getinfo_dev(device_t *dev);

/* interface.c */
void clear_info_link(gchar *buf);
void update_detail_dev(device_t *dev);
void update_detail_plm(void);
void update_info_dev(device_t *dev);
int delete_event(GtkWidget *widget, GdkEvent *event, gpointer data);
void exit_cb(GtkWidget *widget, gpointer data);
void update_devicelist_model(GtkTreeModel *model);
void add_plm_devicelist_model(GtkTreeModel *model);
void update_aldb_devicelist_model(device_t *dev);

/* callbacks.c */
void dd_listitem_selected(GtkWidget *widget, gpointer data);


#endif
