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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "camel-digest-folder.h"
#include "camel-digest-store.h"
#include "camel-exception.h"
#include "camel-private.h"

#define d(x)

static CamelFolder *digest_get_folder (CamelStore *store, const gchar *folder_name, guint32 flags, CamelException *ex);
static void digest_delete_folder (CamelStore *store, const gchar *folder_name, CamelException *ex);
static void digest_rename_folder (CamelStore *store, const gchar *old, const gchar *new, CamelException *ex);
static CamelFolder *digest_get_trash  (CamelStore *store, CamelException *ex);
static CamelFolder *digest_get_junk  (CamelStore *store, CamelException *ex);

static CamelFolderInfo *digest_get_folder_info (CamelStore *store, const gchar *top, guint32 flags, CamelException *ex);

static gint digest_setv (CamelObject *object, CamelException *ex, CamelArgV *args);
static gint digest_getv (CamelObject *object, CamelException *ex, CamelArgGetV *args);

static gpointer parent_class;

static void
digest_store_class_init (CamelDigestStoreClass *class)
{
	CamelObjectClass *camel_object_class;
	CamelStoreClass *store_class;

	parent_class = g_type_class_peek_parent (class);

	camel_object_class = CAMEL_OBJECT_CLASS (class);
	camel_object_class->setv = digest_setv;
	camel_object_class->getv = digest_getv;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->get_folder = digest_get_folder;
	store_class->rename_folder = digest_rename_folder;
	store_class->delete_folder = digest_delete_folder;
	store_class->get_folder_info = digest_get_folder_info;
	store_class->free_folder_info = camel_store_free_folder_info_full;

	store_class->get_trash = digest_get_trash;
	store_class->get_junk = digest_get_junk;
}

static void
digest_store_init (CamelDigestStore *digest_store)
{
	CamelStore *store = CAMEL_STORE (digest_store);

	/* we dont want a vtrash and vjunk on this one */
	store->flags &= ~(CAMEL_STORE_VTRASH | CAMEL_STORE_VJUNK);
}

GType
camel_digest_store_get_type (void)
{
	static GType type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID))
		type = g_type_register_static_simple (
			CAMEL_TYPE_STORE,
			"CamelDigestStore",
			sizeof (CamelDigestStoreClass),
			(GClassInitFunc) digest_store_class_init,
			sizeof (CamelDigestStore),
			(GInstanceInitFunc) digest_store_init,
			0);

	return type;
}

static gint
digest_setv (CamelObject *object, CamelException *ex, CamelArgV *args)
{
	/* CamelDigestStore doesn't currently have anything to set */
	return CAMEL_OBJECT_CLASS (parent_class)->setv (object, ex, args);
}

static gint
digest_getv (CamelObject *object, CamelException *ex, CamelArgGetV *args)
{
	/* CamelDigestStore doesn't currently have anything to get */
	return CAMEL_OBJECT_CLASS (parent_class)->getv (object, ex, args);
}

/**
 * camel_digest_store_new:
 * @url:
 *
 * Create a new CamelDigestStore object.
 *
 * Return value: A new CamelDigestStore widget.
 **/
CamelStore *
camel_digest_store_new (const gchar *url)
{
	CamelStore *store;
	CamelURL *uri;

	uri = camel_url_new (url, NULL);
	if (!uri)
		return NULL;

	store = g_object_new (CAMEL_TYPE_DIGEST_STORE, NULL);
	CAMEL_SERVICE (store)->url = uri;

	return store;
}

static CamelFolder *
digest_get_folder (CamelStore *store, const gchar *folder_name, guint32 flags, CamelException *ex)
{
	return NULL;
}

static CamelFolder *
digest_get_trash (CamelStore *store, CamelException *ex)
{
	return NULL;
}

static CamelFolder *
digest_get_junk (CamelStore *store, CamelException *ex)
{
	return NULL;
}

static CamelFolderInfo *
digest_get_folder_info (CamelStore *store, const gchar *top, guint32 flags, CamelException *ex)
{
	return NULL;
}

static void
digest_delete_folder (CamelStore *store, const gchar *folder_name, CamelException *ex)
{

}

static void
digest_rename_folder (CamelStore *store, const gchar *old, const gchar *new, CamelException *ex)
{

}
