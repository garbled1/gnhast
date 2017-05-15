/**
   \file gtk-insteonedit/callbacks.h
   \author Tim Rightnour
   \brief Callback prototypes
*/

#include <gtk/gtk.h>

void on_getinfo_activate(GtkButton *button, gpointer user_data);
void on_ld_activate(GtkButton *button, gpointer user_data);
void on_new1_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_open1_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_save1_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_save_as1_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_quit1_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_cut1_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_copy1_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_paste1_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_delete1_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_about1_activate(GtkMenuItem *menuitem, gpointer user_data);
