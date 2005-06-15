/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-folder.c: Abstract class for an email folder */

/*
 * Author:
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright 1999-2003 Ximian, Inc. (www.ximian.com)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "camel-folder.h"
#include "camel-exception.h"
#include "camel-store.h"
#include "camel-mime-message.h"
#include "camel-debug.h"
#include "libedataserver/e-memory.h"
#include "camel-operation.h"
#include "camel-session.h"
#include "camel-filter-driver.h"
#include "camel-private.h"
#include "camel-vtrash-folder.h"
#include "camel-i18n.h"

#define d(x) 
#define w(x)

extern int camel_verbose_debug;

static CamelObjectClass *parent_class = NULL;

/* Returns the class for a CamelFolder */
#define CF_CLASS(so) ((CamelFolderClass *)((CamelObject *)(so))->klass)

static void camel_folder_finalize (CamelObject *object);

static void refresh_info (CamelFolder *folder, CamelException *ex);

static void folder_sync (CamelFolder *folder, gboolean expunge,
			 CamelException *ex);

static const char *get_name (CamelFolder *folder);
static const char *get_full_name (CamelFolder *folder);
static CamelStore *get_parent_store   (CamelFolder *folder);

static int folder_getv(CamelObject *object, CamelException *ex, CamelArgGetV *args);
static void folder_free(CamelObject *o, guint32 tag, void *val);

static void append_message (CamelFolder *folder, CamelMimeMessage *message,
			    const CamelMessageInfo *info, char **appended_uid,
			    CamelException *ex);

static CamelMimeMessage *get_message         (CamelFolder *folder, const gchar *uid, CamelException *ex);

static CamelMessageInfo *get_message_info    (CamelFolder *folder, const char *uid);

static CamelMessageIterator *search(CamelFolder *folder, const char *, const char *expression, CamelMessageIterator *, CamelException *ex);

static void            transfer_messages_to  (CamelFolder *source, GPtrArray *uids, CamelFolder *dest,
					      GPtrArray **transferred_uids, gboolean delete_originals, CamelException *ex);

static void            delete                (CamelFolder *folder);
static void            folder_rename         (CamelFolder *folder, const char *new);

static void            freeze                (CamelFolder *folder);
static void            thaw                  (CamelFolder *folder);
static gboolean        is_frozen             (CamelFolder *folder);

static gboolean        folder_changed        (CamelObject *object,
					      gpointer event_data);

static void
camel_folder_class_init (CamelFolderClass *camel_folder_class)
{
	CamelObjectClass *camel_object_class = CAMEL_OBJECT_CLASS (camel_folder_class);

	parent_class = camel_type_get_global_classfuncs (camel_object_get_type ());

	/* virtual method definition */
	camel_folder_class->sync = folder_sync;
	camel_folder_class->refresh_info = refresh_info;
	camel_folder_class->get_name = get_name;
	camel_folder_class->get_full_name = get_full_name;
	camel_folder_class->get_parent_store = get_parent_store;
	camel_folder_class->append_message = append_message;
	camel_folder_class->get_message = get_message;
	camel_folder_class->search = search;
	camel_folder_class->get_message_info = get_message_info;
	camel_folder_class->transfer_messages_to = transfer_messages_to;
	camel_folder_class->delete = delete;
	camel_folder_class->rename = folder_rename;
	camel_folder_class->freeze = freeze;
	camel_folder_class->thaw = thaw;
	camel_folder_class->is_frozen = is_frozen;

	/* virtual method overload */
	camel_object_class->getv = folder_getv;
	camel_object_class->free = folder_free;

	/* events */
	camel_object_class_add_event(camel_object_class, "folder_changed", folder_changed);
	camel_object_class_add_event(camel_object_class, "deleted", NULL);
	camel_object_class_add_event(camel_object_class, "renamed", NULL);
}

static void
camel_folder_init (gpointer object, gpointer klass)
{
	CamelFolder *folder = object;

	folder->priv = g_malloc0(sizeof(*folder->priv));
	folder->priv->frozen = 0;
	folder->priv->changed_frozen = camel_folder_change_info_new();
	folder->priv->lock = e_mutex_new(E_MUTEX_REC);
	folder->priv->change_lock = e_mutex_new(E_MUTEX_SIMPLE);
}

static void
camel_folder_finalize (CamelObject *object)
{
	CamelFolder *camel_folder = CAMEL_FOLDER (object);
	struct _CamelFolderPrivate *p = camel_folder->priv;

	g_free(camel_folder->name);
	g_free(camel_folder->full_name);
	g_free(camel_folder->description);

	if (camel_folder->parent_store)
		camel_object_unref (camel_folder->parent_store);

	if (camel_folder->summary)
		camel_object_unref (camel_folder->summary);

	camel_folder_change_info_free(p->changed_frozen);
	
	e_mutex_destroy(p->lock);
	e_mutex_destroy(p->change_lock);
	
	g_free(p);
}

CamelType
camel_folder_get_type (void)
{
	static CamelType camel_folder_type = CAMEL_INVALID_TYPE;

	if (camel_folder_type == CAMEL_INVALID_TYPE)	{
		camel_folder_type = camel_type_register (CAMEL_OBJECT_TYPE, "CamelFolder",
							 sizeof (CamelFolder),
							 sizeof (CamelFolderClass),
							 (CamelObjectClassInitFunc) camel_folder_class_init,
							 NULL,
							 (CamelObjectInitFunc) camel_folder_init,
							 (CamelObjectFinalizeFunc) camel_folder_finalize );
	}

	return camel_folder_type;
}


/**
 * camel_folder_construct:
 * @folder: a #CamelFolder object to construct
 * @parent_store: parent #CamelStore object of the folder
 * @full_name: full name of the folder
 * @name: short name of the folder
 *
 * Initalizes the folder by setting the parent store and name.
 **/
void
camel_folder_construct (CamelFolder *folder, CamelStore *parent_store,
			const char *full_name, const char *name)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (CAMEL_IS_STORE (parent_store));
	g_return_if_fail (folder->parent_store == NULL);
	g_return_if_fail (folder->name == NULL);

	folder->parent_store = parent_store;
	if (parent_store)
		camel_object_ref(parent_store);

	folder->name = g_strdup (name);
	folder->full_name = g_strdup (full_name);
}

static void
folder_sync (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	w(g_warning ("CamelFolder::sync not implemented for `%s'",
		     camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder))));
}

/**
 * camel_folder_sync:
 * @folder: a #CamelFolder object
 * @expunge: whether or not to expunge deleted messages
 * @ex: a #CamelException
 *
 * Sync changes made to a folder to its backing store, possibly
 * expunging deleted messages as well.
 **/
void
camel_folder_sync (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	if (!(folder->folder_flags & CAMEL_FOLDER_HAS_BEEN_DELETED))
		CF_CLASS (folder)->sync (folder, expunge, ex);
}

static void
refresh_info (CamelFolder *folder, CamelException *ex)
{
	/* No op */
}

/**
 * camel_folder_refresh_info:
 * @folder: a #CamelFolder object
 * @ex: a #CamelException
 *
 * Updates a folder's summary to be in sync with its backing store.
 **/
void
camel_folder_refresh_info (CamelFolder *folder, CamelException *ex)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	CF_CLASS (folder)->refresh_info (folder, ex);
}

static int
folder_getv(CamelObject *object, CamelException *ex, CamelArgGetV *args)
{
	CamelFolder *folder = (CamelFolder *)object;
	int i;
	guint32 tag;

	for (i=0;i<args->argc;i++) {
		CamelArgGet *arg = &args->argv[i];

		tag = arg->tag;

		switch (tag & CAMEL_ARG_TAG) {
			/* CamelObject args */
		case CAMEL_OBJECT_ARG_DESCRIPTION:
			if (folder->description == NULL)
				folder->description = g_strdup_printf("%s", folder->full_name);
			*arg->ca_str = folder->description;
			break;

			/* CamelFolder args */
		case CAMEL_FOLDER_ARG_NAME:
			*arg->ca_str = folder->name;
			break;
		case CAMEL_FOLDER_ARG_FULL_NAME:
			*arg->ca_str = folder->full_name;
			break;
		case CAMEL_FOLDER_ARG_STORE:
			*arg->ca_object = folder->parent_store;
			break;
		case CAMEL_FOLDER_ARG_PERMANENTFLAGS:
			*arg->ca_int = folder->permanent_flags;
			break;
		case CAMEL_FOLDER_ARG_TOTAL:
			*arg->ca_int = folder->summary->root_view->total_count;
			break;
		case CAMEL_FOLDER_ARG_UNREAD:
			*arg->ca_int = folder->summary->root_view->unread_count;
			break;
		case CAMEL_FOLDER_ARG_DELETED:
			*arg->ca_int = folder->summary->root_view->deleted_count;
			break;
		case CAMEL_FOLDER_ARG_JUNKED:
			*arg->ca_int = folder->summary->root_view->junk_count;
			break;
		case CAMEL_FOLDER_ARG_VISIBLE:
			*arg->ca_int = folder->summary->root_view->visible_count;
			break;
		case CAMEL_FOLDER_ARG_UID_ARRAY:
			g_warning("trying to get deprecated UID_ARRAY from folder");
			break;
		case CAMEL_FOLDER_ARG_INFO_ARRAY:
			g_warning("trying to get deprecated INFO_ARRAY from folder");
			break;
		case CAMEL_FOLDER_ARG_PROPERTIES:
			*arg->ca_ptr = NULL;
			break;
		default:
			continue;
		}

		arg->tag = (tag & CAMEL_ARG_TYPE) | CAMEL_ARG_IGNORE;
	}

	return parent_class->getv(object, ex, args);
}

static void
folder_free(CamelObject *o, guint32 tag, void *val)
{
	//CamelFolder *folder = (CamelFolder *)o;

	switch (tag & CAMEL_ARG_TAG) {
	case CAMEL_FOLDER_ARG_UID_ARRAY:
		break;
	case CAMEL_FOLDER_ARG_INFO_ARRAY:
		break;
	case CAMEL_FOLDER_ARG_PROPERTIES:
		g_slist_free(val);
		break;
	default:
		parent_class->free(o, tag, val);
	}
}

static const char *
get_name (CamelFolder *folder)
{
	return folder->name;
}


/**
 * camel_folder_get_name:
 * @folder: a #CamelFolder object
 *
 * Get the (short) name of the folder. The fully qualified name
 * can be obtained with the #camel_folder_get_full_name method.
 *
 * Returns the short name of the folder
 **/
const char *
camel_folder_get_name (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	return CF_CLASS (folder)->get_name (folder);
}


static const char *
get_full_name (CamelFolder *folder)
{
	return folder->full_name;
}


/**
 * camel_folder_get_full_name:
 * @folder: a #CamelFolder object
 *
 * Get the full name of the folder.
 *
 * Returns the full name of the folder
 **/
const char *
camel_folder_get_full_name (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	return CF_CLASS (folder)->get_full_name (folder);
}


static CamelStore *
get_parent_store (CamelFolder * folder)
{
	return folder->parent_store;
}

/**
 * camel_folder_get_parent_store:
 * @folder: a #CamelFolder object
 *
 * Returns the parent #CamelStore of the folder
 **/
CamelStore *
camel_folder_get_parent_store (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	return CF_CLASS (folder)->get_parent_store (folder);
}

static void
append_message (CamelFolder *folder, CamelMimeMessage *message,
		const CamelMessageInfo *info, char **appended_uid,
		CamelException *ex)
{
	camel_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_INVALID,
			      _("Unsupported operation: append message: for %s"),
			      camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));
	
	w(g_warning ("CamelFolder::append_message not implemented for `%s'",
		     camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder))));
	
	return;

}

/**
 * camel_folder_append_message:
 * @folder: a #CamelFolder object
 * @message: a #CamelMimeMessage object
 * @info: a #CamelMessageInfo with additional flags/etc to set on
 * new message, or %NULL
 * @appended_uid: if non-%NULL, the UID of the appended message will
 * be returned here, if it is known.
 * @ex: a #CamelException
 *
 * Append @message to @folder. Only the flag and tag data from @info
 * are used. If @info is %NULL, no flags or tags will be set.
 **/
void
camel_folder_append_message (CamelFolder *folder, CamelMimeMessage *message,
			     const CamelMessageInfo *info, char **appended_uid,
			     CamelException *ex)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	CAMEL_FOLDER_LOCK(folder, lock);

	CF_CLASS (folder)->append_message (folder, message, info, appended_uid, ex);

	CAMEL_FOLDER_UNLOCK(folder, lock);
}

static CamelMessageInfo *
get_message_info (CamelFolder *folder, const char *uid)
{
	g_return_val_if_fail(folder->summary != NULL, NULL);

	return camel_folder_summary_get(folder->summary, uid);
}


/**
 * camel_folder_get_message_info:
 * @folder: a #CamelFolder object
 * @uid: the uid of a message
 *
 * Retrieve the #CamelMessageInfo for the specified @uid.  This return
 * must be freed using #camel_folder_free_message_info.
 *
 * Returns the summary information for the indicated message, or %NULL
 * if the uid does not exist
 **/
CamelMessageInfo *
camel_folder_get_message_info (CamelFolder *folder, const char *uid)
{
	CamelMessageInfo *ret;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	ret = CF_CLASS (folder)->get_message_info (folder, uid);

	return ret;
}

static CamelMimeMessage *
get_message (CamelFolder *folder, const char *uid, CamelException *ex)
{
	w(g_warning ("CamelFolder::get_message not implemented for `%s'",
		     camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder))));
	
	return NULL;
}

/**
 * camel_folder_get_message:
 * @folder: a #CamelFolder object
 * @uid: the UID
 * @ex: a #CamelException
 *
 * Get a message from its UID in the folder.
 *
 * Returns a #CamelMimeMessage corresponding to @uid
 **/
CamelMimeMessage *
camel_folder_get_message (CamelFolder *folder, const char *uid, CamelException *ex)
{
	CamelMimeMessage *ret;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	ret = CF_CLASS (folder)->get_message (folder, uid, ex);
	
	if (ret && camel_debug_start(":folder")) {
		printf("CamelFolder:get_message('%s', '%s') =\n", folder->full_name, uid);
		camel_mime_message_dump(ret, FALSE);
		camel_debug_end();
	}

	return ret;
}

static CamelMessageIterator *
search(CamelFolder *folder, const char *vid, const char *expression, CamelMessageIterator *subset, CamelException *ex)
{
	if (folder->summary)
		return camel_folder_summary_search(folder->summary, vid, expression, subset, ex);

	camel_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_INVALID,
			      _("Unsupported operation: search by expression: for %s"),
			      camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));
	
	w(g_warning ("CamelFolder::search_by_expression not implemented for "
		     "`%s'", camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder))));
	
	return NULL;
}

/**
 * camel_folder_search:
 * @folder: a #CamelFolder object
 * @vid: view id, NULL means the root view
 * @expr: a search expression, NULL means match everything.
 * @subset: A messageiterator used to limit the search.
 * @ex: a #CamelException
 *
 * Searches the folder for messages matching the given search expression.
 *
 * Returns a CamelMessageIterator which can be used to examime the results.
 **/
CamelMessageIterator *
camel_folder_search(CamelFolder *folder, const char *vid, const char *expression, CamelMessageIterator *subset, CamelException *ex)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	/* NOTE: that it is upto the callee to lock */

	return CF_CLASS (folder)->search(folder, vid, expression, subset, ex);
}

static void
transfer_message_to (CamelFolder *source, const char *uid, CamelFolder *dest,
		     char **transferred_uid, gboolean delete_original,
		     CamelException *ex)
{
	CamelMimeMessage *msg;
	CamelMessageInfo *sinfo, *info = NULL;
	
	/* Default implementation. */
	
	msg = camel_folder_get_message(source, uid, ex);
	if (!msg)
		return;

	/* if its deleted we poke the flags, so we need to copy the messageinfo */
	sinfo = camel_folder_get_message_info(source, uid);
	if (sinfo) {
		info = camel_message_info_clone(sinfo);
		/* we don't want to retain the deleted flag */
		camel_message_info_set_flags(info, CAMEL_MESSAGE_DELETED, 0);
	}
	
	camel_folder_append_message (dest, msg, info, transferred_uid, ex);
	camel_object_unref (msg);

	if (info)
		camel_message_info_free (info);
	
	if (delete_original && sinfo && !camel_exception_is_set (ex))
		camel_message_info_set_flags(sinfo, CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_SEEN, ~0);

	if (sinfo)
		camel_message_info_free (sinfo);
}

static void
transfer_messages_to (CamelFolder *source, GPtrArray *uids, CamelFolder *dest, GPtrArray **transferred_uids, gboolean delete_originals, CamelException *ex)
{
	CamelException local;
	char **ret_uid = NULL;
	int i;

	if (transferred_uids) {
		*transferred_uids = g_ptr_array_new ();
		g_ptr_array_set_size (*transferred_uids, uids->len);
	}

	camel_exception_init(&local);
	if (ex == NULL)
		ex = &local;

	camel_operation_start(NULL, delete_originals ? _("Moving messages") : _("Copying messages"));

	if (uids->len > 1) {
		camel_folder_freeze(dest);
		if (delete_originals)
			camel_folder_freeze(source);
	}
	for (i = 0; i < uids->len && !camel_exception_is_set (ex); i++) {
		if (transferred_uids)
			ret_uid = (char **)&((*transferred_uids)->pdata[i]);
		transfer_message_to (source, uids->pdata[i], dest, ret_uid, delete_originals, ex);
		camel_operation_progress(NULL, i * 100 / uids->len);
	}
	if (uids->len > 1) {
		camel_folder_thaw(dest);
		if (delete_originals)
			camel_folder_thaw(source);
	}

	camel_operation_end(NULL);
	camel_exception_clear(&local);
}


/**
 * camel_folder_transfer_messages_to:
 * @source: the source #CamelFolder object
 * @uids: message UIDs in @source
 * @dest: the destination #CamelFolder object
 * @transferred_uids: if non-%NULL, the UIDs of the resulting messages
 * in @dest will be stored here, if known.
 * @delete_originals: whether or not to delete the original messages
 * @ex: a #CamelException
 *
 * This copies or moves messages from one folder to another. If the
 * @source and @dest folders have the same parent_store, this may be
 * more efficient than using #camel_folder_append_message.
 **/
void
camel_folder_transfer_messages_to (CamelFolder *source, GPtrArray *uids,
				   CamelFolder *dest, GPtrArray **transferred_uids,
				   gboolean delete_originals, CamelException *ex)
{
	g_return_if_fail (CAMEL_IS_FOLDER (source));
	g_return_if_fail (CAMEL_IS_FOLDER (dest));
	g_return_if_fail (uids != NULL);
	
	if (source == dest || uids->len == 0) {
		/* source and destination folders are the same, or no work to do, do nothing. */
		return;
	}
	
	if (source->parent_store == dest->parent_store) {
		/* If either folder is a vtrash, we need to use the
		 * vtrash transfer method.
		 */
		if (CAMEL_IS_VTRASH_FOLDER (dest))
			CF_CLASS (dest)->transfer_messages_to (source, uids, dest, transferred_uids, delete_originals, ex);
		else
			CF_CLASS (source)->transfer_messages_to (source, uids, dest, transferred_uids, delete_originals, ex);
	} else
		transfer_messages_to (source, uids, dest, transferred_uids, delete_originals, ex);
}

static void
delete (CamelFolder *folder)
{
	if (folder->summary)
		camel_folder_summary_clear (folder->summary);
}


/**
 * camel_folder_delete:
 * @folder: a #CamelFolder object
 *
 * Marks a folder object as deleted and performs any required cleanup.
 **/
void
camel_folder_delete (CamelFolder *folder)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	
	CAMEL_FOLDER_LOCK (folder, lock);
	if (folder->folder_flags & CAMEL_FOLDER_HAS_BEEN_DELETED) {
		CAMEL_FOLDER_UNLOCK (folder, lock);
		return;
	}
	
	folder->folder_flags |= CAMEL_FOLDER_HAS_BEEN_DELETED;
	
	CF_CLASS (folder)->delete (folder);

	CAMEL_FOLDER_UNLOCK (folder, lock);

	camel_object_trigger_event (folder, "deleted", NULL);
}

static void
folder_rename (CamelFolder *folder, const char *new)
{
	char *tmp;

	d(printf("CamelFolder:rename('%s')\n", new));

	g_free(folder->full_name);
	folder->full_name = g_strdup(new);
	g_free(folder->name);
	tmp = strrchr(new, '/');
	folder->name = g_strdup(tmp?tmp+1:new);
}


/**
 * camel_folder_rename:
 * @folder: a #CamelFolder object
 * @new: new name for the folder
 * 
 * Mark an active folder object as renamed.
 *
 * NOTE: This is an internal function used by camel stores, no locking
 * is performed on the folder.
 **/
void
camel_folder_rename(CamelFolder *folder, const char *new)
{
	char *old;

	old = g_strdup(folder->full_name);

	CF_CLASS (folder)->rename(folder, new);

	camel_object_trigger_event (folder, "renamed", old);
	g_free(old);
}

static void
freeze (CamelFolder *folder)
{
	CAMEL_FOLDER_LOCK(folder, change_lock);

	g_assert(folder->priv->frozen >= 0);

	folder->priv->frozen++;

	d(printf ("freeze(%p '%s') = %d\n", folder, folder->full_name, folder->priv->frozen));
	CAMEL_FOLDER_UNLOCK(folder, change_lock);
}


/**
 * camel_folder_freeze:
 * @folder: a #CamelFolder
 *
 * Freezes the folder so that a series of operation can be performed
 * without "folder_changed" signals being emitted.  When the folder is
 * later thawed with #camel_folder_thaw, the suppressed signals will
 * be emitted.
 **/
void
camel_folder_freeze (CamelFolder * folder)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	CF_CLASS (folder)->freeze (folder);
}

static void
thaw (CamelFolder * folder)
{
	CamelFolderChangeInfo *info = NULL;

	CAMEL_FOLDER_LOCK(folder, change_lock);

	g_assert(folder->priv->frozen > 0);

	folder->priv->frozen--;

	d(printf ("thaw(%p '%s') = %d\n", folder, folder->full_name, folder->priv->frozen));

	if (folder->priv->frozen == 0
	    && camel_folder_change_info_changed(folder->priv->changed_frozen)) {
		info = folder->priv->changed_frozen;
		folder->priv->changed_frozen = camel_folder_change_info_new();
	}
	
	CAMEL_FOLDER_UNLOCK(folder, change_lock);

	if (info) {
		camel_object_trigger_event (folder, "folder_changed", info);
		camel_folder_change_info_free(info);
	}
}

/**
 * camel_folder_thaw:
 * @folder: a #CamelFolder object
 *
 * Thaws the folder and emits any pending folder_changed
 * signals.
 **/
void
camel_folder_thaw (CamelFolder *folder)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (folder->priv->frozen != 0);

	CF_CLASS (folder)->thaw (folder);
}

static gboolean
is_frozen (CamelFolder *folder)
{
	return folder->priv->frozen != 0;
}


/**
 * camel_folder_is_frozen:
 * @folder: a #CamelFolder object
 *
 * Returns whether or not the folder is frozen
 **/
gboolean
camel_folder_is_frozen (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	return CF_CLASS (folder)->is_frozen (folder);
}

struct _folder_filter_msg {
	CamelSessionThreadMsg msg;

	GPtrArray *recents;
	GPtrArray *junk;
	GPtrArray *notjunk;
	CamelFolder *folder;
	CamelFilterDriver *driver;
	CamelException ex;
};

static void
filter_filter(CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _folder_filter_msg *m = (struct _folder_filter_msg *)msg;
	CamelMessageInfo *info;
	int i, status = 0;
	CamelURL *uri;
	char *source_url;
	CamelException ex;
	CamelJunkPlugin *csp = ((CamelService *)m->folder->parent_store)->session->junk_plugin;

	if (m->junk) {
		camel_operation_start (NULL, _("Learning junk"));

		for (i = 0; i < m->junk->len; i ++) {
			CamelMimeMessage *msg = camel_folder_get_message(m->folder, m->junk->pdata[i], NULL);
			int pc = 100 * i / m->junk->len;
			
			camel_operation_progress(NULL, pc);

			if (msg) {
				camel_junk_plugin_report_junk (csp, msg);
				camel_object_unref (msg);
			}
		}
		camel_operation_end (NULL);
	}

	if (m->notjunk) {
		camel_operation_start (NULL, _("Learning non-junk"));
		for (i = 0; i < m->notjunk->len; i ++) {
			CamelMimeMessage *msg = camel_folder_get_message(m->folder, m->notjunk->pdata[i], NULL);
			int pc = 100 * i / m->notjunk->len;

			camel_operation_progress(NULL, pc);

			if (msg) {
				camel_junk_plugin_report_notjunk (csp, msg);
				camel_object_unref (msg);
			}
		}
		camel_operation_end (NULL);
	}

	if (m->junk || m->notjunk)
		camel_junk_plugin_commit_reports (csp);

	if (m->driver && m->recents) {
		camel_operation_start(NULL, _("Filtering new message(s)"));

		source_url = camel_service_get_url((CamelService *)m->folder->parent_store);
		uri = camel_url_new(source_url, NULL);
		g_free(source_url);
		if (m->folder->full_name && m->folder->full_name[0] != '/') {
			char *tmp = alloca(strlen(m->folder->full_name)+2);

			sprintf(tmp, "/%s", m->folder->full_name);
			camel_url_set_path(uri, tmp);
		} else
			camel_url_set_path(uri, m->folder->full_name);
		source_url = camel_url_to_string(uri, CAMEL_URL_HIDE_ALL);
		camel_url_free(uri);

		for (i=0;status == 0 && i<m->recents->len;i++) {
			char *uid = m->recents->pdata[i];
			int pc = 100 * i / m->recents->len;

			camel_operation_progress(NULL, pc);

			info = camel_folder_get_message_info(m->folder, uid);
			if (info == NULL) {
				g_warning("uid %s vanished from folder: %s", uid, source_url);
				continue;
			}

			status = camel_filter_driver_filter_message(m->driver, NULL, info, uid, m->folder, source_url, source_url, &m->ex);

			camel_message_info_free(info);
		}

		camel_exception_init(&ex);
		camel_filter_driver_flush(m->driver, &ex);
		if (!camel_exception_is_set(&m->ex))
			camel_exception_xfer(&m->ex, &ex);

		g_free(source_url);

		camel_operation_end(NULL);
	}
}

static void
filter_free(CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _folder_filter_msg *m = (struct _folder_filter_msg *)msg;

	if (m->driver)
		camel_object_unref(m->driver);
	if (m->recents)
		camel_folder_free_deep(m->folder, m->recents);
	if (m->junk)
		camel_folder_free_deep(m->folder, m->junk);
	if (m->notjunk)
		camel_folder_free_deep(m->folder, m->notjunk);

	camel_folder_thaw(m->folder);
	camel_object_unref(m->folder);
}

static CamelSessionThreadOps filter_ops = {
	filter_filter,
	filter_free,
};

/* Event hooks that block emission when frozen */
static gboolean
folder_changed (CamelObject *obj, gpointer event_data)
{
	CamelFolder *folder = (CamelFolder *)obj;
	CamelFolderChangeInfo *changed = event_data;
	CamelSession *session = ((CamelService *)folder->parent_store)->session;
	CamelFilterDriver *driver = NULL;
	GPtrArray *junk = NULL;
	GPtrArray *notjunk = NULL;
	GPtrArray *recents = NULL;
	int i;

	d(printf ("folder_changed(%p:'%s', %p), frozen=%d\n", obj, folder->full_name, event_data, folder->priv->frozen));
	d(printf(" added %d removed %d changed %d recent %d\n",
		 changed->uid_added->len, changed->uid_removed->len,
		 changed->uid_changed->len, changed->uid_recent->len));

	if (changed == NULL) {
		w(g_warning ("Class %s is passing NULL to folder_changed event",
			     camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder))));
		return TRUE;
	}

	CAMEL_FOLDER_LOCK(folder, change_lock);
	if (folder->priv->frozen) {
		camel_folder_change_info_cat(folder->priv->changed_frozen, changed);
		CAMEL_FOLDER_UNLOCK(folder, change_lock);

		return FALSE;
	}
	CAMEL_FOLDER_UNLOCK(folder, change_lock);

	if (changed->uid_changed->len) {
		guint32 flags;

		for (i = 0; i < changed->uid_changed->len; i ++) {
			CamelMessageInfo *info = camel_folder_get_message_info(folder, changed->uid_changed->pdata [i]);

			if (info == NULL)
				continue;

			flags = camel_message_info_flags(info);
			if (flags & CAMEL_MESSAGE_JUNK_LEARN) {
				if (flags & CAMEL_MESSAGE_JUNK) {
					if (!junk)
						junk = g_ptr_array_new();
					g_ptr_array_add (junk, g_strdup (changed->uid_changed->pdata [i]));
				} else {
					if (!notjunk)
						notjunk = g_ptr_array_new();
					g_ptr_array_add (notjunk, g_strdup (changed->uid_changed->pdata [i]));
				}
				/* reset junk learn flag so that we don't process it again */
				camel_message_info_set_flags(info, CAMEL_MESSAGE_JUNK_LEARN, 0);
			}
			camel_message_info_free(info);
		}
	}

	if ((folder->folder_flags & (CAMEL_FOLDER_FILTER_RECENT|CAMEL_FOLDER_FILTER_JUNK))
	    && changed->uid_recent->len > 0)
		driver = camel_session_get_filter_driver(session,
							 (folder->folder_flags & CAMEL_FOLDER_FILTER_RECENT) 
							 ? "incoming":"junktest", NULL);
		
	if (driver) {
		recents = g_ptr_array_new();
		for (i=0;i<changed->uid_recent->len;i++)
			g_ptr_array_add(recents, g_strdup(changed->uid_recent->pdata[i]));
	}

	if (driver || junk || notjunk) {
		struct _folder_filter_msg *msg;

		d(printf("* launching filter thread %d new mail, %d junk and %d not junk\n",
			 recents?recents->len:0, junk?junk->len:0, notjunk?notjunk->len:0));

		msg = camel_session_thread_msg_new(session, &filter_ops, sizeof(*msg));
		msg->recents = recents;
		msg->junk = junk;
		msg->notjunk = notjunk;
		msg->folder = folder;
		camel_object_ref(folder);
		camel_folder_freeze(folder);
		msg->driver = driver;
		camel_exception_init(&msg->ex);
		camel_session_thread_queue(session, &msg->msg, 0);
	}

	return TRUE;
}


/**
 * camel_folder_free_nop:
 * @folder: a #CamelFolder object
 * @array: an array of uids or #CamelMessageInfo
 *
 * "Frees" the provided array by doing nothing. Used by #CamelFolder
 * subclasses as an implementation for free_uids, or free_summary when
 * the returned array is "static" information and should not be freed.
 **/
void
camel_folder_free_nop (CamelFolder *folder, GPtrArray *array)
{
	;
}


/**
 * camel_folder_free_shallow:
 * @folder: a #CamelFolder object
 * @array: an array of uids or #CamelMessageInfo
 *
 * Frees the provided array but not its contents. Used by #CamelFolder
 * subclasses as an implementation for free_uids or free_summary when
 * the returned array needs to be freed but its contents come from
 * "static" information.
 **/
void
camel_folder_free_shallow (CamelFolder *folder, GPtrArray *array)
{
	g_ptr_array_free (array, TRUE);
}


/**
 * camel_folder_free_deep:
 * @folder: a #CamelFolder object
 * @array: an array of uids
 *
 * Frees the provided array and its contents. Used by #CamelFolder
 * subclasses as an implementation for free_uids when the provided
 * information was created explicitly by the corresponding get_ call.
 **/
void
camel_folder_free_deep (CamelFolder *folder, GPtrArray *array)
{
	int i;

	for (i = 0; i < array->len; i++)
		g_free (array->pdata[i]);
	g_ptr_array_free (array, TRUE);
}

struct _CamelFolderChangeInfoPrivate {
	GHashTable *uid_stored;	/* what we have stored, which array they're in */
	GHashTable *uid_source;	/* used to create unique lists */
	struct _EMemPool *uid_pool;	/* pool used to store copies of uid strings */
};


/**
 * camel_folder_change_info_new:
 * 
 * Create a new folder change info structure.
 *
 * Change info structures are not MT-SAFE and must be
 * locked for exclusive access externally.
 *
 * Returns a new #CamelFolderChangeInfo
 **/
CamelFolderChangeInfo *
camel_folder_change_info_new(void)
{
	CamelFolderChangeInfo *info;

	info = g_malloc(sizeof(*info));
	info->uid_added = g_ptr_array_new();
	info->uid_removed = g_ptr_array_new();
	info->uid_changed = g_ptr_array_new();
	info->uid_recent = g_ptr_array_new();
	info->priv = g_malloc0(sizeof(*info->priv));
	info->priv->uid_stored = g_hash_table_new(g_str_hash, g_str_equal);
	info->priv->uid_source = NULL;
	info->priv->uid_pool = e_mempool_new(512, 256, E_MEMPOOL_ALIGN_BYTE);

	return info;
}


/**
 * camel_folder_change_info_add_source:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 * 
 * Add a source uid for generating a changeset.
 **/
void
camel_folder_change_info_add_source(CamelFolderChangeInfo *info, const char *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	
	g_assert(info != NULL);
	
	p = info->priv;
	
	if (p->uid_source == NULL)
		p->uid_source = g_hash_table_new(g_str_hash, g_str_equal);

	if (g_hash_table_lookup(p->uid_source, uid) == NULL)
		g_hash_table_insert(p->uid_source, e_mempool_strdup(p->uid_pool, uid), GINT_TO_POINTER (1));
}


/**
 * camel_folder_change_info_add_source_list:
 * @info: a #CamelFolderChangeInfo
 * @list: a list of uids
 * 
 * Add a list of source uid's for generating a changeset.
 **/
void
camel_folder_change_info_add_source_list(CamelFolderChangeInfo *info, const GPtrArray *list)
{
	struct _CamelFolderChangeInfoPrivate *p;
	int i;
	
	g_assert(info != NULL);
	g_assert(list != NULL);
	
	p = info->priv;

	if (p->uid_source == NULL)
		p->uid_source = g_hash_table_new(g_str_hash, g_str_equal);

	for (i=0;i<list->len;i++) {
		char *uid = list->pdata[i];

		if (g_hash_table_lookup(p->uid_source, uid) == NULL)
			g_hash_table_insert(p->uid_source, e_mempool_strdup(p->uid_pool, uid), GINT_TO_POINTER (1));
	}
}


/**
 * camel_folder_change_info_add_update:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 * 
 * Add a uid from the updated list, used to generate a changeset diff.
 **/
void
camel_folder_change_info_add_update(CamelFolderChangeInfo *info, const char *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	char *key;
	int value;
	
	g_assert(info != NULL);
	
	p = info->priv;
	
	if (p->uid_source == NULL) {
		camel_folder_change_info_add_uid(info, uid);
		return;
	}

	if (g_hash_table_lookup_extended(p->uid_source, uid, (void **)&key, (void **)&value)) {
		g_hash_table_remove(p->uid_source, key);
	} else {
		camel_folder_change_info_add_uid(info, uid);
	}
}


/**
 * camel_folder_change_info_add_update_list:
 * @info: a #CamelFolderChangeInfo
 * @list: a list of uids
 * 
 * Add a list of uid's from the updated list.
 **/
void
camel_folder_change_info_add_update_list(CamelFolderChangeInfo *info, const GPtrArray *list)
{
	int i;
	
	g_assert(info != NULL);
	g_assert(list != NULL);
	
	for (i=0;i<list->len;i++)
		camel_folder_change_info_add_update(info, list->pdata[i]);
}

static void
change_info_remove(char *key, void *value, CamelFolderChangeInfo *info)
{
	struct _CamelFolderChangeInfoPrivate *p = info->priv;
	GPtrArray *olduids;
	char *olduid;

	if (g_hash_table_lookup_extended(p->uid_stored, key, (void **)&olduid, (void **)&olduids)) {
		/* if it was added/changed them removed, then remove it */
		if (olduids != info->uid_removed) {
			g_ptr_array_remove_fast(olduids, olduid);
			g_ptr_array_add(info->uid_removed, olduid);
			g_hash_table_insert(p->uid_stored, olduid, info->uid_removed);
		}
		return;
	}

	/* we dont need to copy this, as they've already been copied into our pool */
	g_ptr_array_add(info->uid_removed, key);
	g_hash_table_insert(p->uid_stored, key, info->uid_removed);
}


/**
 * camel_folder_change_info_build_diff:
 * @info: a #CamelFolderChangeInfo
 * 
 * Compare the source uid set to the updated uid set and generate the
 * differences into the added and removed lists.
 **/
void
camel_folder_change_info_build_diff(CamelFolderChangeInfo *info)
{
	struct _CamelFolderChangeInfoPrivate *p;
	
	g_assert(info != NULL);
	
	p = info->priv;
	
	if (p->uid_source) {
		g_hash_table_foreach(p->uid_source, (GHFunc)change_info_remove, info);
		g_hash_table_destroy(p->uid_source);
		p->uid_source = NULL;
	}
}

static void
change_info_cat(CamelFolderChangeInfo *info, GPtrArray *source, void (*add)(CamelFolderChangeInfo *info, const char *uid))
{
	int i;

	for (i=0;i<source->len;i++)
		add(info, source->pdata[i]);
}


/**
 * camel_folder_change_info_cat:
 * @info: a #CamelFolderChangeInfo to append to
 * @src: a #CamelFolderChangeInfo to append from
 * 
 * Concatenate one change info onto antoher.  Can be used to copy them
 * too.
 **/
void
camel_folder_change_info_cat(CamelFolderChangeInfo *info, CamelFolderChangeInfo *source)
{
	g_assert(info != NULL);
	g_assert(source != NULL);
	
	change_info_cat(info, source->uid_added, camel_folder_change_info_add_uid);
	change_info_cat(info, source->uid_removed, camel_folder_change_info_remove_uid);
	change_info_cat(info, source->uid_changed, camel_folder_change_info_change_uid);
	change_info_cat(info, source->uid_recent, camel_folder_change_info_recent_uid);
}

/**
 * camel_folder_change_info_add_uid:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 * 
 * Add a new uid to the changeinfo.
 **/
void
camel_folder_change_info_add_uid(CamelFolderChangeInfo *info, const char *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	GPtrArray *olduids;
	char *olduid;
	
	g_assert(info != NULL);
	
	p = info->priv;
	
	if (g_hash_table_lookup_extended(p->uid_stored, uid, (void **)&olduid, (void **)&olduids)) {
		/* if it was removed then added, promote it to a changed */
		/* if it was changed then added, leave as changed */
		if (olduids == info->uid_removed) {
			g_ptr_array_remove_fast(olduids, olduid);
			g_ptr_array_add(info->uid_changed, olduid);
			g_hash_table_insert(p->uid_stored, olduid, info->uid_changed);
		}
		return;
	}

	olduid = e_mempool_strdup(p->uid_pool, uid);
	g_ptr_array_add(info->uid_added, olduid);
	g_hash_table_insert(p->uid_stored, olduid, info->uid_added);
}


/**
 * camel_folder_change_info_remove_uid:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 * 
 * Add a uid to the removed uid list.
 **/
void
camel_folder_change_info_remove_uid(CamelFolderChangeInfo *info, const char *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	GPtrArray *olduids;
	char *olduid;
	
	g_assert(info != NULL);
	
	p = info->priv;
	
	if (g_hash_table_lookup_extended(p->uid_stored, uid, (void **)&olduid, (void **)&olduids)) {
		/* if it was added/changed them removed, then remove it */
		if (olduids != info->uid_removed) {
			g_ptr_array_remove_fast(olduids, olduid);
			g_ptr_array_add(info->uid_removed, olduid);
			g_hash_table_insert(p->uid_stored, olduid, info->uid_removed);
		}
		return;
	}

	olduid = e_mempool_strdup(p->uid_pool, uid);
	g_ptr_array_add(info->uid_removed, olduid);
	g_hash_table_insert(p->uid_stored, olduid, info->uid_removed);
}


/**
 * camel_folder_change_info_change_uid:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 * 
 * Add a uid to the changed uid list.
 **/
void
camel_folder_change_info_change_uid(CamelFolderChangeInfo *info, const char *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	GPtrArray *olduids;
	char *olduid;
	
	g_assert(info != NULL);
	
	p = info->priv;
	
	if (g_hash_table_lookup_extended(p->uid_stored, uid, (void **)&olduid, (void **)&olduids)) {
		/* if we have it already, leave it as that */
		return;
	}

	olduid = e_mempool_strdup(p->uid_pool, uid);
	g_ptr_array_add(info->uid_changed, olduid);
	g_hash_table_insert(p->uid_stored, olduid, info->uid_changed);
}


/**
 * camel_folder_change_info_recent_uid:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 *
 * Add a recent uid to the changedinfo.
 **/
void
camel_folder_change_info_recent_uid(CamelFolderChangeInfo *info, const char *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	GPtrArray *olduids;
	char *olduid;
	
	g_assert(info != NULL);
	
	p = info->priv;

	/* always add to recent, but dont let anyone else know */	
	if (!g_hash_table_lookup_extended(p->uid_stored, uid, (void **)&olduid, (void **)&olduids)) {
		olduid = e_mempool_strdup(p->uid_pool, uid);
	}
	g_ptr_array_add(info->uid_recent, olduid);
}

/**
 * camel_folder_change_info_changed:
 * @info: a #CamelFolderChangeInfo
 *
 * Gets whether or not there have been any changes.
 *
 * Returns %TRUE if the changeset contains any changes or %FALSE
 * otherwise
 **/
gboolean
camel_folder_change_info_changed(CamelFolderChangeInfo *info)
{
	g_assert(info != NULL);
	
	return (info->uid_added->len || info->uid_removed->len || info->uid_changed->len || info->uid_recent->len);
}


/**
 * camel_folder_change_info_clear:
 * @info: a #CamelFolderChangeInfo
 * 
 * Empty out the change info; called after changes have been
 * processed.
 **/
void
camel_folder_change_info_clear(CamelFolderChangeInfo *info)
{
	struct _CamelFolderChangeInfoPrivate *p;
	
	g_assert(info != NULL);
	
	p = info->priv;
	
	g_ptr_array_set_size(info->uid_added, 0);
	g_ptr_array_set_size(info->uid_removed, 0);
	g_ptr_array_set_size(info->uid_changed, 0);
	g_ptr_array_set_size(info->uid_recent, 0);
	if (p->uid_source) {
		g_hash_table_destroy(p->uid_source);
		p->uid_source = NULL;
	}
	g_hash_table_destroy(p->uid_stored);
	p->uid_stored = g_hash_table_new(g_str_hash, g_str_equal);
	e_mempool_flush(p->uid_pool, TRUE);
}


/**
 * camel_folder_change_info_free:
 * @info: a #CamelFolderChangeInfo
 * 
 * Free memory associated with the folder change info lists.
 **/
void
camel_folder_change_info_free(CamelFolderChangeInfo *info)
{
	struct _CamelFolderChangeInfoPrivate *p;

	g_assert(info != NULL);
	
	p = info->priv;
	
	if (p->uid_source)
		g_hash_table_destroy(p->uid_source);

	g_hash_table_destroy(p->uid_stored);
	e_mempool_destroy(p->uid_pool);
	g_free(p);

	g_ptr_array_free(info->uid_added, TRUE);
	g_ptr_array_free(info->uid_removed, TRUE);
	g_ptr_array_free(info->uid_changed, TRUE);
	g_ptr_array_free(info->uid_recent, TRUE);
	g_free(info);
}
