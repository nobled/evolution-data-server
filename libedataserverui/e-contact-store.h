/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-contact-store.h - Contacts store with GtkTreeModel interface.
 *
 * Copyright (C) 2004 Novell, Inc.
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
 *
 * Authors: Hans Petter Jansson <hpj@novell.com>
 */

#ifndef E_CONTACT_STORE_H
#define E_CONTACT_STORE_H

#include <gtk/gtktreemodel.h>
#include <libebook/e-contact.h>
#include <libebook/e-book.h>
#include <libebook/e-book-query.h>
#include <libebook/e-book-types.h>

G_BEGIN_DECLS

#define E_TYPE_CONTACT_STORE            (e_contact_store_get_type ())
#define E_CONTACT_STORE(obj)	        (GTK_CHECK_CAST ((obj), E_TYPE_CONTACT_STORE, EContactStore))
#define E_CONTACT_STORE_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), E_TYPE_CONTACT_STORE, EContactStoreClass))
#define E_IS_CONTACT_STORE(obj)         (GTK_CHECK_TYPE ((obj), E_TYPE_CONTACT_STORE))
#define E_IS_CONTACT_STORE_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), E_TYPE_CONTACT_STORE))
#define E_CONTACT_STORE_GET_CLASS(obj)  (GTK_CHECK_GET_CLASS ((obj), E_TYPE_CONTACT_STORE, EContactStoreClass))

typedef struct _EContactStore       EContactStore;
typedef struct _EContactStoreClass  EContactStoreClass;

struct _EContactStore
{
	GObject     parent;

	/* Private */

	gint        stamp;
	EBookQuery *query;
	GArray     *contact_sources;
};

struct _EContactStoreClass
{
	GObjectClass parent_class;
};

GtkType        e_contact_store_get_type    (void);
EContactStore *e_contact_store_new         (void);

/* Returns a shallow copy; free the list when done, but don't unref elements */
GList         *e_contact_store_get_books   (EContactStore *contact_store);

void           e_contact_store_add_book    (EContactStore *contact_store, EBook *book);
void           e_contact_store_remove_book (EContactStore *contact_store, EBook *book);

void           e_contact_store_set_query   (EContactStore *contact_store, EBookQuery *book_query);
EBookQuery    *e_contact_store_peek_query  (EContactStore *contact_store);

G_END_DECLS

#endif  /* E_CONTACT_STORE_H */
