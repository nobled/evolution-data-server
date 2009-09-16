/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  Camel
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
 *           Michael Zucchi <notzed@novell.com>
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

#ifndef CAMEL_IMAP4_SEARCH_H
#define CAMEL_IMAP4_SEARCH_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_IMAP4_SEARCH \
	(camel_imap4_search_get_type ())
#define CAMEL_IMAP4_SEARCH(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAP4_SEARCH, CamelIMAP4Search))
#define CAMEL_IMAP4_SEARCH_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAP4_SEARCH, CamelIMAP4SearchClass))
#define CAMEL_IS_IMAP4_SEARCH(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAP4_SEARCH))
#define CAMEL_IS_IMAP4_SEARCH_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAP4_SEARCH))
#define CAMEL_IMAP4_SEARCH_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAP4_SEARCH, CamelIMAP4SearchClass))

G_BEGIN_DECLS

typedef struct _CamelIMAP4Search CamelIMAP4Search;
typedef struct _CamelIMAP4SearchClass CamelIMAP4SearchClass;

struct _CamelIMAP4Engine;

struct _CamelIMAP4Search {
	CamelFolderSearch parent;

	struct _CamelIMAP4Engine *engine;

	guint32 lastuid;	/* current 'last uid' for the folder */
	guint32 validity;	/* validity of the current folder */

	CamelDataCache *cache;	/* disk-cache for searches */

	/* cache of body search matches */
	CamelDList matches;
	GHashTable *matches_hash;
	guint matches_count;
};

struct _CamelIMAP4SearchClass {
	CamelFolderSearchClass parent_class;

};

GType camel_imap4_search_get_type (void);

CamelFolderSearch *camel_imap4_search_new (struct _CamelIMAP4Engine *engine, const gchar *cachedir);

G_END_DECLS

#endif /* CAMEL_IMAP4_SEARCH_H */
