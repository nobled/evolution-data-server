/* Evolution calendar - Live search query implementation
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib.h>
#include <bonobo/bonobo-exception.h>
#include <libedataserver/e-component-listener.h>
#include "e-cal-backend-sexp.h"
#include "e-data-cal-view.h"



typedef struct {
	GNOME_Evolution_Calendar_CalViewListener listener;
	EComponentListener *component_listener;

	gboolean notified_start;
	gboolean notified_done;
} ListenerData;

/* Private part of the Query structure */
struct _EDataCalViewPrivate {
	/* The backend we are monitoring */
	ECalBackend *backend;

	gboolean started;
	gboolean done;
	GNOME_Evolution_Calendar_CallStatus done_status;

	GHashTable *matched_objects;

	/* The listener we report to */
	GList *listeners;

	/* Sexp that defines the query */
	ECalBackendSExp *sexp;
};




static void e_data_cal_view_class_init (EDataCalViewClass *class);
static void e_data_cal_view_init (EDataCalView *query, EDataCalViewClass *class);
static void e_data_cal_view_finalize (GObject *object);

static BonoboObjectClass *parent_class;



BONOBO_TYPE_FUNC_FULL (EDataCalView,
		       GNOME_Evolution_Calendar_CalView,
		       BONOBO_TYPE_OBJECT,
		       e_data_cal_view);

/* Property IDs */
enum props {
	PROP_0,
	PROP_BACKEND,
	PROP_LISTENER,
	PROP_SEXP
};


static void
add_object_to_cache (EDataCalView *query, const char *calobj)
{
	ECalComponent *comp;
	char *real_uid;
	const char *uid;
	EDataCalViewPrivate *priv;

	priv = query->priv;

	comp = e_cal_component_new_from_string (calobj);
	if (!comp)
		return;

	e_cal_component_get_uid (comp, &uid);
	if (!uid || !*uid) {
		g_object_unref (comp);
		return;
	}

	if (e_cal_component_is_instance (comp))
		real_uid = g_strdup_printf ("%s@%s", uid, e_cal_component_get_recurid_as_string (comp));
	else
		real_uid = g_strdup (uid);

	if (g_hash_table_lookup (priv->matched_objects, real_uid))
		g_hash_table_replace (priv->matched_objects, real_uid, g_strdup (calobj));
	else
		g_hash_table_insert (priv->matched_objects, real_uid, g_strdup (calobj));

	/* free memory */
	g_object_unref (comp);
}

static gboolean
uncache_with_uid_cb (gpointer key, gpointer value, gpointer user_data)
{
	ECalComponent *comp;
	const char *this_uid;
	char *uid, *object;

	uid = user_data;
	object = value;

	comp = e_cal_component_new_from_string (object);
	if (comp) {
		e_cal_component_get_uid (comp, &this_uid);
		if (this_uid && *this_uid && !strcmp (uid, this_uid)) {
			g_object_unref (comp);
			return TRUE;
		}

		g_object_unref (comp);
	}

	return FALSE;
}

static void
remove_object_from_cache (EDataCalView *query, const char *uid)
{
	EDataCalViewPrivate *priv;

	priv = query->priv;

	g_hash_table_foreach_remove (priv->matched_objects, (GHRFunc) uncache_with_uid_cb, (gpointer) uid);
}

static void
listener_died_cb (EComponentListener *cl, gpointer data)
{
	EDataCalView *query = QUERY (data);
	EDataCalViewPrivate *priv;
	GList *l;

	priv = query->priv;

	for (l = priv->listeners; l != NULL; l = l->next) {
		ListenerData *ld = l->data;

		if (ld->component_listener == cl) {
			g_object_unref (ld->component_listener);
			ld->component_listener = NULL;
	
			bonobo_object_release_unref (ld->listener, NULL);
			ld->listener = NULL;

			priv->listeners = g_list_remove_link (priv->listeners, l);
			g_list_free (l);
			g_free (ld);
			break;
		}
	}
}

static void
notify_matched_object_cb (gpointer key, gpointer value, gpointer user_data)
{
	char *uid, *object;
	EDataCalView *query;
	EDataCalViewPrivate *priv;
	GList *l;

	uid = key;
	object = value;
	query = user_data;
	priv = query->priv;

	for (l = priv->listeners; l != NULL; l = l->next) {
		ListenerData *ld = l->data;

		if (!ld->notified_start) {
			GNOME_Evolution_Calendar_stringlist obj_list;
			CORBA_Environment ev;

			obj_list._buffer = GNOME_Evolution_Calendar_stringlist_allocbuf (1);
			obj_list._maximum = 1;
			obj_list._length = 1;
			obj_list._buffer[0] = CORBA_string_dup (object);

			CORBA_exception_init (&ev);
			GNOME_Evolution_Calendar_CalViewListener_notifyObjectsAdded (ld->listener, &obj_list, &ev);
			CORBA_exception_free (&ev);

			CORBA_free (obj_list._buffer);
		}
	}
}

static void
impl_EDataCalView_start (PortableServer_Servant servant, CORBA_Environment *ev)
{
	EDataCalView *query;
	EDataCalViewPrivate *priv;
	GList *l;

	query = QUERY (bonobo_object_from_servant (servant));
	priv = query->priv;

	if (priv->started) {
		g_warning (G_STRLOC ": Query already started, notifying matched objects");
		g_hash_table_foreach (priv->matched_objects, (GHFunc) notify_matched_object_cb, query);

		/* notify all listeners correctly if the query is already done */
		for (l = priv->listeners; l != NULL; l = l->next) {
			ListenerData *ld = l->data;

			if (!ld->notified_start) {
				ld->notified_start = TRUE;

				if (priv->done && !ld->notified_done) {
					CORBA_Environment ev;

					ld->notified_done = TRUE;

					g_warning (G_STRLOC ": Query already done before, notifying new listener");
					CORBA_exception_init (&ev);
					GNOME_Evolution_Calendar_CalViewListener_notifyQueryDone (
						ld->listener, priv->done_status, &ev);
					CORBA_exception_free (&ev);
				}
			}
		}
	} else {
		g_warning (G_STRLOC ": Starting new query");
		priv->started = TRUE;
		e_cal_backend_start_query (priv->backend, query);

		for (l = priv->listeners; l != NULL; l = l->next) {
			ListenerData *ld = l->data;

			ld->notified_start = TRUE;
		}
	}
}

static void
e_data_cal_view_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	EDataCalView *query;
	EDataCalViewPrivate *priv;

	query = QUERY (object);
	priv = query->priv;
	
	switch (property_id) {
	case PROP_BACKEND:
		priv->backend = E_CAL_BACKEND (g_value_dup_object (value));
		break;
	case PROP_LISTENER:
		e_data_cal_view_add_listener (query, g_value_get_pointer (value));
		break;
	case PROP_SEXP:
		priv->sexp = E_CAL_BACKEND_SEXP (g_value_dup_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
e_data_cal_view_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
	EDataCalView *query;
	EDataCalViewPrivate *priv;
	
	query = QUERY (object);
	priv = query->priv;

	switch (property_id) {
	case PROP_BACKEND:
		g_value_set_object (value, priv->backend);
	case PROP_LISTENER:

		if (priv->listeners) {
			ListenerData *ld;

			ld = priv->listeners->data;
			g_value_set_pointer (value, ld->listener);
		}
		break;
	case PROP_SEXP:
		g_value_set_object (value, priv->sexp);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

/* Class initialization function for the live search query */
static void
e_data_cal_view_class_init (EDataCalViewClass *klass)
{
	GObjectClass *object_class;
	POA_GNOME_Evolution_Calendar_CalView__epv *epv = &klass->epv;
	GParamSpec *param;
	
	object_class = (GObjectClass *) klass;

	parent_class = g_type_class_peek_parent (klass);

	object_class->set_property = e_data_cal_view_set_property;
	object_class->get_property = e_data_cal_view_get_property;
	object_class->finalize = e_data_cal_view_finalize;

	epv->start = impl_EDataCalView_start;

	param =  g_param_spec_object ("backend", NULL, NULL, E_TYPE_CAL_BACKEND,
				      G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
	g_object_class_install_property (object_class, PROP_BACKEND, param);
	param =  g_param_spec_pointer ("listener", NULL, NULL,
				      G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
	g_object_class_install_property (object_class, PROP_LISTENER, param);
	param =  g_param_spec_object ("sexp", NULL, NULL, E_TYPE_CAL_BACKEND_SEXP,
				      G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
	g_object_class_install_property (object_class, PROP_SEXP, param);
}

/* Object initialization function for the live search query */
static void
e_data_cal_view_init (EDataCalView *query, EDataCalViewClass *class)
{
	EDataCalViewPrivate *priv;

	priv = g_new0 (EDataCalViewPrivate, 1);
	query->priv = priv;

	priv->backend = NULL;
	priv->started = FALSE;
	priv->done = FALSE;
	priv->done_status = GNOME_Evolution_Calendar_Success;
	priv->matched_objects = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	priv->listeners = NULL;
	priv->sexp = NULL;
}

/* Finalize handler for the live search query */
static void
e_data_cal_view_finalize (GObject *object)
{
	EDataCalView *query;
	EDataCalViewPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_QUERY (object));

	query = QUERY (object);
	priv = query->priv;

	if (priv->backend)
		g_object_unref (priv->backend);

	while (priv->listeners) {
		ListenerData *ld = priv->listeners->data;

		if (ld->listener)
			bonobo_object_release_unref (ld->listener, NULL);
		if (ld->component_listener)
			g_object_unref (ld->component_listener);
		priv->listeners = g_list_remove (priv->listeners, ld);
		g_free (ld);
	}

	if (priv->matched_objects)
		g_hash_table_destroy (priv->matched_objects);

	if (priv->sexp)
		g_object_unref (priv->sexp);

	g_free (priv);

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/**
 * e_data_cal_view_new:
 * @backend: Calendar backend that the query object will monitor.
 * @ql: Listener for query results.
 * @sexp: Sexp that defines the query.
 * 
 * Creates a new query engine object that monitors a calendar backend.
 * 
 * Return value: A newly-created query object, or NULL on failure.
 **/
EDataCalView *
e_data_cal_view_new (ECalBackend *backend,
		     GNOME_Evolution_Calendar_CalViewListener ql,
		     ECalBackendSExp *sexp)
{
	EDataCalView *query;

	query = g_object_new (E_DATA_CAL_VIEW_TYPE, "backend", backend, "listener", ql,
			      "sexp", sexp, NULL);

	return query;
}

/**
 * e_data_cal_view_add_listener:
 * @query: A #EDataCalView object.
 * @ql: A CORBA query listener to add to the list of listeners.
 *
 * Adds the given CORBA listener to a #EDataCalView object. This makes the view
 * object notify that listener when notifying the other listeners already attached
 * to the view.
 */
void
e_data_cal_view_add_listener (EDataCalView *query, GNOME_Evolution_Calendar_CalViewListener ql)
{
	ListenerData *ld;
	EDataCalViewPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (IS_QUERY (query));
	g_return_if_fail (ql != CORBA_OBJECT_NIL);

	priv = query->priv;

	ld = g_new0 (ListenerData, 1);

	CORBA_exception_init (&ev);
	ld->listener = CORBA_Object_duplicate (ql, &ev);
	CORBA_exception_free (&ev);

	ld->component_listener = e_component_listener_new (ld->listener);
	g_signal_connect (G_OBJECT (ld->component_listener), "component_died",
			  G_CALLBACK (listener_died_cb), query);

	priv->listeners = g_list_prepend (priv->listeners, ld);
}

/**
 * e_data_cal_view_get_sexp
 * @query: A #EDataCalView object.
 *
 * Get the expression used for the given query.
 *
 * Returns: the query expression used to search.
 */
const char *
e_data_cal_view_get_text (EDataCalView *query)
{
	g_return_val_if_fail (IS_QUERY (query), NULL);

	return e_cal_backend_sexp_text (query->priv->sexp);
}

ECalBackendSExp *
e_data_cal_view_get_object_sexp (EDataCalView *query)
{
	g_return_val_if_fail (IS_QUERY (query), NULL);

	return query->priv->sexp;
}

gboolean
e_data_cal_view_object_matches (EDataCalView *query, const char *object)
{
	EDataCalViewPrivate *priv;
	
	g_return_val_if_fail (query != NULL, FALSE);
	g_return_val_if_fail (IS_QUERY (query), FALSE);
	g_return_val_if_fail (object != NULL, FALSE);

	priv = query->priv;
	
	return e_cal_backend_sexp_match_object (priv->sexp, object, priv->backend);
}

static void
add_object_to_list (gpointer key, gpointer value, gpointer user_data)
{
	GList **list = user_data;

	*list = g_list_append (*list, value);
}

GList *
e_data_cal_view_get_matched_objects (EDataCalView *query)
{
	EDataCalViewPrivate *priv;
	GList *list = NULL;

	g_return_val_if_fail (IS_QUERY (query), NULL);

	priv = query->priv;

	g_hash_table_foreach (priv->matched_objects, (GHFunc) add_object_to_list, &list);

	return list;
}

gboolean
e_data_cal_view_is_started (EDataCalView *query)
{
	EDataCalViewPrivate *priv;

	g_return_val_if_fail (IS_QUERY (query), FALSE);

	priv = query->priv;

	return priv->started;
}

gboolean
e_data_cal_view_is_done (EDataCalView *query)
{
	EDataCalViewPrivate *priv;

	g_return_val_if_fail (IS_QUERY (query), FALSE);

	priv = query->priv;

	return priv->done;
}

GNOME_Evolution_Calendar_CallStatus
e_data_cal_view_get_done_status (EDataCalView *query)
{
	EDataCalViewPrivate *priv;

	g_return_val_if_fail (IS_QUERY (query), FALSE);

	priv = query->priv;

	if (priv->done)
		return priv->done_status;

	return GNOME_Evolution_Calendar_Success;
}

void
e_data_cal_view_notify_objects_added (EDataCalView *query, const GList *objects)
{
	EDataCalViewPrivate *priv;
	GNOME_Evolution_Calendar_stringlist obj_list;
	CORBA_Environment ev;
	const GList *l;
	int num_objs, i;
	
	g_return_if_fail (query != NULL);
	g_return_if_fail (IS_QUERY (query));

	priv = query->priv;
	g_return_if_fail (priv->listeners != CORBA_OBJECT_NIL);

	num_objs = g_list_length ((GList*)objects);
	if (num_objs <= 0)
		return;

	obj_list._buffer = GNOME_Evolution_Calendar_stringlist_allocbuf (num_objs);
	obj_list._maximum = num_objs;
	obj_list._length = num_objs;

	for (l = objects, i = 0; l; l = l->next, i++) {
		obj_list._buffer[i] = CORBA_string_dup (l->data);

		/* update our cache */
		add_object_to_cache (query, l->data);
	}

	for (l = priv->listeners; l != NULL; l = l->next) {
		ListenerData *ld = l->data;

		CORBA_exception_init (&ev);

		GNOME_Evolution_Calendar_CalViewListener_notifyObjectsAdded (ld->listener, &obj_list, &ev);
		if (BONOBO_EX (&ev))
			g_warning (G_STRLOC ": could not notify the listener of object addition");

		CORBA_exception_free (&ev);
	}

	CORBA_free (obj_list._buffer);
}

void
e_data_cal_view_notify_objects_added_1 (EDataCalView *query, const char *object)
{
	EDataCalViewPrivate *priv;
	GList objects;
	
	g_return_if_fail (query != NULL);
	g_return_if_fail (IS_QUERY (query));
	g_return_if_fail (object != NULL);

	priv = query->priv;
	g_return_if_fail (priv->listeners != CORBA_OBJECT_NIL);

	objects.next = objects.prev = NULL;
	objects.data = (gpointer)object;

	e_data_cal_view_notify_objects_added (query, &objects);
}

void
e_data_cal_view_notify_objects_modified (EDataCalView *query, const GList *objects)
{
	EDataCalViewPrivate *priv;
	GNOME_Evolution_Calendar_CalObjUIDSeq obj_list;
	CORBA_Environment ev;
	const GList *l;
	int num_objs, i;
	
	g_return_if_fail (query != NULL);
	g_return_if_fail (IS_QUERY (query));

	priv = query->priv;
	g_return_if_fail (priv->listeners != CORBA_OBJECT_NIL);

	num_objs = g_list_length ((GList*)objects);
	if (num_objs <= 0)
		return;

	obj_list._buffer = GNOME_Evolution_Calendar_stringlist_allocbuf (num_objs);
	obj_list._maximum = num_objs;
	obj_list._length = num_objs;

	for (l = objects, i = 0; l; l = l->next, i++) {
		obj_list._buffer[i] = CORBA_string_dup (l->data);

		/* update our cache */
		add_object_to_cache (query, l->data);
	}

	for (l = priv->listeners; l != NULL; l = l->next) {
		ListenerData *ld = l->data;

		CORBA_exception_init (&ev);

		GNOME_Evolution_Calendar_CalViewListener_notifyObjectsModified (ld->listener, &obj_list, &ev);
		if (BONOBO_EX (&ev))
			g_warning (G_STRLOC ": could not notify the listener of object modification");

		CORBA_exception_free (&ev);
	}

	CORBA_free (obj_list._buffer);
}

void
e_data_cal_view_notify_objects_modified_1 (EDataCalView *query, const char *object)
{
	EDataCalViewPrivate *priv;
	GList objects;
	
	g_return_if_fail (query != NULL);
	g_return_if_fail (IS_QUERY (query));
	g_return_if_fail (object != NULL);

	priv = query->priv;
	g_return_if_fail (priv->listeners != CORBA_OBJECT_NIL);

	objects.next = objects.prev = NULL;
	objects.data = (gpointer)object;
	
	e_data_cal_view_notify_objects_modified (query, &objects);
}

void
e_data_cal_view_notify_objects_removed (EDataCalView *query, const GList *uids)
{
	EDataCalViewPrivate *priv;
	GNOME_Evolution_Calendar_CalObjUIDSeq uid_list;
	CORBA_Environment ev;
	const GList *l;
	int num_uids, i;
	
	g_return_if_fail (query != NULL);
	g_return_if_fail (IS_QUERY (query));

	priv = query->priv;
	g_return_if_fail (priv->listeners != CORBA_OBJECT_NIL);

	num_uids = g_list_length ((GList*)uids);
	if (num_uids <= 0)
		return;

	uid_list._buffer = GNOME_Evolution_Calendar_CalObjUIDSeq_allocbuf (num_uids);
	uid_list._maximum = num_uids;
	uid_list._length = num_uids;

	for (l = uids, i = 0; l; l = l->next, i ++) {
		uid_list._buffer[i] = CORBA_string_dup (l->data);

		/* update our cache */
		remove_object_from_cache (query, l->data);
	}

	for (l = priv->listeners; l != NULL; l = l->next) {
		ListenerData *ld = l->data;

		CORBA_exception_init (&ev);

		GNOME_Evolution_Calendar_CalViewListener_notifyObjectsRemoved (ld->listener, &uid_list, &ev);
		if (BONOBO_EX (&ev))
			g_warning (G_STRLOC ": could not notify the listener of object removal");

		CORBA_exception_free (&ev);
	}

	CORBA_free (uid_list._buffer);
}

void
e_data_cal_view_notify_objects_removed_1 (EDataCalView *query, const char *uid)
{
	EDataCalViewPrivate *priv;
	GList uids;
	
	g_return_if_fail (query != NULL);
	g_return_if_fail (IS_QUERY (query));
	g_return_if_fail (uid != NULL);

	priv = query->priv;
	g_return_if_fail (priv->listeners != CORBA_OBJECT_NIL);

	uids.next = uids.prev = NULL;
	uids.data = (gpointer)uid;
	
	e_data_cal_view_notify_objects_removed (query, &uids);
}

void
e_data_cal_view_notify_progress (EDataCalView *query, const char *message, int percent)
{
	EDataCalViewPrivate *priv;	
	CORBA_Environment ev;
	GList *l;

	g_return_if_fail (query != NULL);
	g_return_if_fail (IS_QUERY (query));

	priv = query->priv;
	g_return_if_fail (priv->listeners != CORBA_OBJECT_NIL);

	for (l = priv->listeners; l != NULL; l = l->next) {
		ListenerData *ld = l->data;

		CORBA_exception_init (&ev);

		GNOME_Evolution_Calendar_CalViewListener_notifyQueryProgress (ld->listener, message, percent, &ev);
		if (BONOBO_EX (&ev))
			g_warning (G_STRLOC ": could not notify the listener of query progress");

		CORBA_exception_free (&ev);
	}
}

void
e_data_cal_view_notify_done (EDataCalView *query, GNOME_Evolution_Calendar_CallStatus status)
{
	EDataCalViewPrivate *priv;	
	CORBA_Environment ev;
	GList *l;

	g_return_if_fail (query != NULL);
	g_return_if_fail (IS_QUERY (query));

	priv = query->priv;
	g_return_if_fail (priv->listeners != CORBA_OBJECT_NIL);

	priv->done = TRUE;
	priv->done_status = status;

	for (l = priv->listeners; l != NULL; l = l->next) {
		ListenerData *ld = l->data;

		CORBA_exception_init (&ev);

		GNOME_Evolution_Calendar_CalViewListener_notifyQueryDone (ld->listener, status, &ev);
		if (BONOBO_EX (&ev))
			g_warning (G_STRLOC ": could not notify the listener of query completion");

		CORBA_exception_free (&ev);
	}
}
