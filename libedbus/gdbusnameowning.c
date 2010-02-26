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

#include "gdbusnameowning.h"
#include "gdbuserror.h"
#include "gdbusprivate.h"
#include "gdbusconnection.h"

/**
 * SECTION:gdbusnameowning
 * @title: Owning Bus Names
 * @short_description: Simple API for owning bus names
 * @include: gdbus/gdbus.h
 *
 * Convenience API for owning bus names.
 *
 * <example id="gdbus-owning-names"><title>Simple application owning a name</title><programlisting><xi:include xmlns:xi="http://www.w3.org/2001/XInclude" parse="text" href="../../../../gdbus/example-own-name.c"><xi:fallback>FIXME: MISSING XINCLUDE CONTENT</xi:fallback></xi:include></programlisting></example>
 */

G_LOCK_DEFINE_STATIC (lock);

/* ---------------------------------------------------------------------------------------------------- */

typedef enum
{
  PREVIOUS_CALL_NONE = 0,
  PREVIOUS_CALL_ACQUIRED,
  PREVIOUS_CALL_LOST,
} PreviousCall;

typedef struct
{
  volatile gint             ref_count;
  guint                     id;
  GBusNameOwnerFlags        flags;
  gchar                    *name;
  GBusNameAcquiredCallback  name_acquired_handler;
  GBusNameLostCallback      name_lost_handler;
  gpointer                  user_data;
  GDestroyNotify            user_data_free_func;
  GMainContext             *main_context;

  PreviousCall              previous_call;

  EDBusConnection          *connection;
  gulong                    disconnected_signal_handler_id;
  guint                     name_acquired_subscription_id;
  guint                     name_lost_subscription_id;

  gboolean                  cancelled;

  gboolean                  needs_release;
} Client;

static guint next_global_id = 1;
static GHashTable *map_id_to_client = NULL;


static Client *
client_ref (Client *client)
{
  g_atomic_int_inc (&client->ref_count);
  return client;
}

static void
client_unref (Client *client)
{
  if (g_atomic_int_dec_and_test (&client->ref_count))
    {
      if (client->connection != NULL)
        {
          if (client->disconnected_signal_handler_id > 0)
            g_signal_handler_disconnect (client->connection, client->disconnected_signal_handler_id);
          if (client->name_acquired_subscription_id > 0)
            e_dbus_connection_signal_unsubscribe (client->connection, client->name_acquired_subscription_id);
          if (client->name_lost_subscription_id > 0)
            e_dbus_connection_signal_unsubscribe (client->connection, client->name_lost_subscription_id);
          g_object_unref (client->connection);
        }
      if (client->main_context != NULL)
        g_main_context_unref (client->main_context);
      g_free (client->name);
      if (client->user_data_free_func != NULL)
        client->user_data_free_func (client->user_data);
      g_free (client);
    }
}

static gboolean
schedule_unref_in_idle_cb (gpointer data)
{
  Client *client = data;
  client_unref (client);
  return FALSE;
}

static void
schedule_unref_in_idle (Client *client)
{
  GSource *idle_source;

  idle_source = g_idle_source_new ();
  g_source_set_priority (idle_source, G_PRIORITY_HIGH);
  g_source_set_callback (idle_source,
                         schedule_unref_in_idle_cb,
                         client_ref (client),
                         (GDestroyNotify) client_unref);
  g_source_attach (idle_source, client->main_context);
  g_source_unref (idle_source);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  Client *client;

  /* keep this separate because client->connection may
   * be set to NULL after scheduling the call
   */
  EDBusConnection *connection;

  /* set to TRUE to call acquired */
  gboolean call_acquired;
} CallHandlerData;

static void
call_handler_data_free (CallHandlerData *data)
{
  if (data->connection != NULL)
    g_object_unref (data->connection);
  client_unref (data->client);
  g_free (data);
}

static gboolean
call_in_idle_cb (gpointer _data)
{
  CallHandlerData *data = _data;

  if (data->call_acquired)
    {
      if (data->client->name_acquired_handler != NULL)
        {
          data->client->name_acquired_handler (data->connection,
                                               data->client->name,
                                               data->client->user_data);
        }
    }
  else
    {
      if (data->client->name_lost_handler != NULL)
        {
          data->client->name_lost_handler (data->connection,
                                           data->client->name,
                                           data->client->user_data);
        }
    }

  return FALSE;
}

static void
schedule_call_in_idle (Client *client,
                       gboolean call_acquired)
{
  CallHandlerData *data;
  GSource *idle_source;

  data = g_new0 (CallHandlerData, 1);
  data->client = client_ref (client);
  data->connection = client->connection != NULL ? g_object_ref (client->connection) : NULL;
  data->call_acquired = call_acquired;

  idle_source = g_idle_source_new ();
  g_source_set_priority (idle_source, G_PRIORITY_HIGH);
  g_source_set_callback (idle_source,
                         call_in_idle_cb,
                         data,
                         (GDestroyNotify) call_handler_data_free);
  g_source_attach (idle_source, client->main_context);
  g_source_unref (idle_source);
}

static void
call_acquired_handler (Client *client)
{
  if (client->previous_call != PREVIOUS_CALL_ACQUIRED)
    {
      client->previous_call = PREVIOUS_CALL_ACQUIRED;
      if (!client->cancelled)
        {
          schedule_call_in_idle (client, TRUE);
        }
    }
}

static void
call_lost_handler (Client  *client,
                   gboolean ignore_cancelled)
{
  if (client->previous_call != PREVIOUS_CALL_LOST)
    {
      client->previous_call = PREVIOUS_CALL_LOST;
      if ((!client->cancelled) || ignore_cancelled)
        {
          schedule_call_in_idle (client, FALSE);
        }
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_name_lost_or_acquired (EDBusConnection  *connection,
                          const gchar      *sender_name,
                          const gchar      *object_path,
                          const gchar      *interface_name,
                          const gchar      *signal_name,
                          EVariant         *parameters,
                          gpointer          user_data)
{
  Client *client = user_data;
  const gchar *name;

  if (g_strcmp0 (object_path, "/org/freedesktop/DBus") != 0 ||
      g_strcmp0 (interface_name, "org.freedesktop.DBus") != 0 ||
      g_strcmp0 (sender_name, "org.freedesktop.DBus") != 0)
    goto out;

  if (g_strcmp0 (signal_name, "NameLost") == 0)
    {
      e_variant_get (parameters, "(s)", &name);
      if (g_strcmp0 (name, client->name) == 0)
        {
          call_lost_handler (client, FALSE);
        }
    }
  else if (g_strcmp0 (signal_name, "NameAcquired") == 0)
    {
      e_variant_get (parameters, "(s)", &name);
      if (g_strcmp0 (name, client->name) == 0)
        {
          call_acquired_handler (client);
        }
    }
 out:
  ;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
request_name_cb (GObject      *source_object,
                 GAsyncResult *res,
                 gpointer      user_data)
{
  Client *client = user_data;
  EVariant *result;
  guint32 request_name_reply;
  gboolean subscribe;

  request_name_reply = 0;
  result = NULL;

  result = e_dbus_connection_invoke_method_finish (client->connection,
                                                   res,
                                                   NULL);
  if (result != NULL)
    {
      e_variant_get (result, "(u)", &request_name_reply);
      e_variant_unref (result);
    }

  subscribe = FALSE;

  switch (request_name_reply)
    {
    case 1: /* DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER */
      /* We got the name - now listen for NameLost and NameAcquired */
      call_acquired_handler (client);
      subscribe = TRUE;
      client->needs_release = TRUE;
      break;

    case 2: /* DBUS_REQUEST_NAME_REPLY_IN_QUEUE */
      /* Waiting in line - listen for NameLost and NameAcquired */
      call_lost_handler (client, FALSE);
      subscribe = TRUE;
      client->needs_release = TRUE;
      break;

    default:
      /* assume we couldn't get the name - explicit fallthrough */
    case 3: /* DBUS_REQUEST_NAME_REPLY_EXISTS */
    case 4: /* DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER */
      /* Some other part of the process is already owning the name */
      call_lost_handler (client, FALSE);
      break;
    }

  if (subscribe)
    {
      /* start listening to NameLost and NameAcquired messages */
      client->name_lost_subscription_id =
        e_dbus_connection_signal_subscribe (client->connection,
                                            "org.freedesktop.DBus",
                                            "org.freedesktop.DBus",
                                            "NameLost",
                                            "/org/freedesktop/DBus",
                                            client->name,
                                            on_name_lost_or_acquired,
                                            client,
                                            NULL);
      client->name_acquired_subscription_id =
        e_dbus_connection_signal_subscribe (client->connection,
                                            "org.freedesktop.DBus",
                                            "org.freedesktop.DBus",
                                            "NameAcquired",
                                            "/org/freedesktop/DBus",
                                            client->name,
                                            on_name_lost_or_acquired,
                                            client,
                                            NULL);
    }

  client_unref (client);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_connection_disconnected (EDBusConnection *connection,
                            gpointer         user_data)
{
  Client *client = user_data;

  if (client->disconnected_signal_handler_id > 0)
    g_signal_handler_disconnect (client->connection, client->disconnected_signal_handler_id);
  if (client->name_acquired_subscription_id > 0)
    e_dbus_connection_signal_unsubscribe (client->connection, client->name_acquired_subscription_id);
  if (client->name_lost_subscription_id > 0)
    e_dbus_connection_signal_unsubscribe (client->connection, client->name_lost_subscription_id);
  g_object_unref (client->connection);
  client->disconnected_signal_handler_id = 0;
  client->name_acquired_subscription_id = 0;
  client->name_lost_subscription_id = 0;
  client->connection = NULL;

  call_lost_handler (client, FALSE);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
has_connection (Client *client)
{
  /* listen for disconnection */
  client->disconnected_signal_handler_id = g_signal_connect (client->connection,
                                                             "disconnected",
                                                             G_CALLBACK (on_connection_disconnected),
                                                             client);

  /* attempt to acquire the name */
  e_dbus_connection_invoke_method (client->connection,
                                   "org.freedesktop.DBus",  /* bus name */
                                   "/org/freedesktop/DBus", /* object path */
                                   "org.freedesktop.DBus",  /* interface name */
                                   "RequestName",           /* method name */
                                   e_variant_new ("(su)",
                                                  client->name,
                                                  client->flags),
                                   -1,
                                   NULL,
                                   (GAsyncReadyCallback) request_name_cb,
                                   client_ref (client));
}


static void
connection_get_cb (GObject      *source_object,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  Client *client = user_data;

  client->connection = e_dbus_connection_bus_get_finish (res, NULL);
  if (client->connection == NULL)
    {
      call_lost_handler (client, FALSE);
      goto out;
    }

  has_connection (client);

 out:
  client_unref (client);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * e_bus_own_name_on_connection:
 * @connection: A #EDBusConnection that has not been disconnected.
 * @name: The well-known name to own.
 * @flags: A set of flags from the #GBusNameOwnerFlags enumeration.
 * @name_acquired_handler: Handler to invoke when @name is acquired.
 * @name_lost_handler: Handler to invoke when @name is lost.
 * @user_data: User data to pass to handlers.
 * @user_data_free_func: Function for freeing @user_data or %NULL.
 *
 * Like e_bus_own_name() but takes a #EDBusConnection instead of a
 * #GBusType.
 *
 * Returns: An identifier (never 0) that an be used with
 * e_bus_unown_name() to stop owning the name.
 **/
guint
e_bus_own_name_on_connection (EDBusConnection          *connection,
                              const gchar              *name,
                              GBusNameOwnerFlags        flags,
                              GBusNameAcquiredCallback  name_acquired_handler,
                              GBusNameLostCallback      name_lost_handler,
                              gpointer                  user_data,
                              GDestroyNotify            user_data_free_func)
{
  Client *client;

  g_return_val_if_fail (connection != NULL, 0);
  g_return_val_if_fail (!e_dbus_connection_get_is_disconnected (connection), 0);
  g_return_val_if_fail (name != NULL, 0);
  //g_return_val_if_fail (TODO_is_well_known_name (), 0);
  g_return_val_if_fail (name_acquired_handler != NULL, 0);
  g_return_val_if_fail (name_lost_handler != NULL, 0);

  G_LOCK (lock);

  client = g_new0 (Client, 1);
  client->ref_count = 1;
  client->id = next_global_id++; /* TODO: uh oh, handle overflow */
  client->name = g_strdup (name);
  client->flags = flags;
  client->name_acquired_handler = name_acquired_handler;
  client->name_lost_handler = name_lost_handler;
  client->user_data = user_data;
  client->user_data_free_func = user_data_free_func;
  client->main_context = g_main_context_get_thread_default ();
  if (client->main_context != NULL)
    g_main_context_ref (client->main_context);

  client->connection = g_object_ref (connection);

  if (map_id_to_client == NULL)
    {
      map_id_to_client = g_hash_table_new (g_direct_hash, g_direct_equal);
    }
  g_hash_table_insert (map_id_to_client,
                       GUINT_TO_POINTER (client->id),
                       client);

  G_UNLOCK (lock);

  has_connection (client);

  return client->id;
}

/**
 * e_bus_own_name:
 * @bus_type: The type of bus to own a name on (can't be #G_BUS_TYPE_NONE).
 * @name: The well-known name to own.
 * @flags: A set of flags from the #GBusNameOwnerFlags enumeration.
 * @name_acquired_handler: Handler to invoke when @name is acquired.
 * @name_lost_handler: Handler to invoke when @name is lost.
 * @user_data: User data to pass to handlers.
 * @user_data_free_func: Function for freeing @user_data or %NULL.
 *
 * Starts acquiring @name on the bus specified by @bus_type and calls
 * @name_acquired_handler and @name_lost_handler when the name is
 * acquired respectively lost. Callbacks will be invoked in the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> of the thread you are calling this function from.
 *
 * You are guaranteed that one of the handlers will be invoked after
 * calling this function. When you are done owning the name, just call
 * e_bus_unown_name() with the owner id this function returns.
 *
 * If the name is acquired or lost (for example another application
 * could acquire the name if you allow replacement), the handlers are
 * also invoked. If the #EDBusConnection that is used for attempting
 * to own the name disconnects, then @name_lost_handler is invoked since
 * it is no longer possible for other processes to access the
 * process.
 *
 * You cannot use e_bus_own_name() several times (unless interleaved
 * with calls to e_bus_unown_name()) - only the first call will work.
 *
 * Another guarantee is that invocations of @name_acquired_handler
 * and @name_lost_handler are guaranteed to alternate; that
 * is, if @name_acquired_handler is invoked then you are
 * guaranteed that the next time one of the handlers is invoked, it
 * will be @name_lost_handler. The reverse is also true.
 *
 * This behavior makes it very simple to write applications that wants
 * to own names, see <xref linkend="gdbus-owning-names"/>. Simply
 * register objects to be exported in @name_acquired_handler and
 * unregister the objects (if any) in @name_lost_handler.
 *
 * Returns: An identifier (never 0) that an be used with
 * e_bus_unown_name() to stop owning the name.
 **/
guint
e_bus_own_name (GBusType                  bus_type,
                const gchar              *name,
                GBusNameOwnerFlags        flags,
                GBusNameAcquiredCallback  name_acquired_handler,
                GBusNameLostCallback      name_lost_handler,
                gpointer                  user_data,
                GDestroyNotify            user_data_free_func)
{
  Client *client;

  g_return_val_if_fail (bus_type != G_BUS_TYPE_NONE, 0);
  g_return_val_if_fail (name != NULL, 0);
  //g_return_val_if_fail (TODO_is_well_known_name (), 0);
  g_return_val_if_fail (name_acquired_handler != NULL, 0);
  g_return_val_if_fail (name_lost_handler != NULL, 0);

  G_LOCK (lock);

  client = g_new0 (Client, 1);
  client->ref_count = 1;
  client->id = next_global_id++; /* TODO: uh oh, handle overflow */
  client->name = g_strdup (name);
  client->flags = flags;
  client->name_acquired_handler = name_acquired_handler;
  client->name_lost_handler = name_lost_handler;
  client->user_data = user_data;
  client->user_data_free_func = user_data_free_func;
  client->main_context = g_main_context_get_thread_default ();
  if (client->main_context != NULL)
    g_main_context_ref (client->main_context);

  if (map_id_to_client == NULL)
    {
      map_id_to_client = g_hash_table_new (g_direct_hash, g_direct_equal);
    }
  g_hash_table_insert (map_id_to_client,
                       GUINT_TO_POINTER (client->id),
                       client);

  e_dbus_connection_bus_get (bus_type,
                             NULL,
                             connection_get_cb,
                             client_ref (client));
  G_UNLOCK (lock);

  return client->id;
}

/**
 * e_bus_unown_name:
 * @owner_id: An identifier obtained from e_bus_own_name()
 *
 * Stops owning a name.
 *
 * If currently owning the name (e.g. @name_acquired_handler was the
 * last handler to be invoked), then @name_lost_handler will be invoked.
 **/
void
e_bus_unown_name (guint owner_id)
{
  Client *client;

  client = NULL;

  G_LOCK (lock);
  if (owner_id == 0 || map_id_to_client == NULL ||
      (client = g_hash_table_lookup (map_id_to_client, GUINT_TO_POINTER (owner_id))) == NULL)
    {
      g_warning ("Invalid id %d passed to e_bus_unown_name()", owner_id);
      goto out;
    }

  client->cancelled = TRUE;
  g_warn_if_fail (g_hash_table_remove (map_id_to_client, GUINT_TO_POINTER (owner_id)));

 out:
  G_UNLOCK (lock);

  /* do callback without holding lock */
  if (client != NULL)
    {
      /* Release the name if needed */
      if (client->needs_release && client->connection != NULL)
        {
          EVariant *result;
          GError *error;
          guint32 release_name_reply;

          /* TODO: it kinda sucks having to do a sync call to release the name - but if
           * we don't, then a subsequent grab of the name will make the bus daemon return
           * IN_QUEUE which will trigger name_lost().
           *
           * I believe this is a bug in the bus daemon.
           */
          error = NULL;
          result = e_dbus_connection_invoke_method_sync (client->connection,
                                                         "org.freedesktop.DBus",  /* bus name */
                                                         "/org/freedesktop/DBus", /* object path */
                                                         "org.freedesktop.DBus",  /* interface name */
                                                         "ReleaseName",           /* method name */
                                                         e_variant_new ("(s)", client->name),
                                                         -1,
                                                         NULL,
                                                         &error);
          if (result == NULL)
            {
              g_warning ("Error releasing name %s: %s", client->name, error->message);
              g_error_free (error);
            }
          else
            {
              e_variant_get (result, "(u)", &release_name_reply);
              if (release_name_reply != 1 /* DBUS_RELEASE_NAME_REPLY_RELEASED */)
                {
                  g_warning ("Unexpected reply %d when releasing name %s", release_name_reply, client->name);
                }
              e_variant_unref (result);
            }

          call_lost_handler (client, TRUE);

          if (client->disconnected_signal_handler_id > 0)
            g_signal_handler_disconnect (client->connection, client->disconnected_signal_handler_id);
          if (client->name_acquired_subscription_id > 0)
            e_dbus_connection_signal_unsubscribe (client->connection, client->name_acquired_subscription_id);
          if (client->name_lost_subscription_id > 0)
            e_dbus_connection_signal_unsubscribe (client->connection, client->name_lost_subscription_id);
          g_object_unref (client->connection);
          client->disconnected_signal_handler_id = 0;
          client->name_acquired_subscription_id = 0;
          client->name_lost_subscription_id = 0;
          client->connection = NULL;
        }
      else
        {
          call_lost_handler (client, TRUE);
        }

      schedule_unref_in_idle (client);
    }
}
