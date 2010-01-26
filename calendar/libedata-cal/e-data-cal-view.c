/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - Live search query implementation
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2009 Intel Corporation
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          Ross Burton <ross@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <glib-object.h>

#include <libebackend/e-data-view.h>
#include "e-cal-backend-sexp.h"
#include "e-data-cal-view.h"
#include "e-data-cal-marshal.h"

extern DBusGConnection *connection;

static gboolean impl_EDataCalView_start (EDataCalView *query, GError **error);
#include "e-data-cal-view-glue.h"

#define THRESHOLD 32

struct _EDataCalViewPrivate {
	gboolean started;
};

G_DEFINE_TYPE (EDataCalView, e_data_cal_view, E_TYPE_DATA_VIEW);
#define E_DATA_CAL_VIEW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), E_DATA_CAL_VIEW_TYPE, EDataCalViewPrivate))

/* Signals */
enum {
	OBJECTS_ADDED,
	OBJECTS_MODIFIED,
	OBJECTS_REMOVED,
	PROGRESS,
	DONE,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
e_data_cal_view_stop_if_running (EDataCalView *query)
{
	/* Nothing for us to do here */
}

static void
e_data_cal_view_finalize (GObject *object)
{
	EDataCalView *query;
	EDataCalViewPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_QUERY (object));

	query = QUERY (object);
	priv = query->priv;

	(* G_OBJECT_CLASS (e_data_cal_view_parent_class)->finalize) (object);
}

static guint
id_hash (gconstpointer key)
{
	const ECalComponentId *id = key;
	return g_str_hash (id->uid) ^ (id->rid ? g_str_hash (id->rid) : 0);
}

static gboolean
id_equal (gconstpointer a, gconstpointer b)
{
	const ECalComponentId *id_a = a, *id_b = b;

	return (g_strcmp0 (id_a->uid, id_b->uid) == 0) &&
	       (g_strcmp0 (id_a->rid, id_b->rid) == 0);
}

static const gchar*
id_get_str_id (ECalComponentId *id)
{
	return id->uid;
}

/* Class init */
static void
e_data_cal_view_class_init (EDataCalViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	EDataViewClass *parent_class = E_DATA_VIEW_CLASS (klass);

	g_type_class_add_private (klass, sizeof (EDataCalViewPrivate));

	object_class->finalize = e_data_cal_view_finalize;

	parent_class->stop_if_running = (void (*)(EDataView*)) e_data_cal_view_stop_if_running;
	parent_class->id_hash = (GHashFunc) id_hash;
	parent_class->id_equal = (GEqualFunc) id_equal;
	parent_class->id_destroy = (GDestroyNotify) e_cal_component_free_id;
	parent_class->id_get_str_id = (const gchar* (*)(gconstpointer)) id_get_str_id;

        signals[OBJECTS_ADDED] = g_signal_lookup ("objects-added", E_TYPE_DATA_VIEW);
        signals[OBJECTS_MODIFIED] = g_signal_lookup ("objects-modified", E_TYPE_DATA_VIEW);
        signals[OBJECTS_REMOVED] = g_signal_lookup ("objects-removed", E_TYPE_DATA_VIEW);
        signals[DONE] = g_signal_lookup ("done", E_TYPE_DATA_VIEW);

        signals[PROGRESS] =
          g_signal_new ("progress",
                        G_OBJECT_CLASS_TYPE (klass),
                        G_SIGNAL_RUN_LAST,
                        0, NULL, NULL,
                        e_data_cal_marshal_VOID__STRING_UINT,
                        G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_UINT);

	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (klass), &dbus_glib_e_data_cal_view_object_info);
}

/* Instance init */
static void
e_data_cal_view_init (EDataCalView *view)
{
	EDataCalViewPrivate *priv = E_DATA_CAL_VIEW_GET_PRIVATE (view);

	view->priv = priv;

	priv->started = FALSE;
}

EDataCalView *
e_data_cal_view_new (ECalBackend     *backend,
		     const gchar     *path,
		     ECalBackendSExp *sexp)
{
	EDataCalView *query;

	/* XXX: ideally we would pass an EDataCal for "data" here and remove the
	 * "backend" property from EDataView, since it can be derived from the
	 * "data" value (but it would require another argument to this function,
	 * breaking API stability) */
	query = g_object_new (E_DATA_CAL_VIEW_TYPE,
			"data", NULL,
			"dbus-path", path,
			"backend", backend,
			"sexp", sexp,
			NULL);

	return query;
}

const gchar *
e_data_cal_view_get_dbus_path (EDataCalView *view)
{
	g_return_val_if_fail (E_IS_DATA_CAL_VIEW (view), NULL);

	return e_data_view_get_dbus_path (E_DATA_VIEW (view));
}

static gboolean
impl_EDataCalView_start (EDataCalView *query, GError **error)
{
	EDataCalViewPrivate *priv;

	priv = query->priv;

	if (!priv->started) {
		priv->started = TRUE;
		e_cal_backend_start_query (e_data_cal_view_get_backend (query), query);
	}

	return TRUE;
}

/**
 * e_data_cal_view_get_backend:
 * @query: A query object.
 *
 * Get the #ECalBackend object used for the given query.
 *
 * Return value: The #ECalBackend backend.
 */
ECalBackend *
e_data_cal_view_get_backend (EDataCalView *query)
{
	g_return_val_if_fail (IS_QUERY (query), NULL);

	return E_CAL_BACKEND (e_data_view_get_backend (E_DATA_VIEW (query)));
}

/**
 * e_data_cal_view_get_object_sexp:
 * @query: A query object.
 *
 * Get the #ECalBackendSExp object used for the given query.
 *
 * Return value: The expression object used to search.
 */
ECalBackendSExp *
e_data_cal_view_get_object_sexp (EDataCalView *query)
{
	g_return_val_if_fail (IS_QUERY (query), NULL);

	return E_CAL_BACKEND_SEXP (e_data_view_get_sexp (E_DATA_VIEW (query)));
}

/**
 * e_data_cal_view_get_text:
 * @query: A #EDataCalView object.
 *
 * Get the expression used for the given query.
 *
 * Return value: the query expression used to search.
 */
const gchar *
e_data_cal_view_get_text (EDataCalView *query)
{
	ECalBackendSExp *sexp;

	g_return_val_if_fail (IS_QUERY (query), NULL);

	sexp = e_data_cal_view_get_object_sexp (query);

	return e_cal_backend_sexp_text (sexp);
}

/**
 * e_data_cal_view_object_matches:
 * @query: A query object.
 * @object: Object to match.
 *
 * Compares the given @object to the regular expression used for the
 * given query.
 *
 * Return value: TRUE if the object matches the expression, FALSE if not.
 */
gboolean
e_data_cal_view_object_matches (EDataCalView *query, const gchar *object)
{
	ECalBackend *backend;
	ECalBackendSExp *sexp;

	g_return_val_if_fail (query != NULL, FALSE);
	g_return_val_if_fail (IS_QUERY (query), FALSE);
	g_return_val_if_fail (object != NULL, FALSE);

	backend = e_data_cal_view_get_backend (query);
	sexp = e_data_cal_view_get_object_sexp (query);

	return e_cal_backend_sexp_match_object (sexp, object, backend);
}

/**
 * e_data_cal_view_get_matched_objects:
 * @query: A query object.
 *
 * Gets the list of objects already matched for the given query.
 *
 * Return value: A list of matched objects.
 */
GList *
e_data_cal_view_get_matched_objects (EDataCalView *query)
{
	g_return_val_if_fail (IS_QUERY (query), NULL);
	/* TODO e_data_cal_view_get_matched_objects */
	return NULL;
}

/**
 * e_data_cal_view_is_started:
 * @query: A query object.
 *
 * Checks whether the given query has already been started.
 *
 * Return value: TRUE if the query has already been started, FALSE otherwise.
 */
gboolean
e_data_cal_view_is_started (EDataCalView *view)
{
	g_return_val_if_fail (E_IS_DATA_CAL_VIEW (view), FALSE);

	return view->priv->started;
}

/**
 * e_data_cal_view_is_done:
 * @query: A query object.
 *
 * Checks whether the given query is already done. Being done means the initial
 * matching of objects have been finished, not that no more notifications about
 * changes will be sent. In fact, even after done, notifications will still be sent
 * if there are changes in the objects matching the query search expression.
 *
 * Return value: TRUE if the query is done, FALSE if still in progress.
 */
gboolean
e_data_cal_view_is_done (EDataCalView *query)
{
	return e_data_view_is_done (E_DATA_VIEW (query));
}

/**
 * e_data_cal_view_get_done_status:
 * @query: A query object.
 *
 * Gets the status code obtained when the initial matching of objects was done
 * for the given query.
 *
 * Return value: The query status.
 */
EDataCalCallStatus
e_data_cal_view_get_done_status (EDataCalView *query)
{
	if (e_data_view_is_done (E_DATA_VIEW (query)))
		return e_data_view_get_done_status (E_DATA_VIEW (query));

	return Success;
}

/**
 * e_data_cal_view_notify_objects_added:
 * @query: A query object.
 * @objects: List of objects that have been added.
 *
 * Notifies all query listeners of the addition of a list of objects.
 */
void
e_data_cal_view_notify_objects_added (EDataCalView *view, const GList *objects)
{
	EDataCalViewPrivate *priv;
	const GList *l;

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	priv = view->priv;

	if (objects == NULL)
		return;

	for (l = objects; l; l = l->next) {
		gchar *object;
		ECalComponent *comp;

		object = g_strdup (l->data);
		comp = e_cal_component_new_from_string (object);

		e_data_view_notify_object_add (E_DATA_VIEW (view), e_cal_component_get_id (comp), object);

		g_object_unref (comp);
	}

	e_data_view_send_pending_adds (E_DATA_VIEW (view));
}

/**
 * e_data_cal_view_notify_objects_added_1:
 * @query: A query object.
 * @object: The object that has been added.
 *
 * Notifies all the query listeners of the addition of a single object.
 */
void
e_data_cal_view_notify_objects_added_1 (EDataCalView *view, const gchar *object)
{
	GList l = {0,};

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	g_return_if_fail (object);

	l.data = (gpointer)object;
	e_data_cal_view_notify_objects_added (view, &l);
}

/**
 * e_data_cal_view_notify_objects_modified:
 * @query: A query object.
 * @objects: List of modified objects.
 *
 * Notifies all query listeners of the modification of a list of objects.
 */
void
e_data_cal_view_notify_objects_modified (EDataCalView *view, const GList *objects)
{
	EDataCalViewPrivate *priv;
	const GList *l;

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	priv = view->priv;

	if (objects == NULL)
		return;

	for (l = objects; l; l = l->next) {
		e_data_view_notify_object_modification (E_DATA_VIEW (view), g_strdup (l->data));
	}

	e_data_view_send_pending_modifications (E_DATA_VIEW (view));
}

/**
 * e_data_cal_view_notify_objects_modified_1:
 * @query: A query object.
 * @object: The modified object.
 *
 * Notifies all query listeners of the modification of a single object.
 */
void
e_data_cal_view_notify_objects_modified_1 (EDataCalView *view, const gchar *object)
{
	GList l = {0,};

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	g_return_if_fail (object);

	l.data = (gpointer)object;
	e_data_cal_view_notify_objects_modified (view, &l);
}

/**
 * e_data_cal_view_notify_objects_removed:
 * @query: A query object.
 * @ids: List of IDs for the objects that have been removed.
 *
 * Notifies all query listener of the removal of a list of objects.
 */
void
e_data_cal_view_notify_objects_removed (EDataCalView *view, const GList *ids)
{
	EDataCalViewPrivate *priv;
	const GList *l;

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	priv = view->priv;

	if (ids == NULL)
		return;

	for (l = ids; l; l = l->next) {
		ECalComponentId *id = l->data;

		if (e_data_view_contains_object (E_DATA_VIEW (view), id)) {
			e_data_view_notify_object_remove (E_DATA_VIEW (view), id);
		}
	}

	e_data_view_send_pending_removes (E_DATA_VIEW (view));
}

/**
 * e_data_cal_view_notify_objects_removed_1:
 * @query: A query object.
 * @id: ID of the removed object.
 *
 * Notifies all query listener of the removal of a single object.
 */
void
e_data_cal_view_notify_objects_removed_1 (EDataCalView *view, const ECalComponentId *id)
{
	GList l = {0,};

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	g_return_if_fail (id);

	l.data = (gpointer)id;
	e_data_cal_view_notify_objects_removed (view, &l);
}

/**
 * e_data_cal_view_notify_progress:
 * @query: A query object.
 * @message: Progress message to send to listeners.
 * @percent: Percentage completed.
 *
 * Notifies all query listeners of progress messages.
 */
void
e_data_cal_view_notify_progress (EDataCalView *view, const gchar *message, gint percent)
{
	EDataCalViewPrivate *priv;

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	priv = view->priv;

	if (!priv->started)
		return;

	g_signal_emit (view, signals[PROGRESS], 0, message, percent);
}

/**
 * e_data_cal_view_notify_done:
 * @query: A query object.
 * @status: Query completion status code.
 *
 * Notifies all query listeners of the completion of the query, including a
 * status code.
 */
void
e_data_cal_view_notify_done (EDataCalView *view, GNOME_Evolution_Calendar_CallStatus status)
{
	EDataCalViewPrivate *priv;

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	priv = view->priv;

	if (!priv->started)
		return;

	e_data_view_notify_done (E_DATA_VIEW (view), status);
}
