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
#error "Only <gdbus/gdbus.h> can be included directly."
#endif

#ifndef __E_DBUS_PROXY_H__
#define __E_DBUS_PROXY_H__

#include <gdbus/gdbustypes.h>

G_BEGIN_DECLS

#define E_TYPE_DBUS_PROXY         (e_dbus_proxy_get_type ())
#define E_DBUS_PROXY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_DBUS_PROXY, EDBusProxy))
#define E_DBUS_PROXY_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_DBUS_PROXY, EDBusProxyClass))
#define E_DBUS_PROXY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), E_TYPE_DBUS_PROXY, EDBusProxyClass))
#define G_IS_DBUS_PROXY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_DBUS_PROXY))
#define G_IS_DBUS_PROXY_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_DBUS_PROXY))

typedef struct _EDBusProxyClass   EDBusProxyClass;
typedef struct _EDBusProxyPrivate EDBusProxyPrivate;

/**
 * EDBusProxy:
 *
 * The #EDBusProxy structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _EDBusProxy
{
  /*< private >*/
  GObject parent_instance;
  EDBusProxyPrivate *priv;
};

/**
 * EDBusProxyClass:
 * @properties_changed: Signal class handler for the #EDBusProxy::g-dbus-proxy-properties-changed signal.
 * @signal: Signal class handler for the #EDBusProxy::g-dbus-proxy-signal signal.
 *
 * Class structure for #EDBusProxy.
 */
struct _EDBusProxyClass
{
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/
  void (*properties_changed)           (EDBusProxy   *proxy,
                                        GHashTable   *changed_properties);
  void (*signal)                       (EDBusProxy   *proxy,
                                        const gchar  *sender_name,
                                        const gchar  *signal_name,
                                        EVariant     *parameters);

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

GType            e_dbus_proxy_get_type                  (void) G_GNUC_CONST;
void             e_dbus_proxy_new                       (EDBusConnection     *connection,
                                                         GType                object_type,
                                                         EDBusProxyFlags      flags,
                                                         const gchar         *unique_bus_name,
                                                         const gchar         *object_path,
                                                         const gchar         *interface_name,
                                                         GCancellable        *cancellable,
                                                         GAsyncReadyCallback  callback,
                                                         gpointer             user_data);
EDBusProxy      *e_dbus_proxy_new_finish                (GAsyncResult        *res,
                                                         GError             **error);
EDBusProxy      *e_dbus_proxy_new_sync                  (EDBusConnection     *connection,
                                                         GType                object_type,
                                                         EDBusProxyFlags      flags,
                                                         const gchar         *unique_bus_name,
                                                         const gchar         *object_path,
                                                         const gchar         *interface_name,
                                                         GCancellable        *cancellable,
                                                         GError             **error);
EDBusConnection *e_dbus_proxy_get_connection            (EDBusProxy          *proxy);
EDBusProxyFlags  e_dbus_proxy_get_flags                 (EDBusProxy          *proxy);
const gchar     *e_dbus_proxy_get_unique_bus_name       (EDBusProxy          *proxy);
const gchar     *e_dbus_proxy_get_object_path           (EDBusProxy          *proxy);
const gchar     *e_dbus_proxy_get_interface_name        (EDBusProxy          *proxy);
gint             e_dbus_proxy_get_default_timeout       (EDBusProxy          *proxy);
void             e_dbus_proxy_set_default_timeout       (EDBusProxy          *proxy,
                                                         gint                 timeout_msec);
EVariant        *e_dbus_proxy_get_cached_property       (EDBusProxy          *proxy,
                                                         const gchar         *property_name,
                                                         GError             **error);
gchar          **e_dbus_proxy_get_cached_property_names (EDBusProxy          *proxy,
                                                         GError             **error);
void             e_dbus_proxy_invoke_method             (EDBusProxy          *proxy,
                                                         const gchar         *method_name,
                                                         EVariant            *parameters,
                                                         gint                 timeout_msec,
                                                         GCancellable        *cancellable,
                                                         GAsyncReadyCallback  callback,
                                                         gpointer             user_data);
EVariant        *e_dbus_proxy_invoke_method_finish      (EDBusProxy          *proxy,
                                                         GAsyncResult        *res,
                                                         GError             **error);
EVariant        *e_dbus_proxy_invoke_method_sync        (EDBusProxy          *proxy,
                                                         const gchar         *method_name,
                                                         EVariant            *parameters,
                                                         gint                 timeout_msec,
                                                         GCancellable        *cancellable,
                                                         GError             **error);

G_END_DECLS

#endif /* __E_DBUS_PROXY_H__ */
