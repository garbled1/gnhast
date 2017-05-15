/**
   \file gtk-gnhast/gtk-gnhast.h
   \author Tim Rightnour
   \brief Structures and defines used in gtk-gnhast
*/


#ifndef _GTK_GNHAST_H_
#define _GKT_GNHAST_H_

#include "collector.h"

#define GTK_GNHAST_CONFIG_FILE "gtk-gnhast.conf"
#define GNHASTD_DEVICE_FILE     "devices.conf"
#define GNHASTD_DEVGROUP_FILE   "devgroups.conf"
#define INVALID_UID "_INVALID_UID_"
#define FEED_RATE 60
#define CTX_DEBUG "DEBUG"
#define CTX_NOTICE "NOTICE"
#define CTX_ERROR "ERROR"

#if 0
typedef struct _connection_t {
	int port;
	int type;
	int lastcmd;
	char *host;
	struct bufferevent *bev;
	device_t *current_dev;
	time_t lastdata;
	int shutdown;
} connection_t;
#endif

typedef struct _dd_field_t {
	GtkWidget *label;
	gchar *text;
	GtkWidget *entry;
	gboolean editable;
} dd_field_t;

enum DD_FIELDS {
	DD_TITLE,
	DD_UID,
	DD_NAME,
	DD_RRDNAME,
	DD_SUBTYPE,
	DD_TYPE,
	DD_PROTO,
	DD_SCALE,
	DD_HANDLER,
	DD_HARGS,
	DD_HIWAT,
	DD_LOWAT,
	DD_VALUE,
	NROF_DD,
};

enum TREE_MODEL_COLUMNS {
	NAME_COLUMN,
	UID_COLUMN,
};

enum LIST_MODEL_COLUMNS {
	LIST_NAME_COL,
	LIST_UID_COL,
	LIST_PIXBUF_COL,
	LIST_VALUE_COL,
	LIST_TYPE_COL,
	LIST_PROTO_COL,
	LIST_SUBTYPE_COL,
	LIST_NUM_COLS,
};

enum UID_IS {
	UID_IS_NULL,
	UID_IS_GROUP,
	UID_IS_DEV,
};

int what_is_uid(char *uid);
void cb_sigterm(int fd, short what, void *arg);
int delete_event(GtkWidget *widget, GdkEvent *event, gpointer data);
void exit_cb(GtkWidget *widget, gpointer data);
void request_full_device(device_t *dev);
void request_devlist(void);
void change_connection(char *server, int port);
void dump_rcfile(const char *filename);
void devgroupconf_dump_cb(void);
void devconf_dump_cb(void);

#endif /*_GTK_GNHAST_H_*/
