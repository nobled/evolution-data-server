/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  Camel
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef CAMEL_IMAP4_STORE_H
#define CAMEL_IMAP4_STORE_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_IMAP4_STORE \
	(camel_imap4_store_get_type ())
#define CAMEL_IMAP4_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAP4_STORE, CamelIMAP4Store))
#define CAMEL_IMAP4_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAP4_STORE, CamelIMAP4StoreClass))
#define CAMEL_IS_IMAP4_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAP4_STORE))
#define CAMEL_IS_IMAP4_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAP4_STORE))
#define CAMEL_IMAP4_STORE_GET_CLASS(obj) \
	(CAMEL_CHECK_GET_CLASS \
	((obj), CAMEL_TYPE_IMAP4_STORE, CamelIMAP4StoreClass))

G_BEGIN_DECLS

typedef struct _CamelIMAP4Store CamelIMAP4Store;
typedef struct _CamelIMAP4StoreClass CamelIMAP4StoreClass;

struct _CamelIMAP4Engine;

struct _CamelIMAP4Store {
	CamelOfflineStore parent;

	struct _CamelIMAP4StoreSummary *summary;
	struct _CamelIMAP4Engine *engine;
	gchar *storage_path;
};

struct _CamelIMAP4StoreClass {
	CamelOfflineStoreClass parent_class;
};

GType camel_imap4_store_get_type (void);

G_END_DECLS

#endif /* CAMEL_IMAP4_STORE_H */
