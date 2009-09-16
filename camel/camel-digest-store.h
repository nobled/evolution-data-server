/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_DISABLE_DEPRECATED

#ifndef CAMEL_DIGEST_STORE_H
#define CAMEL_DIGEST_STORE_H

#include <camel/camel-store.h>

/* Standard GObject macros */
#define CAMEL_TYPE_DIGEST_STORE \
	(camel_digest_store_get_type ())
#define CAMEL_DIGEST_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_DIGEST_STORE, CamelDigestStore))
#define CAMEL_DIGEST_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_DIGEST_STORE, CamelDigestStoreClass))
#define CAMEL_IS_DIGEST_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_DIGEST_STORE))
#define CAMEL_IS_DIGEST_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_DIGEST_STORE))
#define CAMEL_DIGEST_STORE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_DIGEST_STORE, CamelDigestStoreClass))

G_BEGIN_DECLS

typedef struct _CamelDigestStore CamelDigestStore;
typedef struct _CamelDigestStoreClass CamelDigestStoreClass;

struct _CamelDigestStore {
	CamelStore parent;
};

struct _CamelDigestStoreClass {
	CamelStoreClass parent_class;
};

GType camel_digest_store_get_type (void);

CamelStore *camel_digest_store_new (const gchar *url);

G_END_DECLS

#endif /* CAMEL_DIGEST_STORE_H */

#endif /* CAMEL_DISABLE_DEPRECATED */
