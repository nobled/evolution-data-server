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

#include "config.h"

#include <stdlib.h>

#include <glib/gi18n.h>

#include "edbusconnection.h"
#include "edbuserror.h"
#include "edbusenumtypes.h"
#include "edbusconversion.h"
#include "edbusintrospection.h"
#include "edbusmethodinvocation.h"
#include "edbusprivate.h"

/**
 * SECTION:gdbusconnection
 * @short_description: D-Bus Connections
 * @include: edbus/edbus.h
 *
 * <para><note>
 * This class is rarely used directly. If you are writing an application, it is often
 * easier to use the e_bus_own_name() or e_bus_watch_name() APIs.
 * </note></para>
 * #EDBusConnection is a thin wrapper class for the #DBusConnection
 * type that integrates with the GLib type system.
 *
 * TODO: stuff about caching unix_process_id etc. when we add that.
 */

struct _EDBusConnectionPrivate
{
  DBusConnection *dbus_1_connection;

  /* construct properties */
  EDBusType        bus_type;
  gchar          *address;
  gboolean        is_private;

  gboolean is_initialized;
  GError *initialization_error;

  /* unfortunately there is no dbus_connection_get_exit_on_disconnect() so we need to track this ourselves */
  gboolean exit_on_disconnect;

  /* Maps used for signal subscription */
  GHashTable *map_rule_to_signal_data;
  GHashTable *map_id_to_signal_data;
  GHashTable *map_sender_to_signal_data_array;

  /* Maps used for exporting interfaces */
  GHashTable *map_object_path_to_eo; /* gchar* -> ExportedObject* */
  GHashTable *map_id_to_ei;          /* guint -> ExportedInterface* */

  /* Map used for exporting subtrees */
  GHashTable *map_object_path_to_es; /* gchar* -> ExportedSubtree* */
  GHashTable *map_id_to_es;          /* guint  -> ExportedSubtree* */
};

static void         e_dbus_connection_send_dbus_1_message_with_reply           (EDBusConnection    *connection,
                                                                                DBusMessage        *message,
                                                                                gint                timeout_msec,
                                                                                GCancellable       *cancellable,
                                                                                GAsyncReadyCallback callback,
                                                                                gpointer            user_data);
static DBusMessage *e_dbus_connection_send_dbus_1_message_with_reply_finish    (EDBusConnection    *connection,
                                                                                GAsyncResult       *res,
                                                                                GError            **error);
static DBusMessage *e_dbus_connection_send_dbus_1_message_with_reply_sync      (EDBusConnection    *connection,
                                                                                DBusMessage        *message,
                                                                                gint                timeout_msec,
                                                                                GCancellable       *cancellable,
                                                                                GError            **error);

typedef struct ExportedObject ExportedObject;
static void exported_object_free (ExportedObject *eo);

typedef struct ExportedSubtree ExportedSubtree;
static void exported_subtree_free (ExportedSubtree *es);

enum
{
  DISCONNECTED_SIGNAL,
  LAST_SIGNAL,
};

enum
{
  PROP_0,
  PROP_BUS_TYPE,
  PROP_ADDRESS,
  PROP_IS_PRIVATE,
  PROP_UNIQUE_NAME,
  PROP_IS_DISCONNECTED,
  PROP_EXIT_ON_DISCONNECT,
};

static void distribute_signals (EDBusConnection *connection,
                                DBusMessage     *message);

static void purge_all_signal_subscriptions (EDBusConnection *connection);

G_LOCK_DEFINE_STATIC (connection_lock);

static EDBusConnection *the_session_bus = NULL;
static EDBusConnection *the_system_bus = NULL;

static GObject *e_dbus_connection_constructor (GType                  type,
                                               guint                  n_construct_properties,
                                               GObjectConstructParam *construct_properties);

static DBusHandlerResult
filter_function (DBusConnection *dbus_1_connection,
                 DBusMessage    *message,
                 void           *user_data);

static guint signals[LAST_SIGNAL] = { 0 };

static void e_dbus_connection_set_dbus_1_connection (EDBusConnection *connection,
                                                     DBusConnection  *dbus_1_connection);

static void initable_iface_init       (GInitableIface *initable_iface);
static void async_initable_iface_init (GAsyncInitableIface *async_initable_iface);

G_DEFINE_TYPE_WITH_CODE (EDBusConnection, e_dbus_connection, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, async_initable_iface_init)
                         );

static void
e_dbus_connection_dispose (GObject *object)
{
  EDBusConnection *connection = E_DBUS_CONNECTION (object);

  G_LOCK (connection_lock);
  if (connection == the_session_bus)
    {
      the_session_bus = NULL;
    }
  else if (connection == the_system_bus)
    {
      the_system_bus = NULL;
    }
  G_UNLOCK (connection_lock);

  if (G_OBJECT_CLASS (e_dbus_connection_parent_class)->dispose != NULL)
    G_OBJECT_CLASS (e_dbus_connection_parent_class)->dispose (object);
}

static void
e_dbus_connection_finalize (GObject *object)
{
  EDBusConnection *connection = E_DBUS_CONNECTION (object);

  e_dbus_connection_set_dbus_1_connection (connection, NULL);

  if (connection->priv->initialization_error != NULL)
    g_error_free (connection->priv->initialization_error);

  g_free (connection->priv->address);

  purge_all_signal_subscriptions (connection);
  g_hash_table_unref (connection->priv->map_rule_to_signal_data);
  g_hash_table_unref (connection->priv->map_id_to_signal_data);
  g_hash_table_unref (connection->priv->map_sender_to_signal_data_array);

  g_hash_table_unref (connection->priv->map_id_to_ei);
  g_hash_table_unref (connection->priv->map_object_path_to_eo);
  g_hash_table_unref (connection->priv->map_id_to_es);
  g_hash_table_unref (connection->priv->map_object_path_to_es);

  if (G_OBJECT_CLASS (e_dbus_connection_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (e_dbus_connection_parent_class)->finalize (object);
}

static void
e_dbus_connection_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  EDBusConnection *connection = E_DBUS_CONNECTION (object);

  switch (prop_id)
    {
    case PROP_BUS_TYPE:
      g_value_set_enum (value, e_dbus_connection_get_bus_type (connection));
      break;

    case PROP_ADDRESS:
      g_value_set_string (value, e_dbus_connection_get_address (connection));
      break;

    case PROP_IS_PRIVATE:
      g_value_set_boolean (value, e_dbus_connection_get_is_private (connection));
      break;

    case PROP_UNIQUE_NAME:
      g_value_set_string (value, e_dbus_connection_get_unique_name (connection));
      break;

    case PROP_IS_DISCONNECTED:
      g_value_set_boolean (value, e_dbus_connection_get_is_disconnected (connection));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
e_dbus_connection_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  EDBusConnection *connection = E_DBUS_CONNECTION (object);

  switch (prop_id)
    {
    case PROP_BUS_TYPE:
      connection->priv->bus_type = g_value_get_enum (value);
      break;

    case PROP_ADDRESS:
      connection->priv->address = g_value_dup_string (value);
      break;

    case PROP_IS_PRIVATE:
      connection->priv->is_private = g_value_get_boolean (value);
      break;

    case PROP_EXIT_ON_DISCONNECT:
      e_dbus_connection_set_exit_on_disconnect (connection, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
e_dbus_connection_class_init (EDBusConnectionClass *klass)
{
  GObjectClass *gobject_class;

  g_type_class_add_private (klass, sizeof (EDBusConnectionPrivate));

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructor  = e_dbus_connection_constructor;
  gobject_class->finalize     = e_dbus_connection_finalize;
  gobject_class->dispose      = e_dbus_connection_dispose;
  gobject_class->set_property = e_dbus_connection_set_property;
  gobject_class->get_property = e_dbus_connection_get_property;

  /**
   * EDBusConnection:bus-type:
   *
   * When constructing an object, set this to the type of the message bus
   * the connection is for or #G_BUS_TYPE_NONE if the connection is not
   * a message bus connection.
   *
   * When reading, this property is never #G_BUS_TYPE_STARTER - if #G_BUS_TYPE_STARTER
   * was passed as a construction property, then this property will be either #G_BUS_TYPE_SESSION
   * or #G_BUS_TYPE_SYSTEM depending on what message bus activated the process.
   *
   * This property must be unset on construction if
   * #EDBusConnection:address is set upon construction.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_BUS_TYPE,
                                   g_param_spec_enum ("bus-type",
                                                      _("bus-type"),
                                                      _("The type of message bus, if any, the connection is for"),
                                                      G_TYPE_BUS_TYPE,
                                                      G_BUS_TYPE_NONE,
                                                      G_PARAM_READABLE |
                                                      G_PARAM_WRITABLE |
                                                      G_PARAM_CONSTRUCT_ONLY |
                                                      G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_BLURB |
                                                      G_PARAM_STATIC_NICK));

  /**
   * EDBusConnection:address:
   *
   * The address of the connection or %NULL if @connection is a
   * message bus connection.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_ADDRESS,
                                   g_param_spec_string ("address",
                                                        _("Address"),
                                                        _("The address of the connection"),
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_STATIC_NICK));

  /**
   * EDBusConnection:is-private:
   *
   * When constructing an object and #EDBusConnection:bus-type is set to something
   * other than #G_BUS_TYPE_NONE, specifies whether the connection to the requested
   * message bus should be a private connection.
   * This property is ignored if #EDBusConnection:dbus-1-connection is set upon construction.
   *
   * When reading, specifies if connection to the message bus is
   * private or shared with others.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_IS_PRIVATE,
                                   g_param_spec_boolean ("is-private",
                                                         _("is-private"),
                                                         _("Whether the connection to the message bus is private"),
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE |
                                                         G_PARAM_CONSTRUCT_ONLY |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_BLURB |
                                                         G_PARAM_STATIC_NICK));

  /**
   * EDBusConnection:unique-name:
   *
   * The unique name as assigned by the message bus or %NULL if the
   * connection is not open or not a message bus connection.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_UNIQUE_NAME,
                                   g_param_spec_string ("unique-name",
                                                        _("unique-name"),
                                                        _("Unique name of bus connection"),
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_STATIC_NICK));

  /**
   * EDBusConnection:is-disconnected:
   *
   * A boolean specifying whether the connection has been disconnected.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_IS_DISCONNECTED,
                                   g_param_spec_boolean ("is-disconnected",
                                                         _("is-disconnected"),
                                                         _("Whether the connection has been disconnected"),
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_BLURB |
                                                         G_PARAM_STATIC_NICK));

  /**
   * EDBusConnection:exit-on-disconnect:
   *
   * A boolean specifying whether _exit() should be called when the
   * connection has been disconnected by the other end.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_EXIT_ON_DISCONNECT,
                                   g_param_spec_boolean ("exit-on-disconnect",
                                                         _("exit-on-disconnect"),
                                                         _("Whether _exit() is called when the connection has been disconnected"),
                                                         TRUE,
                                                         G_PARAM_WRITABLE |
                                                         G_PARAM_CONSTRUCT |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_BLURB |
                                                         G_PARAM_STATIC_NICK));


  /**
   * EDBusConnection::disconnected:
   * @connection: The #EDBusConnection emitting the signal.
   *
   * Emitted when the connection has been disconnected. You should
   * give up your reference to @connection when receiving this signal.
   *
   * You are guaranteed that this signal is emitted only once.
   **/
  signals[DISCONNECTED_SIGNAL] = g_signal_new ("disconnected",
                                               E_TYPE_DBUS_CONNECTION,
                                               G_SIGNAL_RUN_LAST,
                                               G_STRUCT_OFFSET (EDBusConnectionClass, disconnected),
                                               NULL,
                                               NULL,
                                               g_cclosure_marshal_VOID__VOID,
                                               G_TYPE_NONE,
                                               0);
}

static void
e_dbus_connection_init (EDBusConnection *connection)
{
  connection->priv = G_TYPE_INSTANCE_GET_PRIVATE (connection, E_TYPE_DBUS_CONNECTION, EDBusConnectionPrivate);

  connection->priv->map_rule_to_signal_data = g_hash_table_new (g_str_hash,
                                                                g_str_equal);
  connection->priv->map_id_to_signal_data = g_hash_table_new (g_direct_hash,
                                                              g_direct_equal);
  connection->priv->map_sender_to_signal_data_array = g_hash_table_new_full (g_str_hash,
                                                                             g_str_equal,
                                                                             g_free,
                                                                             NULL);

  connection->priv->map_object_path_to_eo = g_hash_table_new_full (g_str_hash,
                                                                   g_str_equal,
                                                                   NULL,
                                                                   (GDestroyNotify) exported_object_free);

  connection->priv->map_id_to_ei = g_hash_table_new (g_direct_hash,
                                                     g_direct_equal);

  connection->priv->map_object_path_to_es = g_hash_table_new_full (g_str_hash,
                                                                   g_str_equal,
                                                                   NULL,
                                                                   (GDestroyNotify) exported_subtree_free);

  connection->priv->map_id_to_es = g_hash_table_new (g_direct_hash,
                                                     g_direct_equal);
}

/**
 * e_dbus_connection_get_bus_type:
 * @connection: A #EDBusConnection.
 *
 * Gets the type of message bus connection, if any.
 *
 * This will never return #G_BUS_TYPE_STARTER. If
 * #G_BUS_TYPE_STARTER was passed to e_dbus_connection_bus_get()
 * then the return value will be either #G_BUS_TYPE_SESSION or
 * #G_BUS_TYPE_SYSTEM depending on what bus started the
 * process.
 *
 * Returns: Type type of the message bus the connection is for or
 * #G_BUS_TYPE_NONE if the connection is not to a message
 * bus.
 **/
EDBusType
e_dbus_connection_get_bus_type (EDBusConnection *connection)
{
  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), G_BUS_TYPE_NONE);

  return connection->priv->bus_type;
}

/**
 * e_dbus_connection_get_address:
 * @connection: A #EDBusConnection.
 *
 * Gets the address that @connection was constructed with.
 *
 * Returns: The address that @connection was constructed with or %NULL
 * if a message bus connection. Do not free this string, it is owned
 * by @connection.
 */
const gchar *
e_dbus_connection_get_address (EDBusConnection *connection)
{
  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);

  return connection->priv->address;
}

/**
 * e_dbus_connection_get_is_disconnected:
 * @connection: A #EDBusConnection.
 *
 * Gets whether a connection has been disconnected.
 *
 * Returns: %TRUE if the connection is open, %FALSE otherwise.
 **/
gboolean
e_dbus_connection_get_is_disconnected (EDBusConnection *connection)
{
  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), FALSE);

  return connection->priv->dbus_1_connection == NULL;
}

/**
 * e_dbus_connection_get_is_private:
 * @connection: A #EDBusConnection.
 *
 * Gets whether the connection is private.
 *
 * Returns: %TRUE if the connection is private, %FALSE otherwise.
 **/
gboolean
e_dbus_connection_get_is_private (EDBusConnection *connection)
{
  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), FALSE);

  return connection->priv->is_private;
}

/* ---------------------------------------------------------------------------------------------------- */

#define PRINT_MESSAGE(message)                          \
  do {                                                  \
    const gchar *message_type;                          \
    switch (dbus_message_get_type (message))            \
      {                                                 \
      case DBUS_MESSAGE_TYPE_METHOD_CALL:               \
        message_type = "method_call";                   \
        break;                                          \
      case DBUS_MESSAGE_TYPE_METHOD_RETURN:             \
        message_type = "method_return";                 \
        break;                                          \
      case DBUS_MESSAGE_TYPE_ERROR:                     \
        message_type = "error";                         \
        break;                                          \
      case DBUS_MESSAGE_TYPE_SIGNAL:                    \
        message_type = "signal";                        \
        break;                                          \
      case DBUS_MESSAGE_TYPE_INVALID:                   \
        message_type = "invalid";                       \
        break;                                          \
      default:                                          \
        message_type = "unknown";                       \
        break;                                          \
      }                                                 \
    g_print ("new message:\n"                           \
             " type:         %s\n"                      \
             " sender:       %s\n"                      \
             " destination:  %s\n"                      \
             " path:         %s\n"                      \
             " interface:    %s\n"                      \
             " member:       %s\n"                      \
             " signature:    %s\n",                     \
             message_type,                              \
             dbus_message_get_sender (message),         \
             dbus_message_get_destination (message),    \
             dbus_message_get_path (message),           \
             dbus_message_get_interface (message),      \
             dbus_message_get_member (message),         \
             dbus_message_get_signature (message));     \
  } while (FALSE)

static void
process_message (EDBusConnection *connection,
                 DBusMessage *message)
{
  DBusError dbus_error;

  //g_debug ("in filter_function for dbus_1_connection %p", connection);
  //PRINT_MESSAGE (message);

  dbus_error_init (&dbus_error);
  /* check if we are disconnected from the bus */
  if (dbus_message_is_signal (message,
                              DBUS_INTERFACE_LOCAL,
                              "Disconnected") &&
      dbus_message_get_sender (message) == NULL &&
      dbus_message_get_destination (message) == NULL &&
      g_strcmp0 (dbus_message_get_path (message), DBUS_PATH_LOCAL) == 0)
    {
      if (connection->priv->dbus_1_connection != NULL)
        {
          e_dbus_connection_set_dbus_1_connection (connection, NULL);

          g_object_notify (G_OBJECT (connection), "is-disconnected");
          g_signal_emit (connection, signals[DISCONNECTED_SIGNAL], 0);
        }
    }
  else
    {
      /* distribute to signal subscribers */
      distribute_signals (connection, message);
    }
}

static DBusHandlerResult
filter_function (DBusConnection *dbus_1_connection,
                 DBusMessage    *message,
                 void           *user_data)
{
  EDBusConnection *connection = E_DBUS_CONNECTION (user_data);

  //PRINT_MESSAGE (message);
  process_message (connection, message);

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* must be called with lock held */
static void
e_dbus_connection_set_dbus_1_connection (EDBusConnection *connection,
                                         DBusConnection  *dbus_1_connection)
{
  g_return_if_fail (G_IS_DBUS_CONNECTION (connection));

  if (connection->priv->dbus_1_connection != NULL)
    {
      dbus_connection_remove_filter (connection->priv->dbus_1_connection,
                                     filter_function,
                                     connection);
      _e_dbus_unintegrate_dbus_1_connection (connection->priv->dbus_1_connection);
      if (connection->priv->is_private)
        {
          dbus_connection_close (connection->priv->dbus_1_connection);
        }
      else
        {
          /* shared connections must not be closed */
        }
      dbus_connection_unref (connection->priv->dbus_1_connection);
    }

  if (dbus_1_connection != NULL)
    {
      connection->priv->dbus_1_connection = dbus_connection_ref (dbus_1_connection);
      _e_dbus_integrate_dbus_1_connection (connection->priv->dbus_1_connection, NULL);
      if (!dbus_connection_add_filter (connection->priv->dbus_1_connection,
                                       filter_function,
                                       connection,
                                       NULL))
        _e_dbus_oom ();
      dbus_connection_set_exit_on_disconnect (connection->priv->dbus_1_connection,
                                              connection->priv->exit_on_disconnect);
    }
  else
    {
      connection->priv->dbus_1_connection = NULL;
    }
}

/**
 * e_dbus_connection_disconnect:
 * @connection: A #EDBusConnection.
 *
 * Disconnects @connection. Note that this never causes the process to
 * exit, #EDBusConnection:exit-on-disconnect is only used if the other
 * end of the connection disconnects.
 *
 * If @connection is already disconnected, this method does nothing.
 */
void
e_dbus_connection_disconnect (EDBusConnection *connection)
{
  gboolean emit_signal;

  g_return_if_fail (G_IS_DBUS_CONNECTION (connection));

  G_LOCK (connection_lock);
  emit_signal = FALSE;
  if (connection->priv->dbus_1_connection != NULL)
    {
      e_dbus_connection_set_dbus_1_connection (connection, NULL);
      emit_signal = TRUE;
    }
  G_UNLOCK (connection_lock);

  if (emit_signal)
    g_signal_emit (connection, signals[DISCONNECTED_SIGNAL], 0);
}

/* ---------------------------------------------------------------------------------------------------- */

static GObject *
e_dbus_connection_constructor (GType                  type,
                               guint                  n_construct_properties,
                               GObjectConstructParam *construct_properties)
{
  EDBusConnection **singleton;
  gboolean is_private;
  GObject *object;
  guint n;

  object = NULL;
  singleton = NULL;
  is_private = FALSE;

  G_LOCK (connection_lock);

  for (n = 0; n < n_construct_properties; n++)
    {
      if (g_strcmp0 (construct_properties[n].pspec->name, "bus-type") == 0)
        {
          EDBusType bus_type;
          const gchar *starter_bus;

          bus_type = g_value_get_enum (construct_properties[n].value);
          switch (bus_type)
            {
            case G_BUS_TYPE_NONE:
              /* do nothing */
              break;

            case G_BUS_TYPE_SESSION:
              singleton = &the_session_bus;
              break;

            case G_BUS_TYPE_SYSTEM:
              singleton = &the_system_bus;
              break;

            case G_BUS_TYPE_STARTER:
              starter_bus = g_getenv ("DBUS_STARTER_BUS_TYPE");
              if (g_strcmp0 (starter_bus, "session") == 0)
                {
                  g_value_set_enum (construct_properties[n].value, G_BUS_TYPE_SESSION);
                  singleton = &the_session_bus;
                }
              else if (g_strcmp0 (starter_bus, "system") == 0)
                {
                  g_value_set_enum (construct_properties[n].value, G_BUS_TYPE_SYSTEM);
                  singleton = &the_system_bus;
                }
              else
                {
                  g_critical (_("Cannot construct a EDBusConnection object with bus_type G_BUS_TYPE_STARTER "
                                "because the DBUS_STARTER_BUS_TYPE environment variable is not set. "
                                "This is an error in the application or library using EDBus."));
                  goto out;
                }
              break;

            default:
              g_assert_not_reached ();
              break;
            }
        }
      else if (g_strcmp0 (construct_properties[n].pspec->name, "is-private") == 0)
        {
          is_private = g_value_get_boolean (construct_properties[n].value);
        }
    }

  if (is_private)
    singleton = NULL;

  if (singleton != NULL && *singleton != NULL)
    {
      object = g_object_ref (*singleton);
      goto out;
    }

  object = G_OBJECT_CLASS (e_dbus_connection_parent_class)->constructor (type,
                                                                         n_construct_properties,
                                                                         construct_properties);

  if (singleton != NULL)
    *singleton = E_DBUS_CONNECTION (object);

 out:
  G_UNLOCK (connection_lock);
  return object;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
initable_init (GInitable       *initable,
               GCancellable    *cancellable,
               GError         **error)
{
  EDBusConnection *connection = E_DBUS_CONNECTION (initable);
  DBusConnection *dbus_1_connection;
  DBusError dbus_error;
  gboolean ret;

  G_LOCK (connection_lock);

  ret = FALSE;

  if (connection->priv->is_initialized)
    {
      if (connection->priv->dbus_1_connection != NULL)
        {
          ret = TRUE;
        }
      else
        {
          g_assert (connection->priv->initialization_error != NULL);
          g_propagate_error (error, g_error_copy (connection->priv->initialization_error));
        }
      goto out;
    }

  g_assert (connection->priv->dbus_1_connection == NULL);
  g_assert (connection->priv->initialization_error == NULL);

  dbus_connection_set_change_sigpipe (TRUE);

  dbus_error_init (&dbus_error);
  if (connection->priv->address != NULL)
    {
      g_assert (connection->priv->bus_type == G_BUS_TYPE_NONE); /* API contract */

      if (connection->priv->is_private)
        {
          dbus_1_connection = dbus_connection_open_private (connection->priv->address,
                                                            &dbus_error);
        }
      else
        {
          dbus_1_connection = dbus_connection_open (connection->priv->address,
                                                    &dbus_error);
        }
    }
  else
    {
      g_assert (connection->priv->bus_type != G_BUS_TYPE_NONE); /* API contract */
      if (connection->priv->is_private)
        {
          dbus_1_connection = dbus_bus_get_private (connection->priv->bus_type,
                                                    &dbus_error);
        }
      else
        {
          dbus_1_connection = dbus_bus_get (connection->priv->bus_type,
                                            &dbus_error);
        }
    }

  if (dbus_1_connection != NULL)
    {
      e_dbus_connection_set_dbus_1_connection (connection, dbus_1_connection);
      dbus_connection_unref (dbus_1_connection);
      ret = TRUE;
    }
  else
    {
      e_dbus_error_set_dbus_error (&connection->priv->initialization_error,
                                   dbus_error.name,
                                   dbus_error.message,
                                   NULL);
      /* this is a locally generated error so strip the remote part */
      e_dbus_error_strip_remote_error (connection->priv->initialization_error);
      dbus_error_free (&dbus_error);
      g_propagate_error (error, g_error_copy (connection->priv->initialization_error));
    }

  connection->priv->is_initialized = TRUE;

 out:
  G_UNLOCK (connection_lock);
  return ret;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = initable_init;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
async_initable_init_async (GAsyncInitable     *initable,
                           gint                io_priority,
                           GCancellable       *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer            user_data)
{
  GSimpleAsyncResult *simple;
  GError *error;

  simple = g_simple_async_result_new (G_OBJECT (initable),
                                      callback,
                                      user_data,
                                      async_initable_init_async);

  /* for now, we just do this asynchronously and complete in idle since libdbus has no way
   * to do it asynchronously
   */
  error = NULL;
  if (!initable_init (G_INITABLE (initable),
                      cancellable,
                      &error))
    {
      g_simple_async_result_set_from_error (simple, error);
      g_error_free (error);
    }

  g_simple_async_result_complete_in_idle (simple);
  g_object_unref (simple);
}

static gboolean
async_initable_init_finish (GAsyncInitable  *initable,
                            GAsyncResult    *res,
                            GError         **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (res);

  g_warn_if_fail (g_simple_async_result_get_source_tag (simple) == async_initable_init_async);

  if (g_simple_async_result_propagate_error (simple, error))
    return FALSE;
  return TRUE;
}

static void
async_initable_iface_init (GAsyncInitableIface *async_initable_iface)
{
  async_initable_iface->init_async = async_initable_init_async;
  async_initable_iface->init_finish = async_initable_init_finish;
}

/* ---------------------------------------------------------------------------------------------------- */

EDBusConnection *
_e_dbus_connection_new_for_dbus_1_connection (DBusConnection  *dbus_1_connection)
{
  EDBusConnection *connection;

  connection = E_DBUS_CONNECTION (g_object_new (E_TYPE_DBUS_CONNECTION,
                                                "is-private", TRUE,
                                                "exit-on-disconnect", FALSE,
                                                NULL));

  /* TODO: set address? */

  e_dbus_connection_set_dbus_1_connection (connection, dbus_connection_ref (dbus_1_connection));
  connection->priv->is_initialized = TRUE;

  return connection;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * e_dbus_connection_bus_get_sync:
 * @bus_type: A #EDBusType.
 * @cancellable: A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously connects to the message bus specified by @bus_type.
 * Note that the returned object may shared with other callers,
 * e.g. if two separate parts of a process calls this function with
 * the same @bus_type, they will share the same object.
 *
 * Use e_dbus_connection_bus_get_private_sync() to get a private
 * connection.
 *
 * This is a synchronous failable constructor. See
 * e_dbus_connection_bus_get() and e_dbus_connection_bus_get_finish()
 * for the asynchronous version.
 *
 * Returns: A #EDBusConnection or %NULL if @error is set. Free with g_object_unref().
 **/
EDBusConnection *
e_dbus_connection_bus_get_sync (EDBusType            bus_type,
                                GCancellable       *cancellable,
                                GError            **error)
{
  GInitable *initable;

  initable = g_initable_new (E_TYPE_DBUS_CONNECTION,
                             cancellable,
                             error,
                             "bus-type", bus_type,
                             NULL);

  if (initable != NULL)
    return E_DBUS_CONNECTION (initable);
  else
    return NULL;
}

/**
 * e_dbus_connection_bus_get:
 * @bus_type: A #EDBusType.
 * @cancellable: A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied.
 * @user_data: The data to pass to @callback.
 *
 * Asynchronously connects to the message bus specified by @bus_type.
 *
 * When the operation is finished, @callback will be invoked. You can
 * then call e_dbus_connection_bus_get_finish() to get the result of
 * the operation.
 *
 * Use e_dbus_connection_bus_get_private() to get a private
 * connection.
 *
 * This is a asynchronous failable constructor. See
 * e_dbus_connection_bus_get_sync() for the synchronous version.
 **/
void
e_dbus_connection_bus_get (EDBusType             bus_type,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  g_async_initable_new_async (E_TYPE_DBUS_CONNECTION,
                              G_PRIORITY_DEFAULT,
                              cancellable,
                              callback,
                              user_data,
                              "bus-type", bus_type,
                              NULL);
}

/**
 * e_dbus_connection_bus_get_finish:
 * @res: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to e_dbus_connection_bus_get().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with e_dbus_connection_bus_get().
 *
 * Note that the returned object may shared with other callers,
 * e.g. if two separate parts of a process calls this function with
 * the same @bus_type, they will share the same object.
 *
 * Returns: A #EDBusConnection or %NULL if @error is set. Free with g_object_unref().
 **/
EDBusConnection *
e_dbus_connection_bus_get_finish (GAsyncResult  *res,
                                  GError       **error)
{
  GObject *object;
  GObject *source_object;

  source_object = g_async_result_get_source_object (res);
  g_assert (source_object != NULL);

  object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object),
                                        res,
                                        error);
  g_object_unref (source_object);

  if (object != NULL)
    return E_DBUS_CONNECTION (object);
  else
    return NULL;
}


/* ---------------------------------------------------------------------------------------------------- */

/**
 * e_dbus_connection_bus_get_private_sync:
 * @bus_type: A #EDBusType.
 * @cancellable: A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Like e_dbus_connection_bus_get_sync() but gets a connection that is not
 * shared with other callers.
 *
 * Returns: A #EDBusConnection. Free with g_object_unref().
 **/
EDBusConnection *
e_dbus_connection_bus_get_private_sync (EDBusType        bus_type,
                                        GCancellable   *cancellable,
                                        GError        **error)
{
  GInitable *initable;

  initable = g_initable_new (E_TYPE_DBUS_CONNECTION,
                             cancellable,
                             error,
                             "bus-type", bus_type,
                             "is-private", TRUE,
                             NULL);

  if (initable != NULL)
    return E_DBUS_CONNECTION (initable);
  else
    return NULL;
}

/**
 * e_dbus_connection_bus_get_private:
 * @bus_type: A #EDBusType.
 * @cancellable: A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied.
 * @user_data: The data to pass to @callback.
 *
 * Asynchronously connects to the message bus specified by @bus_type
 * using a private connection.
 *
 * When the operation is finished, @callback will be invoked. You can
 * then call e_dbus_connection_bus_get_finish() to get the result of
 * the operation.
 *
 * Use e_dbus_connection_bus_get() to get a shared connection.
 *
 * This is a asynchronous failable constructor. See
 * e_dbus_connection_bus_get_private_sync() for the synchronous
 * version.
 **/
void
e_dbus_connection_bus_get_private (EDBusType             bus_type,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
  g_async_initable_new_async (E_TYPE_DBUS_CONNECTION,
                              G_PRIORITY_DEFAULT,
                              cancellable,
                              callback,
                              user_data,
                              "bus-type", bus_type,
                              "is-private", TRUE,
                              NULL);
}

/**
 * e_dbus_connection_bus_get_private_finish:
 * @res: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to e_dbus_connection_bus_get_private().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with e_dbus_connection_bus_get_private().
 *
 * The returned object is never shared with other callers.
 *
 * Returns: A #EDBusConnection or %NULL if @error is set. Free with g_object_unref().
 **/
EDBusConnection *
e_dbus_connection_bus_get_private_finish (GAsyncResult  *res,
                                          GError       **error)
{
  GObject *object;
  GObject *source_object;

  source_object = g_async_result_get_source_object (res);
  g_assert (source_object != NULL);

  object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object),
                                        res,
                                        error);
  g_object_unref (source_object);

  if (object != NULL)
    return E_DBUS_CONNECTION (object);
  else
    return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * e_dbus_connection_new:
 * @address: The address to connect to. See the D-Bus specification for details on address formats.
 * @cancellable: A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied.
 * @user_data: The data to pass to @callback.
 *
 * Asynchronously creates a private connection to @address.
 *
 * When the operation is finished, @callback will be invoked. You can
 * then call e_dbus_connection_new_finish() to get the result of the
 * operation.
 *
 * This is a asynchronous failable constructor. See
 * e_dbus_connection_new_sync() for the synchronous
 * version.
 */
void
e_dbus_connection_new (const gchar         *address,
                       GCancellable        *cancellable,
                       GAsyncReadyCallback  callback,
                       gpointer             user_data)
{
  g_async_initable_new_async (E_TYPE_DBUS_CONNECTION,
                              G_PRIORITY_DEFAULT,
                              cancellable,
                              callback,
                              user_data,
                              "address", address,
                              "is-private", TRUE,
                              "exit-on-disconnect", FALSE,
                              NULL);
}

/**
 * e_dbus_connection_new_finish:
 * @res: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to e_dbus_connection_new().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with e_dbus_connection_new().
 *
 * The returned #EDBusConnection object will have
 * #EDBusConnection:exit-on-disconnect set to %FALSE.
 *
 * Returns: A #EDBusConnection or %NULL if @error is set. Free with g_object_unref().
 */
EDBusConnection *
e_dbus_connection_new_finish (GAsyncResult        *res,
                              GError             **error)
{
  GObject *object;
  GObject *source_object;

  source_object = g_async_result_get_source_object (res);
  g_assert (source_object != NULL);

  object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object),
                                        res,
                                        error);
  g_object_unref (source_object);

  if (object != NULL)
    return E_DBUS_CONNECTION (object);
  else
    return NULL;
}

/**
 * e_dbus_connection_new_sync:
 * @address: The address to connect to. See the D-Bus specification for details on address formats.
 * @cancellable: A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously creates a private connection to @address.
 *
 * This is a synchronous failable constructor. See
 * e_dbus_connection_new() for the asynchronous version.
 *
 * The returned #EDBusConnection object will have
 * #EDBusConnection:exit-on-disconnect set to %FALSE.
 *
 * Returns: A #EDBusConnection or %NULL if @error is set. Free with g_object_unref().
 */
EDBusConnection *
e_dbus_connection_new_sync (const gchar         *address,
                            GCancellable       *cancellable,
                            GError            **error)
{
  GInitable *initable;

  initable = g_initable_new (E_TYPE_DBUS_CONNECTION,
                             cancellable,
                             error,
                             "address", address,
                             "is-private", TRUE,
                             "exit-on-disconnect", FALSE,
                             NULL);

  if (initable != NULL)
    return E_DBUS_CONNECTION (initable);
  else
    return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * e_dbus_connection_set_exit_on_disconnect:
 * @connection: A #EDBusConnection.
 * @exit_on_disconnect: Whether _exit() should be called when @connection is
 * disconnected by the other end.
 *
 * Sets whether _exit() should be called when @connection is
 * disconnected by the other end.
 **/
void
e_dbus_connection_set_exit_on_disconnect (EDBusConnection *connection,
                                          gboolean         exit_on_disconnect)
{
  g_return_if_fail (G_IS_DBUS_CONNECTION (connection));

  connection->priv->exit_on_disconnect = exit_on_disconnect;
  if (connection->priv->dbus_1_connection != NULL)
    dbus_connection_set_exit_on_disconnect (connection->priv->dbus_1_connection,
                                            connection->priv->exit_on_disconnect);
}

/**
 * e_dbus_connection_get_unique_name:
 * @connection: A #EDBusConnection.
 *
 * Gets the unique name of @connection as assigned by the message bus.
 *
 * Returns: The unique name or %NULL if the connection is disconnected
 * or @connection is not a message bus connection. Do not free this
 * string, it is owned by @connection.
 **/
const gchar *
e_dbus_connection_get_unique_name (EDBusConnection *connection)
{
  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);

  if (connection->priv->bus_type == G_BUS_TYPE_NONE)
    return NULL;

  if (connection->priv->dbus_1_connection != NULL)
    return dbus_bus_get_unique_name (connection->priv->dbus_1_connection);
  else
    return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

void
_e_dbus_connection_send_dbus_1_message (EDBusConnection    *connection,
                                        DBusMessage        *message)
{
  g_return_if_fail (G_IS_DBUS_CONNECTION (connection));
  g_return_if_fail (message != NULL);

  if (connection->priv->dbus_1_connection == NULL)
    goto out;

  if (!dbus_connection_send (connection->priv->dbus_1_connection,
                             message,
                             NULL))
    _e_dbus_oom ();

 out:
  ;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
send_dbus_1_message_with_reply_cb (DBusPendingCall *pending_call,
                                   void            *user_data)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);
  EDBusConnection *connection;
  GCancellable *cancellable;
  gulong cancellable_handler_id;
  DBusMessage *reply;

  G_LOCK (connection_lock);
  cancellable_handler_id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (simple), "cancellable-handler-id"));
  connection = E_DBUS_CONNECTION (g_async_result_get_source_object (G_ASYNC_RESULT (simple)));
  G_UNLOCK (connection_lock);

  cancellable = g_object_get_data (G_OBJECT (simple), "cancellable");

  if (cancellable_handler_id > 0)
    g_cancellable_disconnect (cancellable, cancellable_handler_id);

  if (pending_call == NULL)
    {
      g_simple_async_result_set_error (simple,
                                       E_DBUS_ERROR,
                                       E_DBUS_ERROR_CANCELLED,
                                       _("Operation was cancelled"));
    }
  else
    {
      reply = dbus_pending_call_steal_reply (pending_call);
      g_assert (reply != NULL);
      g_simple_async_result_set_op_res_gpointer (simple, reply, (GDestroyNotify) dbus_message_unref);
    }
  g_simple_async_result_complete_in_idle (simple);
  g_object_unref (connection);
  g_object_unref (simple);
}

static gboolean
send_dbus_1_message_with_reply_cancelled_in_idle (gpointer user_data)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);
  send_dbus_1_message_with_reply_cb (NULL, simple);
  return FALSE;
}

static void
send_dbus_1_message_with_reply_cancelled_cb (GCancellable *cancellable,
                                             gpointer      user_data)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);
  DBusPendingCall *pending_call;

  pending_call = g_object_get_data (G_OBJECT (simple), "dbus-1-pending-call");
  dbus_pending_call_cancel (pending_call);

  g_idle_add (send_dbus_1_message_with_reply_cancelled_in_idle, simple);
}

static void
e_dbus_connection_send_dbus_1_message_with_reply (EDBusConnection    *connection,
                                                  DBusMessage        *message,
                                                  gint                timeout_msec,
                                                  GCancellable       *cancellable,
                                                  GAsyncReadyCallback callback,
                                                  gpointer            user_data)
{
  GSimpleAsyncResult *simple;
  DBusPendingCall *pending_call;
  gulong cancellable_handler_id;

  g_return_if_fail (G_IS_DBUS_CONNECTION (connection));
  g_return_if_fail (callback != NULL);
  g_return_if_fail (message != NULL);
  g_return_if_fail (dbus_message_get_type (message) == DBUS_MESSAGE_TYPE_METHOD_CALL);

  G_LOCK (connection_lock);

  simple = g_simple_async_result_new (G_OBJECT (connection),
                                      callback,
                                      user_data,
                                      e_dbus_connection_send_dbus_1_message_with_reply);

  /* don't even send a message if already cancelled */
  if (g_cancellable_is_cancelled (cancellable))
    {
      g_simple_async_result_set_error (simple,
                                       E_DBUS_ERROR,
                                       E_DBUS_ERROR_CANCELLED,
                                       _("Operation was cancelled"));
      g_simple_async_result_complete_in_idle (simple);
      g_object_unref (simple);
      goto out;
    }

  if (connection->priv->dbus_1_connection == NULL)
    {
      g_simple_async_result_set_error (simple,
                                       E_DBUS_ERROR,
                                       E_DBUS_ERROR_DISCONNECTED,
                                       _("Not connected"));
      g_simple_async_result_complete_in_idle (simple);
      g_object_unref (simple);
      goto out;
    }

  if (!dbus_connection_send_with_reply (connection->priv->dbus_1_connection,
                                        message,
                                        &pending_call,
                                        timeout_msec))
    _e_dbus_oom ();

  if (pending_call == NULL)
    {
      g_simple_async_result_set_error (simple,
                                       E_DBUS_ERROR,
                                       E_DBUS_ERROR_FAILED,
                                       _("Not connected"));
      g_simple_async_result_complete_in_idle (simple);
      g_object_unref (simple);
      goto out;
    }

  g_object_set_data_full (G_OBJECT (simple),
                          "dbus-1-pending-call",
                          pending_call,
                          (GDestroyNotify) dbus_pending_call_unref);

  g_object_set_data (G_OBJECT (simple),
                     "cancellable",
                     cancellable);

  dbus_pending_call_set_notify (pending_call,
                                send_dbus_1_message_with_reply_cb,
                                simple,
                                NULL);

  cancellable_handler_id = 0;
  if (cancellable != NULL)
    {
      /* use the lock to ensure cancellable-handler-id is set on simple before trying to get it
       * in send_dbus_1_message_with_reply_cb()
       */
      cancellable_handler_id = g_cancellable_connect (cancellable,
                                                      G_CALLBACK (send_dbus_1_message_with_reply_cancelled_cb),
                                                      simple,
                                                      NULL);
      g_object_set_data (G_OBJECT (simple),
                         "cancellable-handler-id",
                         GUINT_TO_POINTER (cancellable_handler_id));
    }

 out:
  G_UNLOCK (connection_lock);
}

static DBusMessage *
e_dbus_connection_send_dbus_1_message_with_reply_finish (EDBusConnection   *connection,
                                                         GAsyncResult      *res,
                                                         GError           **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (res);
  DBusMessage *reply;

  g_warn_if_fail (g_simple_async_result_get_source_tag (simple) == e_dbus_connection_send_dbus_1_message_with_reply);

  reply = NULL;
  if (g_simple_async_result_propagate_error (simple, error))
    goto out;

  reply = dbus_message_ref (g_simple_async_result_get_op_res_gpointer (simple));

 out:
  return reply;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
send_dbus_1_message_with_reply_sync_cancelled_cb (GCancellable *cancellable,
                                                  gpointer      user_data)
{
  DBusPendingCall *pending_call = user_data;

  dbus_pending_call_cancel (pending_call);
}

static DBusMessage *
e_dbus_connection_send_dbus_1_message_with_reply_sync (EDBusConnection    *connection,
                                                       DBusMessage        *message,
                                                       gint                timeout_msec,
                                                       GCancellable       *cancellable,
                                                       GError            **error)
{
  gulong cancellable_handler_id;
  DBusMessage *result;
  DBusPendingCall *pending_call;

  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), 0);
  g_return_val_if_fail (message != NULL, 0);
  g_return_val_if_fail (dbus_message_get_type (message) == DBUS_MESSAGE_TYPE_METHOD_CALL, 0);

  result = NULL;

  G_LOCK (connection_lock);

  /* don't even send a message if already cancelled */
  if (g_cancellable_is_cancelled (cancellable))
    {
      g_set_error (error,
                   E_DBUS_ERROR,
                   E_DBUS_ERROR_CANCELLED,
                   _("Operation was cancelled"));
      G_UNLOCK (connection_lock);
      goto out;
    }

  if (connection->priv->dbus_1_connection == NULL)
    {
      g_set_error (error,
                   E_DBUS_ERROR,
                   E_DBUS_ERROR_FAILED,
                   _("Not connected"));
      G_UNLOCK (connection_lock);
      goto out;
    }

  if (!dbus_connection_send_with_reply (connection->priv->dbus_1_connection,
                                        message,
                                        &pending_call,
                                        timeout_msec))
    _e_dbus_oom ();

  if (pending_call == NULL)
    {
      g_set_error (error,
                   E_DBUS_ERROR,
                   E_DBUS_ERROR_FAILED,
                   _("Not connected"));
      G_UNLOCK (connection_lock);
      goto out;
    }

  cancellable_handler_id = 0;
  if (cancellable != NULL)
    {
      /* use the lock to ensure cancellable-handler-id is set on simple before trying to get it
       * in send_dbus_1_message_with_reply_cb()
       */
      cancellable_handler_id = g_cancellable_connect (cancellable,
                                                      G_CALLBACK (send_dbus_1_message_with_reply_sync_cancelled_cb),
                                                      pending_call,
                                                      NULL);
    }

  G_UNLOCK (connection_lock);

  /* block without holding the lock */
  dbus_pending_call_block (pending_call);

  if (cancellable_handler_id > 0)
    {
      g_cancellable_disconnect (cancellable,
                                cancellable_handler_id);
    }

  result = dbus_pending_call_steal_reply (pending_call);
  if (pending_call == NULL)
    {
      g_set_error (error,
                   E_DBUS_ERROR,
                   E_DBUS_ERROR_CANCELLED,
                   _("Operation was cancelled"));
    }

  dbus_pending_call_unref (pending_call);

 out:
  return result;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  gchar *rule;
  gchar *sender;
  gchar *interface_name;
  gchar *member;
  gchar *object_path;
  gchar *arg0;
  GArray *subscribers;
} SignalData;

typedef struct
{
  EDBusSignalCallback callback;
  gpointer user_data;
  GDestroyNotify user_data_free_func;
  guint id;
  GMainContext *context;
} SignalSubscriber;

static void
signal_data_free (SignalData *data)
{
  g_free (data->rule);
  g_free (data->sender);
  g_free (data->interface_name);
  g_free (data->member);
  g_free (data->object_path);
  g_free (data->arg0);
  g_array_free (data->subscribers, TRUE);
  g_free (data);
}

static gchar *
args_to_rule (const gchar         *sender,
              const gchar         *interface_name,
              const gchar         *member,
              const gchar         *object_path,
              const gchar         *arg0)
{
  GString *rule;

  rule = g_string_new ("type='signal'");
  if (sender != NULL)
    g_string_append_printf (rule, ",sender='%s'", sender);
  if (interface_name != NULL)
    g_string_append_printf (rule, ",interface='%s'", interface_name);
  if (member != NULL)
    g_string_append_printf (rule, ",member='%s'", member);
  if (object_path != NULL)
    g_string_append_printf (rule, ",path='%s'", object_path);
  if (arg0 != NULL)
    g_string_append_printf (rule, ",arg0='%s'", arg0);

  return g_string_free (rule, FALSE);
}

static guint _global_subscriber_id = 1;
static guint _global_registration_id = 1;
static guint _global_subtree_registration_id = 1;

/* ---------------------------------------------------------------------------------------------------- */

static void
add_match_cb (DBusPendingCall *pending_call,
              void            *user_data)
{
  DBusMessage *reply;
  DBusError dbus_error;

  reply = dbus_pending_call_steal_reply (pending_call);
  g_assert (reply != NULL);

  dbus_error_init (&dbus_error);
  if (dbus_set_error_from_message (&dbus_error, reply))
    {
      if (g_strcmp0 (dbus_error.name, "org.freedesktop.DBus.Error.OOM") == 0)
        {
          g_critical ("Message bus reported OOM when trying to add match rule: %s: %s",
                      dbus_error.name,
                      dbus_error.message);
          _e_dbus_oom ();
        }

      /* Don't report other errors; the bus might have gone away while sending the message
       * so @dbus_error might be a locally generated error.
       */

      dbus_error_free (&dbus_error);
    }
}

static void
add_match_rule (EDBusConnection *connection,
                const gchar     *match_rule)
{
  DBusMessage *message;
  DBusPendingCall *pending_call;

  if ((message = dbus_message_new_method_call (DBUS_SERVICE_DBUS,
                                               DBUS_PATH_DBUS,
                                               DBUS_INTERFACE_DBUS,
                                               "AddMatch")) == NULL)
    _e_dbus_oom ();
  if (!dbus_message_append_args (message,
                                 DBUS_TYPE_STRING, &match_rule,
                                 DBUS_TYPE_INVALID))
    _e_dbus_oom ();

  /* don't use e_dbus_connection_send_dbus_1_message_with_reply() since we don't want to ref @connection */
  if (!dbus_connection_send_with_reply (connection->priv->dbus_1_connection,
                                        message,
                                        &pending_call,
                                        -1))
    _e_dbus_oom ();

  dbus_pending_call_set_notify (pending_call,
                                add_match_cb,
                                NULL,
                                NULL);

  dbus_message_unref (message);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
remove_match_cb (DBusPendingCall *pending_call,
                 void            *user_data)
{
  DBusMessage *reply;
  DBusError dbus_error;

  reply = dbus_pending_call_steal_reply (pending_call);
  g_assert (reply != NULL);

  dbus_error_init (&dbus_error);
  if (dbus_set_error_from_message (&dbus_error, reply))
    {
      if (g_strcmp0 (dbus_error.name, "org.freedesktop.DBus.Error.MatchRuleNotFound") == 0)
        {
          g_warning ("Message bus reported error removing match rule: %s: %s\n"
                     "This is a bug in EDBus.",
                     dbus_error.name,
                     dbus_error.message);
        }
      dbus_error_free (&dbus_error);
    }
}

static void
remove_match_rule (EDBusConnection *connection,
                   const gchar     *match_rule)
{
  DBusMessage *message;
  DBusPendingCall *pending_call;

  if ((message = dbus_message_new_method_call (DBUS_SERVICE_DBUS,
                                               DBUS_PATH_DBUS,
                                               DBUS_INTERFACE_DBUS,
                                               "RemoveMatch")) == NULL)
    _e_dbus_oom ();
  if (!dbus_message_append_args (message,
                                 DBUS_TYPE_STRING, &match_rule,
                                 DBUS_TYPE_INVALID))
    _e_dbus_oom ();

  /* don't use e_dbus_connection_send_dbus_1_message_with_reply() since we don't want to ref @connection */
  if (!dbus_connection_send_with_reply (connection->priv->dbus_1_connection,
                                        message,
                                        &pending_call,
                                        -1))
    _e_dbus_oom ();

  dbus_pending_call_set_notify (pending_call,
                                remove_match_cb,
                                NULL,
                                NULL);

  dbus_message_unref (message);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
is_signal_data_for_name_lost_or_acquired (SignalData *signal_data)
{
  return g_strcmp0 (signal_data->sender, DBUS_SERVICE_DBUS) == 0 &&
    g_strcmp0 (signal_data->interface_name, DBUS_INTERFACE_DBUS) == 0 &&
    g_strcmp0 (signal_data->object_path, DBUS_PATH_DBUS) == 0 &&
    (g_strcmp0 (signal_data->member, "NameLost") == 0 ||
     g_strcmp0 (signal_data->member, "NameAcquired") == 0);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * e_dbus_connection_signal_subscribe:
 * @connection: A #EDBusConnection.
 * @sender: Sender name to match on. Must be either <literal>org.freedesktop.DBus</literal> (for listening to signals from the message bus daemon) or a unique name or %NULL to listen from all senders.
 * @interface_name: D-Bus interface name to match on or %NULL to match on all interfaces.
 * @member: D-Bus signal name to match on or %NULL to match on all signals.
 * @object_path: Object path to match on or %NULL to match on all object paths.
 * @arg0: Contents of first string argument to match on or %NULL to match on all kinds of arguments.
 * @callback: Callback to invoke when there is a signal matching the requested data.
 * @user_data: User data to pass to @callback.
 * @user_data_free_func: Function to free @user_data when subscription is removed or %NULL.
 *
 * Subscribes to signals on @connection and invokes @callback with a
 * whenever the signal is received. Note that @callback
 * will be invoked in the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> of the thread you are calling this method from.
 *
 * It is considered a programming error to use this function if @connection has been disconnected.
 *
 * Note that if @sender is not <literal>org.freedesktop.DBus</literal> (for listening to signals from the
 * message bus daemon), then it needs to be a unique bus name or %NULL (for listening to signals from any
 * name) - you cannot pass a name like <literal>com.example.MyApp</literal>.
 * Use e.g. e_bus_watch_name() to find the unique name for the owner of the name you are interested in. Also note
 * that this function does not remove a subscription if @sender vanishes from the bus. You have to manually
 * call e_dbus_connection_signal_unsubscribe() to remove a subscription.
 *
 * Returns: A subscription identifier that can be used with e_dbus_connection_signal_unsubscribe().
 **/
guint
e_dbus_connection_signal_subscribe (EDBusConnection     *connection,
                                    const gchar         *sender,
                                    const gchar         *interface_name,
                                    const gchar         *member,
                                    const gchar         *object_path,
                                    const gchar         *arg0,
                                    EDBusSignalCallback  callback,
                                    gpointer             user_data,
                                    GDestroyNotify       user_data_free_func)
{
  gchar *rule;
  SignalData *signal_data;
  SignalSubscriber subscriber;
  GPtrArray *signal_data_array;

  /* Right now we abort if AddMatch() fails since it can only fail with the bus being in
   * an OOM condition. We might want to change that but that would involve making
   * e_dbus_connection_signal_subscribe() asynchronous and having the call sites
   * handle that. And there's really no sensible way of handling this short of retrying
   * to add the match rule... and then there's the little thing that, hey, maybe there's
   * a reason the bus in an OOM condition.
   *
   * Doable, but not really sure it's worth it...
   */

  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), 0);
  g_return_val_if_fail (!e_dbus_connection_get_is_disconnected (connection), 0);
  g_return_val_if_fail (sender == NULL || ((strcmp (sender, DBUS_SERVICE_DBUS) == 0 || sender[0] == ':') &&
                                           connection->priv->bus_type != G_BUS_TYPE_NONE), 0);
  g_return_val_if_fail (callback != NULL, 0);
  /* TODO: check that passed in data is well-formed */

  G_LOCK (connection_lock);

  rule = args_to_rule (sender, interface_name, member, object_path, arg0);

  if (sender == NULL)
    sender = "";

  subscriber.callback = callback;
  subscriber.user_data = user_data;
  subscriber.user_data_free_func = user_data_free_func;
  subscriber.id = _global_subscriber_id++; /* TODO: overflow etc. */
  subscriber.context = g_main_context_get_thread_default ();
  if (subscriber.context != NULL)
    g_main_context_ref (subscriber.context);

  /* see if we've already have this rule */
  signal_data = g_hash_table_lookup (connection->priv->map_rule_to_signal_data, rule);
  if (signal_data != NULL)
    {
      g_array_append_val (signal_data->subscribers, subscriber);
      g_free (rule);
      goto out;
    }

  signal_data = g_new0 (SignalData, 1);
  signal_data->rule           = rule;
  signal_data->sender         = g_strdup (sender);
  signal_data->interface_name = g_strdup (interface_name);
  signal_data->member         = g_strdup (member);
  signal_data->object_path    = g_strdup (object_path);
  signal_data->arg0           = g_strdup (arg0);
  signal_data->subscribers    = g_array_new (FALSE, FALSE, sizeof (SignalSubscriber));
  g_array_append_val (signal_data->subscribers, subscriber);

  g_hash_table_insert (connection->priv->map_rule_to_signal_data,
                       signal_data->rule,
                       signal_data);

  /* Add the match rule to the bus...
   *
   * Avoid adding match rules for NameLost and NameAcquired messages - the bus will
   * always send such messages to to us.
   */
  if (connection->priv->bus_type != G_BUS_TYPE_NONE)
    {
      if (!is_signal_data_for_name_lost_or_acquired (signal_data))
        {
          add_match_rule (connection, signal_data->rule);
        }
    }

 out:
  g_hash_table_insert (connection->priv->map_id_to_signal_data,
                       GUINT_TO_POINTER (subscriber.id),
                       signal_data);

  signal_data_array = g_hash_table_lookup (connection->priv->map_sender_to_signal_data_array,
                                           signal_data->sender);
  if (signal_data_array == NULL)
    {
      signal_data_array = g_ptr_array_new ();
      g_hash_table_insert (connection->priv->map_sender_to_signal_data_array,
                           g_strdup (signal_data->sender),
                           signal_data_array);
    }
  g_ptr_array_add (signal_data_array, signal_data);

  G_UNLOCK (connection_lock);

  return subscriber.id;
}

/* ---------------------------------------------------------------------------------------------------- */

/* must hold lock when calling this */
static void
unsubscribe_id_internal (EDBusConnection    *connection,
                         guint               subscription_id,
                         GArray             *out_removed_subscribers)
{
  SignalData *signal_data;
  GPtrArray *signal_data_array;
  guint n;

  signal_data = g_hash_table_lookup (connection->priv->map_id_to_signal_data,
                                     GUINT_TO_POINTER (subscription_id));
  if (signal_data == NULL)
    {
      /* Don't warn here, we may have thrown all subscriptions out when the connection was closed */
      goto out;
    }

  for (n = 0; n < signal_data->subscribers->len; n++)
    {
      SignalSubscriber *subscriber;

      subscriber = &(g_array_index (signal_data->subscribers, SignalSubscriber, n));
      if (subscriber->id != subscription_id)
        continue;

      g_assert (g_hash_table_remove (connection->priv->map_id_to_signal_data,
                                     GUINT_TO_POINTER (subscription_id)));
      g_array_append_val (out_removed_subscribers, *subscriber);
      g_array_remove_index (signal_data->subscribers, n);

      if (signal_data->subscribers->len == 0)
        g_assert (g_hash_table_remove (connection->priv->map_rule_to_signal_data, signal_data->rule));

      signal_data_array = g_hash_table_lookup (connection->priv->map_sender_to_signal_data_array,
                                               signal_data->sender);
      g_assert (signal_data_array != NULL);
      g_assert (g_ptr_array_remove (signal_data_array, signal_data));

      if (signal_data_array->len == 0)
        {
          g_assert (g_hash_table_remove (connection->priv->map_sender_to_signal_data_array, signal_data->sender));

          /* remove the match rule from the bus unless NameLost or NameAcquired (see subscribe()) */
          if (connection->priv->bus_type != G_BUS_TYPE_NONE)
            {
              if (!is_signal_data_for_name_lost_or_acquired (signal_data))
                {
                  if (connection->priv->dbus_1_connection != NULL)
                    {
                      remove_match_rule (connection, signal_data->rule);
                    }
                }
            }

          signal_data_free (signal_data);
        }

      goto out;
    }

  g_assert_not_reached ();

 out:
  ;
}

/**
 * e_dbus_connection_signal_unsubscribe:
 * @connection: A #EDBusConnection.
 * @subscription_id: A subscription id obtained from e_dbus_connection_signal_subscribe().
 *
 * Unsubscribes from signals.
 **/
void
e_dbus_connection_signal_unsubscribe (EDBusConnection    *connection,
                                      guint               subscription_id)
{
  GArray *subscribers;
  guint n;

  subscribers = g_array_new (FALSE, FALSE, sizeof (SignalSubscriber));

  G_LOCK (connection_lock);
  unsubscribe_id_internal (connection,
                           subscription_id,
                           subscribers);
  G_UNLOCK (connection_lock);

  /* invariant */
  g_assert (subscribers->len == 0 || subscribers->len == 1);

  /* call GDestroyNotify without lock held */
  for (n = 0; n < subscribers->len; n++)
    {
      SignalSubscriber *subscriber;
      subscriber = &(g_array_index (subscribers, SignalSubscriber, n));
      if (subscriber->user_data_free_func != NULL)
        subscriber->user_data_free_func (subscriber->user_data);
      if (subscriber->context != NULL)
        g_main_context_unref (subscriber->context);
    }

  g_array_free (subscribers, TRUE);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  EDBusSignalCallback  callback;
  gpointer             user_data;
  DBusMessage         *message;
  EDBusConnection     *connection;
} SignalInstance;

static gboolean
emit_signal_instance_in_idle_cb (gpointer data)
{
  SignalInstance *signal_instance = data;

  EVariant *parameters;
  GError *error;

  error = NULL;
  parameters = _e_dbus_dbus_1_to_gvariant (signal_instance->message, &error);
  if (parameters == NULL)
    {
      g_warning ("Error converting signal parameters to a EVariant: %s", error->message);
      g_error_free (error);
      goto out;
    }

  signal_instance->callback (signal_instance->connection,
                             dbus_message_get_sender (signal_instance->message),
                             dbus_message_get_path (signal_instance->message),
                             dbus_message_get_interface (signal_instance->message),
                             dbus_message_get_member (signal_instance->message),
                             parameters,
                             signal_instance->user_data);
 out:
  e_variant_unref (parameters);

  return FALSE;
}

static void
signal_instance_free (SignalInstance *signal_instance)
{
  dbus_message_unref (signal_instance->message);
  g_object_unref (signal_instance->connection);
  g_free (signal_instance);
}

static void
schedule_callbacks (EDBusConnection *connection,
                    GPtrArray       *signal_data_array,
                    DBusMessage     *message)
{
  guint n, m;

  /* TODO: if this is slow, then we can change signal_data_array into
   *       map_object_path_to_signal_data_array or something.
   */
  for (n = 0; n < signal_data_array->len; n++)
    {
      SignalData *signal_data = signal_data_array->pdata[n];
      const gchar *arg0;

      if (signal_data->interface_name != NULL &&
          g_strcmp0 (signal_data->interface_name, dbus_message_get_interface (message)) != 0)
        continue;

      if (signal_data->member != NULL &&
          g_strcmp0 (signal_data->member, dbus_message_get_member (message)) != 0)
        continue;

      if (signal_data->object_path != NULL &&
          g_strcmp0 (signal_data->object_path, dbus_message_get_path (message)) != 0)
        continue;

      if (signal_data->arg0 != NULL)
        {
          if (!dbus_message_get_args (message,
                                      NULL,
                                      DBUS_TYPE_STRING, &arg0,
                                      DBUS_TYPE_INVALID))
            continue;

          if (g_strcmp0 (signal_data->arg0, arg0) != 0)
            continue;
        }

      for (m = 0; m < signal_data->subscribers->len; m++)
        {
          SignalSubscriber *subscriber;
          GSource *idle_source;
          SignalInstance *signal_instance;

          subscriber = &(g_array_index (signal_data->subscribers, SignalSubscriber, m));

          signal_instance = g_new0 (SignalInstance, 1);
          signal_instance->callback = subscriber->callback;
          signal_instance->user_data = subscriber->user_data;
          signal_instance->message = dbus_message_ref (message);
          signal_instance->connection = g_object_ref (connection);

          /* use higher priority that method_reply to ensure signals are handled before method replies */
          idle_source = g_idle_source_new ();
          g_source_set_priority (idle_source, G_PRIORITY_HIGH);
          g_source_set_callback (idle_source,
                                 emit_signal_instance_in_idle_cb,
                                 signal_instance,
                                 (GDestroyNotify) signal_instance_free);
          g_source_attach (idle_source, subscriber->context);
          g_source_unref (idle_source);
        }
    }
}

/* do not call with any locks held */
static void
distribute_signals (EDBusConnection *connection,
                    DBusMessage     *message)
{
  const gchar *sender;
  GPtrArray *signal_data_array;

  sender = dbus_message_get_sender (message);

  G_LOCK (connection_lock);

  /* collect subcsribers that match on sender */
  if (sender != NULL)
    {
      signal_data_array = g_hash_table_lookup (connection->priv->map_sender_to_signal_data_array, sender);
      if (signal_data_array != NULL) {
        schedule_callbacks (connection, signal_data_array, message);
      }
    }

  /* collect subcsribers not matching on sender */
  signal_data_array = g_hash_table_lookup (connection->priv->map_sender_to_signal_data_array, "");
  if (signal_data_array != NULL) {
    schedule_callbacks (connection, signal_data_array, message);
  }

  G_UNLOCK (connection_lock);
}

/* ---------------------------------------------------------------------------------------------------- */

/* called from finalize(), removes all subscriptions */
static void
purge_all_signal_subscriptions (EDBusConnection *connection)
{
  GHashTableIter iter;
  gpointer key;
  GArray *ids;
  GArray *subscribers;
  guint n;

  G_LOCK (connection_lock);
  ids = g_array_new (FALSE, FALSE, sizeof (guint));
  g_hash_table_iter_init (&iter, connection->priv->map_id_to_signal_data);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    {
      guint subscription_id = GPOINTER_TO_UINT (key);
      g_array_append_val (ids, subscription_id);
    }

  subscribers = g_array_new (FALSE, FALSE, sizeof (SignalSubscriber));
  for (n = 0; n < ids->len; n++)
    {
      guint subscription_id = g_array_index (ids, guint, n);
      unsubscribe_id_internal (connection,
                               subscription_id,
                               subscribers);
    }
  g_array_free (ids, TRUE);

  G_UNLOCK (connection_lock);

  /* call GDestroyNotify without lock held */
  for (n = 0; n < subscribers->len; n++)
    {
      SignalSubscriber *subscriber;
      subscriber = &(g_array_index (subscribers, SignalSubscriber, n));
      if (subscriber->user_data_free_func != NULL)
        subscriber->user_data_free_func (subscriber->user_data);
      if (subscriber->context != NULL)
        g_main_context_unref (subscriber->context);
    }

  g_array_free (subscribers, TRUE);
}

/* ---------------------------------------------------------------------------------------------------- */

struct ExportedObject
{
  gchar *object_path;
  EDBusConnection *connection;

  /* maps gchar* -> ExportedInterface* */
  GHashTable *map_if_name_to_ei;
};

/* only called with lock held */
static void
exported_object_free (ExportedObject *eo)
{
  if (eo->connection->priv->dbus_1_connection != NULL)
    {
      if (!dbus_connection_unregister_object_path (eo->connection->priv->dbus_1_connection,
                                                   eo->object_path))
        _e_dbus_oom ();
    }
  g_free (eo->object_path);
  g_hash_table_unref (eo->map_if_name_to_ei);
  g_free (eo);
}

typedef struct
{
  ExportedObject *eo;

  guint                       id;
  gchar                      *interface_name;
  const EDBusInterfaceVTable *vtable;
  const EDBusInterfaceInfo   *introspection_data;

  GMainContext               *context;
  gpointer                    user_data;
  GDestroyNotify              user_data_free_func;
} ExportedInterface;

/* called with lock held */
static void
exported_interface_free (ExportedInterface *ei)
{
  if (ei->user_data_free_func != NULL)
    {
      /* TODO: push to thread-default mainloop */
      ei->user_data_free_func (ei->user_data);
    }
  if (ei->context != NULL)
    {
      g_main_context_unref (ei->context);
    }
  g_free (ei->interface_name);
  g_free (ei);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  EDBusConnection *connection;
  DBusMessage *message;
  gpointer user_data;
  const char *property_name;
  const EDBusInterfaceVTable *vtable;
  const EDBusInterfaceInfo *interface_info;
  const EDBusPropertyInfo *property_info;
} PropertyData;

static void
property_data_free (PropertyData *data)
{
  g_object_unref (data->connection);
  dbus_message_unref (data->message);
  g_free (data);
}

/* called in thread where object was registered - no locks held */
static gboolean
invoke_get_property_in_idle_cb (gpointer _data)
{
  PropertyData *data = _data;
  EVariant *value;
  GError *error;
  DBusMessage *reply;

  error = NULL;
  value = data->vtable->get_property (data->connection,
                                      data->user_data,
                                      dbus_message_get_sender (data->message),
                                      dbus_message_get_path (data->message),
                                      data->interface_info->name,
                                      data->property_name,
                                      &error);

  if (value != NULL)
    {
      EVariant *packed;

      g_assert_no_error (error);

      packed = e_variant_new ("(v)", value);

      reply = dbus_message_new_method_return (data->message);
      if (!_e_dbus_gvariant_to_dbus_1 (reply,
                                       packed,
                                       &error))
        {
          g_warning ("Error serializing to DBusMessage: %s", error->message);
          g_error_free (error);
          dbus_message_unref (reply);
          e_variant_unref (value);
          e_variant_unref (packed);
          goto out;
        }

      _e_dbus_connection_send_dbus_1_message (data->connection, reply);

      e_variant_unref (value);
      e_variant_unref (packed);
    }
  else
    {
      gchar *dbus_error_name;

      g_assert (error != NULL);

      dbus_error_name = e_dbus_error_encode_gerror (error);
      reply = dbus_message_new_error (data->message,
                                      dbus_error_name,
                                      error->message);

      _e_dbus_connection_send_dbus_1_message (data->connection, reply);

      g_free (dbus_error_name);
      g_error_free (error);
      dbus_message_unref (reply);
    }

 out:
  return FALSE;
}

/* called in thread where object was registered - no locks held */
static gboolean
invoke_set_property_in_idle_cb (gpointer _data)
{
  PropertyData *data = _data;
  GError *error;
  DBusMessage *reply;
  EVariant *parameters;
  EVariant *value;

  error = NULL;
  parameters = NULL;
  value = NULL;

  parameters = _e_dbus_dbus_1_to_gvariant (data->message, &error);
  if (parameters == NULL)
    {
      gchar *dbus_error_name;
      g_assert (error != NULL);
      dbus_error_name = e_dbus_error_encode_gerror (error);
      reply = dbus_message_new_error (data->message,
                                      dbus_error_name,
                                      error->message);
      g_free (dbus_error_name);
      g_error_free (error);
      goto out;
    }

  e_variant_get (parameters,
                 "(ssv)",
                 NULL,
                 NULL,
                 &value);

  /* Fail with org.freedesktop.DBus.Error.InvalidArgs if the type
   * of the given value is wrong
   */
  if (g_strcmp0 (e_variant_get_type_string (value), data->property_info->signature) != 0)
    {
      reply = dbus_message_new_error (data->message,
                                      "org.freedesktop.DBus.Error.InvalidArgs",
                                      _("Type of property to set is incorrect"));
      goto out;
    }

  if (!data->vtable->set_property (data->connection,
                                   data->user_data,
                                   dbus_message_get_sender (data->message),
                                   dbus_message_get_path (data->message),
                                   data->interface_info->name,
                                   data->property_name,
                                   value,
                                   &error))
    {
      gchar *dbus_error_name;
      g_assert (error != NULL);
      dbus_error_name = e_dbus_error_encode_gerror (error);
      reply = dbus_message_new_error (data->message,
                                      dbus_error_name,
                                      error->message);
      g_free (dbus_error_name);
      g_error_free (error);
    }
  else
    {
      reply = dbus_message_new_method_return (data->message);
    }

 out:
  g_assert (reply != NULL);
  _e_dbus_connection_send_dbus_1_message (data->connection, reply);
  if (value != NULL)
    e_variant_unref (value);
  if (parameters != NULL)
    e_variant_unref (parameters);
  dbus_message_unref (reply);
  return FALSE;
}

/* can be called with or without the lock held */
static DBusHandlerResult
validate_and_maybe_schedule_property_getset (EDBusConnection            *connection,
                                             DBusMessage                *message,
                                             const EDBusInterfaceInfo   *introspection_data,
                                             const EDBusInterfaceVTable *vtable,
                                             GMainContext               *main_context,
                                             gpointer                    user_data)
{
  DBusHandlerResult ret;
  const char *interface_name;
  const char *property_name;
  const EDBusPropertyInfo *property_info;
  GSource *idle_source;
  PropertyData *property_data;
  gboolean is_get;
  DBusMessage *reply;

  ret = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  if (!dbus_message_get_args (message,
                              NULL,
                              DBUS_TYPE_STRING, &interface_name,
                              DBUS_TYPE_STRING, &property_name,
                              DBUS_TYPE_INVALID))
    goto out;

  is_get = FALSE;
  if (g_strcmp0 (dbus_message_get_member (message), "Get") == 0)
    is_get = TRUE;

  if (is_get)
    {
      if (vtable == NULL || vtable->get_property == NULL)
        goto out;
    }
  else
    {
      if (vtable == NULL || vtable->set_property == NULL)
        goto out;
    }

  /* Check that the property exists - if not fail with org.freedesktop.DBus.Error.InvalidArgs
   */
  property_info = NULL;

  /* TODO: the cost of this is O(n) - it might be worth caching the result */
  property_info = e_dbus_interface_info_lookup_property (introspection_data,
                                                         property_name);
  if (property_info == NULL)
    {
      reply = dbus_message_new_error (message,
                                      "org.freedesktop.DBus.Error.InvalidArgs",
                                      _("No such property"));
      _e_dbus_connection_send_dbus_1_message (connection, reply);
      dbus_message_unref (reply);
      ret = DBUS_HANDLER_RESULT_HANDLED;
      goto out;
    }

  if (is_get && !(property_info->flags & E_DBUS_PROPERTY_INFO_FLAGS_READABLE))
    {
      reply = dbus_message_new_error (message,
                                      "org.freedesktop.DBus.Error.InvalidArgs",
                                      _("Property is not readable"));
      _e_dbus_connection_send_dbus_1_message (connection, reply);
      dbus_message_unref (reply);
      ret = DBUS_HANDLER_RESULT_HANDLED;
      goto out;
    }
  else if (!is_get && !(property_info->flags & E_DBUS_PROPERTY_INFO_FLAGS_WRITABLE))
    {
      reply = dbus_message_new_error (message,
                                      "org.freedesktop.DBus.Error.InvalidArgs",
                                      _("Property is not writable"));
      _e_dbus_connection_send_dbus_1_message (connection, reply);
      dbus_message_unref (reply);
      ret = DBUS_HANDLER_RESULT_HANDLED;
      goto out;
    }

  /* ok, got the property info - call user in an idle handler */
  property_data = g_new0 (PropertyData, 1);
  property_data->connection = g_object_ref (connection);
  property_data->message = dbus_message_ref (message);
  property_data->user_data = user_data;
  property_data->property_name = property_name;
  property_data->vtable = vtable;
  property_data->interface_info = introspection_data;
  property_data->property_info = property_info;

  idle_source = g_idle_source_new ();
  g_source_set_priority (idle_source, G_PRIORITY_DEFAULT);
  g_source_set_callback (idle_source,
                         is_get ? invoke_get_property_in_idle_cb : invoke_set_property_in_idle_cb,
                         property_data,
                         (GDestroyNotify) property_data_free);
  g_source_attach (idle_source, main_context);
  g_source_unref (idle_source);

  ret = DBUS_HANDLER_RESULT_HANDLED;

 out:
  return ret;
}

/* called with lock held */
static DBusHandlerResult
handle_getset_property (DBusConnection *connection,
                        ExportedObject *eo,
                        DBusMessage    *message)
{
  ExportedInterface *ei;
  DBusHandlerResult ret;
  const char *interface_name;
  const char *property_name;

  ret = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  if (!dbus_message_get_args (message,
                              NULL,
                              DBUS_TYPE_STRING, &interface_name,
                              DBUS_TYPE_STRING, &property_name,
                              DBUS_TYPE_INVALID))
    goto out;

  /* Fail with org.freedesktop.DBus.Error.InvalidArgs if there is
   * no such interface registered
   */
  ei = g_hash_table_lookup (eo->map_if_name_to_ei, interface_name);
  if (ei == NULL)
    {
      DBusMessage *reply;
      reply = dbus_message_new_error (message,
                                      "org.freedesktop.DBus.Error.InvalidArgs",
                                      _("No such interface"));
      _e_dbus_connection_send_dbus_1_message (eo->connection, reply);
      dbus_message_unref (reply);
      ret = DBUS_HANDLER_RESULT_HANDLED;
      goto out;
    }

  ret = validate_and_maybe_schedule_property_getset (eo->connection,
                                                     message,
                                                     ei->introspection_data,
                                                     ei->vtable,
                                                     ei->context,
                                                     ei->user_data);
 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  EDBusConnection *connection;
  DBusMessage *message;
  gpointer user_data;
  const EDBusInterfaceVTable *vtable;
  const EDBusInterfaceInfo *interface_info;
} PropertyGetAllData;

static void
property_get_all_data_free (PropertyData *data)
{
  g_object_unref (data->connection);
  dbus_message_unref (data->message);
  g_free (data);
}

/* called in thread where object was registered - no locks held */
static gboolean
invoke_get_all_properties_in_idle_cb (gpointer _data)
{
  PropertyGetAllData *data = _data;
  EVariantBuilder *builder;
  EVariant *packed;
  EVariant *result;
  GError *error;
  DBusMessage *reply;
  guint n;

  error = NULL;

  /* TODO: Right now we never fail this call - we just omit values if
   *       a get_property() call is failing.
   *
   *       We could fail the whole call if just a single get_property() call
   *       returns an error. We need clarification in the dbus spec for this.
   */
  builder = e_variant_builder_new (E_VARIANT_TYPE_ARRAY);
  for (n = 0; n < data->interface_info->num_properties; n++)
    {
      const EDBusPropertyInfo *property_info = data->interface_info->properties + n;
      EVariant *value;

      if (!(property_info->flags & E_DBUS_PROPERTY_INFO_FLAGS_READABLE))
        continue;

      value = data->vtable->get_property (data->connection,
                                          data->user_data,
                                          dbus_message_get_sender (data->message),
                                          dbus_message_get_path (data->message),
                                          data->interface_info->name,
                                          property_info->name,
                                          NULL);
      if (value == NULL)
        continue;

      e_variant_builder_add (builder,
                             "{sv}",
                             property_info->name,
                             value);
      e_variant_unref (value);
    }
  result = e_variant_builder_end (builder);

  builder = e_variant_builder_new (E_VARIANT_TYPE_TUPLE);
  e_variant_builder_add_value (builder, result); /* steals result since result is floating */
  packed = e_variant_builder_end (builder);

  reply = dbus_message_new_method_return (data->message);
  if (!_e_dbus_gvariant_to_dbus_1 (reply,
                                   packed,
                                   &error))
    {
      g_warning ("Error serializing to DBusMessage: %s", error->message);
      g_error_free (error);
      dbus_message_unref (reply);
      e_variant_unref (packed);
      goto out;
    }
  e_variant_unref (packed);
  _e_dbus_connection_send_dbus_1_message (data->connection, reply);
  dbus_message_unref (reply);

 out:
  return FALSE;
}

/* can be called with or without the lock held */
static DBusHandlerResult
validate_and_maybe_schedule_property_get_all (EDBusConnection            *connection,
                                              DBusMessage                *message,
                                              const EDBusInterfaceInfo   *introspection_data,
                                              const EDBusInterfaceVTable *vtable,
                                              GMainContext               *main_context,
                                              gpointer                    user_data)
{
  DBusHandlerResult ret;
  const char *interface_name;
  GSource *idle_source;
  PropertyGetAllData *property_get_all_data;

  ret = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  if (!dbus_message_get_args (message,
                              NULL,
                              DBUS_TYPE_STRING, &interface_name,
                              DBUS_TYPE_INVALID))
    goto out;

  if (vtable == NULL || vtable->get_property == NULL)
    goto out;

  /* ok, got the property info - call user in an idle handler */
  property_get_all_data = g_new0 (PropertyGetAllData, 1);
  property_get_all_data->connection = g_object_ref (connection);
  property_get_all_data->message = dbus_message_ref (message);
  property_get_all_data->user_data = user_data;
  property_get_all_data->vtable = vtable;
  property_get_all_data->interface_info = introspection_data;

  idle_source = g_idle_source_new ();
  g_source_set_priority (idle_source, G_PRIORITY_DEFAULT);
  g_source_set_callback (idle_source,
                         invoke_get_all_properties_in_idle_cb,
                         property_get_all_data,
                         (GDestroyNotify) property_get_all_data_free);
  g_source_attach (idle_source, main_context);
  g_source_unref (idle_source);

  ret = DBUS_HANDLER_RESULT_HANDLED;

 out:
  return ret;
}

/* called with lock held */
static DBusHandlerResult
handle_get_all_properties (DBusConnection *connection,
                           ExportedObject *eo,
                           DBusMessage    *message)
{
  ExportedInterface *ei;
  DBusHandlerResult ret;
  const char *interface_name;

  ret = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  if (!dbus_message_get_args (message,
                              NULL,
                              DBUS_TYPE_STRING, &interface_name,
                              DBUS_TYPE_INVALID))
    goto out;

  /* Fail with org.freedesktop.DBus.Error.InvalidArgs if there is
   * no such interface registered
   */
  ei = g_hash_table_lookup (eo->map_if_name_to_ei, interface_name);
  if (ei == NULL)
    {
      DBusMessage *reply;
      reply = dbus_message_new_error (message,
                                      "org.freedesktop.DBus.Error.InvalidArgs",
                                      _("No such interface"));
      _e_dbus_connection_send_dbus_1_message (eo->connection, reply);
      dbus_message_unref (reply);
      ret = DBUS_HANDLER_RESULT_HANDLED;
      goto out;
    }

  ret = validate_and_maybe_schedule_property_get_all (eo->connection,
                                                      message,
                                                      ei->introspection_data,
                                                      ei->vtable,
                                                      ei->context,
                                                      ei->user_data);
 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
introspect_append_header (GString *s)
{
  g_string_append (s,
                   DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE
                   "<!-- EDBus 0.1 -->\n"
                   "<node>\n");
}

static void
introspect_append_standard_interfaces (GString *s)
{
  g_string_append (s,
                    "  <interface name=\"org.freedesktop.DBus.Properties\">\n"
                    "    <method name=\"Get\">\n"
                    "      <arg type=\"s\" name=\"interface_name\" direction=\"in\"/>\n"
                    "      <arg type=\"s\" name=\"property_name\" direction=\"in\"/>\n"
                    "      <arg type=\"v\" name=\"value\" direction=\"out\"/>\n"
                    "    </method>\n"
                    "    <method name=\"GetAll\">\n"
                    "      <arg type=\"s\" name=\"interface_name\" direction=\"in\"/>\n"
                    "      <arg type=\"a{sv}\" name=\"properties\" direction=\"out\"/>\n"
                    "    </method>\n"
                    "    <method name=\"Set\">\n"
                    "      <arg type=\"s\" name=\"interface_name\" direction=\"in\"/>\n"
                    "      <arg type=\"s\" name=\"property_name\" direction=\"in\"/>\n"
                    "      <arg type=\"v\" name=\"value\" direction=\"in\"/>\n"
                    "    </method>\n"
                    "    <signal name=\"PropertiesChanged\">\n"
                    "      <arg type=\"s\" name=\"interface_name\"/>\n"
                    "      <arg type=\"a{sv}\" name=\"changed_properties\"/>\n"
                    "    </signal>\n"
                    "  </interface>\n"
                    "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
                    "    <method name=\"Introspect\">\n"
                    "      <arg type=\"s\" name=\"xml_data\" direction=\"out\"/>\n"
                    "    </method>\n"
                    "  </interface>\n"
                    "  <interface name=\"org.freedesktop.DBus.Peer\">\n"
                    "    <method name=\"Ping\"/>\n"
                    "    <method name=\"GetMachineId\">\n"
                    "      <arg type=\"s\" name=\"machine_uuid\" direction=\"out\"/>\n"
                    "    </method>\n"
                    "  </interface>\n");
}

/* called with lock held */
static DBusHandlerResult
handle_introspect (DBusConnection *connection,
                   ExportedObject *eo,
                   DBusMessage    *message)
{
  guint n;
  GString *s;
  char **subnode_paths;
  DBusMessage *reply;
  GHashTableIter hash_iter;
  ExportedInterface *ei;

  //g_debug ("in handle_introspect for %s", eo->object_path);

  /* first the header with the standard interfaces */
  s = g_string_new (NULL);

  introspect_append_header (s);
  introspect_append_standard_interfaces (s);

  /* then include the registered interfaces */
  g_hash_table_iter_init (&hash_iter, eo->map_if_name_to_ei);
  while (g_hash_table_iter_next (&hash_iter, NULL, (gpointer) &ei))
    {
      e_dbus_interface_info_generate_xml (ei->introspection_data,
                                          2,
                                          s);
    }

  /* finally include nodes registered below us */
  if (!dbus_connection_list_registered (eo->connection->priv->dbus_1_connection,
                                        eo->object_path,
                                        &subnode_paths))
    _e_dbus_oom ();
  for (n = 0; subnode_paths != NULL && subnode_paths[n] != NULL; n++)
    {
      g_string_append_printf (s, "  <node name=\"%s\"/>\n", subnode_paths[n]);
    }
  dbus_free_string_array (subnode_paths);

  g_string_append (s, "</node>\n");

  reply = dbus_message_new_method_return (message);
  if (!dbus_message_append_args (reply,
                                 DBUS_TYPE_STRING, &s->str,
                                 DBUS_TYPE_INVALID))
    _e_dbus_oom ();
  _e_dbus_connection_send_dbus_1_message (eo->connection, reply);
  g_string_free (s, TRUE);

  return DBUS_HANDLER_RESULT_HANDLED;
}

/* called in thread where object was registered - no locks held */
static gboolean
invoke_method_in_idle_cb (gpointer user_data)
{
  EDBusMethodInvocation *invocation = E_DBUS_METHOD_INVOCATION (user_data);
  EDBusInterfaceVTable *vtable;

  vtable = g_object_get_data (G_OBJECT (invocation), "g-dbus-interface-vtable");
  g_assert (vtable != NULL && vtable->method_call != NULL);

  vtable->method_call (e_dbus_method_invocation_get_connection (invocation),
                       e_dbus_method_invocation_get_user_data (invocation),
                       e_dbus_method_invocation_get_sender (invocation),
                       e_dbus_method_invocation_get_object_path (invocation),
                       e_dbus_method_invocation_get_interface_name (invocation),
                       e_dbus_method_invocation_get_method_name (invocation),
                       e_dbus_method_invocation_get_parameters (invocation),
                       g_object_ref (invocation));
  return FALSE;
}

/* can be called with or without the lock held */
static DBusHandlerResult
validate_and_maybe_schedule_method_call (EDBusConnection            *connection,
                                         DBusMessage                *message,
                                         const EDBusInterfaceInfo   *introspection_data,
                                         const EDBusInterfaceVTable *vtable,
                                         GMainContext               *main_context,
                                         gpointer                    user_data)
{
  EDBusMethodInvocation *invocation;
  DBusHandlerResult result;
  const EDBusMethodInfo *method_info;
  DBusMessage *reply;
  GError *error;
  EVariant *parameters;
  GSource *idle_source;

  result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  /* TODO: the cost of this is O(n) - it might be worth caching the result */
  method_info = e_dbus_interface_info_lookup_method (introspection_data,
                                                     dbus_message_get_member (message));
  /* if the method doesn't exist, return the org.freedesktop.DBus.Error.UnknownMethod
   * error to the caller */
  if (method_info == NULL)
    {
      reply = dbus_message_new_error (message,
                                      "org.freedesktop.DBus.Error.UnknownMethod",
                                      _("No such method"));
      _e_dbus_connection_send_dbus_1_message (connection, reply);
      dbus_message_unref (reply);
      result = DBUS_HANDLER_RESULT_HANDLED;
      goto out;
    }

  /* Check that the incoming args are of the right type - if they are not, return
   * the org.freedesktop.DBus.Error.InvalidArgs error to the caller
   */
  if (!dbus_message_has_signature (message, method_info->in_signature))
    {
      reply = dbus_message_new_error (message,
                                      "org.freedesktop.DBus.Error.InvalidArgs",
                                      _("Signature of message does not match what is expected"));
      _e_dbus_connection_send_dbus_1_message (connection, reply);
      dbus_message_unref (reply);
      result = DBUS_HANDLER_RESULT_HANDLED;
      goto out;
    }

  error = NULL;
  parameters = _e_dbus_dbus_1_to_gvariant (message, &error);
  if (parameters == NULL)
    {
      g_warning ("Error converting signal parameters to a EVariant: %s", error->message);
      g_error_free (error);
      goto out;
    }

  /* schedule the call in idle */
  invocation = e_dbus_method_invocation_new (dbus_message_get_sender (message),
                                             dbus_message_get_path (message),
                                             dbus_message_get_interface (message),
                                             dbus_message_get_member (message),
                                             connection,
                                             parameters,
                                             user_data);
  e_variant_unref (parameters);
  g_object_set_data_full (G_OBJECT (invocation),
                          "dbus-1-message",
                          dbus_message_ref (message),
                          (GDestroyNotify) dbus_message_unref);
  g_object_set_data (G_OBJECT (invocation),
                     "g-dbus-interface-vtable",
                     (gpointer) vtable);
  g_object_set_data (G_OBJECT (invocation),
                     "g-dbus-method-info",
                     (gpointer) method_info);

  idle_source = g_idle_source_new ();
  g_source_set_priority (idle_source, G_PRIORITY_DEFAULT);
  g_source_set_callback (idle_source,
                         invoke_method_in_idle_cb,
                         invocation,
                         g_object_unref);
  g_source_attach (idle_source, main_context);
  g_source_unref (idle_source);

  result = DBUS_HANDLER_RESULT_HANDLED;

 out:

  return result;
}

static DBusHandlerResult
dbus_1_obj_vtable_message_func (DBusConnection *connection,
                                DBusMessage    *message,
                                void           *user_data)
{
  ExportedObject *eo = user_data;
  const char *interface_name;
  DBusHandlerResult result;

  G_LOCK (connection_lock);

  result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  //g_debug ("in dbus_1_obj_vtable_message_func for path %s", eo->object_path);
  //PRINT_MESSAGE (message);

  /* see if we have an interface for this handling this call */
  interface_name = dbus_message_get_interface (message);
  if (interface_name != NULL)
    {
      ExportedInterface *ei;
      ei = g_hash_table_lookup (eo->map_if_name_to_ei, interface_name);
      if (ei != NULL)
        {
          /* we do - invoke the handler in idle in the right thread */

          /* handle no vtable or handler being present */
          if (ei->vtable == NULL || ei->vtable->method_call == NULL)
            goto out;

          result = validate_and_maybe_schedule_method_call (eo->connection,
                                                            message,
                                                            ei->introspection_data,
                                                            ei->vtable,
                                                            ei->context,
                                                            ei->user_data);
          goto out;
        }
    }

  if (dbus_message_is_method_call (message,
                                   "org.freedesktop.DBus.Introspectable",
                                   "Introspect") &&
      g_strcmp0 (dbus_message_get_signature (message), "") == 0)
    {
      result = handle_introspect (connection, eo, message);
      goto out;
    }
  else if (dbus_message_is_method_call (message,
                                        "org.freedesktop.DBus.Properties",
                                        "Get") &&
           g_strcmp0 (dbus_message_get_signature (message), "ss") == 0)
    {
      result = handle_getset_property (connection, eo, message);
      goto out;
    }
  else if (dbus_message_is_method_call (message,
                                        "org.freedesktop.DBus.Properties",
                                        "Set") &&
           g_strcmp0 (dbus_message_get_signature (message), "ssv") == 0)
    {
      result = handle_getset_property (connection, eo, message);
      goto out;
    }
  else if (dbus_message_is_method_call (message,
                                        "org.freedesktop.DBus.Properties",
                                        "GetAll") &&
           g_strcmp0 (dbus_message_get_signature (message), "s") == 0)
    {
      result = handle_get_all_properties (connection, eo, message);
      goto out;
    }

 out:
  G_UNLOCK (connection_lock);
  return result;
}

static const DBusObjectPathVTable dbus_1_obj_vtable =
{
  NULL,
  dbus_1_obj_vtable_message_func,
  NULL,
  NULL,
  NULL,
  NULL
};

/**
 * e_dbus_connection_register_object:
 * @connection: A #EDBusConnection.
 * @object_path: The object path to register at.
 * @interface_name: The D-Bus interface to register.
 * @introspection_data: Introspection data for the interface.
 * @vtable: A #EDBusInterfaceVTable to call into or %NULL.
 * @user_data: Data to pass to functions in @vtable.
 * @user_data_free_func: Function to call when the object path is unregistered.
 * @error: Return location for error or %NULL.
 *
 * Registers callbacks for exported objects at @object_path with the
 * D-Bus interface @interface_name.
 *
 * Calls to functions in @vtable (and @user_data_free_func) will
 * happen in the <link linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> of the thread you are calling this method from.
 *
 * Note that all #EVariant values passed to functions in @vtable will match
 * the signature given in @introspection_data - if a remote caller passes
 * incorrect values, the <literal>org.freedesktop.DBus.Error.InvalidArgs</literal>
 * is returned to the remote caller.
 *
 * Additionally, if the remote caller attempts to invoke methods or
 * access properties not mentioned in @introspection_data the
 * <literal>org.freedesktop.DBus.Error.UnknownMethod</literal> resp.
 * <literal>org.freedesktop.DBus.Error.InvalidArgs</literal> errors
 * are returned to the caller.
 *
 * It is considered a programming error if the get_property() function
 * in @vtable returns a #EVariant of incorrect type.
 *
 * If an existing callback is already registered at @object_path and
 * @interface_name or another binding is already exporting objects at
 * @object_path, then @error is set to #E_DBUS_ERROR_OBJECT_PATH_IN_USE.
 *
 * Returns: 0 if @error is set, otherwise a registration id (never 0)
 * that can be used with e_dbus_connection_unregister_object() .
 */
guint
e_dbus_connection_register_object (EDBusConnection            *connection,
                                   const gchar                *object_path,
                                   const gchar                *interface_name,
                                   const EDBusInterfaceInfo   *introspection_data,
                                   const EDBusInterfaceVTable *vtable,
                                   gpointer                    user_data,
                                   GDestroyNotify              user_data_free_func,
                                   GError                    **error)
{
  ExportedObject *eo;
  ExportedInterface *ei;
  guint ret;

  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), 0);
  g_return_val_if_fail (!e_dbus_connection_get_is_disconnected (connection), 0);
  g_return_val_if_fail (interface_name != NULL, 0);
  g_return_val_if_fail (object_path != NULL, 0);
  g_return_val_if_fail (introspection_data != NULL, 0);

  ret = 0;

  G_LOCK (connection_lock);

  eo = g_hash_table_lookup (connection->priv->map_object_path_to_eo, object_path);
  if (eo == NULL)
    {
      DBusError dbus_error;

      eo = g_new0 (ExportedObject, 1);
      eo->object_path = g_strdup (object_path);
      eo->connection = connection;
      eo->map_if_name_to_ei = g_hash_table_new_full (g_str_hash,
                                                     g_str_equal,
                                                     NULL,
                                                     (GDestroyNotify) exported_interface_free);
      g_hash_table_insert (connection->priv->map_object_path_to_eo, eo->object_path, eo);

      dbus_error_init (&dbus_error);
      if (!dbus_connection_try_register_object_path (connection->priv->dbus_1_connection,
                                                     object_path,
                                                     &dbus_1_obj_vtable,
                                                     eo,
                                                     &dbus_error))
        {
          if (g_strcmp0 (dbus_error.name, DBUS_ERROR_NO_MEMORY) == 0)
            _e_dbus_oom ();

          e_dbus_error_set_dbus_error (error,
                                       dbus_error.name,
                                       dbus_error.message,
                                       _("Another D-Bus binding is already exporting an object at %s"),
                                       object_path);
          if (error != NULL)
            {
              /* this is a locally generated error so strip the remote part */
              e_dbus_error_strip_remote_error (*error);
            }
          dbus_error_free (&dbus_error);
          goto out;
        }
    }

  ei = g_hash_table_lookup (eo->map_if_name_to_ei, interface_name);
  if (ei != NULL)
    {
      g_set_error (error,
                   E_DBUS_ERROR,
                   E_DBUS_ERROR_OBJECT_PATH_IN_USE,
                   _("An object is already exported for the interface %s at %s"),
                   interface_name,
                   object_path);
      goto out;
    }

  ei = g_new0 (ExportedInterface, 1);
  ei->id = _global_registration_id++; /* TODO: overflow etc. */
  ei->eo = eo;
  ei->user_data = user_data;
  ei->user_data_free_func = user_data_free_func;
  ei->vtable = vtable;
  ei->introspection_data = introspection_data;
  ei->interface_name = g_strdup (interface_name);
  ei->context = g_main_context_get_thread_default ();
  if (ei->context != NULL)
    g_main_context_ref (ei->context);

  g_hash_table_insert (eo->map_if_name_to_ei,
                       (gpointer) ei->interface_name,
                       ei);
  g_hash_table_insert (connection->priv->map_id_to_ei,
                       GUINT_TO_POINTER (ei->id),
                       ei);

  ret = ei->id;

 out:
  G_UNLOCK (connection_lock);

  return ret;
}

/**
 * e_dbus_connection_unregister_object:
 * @connection: A #EDBusConnection.
 * @registration_id: A registration id obtained from e_dbus_connection_register_object().
 *
 * Unregisters an object.
 *
 * Returns: %TRUE if the object was unregistered, %FALSE otherwise.
 */
gboolean
e_dbus_connection_unregister_object (EDBusConnection *connection,
                                     guint            registration_id)
{
  ExportedInterface *ei;
  ExportedObject *eo;
  gboolean ret;

  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), FALSE);

  ret = FALSE;

  G_LOCK (connection_lock);

  ei = g_hash_table_lookup (connection->priv->map_id_to_ei,
                            GUINT_TO_POINTER (registration_id));
  if (ei == NULL)
    {
      goto out;
    }

  eo = ei->eo;

  g_assert (g_hash_table_remove (connection->priv->map_id_to_ei, GUINT_TO_POINTER (ei->id)));
  g_assert (g_hash_table_remove (eo->map_if_name_to_ei, ei->interface_name));
  /* unregister object path if we have no more exported interfaces */
  if (g_hash_table_size (eo->map_if_name_to_ei) == 0)
    {
      g_assert (g_hash_table_remove (connection->priv->map_object_path_to_eo,
                                     eo->object_path));
    }

  ret = TRUE;

 out:
  G_UNLOCK (connection_lock);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * e_dbus_connection_emit_signal:
 * @connection: A #EDBusConnection.
 * @destination_bus_name: The unique bus name for the destination for the signal or %NULL to emit to all listeners.
 * @object_path: Path of remote object.
 * @interface_name: D-Bus interface to emit a signal on.
 * @signal_name: The name of the signal to emit.
 * @parameters: A #EVariant tuple with parameters for the signal or %NULL if not passing parameters.
 * @error: Return location for error or %NULL.
 *
 * Emits a signal.
 *
 * This can only fail if @parameters is not compatible with the D-Bus protocol.
 *
 * Returns: %TRUE unless @error is set.
 */
gboolean
e_dbus_connection_emit_signal (EDBusConnection    *connection,
                               const gchar        *destination_bus_name,
                               const gchar        *object_path,
                               const gchar        *interface_name,
                               const gchar        *signal_name,
                               EVariant           *parameters,
                               GError            **error)
{
  DBusMessage *message;
  gboolean ret;

  message = NULL;
  ret = FALSE;

  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), FALSE);
  g_return_val_if_fail (object_path != NULL, FALSE);
  g_return_val_if_fail (interface_name != NULL, FALSE);
  g_return_val_if_fail (signal_name != NULL, FALSE);
  g_return_val_if_fail ((parameters == NULL) || (e_variant_get_type_class (parameters) == E_VARIANT_CLASS_TUPLE), FALSE);

  message = dbus_message_new_signal (object_path,
                                     interface_name,
                                     signal_name);
  if (message == NULL)
    _e_dbus_oom ();

  if (destination_bus_name != NULL)
    {
      if (!dbus_message_set_destination (message, destination_bus_name))
        _e_dbus_oom ();
    }

  if (!_e_dbus_gvariant_to_dbus_1 (message,
                                   parameters,
                                   error))
    {
      goto out;
    }

  _e_dbus_connection_send_dbus_1_message (connection, message);

  ret = TRUE;

 out:
  if (message != NULL)
    dbus_message_unref (message);

  return ret;
}

/**
 * e_dbus_connection_invoke_method:
 * @connection: A #EDBusConnection.
 * @bus_name: A unique or well-known bus name.
 * @object_path: Path of remote object.
 * @interface_name: D-Bus interface to invoke method on.
 * @method_name: The name of the method to invoke.
 * @parameters: A #EVariant tuple with parameters for the method or %NULL if not passing parameters.
 * @timeout_msec: The timeout in milliseconds or -1 to use the default timeout.
 * @cancellable: A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * Asynchronously invokes the @method_name method on the
 * @interface_name D-Bus interface on the remote object at
 * @object_path owned by @bus_name.
 *
 * If @connection is disconnected then the operation will fail with
 * %E_DBUS_ERROR_DISCONNECTED. If @cancellable is canceled, the
 * operation will fail with %E_DBUS_ERROR_CANCELLED. If @parameters
 * contains a value not compatible with the D-Bus protocol, the operation
 * fails with %E_DBUS_ERROR_CONVERSION_FAILED.
 *
 * This is an asynchronous method. When the operation is finished, @callback will be invoked
 * in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * e_dbus_connection_invoke_method_finish() to get the result of the operation.
 * See e_dbus_connection_invoke_method_sync() for the synchronous version of this
 * function.
 */
void
e_dbus_connection_invoke_method (EDBusConnection    *connection,
                                 const gchar        *bus_name,
                                 const gchar        *object_path,
                                 const gchar        *interface_name,
                                 const gchar        *method_name,
                                 EVariant           *parameters,
                                 gint                timeout_msec,
                                 GCancellable       *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer            user_data)
{
  DBusMessage *message;
  GError *error;

  message = NULL;
  error = NULL;

  g_return_if_fail (G_IS_DBUS_CONNECTION (connection));
  g_return_if_fail ((connection->priv->bus_type == G_BUS_TYPE_NONE && bus_name == NULL) ||
                    (connection->priv->bus_type != G_BUS_TYPE_NONE && bus_name != NULL));
  g_return_if_fail (object_path != NULL);
  g_return_if_fail (interface_name != NULL);
  g_return_if_fail (method_name != NULL);
  g_return_if_fail ((parameters == NULL) || (e_variant_get_type_class (parameters) == E_VARIANT_CLASS_TUPLE));

  message = dbus_message_new_method_call (bus_name,
                                          object_path,
                                          interface_name,
                                          method_name);
  if (message == NULL)
    _e_dbus_oom ();

  if (callback == NULL)
    {
      if (_e_dbus_gvariant_to_dbus_1 (message,
                                      parameters,
                                      &error))
        {
          _e_dbus_connection_send_dbus_1_message (connection, message);
        }
      else
        {
          g_warning ("Tried invoking a method without caring about the reply, "
                     "and encountered an error serializing the parameters: %s",
                     error->message);
          g_error_free (error);
        }
    }
  else
    {
      GSimpleAsyncResult *simple;

      if (!_e_dbus_gvariant_to_dbus_1 (message,
                                       parameters,
                                       &error))
        {
          simple = g_simple_async_result_new (G_OBJECT (connection),
                                              callback,
                                              user_data,
                                              e_dbus_connection_send_dbus_1_message_with_reply);
          g_simple_async_result_set_from_error (simple, error);
          g_error_free (error);
          g_simple_async_result_complete_in_idle (simple);
          g_object_unref (simple);
          goto out;
        }

      e_dbus_connection_send_dbus_1_message_with_reply (connection,
                                                        message,
                                                        timeout_msec,
                                                        cancellable,
                                                        callback,
                                                        user_data);
    }

 out:
  if (message != NULL)
    dbus_message_unref (message);
}

/**
 * e_dbus_connection_invoke_method_finish:
 * @connection: A #EDBusConnection.
 * @res: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to e_dbus_connection_invoke_method().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with e_dbus_connection_invoke_method().
 *
 * Returns: %NULL if @error is set. Otherwise a #EVariant tuple with
 * return values. Free with e_variant_unref().
 */
EVariant *
e_dbus_connection_invoke_method_finish (EDBusConnection    *connection,
                                        GAsyncResult       *res,
                                        GError            **error)
{
  DBusMessage *reply;
  EVariant *result;

  result = NULL;

  reply = e_dbus_connection_send_dbus_1_message_with_reply_finish (connection, res, error);
  if (reply == NULL)
    goto out;

  result = _e_dbus_dbus_1_to_gvariant (reply, error);

  dbus_message_unref (reply);

 out:
  return result;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * e_dbus_connection_invoke_method_sync:
 * @connection: A #EDBusConnection.
 * @bus_name: A unique or well-known bus name.
 * @object_path: Path of remote object.
 * @interface_name: D-Bus interface to invoke method on.
 * @method_name: The name of the method to invoke.
 * @parameters: A #EVariant tuple with parameters for the method or %NULL if not passing parameters.
 * @timeout_msec: The timeout in milliseconds or -1 to use the default timeout.
 * @cancellable: A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously invokes the @method_name method on the
 * @interface_name D-Bus interface on the remote object at
 * @object_path owned by @bus_name.
 *
 * If @connection is disconnected then the operation will fail with
 * %E_DBUS_ERROR_DISCONNECTED. If @cancellable is canceled, the
 * operation will fail with %E_DBUS_ERROR_CANCELLED. If @parameters
 * contains a value not compatible with the D-Bus protocol, the operation
 * fails with %E_DBUS_ERROR_CONVERSION_FAILED.
 *
 * The calling thread is blocked until a reply is received. See
 * e_dbus_connection_invoke_method() for the asynchronous version of
 * this method.
 *
 * Returns: %NULL if @error is set. Otherwise a #EVariant tuple with
 * return values. Free with e_variant_unref().
 */
EVariant *
e_dbus_connection_invoke_method_sync (EDBusConnection    *connection,
                                      const gchar        *bus_name,
                                      const gchar        *object_path,
                                      const gchar        *interface_name,
                                      const gchar        *method_name,
                                      EVariant           *parameters,
                                      gint                timeout_msec,
                                      GCancellable       *cancellable,
                                      GError            **error)
{
  DBusMessage *message;
  DBusMessage *reply;
  EVariant *result;

  message = NULL;
  reply = NULL;
  result = NULL;

  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);
  g_return_val_if_fail ((connection->priv->bus_type == G_BUS_TYPE_NONE && bus_name == NULL) ||
                        (connection->priv->bus_type != G_BUS_TYPE_NONE && bus_name != NULL),
                        NULL);
  g_return_val_if_fail (object_path != NULL, NULL);
  g_return_val_if_fail (interface_name != NULL, NULL);
  g_return_val_if_fail (method_name != NULL, NULL);
  g_return_val_if_fail ((parameters == NULL) || (e_variant_get_type_class (parameters) == E_VARIANT_CLASS_TUPLE), NULL);

  message = dbus_message_new_method_call (bus_name,
                                          object_path,
                                          interface_name,
                                          method_name);
  if (message == NULL)
    _e_dbus_oom ();

  if (parameters != NULL)
    {
      e_variant_ref_sink (parameters);
      if (!_e_dbus_gvariant_to_dbus_1 (message,
                                       parameters,
                                       error))
        goto out;
      e_variant_unref (parameters);
    }

  reply = e_dbus_connection_send_dbus_1_message_with_reply_sync (connection,
                                                                 message,
                                                                 timeout_msec,
                                                                 cancellable,
                                                                 error);

  if (reply == NULL)
    goto out;

  result = _e_dbus_dbus_1_to_gvariant (reply, error);
  if (result == NULL)
    goto out;

 out:
  if (message != NULL)
    dbus_message_unref (message);
  if (reply != NULL)
    dbus_message_unref (reply);

  return result;
}

/* ---------------------------------------------------------------------------------------------------- */

struct ExportedSubtree
{
  guint                     id;
  gchar                    *object_path;
  EDBusConnection          *connection;
  const EDBusSubtreeVTable *vtable;
  EDBusSubtreeFlags         flags;

  GMainContext             *context;
  gpointer                  user_data;
  GDestroyNotify            user_data_free_func;
};

static void
exported_subtree_free (ExportedSubtree *es)
{
  if (es->user_data_free_func != NULL)
    {
      /* TODO: push to thread-default mainloop */
      es->user_data_free_func (es->user_data);
    }
  if (es->context != NULL)
    {
      g_main_context_unref (es->context);
    }
  g_free (es->object_path);
  g_free (es);
}

static gboolean
_g_strv_has_string (const gchar* const * haystack,
                    const gchar *needle)
{
  guint n;

  for (n = 0; haystack != NULL && haystack[n] != NULL; n++)
    {
      if (g_strcmp0 (haystack[n], needle) == 0)
        return TRUE;
    }
  return FALSE;
}

/* called without lock held */
static DBusHandlerResult
handle_subtree_introspect (DBusConnection  *connection,
                           ExportedSubtree *es,
                           DBusMessage     *message)
{
  GString *s;
  DBusHandlerResult result;
  DBusMessage *reply;
  gchar **children;
  gboolean is_root;
  const gchar *sender;
  const gchar *requested_object_path;
  const gchar *requested_node;
  GPtrArray *interfaces;
  guint n;
  char **subnode_paths;

  result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  requested_object_path = dbus_message_get_path (message);
  sender = dbus_message_get_sender (message);
  is_root = (g_strcmp0 (requested_object_path, es->object_path) == 0);

  s = g_string_new (NULL);
  introspect_append_header (s);

  //g_debug ("in handle_subtree_introspect for %s", requested_object_path);

  /* Strictly we don't need the children in dynamic mode, but we avoid the
   * conditionals to preserve code clarity */
  children = es->vtable->enumerate (es->connection,
                                    es->user_data,
                                    sender,
                                    es->object_path);

  if (!is_root)
    {
      requested_node = strrchr (requested_object_path, '/') + 1;

      /* Assert existence of object if we are not dynamic */
      if (!(es->flags & E_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES) &&
          !_g_strv_has_string ((const gchar * const *) children, requested_node))
        goto out;
    }
  else
    {
      requested_node = "/";
    }

  interfaces = es->vtable->introspect (es->connection,
                                       es->user_data,
                                       sender,
                                       es->object_path,
                                       requested_node);
  if (interfaces != NULL)
    {
      if (interfaces->len > 0)
        {
          /* we're in business */
          introspect_append_standard_interfaces (s);

          for (n = 0; n < interfaces->len; n++)
            {
              const EDBusInterfaceInfo *interface_info = interfaces->pdata[n];
              e_dbus_interface_info_generate_xml (interface_info,
                                                  2,
                                                  s);
            }
        }
      g_ptr_array_unref (interfaces);
    }

  /* then include <node> entries from the Subtree for the root */
  if (is_root)
    {
      for (n = 0; children != NULL && children[n] != NULL; n++)
        {
          g_string_append_printf (s, "  <node name=\"%s\"/>\n", children[n]);
        }
    }

  /* finally include nodes registered below us */
  if (!dbus_connection_list_registered (es->connection->priv->dbus_1_connection,
                                        requested_object_path,
                                        &subnode_paths))
    _e_dbus_oom ();
  for (n = 0; subnode_paths != NULL && subnode_paths[n] != NULL; n++)
    {
      g_string_append_printf (s, "  <node name=\"%s\"/>\n", subnode_paths[n]);
    }
  dbus_free_string_array (subnode_paths);

  g_string_append (s, "</node>\n");

  reply = dbus_message_new_method_return (message);
  if (!dbus_message_append_args (reply,
                                 DBUS_TYPE_STRING, &s->str,
                                 DBUS_TYPE_INVALID))
    _e_dbus_oom ();
  _e_dbus_connection_send_dbus_1_message (es->connection, reply);

  result = DBUS_HANDLER_RESULT_HANDLED;

 out:
  g_string_free (s, TRUE);
  g_strfreev (children);
  return result;
}

/* called without lock held */
static DBusHandlerResult
handle_subtree_method_invocation (DBusConnection *connection,
                                  ExportedSubtree *es,
                                  DBusMessage    *message)
{
  DBusHandlerResult result;
  const gchar *sender;
  const gchar *interface_name;
  const gchar *requested_object_path;
  const gchar *requested_node;
  gboolean is_root;
  gchar **children;
  const EDBusInterfaceInfo *introspection_data;
  const EDBusInterfaceVTable *interface_vtable;
  gpointer interface_user_data;
  guint n;
  GPtrArray *interfaces;
  gboolean is_property_get;
  gboolean is_property_set;
  gboolean is_property_get_all;

  result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  interfaces = NULL;

  requested_object_path = dbus_message_get_path (message);
  sender = dbus_message_get_sender (message);
  interface_name = dbus_message_get_interface (message);
  is_root = (g_strcmp0 (requested_object_path, es->object_path) == 0);

  is_property_get = FALSE;
  is_property_set = FALSE;
  is_property_get_all = FALSE;
  if (dbus_message_is_method_call (message,
                                   "org.freedesktop.DBus.Properties",
                                   "Get") &&
      g_strcmp0 (dbus_message_get_signature (message), "ss") == 0)
    {
      is_property_get = TRUE;
    }
  else if (dbus_message_is_method_call (message,
                                        "org.freedesktop.DBus.Properties",
                                        "Set") &&
           g_strcmp0 (dbus_message_get_signature (message), "ssv") == 0)
    {
      is_property_set = TRUE;
    }
  else if (dbus_message_is_method_call (message,
                                        "org.freedesktop.DBus.Properties",
                                        "GetAll") &&
           g_strcmp0 (dbus_message_get_signature (message), "s") == 0)
    {
      is_property_get_all = TRUE;
    }

  children = es->vtable->enumerate (es->connection,
                                    es->user_data,
                                    sender,
                                    es->object_path);

  if (!is_root)
    {
      requested_node = strrchr (requested_object_path, '/') + 1;

      /* If not dynamic, skip if requested node is not part of children */
      if (!(es->flags & E_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES) &&
          !_g_strv_has_string ((const gchar * const *) children, requested_node))
        goto out;
    }
  else
    {
      requested_node = "/";
    }

  /* get introspection data for the node */
  interfaces = es->vtable->introspect (es->connection,
                                       es->user_data,
                                       sender,
                                       requested_object_path,
                                       requested_node);
  g_assert (interfaces != NULL);
  introspection_data = NULL;
  for (n = 0; n < interfaces->len; n++)
    {
      const EDBusInterfaceInfo *id_n = (const EDBusInterfaceInfo *) interfaces->pdata[n];
      if (g_strcmp0 (id_n->name, interface_name) == 0)
        {
          introspection_data = id_n;
        }
    }

  /* dispatch the call if the user wants to handle it */
  if (introspection_data != NULL)
    {
      /* figure out where to dispatch the method call */
      interface_user_data = NULL;
      interface_vtable = es->vtable->dispatch (es->connection,
                                               es->user_data,
                                               sender,
                                               es->object_path,
                                               interface_name,
                                               requested_node,
                                               &interface_user_data);
      if (interface_vtable == NULL)
        goto out;

      result = validate_and_maybe_schedule_method_call (es->connection,
                                                        message,
                                                        introspection_data,
                                                        interface_vtable,
                                                        es->context,
                                                        interface_user_data);
    }
  /* handle org.freedesktop.DBus.Properties interface if not explicitly handled */
  else if (is_property_get || is_property_set || is_property_get_all)
    {
      if (!dbus_message_get_args (message,
                                  NULL,
                                  DBUS_TYPE_STRING, &interface_name,
                                  DBUS_TYPE_INVALID))
        goto out;

      /* see if the object supports this interface at all */
      for (n = 0; n < interfaces->len; n++)
        {
          const EDBusInterfaceInfo *id_n = (const EDBusInterfaceInfo *) interfaces->pdata[n];
          if (g_strcmp0 (id_n->name, interface_name) == 0)
            {
              introspection_data = id_n;
            }
        }

      /* Fail with org.freedesktop.DBus.Error.InvalidArgs if the user-code
       * claims it won't support the interface
       */
      if (introspection_data == NULL)
        {
          DBusMessage *reply;
          reply = dbus_message_new_error (message,
                                          "org.freedesktop.DBus.Error.InvalidArgs",
                                          _("No such interface"));
          _e_dbus_connection_send_dbus_1_message (es->connection, reply);
          dbus_message_unref (reply);
          result = DBUS_HANDLER_RESULT_HANDLED;
          goto out;
        }

      /* figure out where to dispatch the property get/set/getall calls */
      interface_user_data = NULL;
      interface_vtable = es->vtable->dispatch (es->connection,
                                               es->user_data,
                                               sender,
                                               es->object_path,
                                               interface_name,
                                               requested_node,
                                               &interface_user_data);
      if (interface_vtable == NULL)
        goto out;

      if (is_property_get || is_property_set)
        {
          result = validate_and_maybe_schedule_property_getset (es->connection,
                                                                message,
                                                                introspection_data,
                                                                interface_vtable,
                                                                es->context,
                                                                interface_user_data);
        }
      else if (is_property_get_all)
        {
          result = validate_and_maybe_schedule_property_get_all (es->connection,
                                                                 message,
                                                                 introspection_data,
                                                                 interface_vtable,
                                                                 es->context,
                                                                 interface_user_data);
        }
    }

 out:
  if (interfaces != NULL)
    g_ptr_array_unref (interfaces);
  g_strfreev (children);
  return result;
}

typedef struct
{
  DBusMessage *message;
  ExportedSubtree *es;
} SubtreeDeferredData;

static void
subtree_deferred_data_free (SubtreeDeferredData *data)
{
  dbus_message_unref (data->message);
  g_free (data);
}

/* called without lock held in the thread where the caller registered the subtree */
static gboolean
process_subtree_vtable_message_in_idle_cb (gpointer _data)
{
  SubtreeDeferredData *data = _data;
  DBusHandlerResult result;

  result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  if (dbus_message_is_method_call (data->message,
                                   "org.freedesktop.DBus.Introspectable",
                                   "Introspect") &&
      g_strcmp0 (dbus_message_get_signature (data->message), "") == 0)
    {
      result = handle_subtree_introspect (data->es->connection->priv->dbus_1_connection,
                                          data->es,
                                          data->message);
    }
  else
    {
      result = handle_subtree_method_invocation (data->es->connection->priv->dbus_1_connection,
                                                 data->es,
                                                 data->message);
    }

  /* if we couldn't handle the request, just bail with the UnknownMethod error */
  if (result == DBUS_HANDLER_RESULT_NOT_YET_HANDLED)
    {
      DBusMessage *reply;
      reply = dbus_message_new_error (data->message,
                                      "org.freedesktop.DBus.Error.UnknownMethod",
                                      _("Method doesn't exist"));
      _e_dbus_connection_send_dbus_1_message (data->es->connection, reply);
      dbus_message_unref (reply);
    }

  return FALSE;
}

/* called without lock held in libdbus-1 dispatching thread */
static DBusHandlerResult
dbus_1_subtree_vtable_message_func (DBusConnection *connection,
                                    DBusMessage    *message,
                                    void           *user_data)
{
  ExportedSubtree *es = user_data;
  GSource *idle_source;
  SubtreeDeferredData *data;

  //g_debug ("in dbus_1_subtree_vtable_message_func for path %s", es->object_path);
  //PRINT_MESSAGE (message);

  data = g_new0 (SubtreeDeferredData, 1);
  data->message = dbus_message_ref (message);
  data->es = user_data;

  /* defer this call to an idle handler in the right thread */
  idle_source = g_idle_source_new ();
  g_source_set_priority (idle_source, G_PRIORITY_HIGH);
  g_source_set_callback (idle_source,
                         process_subtree_vtable_message_in_idle_cb,
                         data,
                         (GDestroyNotify) subtree_deferred_data_free);
  g_source_attach (idle_source, es->context);
  g_source_unref (idle_source);

  /* since we own the entire subtree, handlers for objects not in the subtree have been
   * tried already by libdbus-1 - so we just need to ensure that we're always going
   * to reply to the message
   */
  return DBUS_HANDLER_RESULT_HANDLED;
}

static const DBusObjectPathVTable dbus_1_subtree_vtable =
{
  NULL,
  dbus_1_subtree_vtable_message_func,
  NULL,
  NULL,
  NULL,
  NULL
};


/**
 * e_dbus_connection_register_subtree:
 * @connection: A #EDBusConnection.
 * @object_path: The object path to register the subtree at.
 * @vtable: A #EDBusSubtreeVTable to enumerate, introspect and dispatch nodes in the subtree.
 * @flags: Flags used to fine tune the behavior of the subtree.
 * @user_data: Data to pass to functions in @vtable.
 * @user_data_free_func: Function to call when the subtree is unregistered.
 * @error: Return location for error or %NULL.
 *
 * Registers a whole subtree of <quote>dynamic</quote> objects.
 *
 * The @enumerate and @introspection functions in @vtable are used to
 * convey, to remote callers, what nodes exist in the subtree rooted
 * by @object_path.
 *
 * When handling remote calls into any node in the subtree, first the
 * @enumerate function is used to check if the node exists. If the node exists
 * or the #E_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES flag is set
 * the @introspection function is used to check if the node supports the
 * requested method. If so, the @dispatch function is used to determine
 * where to dispatch the call. The collected #EDBusInterfaceVTable and
 * #gpointer will be used to call into the interface vtable for processing
 * the request.
 *
 * All calls into user-provided code will be invoked in the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> of the thread you are calling this method from.
 *
 * If an existing subtree is already registered at @object_path or
 * then @error is set to #E_DBUS_ERROR_OBJECT_PATH_IN_USE.
 *
 * Note that it is valid to register regular objects (using
 * e_dbus_connection_register_object()) in a subtree registered with
 * e_dbus_connection_register_subtree() - if so, the subtree handler
 * is tried as the last resort. One way to think about a subtree
 * handler is to consider it a <quote>fallback handler</quote>
 * for object paths not registered via e_dbus_connection_register_object()
 * or other bindings.
 *
 * Returns: 0 if @error is set, otherwise a subtree registration id (never 0)
 * that can be used with e_dbus_connection_unregister_subtree() .
 */
guint
e_dbus_connection_register_subtree (EDBusConnection            *connection,
                                    const gchar                *object_path,
                                    const EDBusSubtreeVTable   *vtable,
                                    EDBusSubtreeFlags           flags,
                                    gpointer                    user_data,
                                    GDestroyNotify              user_data_free_func,
                                    GError                    **error)
{
  guint ret;
  ExportedSubtree *es;
  DBusError dbus_error;

  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), 0);
  g_return_val_if_fail (!e_dbus_connection_get_is_disconnected (connection), 0);
  g_return_val_if_fail (object_path != NULL, 0);
  g_return_val_if_fail (vtable != NULL, 0);
  g_return_val_if_fail (error == NULL || *error == NULL, 0);

  ret = 0;

  G_LOCK (connection_lock);

  es = g_hash_table_lookup (connection->priv->map_object_path_to_es, object_path);
  if (es != NULL)
    {
      g_set_error (error,
                   E_DBUS_ERROR,
                   E_DBUS_ERROR_OBJECT_PATH_IN_USE,
                   _("A subtree is already exported for %s"),
                   object_path);
      goto out;
    }

  es = g_new0 (ExportedSubtree, 1);
  es->object_path = g_strdup (object_path);
  es->connection = connection;

  dbus_error_init (&dbus_error);
  if (!dbus_connection_try_register_fallback (connection->priv->dbus_1_connection,
                                              object_path,
                                              &dbus_1_subtree_vtable,
                                              es,
                                              &dbus_error))
    {
      if (g_strcmp0 (dbus_error.name, DBUS_ERROR_NO_MEMORY) == 0)
        _e_dbus_oom ();

      e_dbus_error_set_dbus_error (error,
                                   dbus_error.name,
                                   dbus_error.message,
                                   _("Another D-Bus binding is already exporting a subtree or objects at %s"),
                                   object_path);
      if (error != NULL)
        {
          /* this is a locally generated error so strip the remote part */
          e_dbus_error_strip_remote_error (*error);
        }
      dbus_error_free (&dbus_error);

      exported_subtree_free (es);
      goto out;
    }

  es->vtable = vtable;
  es->flags = flags;
  es->id = _global_subtree_registration_id++; /* TODO: overflow etc. */
  es->user_data = user_data;
  es->user_data_free_func = user_data_free_func;
  es->context = g_main_context_get_thread_default ();
  if (es->context != NULL)
    g_main_context_ref (es->context);

  g_hash_table_insert (connection->priv->map_object_path_to_es, es->object_path, es);
  g_hash_table_insert (connection->priv->map_id_to_es,
                       GUINT_TO_POINTER (es->id),
                       es);

  ret = es->id;

 out:
  G_UNLOCK (connection_lock);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * e_dbus_connection_unregister_subtree:
 * @connection: A #EDBusConnection.
 * @registration_id: A subtree registration id obtained from e_dbus_connection_register_subtree().
 *
 * Unregisters a subtree.
 *
 * Returns: %TRUE if the subtree was unregistered, %FALSE otherwise.
 */
gboolean
e_dbus_connection_unregister_subtree (EDBusConnection *connection,
                                      guint            registration_id)
{
  ExportedSubtree *es;
  gboolean ret;

  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), FALSE);

  ret = FALSE;

  G_LOCK (connection_lock);

  es = g_hash_table_lookup (connection->priv->map_id_to_es,
                            GUINT_TO_POINTER (registration_id));
  if (es == NULL)
    {
      goto out;
    }

  /* unregister with libdbus-1 */
  g_assert (dbus_connection_unregister_object_path (connection->priv->dbus_1_connection,
                                                    es->object_path));

  g_assert (g_hash_table_remove (connection->priv->map_id_to_es, GUINT_TO_POINTER (es->id)));
  g_assert (g_hash_table_remove (connection->priv->map_object_path_to_es, es->object_path));

  ret = TRUE;

 out:
  G_UNLOCK (connection_lock);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */
