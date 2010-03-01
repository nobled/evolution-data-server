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

#include "edbusnamewatching.h"
#include "edbusproxywatching.h"
#include "edbuserror.h"
#include "edbusprivate.h"
#include "edbusproxy.h"
#include "edbusnamewatching.h"

/**
 * SECTION:gdbusproxywatching
 * @title: Watching Proxies
 * @short_description: Simple API for watching proxies
 * @include: edbus/edbus.h
 *
 * Convenience API for watching bus proxies.
 *
 * <example id="gdbus-watching-proxy"><title>Simple application watching a proxy</title><programlisting><xi:include xmlns:xi="http://www.w3.org/2001/XInclude" parse="text" href="../../../../gdbus/example-watch-proxy.c"><xi:fallback>FIXME: MISSING XINCLUDE CONTENT</xi:fallback></xi:include></programlisting></example>
 */

/* ---------------------------------------------------------------------------------------------------- */

G_LOCK_DEFINE_STATIC (lock);

static guint next_global_id = 1;
static GHashTable *map_id_to_client = NULL;

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  volatile gint              ref_count;
  guint                      id;
  GBusProxyAppearedCallback  proxy_appeared_handler;
  GBusProxyVanishedCallback  proxy_vanished_handler;
  gpointer                   user_data;
  GDestroyNotify             user_data_free_func;
  GMainContext              *main_context;

  gchar                     *name;
  gchar                     *name_owner;
  EDBusConnection           *connection;
  guint                      name_watcher_id;

  GCancellable              *cancellable;

  gchar                     *object_path;
  gchar                     *interface_name;
  GType                      interface_type;
  EDBusProxyFlags            proxy_flags;
  EDBusProxy                *proxy;

  gboolean initial_construction;
} Client;

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
      /* ensure we're only called from e_bus_unwatch_proxy */
      g_assert (client->name_watcher_id == 0);

      /* we can do this because on_name_vanished() will have cleared these */
      g_assert (client->name_owner == NULL);
      g_assert (client->connection == NULL);
      g_assert (client->proxy == NULL);

      g_free (client->name);
      g_free (client->object_path);
      g_free (client->interface_name);

      if (client->main_context != NULL)
        g_main_context_unref (client->main_context);

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

static void
proxy_constructed_cb (GObject       *source_object,
                      GAsyncResult  *res,
                      gpointer       user_data)
{
  Client *client = user_data;
  EDBusProxy *proxy;
  GError *error;

  error = NULL;
  proxy = e_dbus_proxy_new_finish (res, &error);
  if (proxy == NULL)
    {
      /* g_warning ("error while constructing proxy: %s", error->message); */
      g_error_free (error);

      /* handle initial construction, send out vanished if the name
       * is there but we constructing a proxy fails
       */
      if (client->initial_construction)
        {
          if (client->proxy_vanished_handler != NULL)
            {
              client->proxy_vanished_handler (client->connection,
                                              client->name,
                                              client->user_data);
            }
          client->initial_construction = FALSE;
        }
    }
  else
    {
      g_assert (client->proxy == NULL);
      g_assert (client->cancellable != NULL);
      client->proxy = E_DBUS_PROXY (proxy);

      g_object_unref (client->cancellable);
      client->cancellable = NULL;

      /* perform callback */
      if (client->proxy_appeared_handler != NULL)
        {
          client->proxy_appeared_handler (client->connection,
                                          client->name,
                                          client->name_owner,
                                          client->proxy,
                                          client->user_data);
        }
      client->initial_construction = FALSE;
    }
}

static void
on_name_appeared (EDBusConnection *connection,
                  const gchar *name,
                  const gchar *name_owner,
                  gpointer user_data)
{
  Client *client = user_data;

  /*g_debug ("\n\nname appeared (owner `%s')", name_owner);*/

  /* invariants */
  g_assert (client->name_owner == NULL);
  g_assert (client->connection == NULL);
  g_assert (client->cancellable == NULL);

  client->name_owner = g_strdup (name_owner);
  client->connection = g_object_ref (connection);
  client->cancellable = g_cancellable_new ();

  e_dbus_proxy_new (client->connection,
                    client->interface_type,
                    client->proxy_flags,
                    client->name_owner,
                    client->object_path,
                    client->interface_name,
                    client->cancellable,
                    proxy_constructed_cb,
                    client);
}

static void
on_name_vanished (EDBusConnection *connection,
                  const gchar *name,
                  gpointer user_data)
{
  Client *client = user_data;

  /*g_debug ("\n\nname vanished");*/

  g_free (client->name_owner);
  if (client->connection != NULL)
    g_object_unref (client->connection);
  client->name_owner = NULL;
  client->connection = NULL;

  /* free the proxy if we have it */
  if (client->proxy != NULL)
    {
      g_assert (client->cancellable == NULL);

      g_object_unref (client->proxy);
      client->proxy = NULL;

      /* if we have the proxy, it means we last sent out a 'appeared'
       * callback - so send out a 'vanished' callback
       */
      if (client->proxy_vanished_handler != NULL)
        {
          client->proxy_vanished_handler (client->connection,
                                          client->name,
                                          client->user_data);
        }
      client->initial_construction = FALSE;
    }
  else
    {
      /* otherwise cancel construction of the proxy if applicable */
      if (client->cancellable != NULL)
        {
          g_cancellable_cancel (client->cancellable);
          g_object_unref (client->cancellable);
          client->cancellable = NULL;
        }
      else
        {
          /* handle initial construction, send out vanished if
           * the name isn't there
           */
          if (client->initial_construction)
            {
              if (client->proxy_vanished_handler != NULL)
                {
                  client->proxy_vanished_handler (client->connection,
                                                  client->name,
                                                  client->user_data);
                }
              client->initial_construction = FALSE;
            }
        }
    }
}

/**
 * e_bus_watch_proxy:
 * @bus_type: The type of bus to watch a name on (can't be #E_BUS_TYPE_NONE).
 * @name: The name (well-known or unique) to watch.
 * @object_path: The object path of the remote object to watch.
 * @interface_name: The D-Bus interface name for the proxy.
 * @interface_type: The #GType for the kind of proxy to create. This must be a #EDBusProxy derived type.
 * @proxy_flags: Flags from #EDBusProxyFlags to use when constructing the proxy.
 * @proxy_appeared_handler: Handler to invoke when @name is known to exist and the
 * requested proxy is available.
 * @proxy_vanished_handler: Handler to invoke when @name is known to not exist
 * and the previously created proxy is no longer available.
 * @user_data: User data to pass to handlers.
 * @user_data_free_func: Function for freeing @user_data or %NULL.
 *
 * Starts watching a remote object at @object_path owned by @name on
 * the bus specified by @bus_type. When the object is available, a
 * #EDBusProxy (or derived class cf. @interface_type) instance is
 * constructed for the @interface_name D-Bus interface and then
 * @proxy_appeared_handler will be called when the proxy is ready and
 * all properties have been loaded. When @name vanishes,
 * @proxy_vanished_handler is called.
 *
 * This function makes it very simple to write applications that wants
 * to watch a well-known remote object on a well-known name, see <xref
 * linkend="gdbus-watching-proxy"/>. Basically, the application simply
 * starts using the proxy when @proxy_appeared_handler is called and
 * stops using it when @proxy_vanished_handler is called. Callbacks
 * will be invoked in the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> of the thread you are calling this function from.
 *
 * Applications typically use this function to watch the
 * <quote>manager</quote> object of a well-known name. Upon acquiring
 * a proxy for the manager object, applications typically construct
 * additional proxies in response to the result of enumeration methods
 * on the manager object.
 *
 * Many of the comment that applies to e_bus_watch_name() also applies
 * here. For example, you are guaranteed that one of the handlers will
 * be invoked (on the main thread) after calling this function and
 * also that the two handlers alternate. When you are done watching the
 * proxy, just call e_bus_unwatch_proxy().
 *
 * Returns: An identifier (never 0) that can be used with
 * e_bus_unwatch_proxy() to stop watching the remote object.
 **/
guint
e_bus_watch_proxy (EDBusType                   bus_type,
                   const gchar               *name,
                   const gchar               *object_path,
                   const gchar               *interface_name,
                   GType                      interface_type,
                   EDBusProxyFlags            proxy_flags,
                   GBusProxyAppearedCallback  proxy_appeared_handler,
                   GBusProxyVanishedCallback  proxy_vanished_handler,
                   gpointer                   user_data,
                   GDestroyNotify             user_data_free_func)
{
  Client *client;

  g_return_val_if_fail (bus_type != E_BUS_TYPE_NONE, 0);
  g_return_val_if_fail (name != NULL, 0);
  g_return_val_if_fail (proxy_appeared_handler != NULL, 0);
  g_return_val_if_fail (proxy_vanished_handler != NULL, 0);
  g_return_val_if_fail (object_path != NULL, 0);
  g_return_val_if_fail (interface_name != NULL, 0);
  g_return_val_if_fail (g_type_is_a (interface_type, E_TYPE_DBUS_PROXY), 0);

  G_LOCK (lock);

  client = g_new0 (Client, 1);
  client->id = next_global_id++; /* TODO: uh oh, handle overflow */
  client->name = g_strdup (name);
  client->proxy_appeared_handler = proxy_appeared_handler;
  client->proxy_vanished_handler = proxy_vanished_handler;
  client->user_data = user_data;
  client->user_data_free_func = user_data_free_func;
  client->main_context = g_main_context_get_thread_default ();
  if (client->main_context != NULL)
    g_main_context_ref (client->main_context);
  client->name_watcher_id = e_bus_watch_name (bus_type,
                                              name,
                                              on_name_appeared,
                                              on_name_vanished,
                                              client,
                                              NULL);

  client->object_path = g_strdup (object_path);
  client->interface_name = g_strdup (interface_name);
  client->interface_type = interface_type;
  client->proxy_flags = proxy_flags;
  client->initial_construction = TRUE;

  if (map_id_to_client == NULL)
    {
      map_id_to_client = g_hash_table_new (g_direct_hash, g_direct_equal);
    }
  g_hash_table_insert (map_id_to_client,
                       GUINT_TO_POINTER (client->id),
                       client);

  G_UNLOCK (lock);

  return client->id;
}

/**
 * e_bus_unwatch_proxy:
 * @watcher_id: An identifier obtained from e_bus_watch_proxy()
 *
 * Stops watching proxy.
 *
 * If the proxy being watched currently exists
 * (e.g. @proxy_appeared_handler was the last handler to be invoked),
 * then @proxy_vanished_handler will be invoked before this function returns.
 **/
void
e_bus_unwatch_proxy (guint watcher_id)
{
  Client *client;

  client = NULL;

  G_LOCK (lock);
  if (watcher_id == 0 ||
      map_id_to_client == NULL ||
      (client = g_hash_table_lookup (map_id_to_client, GUINT_TO_POINTER (watcher_id))) == NULL)
    {
      g_warning ("Invalid id %d passed to e_bus_unwatch_proxy()", watcher_id);
      goto out;
    }

  g_warn_if_fail (g_hash_table_remove (map_id_to_client, GUINT_TO_POINTER (watcher_id)));

 out:
  G_UNLOCK (lock);

  /* TODO: think about this: this will trigger callbacks from the name_watcher object being used */
  if (client != NULL)
    {
      /* this will trigger on_name_vanished() */
      e_bus_unwatch_name (client->name_watcher_id);
      client->name_watcher_id = 0;
      /* this will happen *after* on_name_vanished() */
      schedule_unref_in_idle (client);
    }
}
