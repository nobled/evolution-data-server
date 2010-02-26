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

#ifndef __E_DBUS_CONNECTION_H__
#define __E_DBUS_CONNECTION_H__

#include <edbus/edbustypes.h>

G_BEGIN_DECLS

#define E_TYPE_DBUS_CONNECTION         (e_dbus_connection_get_type ())
#define E_DBUS_CONNECTION(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_DBUS_CONNECTION, EDBusConnection))
#define E_DBUS_CONNECTION_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_DBUS_CONNECTION, EDBusConnectionClass))
#define E_DBUS_CONNECTION_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), E_TYPE_DBUS_CONNECTION, EDBusConnectionClass))
#define G_IS_DBUS_CONNECTION(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_DBUS_CONNECTION))
#define G_IS_DBUS_CONNECTION_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_DBUS_CONNECTION))

typedef struct _EDBusConnectionClass   EDBusConnectionClass;
typedef struct _EDBusConnectionPrivate EDBusConnectionPrivate;

/**
 * EDBusConnection:
 *
 * The #EDBusConnection structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _EDBusConnection
{
  /*< private >*/
  GObject parent_instance;
  EDBusConnectionPrivate *priv;
};

/**
 * EDBusConnectionClass:
 * @disconnected: Signal class handler for the #EDBusConnection::disconnected signal.
 *
 * Class structure for #EDBusConnection.
 */
struct _EDBusConnectionClass
{
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/

  /* Signals */
  void (*disconnected) (EDBusConnection *connection);

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

GType            e_dbus_connection_get_type                   (void) G_GNUC_CONST;

void             e_dbus_connection_new                        (const gchar         *address,
                                                               GCancellable        *cancellable,
                                                               GAsyncReadyCallback  callback,
                                                               gpointer             user_data);
EDBusConnection *e_dbus_connection_new_finish                 (GAsyncResult        *res,
                                                               GError             **error);
EDBusConnection *e_dbus_connection_new_sync                   (const gchar         *address,
                                                               GCancellable       *cancellable,
                                                               GError            **error);

void             e_dbus_connection_bus_get                    (EDBusType             bus_type,
                                                               GCancellable        *cancellable,
                                                               GAsyncReadyCallback  callback,
                                                               gpointer             user_data);
EDBusConnection *e_dbus_connection_bus_get_finish             (GAsyncResult        *res,
                                                               GError             **error);
EDBusConnection *e_dbus_connection_bus_get_sync               (EDBusType            bus_type,
                                                               GCancellable       *cancellable,
                                                               GError            **error);

void             e_dbus_connection_bus_get_private            (EDBusType             bus_type,
                                                               GCancellable        *cancellable,
                                                               GAsyncReadyCallback  callback,
                                                               gpointer             user_data);
EDBusConnection *e_dbus_connection_bus_get_private_finish     (GAsyncResult        *res,
                                                               GError             **error);
EDBusConnection *e_dbus_connection_bus_get_private_sync       (EDBusType            bus_type,
                                                               GCancellable       *cancellable,
                                                               GError            **error);
EDBusType         e_dbus_connection_get_bus_type               (EDBusConnection    *connection);
const gchar     *e_dbus_connection_get_address                (EDBusConnection    *connection);
const gchar     *e_dbus_connection_get_unique_name            (EDBusConnection    *connection);
gboolean         e_dbus_connection_get_is_private             (EDBusConnection    *connection);
gboolean         e_dbus_connection_get_is_disconnected        (EDBusConnection    *connection);
void             e_dbus_connection_set_exit_on_disconnect     (EDBusConnection    *connection,
                                                               gboolean            exit_on_disconnect);
void             e_dbus_connection_disconnect                 (EDBusConnection    *connection);

/**
 * EDBusInterfaceMethodCallFunc:
 * @connection: A #EDBusConnection.
 * @user_data: The @user_data #gpointer passed to e_dbus_connection_register_object().
 * @sender: The unique bus name of the remote caller.
 * @object_path: The object path that the method was invoked on.
 * @interface_name: The D-Bus interface name the method was invoked on.
 * @method_name: The name of the method that was invoked.
 * @parameters: A #EVariant tuple with parameters.
 * @invocation: A #EDBusMethodInvocation object that can be used to return a value or error.
 *
 * The type of the @method_call function in #EDBusInterfaceVTable.
 */
typedef void (*EDBusInterfaceMethodCallFunc) (EDBusConnection       *connection,
                                              gpointer               user_data,
                                              const gchar           *sender,
                                              const gchar           *object_path,
                                              const gchar           *interface_name,
                                              const gchar           *method_name,
                                              EVariant              *parameters,
                                              EDBusMethodInvocation *invocation);

/**
 * EDBusInterfaceGetPropertyFunc:
 * @connection: A #EDBusConnection.
 * @user_data: The @user_data #gpointer passed to e_dbus_connection_register_object().
 * @sender: The unique bus name of the remote caller.
 * @object_path: The object path that the method was invoked on.
 * @interface_name: The D-Bus interface name for the property.
 * @property_name: The name of the property to get the value of.
 * @error: Return location for error.
 *
 * The type of the @get_property function in #EDBusInterfaceVTable.
 *
 * Returns: A newly-allocated #EVariant with the value for @property_name or %NULL if @error is set.
 */
typedef EVariant *(*EDBusInterfaceGetPropertyFunc) (EDBusConnection       *connection,
                                                    gpointer               user_data,
                                                    const gchar           *sender,
                                                    const gchar           *object_path,
                                                    const gchar           *interface_name,
                                                    const gchar           *property_name,
                                                    GError               **error);

/**
 * EDBusInterfaceSetPropertyFunc:
 * @connection: A #EDBusConnection.
 * @user_data: The @user_data #gpointer passed to e_dbus_connection_register_object().
 * @sender: The unique bus name of the remote caller.
 * @object_path: The object path that the method was invoked on.
 * @interface_name: The D-Bus interface name for the property.
 * @property_name: The name of the property to get the value of.
 * @value: The value to set the property to.
 * @error: Return location for error.
 *
 * The type of the @set_property function in #EDBusInterfaceVTable.
 *
 * Returns: %TRUE if the property was set to @value, %FALSE if @error is set.
 */
typedef gboolean  (*EDBusInterfaceSetPropertyFunc) (EDBusConnection       *connection,
                                                    gpointer               user_data,
                                                    const gchar           *sender,
                                                    const gchar           *object_path,
                                                    const gchar           *interface_name,
                                                    const gchar           *property_name,
                                                    EVariant              *value,
                                                    GError               **error);

/**
 * EDBusInterfaceVTable:
 * @method_call: Function for handling incoming method calls.
 * @get_property: Function for getting a property.
 * @set_property: Function for setting a property.
 *
 * Virtual table for handling properties and method calls for a D-Bus
 * interface.
 *
 * If you want to handle getting/setting D-Bus properties asynchronously, simply
 * register an object with the <literal>org.freedesktop.DBus.Properties</literal>
 * D-Bus interface using e_dbus_connection_register_object().
 */
struct _EDBusInterfaceVTable
{
  EDBusInterfaceMethodCallFunc  method_call;
  EDBusInterfaceGetPropertyFunc get_property;
  EDBusInterfaceSetPropertyFunc set_property;

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

guint            e_dbus_connection_register_object            (EDBusConnection            *connection,
                                                               const gchar                *object_path,
                                                               const gchar                *interface_name,
                                                               const EDBusInterfaceInfo   *introspection_data,
                                                               const EDBusInterfaceVTable *vtable,
                                                               gpointer                    user_data,
                                                               GDestroyNotify              user_data_free_func,
                                                               GError                    **error);
gboolean         e_dbus_connection_unregister_object          (EDBusConnection            *connection,
                                                               guint                       registration_id);

/**
 * EDBusSubtreeEnumerateFunc:
 * @connection: A #EDBusConnection.
 * @user_data: The @user_data #gpointer passed to e_dbus_connection_register_subtree().
 * @sender: The unique bus name of the remote caller.
 * @object_path: The object path that was registered with e_dbus_connection_register_subtree().
 *
 * The type of the @enumerate function in #EDBusSubtreeVTable.
 *
 * Returns: A newly allocated array of strings for node names that are children of @object_path.
 */
typedef gchar** (*EDBusSubtreeEnumerateFunc) (EDBusConnection       *connection,
                                              gpointer               user_data,
                                              const gchar           *sender,
                                              const gchar           *object_path);

/**
 * EDBusSubtreeIntrospectFunc:
 * @connection: A #EDBusConnection.
 * @user_data: The @user_data #gpointer passed to e_dbus_connection_register_subtree().
 * @sender: The unique bus name of the remote caller.
 * @object_path: The object path that was registered with e_dbus_connection_register_subtree().
 * @node: A node that is a child of @object_path (relative to @object_path) or <quote>/</quote> for the root of the subtree.
 *
 * The type of the @introspect function in #EDBusSubtreeVTable.
 *
 * Returns: A newly-allocated #GPtrArray with pointers to #EDBusInterfaceInfo describing
 * the interfaces implemented by @node.
 */
typedef GPtrArray *(*EDBusSubtreeIntrospectFunc) (EDBusConnection       *connection,
                                                  gpointer               user_data,
                                                  const gchar           *sender,
                                                  const gchar           *object_path,
                                                  const gchar           *node);

/**
 * EDBusSubtreeDispatchFunc:
 * @connection: A #EDBusConnection.
 * @user_data: The @user_data #gpointer passed to e_dbus_connection_register_subtree().
 * @sender: The unique bus name of the remote caller.
 * @object_path: The object path that was registered with e_dbus_connection_register_subtree().
 * @interface_name: The D-Bus interface name that the method call or property access is for.
 * @node: A node that is a child of @object_path (relative to @object_path) or <quote>/</quote> for the root of the subtree.
 * @out_user_data: Return location for user data to pass to functions in the returned #EDBusInterfaceVTable (never %NULL).
 *
 * The type of the @dispatch function in #EDBusSubtreeVtable.
 *
 * Returns: A #EDBusInterfaceVTable or %NULL if you don't want to handle the methods.
 */
typedef const EDBusInterfaceVTable * (*EDBusSubtreeDispatchFunc) (EDBusConnection             *connection,
                                                                  gpointer                     user_data,
                                                                  const gchar                 *sender,
                                                                  const gchar                 *object_path,
                                                                  const gchar                 *interface_name,
                                                                  const gchar                 *node,
                                                                  gpointer                    *out_user_data);

/**
 * EDBusSubtreeVTable:
 * @enumerate: Function for enumerating child nodes.
 * @introspect: Function for introspecting a child node.
 * @dispatch: Function for dispatching a remote call on a child node.
 *
 * Virtual table for handling subtrees registered with e_dbus_connection_register_subtree().
 */
struct _EDBusSubtreeVTable
{
  EDBusSubtreeEnumerateFunc  enumerate;
  EDBusSubtreeIntrospectFunc introspect;
  EDBusSubtreeDispatchFunc   dispatch;

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

guint            e_dbus_connection_register_subtree           (EDBusConnection            *connection,
                                                               const gchar                *object_path,
                                                               const EDBusSubtreeVTable   *vtable,
                                                               EDBusSubtreeFlags           flags,
                                                               gpointer                    user_data,
                                                               GDestroyNotify              user_data_free_func,
                                                               GError                    **error);
gboolean         e_dbus_connection_unregister_subtree         (EDBusConnection            *connection,
                                                               guint                       registration_id);

gboolean  e_dbus_connection_emit_signal                       (EDBusConnection    *connection,
                                                               const gchar        *destination_bus_name,
                                                               const gchar        *object_path,
                                                               const gchar        *interface_name,
                                                               const gchar        *signal_name,
                                                               EVariant           *parameters,
                                                               GError            **error);
void      e_dbus_connection_invoke_method                     (EDBusConnection    *connection,
                                                               const gchar        *bus_name,
                                                               const gchar        *object_path,
                                                               const gchar        *interface_name,
                                                               const gchar        *method_name,
                                                               EVariant           *parameters,
                                                               gint                timeout_msec,
                                                               GCancellable       *cancellable,
                                                               GAsyncReadyCallback callback,
                                                               gpointer            user_data);
EVariant *e_dbus_connection_invoke_method_finish              (EDBusConnection    *connection,
                                                               GAsyncResult       *res,
                                                               GError            **error);
EVariant *e_dbus_connection_invoke_method_sync                (EDBusConnection    *connection,
                                                               const gchar        *bus_name,
                                                               const gchar        *object_path,
                                                               const gchar        *interface_name,
                                                               const gchar        *method_name,
                                                               EVariant           *parameters,
                                                               gint                timeout_msec,
                                                               GCancellable       *cancellable,
                                                               GError            **error);

/**
 * EDBusSignalCallback:
 * @connection: A #EDBusConnection.
 * @sender_name: The unique bus name of the sender of the signal.
 * @object_path: The object path that the signal was emitted on.
 * @interface_name: The name of the signal.
 * @signal_name: The name of the signal.
 * @parameters: A #EVariant tuple with parameters for the signal.
 * @user_data: User data passed when subscribing to the signal.
 *
 * Signature for callback function used in e_dbus_connection_signal_subscribe().
 */
typedef void (*EDBusSignalCallback) (EDBusConnection  *connection,
                                     const gchar      *sender_name,
                                     const gchar      *object_path,
                                     const gchar      *interface_name,
                                     const gchar      *signal_name,
                                     EVariant         *parameters,
                                     gpointer          user_data);

guint            e_dbus_connection_signal_subscribe           (EDBusConnection     *connection,
                                                               const gchar         *sender,
                                                               const gchar         *interface_name,
                                                               const gchar         *member,
                                                               const gchar         *object_path,
                                                               const gchar         *arg0,
                                                               EDBusSignalCallback  callback,
                                                               gpointer             user_data,
                                                               GDestroyNotify      user_data_free_func);
void             e_dbus_connection_signal_unsubscribe         (EDBusConnection     *connection,
                                                               guint                subscription_id);

G_END_DECLS

#endif /* __E_DBUS_CONNECTION_H__ */
