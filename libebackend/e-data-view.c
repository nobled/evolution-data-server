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
};

enum {
	PROP_NONE,
	PROP_BACKEND,
	PROP_DATA,
	PROP_DBUS_PATH,
	PROP_SEXP,
};

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

	G_OBJECT_CLASS (e_data_view_parent_class)->dispose (object);
}

static void
e_data_view_finalize (GObject *object)
{
	EDataView *view = E_DATA_VIEW (object);
	EDataViewPrivate *priv = view->priv;

	g_free (priv->dbus_path);

	G_OBJECT_CLASS (e_data_view_parent_class)->finalize (object);
}

static void
e_data_view_constructed (GObject *object)
{
	EDataView *view = E_DATA_VIEW (object);

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

	g_type_class_add_private (klass, sizeof (EDataViewPrivate));
}

static void
e_data_view_init (EDataView *view)
{
	view->priv = E_DATA_VIEW_GET_PRIVATE (view);
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
