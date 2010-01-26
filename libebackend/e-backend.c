/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors:
 *   Nat Friedman <nat@ximian.com>
 *   Travis Reitter <travis.reitter@collabora.co.uk>
 */

#include <config.h>

#include "e-data-view.h"
#include "e-data.h"
#include "e-data-types.h"
#include "e-backend.h"

struct _EBackendPrivate {
	GMutex *open_mutex;

	GMutex *clients_mutex;
	GList *clients;

	ESource *source;

	GMutex *views_mutex;
	EList *views;
};

/* Signal IDs */
enum {
	LAST_CLIENT_GONE,
	LAST_SIGNAL
};

static guint e_backend_signals[LAST_SIGNAL];

static GObjectClass *parent_class;

G_DEFINE_ABSTRACT_TYPE (EBackend, e_backend, G_TYPE_OBJECT);

/**
 * e_backend_get_source:
 * @backend: An #EBackend.
 *
 * Queries the source that @backend is serving.
 *
 * Return value: #ESource for the backend.
 **/
ESource *
e_backend_get_source (EBackend *backend)
{
	g_return_val_if_fail (E_IS_BACKEND (backend), NULL);

	return backend->priv->source;
}

/**
 * e_backend_set_source:
 * @backend: An #EBackend.
 * @source: The #ESource.
 *
 * Sets the source that @backend is serving.
 *
 * Return value: #ESource for the backend.
 **/
void
e_backend_set_source (EBackend *backend,
		      ESource  *source)
{
	g_return_if_fail (E_IS_BACKEND (backend));
	g_return_if_fail (E_IS_SOURCE (source));

	if (backend->priv->source)
		g_object_unref (backend->priv->source);

	backend->priv->source = g_object_ref (backend);
}

/**
 * e_book_backend_stop_view:
 * @backend: an #EBackend
 * @view: the #EDataView to stop
 *
 * Stops running the query specified by @view, emitting
 * no more signals.
 **/
void
e_backend_stop_view (EBackend  *backend,
		     EDataView *view)
{
	EBackendClass *klass;

	g_return_if_fail (E_IS_BACKEND (backend));

	klass = E_BACKEND_GET_CLASS (backend);

	g_assert (klass->stop_view);

	return klass->stop_view (backend, view);
}

/**
 * e_backend_remove:
 * @backend: an #EBackend
 * @data: an #EData
 * @opid: the ID to use for this operation
 *
 * Executes a 'remove' request to remove all of @backend's data,
 * specified by @opid on @data.
 **/
void
e_backend_remove (EBackend *backend,
		  EData    *data,
		  guint32   opid)
{
	EBackendClass *klass;

	g_return_if_fail (E_IS_BACKEND (backend));
	g_return_if_fail (E_IS_DATA (data));

	klass = E_BACKEND_GET_CLASS (data);

	g_assert (klass->remove);

	klass->remove (backend, data, opid);
}

/**
 * e_backend_get_changes:
 * @backend: an #EBackend
 * @data: an #EData
 * @opid: the ID to use for this operation
 * @change_id: the ID of the changeset
 *
 * Executes a 'get changes' request specified by @opid on @data
 * using @backend.
 **/
void
e_backend_get_changes (EBackend    *backend,
		       EData       *data,
		       guint32      opid,
		       const gchar *change_id)
{
	g_return_if_fail (E_IS_BACKEND (backend));
	g_return_if_fail (E_IS_DATA (data));
	g_return_if_fail (change_id);

	g_assert (E_BACKEND_GET_CLASS (backend)->get_changes);

	(* E_BACKEND_GET_CLASS (backend)->get_changes) (backend, data, opid, change_id);
}

static void
last_client_gone (EBackend *backend)
{
	g_signal_emit (backend, e_backend_signals[LAST_CLIENT_GONE], 0);
}

/**
 * e_backend_get_views:
 * @backend: an #EBackend
 *
 * Gets the list of #EDataView views running on this backend.
 *
 * Return value: An #EList of #EDataView objects.
 **/
EList*
e_backend_get_views (EBackend *backend)
{
        g_return_val_if_fail (E_IS_BACKEND (backend), NULL);

        return g_object_ref (backend->priv->views);
}

/**
 * e_backend_add_view:
 * @backend: an #EBackend
 * @view: an #EDataView
 *
 * Adds @view to @backend for querying.
 **/
void
e_backend_add_view (EBackend  *backend,
		    EDataView *view)
{
	g_return_if_fail (E_IS_BACKEND (backend));

	g_mutex_lock (backend->priv->views_mutex);

	e_list_append (backend->priv->views, view);

	g_mutex_unlock (backend->priv->views_mutex);
}

/**
 * e_backend_remove_view:
 * @backend: an #EBackend
 * @view: an #EDataView
 *
 * Removes @view from @backend.
 **/
void
e_backend_remove_view (EBackend  *backend,
		       EDataView *view)
{
	g_return_if_fail (E_IS_BACKEND (backend));

	g_mutex_lock (backend->priv->views_mutex);

	e_list_remove (backend->priv->views, view);

	g_mutex_unlock (backend->priv->views_mutex);
}

/**
 * e_backend_add_client:
 * @backend: An addressbook backend.
 * @data: the corba object representing the client connection.
 *
 * Adds a client to an addressbook backend.
 *
 * Return value: TRUE on success, FALSE on failure to add the client.
 */
gboolean
e_backend_add_client (EBackend *backend,
		      EData    *data)
{
	g_return_val_if_fail (E_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (E_IS_DATA (data), FALSE);

	g_mutex_lock (backend->priv->clients_mutex);
	backend->priv->clients = g_list_prepend (backend->priv->clients, data);
	g_mutex_unlock (backend->priv->clients_mutex);

	return TRUE;
}

/**
 * e_backend_remove_client:
 * @backend: an #EBackend
 * @data: an #EData to remove
 *
 * Removes @data from the list of @backend's clients.
 **/
void
e_backend_remove_client (EBackend *backend,
			 EData    *data)
{
	g_return_if_fail (E_IS_BACKEND (backend));
	g_return_if_fail (E_IS_DATA (data));

	/* up our backend's refcount here so that last_client_gone
	   doesn't end up unreffing us (while we're holding the
	   lock) */
	g_object_ref (backend);

	/* Disconnect */
	g_mutex_lock (backend->priv->clients_mutex);
	backend->priv->clients = g_list_remove (backend->priv->clients, data);

	/* When all clients go away, notify the parent factory about it so that
	 * it may decide whether to kill the backend or not.
	 */
	if (!backend->priv->clients)
		last_client_gone (backend);

	g_mutex_unlock (backend->priv->clients_mutex);

	g_object_unref (backend);
}

/**
 * e_backend_get_clients:
 * @backend: an #EBackend
 *
 * Returns a #GList of #EData clients. This list and its members are owned by
 * @backend. Be sure to also lock and unlock the mutex from
 * @e_backend_get_clients_mutex() as appropriately when using this list of
 * clients.
 **/
GList*
e_backend_get_clients (EBackend *backend)
{
	g_return_val_if_fail (E_IS_BACKEND (backend), NULL);

	return backend->priv->clients;
}

/**
 * e_backend_get_clients_mutex:
 * @backend: an #EBackend
 *
 * Returns the #GMutex used to regulate access of the clients list and the
 * clients themselves. The mutex is owned by @backend.
 **/
GMutex*
e_backend_get_clients_mutex (EBackend *backend)
{
	g_return_val_if_fail (E_IS_BACKEND (backend), NULL);

	return backend->priv->clients_mutex;
}

/**
 * e_backend_get_static_capabilities:
 * @backend: an #EBackend
 *
 * Gets the capabilities offered by this @backend.
 *
 * Return value: A string listing the capabilities.
 **/
gchar *
e_backend_get_static_capabilities (EBackend *backend)
{
	EBackendClass *klass;

	g_return_val_if_fail (E_IS_BACKEND (backend), NULL);

	klass = E_BACKEND_GET_CLASS (backend);

	g_assert (klass->get_static_capabilities);

	return klass->get_static_capabilities (backend);
}

/**
 * e_backend_is_loaded:
 * @backend: an #EBackend
 *
 * Checks if @backend's storage has been opened and the backend
 * itself is ready for accessing.
 *
 * Return value: %TRUE if loaded, %FALSE otherwise.
 **/
gboolean
e_backend_is_loaded (EBackend *backend)
{
	EBackendClass *klass;

	g_return_val_if_fail (E_IS_BACKEND (backend), FALSE);

	klass = E_BACKEND_GET_CLASS (backend);

	g_assert (klass->is_loaded);

	return klass->is_loaded (backend);
}

/**
 * e_backend_set_mode:
 * @backend: an #EBackend
 * @mode: a mode indicating the online/offline status of the backend
 *
 * Sets @backend's online/offline mode to @mode. Mode can be 1 for offline
 * or 2 indicating that it is connected and online.
 **/
void
e_backend_set_mode (EBackend  *backend,
		    EDataMode  mode)
{
	EBackendClass *klass;

	g_return_if_fail (E_IS_BACKEND (backend));

	klass = E_BACKEND_GET_CLASS (backend);

	g_assert (klass->set_mode);

	klass->set_mode (backend, mode);
}

/**
 * e_backend_notify_auth_required:
 * @backend: an #EBackend
 *
 * Notifies clients that @backend requires authentication in order to
 * connect. Means to be used by backend implementations.
 **/
void
e_backend_notify_auth_required (EBackend *backend)
{
	EBackendPrivate *priv;
	GList *clients;

	priv = backend->priv;
	g_mutex_lock (priv->clients_mutex);

	for (clients = priv->clients; clients != NULL; clients = g_list_next (clients))
		e_data_notify_auth_required (E_DATA (clients->data));
	g_mutex_unlock (priv->clients_mutex);
}

static void
e_backend_init (EBackend *backend)
{
	EBackendPrivate *priv;

	priv          = g_new0 (EBackendPrivate, 1);
	priv->clients = NULL;
	priv->source = NULL;
	priv->views   = e_list_new((EListCopyFunc) NULL, (EListFreeFunc) NULL, NULL);
	priv->open_mutex = g_mutex_new ();
	priv->clients_mutex = g_mutex_new ();
	priv->views_mutex = g_mutex_new ();

	backend->priv = priv;
}

static void
e_backend_dispose (GObject *object)
{
	EBackend *backend;

	backend = E_BACKEND (object);

	if (backend->priv) {
		g_list_free (backend->priv->clients);

		if (backend->priv->views) {
			g_object_unref (backend->priv->views);
			backend->priv->views = NULL;
		}

		if (backend->priv->source) {
			g_object_unref (backend->priv->source);
			backend->priv->source = NULL;
		}

		g_mutex_free (backend->priv->open_mutex);
		g_mutex_free (backend->priv->clients_mutex);
		g_mutex_free (backend->priv->views_mutex);

		g_free (backend->priv);
		backend->priv = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
e_backend_class_init (EBackendClass *klass)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (klass);

	object_class = (GObjectClass *) klass;

	object_class->dispose = e_backend_dispose;

	e_backend_signals[LAST_CLIENT_GONE] =
		g_signal_new ("last_client_gone",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (EBackendClass, last_client_gone),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}
