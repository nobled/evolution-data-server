/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2000-2003 Ximian Inc.
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *           Jeffrey Stedfast <fejj@ximian.com>
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

#include <string.h>
#include <pthread.h>

#include "camel-exception.h"
#include "camel-vee-folder.h"
#include "camel-store.h"
#include "camel-mime-message.h"
#include "camel-folder-search.h"
#include "camel-vee-summary.h"

#include "camel-session.h"
#include "camel-vee-store.h"	/* for open flags */
#include "camel-private.h"
#include "camel-debug.h"
#include "camel-i18n.h"

#include "e-util/md5-utils.h"

#if defined (DOEPOOLV) || defined (DOESTRV)
#include "e-util/e-memory.h"
#endif

#define d(x) 
#define dd(x) (camel_debug("vfolder")?(x):0)

#define _PRIVATE(o) (((CamelVeeFolder *)(o))->priv)

#if 0
static void vee_refresh_info(CamelFolder *folder, CamelException *ex);

static void vee_sync (CamelFolder *folder, gboolean expunge, CamelException *ex);
static void vee_expunge (CamelFolder *folder, CamelException *ex);

static void vee_freeze(CamelFolder *folder);
static void vee_thaw(CamelFolder *folder);

static CamelMimeMessage *vee_get_message (CamelFolder *folder, const gchar *uid, CamelException *ex);
static void vee_append_message(CamelFolder *folder, CamelMimeMessage *message, const CamelMessageInfo *info, char **appended_uid, CamelException *ex);
static void vee_transfer_messages_to(CamelFolder *source, GPtrArray *uids, CamelFolder *dest, GPtrArray **transferred_uids, gboolean delete_originals, CamelException *ex);

static GPtrArray *vee_search_by_expression(CamelFolder *folder, const char *expression, CamelException *ex);
static GPtrArray *vee_search_by_uids(CamelFolder *folder, const char *expression, GPtrArray *uids, CamelException *ex);

static void vee_rename(CamelFolder *folder, const char *new);
#endif

static void camel_vee_folder_class_init (CamelVeeFolderClass *klass);
static void camel_vee_folder_init       (CamelVeeFolder *obj);
static void camel_vee_folder_finalise   (CamelObject *obj);

static int vee_rebuild_folder(CamelVeeFolder *vf, CamelFolder *source, CamelException *ex);
static void vee_folder_remove_folder(CamelVeeFolder *vf, CamelFolder *source);

static void folder_changed(CamelFolder *sub, CamelFolderChangeInfo *changes, CamelVeeFolder *vf);
static void subfolder_deleted(CamelFolder *f, void *event_data, CamelVeeFolder *vf);
static void folder_renamed(CamelFolder *f, const char *old, CamelVeeFolder *vf);

static void folder_changed_remove_uid(CamelFolder *sub, const char *uid, const char hash[8], int keep, CamelVeeFolder *vf);

static CamelFolderClass *camel_vee_folder_parent;

CamelType
camel_vee_folder_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_folder_get_type (), "CamelVeeFolder",
					    sizeof (CamelVeeFolder),
					    sizeof (CamelVeeFolderClass),
					    (CamelObjectClassInitFunc) camel_vee_folder_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_vee_folder_init,
					    (CamelObjectFinalizeFunc) camel_vee_folder_finalise);
	}
	
	return type;
}

void
camel_vee_folder_construct (CamelVeeFolder *vf, CamelStore *parent_store, const char *name, guint32 flags)
{
	CamelFolder *folder = (CamelFolder *)vf;
	const char *tmp;

	vf->flags = flags;
	tmp = strrchr(name, '/');
	if (tmp)
		tmp++;
	else
		tmp = name;
	camel_folder_construct(folder, parent_store, name, tmp);

	folder->summary = camel_vee_summary_new(folder);

	if (CAMEL_IS_VEE_STORE(parent_store))
		vf->parent_vee_store = (CamelVeeStore *)parent_store;
}

/**
 * camel_vee_folder_new:
 * @parent_store: the parent CamelVeeStore
 * @name: the vfolder name
 * @ex: a CamelException
 *
 * Create a new CamelVeeFolder object.
 *
 * Return value: A new CamelVeeFolder widget.
 **/
CamelFolder *
camel_vee_folder_new(CamelStore *parent_store, const char *name, guint32 flags)
{
	CamelVeeFolder *vf;
	char *tmp;
	
	if (CAMEL_IS_VEE_STORE(parent_store) && strcmp(name, CAMEL_UNMATCHED_NAME) == 0) {
		vf = ((CamelVeeStore *)parent_store)->folder_unmatched;
		camel_object_ref(vf);
	} else {
		vf = (CamelVeeFolder *)camel_object_new(camel_vee_folder_get_type());
		camel_vee_folder_construct(vf, parent_store, name, flags);
	}

	d(printf("returning folder %s %p, count = %d\n", name, vf, camel_folder_get_message_count((CamelFolder *)vf)));

	tmp = g_strdup_printf("%s/%s.cmeta", ((CamelService *)parent_store)->url->path, name);
	camel_object_set(vf, NULL, CAMEL_OBJECT_STATE_FILE, tmp, NULL);
	g_free(tmp);
	if (camel_object_state_read(vf) == -1) {
		/* setup defaults: we have none currently */
	}

	return (CamelFolder *)vf;
}

void
camel_vee_folder_set_expression(CamelVeeFolder *vf, const char *query)
{
	((CamelVeeFolderClass *)((CamelObject *)vf)->klass)->set_expression(vf, query);
}

/**
 * camel_vee_folder_add_folder:
 * @vf: Virtual Folder object
 * @sub: source CamelFolder to add to @vf
 *
 * Adds @sub as a source folder to @vf.
 **/
void
camel_vee_folder_add_folder(CamelVeeFolder *vf, CamelFolder *sub)
{
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	int i;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	
	if (vf == (CamelVeeFolder *)sub) {
		g_warning("Adding a virtual folder to itself as source, ignored");
		return;
	}

	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);

	/* for normal vfolders we want only unique ones, for unmatched we want them all recorded */
	if (g_list_find(p->folders, sub) == NULL) {
		camel_object_ref((CamelObject *)sub);
		p->folders = g_list_append(p->folders, sub);
		
		CAMEL_FOLDER_LOCK(vf, change_lock);

		/* update the freeze state of 'sub' to match our freeze state */
		for (i = 0; i < ((CamelFolder *)vf)->priv->frozen; i++)
			camel_folder_freeze(sub);

		CAMEL_FOLDER_UNLOCK(vf, change_lock);
	}
	if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0 && !CAMEL_IS_VEE_FOLDER(sub) && folder_unmatched != NULL) {
		struct _CamelVeeFolderPrivate *up = _PRIVATE(folder_unmatched);
		camel_object_ref((CamelObject *)sub);
		up->folders = g_list_append(up->folders, sub);

		CAMEL_FOLDER_LOCK(folder_unmatched, change_lock);

		/* update the freeze state of 'sub' to match Unmatched's freeze state */
		for (i = 0; i < ((CamelFolder *)folder_unmatched)->priv->frozen; i++)
			camel_folder_freeze(sub);

		CAMEL_FOLDER_UNLOCK(folder_unmatched, change_lock);
	}

	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);

	d(printf("camel_vee_folder_add_folde(%p, %p)\n", vf, sub));

	camel_object_hook_event((CamelObject *)sub, "folder_changed", (CamelObjectEventHookFunc)folder_changed, vf);
	camel_object_hook_event((CamelObject *)sub, "deleted", (CamelObjectEventHookFunc)subfolder_deleted, vf);
	camel_object_hook_event((CamelObject *)sub, "renamed", (CamelObjectEventHookFunc)folder_renamed, vf);

	((CamelVeeFolderClass *)((CamelObject *)vf)->klass)->add_folder(vf, sub);
}

/**
 * camel_vee_folder_remove_folder:
 * @vf: Virtual Folder object
 * @sub: source CamelFolder to remove from @vf
 *
 * Removed the source folder, @sub, from the virtual folder, @vf.
 **/
void
camel_vee_folder_remove_folder(CamelVeeFolder *vf, CamelFolder *sub)
{
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	int i;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	
	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);

	CAMEL_VEE_FOLDER_LOCK(vf, changed_lock);
	p->folders_changed = g_list_remove(p->folders_changed, sub);
	CAMEL_VEE_FOLDER_UNLOCK(vf, changed_lock);

	if (g_list_find(p->folders, sub) == NULL) {
		CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);
		return;
	}
	
	camel_object_unhook_event((CamelObject *)sub, "folder_changed", (CamelObjectEventHookFunc) folder_changed, vf);
	camel_object_unhook_event((CamelObject *)sub, "deleted", (CamelObjectEventHookFunc) subfolder_deleted, vf);
	camel_object_unhook_event((CamelObject *)sub, "renamed", (CamelObjectEventHookFunc) folder_renamed, vf);

	p->folders = g_list_remove(p->folders, sub);
	
	/* undo the freeze state that we have imposed on this source folder */
	CAMEL_FOLDER_LOCK(vf, change_lock);
	for (i = 0; i < ((CamelFolder *)vf)->priv->frozen; i++)
		camel_folder_thaw(sub);
	CAMEL_FOLDER_UNLOCK(vf, change_lock);
	
	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);

	if (folder_unmatched != NULL) {
		struct _CamelVeeFolderPrivate *up = _PRIVATE(folder_unmatched);

		CAMEL_VEE_FOLDER_LOCK(folder_unmatched, subfolder_lock);
		/* if folder deleted, then blow it away from unmatched always, and remove all refs to it */
		if (sub->folder_flags & CAMEL_FOLDER_HAS_BEEN_DELETED) {
			while (g_list_find(up->folders, sub)) {
				up->folders = g_list_remove(up->folders, sub);
				camel_object_unref((CamelObject *)sub);
				
				/* undo the freeze state that Unmatched has imposed on this source folder */
				CAMEL_FOLDER_LOCK(folder_unmatched, change_lock);
				for (i = 0; i < ((CamelFolder *)folder_unmatched)->priv->frozen; i++)
					camel_folder_thaw(sub);
				CAMEL_FOLDER_UNLOCK(folder_unmatched, change_lock);
			}
		} else if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0) {
			if (g_list_find(up->folders, sub) != NULL) {
				up->folders = g_list_remove(up->folders, sub);
				camel_object_unref((CamelObject *)sub);
				
				/* undo the freeze state that Unmatched has imposed on this source folder */
				CAMEL_FOLDER_LOCK(folder_unmatched, change_lock);
				for (i = 0; i < ((CamelFolder *)folder_unmatched)->priv->frozen; i++)
					camel_folder_thaw(sub);
				CAMEL_FOLDER_UNLOCK(folder_unmatched, change_lock);
			}
		}
		CAMEL_VEE_FOLDER_UNLOCK(folder_unmatched, subfolder_lock);
	}

	((CamelVeeFolderClass *)((CamelObject *)vf)->klass)->remove_folder(vf, sub);

	camel_object_unref((CamelObject *)sub);
}

/**
 * camel_vee_folder_rebuild_folder:
 * @vf: Virtual Folder object
 * @sub: source CamelFolder to add to @vf
 * @ex: Exception.
 *
 * Rebuild the folder @sub, if it should be.
 **/
int
camel_vee_folder_rebuild_folder(CamelVeeFolder *vf, CamelFolder *sub, CamelException *ex)
{
	return ((CamelVeeFolderClass *)((CamelObject *)vf)->klass)->rebuild_folder(vf, sub, ex);
}

static void
remove_folders(CamelFolder *folder, CamelFolder *foldercopy, CamelVeeFolder *vf)
{
	camel_vee_folder_remove_folder(vf, folder);
	camel_object_unref((CamelObject *)folder);
}

/**
 * camel_vee_folder_set_folders:
 * @vf: 
 * @folders: 
 * 
 * Set the whole list of folder sources on a vee folder.
 **/
void
camel_vee_folder_set_folders(CamelVeeFolder *vf, GList *folders)
{
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	GHashTable *remove = g_hash_table_new(NULL, NULL);
	GList *l;
	CamelFolder *folder;

	/* setup a table of all folders we have currently */
	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);
	l = p->folders;
	while (l) {
		g_hash_table_insert(remove, l->data, l->data);
		camel_object_ref((CamelObject *)l->data);
		l = l->next;
	}
	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);

	/* if we already have the folder, ignore it, otherwise add it */
	l = folders;
	while (l) {
		if ((folder = g_hash_table_lookup(remove, l->data))) {
			g_hash_table_remove(remove, folder);
			camel_object_unref((CamelObject *)folder);
		} else {
			camel_vee_folder_add_folder(vf, l->data);
		}
		l = l->next;
	}

	/* then remove any we still have */
	g_hash_table_foreach(remove, (GHFunc)remove_folders, vf);
	g_hash_table_destroy(remove);
}

/**
 * camel_vee_folder_hash_folder:
 * @folder: 
 * @: 
 * 
 * Create a hash string representing the folder name, which should be
 * unique, and remain static for a given folder.
 **/
void
camel_vee_folder_hash_folder(CamelFolder *folder, char buffer[8])
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
	md5_update(&ctx, folder->full_name, strlen(folder->full_name));
	md5_final(&ctx, digest);
	camel_base64_encode_close(digest, 6, FALSE, buffer, &state, &save);

	for (i=0;i<8;i++) {
		if (buffer[i] == '+')
			buffer[i] = '.';
		if (buffer[i] == '/')
			buffer[i] = '_';
	}
}

/**
 * camel_vee_folder_get_location:
 * @vf: 
 * @vinfo: 
 * @realuid: if not NULL, set to the uid of the real message, must be
 * g_free'd by caller.
 * 
 * Find the real folder (and uid)
 * 
 * Return value: 
 **/
CamelFolder *
camel_vee_folder_get_location(CamelVeeFolder *vf, const CamelVeeMessageInfo *vinfo, char **realuid)
{
	CamelFolder *folder;

	folder = vinfo->real->summary->folder;

	/* locking?  yes?  no?  although the vfolderinfo is valid when obtained
	   the folder in it might not necessarily be so ...? */
	if (CAMEL_IS_VEE_FOLDER(folder)) {
		CamelFolder *res;
		const CamelVeeMessageInfo *vfinfo;

		vfinfo = (CamelVeeMessageInfo *)camel_folder_get_message_info(folder, camel_message_info_uid(vinfo)+8);
		res = camel_vee_folder_get_location((CamelVeeFolder *)folder, vfinfo, realuid);
		camel_folder_free_message_info(folder, (CamelMessageInfo *)vfinfo);
		return res;
	} else {
		if (realuid)
			*realuid = g_strdup(camel_message_info_uid(vinfo)+8);

		return folder;
	}
}

static void vee_refresh_info(CamelFolder *folder, CamelException *ex)
{
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	GList *node, *list;

	CAMEL_VEE_FOLDER_LOCK(vf, changed_lock);
	list = p->folders_changed;
	p->folders_changed = NULL;
	CAMEL_VEE_FOLDER_UNLOCK(vf, changed_lock);

	node = list;
	while (node) {
		CamelFolder *f = node->data;

		if (camel_vee_folder_rebuild_folder(vf, f, ex) == -1)
			break;

		node = node->next;
	}

	g_list_free(list);
}

static void
vee_sync(CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	GList *node;

	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);

	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;

		camel_folder_sync(f, expunge, ex);
		if (camel_exception_is_set(ex)) {
			char *desc;

			camel_object_get(f, NULL, CAMEL_OBJECT_DESCRIPTION, &desc, NULL);
			camel_exception_setv(ex, ex->id, _("Error storing `%s': %s"), desc, ex->desc);
			break;
		}

		/* auto update vfolders shouldn't need a rebuild */
		if ((vf->flags & CAMEL_STORE_VEE_FOLDER_AUTO) == 0
		    && camel_vee_folder_rebuild_folder(vf, f, ex) == -1)
			break;

		node = node->next;
	}

	if (node == NULL) {
		CAMEL_VEE_FOLDER_LOCK(vf, changed_lock);
		g_list_free(p->folders_changed);
		p->folders_changed = NULL;
		CAMEL_VEE_FOLDER_UNLOCK(vf, changed_lock);
	}

	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);

	camel_object_state_write(vf);
}

static void
vee_expunge (CamelFolder *folder, CamelException *ex)
{
	((CamelFolderClass *)((CamelObject *)folder)->klass)->sync(folder, TRUE, ex);
}

static CamelMimeMessage *
vee_get_message(CamelFolder *folder, const char *uid, CamelException *ex)
{
	CamelVeeMessageInfo *mi;
	CamelMimeMessage *msg = NULL;

	mi = (CamelVeeMessageInfo *)camel_folder_summary_uid(folder->summary, uid);
	if (mi) {
		msg =  camel_folder_get_message(mi->real->summary->folder, camel_message_info_uid(mi)+8, ex);
		camel_message_info_free((CamelMessageInfo *)mi);
	} else {
		camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				     _("No such message %s in %s"), uid,
				     folder->name);
	}

	return msg;
}

static GPtrArray *
vee_search_by_expression(CamelFolder *folder, const char *expression, CamelException *ex)
{
	GList *node;
	GPtrArray *matches, *result = g_ptr_array_new ();
	char *expr;
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	GHashTable *searched = g_hash_table_new(NULL, NULL);
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	
	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);
	
	if (vf != folder_unmatched)
		expr = g_strdup_printf ("(and %s %s)", vf->expression, expression);
	else
		expr = g_strdup (expression);
	
	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;
		int i;
		char hash[8];
		
		/* make sure we only search each folder once - for unmatched folder to work right */
		if (g_hash_table_lookup(searched, f) == NULL) {
			camel_vee_folder_hash_folder(f, hash);
			/* FIXME: shouldn't ignore search exception */
			matches = camel_folder_search_by_expression(f, expression, NULL);
			if (matches) {
				for (i = 0; i < matches->len; i++) {
					char *uid = matches->pdata[i], *vuid;

					vuid = g_malloc(strlen(uid)+9);
					memcpy(vuid, hash, 8);
					strcpy(vuid+8, uid);
					g_ptr_array_add(result, vuid);
				}
				camel_folder_search_free(f, matches);
			}
			g_hash_table_insert(searched, f, f);
		}
		node = g_list_next(node);
	}

	g_free(expr);
	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);

	g_hash_table_destroy(searched);

	return result;
}

static GPtrArray *
vee_search_by_uids(CamelFolder *folder, const char *expression, GPtrArray *uids, CamelException *ex)
{
	GList *node;
	GPtrArray *matches, *result = g_ptr_array_new ();
	GPtrArray *folder_uids = g_ptr_array_new();
	char *expr;
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	GHashTable *searched = g_hash_table_new(NULL, NULL);

	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);

	expr = g_strdup_printf("(and %s %s)", vf->expression, expression);
	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;
		int i;
		char hash[8];
		
		/* make sure we only search each folder once - for unmatched folder to work right */
		if (g_hash_table_lookup(searched, f) == NULL) {
			camel_vee_folder_hash_folder(f, hash);

			/* map the vfolder uid's to the source folder uid's first */
			g_ptr_array_set_size(folder_uids, 0);
			for (i=0;i<uids->len;i++) {
				char *uid = uids->pdata[i];

				if (strlen(uid) >= 8 && strncmp(uid, hash, 8) == 0)
					g_ptr_array_add(folder_uids, uid+8);
			}
			if (folder_uids->len > 0) {
				matches = camel_folder_search_by_uids(f, expression, folder_uids, ex);
				if (matches) {
					for (i = 0; i < matches->len; i++) {
						char *uid = matches->pdata[i], *vuid;
				
						vuid = g_malloc(strlen(uid)+9);
						memcpy(vuid, hash, 8);
						strcpy(vuid+8, uid);
						g_ptr_array_add(result, vuid);
					}
					camel_folder_search_free(f, matches);
				} else {
					g_warning("Search failed: %s", camel_exception_get_description(ex));
				}
			}
			g_hash_table_insert(searched, f, f);
		}
		node = g_list_next(node);
	}

	g_free(expr);
	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);

	g_hash_table_destroy(searched);
	g_ptr_array_free(folder_uids, TRUE);

	return result;
}

static void
vee_append_message(CamelFolder *folder, CamelMimeMessage *message, const CamelMessageInfo *info, char **appended_uid, CamelException *ex)
{
	camel_exception_set(ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot copy or move messages into a Virtual Folder"));
}

static void
vee_transfer_messages_to (CamelFolder *folder, GPtrArray *uids, CamelFolder *dest, GPtrArray **transferred_uids, gboolean delete_originals, CamelException *ex)
{
	camel_exception_set(ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot copy or move messages into a Virtual Folder"));
}

static void vee_rename(CamelFolder *folder, const char *new)
{
	/*CamelVeeFolder *vf = (CamelVeeFolder *)folder;*/

	((CamelFolderClass *)camel_vee_folder_parent)->rename(folder, new);
}

static void vee_delete(CamelFolder *folder)
{
	struct _CamelVeeFolderPrivate *p = _PRIVATE(folder);

	/* NB: this is never called on UNMTACHED */

	CAMEL_VEE_FOLDER_LOCK(folder, subfolder_lock);
	while (p->folders) {
		CamelFolder *f = p->folders->data;

		camel_object_ref(f);
		CAMEL_VEE_FOLDER_UNLOCK(folder, subfolder_lock);

		camel_vee_folder_remove_folder((CamelVeeFolder *)folder, f);
		camel_object_unref(f);
		CAMEL_VEE_FOLDER_LOCK(folder, subfolder_lock);
	}
	CAMEL_VEE_FOLDER_LOCK(folder, subfolder_lock);

	((CamelFolderClass *)camel_vee_folder_parent)->delete(folder);
}

/* ********************************************************************** *
   utility functions */

/* must be called with summary_lock held */
static CamelVeeMessageInfo *
vee_folder_add_uid(CamelVeeFolder *vf, CamelFolder *f, const char *inuid, const char hash[8])
{
	CamelMessageInfo *info;
	CamelVeeMessageInfo *mi = NULL;

	info = camel_folder_get_message_info(f, inuid);
	if (info) {
		mi = camel_vee_summary_add((CamelVeeSummary *)((CamelFolder *)vf)->summary, info, hash);
		camel_folder_free_message_info(f, info);
	}
	return mi;
}

static void
vee_folder_remove_folder(CamelVeeFolder *vf, CamelFolder *source)
{
	int i, count, n, still = FALSE, start, last;
	char *oldkey;
	CamelFolder *folder = (CamelFolder *)vf;
	char hash[8];
	/*struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);*/
	CamelFolderChangeInfo *vf_changes = NULL, *unmatched_changes = NULL;
	void *oldval;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	GHashTable *unmatched_uids = vf->parent_vee_store ? vf->parent_vee_store->unmatched_uids : NULL;
	CamelFolderSummary *ssummary = source->summary;
	int killun = FALSE;

	if (vf == folder_unmatched)
		return;

	if ((source->folder_flags & CAMEL_FOLDER_HAS_BEEN_DELETED))
		killun = TRUE;

	CAMEL_VEE_FOLDER_LOCK(vf, summary_lock);

	if (folder_unmatched != NULL) {
		/* check if this folder is still to be part of unmatched */
		if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0 && !killun) {
			CAMEL_VEE_FOLDER_LOCK(folder_unmatched, subfolder_lock);
			still = g_list_find(_PRIVATE(folder_unmatched)->folders, source) != NULL;
			CAMEL_VEE_FOLDER_UNLOCK(folder_unmatched, subfolder_lock);
			camel_vee_folder_hash_folder(source, hash);
		}

		CAMEL_VEE_FOLDER_LOCK(folder_unmatched, summary_lock);

		/* See if we just blow all uid's from this folder away from unmatched, regardless */
		if (killun) {
			start = -1;
			last = -1;
			count = camel_folder_summary_count(((CamelFolder *)folder_unmatched)->summary);
			for (i=0;i<count;i++) {
				CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)camel_folder_summary_index(((CamelFolder *)folder_unmatched)->summary, i);
				
				if (mi) {
					if (mi->real->summary == ssummary) {
						camel_folder_change_info_remove_uid(folder_unmatched->changes, camel_message_info_uid(mi));
						if (last == -1) {
							last = start = i;
						} else if (last+1 == i) {
							last = i;
						} else {
							camel_folder_summary_remove_range(((CamelFolder *)folder_unmatched)->summary, start, last);
							i -= (last-start)+1;
							start = last = i;
						}
					}
					camel_message_info_free((CamelMessageInfo *)mi);
				}
			}
			if (last != -1)
				camel_folder_summary_remove_range(((CamelFolder *)folder_unmatched)->summary, start, last);
		}
	}

	start = -1;
	last = -1;
	count = camel_folder_summary_count(folder->summary);
	for (i=0;i<count;i++) {
		CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)camel_folder_summary_index(folder->summary, i);
		if (mi) {
			if (mi->real->summary == ssummary) {
				const char *uid = camel_message_info_uid(mi);

				camel_folder_change_info_remove_uid(vf->changes, uid);

				if (last == -1) {
					last = start = i;
				} else if (last+1 == i) {
					last = i;
				} else {
					camel_folder_summary_remove_range(folder->summary, start, last);
					i -= (last-start)+1;
					start = last = i;
				}
				if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0 && folder_unmatched != NULL) {
					if (still) {
						if (g_hash_table_lookup_extended(unmatched_uids, uid, (void **)&oldkey, &oldval)) {
							n = GPOINTER_TO_INT (oldval);
							if (n == 1) {
								g_hash_table_remove(unmatched_uids, oldkey);
								if (vee_folder_add_uid(folder_unmatched, source, oldkey+8, hash))
									camel_folder_change_info_add_uid(folder_unmatched->changes, oldkey);
								g_free(oldkey);
							} else {
								g_hash_table_insert(unmatched_uids, oldkey, GINT_TO_POINTER(n-1));
							}
						}
					} else {
						if (g_hash_table_lookup_extended(unmatched_uids, camel_message_info_uid(mi), (void **)&oldkey, &oldval)) {
							g_hash_table_remove(unmatched_uids, oldkey);
							g_free(oldkey);
						}
					}
				}
			}
			camel_message_info_free((CamelMessageInfo *)mi);
		}
	}

	if (last != -1)
		camel_folder_summary_remove_range(folder->summary, start, last);

	if (folder_unmatched) {
		if (camel_folder_change_info_changed(folder_unmatched->changes)) {
			unmatched_changes = folder_unmatched->changes;
			folder_unmatched->changes = camel_folder_change_info_new();
		}

		CAMEL_VEE_FOLDER_UNLOCK(folder_unmatched, summary_lock);
	}

	if (camel_folder_change_info_changed(vf->changes)) {
		vf_changes = vf->changes;
		vf->changes = camel_folder_change_info_new();
	}

	CAMEL_VEE_FOLDER_UNLOCK(vf, summary_lock);

	if (unmatched_changes) {
		camel_object_trigger_event((CamelObject *)folder_unmatched, "folder_changed", unmatched_changes);
		camel_folder_change_info_free(unmatched_changes);
	}

	if (vf_changes) {
		camel_object_trigger_event((CamelObject *)vf, "folder_changed", vf_changes);
		camel_folder_change_info_free(vf_changes);
	}
}

struct _update_data {
	CamelFolder *source;
	CamelVeeFolder *vf;
	char hash[8];
	CamelVeeFolder *folder_unmatched;
	GHashTable *unmatched_uids;
};

static void
unmatched_check_uid(char *uidin, void *value, struct _update_data *u)
{
	char *uid;
	int n;

	uid = alloca(strlen(uidin)+9);
	memcpy(uid, u->hash, 8);
	strcpy(uid+8, uidin);
	n = GPOINTER_TO_INT(g_hash_table_lookup(u->unmatched_uids, uid));
	if (n == 0) {
		if (vee_folder_add_uid(u->folder_unmatched, u->source, uidin, u->hash))
			camel_folder_change_info_add_uid(u->folder_unmatched->changes, uid);
	} else {
		CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)camel_folder_summary_uid(((CamelFolder *)u->folder_unmatched)->summary, uid);
		if (mi) {
			camel_folder_summary_remove(((CamelFolder *)u->folder_unmatched)->summary, (CamelMessageInfo *)mi);
			camel_folder_change_info_remove_uid(u->folder_unmatched->changes, uid);
			camel_message_info_free((CamelMessageInfo *)mi);
		}
	}
}

static void
folder_added_uid(char *uidin, void *value, struct _update_data *u)
{
	CamelVeeMessageInfo *mi;
	char *oldkey;
	void *oldval;
	int n;

	if ( (mi = vee_folder_add_uid(u->vf, u->source, uidin, u->hash)) ) {
		camel_folder_change_info_add_uid(u->vf->changes, camel_message_info_uid(mi));

		if (!CAMEL_IS_VEE_FOLDER(u->source) && u->unmatched_uids != NULL) {
			if (g_hash_table_lookup_extended(u->unmatched_uids, camel_message_info_uid(mi), (void **)&oldkey, &oldval)) {
				n = GPOINTER_TO_INT (oldval);
				g_hash_table_insert(u->unmatched_uids, oldkey, GINT_TO_POINTER(n+1));
			} else {
				g_hash_table_insert(u->unmatched_uids, g_strdup(camel_message_info_uid(mi)), GINT_TO_POINTER(1));
			}
		}
	}
}

/* build query contents for a single folder */
static int
vee_rebuild_folder(CamelVeeFolder *vf, CamelFolder *source, CamelException *ex)
{
	GPtrArray *match, *all;
	GHashTable *allhash, *matchhash;
	CamelFolder *f = source;
	CamelFolder *folder = (CamelFolder *)vf;
	int i, n, count, start, last;
	struct _update_data u;
	CamelFolderChangeInfo *vf_changes = NULL, *unmatched_changes = NULL;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	GHashTable *unmatched_uids = vf->parent_vee_store ? vf->parent_vee_store->unmatched_uids : NULL;
	CamelFolderSummary *ssummary = source->summary;

	if (vf == folder_unmatched)
		return 0;

	/* if we have no expression, or its been cleared, then act as if no matches */
	if (vf->expression == NULL) {
		match = g_ptr_array_new();
	} else {
		match = camel_folder_search_by_expression(f, vf->expression, ex);
		if (match == NULL)
			return -1;
	}

	u.source = source;
	u.vf = vf;
	u.folder_unmatched = folder_unmatched;
	u.unmatched_uids = unmatched_uids;
	camel_vee_folder_hash_folder(source, u.hash);

	CAMEL_VEE_FOLDER_LOCK(vf, summary_lock);

	/* we build 2 hash tables, one for all uid's not matched, the other for all matched uid's,
	   we just ref the real memory */
	matchhash = g_hash_table_new(g_str_hash, g_str_equal);
	for (i=0;i<match->len;i++)
		g_hash_table_insert(matchhash, match->pdata[i], GINT_TO_POINTER (1));

	allhash = g_hash_table_new(g_str_hash, g_str_equal);
	all = camel_folder_get_uids(f);
	for (i=0;i<all->len;i++)
		if (g_hash_table_lookup(matchhash, all->pdata[i]) == NULL)
			g_hash_table_insert(allhash, all->pdata[i], GINT_TO_POINTER (1));

	if (folder_unmatched != NULL)
		CAMEL_VEE_FOLDER_LOCK(folder_unmatched, summary_lock);

	/* scan, looking for "old" uid's to be removed */
	start = -1;
	last = -1;
	count = camel_folder_summary_count(folder->summary);
	for (i=0;i<count;i++) {
		CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)camel_folder_summary_index(folder->summary, i);

		if (mi) {
			if (mi->real->summary == ssummary) {
				char *uid = (char *)camel_message_info_uid(mi), *oldkey;
				void *oldval;
				
				if (g_hash_table_lookup(matchhash, uid+8) == NULL) {
					if (last == -1) {
						last = start = i;
					} else if (last+1 == i) {
						last = i;
					} else {
						camel_folder_summary_remove_range(folder->summary, start, last);
						i -= (last-start)+1;
						start = last = i;
					}
					camel_folder_change_info_remove_uid(vf->changes, camel_message_info_uid(mi));
					if (!CAMEL_IS_VEE_FOLDER(source)
					    && unmatched_uids != NULL
					    && g_hash_table_lookup_extended(unmatched_uids, uid, (void **)&oldkey, &oldval)) {
						n = GPOINTER_TO_INT (oldval);
						if (n == 1) {
							g_hash_table_remove(unmatched_uids, oldkey);
							g_free(oldkey);
						} else {
							g_hash_table_insert(unmatched_uids, oldkey, GINT_TO_POINTER(n-1));
						}
					}
				} else {
					g_hash_table_remove(matchhash, uid+8);
				}
			}
			camel_message_info_free((CamelMessageInfo *)mi);
		}
	}
	if (last != -1)
		camel_folder_summary_remove_range(folder->summary, start, last);

	/* now matchhash contains any new uid's, add them, etc */
	g_hash_table_foreach(matchhash, (GHFunc)folder_added_uid, &u);

	if (folder_unmatched != NULL) {
		/* scan unmatched, remove any that have vanished, etc */
		count = camel_folder_summary_count(((CamelFolder *)folder_unmatched)->summary);
		for (i=0;i<count;i++) {
			CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)camel_folder_summary_index(((CamelFolder *)folder_unmatched)->summary, i);

			if (mi) {
				if (mi->real->summary == ssummary) {
					char *uid = (char *)camel_message_info_uid(mi);

					if (g_hash_table_lookup(allhash, uid+8) == NULL) {
						/* no longer exists at all, just remove it entirely */
						camel_folder_summary_remove_index(((CamelFolder *)folder_unmatched)->summary, i);
						camel_folder_change_info_remove_uid(folder_unmatched->changes, camel_message_info_uid(mi));
						i--;
					} else {
						g_hash_table_remove(allhash, uid+8);
					}
				}
				camel_message_info_free((CamelMessageInfo *)mi);
			}
		}

		/* now allhash contains all potentially new uid's for the unmatched folder, process */
		if (!CAMEL_IS_VEE_FOLDER(source))
			g_hash_table_foreach(allhash, (GHFunc)unmatched_check_uid, &u);

		/* copy any changes so we can raise them outside the lock */
		if (camel_folder_change_info_changed(folder_unmatched->changes)) {
			unmatched_changes = folder_unmatched->changes;
			folder_unmatched->changes = camel_folder_change_info_new();
		}

		CAMEL_VEE_FOLDER_UNLOCK(folder_unmatched, summary_lock);
	}

	if (camel_folder_change_info_changed(vf->changes)) {
		vf_changes = vf->changes;
		vf->changes = camel_folder_change_info_new();
	}

	CAMEL_VEE_FOLDER_UNLOCK(vf, summary_lock);

	g_hash_table_destroy(matchhash);
	g_hash_table_destroy(allhash);
	/* if expression not set, we only had a null list */
	if (vf->expression == NULL)
		g_ptr_array_free(match, TRUE);
	else
		camel_folder_search_free(f, match);
	camel_folder_free_uids(f, all);

	if (unmatched_changes) {
		camel_object_trigger_event((CamelObject *)folder_unmatched, "folder_changed", unmatched_changes);
		camel_folder_change_info_free(unmatched_changes);
	}

	if (vf_changes) {
		camel_object_trigger_event((CamelObject *)vf, "folder_changed", vf_changes);
		camel_folder_change_info_free(vf_changes);
	}

	return 0;
}

/*

  (match-folder "folder1" "folder2")

 */


/* Hold all these with summary lock and unmatched summary lock held */
static void
folder_changed_add_uid(CamelFolder *sub, const char *uid, const char hash[8], CamelVeeFolder *vf)
{
	CamelVeeMessageInfo *vinfo;
	const char *vuid;
	char *oldkey;
	void *oldval;
	int n;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	GHashTable *unmatched_uids = vf->parent_vee_store ? vf->parent_vee_store->unmatched_uids : NULL;

	vinfo = vee_folder_add_uid(vf, sub, uid, hash);
	if (vinfo == NULL)
		return;
	
	vuid = camel_message_info_uid(vinfo);
	camel_folder_change_info_add_uid(vf->changes,  vuid);

	if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0 && !CAMEL_IS_VEE_FOLDER(sub) && folder_unmatched != NULL) {
		if (g_hash_table_lookup_extended(unmatched_uids, vuid, (void **)&oldkey, &oldval)) {
			n = GPOINTER_TO_INT (oldval);
			g_hash_table_insert(unmatched_uids, oldkey, GINT_TO_POINTER(n+1));
		} else {
			g_hash_table_insert(unmatched_uids, g_strdup(vuid), GINT_TO_POINTER (1));
		}
		vinfo = (CamelVeeMessageInfo *)camel_folder_get_message_info((CamelFolder *)folder_unmatched, vuid);
		if (vinfo) {
			camel_folder_change_info_remove_uid(folder_unmatched->changes, vuid);
			camel_folder_summary_remove(((CamelFolder *)folder_unmatched)->summary, (CamelMessageInfo *)vinfo);
			camel_folder_free_message_info((CamelFolder *)folder_unmatched, (CamelMessageInfo *)vinfo);
		}
	}
}

static void
folder_changed_remove_uid(CamelFolder *sub, const char *uid, const char hash[8], int keep, CamelVeeFolder *vf)
{
	CamelFolder *folder = (CamelFolder *)vf;
	char *vuid, *oldkey;
	void *oldval;
	int n;
	CamelVeeMessageInfo *vinfo;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	GHashTable *unmatched_uids = vf->parent_vee_store ? vf->parent_vee_store->unmatched_uids : NULL;

	vuid = alloca(strlen(uid)+9);
	memcpy(vuid, hash, 8);
	strcpy(vuid+8, uid);

	vinfo = (CamelVeeMessageInfo *)camel_folder_summary_uid(folder->summary, vuid);
	if (vinfo) {
		camel_folder_change_info_remove_uid(vf->changes, vuid);
		camel_folder_summary_remove(folder->summary, (CamelMessageInfo *)vinfo);
		camel_message_info_free((CamelMessageInfo *)vinfo);
	}

	if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0 && !CAMEL_IS_VEE_FOLDER(sub) && folder_unmatched != NULL) {
		if (keep) {
			if (g_hash_table_lookup_extended(unmatched_uids, vuid, (void **)&oldkey, &oldval)) {
				n = GPOINTER_TO_INT (oldval);
				if (n == 1) {
					g_hash_table_remove(unmatched_uids, oldkey);
					if (vee_folder_add_uid(folder_unmatched, sub, uid, hash))
						camel_folder_change_info_add_uid(folder_unmatched->changes, oldkey);
					g_free(oldkey);
				} else {
					g_hash_table_insert(unmatched_uids, oldkey, GINT_TO_POINTER(n-1));
				}
			} else {
				if (vee_folder_add_uid(folder_unmatched, sub, uid, hash))
					camel_folder_change_info_add_uid(folder_unmatched->changes, oldkey);
			}
		} else {
			if (g_hash_table_lookup_extended(unmatched_uids, vuid, (void **)&oldkey, &oldval)) {
				g_hash_table_remove(unmatched_uids, oldkey);
				g_free(oldkey);
			}

			vinfo = (CamelVeeMessageInfo *)camel_folder_get_message_info((CamelFolder *)folder_unmatched, vuid);
			if (vinfo) {
				camel_folder_change_info_remove_uid(folder_unmatched->changes, vuid);
				camel_folder_summary_remove_uid(((CamelFolder *)folder_unmatched)->summary, vuid);
				camel_folder_free_message_info((CamelFolder *)folder_unmatched, (CamelMessageInfo *)vinfo);
			}
		}
	}
}

static void
folder_changed_change_uid(CamelFolder *sub, const char *uid, const char hash[8], CamelVeeFolder *vf)
{
	char *vuid;
	CamelVeeMessageInfo *vinfo, *uinfo = NULL;
	CamelMessageInfo *info;
	CamelFolder *folder = (CamelFolder *)vf;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;

	vuid = alloca(strlen(uid)+9);
	memcpy(vuid, hash, 8);
	strcpy(vuid+8, uid);

	vinfo = (CamelVeeMessageInfo *)camel_folder_summary_uid(folder->summary, vuid);
	if (folder_unmatched != NULL)
		uinfo = (CamelVeeMessageInfo *)camel_folder_summary_uid(((CamelFolder *)folder_unmatched)->summary, vuid);
	if (vinfo || uinfo) {
		info = camel_folder_get_message_info(sub, uid);
		if (info) {
			if (vinfo) {
				camel_folder_change_info_change_uid(vf->changes, vuid);
				camel_message_info_free((CamelMessageInfo *)vinfo);
			}

			if (uinfo) {
				camel_folder_change_info_change_uid(folder_unmatched->changes, vuid);
				camel_message_info_free((CamelMessageInfo *)uinfo);
			}

			camel_folder_free_message_info(sub, info);
		} else {
			if (vinfo) {
				folder_changed_remove_uid(sub, uid, hash, FALSE, vf);
				camel_message_info_free((CamelMessageInfo *)vinfo);
			}
			if (uinfo)
				camel_message_info_free((CamelMessageInfo *)uinfo);
		}
	}
}

struct _folder_changed_msg {
	CamelSessionThreadMsg msg;
	CamelFolderChangeInfo *changes;
	CamelFolder *sub;
	CamelVeeFolder *vf;
};

static void
folder_changed_change(CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _folder_changed_msg *m = (struct _folder_changed_msg *)msg;
	CamelFolder *sub = m->sub;
	CamelFolder *folder = (CamelFolder *)m->vf;
	CamelVeeFolder *vf = m->vf;
	CamelFolderChangeInfo *changes = m->changes;
	char *vuid = NULL, hash[8];
	const char *uid;
	CamelVeeMessageInfo *vinfo;
	int i, vuidlen = 0;
	CamelFolderChangeInfo *vf_changes = NULL, *unmatched_changes = NULL;
	GPtrArray *matches_added = NULL, /* newly added, that match */
		*matches_changed = NULL, /* newly changed, that now match */
		*newchanged = NULL,
		*changed;
	GPtrArray *always_changed = NULL;
	GHashTable *matches_hash;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	GHashTable *unmatched_uids = vf->parent_vee_store ? vf->parent_vee_store->unmatched_uids : NULL;

	/* Check the folder hasn't beem removed while we weren't watching */
	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);
	if (g_list_find(_PRIVATE(vf)->folders, sub) == NULL) {
		CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);
		return;
	}

	camel_vee_folder_hash_folder(sub, hash);

	/* Lookup anything before we lock anything, to avoid deadlock with build_folder */

	/* Find newly added that match */
	if (changes->uid_added->len > 0) {
		dd(printf(" Searching for added matches '%s'\n", vf->expression));
		matches_added = camel_folder_search_by_uids(sub, vf->expression, changes->uid_added, NULL);
	}

	/* TODO:
	   In this code around here, we can work out if the search will affect the changes
	   we had, and only re-search against them if they might have */

	/* Search for changed items that newly match, but only if we dont have them */
	changed = changes->uid_changed;
	if (changed->len > 0) {
		dd(printf(" Searching for changed matches '%s'\n", vf->expression));

		if ((vf->flags & CAMEL_STORE_VEE_FOLDER_AUTO) == 0) {
			newchanged = g_ptr_array_new();
			always_changed = g_ptr_array_new();
			for (i=0;i<changed->len;i++) {
				uid = changed->pdata[i];
				if (strlen(uid)+9 > vuidlen) {
					vuidlen = strlen(uid)+64;
					vuid = g_realloc(vuid, vuidlen);
				}
				memcpy(vuid, hash, 8);
				strcpy(vuid+8, uid);
				vinfo = (CamelVeeMessageInfo *)camel_folder_summary_uid(folder->summary, vuid);
				if (vinfo == NULL) {
					g_ptr_array_add(newchanged, (char *)uid);
				} else {
					g_ptr_array_add(always_changed, (char *)uid);
					camel_message_info_free((CamelMessageInfo *)vinfo);
				}
			}
			changed = newchanged;
		}

		if (changed->len)
			matches_changed = camel_folder_search_by_uids(sub, vf->expression, changed, NULL);
	}

	CAMEL_VEE_FOLDER_LOCK(vf, summary_lock);
	if (folder_unmatched != NULL)
		CAMEL_VEE_FOLDER_LOCK(folder_unmatched, summary_lock);

	dd(printf("Vfolder '%s' subfolder changed '%s'\n", folder->full_name, sub->full_name));
	dd(printf(" changed %d added %d removed %d\n", changes->uid_changed->len, changes->uid_added->len, changes->uid_removed->len));

	/* Always remove removed uid's, in any case */
	for (i=0;i<changes->uid_removed->len;i++) {
		dd(printf("  removing uid '%s'\n", (char *)changes->uid_removed->pdata[i]));
		folder_changed_remove_uid(sub, changes->uid_removed->pdata[i], hash, FALSE, vf);
	}

	/* Add any newly matched or to unmatched folder if they dont */
	if (matches_added) {
		matches_hash = g_hash_table_new(g_str_hash, g_str_equal);
		for (i=0;i<matches_added->len;i++) {
			dd(printf(" %s", (char *)matches_added->pdata[i]));
			g_hash_table_insert(matches_hash, matches_added->pdata[i], matches_added->pdata[i]);
		}
		for (i=0;i<changes->uid_added->len;i++) {
			uid = changes->uid_added->pdata[i];
			if (g_hash_table_lookup(matches_hash, uid)) {
				dd(printf("  adding uid '%s' [newly matched]\n", (char *)uid));
				folder_changed_add_uid(sub, uid, hash, vf);
			} else if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0) {
				if (strlen(uid)+9 > vuidlen) {
					vuidlen = strlen(uid)+64;
					vuid = g_realloc(vuid, vuidlen);
				}
				memcpy(vuid, hash, 8);
				strcpy(vuid+8, uid);
				
				if (!CAMEL_IS_VEE_FOLDER(sub) && folder_unmatched != NULL && g_hash_table_lookup(unmatched_uids, vuid) == NULL) {
					dd(printf("  adding uid '%s' to Unmatched [newly unmatched]\n", (char *)uid));
					vinfo = (CamelVeeMessageInfo *)camel_folder_get_message_info((CamelFolder *)folder_unmatched, vuid);
					if (vinfo == NULL) {
						if (vee_folder_add_uid(folder_unmatched, sub, uid, hash))
							camel_folder_change_info_add_uid(folder_unmatched->changes, vuid);
					} else {
						camel_folder_free_message_info((CamelFolder *)folder_unmatched, (CamelMessageInfo *)vinfo);
					}
				}
			}
		}
		g_hash_table_destroy(matches_hash);
	}

	/* Change any newly changed */
	if (always_changed) {
		for (i=0;i<always_changed->len;i++)
			folder_changed_change_uid(sub, always_changed->pdata[i], hash, vf);
		g_ptr_array_free(always_changed, TRUE);
	}

	/* Change/add/remove any changed */
	if (matches_changed) {
		/* If we are auto-updating, then re-check changed uids still match */
		dd(printf(" Vfolder %supdate\nuids match:", (vf->flags & CAMEL_STORE_VEE_FOLDER_AUTO)?"auto-":""));
		matches_hash = g_hash_table_new(g_str_hash, g_str_equal);
		for (i=0;i<matches_changed->len;i++) {
			dd(printf(" %s", (char *)matches_changed->pdata[i]));
			g_hash_table_insert(matches_hash, matches_changed->pdata[i], matches_changed->pdata[i]);
		}
		dd(printf("\n"));
		for (i=0;i<changed->len;i++) {
			uid = changed->pdata[i];
			if (strlen(uid)+9 > vuidlen) {
				vuidlen = strlen(uid)+64;
				vuid = g_realloc(vuid, vuidlen);
			}
			memcpy(vuid, hash, 8);
			strcpy(vuid+8, uid);
			vinfo = (CamelVeeMessageInfo *)camel_folder_summary_uid(folder->summary, vuid);
			if (vinfo == NULL) {
				if (g_hash_table_lookup(matches_hash, uid)) {
					/* A uid we dont have, but now it matches, add it */
					dd(printf("  adding uid '%s' [newly matched]\n", uid));
					folder_changed_add_uid(sub, uid, hash, vf);
				} else {
					/* A uid we still don't have, just change it (for unmatched) */
					folder_changed_change_uid(sub, uid, hash, vf);
				}
			} else {
				if ((vf->flags & CAMEL_STORE_VEE_FOLDER_AUTO) == 0
				    || g_hash_table_lookup(matches_hash, uid)) {
					/* still match, or we're not auto-updating, change event, (if it changed) */
					dd(printf("  changing uid '%s' [still matches]\n", uid));
					folder_changed_change_uid(sub, uid, hash, vf);
				} else {
					/* No longer matches, remove it, but keep it in unmatched (potentially) */
					dd(printf("  removing uid '%s' [did match]\n", uid));
					folder_changed_remove_uid(sub, uid, hash, TRUE, vf);
				}
				camel_message_info_free((CamelMessageInfo *)vinfo);
			}
		}
		g_hash_table_destroy(matches_hash);
	} else {
		/* stuff didn't match but it changed - check unmatched folder for changes */
		for (i=0;i<changed->len;i++)
			folder_changed_change_uid(sub, changed->pdata[i], hash, vf);
	}

	if (folder_unmatched != NULL) {
		if (camel_folder_change_info_changed(folder_unmatched->changes)) {
			unmatched_changes = folder_unmatched->changes;
			folder_unmatched->changes = camel_folder_change_info_new();
		}
		
		CAMEL_VEE_FOLDER_UNLOCK(folder_unmatched, summary_lock);
	}

	if (camel_folder_change_info_changed(vf->changes)) {
		vf_changes = vf->changes;
		vf->changes = camel_folder_change_info_new();
	}

	CAMEL_VEE_FOLDER_UNLOCK(vf, summary_lock);

	/* Cleanup stuff on our folder */
	if (matches_added)
		camel_folder_search_free(sub, matches_added);

	if (matches_changed)
		camel_folder_search_free(sub, matches_changed);

	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);

	/* cleanup the rest */
	if (newchanged)
		g_ptr_array_free(newchanged, TRUE);

	g_free(vuid);
	
	if (unmatched_changes) {
		camel_object_trigger_event((CamelObject *)folder_unmatched, "folder_changed", unmatched_changes);
		camel_folder_change_info_free(unmatched_changes);
	}
	
	if (vf_changes) {
		/* If not auto-updating, keep track of changed folders for later re-sync */
		if ((vf->flags & CAMEL_STORE_VEE_FOLDER_AUTO) == 0) {
			CAMEL_VEE_FOLDER_LOCK(vf, changed_lock);
			if (g_list_find(vf->priv->folders_changed, sub) != NULL)
				vf->priv->folders_changed = g_list_prepend(vf->priv->folders_changed, sub);
			CAMEL_VEE_FOLDER_UNLOCK(vf, changed_lock);
		}

		camel_object_trigger_event((CamelObject *)vf, "folder_changed", vf_changes);
		camel_folder_change_info_free(vf_changes);
	}
}

static void
folder_changed_free(CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _folder_changed_msg *m = (struct _folder_changed_msg *)msg;

	camel_folder_change_info_free(m->changes);
	camel_object_unref((CamelObject *)m->vf);
	camel_object_unref((CamelObject *)m->sub);
}

static CamelSessionThreadOps folder_changed_ops = {
	folder_changed_change,
	folder_changed_free,
};

static void
folder_changed_base(CamelVeeFolder *vf, CamelFolder *sub, CamelFolderChangeInfo *changes)
{
	struct _folder_changed_msg *m;
	CamelSession *session = ((CamelService *)((CamelFolder *)vf)->parent_store)->session;
	
	m = camel_session_thread_msg_new(session, &folder_changed_ops, sizeof(*m));
	m->changes = camel_folder_change_info_new();
	camel_folder_change_info_cat(m->changes, changes);
	m->sub = sub;
	camel_object_ref((CamelObject *)sub);
	m->vf = vf;
	camel_object_ref((CamelObject *)vf);
	camel_session_thread_queue(session, &m->msg, 0);
}

static void
folder_changed(CamelFolder *sub, CamelFolderChangeInfo *changes, CamelVeeFolder *vf)
{
	((CamelVeeFolderClass *)((CamelObject *)vf)->klass)->folder_changed(vf, sub, changes);
}

/* track vanishing folders */
static void
subfolder_deleted(CamelFolder *f, void *event_data, CamelVeeFolder *vf)
{
	camel_vee_folder_remove_folder(vf, f);
}

static void
subfolder_renamed_update(CamelVeeFolder *vf, CamelFolder *sub, char hash[8])
{
	int count, i;
	CamelFolderChangeInfo *changes = NULL;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	GHashTable *unmatched_uids = vf->parent_vee_store ? vf->parent_vee_store->unmatched_uids : NULL;
	CamelFolderSummary *ssummary = sub->summary;

	CAMEL_VEE_FOLDER_LOCK(vf, summary_lock);

	count = camel_folder_summary_count(((CamelFolder *)vf)->summary);
	for (i=0;i<count;i++) {
		CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)camel_folder_summary_index(((CamelFolder *)vf)->summary, i);
		CamelVeeMessageInfo *vinfo;

		if (mi == NULL)
			continue;

		if (mi->real->summary == ssummary) {
			char *uid = (char *)camel_message_info_uid(mi);
			char *oldkey;
			void *oldval;

			camel_folder_change_info_remove_uid(vf->changes, uid);
			camel_folder_summary_remove(((CamelFolder *)vf)->summary, (CamelMessageInfo *)mi);

			/* works since we always append on the end */
			i--;
			count--;

			vinfo = vee_folder_add_uid(vf, sub, uid+8, hash);
			if (vinfo)
				camel_folder_change_info_add_uid(vf->changes, camel_message_info_uid(vinfo));

			/* check unmatched uid's table for any matches */
			if (vf == folder_unmatched
			    && g_hash_table_lookup_extended(unmatched_uids, uid, (void **)&oldkey, &oldval)) {
				g_hash_table_remove(unmatched_uids, oldkey);
				g_hash_table_insert(unmatched_uids, g_strdup(camel_message_info_uid(vinfo)), oldval);
				g_free(oldkey);
			}
		}

		camel_message_info_free((CamelMessageInfo *)mi);
	}

	if (camel_folder_change_info_changed(vf->changes)) {
		changes = vf->changes;
		vf->changes = camel_folder_change_info_new();
	}

	CAMEL_VEE_FOLDER_UNLOCK(vf, summary_lock);

	if (changes) {
		camel_object_trigger_event((CamelObject *)vf, "folder_changed", changes);
		camel_folder_change_info_free(changes);
	}
}

static void
folder_renamed_base(CamelVeeFolder *vf, CamelFolder *f, const char *old)
{
	char hash[8];
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;

	/* TODO: This could probably be done in another thread, tho it is pretty quick/memory bound */

	/* Life just got that little bit harder, if the folder is renamed, it means it breaks all of our uid's.
	   We need to remove the old uid's, fix them up, then release the new uid's, for the uid's that match this folder */

	camel_vee_folder_hash_folder(f, hash);

	subfolder_renamed_update(vf, f, hash);
	if (folder_unmatched != NULL)
		subfolder_renamed_update(folder_unmatched, f, hash);
}

static void
folder_renamed(CamelFolder *sub, const char *old, CamelVeeFolder *vf)
{
	((CamelVeeFolderClass *)((CamelObject *)vf)->klass)->folder_renamed(vf, sub, old);
}

static void
vee_freeze (CamelFolder *folder)
{
	CamelVeeFolder *vfolder = (CamelVeeFolder *)folder;
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vfolder);
	GList *node;
	
	CAMEL_VEE_FOLDER_LOCK(vfolder, subfolder_lock);
	
	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;
		
		camel_folder_freeze(f);
		node = node->next;
	}
	
	CAMEL_VEE_FOLDER_UNLOCK(vfolder, subfolder_lock);
	
	/* call parent implementation */
	CAMEL_FOLDER_CLASS (camel_vee_folder_parent)->freeze(folder);
}

static void
vee_thaw(CamelFolder *folder)
{
	CamelVeeFolder *vfolder = (CamelVeeFolder *)folder;
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vfolder);
	GList *node;
	
	CAMEL_VEE_FOLDER_LOCK(vfolder, subfolder_lock);
	
	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;
		
		camel_folder_thaw(f);
		node = node->next;
	}
	
	CAMEL_VEE_FOLDER_UNLOCK(vfolder, subfolder_lock);
	
	/* call parent implementation */
	CAMEL_FOLDER_CLASS (camel_vee_folder_parent)->thaw(folder);
}

/* vfolder base implementaitons */
static void
vee_add_folder(CamelVeeFolder *vf, CamelFolder *sub)
{
	vee_rebuild_folder(vf, sub, NULL);
}

static void
vee_remove_folder(CamelVeeFolder *vf, CamelFolder *sub)
{
	vee_folder_remove_folder(vf, sub);
}

static void
vee_set_expression(CamelVeeFolder *vf, const char *query)
{
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	GList *node;

	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);

	/* no change, do nothing */
	if ((vf->expression && query && strcmp(vf->expression, query) == 0)
	    || (vf->expression == NULL && query == NULL)) {
		CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);
		return;
	}

	g_free(vf->expression);
	if (query)
		vf->expression = g_strdup(query);

	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;

		if (camel_vee_folder_rebuild_folder(vf, f, NULL) == -1)
			break;

		node = node->next;
	}

	CAMEL_VEE_FOLDER_LOCK(vf, changed_lock);
	g_list_free(p->folders_changed);
	p->folders_changed = NULL;
	CAMEL_VEE_FOLDER_UNLOCK(vf, changed_lock);

	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);
}

static void
camel_vee_folder_class_init (CamelVeeFolderClass *klass)
{
	CamelFolderClass *folder_class = (CamelFolderClass *) klass;

	camel_vee_folder_parent = CAMEL_FOLDER_CLASS(camel_type_get_global_classfuncs (camel_folder_get_type ()));

	folder_class->refresh_info = vee_refresh_info;
	folder_class->sync = vee_sync;
	folder_class->expunge = vee_expunge;

	folder_class->get_message = vee_get_message;
	folder_class->append_message = vee_append_message;
	folder_class->transfer_messages_to = vee_transfer_messages_to;

	folder_class->search_by_expression = vee_search_by_expression;
	folder_class->search_by_uids = vee_search_by_uids;

	folder_class->rename = vee_rename;
	folder_class->delete = vee_delete;

	folder_class->freeze = vee_freeze;
	folder_class->thaw = vee_thaw;

	klass->set_expression = vee_set_expression;
	klass->add_folder = vee_add_folder;
	klass->remove_folder = vee_remove_folder;
	klass->rebuild_folder = vee_rebuild_folder;
	klass->folder_changed = folder_changed_base;
	klass->folder_renamed = folder_renamed_base;
}

static void
camel_vee_folder_init (CamelVeeFolder *obj)
{
	struct _CamelVeeFolderPrivate *p;
	CamelFolder *folder = (CamelFolder *)obj;

	p = _PRIVATE(obj) = g_malloc0(sizeof(*p));

	folder->folder_flags |= (CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY |
				 CAMEL_FOLDER_HAS_SEARCH_CAPABILITY);

	/* FIXME: what to do about user flags if the subfolder doesn't support them? */
	folder->permanent_flags = CAMEL_MESSAGE_ANSWERED |
		CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT |
		CAMEL_MESSAGE_FLAGGED |
		CAMEL_MESSAGE_SEEN;

	obj->changes = camel_folder_change_info_new();
	obj->search = camel_folder_search_new();
	
	p->summary_lock = g_mutex_new();
	p->subfolder_lock = g_mutex_new();
	p->changed_lock = g_mutex_new();
}

static void
camel_vee_folder_finalise (CamelObject *obj)
{
	CamelVeeFolder *vf = (CamelVeeFolder *)obj;
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	GList *node;

	/* TODO: there may be other leaks? */

	/* This may invoke sub-classes with partially destroyed state, they must deal with this */
	if (vf == folder_unmatched) {
		for (node = p->folders;node;node = g_list_next(node))
			camel_object_unref(node->data);
	} else {
		while (p->folders) {
			CamelFolder *f = p->folders->data;

			camel_vee_folder_remove_folder(vf, f);
		}
	}

	g_free(vf->expression);
	
	g_list_free(p->folders);
	g_list_free(p->folders_changed);

	camel_folder_change_info_free(vf->changes);
	camel_object_unref((CamelObject *)vf->search);
	
	g_mutex_free(p->summary_lock);
	g_mutex_free(p->subfolder_lock);
	g_mutex_free(p->changed_lock);
	
	g_free(p);
}
