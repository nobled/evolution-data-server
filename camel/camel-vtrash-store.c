/*
 *  Copyright (C) 2002 Ximian Inc.
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "camel-exception.h"
#include "camel-vtrash-store.h"
#include "camel-vtrash-folder.h"

#include "camel-private.h"

#include <string.h>

#define d(x)

static CamelFolder *vtrash_get_folder (CamelStore *store, const char *folder_name, guint32 flags, CamelException *ex);
static void vtrash_delete_folder(CamelStore *store, const char *folder_name, CamelException *ex);
static void vtrash_rename_folder(CamelStore *store, const char *old, const char *new, CamelException *ex);
static void vtrash_init_trash (CamelStore *store);
static CamelFolder *vtrash_get_trash  (CamelStore *store, CamelException *ex);

static CamelFolderInfo *vtrash_get_folder_info(CamelStore *store, const char *top, guint32 flags, CamelException *ex);

struct _CamelVTrashStorePrivate {
};

#define _PRIVATE(o) (((CamelVTrashStore *)(o))->priv)

static void camel_vtrash_store_class_init (CamelVTrashStoreClass *klass);
static void camel_vtrash_store_init       (CamelVTrashStore *obj);
static void camel_vtrash_store_finalise   (CamelObject *obj);

static CamelStoreClass *camel_vtrash_store_parent;

CamelType
camel_vtrash_store_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_store_get_type (), "CamelVTrashStore",
					    sizeof (CamelVTrashStore),
					    sizeof (CamelVTrashStoreClass),
					    (CamelObjectClassInitFunc) camel_vtrash_store_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_vtrash_store_init,
					    (CamelObjectFinalizeFunc) camel_vtrash_store_finalise);
	}
	
	return type;
}

static void
camel_vtrash_store_class_init (CamelVTrashStoreClass *klass)
{
	CamelStoreClass *store_class = (CamelStoreClass *) klass;
	
	camel_vtrash_store_parent = CAMEL_STORE_CLASS(camel_type_get_global_classfuncs (camel_store_get_type ()));

	/* virtual method overload */
	store_class->get_folder = vtrash_get_folder;
	store_class->rename_folder = vtrash_rename_folder;
	store_class->delete_folder = vtrash_delete_folder;
	store_class->get_folder_info = vtrash_get_folder_info;
	store_class->free_folder_info = camel_store_free_folder_info_full;

	store_class->init_trash = vtrash_init_trash;
	store_class->get_trash = vtrash_get_trash;
}

static void
camel_vtrash_store_init (CamelVTrashStore *obj)
{
	struct _CamelVTrashStorePrivate *p;
	CamelStore *store = (CamelStore *)obj;

	/* we dont want a vtrash on the uh, vtrash */
	store->flags &= ~(CAMEL_STORE_VTRASH);	

	p = _PRIVATE(obj) = g_malloc0(sizeof(*p));
}

static void
camel_vtrash_store_finalise (CamelObject *obj)
{
	CamelVTrashStore *vs = (CamelVTrashStore *)obj;

	g_free(vs->priv);
}

/**
 * camel_vtrash_store_new:
 *
 * Create a new CamelVTrashStore object.
 * 
 * Return value: A new CamelVTrashStore widget.
 **/
CamelVTrashStore *
camel_vtrash_store_new (void)
{
	CamelVTrashStore *new = CAMEL_VTRASH_STORE(camel_object_new(camel_vtrash_store_get_type ()));
	return new;
}

/* create a 'name' that is used on the vtrash */
void
camel_vtrash_store_hash_store(CamelStore *store, char buffer[16])
{
	MD5Context ctx;
	unsigned char digest[16];
	unsigned int state = 0, save = 0;
	char *tmp;
	int i;

	md5_init(&ctx);
	tmp = camel_service_get_url((CamelService *)folder->parent_store);
	md5_update(&ctx, tmp, strlen(tmp));
	g_free(tmp);
	md5_final(&ctx, digest);
	base64_encode_close(digest, 12, FALSE, buffer, &state, &save);

	for (i=0;i<16;i++) {
		if (buffer[i] == '+')
			buffer[i] = '.';
		if (buffer[i] == '/')
			buffer[i] = '_';
	}
}

CamelVTrashFolder *
camel_vtrash_store_new_folder(CamelVTrashStore *store, CamelStore *parent)
{
	char hash[17];

	camel_vtrash_store_hash_store(parent, hash);
	hash[16] = 0;

	return (CamelVTrashFolder *)camel_store_get_folder((CamelStore *)store, hash, 0, NULL);
}

static CamelFolder *
vtrash_get_folder (CamelStore *store, const char *folder_name, guint32 flags, CamelException *ex)
{
	CamelVTrashFolder *vf;

	vf = (CamelVTrashFolder *)camel_vtrash_folder_new(store, folder_name);

	return (CamelFolder *)vf;
}

static void
vtrash_init_trash (CamelStore *store)
{
	/* no-op */
}

static CamelFolder *
vtrash_get_trash (CamelStore *store, CamelException *ex)
{
	camel_exception_setv(ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
			     _("Cannot get VTrash trash folder: Invalid operation"));

	return NULL;
}

static CamelFolderInfo *
vtrash_get_folder_info(CamelStore *store, const char *top, guint32 flags, CamelException *ex)
{
	camel_exception_setv(ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
			     _("Cannot get VTrash folder info: Invalid operation"));

	return NULL;
}

static void
vtrash_delete_folder(CamelStore *store, const char *folder_name, CamelException *ex)
{
	camel_exception_setv(ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
			     _("Cannot delete folder: %s: Invalid operation"), folder_name);
}

static void
vtrash_rename_folder(CamelStore *store, const char *old, const char *new, CamelException *ex)
{
	camel_exception_setv(ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
			     _("Cannot rename folder: %s: Invalid operation"), old);
}
