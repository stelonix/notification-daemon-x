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
#ifndef NOTIFY_DAEMON_H
#define NOTIFY_DAEMON_H

#include <glib.h>
#include <glib-object.h>

#define NOTIFY_TYPE_DAEMON (notify_daemon_get_type())
#define NOTIFY_DAEMON(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST ((obj), NOTIFY_TYPE_DAEMON, NotifyDaemon))
#define NOTIFY_DAEMON_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST ((klass), NOTIFY_TYPE_DAEMON, NotifyDaemonClass))
#define NOTIFY_IS_DAEMON(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NOTIFY_TYPE_DAEMON))
#define NOTIFY_IS_DAEMON_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE ((klass), NOTIFY_TYPE_DAEMON))
#define NOTIFY_DAEMON_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS((obj), NOTIFY_TYPE_DAEMON, NotifyDaemonClass))

#define NOTIFY_DAEMON_DEFAULT_TIMEOUT 7000

enum
{
        URGENCY_LOW,
        URGENCY_NORMAL,
        URGENCY_CRITICAL
};

typedef struct _NotifyDaemon NotifyDaemon;
typedef struct _NotifyDaemonClass NotifyDaemonClass;
typedef struct _NotifyDaemonPrivate NotifyDaemonPrivate;

struct _NotifyDaemon
{
        GObject         parent;

        /*< private > */
        NotifyDaemonPrivate *priv;
};

struct _NotifyDaemonClass
{
        GObjectClass    parent_class;
};

G_BEGIN_DECLS GType notify_daemon_get_type (void);

G_END_DECLS
#endif /* NOTIFY_DAEMON_H */
