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
#include "camel-vee-folder.h"
#include "camel-store.h"
#include "camel-folder-summary.h"
#include "camel-mime-message.h"
#include "camel-folder-search.h"

#include "camel-session.h"
#include "camel-vee-store.h"	/* for open flags */
#include "camel-private.h"

#include "e-util/md5-utils.h"

#if defined (DOEPOOLV) || defined (DOESTRV)
#include "e-util/e-memory.h"
#endif

#define d(x)
extern int camel_verbose_debug;
#define dd(x) (camel_verbose_debug?(x):0)

#define _PRIVATE(o) (((CamelVeeFolder *)(o))->priv)

static void vee_refresh_info(CamelFolder *folder, CamelException *ex);

static void vee_sync (CamelFolder *folder, guint32 flags, CamelException *ex);

static CamelMimeMessage *vee_get_message (CamelFolder *folder, const gchar *uid, CamelException *ex);
static void vee_append_message(CamelFolder *folder, CamelMimeMessage *message, const CamelMessageInfo *info, char **appended_uid, CamelException *ex);
static void vee_transfer_messages_to(CamelFolder *source, GPtrArray *uids, CamelFolder *dest, GPtrArray **transferred_uids, gboolean delete_originals, CamelException *ex);

static GPtrArray *vee_search_by_expression(CamelFolder *folder, const char *expression, CamelException *ex);
static GPtrArray *vee_search_by_uids(CamelFolder *folder, const char *expression, GPtrArray *uids, CamelException *ex);

static void vee_set_message_flags (CamelFolder *folder, const char *uid, guint32 flags, guint32 set);
static void vee_set_message_user_flag (CamelFolder *folder, const char *uid, const char *name, gboolean value);
static void vee_rename(CamelFolder *folder, const char *new);

static void camel_vee_folder_class_init (CamelVeeFolderClass *klass);
static void camel_vee_folder_init       (CamelVeeFolder *obj);
static void camel_vee_folder_finalise   (CamelObject *obj);

static void message_changed(CamelFolder *f, const char *uid, CamelVeeFolder *vf);
static void folder_changed(CamelFolder *sub, CamelFolderChangeInfo *changes, CamelVeeFolder *vf);
static void subfolder_deleted(CamelFolder *f, void *event_data, CamelVeeFolder *vf);

static void folder_changed_remove_uid(CamelFolder *sub, const char *uid, const char hash[8], int keep, CamelVeeFolder *vf);

static void vee_add_folder(CamelVeeFolder *vf, CamelFolder *sub);
static void vee_remove_folder(CamelVeeFolder *vf, CamelFolder *sub);
static void vee_rebuild_folder(CamelVeeFolder *vf, CamelFolder *source);

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

static void
camel_vee_folder_class_init (CamelVeeFolderClass *klass)
{
	CamelFolderClass *folder_class = (CamelFolderClass *) klass;

	camel_vee_folder_parent = CAMEL_FOLDER_CLASS(camel_type_get_global_classfuncs (camel_folder_get_type ()));

	folder_class->refresh_info = vee_refresh_info;
	folder_class->sync = vee_sync;

	folder_class->get_message = vee_get_message;
	folder_class->append_message = vee_append_message;
	folder_class->transfer_messages_to = vee_transfer_messages_to;

	folder_class->search_by_expression = vee_search_by_expression;
	folder_class->search_by_uids = vee_search_by_uids;

	folder_class->set_message_flags = vee_set_message_flags;
	folder_class->set_message_user_flag = vee_set_message_user_flag;

	folder_class->rename = vee_rename;

	klass->add_folder = vee_add_folder;
	klass->remove_folder = vee_remove_folder;
	klass->rebuild_folder = vee_rebuild_folder;
	klass->change_folder = vee_change_folder;

	camel_object_class_add_event(klass, "folder_added", NULL);
	camel_object_class_add_event(klass, "folder_removed", NULL);
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

#ifdef ENABLE_THREADS
	p->summary_lock = g_mutex_new();
	p->subfolder_lock = g_mutex_new();
	p->changed_lock = g_mutex_new();
#endif

}

static void
camel_vee_folder_finalise (CamelObject *obj)
{
	CamelVeeFolder *vf = (CamelVeeFolder *)obj;
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	GList *node;

	/* FIXME: check leaks */
	while (p->folders) {
		CamelFolder *f = p->folders->data;

		camel_vee_folder_remove_folder(vf, f);
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

void
camel_vee_folder_construct(CamelVeeFolder *vf, CamelStore *parent_store, const char *name, guint32 flags)
{
	CamelFolder *folder = (CamelFolder *)vf;
	char *tmp;

	vf->flags = flags;
	vf->vname = g_strdup(name);
	tmp = strrchr(vf->vname, '/');
	if (tmp)
		tmp++;
	else
		tmp = vf->vname;
	camel_folder_construct(folder, parent_store, vf->vname, tmp);

	/* should CamelVeeMessageInfo be subclassable ..? */
	folder->summary = camel_folder_summary_new();
	folder->summary->message_info_size = sizeof(CamelVeeMessageInfo);
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

	vf = (CamelVeeFolder *)camel_object_new(camel_vee_folder_get_type());
	vee_folder_construct(vf, parent_store, name, flags);

	d(printf("returning folder %s %p, count = %d\n", name, vf, camel_folder_get_message_count((CamelFolder *)vf)));

	return (CamelFolder *)vf;
}

void
camel_vee_folder_set_expression(CamelVeeFolder *vf, const char *query)
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

		camel_vee_folder_rebuild_folder(vf, f);

		node = node->next;
	}

	CAMEL_VEE_FOLDER_LOCK(vf, changed_lock);
	g_list_free(p->folders_changed);
	p->folders_changed = NULL;
	CAMEL_VEE_FOLDER_UNLOCK(vf, changed_lock);

	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);
}

static int
vee_add_folder(CamelVeeFolder *vf, CamelFolder *sub)
{
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	int ret = -1;

	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);

	/* we only keep track of unique folders */
	if (g_list_find(p->folders, sub) == NULL) {
		camel_object_ref((CamelObject *)sub);
		p->folders = g_list_append(p->folders, sub);

		d(printf("camel_vee_folder_add_folde(%p, %p)\n", vf, sub));
		
		camel_object_hook_event((CamelObject *)sub, "folder_changed", (CamelObjectEventHookFunc)folder_changed, vf);
		camel_object_hook_event((CamelObject *)sub, "message_changed", (CamelObjectEventHookFunc)message_changed, vf);
		camel_object_hook_event((CamelObject *)sub, "deleted", (CamelObjectEventHookFunc)subfolder_deleted, vf);

		camel_vee_folder_rebuild_folder(vf, sub);
		ret = 0;
	} else
		g_warning("Trying to re-add folder '%s' to vfolder '%s'", sub->full_name, vf->parent.full_name);
		

	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);

	return ret;
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
	int ret;

	g_return_if_fail(vf != (CamelVeeFolder *)sub);
	g_return_if_fail(CAMEL_IS_VEE_FOLDER(vf));
	g_return_if_fail(CAMEL_IS_FOLDER(sub));

	CAMEL_FOLDER_LOCK(vf, lock);

	ret = ((CamelVeeFolderClass *)((CamelObject *)vf)->klass)->add_folder(vf, sub);

	CAMEL_FOLDER_UNLOCK(vf, lock);

	if (ret == 0)
		camel_object_trigger_event(vf, "folder_added", sub);
}

static int
vee_remove_folder(CamelVeeFolder *vf, CamelFolder *sub)
{
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	int i, count, n;
	char *oldkey;
	CamelFolder *folder = (CamelFolder *)vf;
	char hash[8];
	/*struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);*/
	CamelFolderChangeInfo *vf_changes = NULL;
	int ret = -1;

	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);

	CAMEL_VEE_FOLDER_LOCK(vf, changed_lock);
	p->folders_changed = g_list_remove(p->folders_changed, sub);
	CAMEL_VEE_FOLDER_UNLOCK(vf, changed_lock);

	if (g_list_find(p->folders, sub) != NULL) {
		camel_object_unhook_event((CamelObject *)sub, "folder_changed", (CamelObjectEventHookFunc) folder_changed, vf);
		camel_object_unhook_event((CamelObject *)sub, "message_changed", (CamelObjectEventHookFunc) message_changed, vf);
		camel_object_unhook_event((CamelObject *)sub, "deleted", (CamelObjectEventHookFunc) subfolder_deleted, vf);

		p->folders = g_list_remove(p->folders, sub);
		
		vee_folder_remove_folder(vf, sub);

		CAMEL_VEE_FOLDER_LOCK(vf, summary_lock);

		count = camel_folder_summary_count(folder->summary);
		for (i=0;i<count;i++) {
			CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)camel_folder_summary_index(folder->summary, i);
			if (mi) {
				if (mi->folder == source) {
					const char *uid = camel_message_info_uid(mi);
					
					camel_folder_change_info_remove_uid(vf->changes, uid);
					camel_folder_summary_remove_index(folder->summary, i);
					i--;
				}
				camel_folder_summary_info_free(folder->summary, (CamelMessageInfo *)mi);
			}
		}
		
		if (camel_folder_change_info_changed(vf->changes)) {
			vf_changes = vf->changes;
			vf->changes = camel_folder_change_info_new();
		}
		
		CAMEL_VEE_FOLDER_UNLOCK(vf, summary_lock);

		if (vf_changes) {
			camel_object_trigger_event((CamelObject *)vf, "folder_changed", vf_changes);
			camel_folder_change_info_free(vf_changes);
		}

		camel_object_unref((CamelObject *)sub);
		ret = 0;
	}

	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);

	return ret;
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
	int ret;

	g_return_if_fail(vf != (CamelVeeFolder *)sub);
	g_return_if_fail(CAMEL_IS_VEE_FOLDER(vf));
	g_return_if_fail(CAMEL_IS_FOLDER(sub));

	CAMEL_FOLDER_LOCK(vf, lock);

	ret = ((CamelVeeFolderClass *)((CamelObject *)vf)->klass)->remove_folder(vf, sub);
	
	CAMEL_FOLDER_UNLOCK(vf, lock);

	if (ret == 0)
		camel_object_trigger_event(vf, "folder_removed", sub);
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
	int changed;

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
			camel_object_unref(folder);

			/* if this was a changed folder, re-update it while we're here */
			CAMEL_VEE_FOLDER_LOCK(vf, changed_lock);
			changed = g_list_find(p->folders_changed, folder) != NULL;
			if (changed)
				p->folders_changed = g_list_remove(p->folders_changed, folder);
			CAMEL_VEE_FOLDER_UNLOCK(vf, changed_lock);
			if (changed)
				camel_vee_folder_rebuild_folder(vf, folder);
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
	base64_encode_close(digest, 6, FALSE, buffer, &state, &save);

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
	/* locking?  yes?  no?  although the vfolderinfo is valid when obtained
	   the folder in it might not necessarily be so ...? */
	if (CAMEL_IS_VEE_FOLDER(vinfo->folder)) {
		CamelFolder *folder;
		const CamelVeeMessageInfo *vfinfo;

		vfinfo = (CamelVeeMessageInfo *)camel_folder_get_message_info(vinfo->folder, camel_message_info_uid(vinfo)+8);
		folder = camel_vee_folder_get_location((CamelVeeFolder *)vinfo->folder, vfinfo, realuid);
		camel_folder_free_message_info(vinfo->folder, (CamelMessageInfo *)vfinfo);
		return folder;
	} else {
		if (realuid)
			*realuid = g_strdup(camel_message_info_uid(vinfo)+8);

		return vinfo->folder;
	}
}

static void vee_refresh_info(CamelFolder *folder, CamelException *ex)
{
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	GList *node, *list;

	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);

	CAMEL_VEE_FOLDER_LOCK(vf, changed_lock);
	list = p->folders_changed;
	p->folders_changed = NULL;
	CAMEL_VEE_FOLDER_UNLOCK(vf, changed_lock);

	node = list;
	while (node) {
		CamelFolder *f = node->data;

		camel_vee_folder_rebuild_folder(vf, f);
		node = node->next;
	}

	g_list_free(list);

	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);
}

static void
vee_sync(CamelFolder *folder, guint32 flags, CamelException *ex)
{
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	GList *node;
	int expunge = (flags & CAMEL_STORE_SYNC_EXPUNGE) != 0;

	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);

	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;

		camel_folder_sync(f, flags, ex);
		if (camel_exception_is_set(ex))
			break;

		if (expunge)
			camel_vee_folder_rebuild_folder(vf, f);

		node = node->next;
	}

	if (expunge && node == NULL) {
		CAMEL_VEE_FOLDER_LOCK(vf, changed_lock);
		g_list_free(p->folders_changed);
		p->folders_changed = NULL;
		CAMEL_VEE_FOLDER_UNLOCK(vf, changed_lock);
	}

	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);
}

static CamelMimeMessage *
vee_get_message(CamelFolder *folder, const char *uid, CamelException *ex)
{
	CamelVeeMessageInfo *mi;
	CamelMimeMessage *msg = NULL;

	mi = (CamelVeeMessageInfo *)camel_folder_summary_uid(folder->summary, uid);
	if (mi) {
		msg =  camel_folder_get_message(mi->folder, camel_message_info_uid(mi)+8, ex);
		camel_folder_summary_info_free(folder->summary, (CamelMessageInfo *)mi);
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
	
	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);

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
		
		camel_vee_folder_hash_folder(f, hash);
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

	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);

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

	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);

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
		
		camel_vee_folder_hash_folder(f, hash);

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

	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);

	g_ptr_array_free(folder_uids, 0);

	return result;
}

static void
vee_set_message_flags(CamelFolder *folder, const char *uid, guint32 flags, guint32 set)
{
	CamelVeeMessageInfo *mi;

	mi = (CamelVeeMessageInfo *)camel_folder_summary_uid(folder->summary, uid);
	if (mi) {
		camel_folder_set_message_flags(mi->folder, camel_message_info_uid(mi) + 8, flags, set);
		camel_folder_summary_info_free(folder->summary, (CamelMessageInfo *)mi);
		((CamelFolderClass *)camel_vee_folder_parent)->set_message_flags(folder, uid, flags, set);
	}
}

static void
vee_set_message_user_flag(CamelFolder *folder, const char *uid, const char *name, gboolean value)
{
	CamelVeeMessageInfo *mi;

	mi = (CamelVeeMessageInfo *)camel_folder_summary_uid(folder->summary, uid);
	if (mi) {
		camel_folder_set_message_user_flag(mi->folder, camel_message_info_uid(mi) + 8, name, value);
		camel_folder_summary_info_free(folder->summary, (CamelMessageInfo *)mi);
		((CamelFolderClass *)camel_vee_folder_parent)->set_message_user_flag(folder, uid, name, value);
	}
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
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;

	g_free(vf->vname);
	vf->vname = g_strdup(new);

	((CamelFolderClass *)camel_vee_folder_parent)->rename(folder, new);
}

/* ********************************************************************** *
   utility functions */

/* must be called with summary_lock held */
static CamelVeeMessageInfo *
vee_folder_add_info(CamelVeeFolder *vf, CamelFolder *f, CamelMessageInfo *info, const char hash[8])
{
	CamelVeeMessageInfo *mi;
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

	mi = (CamelVeeMessageInfo *)camel_folder_summary_info_new(folder->summary);
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
	CamelVeeFolder *vf;
	char hash[8];
};

static void
folder_added_uid(char *uidin, void *value, struct _update_data *u)
{
	folder_changed_add_uid(u->source, uidin, u->hash, u->vf);
}

/* build query contents for a single folder */
static void
vee_rebuild_folder(CamelVeeFolder *vf, CamelFolder *source)
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
	camel_vee_folder_hash_folder(source, u.hash);

	CAMEL_VEE_FOLDER_LOCK(vf, summary_lock);

	matchhash = g_hash_table_new(g_str_hash, g_str_equal);
	for (i=0;i<match->len;i++)
		g_hash_table_insert(matchhash, match->pdata[i], (void *)1);

	/* scan, looking for "old" uid's to be removed */
	count = camel_folder_summary_count(folder->summary);
	for (i=0;i<count;i++) {
		CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)camel_folder_summary_index(folder->summary, i);

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

	CAMEL_VEE_FOLDER_UNLOCK(vf, summary_lock);

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
folder_changed_add_uid(CamelFolder *sub, const char *uid, const char hash[8], CamelVeeFolder *vf)
{
	CamelVeeMessageInfo *mi;
	CamelMessageInfo *info;

	info = camel_folder_get_message_info(sub, uid);
	if (info) {
		mi = vee_folder_add_info(vf, sub, info, hash);
		if (mi)
			camel_folder_change_info_add_uid(vf->changes, camel_message_info_uid(mi);
		camel_folder_free_message_info(sub, info);
	}
}

static void
folder_changed_remove_uid(CamelFolder *sub, const char *uid, const char hash[8], CamelVeeFolder *vf)
{
	CamelFolder *folder = (CamelFolder *)vf;
	char *vuid, *oldkey;
	int n;
	CamelVeeMessageInfo *vinfo;

	vuid = alloca(strlen(uid)+9);
	memcpy(vuid, hash, 8);
	strcpy(vuid+8, uid);

	vinfo = (CamelVeeMessageInfo *)camel_folder_summary_uid(folder->summary, vuid);
	if (vinfo) {
		camel_folder_change_info_remove_uid(vf->changes, vuid);
		camel_folder_summary_remove(folder->summary, (CamelMessageInfo *)vinfo);
		camel_folder_summary_info_free(folder->summary, (CamelMessageInfo *)vinfo);
	}
}

static void
folder_changed_change_uid(CamelFolder *sub, const char *uid, const char hash[8], CamelVeeFolder *vf)
{
	char *vuid;
	CamelVeeMessageInfo *vinfo;
	CamelMessageInfo *info;
	CamelFolder *folder = (CamelFolder *)vf;

	vuid = alloca(strlen(uid)+9);
	memcpy(vuid, hash, 8);
	strcpy(vuid+8, uid);

	vinfo = (CamelVeeMessageInfo *)camel_folder_summary_uid(folder->summary, vuid);
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
vee_change_folder(CamelVeefolder *vf, CamelFolder *sub, CamelFolderChangeInfo *changes)
{
	CamelFolder *folder = (CamelFolder *)vf;
	char hash[8];
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
	GByteArray *uidba;

	/* Check the folder hasn't beem removed while we weren't watching */
	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);
	if (g_list_find(_PRIVATE(vf)->folders, sub) == NULL) {
		CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);
		return;
	}

	uidba = g_byte_array_new();

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
				g_byte_array_set_size(uidba, 0);
				g_byte_array_append(uidba, hash, 8);
				g_byte_array_append(uidba, uid, strlen(uid)+1);
				vinfo = (CamelVeeMessageInfo *)camel_folder_summary_uid(folder->summary, uidba->data);
				if (vinfo == NULL) {
					g_ptr_array_add(newchanged, (char *)uid);
				} else {
					g_ptr_array_add(always_changed, (char *)uid);
					camel_folder_summary_info_free(folder->summary, (CamelMessageInfo *)vinfo);
				}
			}
			changed = newchanged;
		}

		if (changed->len)
			matches_changed = camel_folder_search_by_uids(sub, vf->expression, changed, NULL);
	}

	CAMEL_VEE_FOLDER_LOCK(vf, summary_lock);

	dd(printf("Vfolder '%s' subfolder changed '%s'\n", folder->full_name, sub->full_name));
	dd(printf(" changed %d added %d removed %d\n", changes->uid_changed->len, changes->uid_added->len, changes->uid_removed->len));

	/* Always remove removed uid's, in any case */
	for (i=0;i<changes->uid_removed->len;i++) {
		dd(printf("  removing uid '%s'\n", (char *)changes->uid_removed->pdata[i]));
		folder_changed_remove_uid(sub, changes->uid_removed->pdata[i], hash, vf);
	}

	/* Add any newly matched */
	if (matches_added) {
		for (i=0;i<matches_added->len;i++) {
			uid = changes->matches_added->pdata[i];
			folder_changed_add_uid(sub, uid, hash, vf);
			dd(printf("  adding uid '%s' [newly matched]", (char *)matches_added->pdata[i]));
		}
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
			g_byte_array_set_size(uidba, 0);
			g_byte_array_append(uidba, hash, 8);
			g_byte_array_append(uidba, uid, strlen(uid)+1);
			vinfo = (CamelVeeMessageInfo *)camel_folder_summary_uid(folder->summary, uidba->data);
			if (vinfo == NULL) {
				/* A uid we dont have, but now it matches, add it */
				if (g_hash_table_lookup(matches_hash, uid)) {
					dd(printf("  adding uid '%s' [newly matched]\n", uid));
					folder_changed_add_uid(sub, uid, hash, vf);
				}
			} else {
				if ((vf->flags & CAMEL_STORE_VEE_FOLDER_AUTO) == 0
				    || g_hash_table_lookup(matches_hash, uid)) {
					/* still match, or we're not auto-updating, change event, (if it changed) */
					dd(printf("  changing uid '%s' [still matches]\n", uid));
					folder_changed_change_uid(sub, uid, hash, vf);
				} else {
					/* No longer matches, remove it */
					dd(printf("  removing uid '%s' [did match]\n", uid));
					folder_changed_remove_uid(sub, uid, hash, vf);
				}
				camel_folder_summary_info_free(folder->summary, (CamelMessageInfo *)vinfo);
			}
		}
		g_hash_table_destroy(matches_hash);
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


#ifdef ENABLE_THREADS

/* this is just a mt-marshaller used in the mt-case */
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

	camel_vee_folder_change_folder(m->vf, m->sub, m->changes);
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
folder_changed(CamelFolder *sub, CamelFolderChangeInfo *changes, CamelVeeFolder *vf)
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
	camel_vee_folder_change_folder(vf, sub, changes);
#endif
}

/* track flag changes in the summary, we just promote it to a folder_changed event */
static void
message_changed(CamelFolder *f, const char *uid, CamelVeeFolder *vf)
{
	CamelFolderChangeInfo *changes;

	changes = camel_folder_change_info_new();
	camel_folder_change_info_change_uid(changes, uid);
	folder_changed(f, changes, vf);
	camel_folder_change_info_free(changes);
}

/* track vanishing folders */
static void
subfolder_deleted(CamelFolder *f, void *event_data, CamelVeeFolder *vf)
{
	camel_vee_folder_remove_folder(vf, f);
}
