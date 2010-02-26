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

#ifndef __E_DBUS_NAME_OWNING_H__
#define __E_DBUS_NAME_OWNING_H__

#include <edbus/edbustypes.h>

G_BEGIN_DECLS

/**
 * GBusNameAcquiredCallback:
 * @connection: The #EDBusConnection on which to acquired the name.
 * @name: The name being owned.
 * @user_data: User data passed to e_bus_own_name().
 *
 * Invoked when the name is acquired.
 */
typedef void (*GBusNameAcquiredCallback) (EDBusConnection *connection,
                                          const gchar     *name,
                                          gpointer         user_data);

/**
 * GBusNameLostCallback:
 * @connection: The #EDBusConnection on which to acquire the name or %NULL if
 * the connection was disconnected.
 * @name: The name being owned.
 * @user_data: User data passed to e_bus_own_name().
 *
 * Invoked when the name is lost or @connection has been disconnected.
 */
typedef void (*GBusNameLostCallback) (EDBusConnection *connection,
                                      const gchar     *name,
                                      gpointer         user_data);

guint e_bus_own_name                 (EDBusType                  bus_type,
                                      const gchar              *name,
                                      EDBusNameOwnerFlags        flags,
                                      GBusNameAcquiredCallback  name_acquired_handler,
                                      GBusNameLostCallback      name_lost_handler,
                                      gpointer                  user_data,
                                      GDestroyNotify            user_data_free_func);
guint e_bus_own_name_on_connection   (EDBusConnection          *connection,
                                      const gchar              *name,
                                      EDBusNameOwnerFlags        flags,
                                      GBusNameAcquiredCallback  name_acquired_handler,
                                      GBusNameLostCallback      name_lost_handler,
                                      gpointer                  user_data,
                                      GDestroyNotify            user_data_free_func);
void  e_bus_unown_name               (guint                     owner_id);

G_END_DECLS

#endif /* __E_DBUS_NAME_OWNING_H__ */
