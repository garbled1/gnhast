#include <gtk/gtk.h>


void on_new1_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_open1_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_save1_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_savecfg_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_savedev_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_savegrp_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_quit1_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_cut1_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_copy1_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_paste1_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_delete1_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_about1_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_enable_datastream1_activate(GtkMenuItem *menuitem, gpointer user_data);
void req_devlist_activate(GtkMenuItem *menuitem, gpointer user_data);
void dd_listitem_selected(GtkWidget *widget, gpointer data);
void on_dd_save_activate(GtkButton *button, gpointer user_data);
void on_dd_reset_activate(GtkButton *button, gpointer user_data);
void overview_listitem_selected(GtkWidget *widget, gpointer data);
void iconview_icon_activated(GtkIconView *widget, GtkTreePath *path,
			     gpointer data);
void on_icon_drag_data_get(GtkWidget *widget, GdkDragContext *drag_context,
			   GtkSelectionData *data, guint info, guint time,
			   gpointer user_data);
void treeview_on_drag_rec(GtkWidget *widget, GdkDragContext *ctx, int x, int y,
			  GtkSelectionData *data, guint info, guint time,
			  gpointer user_data);
gboolean treeview_keypress_cb(GtkWidget *widget, GdkEvent *event,
			      gpointer user_data);
