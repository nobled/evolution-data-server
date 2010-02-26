/* EDBus - GLib D-Bus Library
 *
 * Copyright (C) 2008-2009 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#if !defined (__E_DBUS_H_INSIDE__) && !defined (E_DBUS_COMPILATION)
#error "Only <edbus/edbus.h> can be included directly."
#endif

#ifndef __E_DBUS_SERVER_H__
#define __E_DBUS_SERVER_H__

#include <edbus/edbustypes.h>

G_BEGIN_DECLS

#define E_TYPE_DBUS_SERVER         (e_dbus_server_get_type ())
#define E_DBUS_SERVER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_DBUS_SERVER, EDBusServer))
#define E_DBUS_SERVER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_DBUS_SERVER, EDBusServerClass))
#define E_DBUS_SERVER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), E_TYPE_DBUS_SERVER, EDBusServerClass))
#define G_IS_DBUS_SERVER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_DBUS_SERVER))
#define G_IS_DBUS_SERVER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_DBUS_SERVER))

typedef struct _EDBusServerClass   EDBusServerClass;
typedef struct _EDBusServerPrivate EDBusServerPrivate;

/**
 * EDBusServer:
 *
 * The #EDBusServer structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _EDBusServer
{
  /*< private >*/
  GObject parent_instance;
  EDBusServerPrivate *priv;
};

/**
 * EDBusServerClass:
 * @new_connection: Signal class handler for the #EDBusServer::new-connection signal.
 *
 * Class structure for #EDBusServer.
 */
struct _EDBusServerClass
{
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/

  /* Signals */
  void (*new_connection) (EDBusServer     *server,
                          EDBusConnection *connection);

  /*< private >*/
  /* Padding for future expansion */
  void (*_g_reserved1) (void);
  void (*_g_reserved2) (void);
  void (*_g_reserved3) (void);
  void (*_g_reserved4) (void);
  void (*_g_reserved5) (void);
  void (*_g_reserved6) (void);
  void (*_g_reserved7) (void);
  void (*_g_reserved8) (void);
};

GType        e_dbus_server_get_type    (void) G_GNUC_CONST;
EDBusServer *e_dbus_server_new         (const gchar *address,
                                        GError      **error);
const gchar *e_dbus_server_get_address (EDBusServer *server);

G_END_DECLS

#endif /* __E_DBUS_SERVER_H__ */
