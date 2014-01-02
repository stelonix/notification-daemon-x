/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 Christian Hammond <chipx86@chipx86.com>
 * Copyright (C) 2005 John (J5) Palmieri <johnp@redhat.com>
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include <glib/gi18n.h>
#include <glib.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include <X11/Xproto.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <gdk/gdkx.h>

#include "daemon.h"
#include "nd-notification.h"
#include "nd-queue.h"

#define MAX_NOTIFICATIONS 20

#define IDLE_SECONDS 30
#define NOTIFICATION_BUS_NAME      "org.freedesktop.Notifications"
#define NOTIFICATION_BUS_PATH      "/org/freedesktop/Notifications"

#define NOTIFICATION_SPEC_VERSION  "1.2"

#define NW_GET_DAEMON(nw) \
        (g_object_get_data(G_OBJECT(nw), "_notify_daemon"))

struct _NotifyDaemonPrivate
{
        GDBusConnection *connection;
        NdQueue         *queue;
};

static void notify_daemon_finalize (GObject *object);

G_DEFINE_TYPE (NotifyDaemon, notify_daemon, G_TYPE_OBJECT);

static void
notify_daemon_class_init (NotifyDaemonClass *daemon_class)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (daemon_class);

        object_class->finalize = notify_daemon_finalize;

        g_type_class_add_private (daemon_class, sizeof (NotifyDaemonPrivate));
}

static void
notify_daemon_init (NotifyDaemon *daemon)
{
        daemon->priv = G_TYPE_INSTANCE_GET_PRIVATE (daemon,
                                                    NOTIFY_TYPE_DAEMON,
                                                    NotifyDaemonPrivate);

        daemon->priv->queue = nd_queue_new ();
}

static void
notify_daemon_finalize (GObject *object)
{
        NotifyDaemon *daemon;

        daemon = NOTIFY_DAEMON (object);

        g_object_unref (daemon->priv->queue);

        g_free (daemon->priv);

        G_OBJECT_CLASS (notify_daemon_parent_class)->finalize (object);
}

static void
on_notification_close (NdNotification *notification,
                       int             reason,
                       NotifyDaemon   *daemon)
{
        g_dbus_connection_emit_signal (daemon->priv->connection,
                                       nd_notification_get_sender (notification),
                                       "/org/freedesktop/Notifications",
                                       "org.freedesktop.Notifications",
                                       "NotificationClosed",
                                       g_variant_new ("(uu)", nd_notification_get_id (notification), reason),
                                       NULL);
}

static void
on_notification_action_invoked (NdNotification *notification,
                                const char     *action,
                                NotifyDaemon   *daemon)
{
        g_dbus_connection_emit_signal (daemon->priv->connection,
                                       nd_notification_get_sender (notification),
                                       "/org/freedesktop/Notifications",
                                       "org.freedesktop.Notifications",
                                       "ActionInvoked",
                                       g_variant_new ("(us)", nd_notification_get_id (notification), action),
                                       NULL);

        /* resident notifications don't close when actions are invoked */
        if (! nd_notification_get_is_resident (notification)) {
                nd_notification_close (notification, ND_NOTIFICATION_CLOSED_USER);
        }
}

/* ---------------------------------------------------------------------------------------------- */

static GDBusNodeInfo *introspection_data = NULL;

/* Introspection data for the service we are exporting */
static const char introspection_xml[] =
        "<node>"
        "  <interface name='org.freedesktop.Notifications'>"
        "    <method name='Notify'>"
        "      <arg type='s' name='app_name' direction='in' />"
        "      <arg type='u' name='id' direction='in' />"
        "      <arg type='s' name='icon' direction='in' />"
        "      <arg type='s' name='summary' direction='in' />"
        "      <arg type='s' name='body' direction='in' />"
        "      <arg type='as' name='actions' direction='in' />"
        "      <arg type='a{sv}' name='hints' direction='in' />"
        "      <arg type='i' name='timeout' direction='in' />"
        "      <arg type='u' name='return_id' direction='out' />"
        "    </method>"
        "    <method name='CloseNotification'>"
        "      <arg type='u' name='id' direction='in' />"
        "    </method>"
        "    <method name='GetCapabilities'>"
        "      <arg type='as' name='return_caps' direction='out'/>"
        "    </method>"
        "    <method name='GetServerInformation'>"
        "      <arg type='s' name='return_name' direction='out'/>"
        "      <arg type='s' name='return_vendor' direction='out'/>"
        "      <arg type='s' name='return_version' direction='out'/>"
        "      <arg type='s' name='return_spec_version' direction='out'/>"
        "    </method>"
        "  </interface>"
        "</node>";

static void
handle_notify (NotifyDaemon          *daemon,
               const char            *sender,
               GVariant              *parameters,
               GDBusMethodInvocation *invocation)
{
        NdNotification *notification;
        const char     *app_name;
        guint           id;
        const char     *icon_name;
        const char     *summary;
        const char     *body;
        const char    **actions;
        GVariantIter   *hints_iter;
        int             timeout;

        if (nd_queue_length (daemon->priv->queue) > MAX_NOTIFICATIONS) {
                g_dbus_method_invocation_return_dbus_error (invocation,
                                                            "org.freedesktop.Notifications.MaxNotificationsExceeded",
                                                            _("Exceeded maximum number of notifications"));
                return;
        }

        g_variant_get (parameters,
                       "(&su&s&s&s^a&sa{sv}i)",
                       &app_name,
                       &id,
                       &icon_name,
                       &summary,
                       &body,
                       &actions,
                       &hints_iter,
                       &timeout);

        if (id > 0) {
                notification = nd_queue_lookup (daemon->priv->queue, id);
                if (notification == NULL) {
                        id = 0;
                } else {
                        g_object_ref (notification);
                }
        }

        if (id == 0) {
                notification = nd_notification_new (sender);
                g_signal_connect (notification, "closed", G_CALLBACK (on_notification_close), daemon);
                g_signal_connect (notification, "action-invoked", G_CALLBACK (on_notification_action_invoked), daemon);
        }

        nd_notification_update (notification,
                                app_name,
                                icon_name,
                                summary,
                                body,
                                actions,
                                hints_iter,
                                timeout);

        if (id == 0) {
                nd_queue_add (daemon->priv->queue, notification);
        }

        g_dbus_method_invocation_return_value (invocation,
                                               g_variant_new ("(u)", nd_notification_get_id (notification)));

        g_object_unref (notification);
}

static void
handle_close_notification (NotifyDaemon          *daemon,
                           const char            *sender,
                           GVariant              *parameters,
                           GDBusMethodInvocation *invocation)
{
        NdNotification *notification;
        guint           id;

        g_variant_get (parameters, "(u)", &id);

        if (id == 0) {
                g_dbus_method_invocation_return_dbus_error (invocation,
                                                            "org.freedesktop.Notifications.InvalidId",
                                                            _("Invalid notification identifier"));
                return;
        }

        notification = nd_queue_lookup (daemon->priv->queue, id);
        if (notification != NULL) {
                nd_notification_close (notification, ND_NOTIFICATION_CLOSED_API);
        }

        g_dbus_method_invocation_return_value (invocation, NULL);
}

static void
handle_get_capabilities (NotifyDaemon          *daemon,
                         const char            *sender,
                         GVariant              *parameters,
                         GDBusMethodInvocation *invocation)
{
        GVariantBuilder *builder;

        builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));
        g_variant_builder_add (builder, "s", "actions");
        g_variant_builder_add (builder, "s", "body");
        g_variant_builder_add (builder, "s", "body-hyperlinks");
        g_variant_builder_add (builder, "s", "body-markup");
        g_variant_builder_add (builder, "s", "icon-static");
        g_variant_builder_add (builder, "s", "sound");
        g_variant_builder_add (builder, "s", "persistence");
        g_variant_builder_add (builder, "s", "action-icons");

        g_dbus_method_invocation_return_value (invocation,
                                               g_variant_new ("(as)", builder));
        g_variant_builder_unref (builder);
}

static void
handle_get_server_information (NotifyDaemon          *daemon,
                               const char            *sender,
                               GVariant              *parameters,
                               GDBusMethodInvocation *invocation)
{
        g_dbus_method_invocation_return_value (invocation,
                                               g_variant_new ("(ssss)",
                                                              "Notification Daemon",
                                                              "GNOME",
                                                              PACKAGE_VERSION,
                                                              NOTIFICATION_SPEC_VERSION));
}

static void
handle_method_call (GDBusConnection       *connection,
                    const char            *sender,
                    const char            *object_path,
                    const char            *interface_name,
                    const char            *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data)
{
        NotifyDaemon *daemon = user_data;

        if (g_strcmp0 (method_name, "Notify") == 0) {
                handle_notify (daemon, sender, parameters, invocation);
        } else if (g_strcmp0 (method_name, "CloseNotification") == 0) {
                handle_close_notification (daemon, sender, parameters, invocation);
        } else if (g_strcmp0 (method_name, "GetCapabilities") == 0) {
                handle_get_capabilities (daemon, sender, parameters, invocation);
        } else if (g_strcmp0 (method_name, "GetServerInformation") == 0) {
                handle_get_server_information (daemon, sender, parameters, invocation);
        }
}

/* for now */
static const GDBusInterfaceVTable interface_vtable =
{
        handle_method_call,
        NULL, /* get property */
        NULL  /* set property */
};

static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
        NotifyDaemon *daemon = user_data;
        guint         registration_id;

        registration_id = g_dbus_connection_register_object (connection,
                                                             "/org/freedesktop/Notifications",
                                                             introspection_data->interfaces[0],
                                                             &interface_vtable,
                                                             daemon,
                                                             NULL,  /* user_data_free_func */
                                                             NULL); /* GError** */
        g_assert (registration_id > 0);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
        NotifyDaemon *daemon = user_data;
        daemon->priv->connection = connection;
}

static void
on_name_lost (GDBusConnection *connection,
              const char      *name,
              gpointer         user_data)
{
        exit (1);
}


int
main (int argc, char **argv)
{
        NotifyDaemon *daemon;
        guint         owner_id;

        g_log_set_always_fatal (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

        gtk_init (&argc, &argv);

        introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
        g_assert (introspection_data != NULL);

        daemon = g_object_new (NOTIFY_TYPE_DAEMON, NULL);

        owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                   "org.freedesktop.Notifications",
                                   G_BUS_NAME_OWNER_FLAGS_NONE,
                                   on_bus_acquired,
                                   on_name_acquired,
                                   on_name_lost,
                                   daemon,
                                   NULL);

        gtk_main ();

        g_bus_unown_name (owner_id);
        g_dbus_node_info_unref (introspection_data);

        g_object_unref (daemon);

        return 0;
}
