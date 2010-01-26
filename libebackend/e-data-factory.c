/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2006 OpenedHand Ltd
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of version 2.1 of the GNU Lesser General Public License as
 * published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Authors:
 *   Ross Burton <ross@linux.intel.com>
 *   Travis Reitter <travis.reitter@collabora.co.uk>
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <dbus/dbus-protocol.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus-glib-bindings.h>
#include "e-data-server-module.h"
#include "e-offline-listener.h"
#include "e-backend-factory.h"
#include "e-data-factory.h"
#include "e-data.h"
#include "e-backend.h"

#define d(x)

static GMainLoop *loop;
DBusGConnection *connection;

/* Convenience macro to test and set a GError/return on failure */
#define g_set_error_val_if_fail(test, returnval, error, domain, code) G_STMT_START{ \
		if G_LIKELY (test) {} else {				\
			g_set_error (error, domain, code, #test);	\
			g_warning(#test " failed");			\
			return (returnval);				\
		}							\
	}G_STMT_END

G_DEFINE_ABSTRACT_TYPE (EDataFactory, e_data_factory, G_TYPE_OBJECT);

#define E_DATA_FACTORY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), E_TYPE_DATA_FACTORY, EDataFactoryPrivate))

struct _EDataFactoryPrivate {
	/* TODO: as the factory is not threaded these locks could be removed */
	GMutex *backends_lock;
	GHashTable *backends;
	GHashTable *backend_factories;

	GMutex *books_lock;
	/* A hash of object paths for data URIs to EDatas */
	GHashTable *books;

	GMutex *connections_lock;
	/* This is a hash of client addresses to GList* of EDatas */
	GHashTable *connections;

	guint exit_timeout;

        gint mode;
};

/* Create the EDataFactory error quark */
GQuark
e_data_factory_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("e_data_factory_error");
	return quark;
}

/**
 * register_backend_factory:
 * @data_factory: an #EDataFactory
 * @backend_factory: an #EBackendFactory
 *
 * Registers @backend_factory with @data_factory.
 **/
static void
register_backend_factory (EDataFactory    *data_factory,
				 EBackendFactory *backend_factory)
{
	const gchar *proto;

	g_return_if_fail (E_IS_DATA_FACTORY (data_factory));
	g_return_if_fail (E_IS_BACKEND_FACTORY (backend_factory));

	proto = E_BACKEND_FACTORY_GET_CLASS (backend_factory)->get_protocol (backend_factory);

	if (g_hash_table_lookup (data_factory->priv->backend_factories, proto) != NULL) {
		g_warning (G_STRLOC ": Proto \"%s\" already registered!\n", proto);
	}

	g_hash_table_insert (data_factory->priv->backend_factories,
			     g_strdup (proto), backend_factory);
}

/**
 * register_backend_factories:
 * @data_factory: an #EDataFactory
 *
 * Register the backends supported by the Evolution Data Server,
 * with @data_factory.
 **/
static void
register_backend_factories (EDataFactory *data_factory,
				  GType         backend_type)
{
	GList *factories, *f;

	factories = e_data_server_get_extensions_for_type (backend_type);

	for (f = factories; f; f = f->next) {
		EBackendFactory *backend_factory = f->data;

		register_backend_factory (data_factory, g_object_ref (backend_factory));
	}

	e_data_server_extension_list_free (factories);
	e_data_server_module_remove_unused ();
}

static void
set_backend_online_status (gpointer key, gpointer value, gpointer data)
{
	GList *books;
	EBackend *backend = NULL;

	for (books = (GList *) value; books; books = g_list_next (books)) {
		backend =  E_BACKEND (books->data);
		e_backend_set_mode (backend,  GPOINTER_TO_INT (data));
	}
}

/**
 * e_data_factory_set_backend_mode:
 * @data_factory: A bookendar data_factory.
 * @mode: Online mode to set.
 *
 * Sets the online mode for all backends created by the given data_factory.
 */
void
e_data_factory_set_backend_mode (EDataFactory *data_factory, gint mode)
{
	EDataFactoryPrivate *priv = data_factory->priv;

	priv->mode = mode;
	g_mutex_lock (priv->connections_lock);
	g_hash_table_foreach (priv->connections, set_backend_online_status, GINT_TO_POINTER (priv->mode));
	g_mutex_unlock (priv->connections_lock);
}

static void
e_data_factory_class_init (EDataFactoryClass *e_data_factory_class)
{
	g_type_class_add_private (e_data_factory_class, sizeof (EDataFactoryPrivate));
}

/* Instance init */
static void
e_data_factory_init (EDataFactory *data_factory)
{
	data_factory->priv = E_DATA_FACTORY_GET_PRIVATE (data_factory);

	data_factory->priv->backends_lock = g_mutex_new ();
	data_factory->priv->backends = g_hash_table_new (g_str_hash, g_str_equal);
	data_factory->priv->backend_factories = g_hash_table_new (g_str_hash, g_str_equal);

	data_factory->priv->books_lock = g_mutex_new ();
	data_factory->priv->books = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	data_factory->priv->connections_lock = g_mutex_new ();
	data_factory->priv->connections = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	e_data_server_module_init ();
}

/* FIXME: write dispose to kill all the hashes and locks in priv */

static gchar *
e_data_factory_extract_proto_from_uri (const gchar *uri)
{
	gchar *proto, *p;
	p = strchr (uri, ':');
	if (p == NULL)
		return NULL;
	proto = g_malloc0 (p - uri + 1);
	strncpy (proto, uri, p - uri);
	return proto;
}

static EBackendFactory*
e_data_factory_lookup_backend_factory (EDataFactory *data_factory,
				       const gchar  *uri)
{
	EBackendFactory *backend_factory;
	char *proto;

	g_return_val_if_fail (E_IS_DATA_FACTORY (data_factory), NULL);
	g_return_val_if_fail (uri != NULL, NULL);

	proto = e_data_factory_extract_proto_from_uri (uri);
	if (proto == NULL) {
		g_warning ("Cannot extract protocol from URI %s", uri);
		return NULL;
	}

	backend_factory = g_hash_table_lookup (data_factory->priv->backend_factories, proto);

	g_free (proto);

	return backend_factory;
}

static gchar *
construct_factory_path (EDataFactory *data_factory)
{
	static volatile gint counter = 1;
        EDataFactoryClass *klass;

        klass = E_DATA_FACTORY_GET_CLASS (data_factory);

        g_assert (klass->get_dbus_name_format);

	return g_strdup_printf (
		klass->get_dbus_name_format (data_factory),
		getpid (),
		g_atomic_int_exchange_and_add (&counter, 1));
}

typedef struct {
	gchar *path;
	EDataFactory *factory;
} DataWeakNotifyClosure;

static void
data_weak_notify_closure_free (DataWeakNotifyClosure *closure)
{
	g_free (closure->path);
	g_object_unref (closure->factory);
	g_free (closure);
}

static void
my_remove (DataWeakNotifyClosure *closure, GObject *dead)
{
	EDataFactoryPrivate *priv = closure->factory->priv;
	GHashTableIter iter;
	gpointer hkey, hvalue;

	d (g_debug ("%s (%p) is dead", closure->path, dead));

	g_hash_table_remove (priv->books, closure->path);

	g_hash_table_iter_init (&iter, priv->connections);
	while (g_hash_table_iter_next (&iter, &hkey, &hvalue)) {
		GList *books = hvalue;

		if (g_list_find (books, dead)) {
			books = g_list_remove (books, dead);
			if (books) {
				g_hash_table_insert (priv->connections, g_strdup (hkey), books);
			} else {
				g_hash_table_remove (priv->connections, hkey);
			}

			break;
		}
	}

	data_weak_notify_closure_free (closure);

	/* If there are no open books, start a timer to quit */
	if (priv->exit_timeout == 0 && g_hash_table_size (priv->books) == 0) {
		priv->exit_timeout = g_timeout_add (10000, (GSourceFunc)g_main_loop_quit, loop);
	}
}

void
e_data_factory_publish_data (EDataFactory *data_factory, const gchar *IN_source, DBusGMethodInvocation *context)
{
        EDataFactoryClass *klass;
	EData *data;
	EBackend *backend;
	EDataFactoryPrivate *priv = data_factory->priv;
	ESource *source;
	gchar *uri, *path, *sender;
	GList *list;
	DataWeakNotifyClosure *closure;

	klass = E_DATA_FACTORY_GET_CLASS (data_factory);

	/* FIXME: the status types (eg, NO_SUCH_DATA) need to be translated to
	 * something that makes sense for the class */
	if (IN_source == NULL || IN_source[0] == '\0') {
		dbus_g_method_return_error (context, g_error_new (E_DATA_ERROR, E_DATA_STATUS_NO_SUCH_DATA, _("Empty URI")));
		return;
	}

	/* Remove a pending exit */
	if (priv->exit_timeout) {
		g_source_remove (priv->exit_timeout);
		priv->exit_timeout = 0;
	}

	g_mutex_lock (priv->backends_lock);

	source = e_source_new_from_standalone_xml (IN_source);
	if (!source) {
		g_mutex_unlock (priv->backends_lock);
		dbus_g_method_return_error (context, g_error_new (E_DATA_ERROR, E_DATA_STATUS_NO_SUCH_DATA, _("Invalid source")));
		return;
	}

	uri = e_source_get_uri (source);

	g_mutex_lock (priv->books_lock);

	backend = g_hash_table_lookup (priv->backends, uri);

	if (!backend) {
		EBackendFactory *backend_factory = e_data_factory_lookup_backend_factory (data_factory, uri);

		if (backend_factory)
			backend = e_backend_factory_new_backend (backend_factory);

		if (backend) {
			g_hash_table_insert (priv->backends,
					g_strdup (uri), backend);

			e_backend_set_mode (backend, priv->mode);
		}
	}

	if (!backend) {
		g_free (uri);
		g_object_unref (source);

		g_mutex_unlock (priv->books_lock);
		g_mutex_unlock (priv->backends_lock);
		dbus_g_method_return_error (context, g_error_new (E_DATA_ERROR, E_DATA_STATUS_NO_SUCH_DATA, _("Invalid source")));
		return;
	}

	path = construct_factory_path (data_factory);

	g_assert (klass->data_new);
	data = klass->data_new (backend, source);

	g_hash_table_insert (priv->books, g_strdup (path), data);
	e_backend_add_client (backend, data);
	dbus_g_connection_register_g_object (connection, path, G_OBJECT (data));

	closure = g_new0 (DataWeakNotifyClosure, 1);
	closure->path = g_strdup (path);
	closure->factory = g_object_ref (data_factory);
	g_object_weak_ref (G_OBJECT (data), (GWeakNotify)my_remove, closure);

	/* Update the hash of open connections */
	g_mutex_lock (priv->connections_lock);
	sender = dbus_g_method_get_sender (context);
	list = g_hash_table_lookup (priv->connections, sender);
	list = g_list_prepend (list, data);
	g_hash_table_insert (priv->connections, sender, list);
	g_mutex_unlock (priv->connections_lock);

	g_mutex_unlock (priv->books_lock);
	g_mutex_unlock (priv->backends_lock);

	g_object_unref (source);
	g_free (uri);

	dbus_g_method_return (context, path);
}

static void
name_owner_changed (DBusGProxy *proxy,
                    const gchar *name,
                    const gchar *prev_owner,
                    const gchar *new_owner,
                    EDataFactory *data_factory)
{
	if (strcmp (new_owner, "") == 0 && strcmp (name, prev_owner) == 0) {
		gchar *key;
		GList *list = NULL;
		g_mutex_lock (data_factory->priv->connections_lock);
		while (g_hash_table_lookup_extended (data_factory->priv->connections, prev_owner, (gpointer)&key, (gpointer)&list)) {
			/* this should trigger the data's weak ref notify
			 * function, which will remove it from the list before
			 * it's freed, and will remove the connection from
			 * priv->connections once they're all gone */
			g_object_unref (list->data);
		}

		g_mutex_unlock (data_factory->priv->connections_lock);
	}
}

/* Convenience function to print an error and exit */
G_GNUC_NORETURN static void
die (const gchar *prefix, GError *error)
{
	g_error("%s: %s", prefix, error->message);
	g_error_free (error);
	exit(1);
}

static void
offline_state_changed_cb (EOfflineListener *eol, EDataFactory *data_factory)
{
	EOfflineListenerState state = e_offline_listener_get_state (eol);

	g_return_if_fail (state == EOL_STATE_ONLINE || state == EOL_STATE_OFFLINE);

	e_data_factory_set_backend_mode (data_factory, state == EOL_STATE_ONLINE ? E_DATA_MODE_REMOTE : E_DATA_MODE_LOCAL);
}

gint
e_data_factory_main (gint           argc,
 		     gchar        **argv,
		     EDataFactory  *factory,
		     GMainLoop     *loop_in,
		     const char    *service_name,
		     const char    *object_path)
{
	GError *error = NULL;
	DBusGProxy *bus_proxy;
	guint32 request_name_ret;
	EOfflineListener *eol;
        EDataFactoryClass *klass;

	g_return_val_if_fail (E_IS_DATA_FACTORY (factory), -1);
	g_return_val_if_fail (loop_in, -1);
	g_return_val_if_fail (service_name && service_name[0], -1);
	g_return_val_if_fail (object_path && object_path[0], -1);

	dbus_g_thread_init ();

	loop = loop_in;

	/* Obtain a connection to the session bus */
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (connection == NULL)
		die ("Failed to open connection to bus", error);

	bus_proxy = dbus_g_proxy_new_for_name (connection,
					       DBUS_SERVICE_DBUS,
					       DBUS_PATH_DBUS,
					       DBUS_INTERFACE_DBUS);

	if (!org_freedesktop_DBus_request_name (bus_proxy, service_name,
						0, &request_name_ret, &error))
		die ("Failed to get name", error);

	if (request_name_ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		g_error ("Got result code %u from requesting name", request_name_ret);
		exit (1);
	}

	klass = E_DATA_FACTORY_GET_CLASS (factory);

	g_assert (klass->get_backend_type);
	register_backend_factories (factory, klass->get_backend_type());

	dbus_g_connection_register_g_object (connection,
					     object_path,
					     G_OBJECT (factory));

	dbus_g_proxy_add_signal (bus_proxy, "NameOwnerChanged",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (bus_proxy, "NameOwnerChanged", G_CALLBACK (name_owner_changed), factory, NULL);

	eol = e_offline_listener_new ();
	offline_state_changed_cb (eol, factory);
	g_signal_connect (eol, "changed", G_CALLBACK (offline_state_changed_cb), factory);

	printf ("Server is up and running...\n");

	g_main_loop_run (loop);

	g_object_unref (eol);

	dbus_g_connection_unref (connection);

	printf ("Bye.\n");

	return 0;
}
