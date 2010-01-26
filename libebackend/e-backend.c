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

	/* signal handler ID for source's 'changed' signal */
	gulong source_changed_id;

	/* URI, from source. This is cached, since we return const. */
	gchar *uri;
};

#define E_BACKEND_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), E_TYPE_BACKEND, EBackendPrivate))

/* Property IDs */
enum props {
        PROP_0,
        PROP_SOURCE,
        PROP_URI,
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

static void
source_changed_cb (ESource  *source,
		   EBackend *backend)
{
	EBackendPrivate *priv;
	gchar *suri;

	g_return_if_fail (source != NULL);
	g_return_if_fail (E_IS_BACKEND (backend));

	priv = backend->priv;
	g_return_if_fail (priv != NULL);
	g_return_if_fail (e_backend_get_source (backend) == source);

	suri = e_source_get_uri (e_backend_get_source (backend));
	if (!priv->uri || (suri && !g_str_equal (priv->uri, suri))) {
		g_free (priv->uri);
		priv->uri = suri;
	} else {
		g_free (suri);
	}
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
	EBackendPrivate *priv;

	g_return_if_fail (E_IS_BACKEND (backend));
	g_return_if_fail (E_IS_SOURCE (source) || !source);

	g_return_if_fail (E_IS_BACKEND (backend));

	priv = backend->priv;
	if (priv->source_changed_id && priv->source) {
		g_signal_handler_disconnect (priv->source, priv->source_changed_id);
		priv->source_changed_id = 0;
	}

	if (priv->source)
		g_object_unref (priv->source);

	if (source) {
		priv->source = g_object_ref (source);
		priv->source_changed_id = g_signal_connect (source, "changed", G_CALLBACK (source_changed_cb), backend);

		/* Cache the URI */
		g_free (priv->uri);
		priv->uri = e_source_get_uri (priv->source);
	}
}

/**
 * e_backend_get_uri:
 * @backend: An #EBackend.
 *
 * Gets the URI of the source that @backend is serving.
 *
 * Return value: uri for the backend.
 **/
const gchar*
e_backend_get_uri (EBackend *backend)
{
	g_return_val_if_fail (E_IS_BACKEND (backend), NULL);

	return backend->priv->uri;
}

/**
 * e_backend_set_uri:
 * @backend: An #EBackend.
 * @uri: The URI of the source @backend is using.
 *
 * Sets the source that @backend is serving.
 **/
void
e_backend_set_uri (EBackend    *backend,
		   const gchar *uri)
{
	g_return_if_fail (E_IS_BACKEND (backend));

	if (!backend->priv->source) {
		g_free (backend->priv->uri);
		backend->priv->uri = g_strdup (uri);
	}
}

static void
last_client_gone (EBackend *backend)
{
	g_signal_emit (backend, e_backend_signals[LAST_CLIENT_GONE], 0);
}

/**
 * e_backend_get_views_mutex:
 * @backend: an #EBackend
 *
 * Returns the #GMutex used to regulate access of the vies list and the
 * views themselves. The mutex is owned by @backend.
 **/
GMutex*
e_backend_get_views_mutex (EBackend *backend)
{
	g_return_val_if_fail (E_IS_BACKEND (backend), NULL);

	return backend->priv->views_mutex;
}

/**
 * e_backend_get_views:
 * @backend: an #EBackend
 *
 * Gets the list of #EDataView views running on this backend. This list (but not
 * its members) must be g_object_unref()'d when you're done with it.
 *
 * Be sure to also lock and unlock the mutex from @e_backend_get_views_mutex()
 * as appropriate when using this list of views.
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
	g_return_if_fail (E_IS_DATA_VIEW (view));

	g_mutex_lock (backend->priv->views_mutex);

	e_list_append (backend->priv->views, g_object_ref (view));

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
	g_return_if_fail (E_IS_DATA_VIEW (view));

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

	g_return_if_fail (E_IS_BACKEND (backend));

	priv = backend->priv;
	g_mutex_lock (priv->clients_mutex);

	for (clients = priv->clients; clients != NULL; clients = g_list_next (clients))
		e_data_notify_auth_required (E_DATA (clients->data));
	g_mutex_unlock (priv->clients_mutex);
}

static void
e_backend_get_property (GObject      *object,
			guint         property_id,
			GValue       *value,
			GParamSpec   *pspec)
{
        EBackend *backend;
        EBackendPrivate *priv;

        backend = E_BACKEND (object);
        priv = backend->priv;

        switch (property_id) {
        case PROP_SOURCE:
                g_value_set_object (value, e_backend_get_source (backend));
                break;
        case PROP_URI:
                g_value_set_string (value, e_backend_get_uri (backend));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
                break;
        }
}

static void
e_backend_set_property (GObject      *object,
			guint         property_id,
			const GValue *value,
			GParamSpec   *pspec)
{
	EBackend *backend;
	EBackendPrivate *priv;

	backend = E_BACKEND (object);
	priv = backend->priv;

	switch (property_id) {
	case PROP_SOURCE:
		e_backend_set_source (backend, g_value_get_object (value));
		break;
	case PROP_URI:
		e_backend_set_uri (backend, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
e_backend_init (EBackend *backend)
{
	EBackendPrivate *priv;

	priv = E_BACKEND_GET_PRIVATE (backend);
	backend->priv = priv;

	priv->views = e_list_new((EListCopyFunc) NULL, (EListFreeFunc) g_object_unref, NULL);
	priv->open_mutex = g_mutex_new ();
	priv->clients_mutex = g_mutex_new ();
	priv->views_mutex = g_mutex_new ();
}

static void
e_backend_dispose (GObject *object)
{
	EBackend *backend;
	EBackendPrivate *priv;

	backend = E_BACKEND (object);
	priv = backend->priv;

	if (priv) {
		g_assert (priv->clients == NULL);

		if (priv->views) {
			g_object_unref (priv->views);
			priv->views = NULL;
		}

		if (priv->source_changed_id && priv->source) {
			g_signal_handler_disconnect (priv->source, priv->source_changed_id);
			priv->source_changed_id = 0;
		}

		if (priv->source) {
			g_object_unref (priv->source);
			priv->source = NULL;
		}

		g_mutex_free (priv->open_mutex);
		g_mutex_free (priv->clients_mutex);
		g_mutex_free (priv->views_mutex);

		g_free (priv);
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
	object_class->get_property = e_backend_get_property;
	object_class->set_property = e_backend_set_property;

	g_object_class_install_property
		(object_class, PROP_SOURCE,
		 g_param_spec_object
			("source",
			 NULL,
			 NULL,
			 E_TYPE_SOURCE,
			 G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property
		(object_class, PROP_URI,
		 g_param_spec_string
			("uri",
			 NULL,
			 NULL,
			 "",
			 G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	e_backend_signals[LAST_CLIENT_GONE] =
		g_signal_new ("last_client_gone",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (EBackendClass, last_client_gone),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (EBackendPrivate));
}
