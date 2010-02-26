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

#include <glib-object.h>
#include "e-book.h"
#include "e-book-view.h"
#include "e-book-view-private.h"
#include "e-data-book-view-gdbus-bindings.h"
#include "e-book-marshal.h"

G_DEFINE_TYPE(EBookView, e_book_view, G_TYPE_OBJECT);

#define E_BOOK_VIEW_GET_PRIVATE(o)					\
	(G_TYPE_INSTANCE_GET_PRIVATE ((o), E_TYPE_BOOK_VIEW, EBookViewPrivate))

struct _EBookViewPrivate {
	EBook *book;
	EDBusProxy *view_proxy;
	GStaticRecMutex *view_proxy_lock;
	gboolean running;
};

enum {
	CONTACTS_CHANGED,
	CONTACTS_REMOVED,
	CONTACTS_ADDED,
	SEQUENCE_COMPLETE,
	STATUS_MESSAGE,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL];

#define LOCK_CONN()   g_static_rec_mutex_lock (book_view->priv->view_proxy_lock)
#define UNLOCK_CONN() g_static_rec_mutex_unlock (book_view->priv->view_proxy_lock)

static void
book_view_handle_signal_status_message (EBookView   *book_view,
					const gchar *message)
{
	if (!book_view->priv->running)
		return;

	g_signal_emit (book_view, signals[STATUS_MESSAGE], 0, message);
}

static void
book_view_handle_signal_contacts_added (EBookView   *book_view,
					const char **vcards)
{
	const gchar **p;
	GList *contacts = NULL;

	if (!book_view->priv->running)
		return;

	for (p = vcards; *p; p++) {
		contacts = g_list_prepend (contacts, e_contact_new_from_vcard (*p));
	}
	contacts = g_list_reverse (contacts);

	g_signal_emit (book_view, signals[CONTACTS_ADDED], 0, contacts);

	g_list_foreach (contacts, (GFunc)g_object_unref, NULL);
	g_list_free (contacts);
}

static void
book_view_handle_signal_contacts_changed (EBookView   *book_view,
					  const char **vcards)
{
	const gchar **p;
	GList *contacts = NULL;

	if (!book_view->priv->running)
		return;

	for (p = vcards; *p; p++) {
		contacts = g_list_prepend (contacts, e_contact_new_from_vcard (*p));
	}
	contacts = g_list_reverse (contacts);

	g_signal_emit (book_view, signals[CONTACTS_CHANGED], 0, contacts);

	g_list_foreach (contacts, (GFunc)g_object_unref, NULL);
	g_list_free (contacts);
}

static void
book_view_handle_signal_contacts_removed (EBookView   *book_view,
					  const char **ids)
{
	const gchar **p;
	GList *list = NULL;

	if (!book_view->priv->running)
		return;

	for (p = ids; *p; p++) {
		list = g_list_prepend (list, (gchar *)*p);
	}
	list = g_list_reverse (list);

	g_signal_emit (book_view, signals[CONTACTS_REMOVED], 0, list);

	/* No need to free the values, our caller will */
	g_list_free (list);
}

static void
book_view_handle_signal_complete (EBookView *book_view,
				  guint      status)
{
	if (!book_view->priv->running)
		return;

	g_signal_emit (book_view, signals[SEQUENCE_COMPLETE], 0, status);
}

static void
book_view_proxy_signal_cb (EDBusProxy *proxy,
			   gchar      *sender_name,
			   gchar      *signal_name,
			   EVariant   *parameters,
			   EBookView  *book_view)
{
        if (FALSE) {
        } else if (!g_strcmp0 (signal_name, "StatusMessage")) {
		const char *value;
		e_variant_get (parameters, "(&s)", &value);

                book_view_handle_signal_status_message (book_view, value);
        } else if (!g_strcmp0 (signal_name, "ContactsAdded")) {
		const char **value;

		e_variant_get (parameters, "(^a&s)", &value, NULL);

                book_view_handle_signal_contacts_added (book_view, value);
		g_free (value);
        } else if (!g_strcmp0 (signal_name, "ContactsChanged")) {
		const char **value;
		e_variant_get (parameters, "(^a&s)", &value, NULL);

                book_view_handle_signal_contacts_changed (book_view, value);
		g_free (value);
        } else if (!g_strcmp0 (signal_name, "ContactsRemoved")) {
		const char **value;
		e_variant_get (parameters, "(^a&s)", &value, NULL);

                book_view_handle_signal_contacts_removed (book_view, value);
		g_free (value);
        } else if (!g_strcmp0 (signal_name, "Complete")) {
		const guint value;
		e_variant_get (parameters, "(u)", &value);

                book_view_handle_signal_complete (book_view, value);
        }
}

/*
 * e_book_view_new:
 * @book: an #EBook
 * @view_proxy: The #EDBusProxy to get signals from
 *
 * Creates a new #EBookView based on #EBook and listening to @view_proxy.  This
 * is a private function, applications should call #e_book_get_book_view or
 * #e_book_async_get_book_view.
 *
 * Return value: A new #EBookView.
 **/
EBookView *
_e_book_view_new (EBook           *book,
		  EDBusProxy      *view_proxy,
		  GStaticRecMutex *view_proxy_lock)
{
	EBookView *view;
	EBookViewPrivate *priv;

	view = g_object_new (E_TYPE_BOOK_VIEW, NULL);
	priv = view->priv;

	priv->book = g_object_ref (book);

	/* Take ownership of the view_proxy object */
	priv->view_proxy = view_proxy;
	priv->view_proxy_lock = view_proxy_lock;
	g_object_add_weak_pointer (G_OBJECT (view_proxy), (gpointer)&priv->view_proxy);

	g_signal_connect (view_proxy, "g-signal", G_CALLBACK (book_view_proxy_signal_cb), view);

	return view;
}

/**
 * e_book_view_get_book:
 * @book_view: an #EBookView
 *
 * Returns the #EBook that this book view is monitoring.
 *
 * Return value: an #EBook.
 **/
EBook *
e_book_view_get_book (EBookView *book_view)
{
	g_return_val_if_fail (E_IS_BOOK_VIEW (book_view), NULL);

	return book_view->priv->book;
}

/**
 * e_book_view_start:
 * @book_view: an #EBookView
 *
 * Tells @book_view to start processing events.
 */
void
e_book_view_start (EBookView *book_view)
{
	GError *error = NULL;

	g_return_if_fail (E_IS_BOOK_VIEW (book_view));

	book_view->priv->running = TRUE;

	if (book_view->priv->view_proxy) {
		LOCK_CONN ();
		e_data_book_view_gdbus_start_sync (book_view->priv->view_proxy, &error);
		UNLOCK_CONN ();
		if (error) {
			g_warning ("Cannot start book view: %s\n", error->message);

			/* Fake a sequence-complete so that the application knows this failed */
			/* TODO: use get_status_from_error */
			g_signal_emit (book_view, signals[SEQUENCE_COMPLETE], 0,
				       E_BOOK_ERROR_CORBA_EXCEPTION);

			g_error_free (error);
		}
	}
}

/**
 * e_book_view_stop:
 * @book_view: an #EBookView
 *
 * Tells @book_view to stop processing events.
 **/
void
e_book_view_stop (EBookView *book_view)
{
	GError *error = NULL;

	g_return_if_fail (E_IS_BOOK_VIEW (book_view));

	book_view->priv->running = FALSE;

	if (book_view->priv->view_proxy) {
		LOCK_CONN ();
		e_data_book_view_gdbus_stop_sync (book_view->priv->view_proxy, &error);
		UNLOCK_CONN ();
		if (error) {
			g_warning ("Cannot stop book view: %s\n", error->message);
			g_error_free (error);
		}
	}
}

static void
e_book_view_init (EBookView *book_view)
{
	EBookViewPrivate *priv = E_BOOK_VIEW_GET_PRIVATE (book_view);

	priv->book = NULL;
	priv->view_proxy = NULL;
	priv->view_proxy_lock = NULL;
	priv->running = FALSE;

	book_view->priv = priv;
}

static void
e_book_view_dispose (GObject *object)
{
	EBookView *book_view = E_BOOK_VIEW (object);

	if (book_view->priv->view_proxy) {
		LOCK_CONN ();
		e_data_book_view_gdbus_dispose_sync (book_view->priv->view_proxy, NULL);
		g_object_unref (book_view->priv->view_proxy);
		book_view->priv->view_proxy = NULL;
		UNLOCK_CONN ();
	}

	if (book_view->priv->book) {
		g_object_unref (book_view->priv->book);
		book_view->priv->book = NULL;
	}
}

static void
e_book_view_class_init (EBookViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (EBookViewPrivate));

	signals [CONTACTS_CHANGED] = g_signal_new ("contacts_changed",
						   G_OBJECT_CLASS_TYPE (object_class),
						   G_SIGNAL_RUN_LAST,
						   G_STRUCT_OFFSET (EBookViewClass, contacts_changed),
						   NULL, NULL,
						   e_book_marshal_NONE__POINTER,
						   G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals [CONTACTS_REMOVED] = g_signal_new ("contacts_removed",
						   G_OBJECT_CLASS_TYPE (object_class),
						   G_SIGNAL_RUN_LAST,
						   G_STRUCT_OFFSET (EBookViewClass, contacts_removed),
						   NULL, NULL,
						   e_book_marshal_NONE__POINTER,
						   G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals [CONTACTS_ADDED] = g_signal_new ("contacts_added",
						 G_OBJECT_CLASS_TYPE (object_class),
						 G_SIGNAL_RUN_LAST,
						 G_STRUCT_OFFSET (EBookViewClass, contacts_added),
						 NULL, NULL,
						 e_book_marshal_NONE__POINTER,
						 G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals [SEQUENCE_COMPLETE] = g_signal_new ("sequence_complete",
						    G_OBJECT_CLASS_TYPE (object_class),
						    G_SIGNAL_RUN_LAST,
						    G_STRUCT_OFFSET (EBookViewClass, sequence_complete),
						    NULL, NULL,
						    e_book_marshal_NONE__INT,
						    G_TYPE_NONE, 1, G_TYPE_INT);
	signals [STATUS_MESSAGE] = g_signal_new ("status_message",
						 G_OBJECT_CLASS_TYPE (object_class),
						 G_SIGNAL_RUN_LAST,
						 G_STRUCT_OFFSET (EBookViewClass, status_message),
						 NULL, NULL,
						 e_book_marshal_NONE__STRING,
						 G_TYPE_NONE, 1, G_TYPE_STRING);

	object_class->dispose = e_book_view_dispose;
}
