/*
 *  Copyright (C) 2000 Ximian Inc.
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

#include "camel-exception.h"
#include "camel-unmatched-folder.h"
#include "camel-store.h"
#include "camel-folder-summary.h"
#include "camel-mime-message.h"
#include "camel-folder-search.h"

#include "camel-session.h"
#include "camel-unmatched-store.h"	/* for open flags */
#include "camel-private.h"

#include "e-util/md5-utils.h"

#if defined (DOEPOOLV) || defined (DOESTRV)
#include "e-util/e-memory.h"
#endif

#define d(x)
extern int camel_verbose_debug;
#define dd(x) (camel_verbose_debug?(x):0)

#define _PRIVATE(o) (((CamelUnmatchedFolder *)(o))->priv)

static void unmatched_refresh_info(CamelFolder *folder, CamelException *ex);

static void unmatched_sync (CamelFolder *folder, guint32 flags, CamelException *ex);

static CamelMimeMessage *unmatched_get_message (CamelFolder *folder, const gchar *uid, CamelException *ex);
static void unmatched_append_message(CamelFolder *folder, CamelMimeMessage *message, const CamelMessageInfo *info, char **appended_uid, CamelException *ex);
static void unmatched_transfer_messages_to(CamelFolder *source, GPtrArray *uids, CamelFolder *dest, GPtrArray **transferred_uids, gboolean delete_originals, CamelException *ex);

static GPtrArray *unmatched_search_by_expression(CamelFolder *folder, const char *expression, CamelException *ex);
static GPtrArray *unmatched_search_by_uids(CamelFolder *folder, const char *expression, GPtrArray *uids, CamelException *ex);

static void unmatched_set_message_flags (CamelFolder *folder, const char *uid, guint32 flags, guint32 set);
static void unmatched_set_message_user_flag (CamelFolder *folder, const char *uid, const char *name, gboolean value);
static void unmatched_rename(CamelFolder *folder, const char *new);

static void camel_unmatched_folder_class_init (CamelUnmatchedFolderClass *klass);
static void camel_unmatched_folder_init       (CamelUnmatchedFolder *obj);
static void camel_unmatched_folder_finalise   (CamelObject *obj);

static void message_changed(CamelFolder *f, const char *uid, CamelUnmatchedFolder *vf);
static void folder_changed(CamelFolder *sub, CamelFolderChangeInfo *changes, CamelUnmatchedFolder *vf);
static void subfolder_deleted(CamelFolder *f, void *event_data, CamelUnmatchedFolder *vf);

static void folder_changed_remove_uid(CamelFolder *sub, const char *uid, const char hash[8], int keep, CamelUnmatchedFolder *vf);

static void unmatched_add_folder(CamelUnmatchedFolder *vf, CamelFolder *sub);
static void unmatched_remove_folder(CamelUnmatchedFolder *vf, CamelFolder *sub);
static void unmatched_rebuild_folder(CamelUnmatchedFolder *vf, CamelFolder *source);

static CamelVeeFolderClass *parent_class;

CamelType
camel_unmatched_folder_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_vee_folder_get_type (), "CamelUnmatchedFolder",
					    sizeof (CamelUnmatchedFolder),
					    sizeof (CamelUnmatchedFolderClass),
					    (CamelObjectClassInitFunc) camel_unmatched_folder_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_unmatched_folder_init,
					    (CamelObjectFinalizeFunc) camel_unmatched_folder_finalise);
	}
	
	return type;
}

static void
camel_unmatched_folder_class_init (CamelUnmatchedFolderClass *klass)
{
	CamelFolderClass *folder_class = (CamelFolderClass *) klass;

	parent_class = CAMEL_FOLDER_CLASS(camel_vee_folder_get_type());

#if 0
	folder_class->refresh_info = unmatched_refresh_info;
	folder_class->sync = unmatched_sync;

	folder_class->get_message = unmatched_get_message;
	folder_class->append_message = unmatched_append_message;
	folder_class->transfer_messages_to = unmatched_transfer_messages_to;

	folder_class->search_by_expression = unmatched_search_by_expression;
	folder_class->search_by_uids = unmatched_search_by_uids;

	folder_class->set_message_flags = unmatched_set_message_flags;
	folder_class->set_message_user_flag = unmatched_set_message_user_flag;

	folder_class->rename = unmatched_rename;
#endif
	klass->add_folder = unmatched_add_folder;
	klass->remove_folder = unmatched_remove_folder;
	klass->rebuild_folder = unmatched_rebuild_folder;
	klass->change_folder = unmatched_change_folder;

	camel_object_class_add_event(klass, "folder_added", NULL);
	camel_object_class_add_event(klass, "folder_removed", NULL);
}

static void
camel_unmatched_folder_init (CamelUnmatchedFolder *obj)
{
	struct _CamelUnmatchedFolderPrivate *p;
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

#ifdef ENABLE_THREADS
	p->summary_lock = g_mutex_new();
	p->subfolder_lock = g_mutex_new();
	p->changed_lock = g_mutex_new();
#endif

}

static void
camel_unmatched_folder_finalise (CamelObject *obj)
{
	CamelUnmatchedFolder *vf = (CamelUnmatchedFolder *)obj;
	struct _CamelUnmatchedFolderPrivate *p = _PRIVATE(vf);
	GList *node;

	/* FIXME: check leaks */
	while (p->folders) {
		CamelFolder *f = p->folders->data;

		camel_unmatched_folder_remove_folder(vf, f);
	}

	g_free(vf->expression);
	g_free(vf->vname);
	
	g_list_free(p->folders);
	g_list_free(p->folders_changed);

	camel_folder_change_info_free(vf->changes);
	camel_object_unref((CamelObject *)vf->search);

#ifdef ENABLE_THREADS
	g_mutex_free(p->summary_lock);
	g_mutex_free(p->subfolder_lock);
	g_mutex_free(p->changed_lock);
#endif
	g_free(p);
}

/**
 * camel_unmatched_folder_new:
 * @parent_store: the parent CamelUnmatchedStore
 * @name: the vfolder name
 * @ex: a CamelException
 *
 * Create a new CamelUnmatchedFolder object.
 *
 * Return value: A new CamelUnmatchedFolder widget.
 **/
CamelFolder *
camel_unmatched_folder_new(CamelStore *parent_store, const char *name, guint32 flags)
{
	CamelUnmatchedFolder *vf;

	vf = (CamelUnmatchedFolder *)camel_object_new(camel_unmatched_folder_get_type());
	camel_vee_folder(vf, parent_store, name, flags);

	d(printf("returning folder %s %p, count = %d\n", name, vf, camel_folder_get_message_count((CamelFolder *)vf)));

	return (CamelFolder *)vf;
}

static int
unmatched_add_folder(CamelUnmatchedFolder *vf, CamelFolder *sub)
{
	int ret;

	g_return_val_if_fail(CAMEL_IS_VEE_FOLDER(sub), -1);

	ret = parent_class->add_folder(vf, sub);
	if (ret == 0) {
		camel_object_hook_event((CamelObject *)sub, "folder_added", (CamelObjectEventHookFunc)folder_added, vf);
		camel_object_hook_event((CamelObject *)sub, "folder_removed", (CamelObjectEventHookFunc)folder_removed, vf);
	}

	return ret;
}

static void
unmatched_remove_folder(CamelUnmatchedFolder *vf, CamelFolder *sub)
{
	g_return_val_if_fail(CAMEL_IS_VEE_FOLDER(sub), -1);

	camel_object_unhook_event((CamelObject *)sub, "folder_added", (CamelObjectEventHookFunc)folder_added, vf);
	camel_object_unhook_event((CamelObject *)sub, "folder_removed", (CamelObjectEventHookFunc)folder_removed, vf);

	return parent_class->remove_folder(vf, sub);
}

#if 0
/* should this be specialised for unmatched?? */
static GPtrArray *
unmatched_search_by_expression(CamelFolder *folder, const char *expression, CamelException *ex)
{
	GList *node;
	GPtrArray *matches, *result = g_ptr_array_new ();
	char *expr;
	CamelUnmatchedFolder *vf = (CamelUnmatchedFolder *)folder;
	struct _CamelUnmatchedFolderPrivate *p = _PRIVATE(vf);
	
	CAMEL_UNMATCHED_FOLDER_LOCK(vf, subfolder_lock);

	if (vf->expression) {
		expr = alloca(strlen(vf->expression)+strlen(expression)+16);
		sprintf(expr, "(and %s %s)", vf->expression, expression);
	} else {
		expr = (char *)expression;
	}
	
	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;
		int i;
		char hash[8];

		/* FIXME: only add uid's that we have ourselves? */
		
		camel_unmatched_folder_hash_folder(f, hash);
		matches = camel_folder_search_by_expression(f, expression, ex);
		for (i = 0; i < matches->len; i++) {
			char *uid = matches->pdata[i], *vuid;
			
			vuid = g_malloc(strlen(uid)+9);
			memcpy(vuid, hash, 8);
			strcpy(vuid+8, uid);
			g_ptr_array_add(result, vuid);
		}
		camel_folder_search_free(f, matches);
		node = g_list_next(node);
	}

	CAMEL_UNMATCHED_FOLDER_UNLOCK(vf, subfolder_lock);

	return result;
}

static GPtrArray *
unmatched_search_by_uids(CamelFolder *folder, const char *expression, GPtrArray *uids, CamelException *ex)
{
	GList *node;
	GPtrArray *matches, *result = g_ptr_array_new ();
	GPtrArray *folder_uids = g_ptr_array_new();
	char *expr;
	CamelUnmatchedFolder *vf = (CamelUnmatchedFolder *)folder;
	struct _CamelUnmatchedFolderPrivate *p = _PRIVATE(vf);

	CAMEL_UNMATCHED_FOLDER_LOCK(vf, subfolder_lock);

	/* FIXME: unmatched should override search_by_uids then? */
	/* if vf->expression is null, assume the vfolder uses another mechanism to track */
	if (vf->expression) {
		expr = alloca(strlen(vf->expression)+strlen(expression)+16);
		sprintf(expr, "(and %s %s)", vf->expression, expression);
	} else {
		expr = (char *)expression;
	}

	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;
		int i;
		char hash[8];
		
		camel_unmatched_folder_hash_folder(f, hash);

		/* FIXME: lookup the uid's in us, to see if they're there first */

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
		node = g_list_next(node);
	}

	CAMEL_UNMATCHED_FOLDER_UNLOCK(vf, subfolder_lock);

	g_ptr_array_free(folder_uids, 0);

	return result;
}
#endif

/* ********************************************************************** *
   utility functions */

/* must be called with summary_lock held */
static CamelUnmatchedMessageInfo *
unmatched_folder_add_info(CamelUnmatchedFolder *vf, CamelFolder *f, CamelMessageInfo *info, const char hash[8])
{
	CamelUnmatchedMessageInfo *mi;
	char *vuid;
	const char *uid;
	CamelFolder *folder = (CamelFolder *)vf;
	CamelMessageInfo *dinfo;

	uid = camel_message_info_uid(info);
	vuid = alloca(strlen(uid)+9);
	memcpy(vuid, hash, 8);
	strcpy(vuid+8, uid);
	dinfo = camel_folder_summary_uid(folder->summary, vuid);
	if (dinfo) {
		d(printf("w:clash, we already have '%s' in summary\n", vuid));
		camel_folder_summary_info_free(folder->summary, dinfo);
		return NULL;
	}

	d(printf("adding vuid %s to %s\n", vuid, vf->vname));

	mi = (CamelUnmatchedMessageInfo *)camel_folder_summary_info_new(folder->summary);
	camel_message_info_dup_to(info, (CamelMessageInfo *)mi);
#ifdef DOEPOOLV
	mi->info.strings = e_poolv_set(mi->info.strings, CAMEL_MESSAGE_INFO_UID, vuid, FALSE);
#elif defined (DOESTRV)
	mi->info.strings = e_strv_set_ref(mi->info.strings, CAMEL_MESSAGE_INFO_UID, vuid);
	mi->info.strings = e_strv_pack(mi->info.strings);
#else	
	g_free(mi->info.uid);
	mi->info.uid = g_strdup(vuid);
#endif
	mi->folder = f;
	camel_folder_summary_add(folder->summary, (CamelMessageInfo *)mi);

	return mi;
}

/* damn hashtable snot */
struct _update_data {
	CamelFolder *source;
	CamelUnmatchedFolder *vf;
	char hash[8];
};

static void
folder_added_uid(char *uidin, void *value, struct _update_data *u)
{
	folder_changed_add_uid(u->source, uidin, u->hash, u->vf);
}

#if 0
rebuild_rec(CamelUnmatchedFolder *uf, CamelFolder *sub, CamelVeeFolder *parent)
{
	char hash[8];
	char *uid;
	GByteArray *uidba;
	GPtrArray *uids;

	if (CAMEL_IS_VEE_FOLDER(sub)) {
		CamelVeeFolder *vf = sub;

		CAMEL_VEE_FOLDER_LOCK(sub, subfolder_lock);
		node = vf->priv->folders;
		while (node) {
			rebuild_rec(uf, (CamelFolder *)node->data);
			node = node->next;
		}
		CAMEL_VEE_FOLDER_UNLOCK(sub, subfolder_lock);
		return;
	}

	/* simple folder, iterate through all uid's on it */
	camel_vee_folder_hash_folder(sub, hash);
	uidba = g_byte_array_new();
	g_byte_array_append(uidba, hash, 8);
	uids = camel_folder_get_uids(sub);
	for (i=0;i<uids->len;i++) {
		uid = uids->pdata[i];
		g_byte_array_set_size(uidba, 8);
		g_byte_array_append(uidba, uid, strlen(uid)+1);
	}
}
#endif

/* build query contents for a single folder */
static void
unmatched_rebuild_folder(CamelUnmatchedFolder *vf, CamelFolder *source)
{
	GPtrArray *match, *search = NULL;
	GHashTable *matchhash;
	CamelFolder *f = source;
	CamelFolder *folder = (CamelFolder *)vf;
	int i, n, count;
	struct _update_data u;
	CamelFolderChangeInfo *vf_changes = NULL;

	/* if we have no expression, or its been cleared, then act as if no matches */
	if (vf->expression == NULL
	    || (search = match = camel_folder_search_by_expression(f, vf->expression, NULL)) == NULL)
		match = g_ptr_array_new();

	u.source = source;
	u.vf = vf;
	camel_unmatched_folder_hash_folder(source, u.hash);

	CAMEL_UNMATCHED_FOLDER_LOCK(vf, summary_lock);

	matchhash = g_hash_table_new(g_str_hash, g_str_equal);
	for (i=0;i<match->len;i++)
		g_hash_table_insert(matchhash, match->pdata[i], (void *)1);

	/* scan, looking for "old" uid's to be removed */
	count = camel_folder_summary_count(folder->summary);
	for (i=0;i<count;i++) {
		CamelUnmatchedMessageInfo *mi = (CamelUnmatchedMessageInfo *)camel_folder_summary_index(folder->summary, i);

		if (mi) {
			if (mi->folder == source) {
				char *uid = (char *)camel_message_info_uid(mi), *oldkey;

				if (g_hash_table_lookup(matchhash, uid+8) == NULL) {
					camel_folder_summary_remove_index(folder->summary, i);
					camel_folder_change_info_remove_uid(vf->changes, camel_message_info_uid(mi));
					i--;
				} else {
					g_hash_table_remove(matchhash, uid+8);
				}
			}
			camel_folder_summary_info_free(folder->summary, (CamelMessageInfo *)mi);
		}
	}

	/* now matchhash contains any new uid's, add them, etc */
	g_hash_table_foreach(matchhash, (GHFunc)folder_added_uid, &u);

	/* copy any changes so we can raise them outside the lock */
	if (camel_folder_change_info_changed(vf->changes)) {
		vf_changes = vf->changes;
		vf->changes = camel_folder_change_info_new();
	}

	CAMEL_UNMATCHED_FOLDER_UNLOCK(vf, summary_lock);

	g_hash_table_destroy(matchhash);

	if (search)
		camel_folder_search_free(f, search);
	else
		g_ptr_array_free(match, TRUE);

	if (vf_changes) {
		camel_object_trigger_event((CamelObject *)vf, "folder_changed", vf_changes);
		camel_folder_change_info_free(vf_changes);
	}
}

/*

  (match-folder "folder1" "folder2")

 */


/* Hold all these with summary lock held */
static void
folder_changed_add_uid(CamelFolder *sub, const char *uid, const char hash[8], CamelUnmatchedFolder *vf)
{
	CamelUnmatchedMessageInfo *mi;
	CamelMessageInfo *info;

	info = camel_folder_get_message_info(sub, uid);
	if (info) {
		mi = unmatched_folder_add_info(vf, sub, info, hash);
		if (mi)
			camel_folder_change_info_add_uid(vf->changes, camel_message_info_uid(mi);
		camel_folder_free_message_info(sub, info);
	}
}

static void
folder_changed_remove_uid(CamelFolder *sub, const char *uid, const char hash[8], CamelUnmatchedFolder *vf)
{
	CamelFolder *folder = (CamelFolder *)vf;
	char *vuid, *oldkey;
	int n;
	CamelUnmatchedMessageInfo *vinfo;

	vuid = alloca(strlen(uid)+9);
	memcpy(vuid, hash, 8);
	strcpy(vuid+8, uid);

	vinfo = (CamelUnmatchedMessageInfo *)camel_folder_summary_uid(folder->summary, vuid);
	if (vinfo) {
		camel_folder_change_info_remove_uid(vf->changes, vuid);
		camel_folder_summary_remove(folder->summary, (CamelMessageInfo *)vinfo);
		camel_folder_summary_info_free(folder->summary, (CamelMessageInfo *)vinfo);
	}
}

static void
folder_changed_change_uid(CamelFolder *sub, const char *uid, const char hash[8], CamelUnmatchedFolder *vf)
{
	char *vuid;
	CamelUnmatchedMessageInfo *vinfo;
	CamelMessageInfo *info;
	CamelFolder *folder = (CamelFolder *)vf;

	vuid = alloca(strlen(uid)+9);
	memcpy(vuid, hash, 8);
	strcpy(vuid+8, uid);

	vinfo = (CamelUnmatchedMessageInfo *)camel_folder_summary_uid(folder->summary, vuid);
	if (vinfo) {
		info = camel_folder_get_message_info(sub, uid);
		if (info) {
			if (vinfo) {
				int changed = FALSE;

				if (vinfo->info.flags != info->flags){
					vinfo->info.flags = info->flags;
					changed = TRUE;
				}
			
				changed |= camel_flag_list_copy(&vinfo->info.user_flags, &info->user_flags);
				changed |= camel_tag_list_copy(&vinfo->info.user_tags, &info->user_tags);
				if (changed)
					camel_folder_change_info_change_uid(vf->changes, vuid);

				camel_folder_summary_info_free(folder->summary, (CamelMessageInfo *)vinfo);
			}
			camel_folder_free_message_info(sub, info);
		} else {
			if (vinfo) {
				folder_changed_remove_uid(sub, uid, hash, vf);
				camel_folder_summary_info_free(folder->summary, (CamelMessageInfo *)vinfo);
			}
		}
	}
}

static void
unmatched_change_folder(CamelUnmatchedfolder *vf, CamelFolder *sub, CamelFolderChangeInfo *changes)
{
	CamelFolder *folder = (CamelFolder *)vf;
	char hash[8];
	const char *uid;
	char *key;
	unsigned int count;
	CamelUnmatchedMessageInfo *vinfo;
	int i, vuidlen = 0;
	CamelFolderChangeInfo *vf_changes = NULL, *unmatched_changes = NULL;
	GPtrArray *matches_added = NULL, /* newly added, that match */
		*matches_changed = NULL, /* newly changed, that now match */
		*newchanged = NULL,
		*changed;
	GPtrArray *always_changed = NULL;
	GHashTable *matches_hash;
	GByteArray *uidba;

	/* Check the folder hasn't beem removed while we weren't watching */
	CAMEL_UNMATCHED_FOLDER_LOCK(vf, subfolder_lock);
	if (g_list_find(_PRIVATE(vf)->folders, sub) == NULL) {
		CAMEL_UNMATCHED_FOLDER_UNLOCK(vf, subfolder_lock);
		return;
	}

	uidba = g_byte_array_new();
	camel_vee_folder_hash_folder(sub, hash);

	CAMEL_VEE_FOLDER_LOCK(vf, summary_lock);

	dd(printf("Vfolder '%s' subfolder changed '%s'\n", folder->full_name, sub->full_name));
	dd(printf(" changed %d added %d removed %d\n", changes->uid_changed->len, changes->uid_added->len, changes->uid_removed->len));

	/* Add uid's if they're still there, and they got removed from the vfolder */
	for (i=0;i<changes->uid_removed->len;i++) {
		uid = changes->uid_removed->pdata[i];
		if (g_hash_table_lookup_extended(uf->uid_table, uid, (void **)&key, (void **)&count)) {
			count--;
			g_hash_table_insert(uf->uid_table, key, (void *)count);
			if (count == 0)
				folder_changed_add_uid(sub, uid, hash, vf);
		}
		dd(printf("  removing uid '%s'\n", uid));
		folder_changed_remove_uid(sub, uid, hash, vf);
	}

	/* Always remove newly matched */
	for (i=0;i<changes->uid_added->len;i++) {
		uid = changes->uid_added->pdata[i];
		if (g_hash_table_lookup_extended(uf->uid_table, uid, (void **)&key, (void **)&count)) {
			folder_changed_remove_uid(sub, uid, hash, vf);
		} else {
			key = g_strdup(uid);
			count = 0;
		}
		g_hash_table_insert(uf->uid_table, key, (void *)(count+1));
	}

	/* changed are just changed */
	for (i=0;i<changes->uid_changed->len;i++) {
		uid = changes->uid_changed->pdata[i];
		folder_changed_change_uid(sub, uid, hash, vf);
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

	CAMEL_UNMATCHED_FOLDER_UNLOCK(vf, subfolder_lock);

	/* cleanup the rest */
	if (newchanged)
		g_ptr_array_free(newchanged, TRUE);

	if (vf_changes) {
		/* If not auto-updating, keep track of changed folders for later re-sync */
		if ((vf->flags & CAMEL_STORE_UNMATCHED_FOLDER_AUTO) == 0) {
			CAMEL_UNMATCHED_FOLDER_LOCK(vf, changed_lock);
			if (g_list_find(vf->priv->folders_changed, sub) != NULL)
				vf->priv->folders_changed = g_list_prepend(vf->priv->folders_changed, sub);
			CAMEL_UNMATCHED_FOLDER_UNLOCK(vf, changed_lock);
		}

		camel_object_trigger_event((CamelObject *)vf, "folder_changed", vf_changes);
		camel_folder_change_info_free(vf_changes);
	}
}


#ifdef ENABLE_THREADS

/* this is just a mt-marshaller used in the mt-case */
struct _folder_changed_msg {
	CamelSessionThreadMsg msg;
	CamelFolderChangeInfo *changes;
	CamelFolder *sub;
	CamelUnmatchedFolder *vf;
};

static void
folder_changed_change(CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _folder_changed_msg *m = (struct _folder_changed_msg *)msg;

	camel_unmatched_folder_change_folder(m->vf, m->sub, m->changes);
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
#endif

static void
folder_changed(CamelFolder *sub, CamelFolderChangeInfo *changes, CamelUnmatchedFolder *vf)
{
	struct _folder_changed_msg *m;
	CamelSession *session = ((CamelService *)((CamelFolder *)vf)->parent_store)->session;

#ifdef ENABLE_THREADS
	m = camel_session_thread_msg_new(session, &folder_changed_ops, sizeof(*m));
	m->changes = camel_folder_change_info_new();
	camel_folder_change_info_cat(m->changes, changes);
	m->sub = sub;
	camel_object_ref((CamelObject *)sub);
	m->vf = vf;
	camel_object_ref((CamelObject *)vf);
	camel_session_thread_queue(session, &m->msg, 0);
#else
	camel_unmatched_folder_change_folder(vf, sub, changes);
#endif
}

/* track flag changes in the summary, we just promote it to a folder_changed event */
static void
message_changed(CamelFolder *f, const char *uid, CamelUnmatchedFolder *vf)
{
	CamelFolderChangeInfo *changes;

	changes = camel_folder_change_info_new();
	camel_folder_change_info_change_uid(changes, uid);
	folder_changed(f, changes, vf);
	camel_folder_change_info_free(changes);
}

/* track vanishing folders */
static void
subfolder_deleted(CamelFolder *f, void *event_data, CamelUnmatchedFolder *vf)
{
	camel_unmatched_folder_remove_folder(vf, f);
}
