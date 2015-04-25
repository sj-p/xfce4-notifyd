/*
 *  xfce4-notifyd
 *
 *  Copyright (c) 2008 Brian Tarricone <bjt23@cornell.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License ONLY.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <gtk/gtk.h>

#include <dbus/dbus-glib.h>

#include <xfconf/xfconf.h>
#include <libxfce4ui/libxfce4ui.h>
#include <libnotify/notify.h>

#include "xfce4-notifyd-config.ui.h"

enum
{
    MONITOR_PRIMARY = 0,
    MONITOR_MOUSE,
    MONITOR_WINDOW,
    MONITOR_FIXED,
    NUM_MONITOR_MODES,
};


static void
xfce_notifyd_config_show_notification_callback(NotifyNotification *notification,
                                               const char         *action,
                                               gpointer            unused)
{
  /* Don't do anything, we just have a button to show its style */
}

static void
xfce_notifyd_config_show_notification_preview(GtkWindow *parent_window)
{
    NotifyNotification *notification;
    GError             *error = NULL;

    notification =
        notify_notification_new(_("Notification Preview"),
                                _("This is what notifications will look like"),
                                "xfce4-notifyd");

    notify_notification_add_action(notification,
                                   "button",
                                   _("Button"),
                                   (NotifyActionCallback) xfce_notifyd_config_show_notification_callback,
                                   NULL,
                                   NULL);

    if (!notify_notification_show(notification, &error)) {
        xfce_dialog_show_error(parent_window, error,
                               _("Notification preview failed"));

        g_error_free(error);
    }

    g_object_unref(notification);
}

static gchar *
xfce4_notifyd_slider_format_value(GtkScale *slider,
                                  gdouble value,
                                  gpointer user_data)
{
    return g_strdup_printf("%d%%", (gint)(value * 100));
}

static void
xfce4_notifyd_config_theme_combo_changed(GtkComboBox *theme_combo,
                                         gpointer user_data)
{
    XfconfChannel *channel = user_data;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *theme = NULL;

    if(!gtk_combo_box_get_active_iter(theme_combo, &iter))
        return;

    model = gtk_combo_box_get_model (theme_combo);

    gtk_tree_model_get(model, &iter, 0, &theme, -1);
    xfconf_channel_set_string(channel, "/theme", theme);
    g_free(theme);
}

static void
xfce4_notifyd_config_theme_changed(XfconfChannel *channel,
                                   const gchar *property,
                                   const GValue *value,
                                   gpointer user_data)
{
    GtkWidget *theme_combo = user_data;
    GtkListStore *ls;
    GtkTreeIter iter;
    gchar *theme;
    const gchar *new_theme;

    new_theme = G_VALUE_TYPE(value) ? g_value_get_string(value) : "Default";

    ls = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(theme_combo)));

    for(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(ls), &iter);
        gtk_list_store_iter_is_valid(ls, &iter);
        gtk_tree_model_iter_next(GTK_TREE_MODEL(ls), &iter))
    {
        gtk_tree_model_get(GTK_TREE_MODEL(ls), &iter, 0, &theme, -1);
        if(!strcmp(theme, new_theme)) {

            gtk_combo_box_set_active_iter(GTK_COMBO_BOX(theme_combo),
                                          &iter);
            g_free(theme);

            xfce_notifyd_config_show_notification_preview(GTK_WINDOW(gtk_widget_get_toplevel(theme_combo)));

            return;
        }
        g_free(theme);
    }

    gtk_list_store_append(ls, &iter);
    gtk_list_store_set(ls, &iter, 0, new_theme, -1);
    gtk_combo_box_set_active_iter(GTK_COMBO_BOX(theme_combo), &iter);
}

static void
list_store_add_themes_in_dir(GtkListStore *ls,
                             const gchar *path,
                             const gchar *current_theme,
                             GHashTable *themes_seen,
                             GtkTreeIter *current_theme_iter)
{
    GDir *dir;
    const gchar *file;
    gchar *filename;

    dir = g_dir_open(path, 0, NULL);
    if(!dir)
        return;

    while((file = g_dir_read_name(dir))) {
        if(g_hash_table_lookup(themes_seen, file))
            continue;

        filename =
            g_build_filename(path, file, "xfce-notify-4.0", "gtkrc", NULL);

        if(g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
            GtkTreeIter iter;

            gtk_list_store_append(ls, &iter);
            gtk_list_store_set(ls, &iter, 0, file, -1);
            g_hash_table_insert(themes_seen, g_strdup(file),
                                GUINT_TO_POINTER(1));

            if(!strcmp(file, current_theme))
                memcpy(current_theme_iter, &iter, sizeof(iter));
        }

        g_free(filename);
    }

    g_dir_close(dir);
}

static void
xfce4_notifyd_config_setup_theme_combo(GtkWidget *theme_combo,
                                       const gchar *current_theme)
{
    GtkListStore *ls;
    gchar *dirname, **dirnames;
    GHashTable *themes_seen;
    gint i;
    GtkTreeIter current_theme_iter;

    ls = gtk_list_store_new(1, G_TYPE_STRING);
    themes_seen = g_hash_table_new_full(g_str_hash, g_str_equal,
                                        (GDestroyNotify)g_free, NULL);

    dirname = g_build_filename(xfce_get_homedir(), ".themes", NULL);
    list_store_add_themes_in_dir(ls, dirname, current_theme,
                                 themes_seen, &current_theme_iter);
    g_free(dirname);

    dirnames = xfce_resource_lookup_all(XFCE_RESOURCE_DATA, "themes/");
    for(i = 0; dirnames && dirnames[i]; ++i)
        list_store_add_themes_in_dir(ls, dirnames[i], current_theme,
                                     themes_seen, &current_theme_iter);
    g_strfreev(dirnames);

    g_hash_table_destroy(themes_seen);

    gtk_combo_box_set_model(GTK_COMBO_BOX(theme_combo), GTK_TREE_MODEL(ls));

    if(gtk_list_store_iter_is_valid(ls, &current_theme_iter))
        gtk_combo_box_set_active_iter(GTK_COMBO_BOX(theme_combo),
                                      &current_theme_iter);

    g_object_unref(G_OBJECT(ls));
}

static void
xfce_notifyd_config_dialog_response(GtkWidget *dialog, gint response, gpointer unused)
{
  if (response == 0)
      g_signal_stop_emission_by_name (dialog, "response");
}

static void
xfce_notifyd_config_preview_clicked(GtkButton *button,
                                    GtkWidget *dialog)
{
    xfce_notifyd_config_show_notification_preview(GTK_WINDOW(dialog));
}

static void
xfce_notifyd_config_fixed_set_sensitive_cb (GtkWidget *widget, gpointer data)
{
    if (GTK_IS_HBOX (widget))
        gtk_widget_set_sensitive (widget, GPOINTER_TO_INT (data));
}

static void
xfce_notifyd_config_fixed_set_sensitive (GtkWidget *rbtn, gboolean sensitive)
{
    GtkWidget *vbox;

    vbox = gtk_widget_get_ancestor (rbtn, GTK_TYPE_VBOX);

    gtk_container_foreach (GTK_CONTAINER (vbox),
                           xfce_notifyd_config_fixed_set_sensitive_cb,
                           GINT_TO_POINTER (sensitive));
}

static void
xfce4_notifyd_config_set_radio_buttons(GtkWidget *rbtn, guint monitor_mode)
{
    GSList *buttons;


    buttons = gtk_radio_button_get_group (GTK_RADIO_BUTTON(rbtn));

    if (monitor_mode >= NUM_MONITOR_MODES) monitor_mode = MONITOR_PRIMARY;

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(g_slist_nth_data (buttons, NUM_MONITOR_MODES - 1 - monitor_mode)),
                                  TRUE);

    xfce_notifyd_config_fixed_set_sensitive (rbtn, (monitor_mode == MONITOR_FIXED));
}

static void
xfce4_notifyd_config_mode_changed(XfconfChannel *channel,
                                  const gchar *property,
                                  const GValue *value,
                                  gpointer rbtn)
{
    guint monitor_mode = g_value_get_uint (value);

    xfce4_notifyd_config_set_radio_buttons (rbtn, monitor_mode);
}

static void
xfce4_notifyd_rbtn_toggled(GtkRadioButton *rbtn,
                            gpointer channel)
{
    gint index;
    GSList *buttons;

    if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(rbtn))) return;

    buttons = gtk_radio_button_get_group (rbtn);
    index = g_slist_index (buttons, rbtn);

    if ( index < 0 ) return;
    index = NUM_MONITOR_MODES - 1 - index;
    if ( index < 0 ) return;

    xfconf_channel_set_uint (channel, "/monitor-mode", (guint) index);

    xfce_notifyd_config_fixed_set_sensitive (GTK_WIDGET (rbtn), (index == MONITOR_FIXED));
}

static void
xfce4_notifyd_config_mon_name_show(GtkWidget *entry, const gchar *text)
{
    gtk_entry_set_text (GTK_ENTRY(entry), text);
}

static void
xfce4_notifyd_config_mon_name_save(GtkEditable *entry, gpointer channel)
{
    xfconf_channel_set_string (channel,
                               "/monitor-name",
                               gtk_entry_get_text (GTK_ENTRY(entry)));
}

static void
xfce4_notifyd_config_mon_name_changed(XfconfChannel *channel,
                                          const gchar *property,
                                          const GValue *value,
                                          gpointer entry)
{
    if ( !G_VALUE_TYPE(value) ) return;

    xfce4_notifyd_config_mon_name_show (entry, g_value_get_string(value));
}

static GtkWidget *
xfce4_notifyd_config_setup_dialog(GtkBuilder *builder)
{
    XfconfChannel *channel;
    GtkWidget *dlg, *btn, *sbtn, *slider, *theme_combo, *position_combo;
    GtkWidget *screen_num, *rbtn, *entry;
    GtkAdjustment *adj;
    GError *error = NULL;
    gchar *current_theme;

    gtk_builder_connect_signals(builder, NULL);

    dlg = GTK_WIDGET(gtk_builder_get_object(builder, "notifyd_settings_dlg"));
    g_signal_connect(G_OBJECT(dlg), "response",
                     G_CALLBACK(xfce_notifyd_config_dialog_response), NULL);

    btn = GTK_WIDGET(gtk_builder_get_object(builder, "close_btn"));
    g_signal_connect_swapped(G_OBJECT(btn), "clicked",
                             G_CALLBACK(gtk_dialog_response), dlg);

    btn = GTK_WIDGET(gtk_builder_get_object(builder, "preview_button"));
    g_signal_connect(G_OBJECT(btn), "clicked",
                     G_CALLBACK(xfce_notifyd_config_preview_clicked), dlg);

    if(!xfconf_init(&error)) {
        xfce_message_dialog(NULL, _("Xfce Notify Daemon"),
                            GTK_STOCK_DIALOG_ERROR,
                            _("Settings daemon is unavailable"),
                            error->message,
                            GTK_STOCK_QUIT, GTK_RESPONSE_ACCEPT,
                            NULL);
        exit(EXIT_FAILURE);
    }

    channel = xfconf_channel_new("xfce4-notifyd");

    sbtn = GTK_WIDGET(gtk_builder_get_object(builder, "expire_timeout_sbtn"));
    xfconf_g_property_bind(channel, "/expire-timeout", G_TYPE_INT,
                           G_OBJECT(sbtn), "value");

    slider = GTK_WIDGET(gtk_builder_get_object(builder, "opacity_slider"));
    g_signal_connect(G_OBJECT(slider), "format-value",
                     G_CALLBACK(xfce4_notifyd_slider_format_value), NULL);
    adj = gtk_range_get_adjustment(GTK_RANGE(slider));
    xfconf_g_property_bind(channel, "/initial-opacity", G_TYPE_DOUBLE,
                           G_OBJECT(adj), "value");

    theme_combo = GTK_WIDGET(gtk_builder_get_object(builder, "theme_combo"));
    current_theme = xfconf_channel_get_string(channel, "/theme", "Default");
    xfce4_notifyd_config_setup_theme_combo(theme_combo, current_theme);
    g_free(current_theme);

    g_signal_connect(G_OBJECT(theme_combo), "changed",
                     G_CALLBACK(xfce4_notifyd_config_theme_combo_changed),
                     channel);
    g_signal_connect(G_OBJECT(channel), "property-changed::/theme",
                     G_CALLBACK(xfce4_notifyd_config_theme_changed),
                     theme_combo);

    position_combo = GTK_WIDGET(gtk_builder_get_object(builder, "position_combo"));
    xfconf_g_property_bind(channel, "/notify-location", G_TYPE_UINT,
                           G_OBJECT(position_combo), "active");
    if(gtk_combo_box_get_active(GTK_COMBO_BOX(position_combo)) == -1)
        gtk_combo_box_set_active(GTK_COMBO_BOX(position_combo), GTK_CORNER_TOP_RIGHT);

    screen_num = GTK_WIDGET(gtk_builder_get_object(builder, "screen_num"));
    adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(screen_num));
    xfconf_g_property_bind(channel, "/monitor-screen", G_TYPE_INT,
                           G_OBJECT(adj), "value");

    rbtn = GTK_WIDGET(gtk_builder_get_object(builder, "radio0"));
    g_signal_connect(G_OBJECT(rbtn), "toggled",
                     G_CALLBACK(xfce4_notifyd_rbtn_toggled), channel);
    xfce4_notifyd_config_set_radio_buttons (rbtn,
                     xfconf_channel_get_uint(channel, "/monitor-mode", MONITOR_MOUSE));
    g_signal_connect(G_OBJECT(channel), "property-changed::/monitor-mode",
                     G_CALLBACK(xfce4_notifyd_config_mode_changed),
                     rbtn);

    rbtn = GTK_WIDGET(gtk_builder_get_object(builder, "radio1"));
    g_signal_connect(G_OBJECT(rbtn), "toggled",
                     G_CALLBACK(xfce4_notifyd_rbtn_toggled), channel);

    rbtn = GTK_WIDGET(gtk_builder_get_object(builder, "radio2"));
    g_signal_connect(G_OBJECT(rbtn), "toggled",
                     G_CALLBACK(xfce4_notifyd_rbtn_toggled), channel);

    rbtn = GTK_WIDGET(gtk_builder_get_object(builder, "radio3"));
    g_signal_connect(G_OBJECT(rbtn), "toggled",
                     G_CALLBACK(xfce4_notifyd_rbtn_toggled), channel);


    entry = GTK_WIDGET(gtk_builder_get_object(builder, "monitor_name"));
    g_signal_connect(G_OBJECT(entry), "changed",
                     G_CALLBACK(xfce4_notifyd_config_mon_name_save), channel);
    xfce4_notifyd_config_mon_name_show (entry,
                    xfconf_channel_get_string(channel, "/monitor-name", ""));
    g_signal_connect(G_OBJECT(channel), "property-changed::/monitor-name",
                     G_CALLBACK(xfce4_notifyd_config_mon_name_changed),
                     entry);

    return dlg;
}

int
main(int argc,
     char **argv)
{
    GtkWidget *settings_dialog = NULL;
    GtkBuilder *builder;
    gboolean opt_version = FALSE;
    GdkNativeWindow opt_socket_id = 0;
    GOptionEntry option_entries[] = {
        { "version", 'V', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &opt_version, N_("Display version information"), NULL },
        { "socket-id", 's', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_INT, &opt_socket_id, N_("Settings manager socket"), N_("SOCKET_ID") },
        { NULL, },
    };
    GError *error = NULL;

    xfce_textdomain(GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");

    if(!gtk_init_with_args(&argc, &argv, "", option_entries, PACKAGE, &error)) {
        if(G_LIKELY(error)) {
            g_printerr("%s: %s.\n", G_LOG_DOMAIN, error->message);
            g_printerr(_("Type '%s --help' for usage."), G_LOG_DOMAIN);
            g_printerr("\n");
            g_error_free(error);
        } else
            g_error("Unable to open display.");

        return EXIT_FAILURE;
    }

    if(G_UNLIKELY(opt_version)) {
        g_print("%s %s\n", G_LOG_DOMAIN, VERSION);
        g_print("Copyright (c) 2010 Brian Tarricone <bjt23@cornell.edu>\n");
        g_print("Copyright (c) 2010 Jérôme Guelfucci <jeromeg@xfce.org>\n");
        g_print(_("Released under the terms of the GNU General Public License, version 2\n"));
        g_print(_("Please report bugs to %s.\n"), PACKAGE_BUGREPORT);

        return EXIT_SUCCESS;
    }

    if (!notify_init ("Xfce4-notifyd settings")) {
      g_error ("Failed to initialize libnotify.");
      return EXIT_FAILURE;
    }

    builder = gtk_builder_new();
    gtk_builder_add_from_string(builder, xfce4_notifyd_config_ui, xfce4_notifyd_config_ui_length, NULL);
    if(G_UNLIKELY(!builder)) {
        g_error("Unable to read embedded UI definition file");
        return EXIT_FAILURE;
    }

    settings_dialog = xfce4_notifyd_config_setup_dialog(builder);

    if(opt_socket_id) {
        GtkWidget *plug, *plug_child;

        plug = gtk_plug_new(opt_socket_id);
        gtk_widget_show(plug);
        g_signal_connect(G_OBJECT(plug), "delete-event",
                         G_CALLBACK(gtk_main_quit), NULL);

        plug_child = GTK_WIDGET(gtk_builder_get_object(builder, "plug-child"));
        gtk_widget_reparent(plug_child, plug);
        gtk_widget_show(plug_child);

        gdk_notify_startup_complete();
        g_object_unref(G_OBJECT(builder));
        gtk_widget_destroy(settings_dialog);

        gtk_main();
    } else {
        g_object_unref(G_OBJECT(builder));

        gtk_dialog_run(GTK_DIALOG(settings_dialog));
    }

    notify_uninit();
    xfconf_shutdown();

    return EXIT_SUCCESS;
}
