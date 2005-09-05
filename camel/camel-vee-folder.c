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
#include "camel-vee-summary.h"

#include "camel-session.h"
#include "camel-vee-store.h"	/* for open flags */
#include "camel-private.h"
#include "camel-debug.h"
#include "camel-i18n.h"

#include "libedataserver/md5-utils.h"

#if defined (DOEPOOLV) || defined (DOESTRV)
#include "libedataserver/e-memory.h"
#endif

#define d(x) 
#define dd(x) (camel_debug("vfolder")?(x):0)

#define _PRIVATE(o) (((CamelVeeFolder *)(o))->priv)

#if 0
static void vee_refresh_info(CamelFolder *folder, CamelException *ex);

static void vee_sync (CamelFolder *folder, gboolean expunge, CamelException *ex);

static void vee_freeze(CamelFolder *folder);
static void vee_thaw(CamelFolder *folder);

static CamelMimeMessage *vee_get_message (CamelFolder *folder, const gchar *uid, CamelException *ex);
static void vee_append_message(CamelFolder *folder, CamelMimeMessage *message, const CamelMessageInfo *info, char **appended_uid, CamelException *ex);
static void vee_transfer_messages_to(CamelFolder *source, GPtrArray *uids, CamelFolder *dest, GPtrArray **transferred_uids, gboolean delete_originals, CamelException *ex);

static void vee_rename(CamelFolder *folder, const char *new);
#endif

static void camel_vee_folder_class_init (CamelVeeFolderClass *klass);
static void camel_vee_folder_init       (CamelVeeFolder *obj);
static void camel_vee_folder_finalise   (CamelObject *obj);

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
camel_vee_folder_construct (CamelVeeFolder *vf, CamelStore *parent_store, const char *vid, const char *full, const char *name, guint32 flags)
{
	CamelFolder *folder = (CamelFolder *)vf;

	vf->flags = flags;
	camel_folder_construct(folder, parent_store, full, name);

	folder->summary = camel_vee_summary_new(folder, vid);

	if (CAMEL_IS_VEE_STORE(parent_store))
		vf->parent_vee_store = (CamelVeeStore *)parent_store;
}

/**
 * camel_vee_folder_new:
 * @parent_store: the parent CamelVeeStore
 * @vid: unique id of this vfolder.
 * @full: the full path to the vfolder.
 * @ex: a CamelException
 *
 * Create a new CamelVeeFolder object.
 *
 * Return value: A new CamelVeeFolder widget.
 **/
CamelFolder *
camel_vee_folder_new(CamelStore *parent_store, const char *vid, const char *full, guint32 flags)
{
	CamelVeeFolder *vf;
	char *tmp;

	return NULL;
	
	if (CAMEL_IS_VEE_STORE(parent_store) && strcmp(full, CAMEL_UNMATCHED_NAME) == 0) {
		vf = ((CamelVeeStore *)parent_store)->folder_unmatched;
		camel_object_ref(vf);
	} else {
		const char *name = strrchr(full, '/');

		if (name == NULL)
			name = full;
		else
			name++;
		vf = (CamelVeeFolder *)camel_object_new(camel_vee_folder_get_type());
		camel_vee_folder_construct(vf, parent_store, vid, full, name, flags);
	}

	d(printf("returning folder %s %p, count = %d\n", name, vf, camel_folder_get_message_count((CamelFolder *)vf)));

	tmp = g_strdup_printf("%s/%s.cmeta", ((CamelService *)parent_store)->url->path, full);
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
	if (vf == NULL)
		return;

	camel_vee_summary_set_expression((CamelVeeSummary *)((CamelFolder *)vf)->summary, query);
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
	if (vf == NULL)
		return;

	if (vf == (CamelVeeFolder *)sub) {
		g_warning("Adding a virtual folder to itself as source, ignored");
		return;
	}

	camel_vee_summary_add_folder((CamelVeeSummary *)((CamelFolder *)vf)->summary, NULL, sub);
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
	if (vf == NULL)
		return;

	camel_vee_summary_remove_folder((CamelVeeSummary *)((CamelFolder *)vf)->summary, sub);
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
	printf("rebuild folder, why is this called?\n");

	//return ((CamelVeeFolderClass *)((CamelObject *)vf)->klass)->rebuild_folder(vf, sub, ex);
	return 0;
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
	camel_vee_summary_set_folders((CamelVeeSummary *)((CamelFolder *)vf)->summary, folders);
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
		camel_message_info_free((CamelMessageInfo *)vfinfo);
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

static CamelMimeMessage *
vee_get_message(CamelFolder *folder, const char *uid, CamelException *ex)
{
	CamelVeeMessageInfo *mi;
	CamelMimeMessage *msg = NULL;

	mi = (CamelVeeMessageInfo *)camel_folder_summary_get(folder->summary, uid);
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

/* ********************************************************************** */

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
#if 0
	struct _CamelVeeFolderPrivate *p = _PRIVATE(folder);

	/* ?? */

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
	CAMEL_VEE_FOLDER_UNLOCK(folder, subfolder_lock);
#endif
	((CamelFolderClass *)camel_vee_folder_parent)->delete(folder);
}

/* ********************************************************************** *
   utility functions */

static void
vee_freeze (CamelFolder *folder)
{
	/* Hmm, do we freeze subfolders?  run off summary? */
#if 0
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
#endif
	/* call parent implementation */
	CAMEL_FOLDER_CLASS (camel_vee_folder_parent)->freeze(folder);
}

static void
vee_thaw(CamelFolder *folder)
{
#if 0
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
#endif
	/* call parent implementation */
	CAMEL_FOLDER_CLASS (camel_vee_folder_parent)->thaw(folder);
}

/* vfolder base implementaitons */
static void
vee_add_folder(CamelVeeFolder *vf, CamelFolder *sub)
{
	//vee_rebuild_folder(vf, sub, NULL);
}

static void
vee_remove_folder(CamelVeeFolder *vf, CamelFolder *sub)
{
	//vee_folder_remove_folder(vf, sub);
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

	folder_class->get_message = vee_get_message;
	folder_class->append_message = vee_append_message;
	folder_class->transfer_messages_to = vee_transfer_messages_to;

	folder_class->rename = vee_rename;
	folder_class->delete = vee_delete;

	folder_class->freeze = vee_freeze;
	folder_class->thaw = vee_thaw;

	klass->set_expression = vee_set_expression;
	klass->add_folder = vee_add_folder;
	klass->remove_folder = vee_remove_folder;
//	klass->rebuild_folder = vee_rebuild_folder;
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

	g_mutex_free(p->summary_lock);
	g_mutex_free(p->subfolder_lock);
	g_mutex_free(p->changed_lock);
	
	g_free(p);
}
