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
 * Author: Ross Burton <ross@linux.intel.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>

#include "e-data-view.h"

extern DBusGConnection *connection;

#define THRESHOLD 32

G_DEFINE_ABSTRACT_TYPE (EDataView, e_data_view, G_TYPE_OBJECT);

#define E_DATA_VIEW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), E_TYPE_DATA_VIEW, EDataViewPrivate))

struct _EDataViewPrivate {
	EData *data;
	EBackend *backend;

	EBackendSExp *sexp;

	gchar *dbus_path;

	GArray *adds;
	GArray *modifications;
	GArray *removes;

	GHashTable *ids;

	gboolean done;
	guint done_status;
};

enum {
	PROP_NONE,
	PROP_BACKEND,
	PROP_DATA,
	PROP_DBUS_PATH,
	PROP_SEXP,
};

enum {
	OBJECTS_ADDED,
	OBJECTS_MODIFIED,
	OBJECTS_REMOVED,
	DONE,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
data_destroyed_cb (gpointer data, GObject *dead)
{
	EDataView *view = E_DATA_VIEW (data);
        EDataViewClass *klass;

        klass = E_DATA_VIEW_GET_CLASS (view);

	/* The data has just died, so unset the pointer so we don't try and
	 * remove a dead weak reference. */
	view->priv->data = NULL;

	g_assert (klass->stop_if_running);
	klass->stop_if_running (view);
}

static void
e_data_view_get_property (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
	EDataViewPrivate *priv = E_DATA_VIEW (object)->priv;

	switch (property_id)
	{
		case PROP_BACKEND:
			g_value_set_object (value, priv->backend);
		break;

		case PROP_DATA:
			g_value_set_object (value, priv->data);
		break;

		case PROP_DBUS_PATH:
			g_value_set_string (value, priv->dbus_path);
		break;

		case PROP_SEXP:
			g_value_set_object (value, priv->sexp);
		break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
e_data_view_set_property (GObject      *object,
			  guint         property_id,
			  const GValue *value,
			  GParamSpec   *pspec)
{
	EDataViewPrivate *priv;
	EDataView *view;

	view = E_DATA_VIEW (object);
	priv = view->priv;

	switch (property_id)
	{
		/* XXX: this property is only independent of the "data" property
		 * because it would break compatability with EDataCalView (which
		 * doesn't provide a "data" property value); it would be a bit
		 * cleaner to make this small break, though */
		case PROP_BACKEND:
			/* make the "data" value override the "backend" value
			 * for priv->backend to avoid a conflict for
			 * EDataBookView */
			if (priv->data)
				break;

			if (priv->backend)
				g_object_unref (priv->backend);

			priv->backend = g_value_dup_object (value);
		break;

		case PROP_DATA:
			if (priv->data)
				g_object_weak_unref (G_OBJECT (priv->data), data_destroyed_cb, view);

			priv->data = g_value_get_object (value);

			if (priv->data) {
				/* Attach a weak reference to the data, so if it
				 * dies the data view is destroyed too */
				g_object_weak_ref (G_OBJECT (priv->data), data_destroyed_cb, view);

				if (priv->backend)
					g_object_unref (priv->backend);

				priv->backend = g_object_ref (e_data_get_backend (priv->data));
			}
		break;

		case PROP_DBUS_PATH:
			priv->dbus_path = g_value_dup_string (value);
		break;

		case PROP_SEXP:
			priv->sexp = g_value_dup_object (value);
		break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
e_data_view_dispose (GObject *object)
{
	EDataView *view = E_DATA_VIEW (object);
	EDataViewPrivate *priv = view->priv;

	if (priv->data) {
		/* Remove the weak reference */
		g_object_weak_unref (G_OBJECT (priv->data), data_destroyed_cb, view);
		priv->data = NULL;
	}

	if (priv->backend) {
		e_backend_remove_view (priv->backend, view);
		g_object_unref (priv->backend);
		priv->backend = NULL;
	}

	if (priv->sexp) {
		g_object_unref (priv->sexp);
		priv->sexp = NULL;
	}

	if (priv->adds) {
		g_array_unref (priv->adds);
		priv->adds = NULL;
	}

	if (priv->modifications) {
		g_array_unref (priv->modifications);
		priv->modifications = NULL;
	}

	if (priv->removes) {
		g_array_unref (priv->removes);
		priv->removes = NULL;
	}

	G_OBJECT_CLASS (e_data_view_parent_class)->dispose (object);
}

static void
e_data_view_finalize (GObject *object)
{
	EDataView *view = E_DATA_VIEW (object);
	EDataViewPrivate *priv = view->priv;

	g_free (priv->dbus_path);

	g_hash_table_destroy (priv->ids);

	G_OBJECT_CLASS (e_data_view_parent_class)->finalize (object);
}

static void
e_data_view_constructed (GObject *object)
{
	EDataView *view = E_DATA_VIEW (object);
        EDataViewClass *klass;
	GHashFunc id_hash;
	GEqualFunc id_equal;
	GDestroyNotify id_destroy;

        klass = E_DATA_VIEW_GET_CLASS (view);

	id_hash = klass->id_hash ? klass->id_hash : g_str_hash;
	id_equal = klass->id_equal ? klass->id_equal : g_str_equal;
	id_destroy = klass->id_destroy ? klass->id_destroy : g_free;

        view->priv->ids = g_hash_table_new_full (id_hash, id_equal, id_destroy, NULL);

	dbus_g_connection_register_g_object (connection, view->priv->dbus_path, G_OBJECT (view));
}

static void
e_data_view_class_init (EDataViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = e_data_view_dispose;
	object_class->finalize = e_data_view_finalize;
	object_class->constructed = e_data_view_constructed;
	object_class->get_property = e_data_view_get_property;
	object_class->set_property = e_data_view_set_property;

        g_object_class_install_property
                (object_class,
                 PROP_DATA,
                 g_param_spec_object
                         ("data",
                          "The backing EData",
                          "The backing EData to base this view upon.",
                          E_TYPE_DATA,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

        g_object_class_install_property
                (object_class,
                 PROP_BACKEND,
                 g_param_spec_object
                         ("backend",
                          "The backend of the view",
                          "The backing EBackend to base this view upon.",
                          E_TYPE_BACKEND,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

        g_object_class_install_property
                (object_class,
                 PROP_DBUS_PATH,
                 g_param_spec_string
                         ("dbus-path",
                          "D-Bus Object Path",
                          "D-Bus object path of this object.",
                          "",
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

        g_object_class_install_property
                (object_class,
                 PROP_SEXP,
                 g_param_spec_object
                         ("sexp",
                          "The query as an EBackendSExp",
                          "Structured EBackendSExp version of the query.",
                          E_TYPE_BACKEND_SEXP,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

        signals[OBJECTS_ADDED] =
		g_signal_new ("objects-added",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EDataViewClass, objects_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE, 1, G_TYPE_STRV);

        signals[OBJECTS_MODIFIED] =
		g_signal_new ("objects-modified",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EDataViewClass, objects_modified),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE, 1, G_TYPE_STRV);

        signals[OBJECTS_REMOVED] =
		g_signal_new ("objects-removed",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EDataViewClass, objects_removed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE, 1, G_TYPE_STRV);

        signals[DONE] =
		g_signal_new ("done",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);


	g_type_class_add_private (klass, sizeof (EDataViewPrivate));
}

static void
e_data_view_init (EDataView *view)
{
	EDataViewPrivate *priv = E_DATA_VIEW_GET_PRIVATE (view);

	view->priv = priv;

        priv->adds = g_array_sized_new (TRUE, TRUE, sizeof (gchar *), THRESHOLD);
        priv->modifications = g_array_sized_new (TRUE, TRUE, sizeof (gchar *), THRESHOLD);
        priv->removes = g_array_sized_new (TRUE, TRUE, sizeof (gchar *), THRESHOLD);

	priv->done = FALSE;
	priv->done_status = 0;
}

/**
 * e_data_view_get_sexp:
 * @view: an #EDataView
 *
 * Gets the s-expression used for matching contacts to
 * @view.
 *
 * Return value: The #EBackendSExp used.
 **/
EBackendSExp*
e_data_view_get_sexp (EDataView *view)
{
	g_return_val_if_fail (E_IS_DATA_VIEW (view), NULL);

	return view->priv->sexp;
}

/**
 * e_data_view_get_backend:
 * @view: an #EDataView
 *
 * Gets the backend that @view is querying.
 *
 * Return value: The associated #EBackend.
 **/
EBackend*
e_data_view_get_backend (EDataView *view)
{
	g_return_val_if_fail (E_IS_DATA_VIEW (view), NULL);

	return view->priv->backend;
}

/**
 * e_data_view_get_dbus_path:
 * @view: an #EDataView
 *
 * Gets the D-Bus object path for @view.
 *
 * Return value: The D-Bus path.
 **/
const gchar*
e_data_view_get_dbus_path (EDataView *view)
{
	g_return_val_if_fail (E_IS_DATA_VIEW (view), NULL);

	return view->priv->dbus_path;
}

static void
reset_array (GArray *array)
{
        gint i = 0;
        gchar *tmp = NULL;

        /* Free stored strings */
        for (i = 0; i < array->len; i++) {
                tmp = g_array_index (array, gchar *, i);
                g_free (tmp);
        }

        /* Force the array size to 0 */
        g_array_set_size (array, 0);
}

void
e_data_view_send_pending_adds (EDataView *view)
{
        EDataViewPrivate *priv = view->priv;

        if (priv->adds->len == 0)
                return;

        g_signal_emit (view, signals[OBJECTS_ADDED], 0, priv->adds->data);
        reset_array (priv->adds);
}

void
e_data_view_send_pending_modifications (EDataView *view)
{
        EDataViewPrivate *priv = view->priv;

        if (priv->modifications->len == 0)
                return;

        g_signal_emit (view, signals[OBJECTS_MODIFIED], 0, priv->modifications->data);
        reset_array (priv->modifications);
}

void
e_data_view_send_pending_removes (EDataView *view)
{
        EDataViewPrivate *priv = view->priv;

        if (priv->removes->len == 0)
                return;

        /* TODO: send ECalComponentIds as a list of pairs */
        g_signal_emit (view, signals[OBJECTS_REMOVED], 0, priv->removes->data);
        reset_array (priv->removes);
}

gboolean
e_data_view_contains_object (EDataView *view,
			     gpointer   id)
{
	g_return_val_if_fail (E_IS_DATA_VIEW (view), FALSE);

	return g_hash_table_lookup (view->priv->ids, id) != NULL;
}

void
e_data_view_notify_object_add (EDataView   *view,
			       gpointer     id,
			       const gchar *obj)
{
        EDataViewPrivate *priv;
        EDataViewClass *klass;

	g_return_if_fail (E_IS_DATA_VIEW (view));

        priv = view->priv;
        klass = E_DATA_VIEW_GET_CLASS (view);

        e_data_view_send_pending_modifications (view);
        e_data_view_send_pending_removes (view);

        if (priv->adds->len == THRESHOLD) {
                e_data_view_send_pending_adds (view);
        }

	if (e_data_view_contains_object (view, id)) {
		if (klass->id_destroy) {
			klass->id_destroy (id);
		} else {
			g_free (id);
		}

		return;
	}

        g_array_append_val (priv->adds, obj);

        g_hash_table_insert (priv->ids, id, GUINT_TO_POINTER (1));
}

void
e_data_view_notify_object_modification (EDataView *view,
					gchar     *obj)
{
        EDataViewPrivate *priv;

	g_return_if_fail (E_IS_DATA_VIEW (view));

        priv = view->priv;

        e_data_view_send_pending_adds (view);
        e_data_view_send_pending_removes (view);

        g_array_append_val (priv->modifications, obj);
}

void
e_data_view_notify_object_remove (EDataView *view,
				  gpointer   id)
{
        EDataViewPrivate *priv;
        gchar *uid;
        EDataViewClass *klass;

	g_return_if_fail (E_IS_DATA_VIEW (view));

        priv = view->priv;
        klass = E_DATA_VIEW_GET_CLASS (view);

	if (!e_data_view_contains_object (view, id)) {
		g_warning (G_STRLOC ": view does not contain id %p", id);
		return;
	}

        e_data_view_send_pending_adds (view);
        e_data_view_send_pending_modifications (view);

	if (klass->id_get_str_id) {
		uid = g_strdup (klass->id_get_str_id (id));
	} else {
		uid = g_strdup ((const gchar*) id);
	}

        g_array_append_val (priv->removes, uid);

        g_hash_table_remove (priv->ids, id);
}

void
e_data_view_notify_done (EDataView *view,
			 guint      status)
{
        e_data_view_send_pending_adds (view);
        e_data_view_send_pending_modifications (view);
        e_data_view_send_pending_removes (view);

        g_signal_emit (view, signals[DONE], 0, status);
}

/**
 * e_data_view_is_done:
 * @view: An #EDataView
 *
 * Checks whether the given view is already done. Being done means the initial
 * matching of objects have been finished, not that no more notifications about
 * changes will be sent. In fact, even after done, notifications will still be
 * sent if there are changes in the objects matching the query search
 * expression.
 *
 * Return value: TRUE if the view is done, FALSE if still in progress.
 */
gboolean
e_data_view_is_done (EDataView *view)
{
	g_return_val_if_fail (E_IS_DATA_VIEW (view), FALSE);

	return view->priv->done;
}

/**
 * e_data_view_get_done_status:
 * @view: An #EDataView
 *
 * Gets the status code obtained when the initial matching of objects was done
 * for the given view.
 *
 * Return value: The view status.
 */
guint
e_data_view_get_done_status (EDataView *view)
{
	g_return_val_if_fail (E_IS_DATA_VIEW (view), FALSE);

	return view->priv->done_status;
}
