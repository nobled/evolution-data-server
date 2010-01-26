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
#include <libebook/e-contact.h>
#include "e-data-book-view.h"

static gboolean impl_BookView_start (EDataBookView *view, GError **error);
static gboolean impl_BookView_stop (EDataBookView *view, GError **error);
static gboolean impl_BookView_dispose (EDataBookView *view, GError **eror);

#include "e-data-book-view-glue.h"

G_DEFINE_TYPE (EDataBookView, e_data_book_view, E_TYPE_DATA_VIEW);
#define E_DATA_BOOK_VIEW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), E_TYPE_DATA_BOOK_VIEW, EDataBookViewPrivate))

struct _EDataBookViewPrivate {
	gchar* query;
	gint max_results;

	gboolean running;

	GMutex *pending_mutex;

	guint idle_id;
};

enum {
	PROP_NONE,
	PROP_MAX_RESULTS,
	PROP_QUERY,
};

enum {
	CONTACTS_ADDED,
	CONTACTS_CHANGED,
	CONTACTS_REMOVED,
	STATUS_MESSAGE,
	COMPLETE,
	DONE,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
e_data_book_view_stop_if_running (EDataBookView *view)
{
	EDataBookViewPrivate *priv = view->priv;

	/* If the view is running stop it here. */
	if (priv->running) {
		EBookBackend *backend;

		backend = E_BOOK_BACKEND (e_data_view_get_backend (E_DATA_VIEW (view)));
		e_book_backend_stop_book_view (backend, view);
		priv->running = FALSE;
	}
}

static void
contacts_added (EDataView    *view,
		const gchar **objects)
{
	g_signal_emit (view, signals[CONTACTS_ADDED], 0, objects);
}

static void
contacts_changed (EDataView    *view,
		  const gchar **objects)
{
	g_signal_emit (view, signals[CONTACTS_CHANGED], 0, objects);
}

static void
contacts_removed (EDataView    *view,
		  const gchar **ids)
{
	g_signal_emit (view, signals[CONTACTS_REMOVED], 0, ids);
}

/**
 * e_data_book_view_notify_update:
 * @book_view: an #EDataBookView
 * @contact: an #EContact
 *
 * Notify listeners that @contact has changed. This can
 * trigger an add, change or removal event depending on
 * whether the change causes the contact to start matching,
 * no longer match, or stay matching the query specified
 * by @book_view.
 **/
void
e_data_book_view_notify_update (EDataBookView *book_view,
                                EContact      *contact)
{
	EDataBookViewPrivate *priv = book_view->priv;
	gboolean currently_in_view, want_in_view;
	const gchar *id;
	gchar *vcard;
	EBookBackendSExp *sexp;

	if (!priv->running)
		return;

	g_mutex_lock (priv->pending_mutex);

	id = e_contact_get_const (contact, E_CONTACT_UID);

	currently_in_view = e_data_view_contains_object (E_DATA_VIEW (book_view), (gpointer) id);

	sexp = E_BOOK_BACKEND_SEXP (e_data_view_get_sexp (E_DATA_VIEW (book_view)));
	want_in_view =
		e_book_backend_sexp_match_contact (sexp, contact);

	if (want_in_view) {
		vcard = e_vcard_to_string (E_VCARD (contact),
					   EVC_FORMAT_VCARD_30);

		if (currently_in_view) {
			e_data_view_notify_object_modification (E_DATA_VIEW (book_view), vcard);
		} else {
			e_data_view_notify_object_add (E_DATA_VIEW (book_view), (gpointer) g_strdup (id), vcard);
		}
	} else {
		if (currently_in_view) {
			e_data_view_notify_object_remove (E_DATA_VIEW (book_view), (gpointer) id);
		}
		/* else nothing; we're removing a card that wasn't there */
	}

	g_mutex_unlock (priv->pending_mutex);
}

/**
 * e_data_book_view_notify_update_vcard:
 * @book_view: an #EDataBookView
 * @vcard: a plain vCard
 *
 * Notify listeners that @vcard has changed. This can
 * trigger an add, change or removal event depending on
 * whether the change causes the contact to start matching,
 * no longer match, or stay matching the query specified
 * by @book_view.  This method should be preferred over
 * #e_data_book_view_notify_update when the native
 * representation of a contact is a vCard.
 **/
void
e_data_book_view_notify_update_vcard (EDataBookView *book_view, gchar *vcard)
{
	EDataBookViewPrivate *priv = book_view->priv;
	gboolean currently_in_view, want_in_view;
	const gchar *id;
	EContact *contact;
	EBookBackendSExp *sexp;

	if (!priv->running)
		return;

	g_mutex_lock (priv->pending_mutex);

	contact = e_contact_new_from_vcard (vcard);
	id = e_contact_get_const (contact, E_CONTACT_UID);
	currently_in_view = e_data_view_contains_object (E_DATA_VIEW (book_view), (gpointer) id);

	sexp = E_BOOK_BACKEND_SEXP (e_data_view_get_sexp (E_DATA_VIEW (book_view)));
	want_in_view =
		e_book_backend_sexp_match_contact (sexp, contact);

	if (want_in_view) {
		if (currently_in_view) {
			e_data_view_notify_object_modification (E_DATA_VIEW (book_view), vcard);
		} else {
			e_data_view_notify_object_add (E_DATA_VIEW (book_view), (gpointer) g_strdup (id), vcard);
		}
	} else {
		if (currently_in_view) {
			e_data_view_notify_object_remove (E_DATA_VIEW (book_view), (gpointer) id);
		} else {
			/* else nothing; we're removing a card that wasn't there */
			/* FIXME: this seems to not have been intended to be
			 * included in this else block, but it was */
			g_free (vcard);
		}
	}
	/* Do this last so that id is still valid when notify_ is called */
	g_object_unref (contact);

	g_mutex_unlock (priv->pending_mutex);
}

/**
 * e_data_book_view_notify_update_prefiltered_vcard:
 * @book_view: an #EDataBookView
 * @id: the UID of this contact
 * @vcard: a plain vCard
 *
 * Notify listeners that @vcard has changed. This can
 * trigger an add, change or removal event depending on
 * whether the change causes the contact to start matching,
 * no longer match, or stay matching the query specified
 * by @book_view.  This method should be preferred over
 * #e_data_book_view_notify_update when the native
 * representation of a contact is a vCard.
 *
 * The important difference between this method and
 * #e_data_book_view_notify_update and #e_data_book_view_notify_update_vcard is
 * that it doesn't match the contact against the book view query to see if it
 * should be included, it assumes that this has been done and the contact is
 * known to exist in the view.
 **/
void
e_data_book_view_notify_update_prefiltered_vcard (EDataBookView *book_view, const gchar *id, gchar *vcard)
{
	EDataBookViewPrivate *priv = book_view->priv;
	gboolean currently_in_view;

	if (!priv->running)
		return;

	g_mutex_lock (priv->pending_mutex);

	currently_in_view = e_data_view_contains_object (E_DATA_VIEW (book_view), (gpointer) id);

	if (currently_in_view) {
		e_data_view_notify_object_modification (E_DATA_VIEW (book_view), vcard);
	} else {
		e_data_view_notify_object_add (E_DATA_VIEW (book_view), (gpointer) g_strdup (id), vcard);
	}

	g_mutex_unlock (priv->pending_mutex);
}

/**
 * e_data_book_view_notify_remove:
 * @book_view: an #EDataBookView
 * @id: a unique contact ID
 *
 * Notify listeners that a contact specified by @id
 * was removed from @book_view.
 **/
void
e_data_book_view_notify_remove (EDataBookView *book_view, const gchar *id)
{
	EDataBookViewPrivate *priv = E_DATA_BOOK_VIEW_GET_PRIVATE (book_view);

	if (!priv->running)
		return;

	g_mutex_lock (priv->pending_mutex);

	if (e_data_view_contains_object (E_DATA_VIEW (book_view), (gpointer) id)) {
		e_data_view_notify_object_remove (E_DATA_VIEW (book_view), (gpointer) id);
	}

	g_mutex_unlock (priv->pending_mutex);
}

/**
 * e_data_book_view_notify_complete:
 * @book_view: an #EDataBookView
 * @status: the status of the query
 *
 * Notifies listeners that all pending updates on @book_view
 * have been sent. The listener's information should now be
 * in sync with the backend's.
 **/
void
e_data_book_view_notify_complete (EDataBookView *book_view, EDataBookStatus status)
{
	EDataBookViewPrivate *priv = book_view->priv;

	if (!priv->running)
		return;

	g_mutex_lock (priv->pending_mutex);

	e_data_view_send_pending_adds (E_DATA_VIEW (book_view));
	e_data_view_send_pending_modifications (E_DATA_VIEW (book_view));
	e_data_view_send_pending_removes (E_DATA_VIEW (book_view));

	g_mutex_unlock (priv->pending_mutex);

	/* We're done now, so tell the backend to stop?  TODO: this is a bit different to
	   how the CORBA backend works... */

	e_data_view_notify_done (E_DATA_VIEW (book_view), status);
	/* retained for backwards compatibility */
	g_signal_emit (book_view, signals[COMPLETE], 0, status);
}

/**
 * e_data_book_view_notify_status_message:
 * @book_view: an #EDataBookView
 * @message: a text message
 *
 * Provides listeners with a human-readable text describing the
 * current backend operation. This can be used for progress
 * reporting.
 **/
void
e_data_book_view_notify_status_message (EDataBookView *book_view, const gchar *message)
{
	EDataBookViewPrivate *priv = E_DATA_BOOK_VIEW_GET_PRIVATE (book_view);

	if (!priv->running)
		return;

	g_signal_emit (book_view, signals[STATUS_MESSAGE], 0, message);
}

/**
 * e_data_book_view_new:
 * @book: The #EDataBook to search
 * @path: The object path that this book view should have
 * @card_query: The query as a string
 * @card_sexp: The query as an #EBookBackendSExp
 * @max_results: The maximum number of results to return
 *
 * Create a new #EDataBookView for the given #EBook, filtering on #card_sexp,
 * and place it on DBus at the object path #path.
 */
EDataBookView *
e_data_book_view_new (EDataBook *book, const gchar *path, const gchar *card_query, EBookBackendSExp *card_sexp, gint max_results)
{
	return g_object_new (E_TYPE_DATA_BOOK_VIEW,
			"data", book,
			"dbus-path", path,
			"query", card_query,
			"sexp", card_sexp,
			"max-results", max_results,
			NULL);
}

static void
e_data_book_view_dispose (GObject *object)
{
	EDataBookView *book_view = E_DATA_BOOK_VIEW (object);
	EDataBookViewPrivate *priv = book_view->priv;

	if (priv->idle_id) {
		g_source_remove (priv->idle_id);
		priv->idle_id = 0;
	}

	G_OBJECT_CLASS (e_data_book_view_parent_class)->dispose (object);
}

static void
e_data_book_view_finalize (GObject *object)
{
	EDataBookView *book_view = E_DATA_BOOK_VIEW (object);
	EDataBookViewPrivate *priv = book_view->priv;

	g_free (priv->query);
	g_mutex_free (priv->pending_mutex);

	G_OBJECT_CLASS (e_data_book_view_parent_class)->finalize (object);
}

static void
e_data_book_view_get_property (GObject    *object,
			       guint       property_id,
			       GValue     *value,
			       GParamSpec *pspec)
{
	EDataBookViewPrivate *priv = E_DATA_BOOK_VIEW (object)->priv;

	switch (property_id)
	{
		case PROP_MAX_RESULTS:
			g_value_set_int (value, priv->max_results);
		break;

		case PROP_QUERY:
			g_value_set_string (value, priv->query);
		break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
e_data_book_view_set_property (GObject      *object,
			       guint         property_id,
			       const GValue *value,
			       GParamSpec   *pspec)
{
	EDataBookViewPrivate *priv = E_DATA_BOOK_VIEW (object)->priv;

        switch (property_id)
        {
                case PROP_MAX_RESULTS:
                        priv->max_results = g_value_get_int (value);
                break;

                case PROP_QUERY:
                        priv->query = g_value_dup_string (value);
                break;

                default:
                        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
}

static void
e_data_book_view_class_init (EDataBookViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	EDataViewClass *parent_class = E_DATA_VIEW_CLASS (klass);

	object_class->dispose = e_data_book_view_dispose;
	object_class->finalize = e_data_book_view_finalize;
	object_class->get_property = e_data_book_view_get_property;
	object_class->set_property = e_data_book_view_set_property;

	parent_class->stop_if_running = (void (*)(EDataView*)) e_data_book_view_stop_if_running;
	parent_class->objects_added = contacts_added;
	parent_class->objects_modified = contacts_changed;
	parent_class->objects_removed = contacts_removed;

	signals[CONTACTS_ADDED] =
		g_signal_new ("contacts-added",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE, 1, G_TYPE_STRV);

	signals[CONTACTS_CHANGED] =
		g_signal_new ("contacts-changed",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE, 1, G_TYPE_STRV);

	signals[CONTACTS_REMOVED] =
		g_signal_new ("contacts-removed",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE, 1, G_TYPE_STRV);

	signals[STATUS_MESSAGE] =
		g_signal_new ("status-message",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);

	/* this is meant to do the same thing as "done", but existed before it,
	 * so it's retained for compatibility */
	signals[COMPLETE] =
		g_signal_new ("complete",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	signals[DONE] = g_signal_lookup ("done", E_TYPE_DATA_VIEW);

        g_object_class_install_property
                (object_class,
                 PROP_MAX_RESULTS,
                 g_param_spec_int
                         ("max-results",
                          "The maximum results to return",
                          "The maximum number of results to return.",
                          -1, G_MAXINT, -1,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

        g_object_class_install_property
                (object_class,
                 PROP_QUERY,
                 g_param_spec_string
                         ("query",
                          "String version of view's query",
                          "The string version of the view's query.",
                          "",
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

	g_type_class_add_private (klass, sizeof (EDataBookViewPrivate));

	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (klass), &dbus_glib_e_data_book_view_object_info);
}

static void
e_data_book_view_init (EDataBookView *book_view)
{
	EDataBookViewPrivate *priv = E_DATA_BOOK_VIEW_GET_PRIVATE (book_view);
	book_view->priv = priv;

	priv->running = FALSE;
	priv->pending_mutex = g_mutex_new ();
}

static gboolean
bookview_idle_start (gpointer data)
{
	EDataBookView *book_view = data;
	EBookBackend *backend;

	book_view->priv->running = TRUE;
	book_view->priv->idle_id = 0;

	backend = E_BOOK_BACKEND (e_data_view_get_backend (E_DATA_VIEW (book_view)));
	e_book_backend_start_book_view (backend, book_view);

	return FALSE;
}

static gboolean
impl_BookView_start (EDataBookView *book_view, GError **error)
{
	book_view->priv->idle_id = g_idle_add (bookview_idle_start, book_view);
	return TRUE;
}

static gboolean
bookview_idle_stop (gpointer data)
{
	EDataBookView *book_view = data;
	EBookBackend *backend;

	backend = E_BOOK_BACKEND (e_data_view_get_backend (E_DATA_VIEW (book_view)));
	e_book_backend_stop_book_view (backend, book_view);

	book_view->priv->running = FALSE;
	book_view->priv->idle_id = 0;

	return FALSE;
}

static gboolean
impl_BookView_stop (EDataBookView *book_view, GError **error)
{
	if (book_view->priv->idle_id)
		g_source_remove (book_view->priv->idle_id);

	book_view->priv->idle_id = g_idle_add (bookview_idle_stop, book_view);
	return TRUE;
}

static gboolean
impl_BookView_dispose (EDataBookView *book_view, GError **eror)
{
	g_object_unref (book_view);

	return TRUE;
}

void
e_data_book_view_set_thresholds (EDataBookView *book_view,
                                 gint minimum_grouping_threshold,
                                 gint maximum_grouping_threshold)
{
	g_return_if_fail (E_IS_DATA_BOOK_VIEW (book_view));

	g_debug ("e_data_book_view_set_thresholds does nothing in eds-dbus");
}

/**
 * e_data_book_view_get_card_query:
 * @book_view: an #EDataBookView
 *
 * Gets the text representation of the s-expression used
 * for matching contacts to @book_view.
 *
 * Return value: The textual s-expression used.
 **/
const gchar *
e_data_book_view_get_card_query (EDataBookView *book_view)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (book_view), NULL);

	return book_view->priv->query;
}

/**
 * e_data_book_view_get_card_sexp:
 * @book_view: an #EDataBookView
 *
 * Gets the s-expression used for matching contacts to
 * @book_view.
 *
 * Return value: The #EBookBackendSExp used.
 **/
EBookBackendSExp*
e_data_book_view_get_card_sexp (EDataBookView *book_view)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (book_view), NULL);

	return E_BOOK_BACKEND_SEXP (e_data_view_get_sexp (E_DATA_VIEW (book_view)));
}

/**
 * e_data_book_view_get_max_results:
 * @book_view: an #EDataBookView
 *
 * Gets the maximum number of results returned by
 * @book_view's query.
 *
 * Return value: The maximum number of results returned.
 **/
gint
e_data_book_view_get_max_results (EDataBookView *book_view)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (book_view), 0);

	return book_view->priv->max_results;
}

/**
 * e_data_book_view_get_backend:
 * @book_view: an #EDataBookView
 *
 * Gets the backend that @book_view is querying.
 *
 * Return value: The associated #EBookBackend.
 **/
EBookBackend*
e_data_book_view_get_backend (EDataBookView *book_view)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (book_view), NULL);

	return E_BOOK_BACKEND (e_data_view_get_backend (E_DATA_VIEW (book_view)));
}

/**
 * e_data_book_view_ref
 * @book_view: an #EBookView
 *
 * Increase the reference count of the book view. This is a function to aid
 * the transition from Bonobo to DBUS.
 */
void
e_data_book_view_ref (EDataBookView *book_view)
{
	g_object_ref (book_view);
}

/**
 * e_data_book_view_unref
 * @book_view: an #EBookView
 *
 * Decrease the reference count of the book view. This is a function to aid
 * the transition from Bonobo to DBUS.
 */
void
e_data_book_view_unref (EDataBookView *book_view)
{
	g_object_unref (book_view);
}
