/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-folder.c: Abstract class for an email folder */

/*
 * Author:
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib/gi18n-lib.h>

#include "camel-db.h"
#include "camel-debug.h"
#include "camel-filter-driver.h"
#include "camel-folder.h"
#include "camel-mempool.h"
#include "camel-mime-message.h"
#include "camel-operation.h"
#include "camel-private.h"
#include "camel-session.h"
#include "camel-store.h"
#include "camel-vtrash-folder.h"
#include "camel-string-utils.h"

#define CAMEL_FOLDER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_FOLDER, CamelFolderPrivate))

#define d(x)
#define w(x)

static gboolean refresh_info (CamelFolder *folder, GError **error);

static const gchar *get_name (CamelFolder *folder);
static const gchar *get_full_name (CamelFolder *folder);
static CamelStore *get_parent_store   (CamelFolder *folder);

static guint32 get_permanent_flags (CamelFolder *folder);
static guint32 get_message_flags (CamelFolder *folder, const gchar *uid);
static gboolean set_message_flags (CamelFolder *folder, const gchar *uid, guint32 flags, guint32 set);
static gboolean get_message_user_flag (CamelFolder *folder, const gchar *uid, const gchar *name);
static void set_message_user_flag (CamelFolder *folder, const gchar *uid, const gchar *name, gboolean value);
static const gchar *get_message_user_tag (CamelFolder *folder, const gchar *uid, const gchar *name);
static void set_message_user_tag (CamelFolder *folder, const gchar *uid, const gchar *name, const gchar *value);

static gint get_message_count (CamelFolder *folder);

static gint folder_getv (CamelObject *object, GError **error, CamelArgGetV *args);
static void folder_free (CamelObject *o, guint32 tag, gpointer val);

static GPtrArray        *get_uids            (CamelFolder *folder);
static GPtrArray	*get_uncached_uids   (CamelFolder *, GPtrArray * uids, GError **);
static void              free_uids           (CamelFolder *folder,
					      GPtrArray *array);
static gint cmp_uids (CamelFolder *folder, const gchar *uid1, const gchar *uid2);
static void              sort_uids           (CamelFolder *folder,
					      GPtrArray *uids);
static GPtrArray        *get_summary         (CamelFolder *folder);
static void              free_summary        (CamelFolder *folder,
					      GPtrArray *array);

static CamelMessageInfo *get_message_info    (CamelFolder *folder, const gchar *uid);
static void		 free_message_info   (CamelFolder *folder, CamelMessageInfo *info);
static void		 ref_message_info    (CamelFolder *folder, CamelMessageInfo *info);

static void            search_free           (CamelFolder * folder, GPtrArray *result);

static gboolean        transfer_messages_to  (CamelFolder *source, GPtrArray *uids, CamelFolder *dest,
					      GPtrArray **transferred_uids, gboolean delete_originals, GError **error);

static void            delete                (CamelFolder *folder);
static void            folder_rename         (CamelFolder *folder, const gchar *new);

static void            freeze                (CamelFolder *folder);
static void            thaw                  (CamelFolder *folder);
static gboolean        is_frozen             (CamelFolder *folder);

static gboolean        folder_changed        (CamelObject *object,
					      gpointer event_data);

static CamelFolderQuotaInfo *get_quota_info  (CamelFolder *folder);

G_DEFINE_ABSTRACT_TYPE (CamelFolder, camel_folder, CAMEL_TYPE_OBJECT)

static void
folder_dispose (GObject *object)
{
	CamelFolder *folder;

	folder = CAMEL_FOLDER (object);

	if (folder->parent_store != NULL) {
		g_object_unref (folder->parent_store);
		folder->parent_store = NULL;
	}

	if (folder->summary) {
		folder->summary->folder = NULL;
		g_object_unref (folder->summary);
		folder->summary = NULL;
	}

	/* Chain up to parent's dispose () method. */
	G_OBJECT_CLASS (camel_folder_parent_class)->dispose (object);
}

static void
folder_finalize (GObject *object)
{
	CamelFolder *folder;
	CamelFolderPrivate *priv;

	folder = CAMEL_FOLDER (object);
	priv = CAMEL_FOLDER_GET_PRIVATE (object);

	g_free (folder->name);
	g_free (folder->full_name);
	g_free (folder->description);

	camel_folder_change_info_free (priv->changed_frozen);

	g_static_rec_mutex_free (&priv->lock);
	g_static_mutex_free (&priv->change_lock);

	/* Chain up to parent's finalize () method. */
	G_OBJECT_CLASS (camel_folder_parent_class)->finalize (object);
}

static void
camel_folder_class_init (CamelFolderClass *class)
{
	GObjectClass *object_class;
	CamelObjectClass *camel_object_class;

	g_type_class_add_private (class, sizeof (CamelFolderPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = folder_dispose;
	object_class->finalize = folder_finalize;

	camel_object_class = CAMEL_OBJECT_CLASS (class);
	camel_object_class->getv = folder_getv;
	camel_object_class->free = folder_free;

	class->refresh_info = refresh_info;
	class->get_name = get_name;
	class->get_full_name = get_full_name;
	class->get_parent_store = get_parent_store;
	class->get_message_count = get_message_count;
	class->get_permanent_flags = get_permanent_flags;
	class->get_message_flags = get_message_flags;
	class->set_message_flags = set_message_flags;
	class->get_message_user_flag = get_message_user_flag;
	class->set_message_user_flag = set_message_user_flag;
	class->get_message_user_tag = get_message_user_tag;
	class->set_message_user_tag = set_message_user_tag;
	class->get_uids = get_uids;
	class->get_uncached_uids = get_uncached_uids;
	class->free_uids = free_uids;
	class->cmp_uids = cmp_uids;
	class->sort_uids = sort_uids;
	class->get_summary = get_summary;
	class->free_summary = free_summary;
	class->search_free = search_free;
	class->get_message_info = get_message_info;
	class->ref_message_info = ref_message_info;
	class->free_message_info = free_message_info;
	class->transfer_messages_to = transfer_messages_to;
	class->delete = delete;
	class->rename = folder_rename;
	class->freeze = freeze;
	class->sync_message = NULL;
	class->thaw = thaw;
	class->is_frozen = is_frozen;
	class->get_quota_info = get_quota_info;

	camel_object_class_add_event (
		camel_object_class, "folder_changed", folder_changed);
	camel_object_class_add_event (
		camel_object_class, "deleted", NULL);
	camel_object_class_add_event (
		camel_object_class, "renamed", NULL);
}

static void
camel_folder_init (CamelFolder *folder)
{
	folder->priv = CAMEL_FOLDER_GET_PRIVATE (folder);

	folder->priv->frozen = 0;
	folder->priv->changed_frozen = camel_folder_change_info_new ();

	g_static_rec_mutex_init (&folder->priv->lock);
	g_static_mutex_init (&folder->priv->change_lock);
}

GQuark
camel_folder_error_quark (void)
{
	static GQuark quark = 0;

	if (G_UNLIKELY (quark == 0)) {
		const gchar *string = "camel-folder-error-quark";
		quark = g_quark_from_static_string (string);
	}

	return quark;
}

/**
 * camel_folder_get_filename:
 *
 * Since: 2.26
 **/
gchar *
camel_folder_get_filename (CamelFolder *folder,
                           const gchar *uid,
                           GError **error)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_filename != NULL, NULL);

	return class->get_filename (folder, uid, error);
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
			const gchar *full_name, const gchar *name)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (CAMEL_IS_STORE (parent_store));
	g_return_if_fail (folder->parent_store == NULL);
	g_return_if_fail (folder->name == NULL);

	folder->parent_store = parent_store;
	if (parent_store)
		g_object_ref (parent_store);

	folder->name = g_strdup (name);
	folder->full_name = g_strdup (full_name);
}

/**
 * camel_folder_sync:
 * @folder: a #CamelFolder object
 * @expunge: whether or not to expunge deleted messages
 * @error: return location for a #GError, or %NULL
 *
 * Sync changes made to a folder to its backing store, possibly
 * expunging deleted messages as well.
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
camel_folder_sync (CamelFolder *folder,
                   gboolean expunge,
                   GError **error)
{
	CamelFolderClass *class;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->sync != NULL, FALSE);

	CAMEL_FOLDER_REC_LOCK (folder, lock);

	if (!(folder->folder_flags & CAMEL_FOLDER_HAS_BEEN_DELETED))
		success = class->sync (folder, expunge, error);

	CAMEL_FOLDER_REC_UNLOCK (folder, lock);

	return success;
}

static gboolean
refresh_info (CamelFolder *folder,
              GError **error)
{
	return TRUE;
}

/**
 * camel_folder_refresh_info:
 * @folder: a #CamelFolder object
 * @error: return location for a #GError, or %NULL
 *
 * Updates a folder's summary to be in sync with its backing store.
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
camel_folder_refresh_info (CamelFolder *folder,
                           GError **error)
{
	CamelFolderClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->refresh_info != NULL, FALSE);

	CAMEL_FOLDER_REC_LOCK (folder, lock);

	success = class->refresh_info (folder, error);

	CAMEL_FOLDER_REC_UNLOCK (folder, lock);

	return success;
}

static gint
folder_getv (CamelObject *object,
             GError **error,
             CamelArgGetV *args)
{
	CamelFolder *folder = (CamelFolder *)object;
	gint i;
	guint32 tag;
	gint unread = -1, deleted = 0, junked = 0, junked_not_deleted = 0, visible = 0, count = -1;

	for (i = 0; i < args->argc; i++) {
		CamelArgGet *arg = &args->argv[i];

		tag = arg->tag;

		switch (tag & CAMEL_ARG_TAG) {
			/* CamelObject args */
		case CAMEL_OBJECT_ARG_DESCRIPTION:
			if (folder->description == NULL)
				folder->description = g_strdup_printf ("%s", folder->full_name);
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
			*arg->ca_int = camel_folder_summary_count (folder->summary);
			break;
		case CAMEL_FOLDER_ARG_UNREAD:
		case CAMEL_FOLDER_ARG_DELETED:
		case CAMEL_FOLDER_ARG_JUNKED:
		case CAMEL_FOLDER_ARG_JUNKED_NOT_DELETED:
		case CAMEL_FOLDER_ARG_VISIBLE:
			/* This is so we can get the values atomically, and also so we can calculate them only once */

			/* FIXME[disk-summary] Add a better base class
			 * function to get counts specific to normal/vee
			 * folder. */
			if (unread == -1) {

				if (1) {
					unread = folder->summary->unread_count;
					deleted = folder->summary->deleted_count;
					junked = folder->summary->junk_count;
					junked_not_deleted = folder->summary->junk_not_deleted_count;
					visible = folder->summary->visible_count;
				} else {
					/* count = camel_folder_summary_count (folder->summary);
					for (j = 0; j < count; j++) {
						if ((info = camel_folder_summary_index (folder->summary, j))) {
							guint32 flags = camel_message_info_flags (info);

							if ((flags & (CAMEL_MESSAGE_SEEN|CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_JUNK)) == 0)
								unread++;
							if (flags & CAMEL_MESSAGE_DELETED)
								deleted++;
							if (flags & CAMEL_MESSAGE_JUNK) {
								junked++;
								if (!(flags & CAMEL_MESSAGE_DELETED))
									junked_not_deleted++;
							}
							if ((flags & (CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_JUNK)) == 0)
								visible++;
							camel_message_info_free (info);
						}

					}*/
                                        /* FIXME[disk-summary] I added it for
					 * vfolders summary storage, does it
					 * harm ? */
					if (unread == -1) {
						/*unread = folder->summary->unread_count;*/
						/*
						folder->summary->junk_count = junked;
						folder->summary->deleted_count = deleted;
						printf ("*************************** %s %d %d %d\n", folder->full_name, folder->summary->unread_count, unread, count);
						folder->summary->unread_count = unread; */
					}
				}

			}

			switch (tag & CAMEL_ARG_TAG) {
			case CAMEL_FOLDER_ARG_UNREAD:
				count = unread == -1 ? 0 : unread;
				break;
			case CAMEL_FOLDER_ARG_DELETED:
				count = deleted == -1 ? 0 : deleted;
				break;
			case CAMEL_FOLDER_ARG_JUNKED:
				count = junked == -1 ? 0 : junked;
				break;
			case CAMEL_FOLDER_ARG_JUNKED_NOT_DELETED:
				count = junked_not_deleted == -1 ? 0 : junked_not_deleted;
				break;
			case CAMEL_FOLDER_ARG_VISIBLE:
				count = visible == -1 ? 0 : visible;
				break;
			}

			*arg->ca_int = count;
			break;
		case CAMEL_FOLDER_ARG_UID_ARRAY: {
/*			gint j;
			CamelMessageInfo *info;
			GPtrArray *array;

			count = camel_folder_summary_count (folder->summary);
			array = g_ptr_array_new ();
			g_ptr_array_set_size (array, count);
			for (j=0; j<count; j++) {
				if ((info = camel_folder_summary_index (folder->summary, j))) {
					array->pdata[i] = g_strdup (camel_message_info_uid (info));
					camel_message_info_free (info);
				}
			}
			*arg->ca_ptr = array;*/
			/* WTH this is reqd ?, let it crash to find out who uses this */
			g_assert (0);
			break; }
		case CAMEL_FOLDER_ARG_INFO_ARRAY:
			*arg->ca_ptr = camel_folder_summary_array (folder->summary);
			break;
		case CAMEL_FOLDER_ARG_PROPERTIES:
			*arg->ca_ptr = NULL;
			break;
		default:
			continue;
		}

		arg->tag = (tag & CAMEL_ARG_TYPE) | CAMEL_ARG_IGNORE;
	}

	return CAMEL_OBJECT_CLASS (camel_folder_parent_class)->getv (object, error, args);
}

static void
folder_free (CamelObject *o, guint32 tag, gpointer val)
{
	CamelFolder *folder = (CamelFolder *)o;

	switch (tag & CAMEL_ARG_TAG) {
	case CAMEL_FOLDER_ARG_UID_ARRAY: {
		GPtrArray *array = val;
		gint i;

		for (i=0; i<array->len; i++)
			g_free (array->pdata[i]);
		g_ptr_array_free (array, TRUE);
		break; }
	case CAMEL_FOLDER_ARG_INFO_ARRAY:
		camel_folder_free_summary (folder, val);
		break;
	case CAMEL_FOLDER_ARG_PROPERTIES:
		g_slist_free (val);
		break;
	default:
		CAMEL_OBJECT_CLASS (camel_folder_parent_class)->free (o, tag, val);
	}
}

static const gchar *
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
 * Returns: the short name of the folder
 **/
const gchar *
camel_folder_get_name (CamelFolder *folder)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_name != NULL, NULL);

	return class->get_name (folder);
}

static const gchar *
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
 * Returns: the full name of the folder
 **/
const gchar *
camel_folder_get_full_name (CamelFolder *folder)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_full_name != NULL, NULL);

	return class->get_full_name (folder);
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
 * Returns: the parent #CamelStore of the folder
 **/
CamelStore *
camel_folder_get_parent_store (CamelFolder *folder)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_parent_store != NULL, NULL);

	return class->get_parent_store (folder);
}

/**
 * camel_folder_expunge:
 * @folder: a #CamelFolder object
 * @error: return location for a #GError, or %NULL
 *
 * Delete messages which have been marked as "DELETED"
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
camel_folder_expunge (CamelFolder *folder,
                      GError **error)
{
	CamelFolderClass *class;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->expunge != NULL, FALSE);

	CAMEL_FOLDER_REC_LOCK (folder, lock);

	if (!(folder->folder_flags & CAMEL_FOLDER_HAS_BEEN_DELETED))
		success = class->expunge (folder, error);

	CAMEL_FOLDER_REC_UNLOCK (folder, lock);

	return success;
}

static gint
get_message_count (CamelFolder *folder)
{
	g_return_val_if_fail (folder->summary != NULL, -1);

	return camel_folder_summary_count (folder->summary);
}

/**
 * camel_folder_get_message_count:
 * @folder: a #CamelFolder object
 *
 * Returns: the number of messages in the folder, or %-1 if unknown
 **/
gint
camel_folder_get_message_count (CamelFolder *folder)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), -1);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_message_count != NULL, -1);

	return class->get_message_count (folder);
}

/**
 * camel_folder_get_unread_message_count:
 * @folder: a #CamelFolder object
 *
 * DEPRECATED: use #camel_object_get instead.
 *
 * Returns: the number of unread messages in the folder, or %-1 if
 * unknown
 **/
gint
camel_folder_get_unread_message_count (CamelFolder *folder)
{
	gint count = -1;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), -1);

	camel_object_get (folder, NULL, CAMEL_FOLDER_UNREAD, &count, 0);

	return count;
}

/**
 * camel_folder_get_deleted_message_count:
 * @folder: a #CamelFolder object
 *
 * Returns: the number of deleted messages in the folder, or %-1 if
 * unknown
 **/
gint
camel_folder_get_deleted_message_count (CamelFolder *folder)
{
	gint count = -1;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), -1);

	camel_object_get (folder, NULL, CAMEL_FOLDER_DELETED, &count, 0);

	return count;
}

/**
 * camel_folder_append_message:
 * @folder: a #CamelFolder object
 * @message: a #CamelMimeMessage object
 * @info: a #CamelMessageInfo with additional flags/etc to set on
 * new message, or %NULL
 * @appended_uid: if non-%NULL, the UID of the appended message will
 * be returned here, if it is known.
 * @error: return location for a #GError, or %NULL
 *
 * Append @message to @folder. Only the flag and tag data from @info
 * are used. If @info is %NULL, no flags or tags will be set.
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
camel_folder_append_message (CamelFolder *folder,
                             CamelMimeMessage *message,
                             const CamelMessageInfo *info,
                             gchar **appended_uid,
                             GError **error)
{
	CamelFolderClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->append_message != NULL, FALSE);

	CAMEL_FOLDER_REC_LOCK (folder, lock);

	success = class->append_message (
		folder, message, info, appended_uid, error);

	CAMEL_FOLDER_REC_UNLOCK (folder, lock);

	return success;
}

static guint32
get_permanent_flags (CamelFolder *folder)
{
	return folder->permanent_flags;
}

/**
 * camel_folder_get_permanent_flags:
 * @folder: a #CamelFolder object
 *
 * Returns: the set of #CamelMessageFlags that can be permanently
 * stored on a message between sessions. If it includes
 * #CAMEL_FLAG_USER, then user-defined flags will be remembered.
 **/
guint32
camel_folder_get_permanent_flags (CamelFolder *folder)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_permanent_flags != NULL, 0);

	return class->get_permanent_flags (folder);
}

static guint32
get_message_flags (CamelFolder *folder, const gchar *uid)
{
	CamelMessageInfo *info;
	guint32 flags;

	g_return_val_if_fail (folder->summary != NULL, 0);

	info = camel_folder_summary_uid (folder->summary, uid);
	if (info == NULL)
		return 0;

	flags = camel_message_info_flags (info);
	camel_message_info_free (info);

	return flags;
}

/**
 * camel_folder_get_message_flags:
 * @folder: a #CamelFolder object
 * @uid: the UID of a message in @folder
 *
 * Deprecated: Use #camel_folder_get_message_info instead.
 *
 * Returns: the #CamelMessageFlags that are set on the indicated
 * message.
 **/
guint32
camel_folder_get_message_flags (CamelFolder *folder,
                                const gchar *uid)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);
	g_return_val_if_fail (uid != NULL, 0);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_message_flags != NULL, 0);

	return class->get_message_flags (folder, uid);
}

static gboolean
set_message_flags (CamelFolder *folder, const gchar *uid, guint32 flags, guint32 set)
{
	CamelMessageInfo *info;
	gint res;

	g_return_val_if_fail (folder->summary != NULL, FALSE);

	info = camel_folder_summary_uid (folder->summary, uid);
	if (info == NULL)
		return FALSE;

	res = camel_message_info_set_flags (info, flags, set);
	camel_message_info_free (info);

	return res;
}

/**
 * camel_folder_set_message_flags:
 * @folder: a #CamelFolder object
 * @uid: the UID of a message in @folder
 * @flags: a set of #CamelMessageFlag values to set
 * @set: the mask of values in @flags to use.
 *
 * Sets those flags specified by @flags to the values specified by @set
 * on the indicated message. (This may or may not persist after the
 * folder or store is closed. See #camel_folder_get_permanent_flags)
 *
 * E.g. to set the deleted flag and clear the draft flag, use
 * #camel_folder_set_message_flags (folder, uid, CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_DRAFT, CAMEL_MESSAGE_DELETED);
 *
 * DEPRECATED: Use #camel_message_info_set_flags on the message info directly
 * (when it works)
 *
 * Returns: %TRUE if the flags were changed or %FALSE otherwise
 **/
gboolean
camel_folder_set_message_flags (CamelFolder *folder,
                                const gchar *uid,
                                guint32 flags,
                                guint32 set)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->set_message_flags != NULL, FALSE);

	if ((flags & (CAMEL_MESSAGE_JUNK|CAMEL_MESSAGE_JUNK_LEARN)) == CAMEL_MESSAGE_JUNK) {
		flags |= CAMEL_MESSAGE_JUNK_LEARN;
		set &= ~CAMEL_MESSAGE_JUNK_LEARN;
	}

	return class->set_message_flags (folder, uid, flags, set);
}

static gboolean
get_message_user_flag (CamelFolder *folder, const gchar *uid, const gchar *name)
{
	CamelMessageInfo *info;
	gboolean ret;

	g_return_val_if_fail (folder->summary != NULL, FALSE);

	info = camel_folder_summary_uid (folder->summary, uid);
	if (info == NULL)
		return FALSE;

	ret = camel_message_info_user_flag (info, name);
	camel_message_info_free (info);

	return ret;
}

/**
 * camel_folder_get_message_user_flag:
 * @folder: a #CamelFolder object
 * @uid: the UID of a message in @folder
 * @name: the name of a user flag
 *
 * DEPRECATED: Use #camel_message_info_get_user_flag on the message
 * info directly
 *
 * Returns: %TRUE if the given user flag is set on the message or
 * %FALSE otherwise
 **/
gboolean
camel_folder_get_message_user_flag (CamelFolder *folder,
                                    const gchar *uid,
                                    const gchar *name)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);
	g_return_val_if_fail (uid != NULL, 0);
	g_return_val_if_fail (name != NULL, 0);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_message_user_flag != NULL, 0);

	return class->get_message_user_flag (folder, uid, name);
}

static void
set_message_user_flag (CamelFolder *folder, const gchar *uid, const gchar *name, gboolean value)
{
	CamelMessageInfo *info;

	g_return_if_fail (folder->summary != NULL);

	info = camel_folder_summary_uid (folder->summary, uid);
	if (info == NULL)
		return;

	camel_message_info_set_user_flag (info, name, value);
	camel_message_info_free (info);
}

/**
 * camel_folder_set_message_user_flag:
 * @folder: a #CamelFolder object
 * @uid: the UID of a message in @folder
 * @name: the name of the user flag to set
 * @value: the value to set it to
 *
 * DEPRECATED: Use #camel_message_info_set_user_flag on the
 * #CamelMessageInfo directly (when it works)
 *
 * Sets the user flag specified by @name to the value specified by @value
 * on the indicated message. (This may or may not persist after the
 * folder or store is closed. See #camel_folder_get_permanent_flags)
 **/
void
camel_folder_set_message_user_flag (CamelFolder *folder,
                                    const gchar *uid,
                                    const gchar *name,
                                    gboolean value)
{
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uid != NULL);
	g_return_if_fail (name != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->set_message_user_flag != NULL);

	class->set_message_user_flag (folder, uid, name, value);
}

static const gchar *
get_message_user_tag (CamelFolder *folder, const gchar *uid, const gchar *name)
{
	CamelMessageInfo *info;
	const gchar *ret;

	g_return_val_if_fail (folder->summary != NULL, NULL);

	info = camel_folder_summary_uid (folder->summary, uid);
	if (info == NULL)
		return NULL;

	ret = camel_message_info_user_tag (info, name);
	camel_message_info_free (info);

	return ret;
}

/**
 * camel_folder_get_message_user_tag:
 * @folder: a #CamelFolder object
 * @uid: the UID of a message in @folder
 * @name: the name of a user tag
 *
 * DEPRECATED: Use #camel_message_info_get_user_tag on the
 * #CamelMessageInfo directly.
 *
 * Returns: the value of the user tag
 **/
const gchar *
camel_folder_get_message_user_tag (CamelFolder *folder,
                                   const gchar *uid,
                                   const gchar *name)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (uid != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_message_user_tag != NULL, NULL);

	/* FIXME: should duplicate string */
	return class->get_message_user_tag (folder, uid, name);
}

static void
set_message_user_tag (CamelFolder *folder, const gchar *uid, const gchar *name, const gchar *value)
{
	CamelMessageInfo *info;

	g_return_if_fail (folder->summary != NULL);

	info = camel_folder_summary_uid (folder->summary, uid);
	if (info == NULL)
		return;

	camel_message_info_set_user_tag (info, name, value);
	camel_message_info_free (info);
}

/**
 * camel_folder_set_message_user_tag:
 * @folder: a #CamelFolder object
 * @uid: the UID of a message in @folder
 * @name: the name of the user tag to set
 * @value: the value to set it to
 *
 * DEPRECATED: Use #camel_message_info_set_user_tag on the
 * #CamelMessageInfo directly (when it works).
 *
 * Sets the user tag specified by @name to the value specified by @value
 * on the indicated message. (This may or may not persist after the
 * folder or store is closed. See #camel_folder_get_permanent_flags)
 **/
void
camel_folder_set_message_user_tag (CamelFolder *folder,
                                   const gchar *uid,
                                   const gchar *name,
                                   const gchar *value)
{
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uid != NULL);
	g_return_if_fail (name != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->set_message_user_tag != NULL);

	class->set_message_user_tag (folder, uid, name, value);
}

static CamelMessageInfo *
get_message_info (CamelFolder *folder, const gchar *uid)
{
	g_return_val_if_fail (folder->summary != NULL, NULL);

	return camel_folder_summary_uid (folder->summary, uid);
}

/**
 * camel_folder_get_message_info:
 * @folder: a #CamelFolder object
 * @uid: the uid of a message
 *
 * Retrieve the #CamelMessageInfo for the specified @uid.  This return
 * must be freed using #camel_folder_free_message_info.
 *
 * Returns: the summary information for the indicated message, or %NULL
 * if the uid does not exist
 **/
CamelMessageInfo *
camel_folder_get_message_info (CamelFolder *folder,
                               const gchar *uid)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_message_info != NULL, NULL);

	return class->get_message_info (folder, uid);
}

static void
free_message_info (CamelFolder *folder, CamelMessageInfo *info)
{
	g_return_if_fail (folder->summary != NULL);

	camel_message_info_free (info);
}

/**
 * camel_folder_free_message_info:
 * @folder: a #CamelFolder object
 * @info: a #CamelMessageInfo
 *
 * Free (unref) a #CamelMessageInfo, previously obtained with
 * #camel_folder_get_message_info.
 **/
void
camel_folder_free_message_info (CamelFolder *folder,
                                CamelMessageInfo *info)
{
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (info != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->free_message_info != NULL);

	class->free_message_info (folder, info);
}

static void
ref_message_info (CamelFolder *folder, CamelMessageInfo *info)
{
	g_return_if_fail (folder->summary != NULL);

	camel_message_info_ref (info);
}

/**
 * camel_folder_ref_message_info:
 * @folder: a #CamelFolder object
 * @info: a #CamelMessageInfo
 *
 * DEPRECATED: Use #camel_message_info_ref directly.
 *
 * Ref a #CamelMessageInfo, previously obtained with
 * #camel_folder_get_message_info.
 **/
void
camel_folder_ref_message_info (CamelFolder *folder,
                               CamelMessageInfo *info)
{
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (info != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->ref_message_info != NULL);

	class->ref_message_info (folder, info);
}

/* TODO: is this function required anyway? */
/**
 * camel_folder_has_summary_capability:
 * @folder: a #CamelFolder object
 *
 * Get whether or not the folder has a summary.
 *
 * Returns: %TRUE if a summary is available or %FALSE otherwise
 **/
gboolean
camel_folder_has_summary_capability (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	return folder->folder_flags & CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY;
}

/* UIDs stuff */

/**
 * camel_folder_get_message:
 * @folder: a #CamelFolder object
 * @uid: the UID
 * @error: return location for a #GError, or %NULL
 *
 * Get a message from its UID in the folder.
 *
 * Returns: a #CamelMimeMessage corresponding to @uid
 **/
CamelMimeMessage *
camel_folder_get_message (CamelFolder *folder,
                          const gchar *uid,
                          GError **error)
{
	CamelMimeMessage *ret;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	CAMEL_FOLDER_REC_LOCK (folder, lock);

	ret = CAMEL_FOLDER_GET_CLASS (folder)->get_message (folder, uid, error);

	CAMEL_FOLDER_REC_UNLOCK (folder, lock);

	if (ret && camel_debug_start (":folder")) {
		printf ("CamelFolder:get_message ('%s', '%s') =\n", folder->full_name, uid);
		camel_mime_message_dump (ret, FALSE);
		camel_debug_end ();
	}

	return ret;
}

/**
 * camel_folder_sync_message:
 * @folder: a #CamelFolder object
 * @uid: the UID
 * @error: return location for a #GError, or %NULL
 *
 * Ensure that a message identified by UID has been synced in the folder (so
 * that camel_folder_get_message on it later will work in offline mode).
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 2.26
 **/
gboolean
camel_folder_sync_message (CamelFolder *folder,
                           const gchar *uid,
                           GError **error)
{
	CamelFolderClass *class;
	gboolean success = FALSE;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);

	CAMEL_FOLDER_REC_LOCK (folder, lock);

	/* Use the sync_message method if the class implements it. */
	if (class->sync_message != NULL)
		success = class->sync_message (folder, uid, error);
	else {
		CamelMimeMessage *message;
		message = class->get_message (folder, uid, error);
		if (message != NULL) {
			g_object_unref (message);
			success = TRUE;
		}
	}

	CAMEL_FOLDER_REC_UNLOCK (folder, lock);

	return success;
}

static GPtrArray *
get_uids (CamelFolder *folder)
{
	g_return_val_if_fail (folder->summary != NULL, g_ptr_array_new ());

	return camel_folder_summary_array (folder->summary);
}

/**
 * camel_folder_get_uids:
 * @folder: a #CamelFolder object
 *
 * Get the list of UIDs available in a folder. This routine is useful
 * for finding what messages are available when the folder does not
 * support summaries. The returned array should not be modified, and
 * must be freed by passing it to #camel_folder_free_uids.
 *
 * Returns: a GPtrArray of UIDs corresponding to the messages available
 * in the folder
 **/
GPtrArray *
camel_folder_get_uids (CamelFolder *folder)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_uids != NULL, NULL);

	return class->get_uids (folder);
}

static void
free_uids (CamelFolder *folder, GPtrArray *array)
{
	gint i;

	for (i=0; i<array->len; i++)
		camel_pstring_free (array->pdata[i]);
	g_ptr_array_free (array, TRUE);
}

/**
 * camel_folder_free_uids:
 * @folder: a #CamelFolder object
 * @array: the array of uids to free
 *
 * Frees the array of UIDs returned by #camel_folder_get_uids.
 **/
void
camel_folder_free_uids (CamelFolder *folder,
                        GPtrArray *array)
{
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (array != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->free_uids != NULL);

	class->free_uids (folder, array);
}

/**
 * Default: return the uids we are given.
 */
static GPtrArray *
get_uncached_uids (CamelFolder *folder,
                   GPtrArray * uids,
                   GError **error)
{
	GPtrArray *result;
	gint i;

	result = g_ptr_array_new ();

	g_ptr_array_set_size (result, uids->len);
	for (i = 0; i < uids->len; i++)
	    result->pdata[i] = (gchar *)camel_pstring_strdup (uids->pdata[i]);
	return result;
}

/**
 * camel_folder_get_uncached_uids:
 * @folder: a #CamelFolder object
 * @uids: the array of uids to filter down to uncached ones.
 *
 * Returns the known-uncached uids from a list of uids. It may return uids
 * which are locally cached but should never filter out a uid which is not
 * locally cached. Free the result by called #camel_folder_free_uids.
 * Frees the array of UIDs returned by #camel_folder_get_uids.
 *
 * Since: 2.26
 **/
GPtrArray *
camel_folder_get_uncached_uids (CamelFolder *folder,
                                GPtrArray *uids,
                                GError **error)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (uids != NULL, NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_uncached_uids != NULL, NULL);

	return class->get_uncached_uids (folder, uids, error);
}

static gint
cmp_uids (CamelFolder *folder, const gchar *uid1, const gchar *uid2)
{
	g_return_val_if_fail (uid1 != NULL, 0);
	g_return_val_if_fail (uid2 != NULL, 0);

	return strtoul (uid1, NULL, 10) - strtoul (uid2, NULL, 10);
}

/**
 * camel_folder_cmp_uids:
 * @folder: a #CamelFolder object
 * @uid1: The first uid.
 * @uid2: the second uid.
 *
 * Compares two uids. The return value meaning is the same as in any other compare function.
 *
 * Note that the default compare function expects a decimal number at the beginning of a uid,
 * thus if provider uses different uid values, then it should subclass this function.
 *
 * Since: 2.28
 **/
gint
camel_folder_cmp_uids (CamelFolder *folder,
                       const gchar *uid1,
                       const gchar *uid2)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);
	g_return_val_if_fail (uid1 != NULL, 0);
	g_return_val_if_fail (uid2 != NULL, 0);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->cmp_uids != NULL, 0);

	return class->cmp_uids (folder, uid1, uid2);
}

static gint
cmp_array_uids (gconstpointer a, gconstpointer b, gpointer user_data)
{
	const gchar *uid1 = *(const gchar **) a;
	const gchar *uid2 = *(const gchar **) b;
	CamelFolder *folder = user_data;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);

	return camel_folder_cmp_uids (folder, uid1, uid2);
}

static void
sort_uids (CamelFolder *folder, GPtrArray *uids)
{
	g_qsort_with_data (uids->pdata, uids->len, sizeof (gpointer), cmp_array_uids, folder);
}

/**
 * camel_folder_sort_uids:
 * @folder: a #CamelFolder object
 * @uids: array of uids
 *
 * Sorts the array of UIDs.
 *
 * Since: 2.24
 **/
void
camel_folder_sort_uids (CamelFolder *folder,
                        GPtrArray *uids)
{
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uids != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->sort_uids != NULL);

	class->sort_uids (folder, uids);
}

static GPtrArray *
get_summary (CamelFolder *folder)
{
	g_assert (folder->summary != NULL);

	return camel_folder_summary_array (folder->summary);
}

/**
 * camel_folder_get_summary:
 * @folder: a #CamelFolder object
 *
 * This returns the summary information for the folder. This array
 * should not be modified, and must be freed with
 * #camel_folder_free_summary.
 *
 * Returns: an array of #CamelMessageInfo
 **/
GPtrArray *
camel_folder_get_summary (CamelFolder *folder)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_summary != NULL, NULL);

	return class->get_summary (folder);
}

static void
free_summary (CamelFolder *folder, GPtrArray *summary)
{
	g_ptr_array_foreach (summary, (GFunc) camel_pstring_free, NULL);
	g_ptr_array_free (summary, TRUE);
}

/**
 * camel_folder_free_summary:
 * @folder: a #CamelFolder object
 * @array: the summary array to free
 *
 * Frees the summary array returned by #camel_folder_get_summary.
 **/
void
camel_folder_free_summary (CamelFolder *folder,
                           GPtrArray *array)
{
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (array != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->free_summary != NULL);

	class->free_summary (folder, array);
}

/**
 * camel_folder_has_search_capability:
 * @folder: a #CamelFolder object
 *
 * Checks if a folder supports searching.
 *
 * Returns: %TRUE if the folder supports searching or %FALSE otherwise
 **/
gboolean
camel_folder_has_search_capability (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	return folder->folder_flags & CAMEL_FOLDER_HAS_SEARCH_CAPABILITY;
}

/**
 * camel_folder_search_by_expression:
 * @folder: a #CamelFolder object
 * @expr: a search expression
 * @error: return location for a #GError, or %NULL
 *
 * Searches the folder for messages matching the given search expression.
 *
 * Returns: a #GPtrArray of uids of matching messages. The caller must
 * free the list and each of the elements when it is done.
 **/
GPtrArray *
camel_folder_search_by_expression (CamelFolder *folder,
                                   const gchar *expression,
                                   GError **error)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (folder->folder_flags & CAMEL_FOLDER_HAS_SEARCH_CAPABILITY, NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->search_by_expression != NULL, NULL);

	/* NOTE: that it is upto the callee to lock */

	return class->search_by_expression (folder, expression, error);
}

/**
 * camel_folder_count_by_expression:
 * @folder: a #CamelFolder object
 * @expr: a search expression
 * @error: return location for a #GError, or %NULL
 *
 * Searches the folder for count of messages matching the given search expression.
 *
 * Returns: an interger
 *
 * Since: 2.26
 **/
guint32
camel_folder_count_by_expression (CamelFolder *folder,
                                  const gchar *expression,
                                  GError **error)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);
	g_return_val_if_fail (folder->folder_flags & CAMEL_FOLDER_HAS_SEARCH_CAPABILITY, 0);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->count_by_expression != NULL, 0);

	/* NOTE: that it is upto the callee to lock */

	return class->count_by_expression (folder, expression, error);
}

/**
 * camel_folder_search_by_uids:
 * @folder: a #CamelFolder object
 * @expr: search expression
 * @uids: array of uid's to match against.
 * @error: return location for a #GError, or %NULL
 *
 * Search a subset of uid's for an expression match.
 *
 * Returns: a #GPtrArray of uids of matching messages. The caller must
 * free the list and each of the elements when it is done.
 **/
GPtrArray *
camel_folder_search_by_uids (CamelFolder *folder,
                             const gchar *expr,
                             GPtrArray *uids,
                             GError **error)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (folder->folder_flags & CAMEL_FOLDER_HAS_SEARCH_CAPABILITY, NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->search_by_uids != NULL, NULL);

	/* NOTE: that it is upto the callee to lock */

	return class->search_by_uids (folder, expr, uids, error);
}

static void
search_free (CamelFolder *folder, GPtrArray *result)
{
	gint i;

	for (i = 0; i < result->len; i++)
		camel_pstring_free (g_ptr_array_index (result, i));
	g_ptr_array_free (result, TRUE);
}

/**
 * camel_folder_search_free:
 * @folder: a #CamelFolder object
 * @result: search results to free
 *
 * Free the result of a search as gotten by #camel_folder_search or
 * #camel_folder_search_by_uids.
 **/
void
camel_folder_search_free (CamelFolder *folder,
                          GPtrArray *result)
{
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (result != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->search_free != NULL);

	/* NOTE: upto the callee to lock */

	class->search_free (folder, result);
}

static void
transfer_message_to (CamelFolder *source,
                     const gchar *uid,
                     CamelFolder *dest,
                     gchar **transferred_uid,
                     gboolean delete_original,
                     GError **error)
{
	CamelMimeMessage *msg;
	CamelMessageInfo *minfo, *info;
	GError *local_error = NULL;

	/* Default implementation. */

	msg = camel_folder_get_message (source, uid, error);
	if (!msg)
		return;

	/* if its deleted we poke the flags, so we need to copy the messageinfo */
	if ((source->folder_flags & CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY)
			&& (minfo = camel_folder_get_message_info (source, uid))) {
		info = camel_message_info_clone (minfo);
		camel_folder_free_message_info (source, minfo);
	} else {
		CamelMimePart *mime_part;
		GQueue *header_queue;

		mime_part = CAMEL_MIME_PART (msg);
		header_queue = camel_mime_part_get_raw_headers (mime_part);
		info = camel_message_info_new_from_header (NULL, header_queue);
	}

	/* we don't want to retain the deleted flag */
	camel_message_info_set_flags (info, CAMEL_MESSAGE_DELETED, 0);

	camel_folder_append_message (
		dest, msg, info, transferred_uid, &local_error);
	g_object_unref (msg);

	if (local_error != NULL)
		g_propagate_error (error, local_error);
	else if (delete_original)
		camel_folder_set_message_flags (
			source, uid, CAMEL_MESSAGE_DELETED |
			CAMEL_MESSAGE_SEEN, ~0);

	camel_message_info_free (info);
}

static gboolean
transfer_messages_to (CamelFolder *source,
                      GPtrArray *uids,
                      CamelFolder *dest,
                      GPtrArray **transferred_uids,
                      gboolean delete_originals,
                      GError **error)
{
	gchar **ret_uid = NULL;
	gint i;
	GError *local_error = NULL;

	if (transferred_uids) {
		*transferred_uids = g_ptr_array_new ();
		g_ptr_array_set_size (*transferred_uids, uids->len);
	}

	if (delete_originals)
		camel_operation_start (NULL, _("Moving messages"));
	else
		camel_operation_start (NULL, _("Copying messages"));

	if (uids->len > 1) {
		camel_folder_freeze (dest);
		if (delete_originals)
			camel_folder_freeze (source);
	}

	for (i = 0; i < uids->len && local_error != NULL; i++) {
		if (transferred_uids)
			ret_uid = (gchar **)&((*transferred_uids)->pdata[i]);
		transfer_message_to (
			source, uids->pdata[i], dest, ret_uid,
			delete_originals, &local_error);
		camel_operation_progress (NULL, i * 100 / uids->len);
	}

	if (uids->len > 1) {
		camel_folder_thaw (dest);
		if (delete_originals)
			camel_folder_thaw (source);
	}

	camel_operation_end (NULL);

	if (local_error != NULL)
		g_propagate_error (error, local_error);

	return TRUE;
}

/**
 * camel_folder_transfer_messages_to:
 * @source: the source #CamelFolder object
 * @uids: message UIDs in @source
 * @dest: the destination #CamelFolder object
 * @transferred_uids: if non-%NULL, the UIDs of the resulting messages
 * in @dest will be stored here, if known.
 * @delete_originals: whether or not to delete the original messages
 * @error: return location for a #GError, or %NULL
 *
 * This copies or moves messages from one folder to another. If the
 * @source and @dest folders have the same parent_store, this may be
 * more efficient than using #camel_folder_append_message.
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
camel_folder_transfer_messages_to (CamelFolder *source,
                                   GPtrArray *uids,
                                   CamelFolder *dest,
                                   GPtrArray **transferred_uids,
                                   gboolean delete_originals,
                                   GError **error)
{
	CamelFolderClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_FOLDER (source), FALSE);
	g_return_val_if_fail (uids != NULL, FALSE);
	g_return_val_if_fail (CAMEL_IS_FOLDER (dest), FALSE);

	if (source == dest || uids->len == 0) {
		/* source and destination folders are the same, or no work to do, do nothing. */
		return TRUE;
	}

	if (source->parent_store == dest->parent_store) {
		/* If either folder is a vtrash, we need to use the
		 * vtrash transfer method.
		 */
		if (CAMEL_IS_VTRASH_FOLDER (dest))
			class = CAMEL_FOLDER_GET_CLASS (dest);
		else
			class = CAMEL_FOLDER_GET_CLASS (source);
		success = class->transfer_messages_to (
			source, uids, dest, transferred_uids,
			delete_originals, error);
	} else
		success = transfer_messages_to (
			source, uids, dest, transferred_uids,
			delete_originals, error);

	return success;
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
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->delete != NULL);

	CAMEL_FOLDER_REC_LOCK (folder, lock);
	if (folder->folder_flags & CAMEL_FOLDER_HAS_BEEN_DELETED) {
		CAMEL_FOLDER_REC_UNLOCK (folder, lock);
		return;
	}

	folder->folder_flags |= CAMEL_FOLDER_HAS_BEEN_DELETED;

	class->delete (folder);

	CAMEL_FOLDER_REC_UNLOCK (folder, lock);

	/* Delete the references of the folder from the DB.*/
	camel_db_delete_folder (folder->parent_store->cdb_w, folder->full_name, NULL);

	camel_object_trigger_event (folder, "deleted", NULL);
}

static void
folder_rename (CamelFolder *folder, const gchar *new)
{
	gchar *tmp;

	d (printf ("CamelFolder:rename ('%s')\n", new));

	g_free (folder->full_name);
	folder->full_name = g_strdup (new);
	g_free (folder->name);
	tmp = strrchr (new, '/');
	folder->name = g_strdup (tmp?tmp+1:new);
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
camel_folder_rename (CamelFolder *folder,
                     const gchar *new)
{
	CamelFolderClass *class;
	gchar *old;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (new != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->rename != NULL);

	old = g_strdup (folder->full_name);

	class->rename (folder, new);
	camel_db_rename_folder (folder->parent_store->cdb_w, old, new, NULL);
	camel_object_trigger_event (folder, "renamed", old);

	g_free (old);
}

static void
freeze (CamelFolder *folder)
{
	CAMEL_FOLDER_LOCK (folder, change_lock);

	g_assert (folder->priv->frozen >= 0);

	folder->priv->frozen++;

	d (printf ("freeze (%p '%s') = %d\n", folder, folder->full_name, folder->priv->frozen));
	CAMEL_FOLDER_UNLOCK (folder, change_lock);
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
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->freeze != NULL);

	class->freeze (folder);
}

static void
thaw (CamelFolder * folder)
{
	CamelFolderChangeInfo *info = NULL;

	CAMEL_FOLDER_LOCK (folder, change_lock);

	g_assert (folder->priv->frozen > 0);

	folder->priv->frozen--;

	d (printf ("thaw (%p '%s') = %d\n", folder, folder->full_name, folder->priv->frozen));

	if (folder->priv->frozen == 0
	    && camel_folder_change_info_changed (folder->priv->changed_frozen)) {
		info = folder->priv->changed_frozen;
		folder->priv->changed_frozen = camel_folder_change_info_new ();
	}

	CAMEL_FOLDER_UNLOCK (folder, change_lock);

	if (info) {
		camel_object_trigger_event (folder, "folder_changed", info);
		camel_folder_change_info_free (info);
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
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (folder->priv->frozen != 0);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->thaw != NULL);

	class->thaw (folder);
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
 * Returns: whether or not the folder is frozen
 **/
gboolean
camel_folder_is_frozen (CamelFolder *folder)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->is_frozen != NULL, FALSE);

	return class->is_frozen (folder);
}

static CamelFolderQuotaInfo *
get_quota_info (CamelFolder *folder)
{
	return NULL;
}

/**
 * camel_folder_get_quota_info:
 * @folder: a #CamelFolder object
 *
 * Returns: list of known quota(s) for the folder.
 *
 * Since: 2.24
 **/
CamelFolderQuotaInfo *
camel_folder_get_quota_info (CamelFolder *folder)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_quota_info != NULL, NULL);

	return class->get_quota_info (folder);
}

/**
 * camel_folder_quota_info_new:
 * @name: Name of the quota.
 * @used: Current usage of the quota.
 * @total: Total available size of the quota.
 *
 * Returns: newly allocated #CamelFolderQuotaInfo structure with
 * initialized values based on the parameters, with next member set to NULL.
 *
 * Since: 2.24
 **/
CamelFolderQuotaInfo *
camel_folder_quota_info_new (const gchar *name, guint64 used, guint64 total)
{
	CamelFolderQuotaInfo *info;

	info = g_malloc0 (sizeof (CamelFolderQuotaInfo));
	info->name = g_strdup (name);
	info->used = used;
	info->total = total;
	info->next = NULL;

	return info;
}

/**
 * camel_folder_quota_info_clone:
 * @info: a #CamelFolderQuotaInfo object to clone.
 *
 * Makes a copy of the given info and all next-s.
 *
 * Since: 2.24
 **/
CamelFolderQuotaInfo *
camel_folder_quota_info_clone (const CamelFolderQuotaInfo *info)
{
	CamelFolderQuotaInfo *clone = NULL, *last = NULL;
	const CamelFolderQuotaInfo *iter;

	for (iter = info; iter != NULL; iter = iter->next) {
		CamelFolderQuotaInfo *n = camel_folder_quota_info_new (iter->name, iter->used, iter->total);

		if (last)
			last->next = n;
		else
			clone = n;

		last = n;
	}

	return clone;
}

/**
 * camel_folder_quota_info_free:
 * @info: a #CamelFolderQuotaInfo object to free.
 *
 * Frees this and all next objects.
 *
 * Since: 2.24
 **/
void
camel_folder_quota_info_free (CamelFolderQuotaInfo *info)
{
	CamelFolderQuotaInfo *next = info;

	while (next) {
		info = next;
		next = next->next;

		g_free (info->name);
		g_free (info);
	}
}

struct _folder_filter_msg {
	CamelSessionThreadMsg msg;

	GPtrArray *recents;
	GPtrArray *junk;
	GPtrArray *notjunk;
	CamelFolder *folder;
	CamelFilterDriver *driver;
	GError *error;
};

static void
filter_filter (CamelSession *session, CamelSessionThreadMsg *tmsg)
{
	struct _folder_filter_msg *m = (struct _folder_filter_msg *) tmsg;
	CamelMessageInfo *info;
	gint i, status = 0;
	CamelURL *uri;
	gchar *source_url;
	CamelJunkPlugin *csp = ((CamelService *)m->folder->parent_store)->session->junk_plugin;
	GError *local_error = NULL;

	if (m->junk) {
		camel_operation_start (NULL, ngettext ("Learning new spam message", "Learning new spam messages", m->junk->len));

		for (i = 0; i < m->junk->len; i ++) {
			CamelMimeMessage *msg = camel_folder_get_message (m->folder, m->junk->pdata[i], NULL);
			gint pc = 100 * i / m->junk->len;

			camel_operation_progress (NULL, pc);

			if (msg) {
				camel_junk_plugin_report_junk (csp, msg);
				g_object_unref (msg);
			}
		}
		camel_operation_end (NULL);
	}

	if (m->notjunk) {
		camel_operation_start (NULL, ngettext ("Learning new ham message", "Learning new ham messages", m->notjunk->len));
		for (i = 0; i < m->notjunk->len; i ++) {
			CamelMimeMessage *msg = camel_folder_get_message (m->folder, m->notjunk->pdata[i], NULL);
			gint pc = 100 * i / m->notjunk->len;

			camel_operation_progress (NULL, pc);

			if (msg) {
				camel_junk_plugin_report_notjunk (csp, msg);
				g_object_unref (msg);
			}
		}
		camel_operation_end (NULL);
	}

	if (m->junk || m->notjunk)
		camel_junk_plugin_commit_reports (csp);

	if (m->driver && m->recents) {
		camel_operation_start (NULL, ngettext ("Filtering new message", "Filtering new messages", m->recents->len));

		source_url = camel_service_get_url ((CamelService *)m->folder->parent_store);
		uri = camel_url_new (source_url, NULL);
		g_free (source_url);
		if (m->folder->full_name && m->folder->full_name[0] != '/') {
			gchar *tmp = alloca (strlen (m->folder->full_name)+2);

			sprintf (tmp, "/%s", m->folder->full_name);
			camel_url_set_path (uri, tmp);
		} else
			camel_url_set_path (uri, m->folder->full_name);
		source_url = camel_url_to_string (uri, CAMEL_URL_HIDE_ALL);
		camel_url_free (uri);

		for (i=0;status == 0 && i<m->recents->len;i++) {
			gchar *uid = m->recents->pdata[i];
			gint pc = 100 * i / m->recents->len;

			camel_operation_progress (NULL, pc);

			info = camel_folder_get_message_info (m->folder, uid);
			if (info == NULL) {
				g_warning ("uid %s vanished from folder: %s", uid, source_url);
				continue;
			}

			status = camel_filter_driver_filter_message (m->driver, NULL, info, uid, m->folder, source_url, source_url, &local_error);

			camel_folder_free_message_info (m->folder, info);
		}

		if (local_error == NULL)
			camel_filter_driver_flush (m->driver, &local_error);
		else
			camel_filter_driver_flush (m->driver, NULL);

		if (local_error != NULL)
			g_propagate_error (&m->error, local_error);

		g_free (source_url);

		camel_operation_end (NULL);
	}
}

static void
filter_free (CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _folder_filter_msg *m = (struct _folder_filter_msg *)msg;

	if (m->driver)
		g_object_unref (m->driver);
	if (m->recents)
		camel_folder_free_deep (m->folder, m->recents);
	if (m->junk)
		camel_folder_free_deep (m->folder, m->junk);
	if (m->notjunk)
		camel_folder_free_deep (m->folder, m->notjunk);

	camel_folder_summary_save_to_db (m->folder->summary, &m->error);
	camel_folder_thaw (m->folder);
	g_object_unref (m->folder);
}

static CamelSessionThreadOps filter_ops = {
	filter_filter,
	filter_free
};

struct _CamelFolderChangeInfoPrivate {
	GHashTable *uid_stored;	/* what we have stored, which array they're in */
	GHashTable *uid_source;	/* used to create unique lists */
	GPtrArray  *uid_filter; /* uids to be filtered */
	CamelMemPool *uid_pool;	/* pool used to store copies of uid strings */
};

/* Event hooks that block emission when frozen */
static gboolean
folder_changed (CamelObject *obj, gpointer event_data)
{
	CamelFolder *folder = (CamelFolder *)obj;
	CamelFolderChangeInfo *changed = event_data;
	struct _CamelFolderChangeInfoPrivate *p = changed->priv;
	CamelSession *session = ((CamelService *)folder->parent_store)->session;
	CamelFilterDriver *driver = NULL;
	GPtrArray *junk = NULL;
	GPtrArray *notjunk = NULL;
	GPtrArray *recents = NULL;
	gint i;

	d (printf ("folder_changed (%p:'%s', %p), frozen=%d\n", obj, folder->full_name, event_data, folder->priv->frozen));
	d (printf (" added %d removed %d changed %d recent %d filter %d\n",
		 changed->uid_added->len, changed->uid_removed->len,
		 changed->uid_changed->len, changed->uid_recent->len,
		 p->uid_filter->len));

	if (changed == NULL) {
		w (g_warning ("Class %s is passing NULL to folder_changed event",
			     G_OBJECT_CLASS_NAME (G_OBJECT_TYPE (folder))));
		return TRUE;
	}

	CAMEL_FOLDER_LOCK (folder, change_lock);
	if (folder->priv->frozen) {
		camel_folder_change_info_cat (folder->priv->changed_frozen, changed);
		CAMEL_FOLDER_UNLOCK (folder, change_lock);

		return FALSE;
	}
	CAMEL_FOLDER_UNLOCK (folder, change_lock);

	if (session->junk_plugin && changed->uid_changed->len) {
		guint32 flags;

		for (i = 0; i < changed->uid_changed->len; i++) {
			flags = camel_folder_get_message_flags (folder, changed->uid_changed->pdata [i]);
			if (flags & CAMEL_MESSAGE_JUNK_LEARN) {
				if (flags & CAMEL_MESSAGE_JUNK) {
					if (!junk)
						junk = g_ptr_array_new ();
					g_ptr_array_add (junk, g_strdup (changed->uid_changed->pdata [i]));
				} else {
					if (!notjunk)
						notjunk = g_ptr_array_new ();
					g_ptr_array_add (notjunk, g_strdup (changed->uid_changed->pdata [i]));
				}
				/* reset junk learn flag so that we don't process it again*/
				camel_folder_set_message_flags (folder, changed->uid_changed->pdata [i], CAMEL_MESSAGE_JUNK_LEARN, 0);
			}
		}
	}

	if ((folder->folder_flags & (CAMEL_FOLDER_FILTER_RECENT|CAMEL_FOLDER_FILTER_JUNK))
	    && p->uid_filter->len > 0)
		driver = camel_session_get_filter_driver (session,
							 (folder->folder_flags & CAMEL_FOLDER_FILTER_RECENT)
							 ? "incoming":"junktest", NULL);

	if (driver) {
		recents = g_ptr_array_new ();
		for (i = 0; i < p->uid_filter->len; i++)
			g_ptr_array_add (recents, g_strdup (p->uid_filter->pdata[i]));

		g_ptr_array_set_size (p->uid_filter, 0);
	}

	if (driver || junk || notjunk) {
		struct _folder_filter_msg *msg;

		d (printf ("* launching filter thread %d new mail, %d junk and %d not junk\n",
			 recents?recents->len:0, junk?junk->len:0, notjunk?notjunk->len:0));

		msg = camel_session_thread_msg_new (session, &filter_ops, sizeof (*msg));
		msg->recents = recents;
		msg->junk = junk;
		msg->notjunk = notjunk;
		msg->folder = folder;
		g_object_ref (folder);
		camel_folder_freeze (folder);
		/* Copy changes back to changed_frozen list to retain
		 * them while we are filtering */
		CAMEL_FOLDER_LOCK (folder, change_lock);
		camel_folder_change_info_cat (folder->priv->changed_frozen, changed);
		CAMEL_FOLDER_UNLOCK (folder, change_lock);
		msg->driver = driver;
		msg->error = NULL;
		camel_session_thread_queue (session, &msg->msg, 0);
		return FALSE;
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
	gint i;

	for (i = 0; i < array->len; i++)
		g_free (array->pdata[i]);
	g_ptr_array_free (array, TRUE);
}

/**
 * camel_folder_change_info_new:
 *
 * Create a new folder change info structure.
 *
 * Change info structures are not MT-SAFE and must be
 * locked for exclusive access externally.
 *
 * Returns: a new #CamelFolderChangeInfo
 **/
CamelFolderChangeInfo *
camel_folder_change_info_new (void)
{
	CamelFolderChangeInfo *info;

	info = g_slice_new (CamelFolderChangeInfo);
	info->uid_added = g_ptr_array_new ();
	info->uid_removed = g_ptr_array_new ();
	info->uid_changed = g_ptr_array_new ();
	info->uid_recent = g_ptr_array_new ();
	info->priv = g_slice_new (struct _CamelFolderChangeInfoPrivate);
	info->priv->uid_stored = g_hash_table_new (g_str_hash, g_str_equal);
	info->priv->uid_source = NULL;
	info->priv->uid_filter = g_ptr_array_new ();
	info->priv->uid_pool = camel_mempool_new (512, 256, CAMEL_MEMPOOL_ALIGN_BYTE);

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
camel_folder_change_info_add_source (CamelFolderChangeInfo *info, const gchar *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;

	g_assert (info != NULL);

	p = info->priv;

	if (p->uid_source == NULL)
		p->uid_source = g_hash_table_new (g_str_hash, g_str_equal);

	if (g_hash_table_lookup (p->uid_source, uid) == NULL)
		g_hash_table_insert (p->uid_source, camel_mempool_strdup (p->uid_pool, uid), GINT_TO_POINTER (1));
}

/**
 * camel_folder_change_info_add_source_list:
 * @info: a #CamelFolderChangeInfo
 * @list: a list of uids
 *
 * Add a list of source uid's for generating a changeset.
 **/
void
camel_folder_change_info_add_source_list (CamelFolderChangeInfo *info, const GPtrArray *list)
{
	struct _CamelFolderChangeInfoPrivate *p;
	gint i;

	g_assert (info != NULL);
	g_assert (list != NULL);

	p = info->priv;

	if (p->uid_source == NULL)
		p->uid_source = g_hash_table_new (g_str_hash, g_str_equal);

	for (i=0;i<list->len;i++) {
		gchar *uid = list->pdata[i];

		if (g_hash_table_lookup (p->uid_source, uid) == NULL)
			g_hash_table_insert (p->uid_source, camel_mempool_strdup (p->uid_pool, uid), GINT_TO_POINTER (1));
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
camel_folder_change_info_add_update (CamelFolderChangeInfo *info, const gchar *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	gchar *key;
	gint value;

	g_assert (info != NULL);

	p = info->priv;

	if (p->uid_source == NULL) {
		camel_folder_change_info_add_uid (info, uid);
		return;
	}

	if (g_hash_table_lookup_extended (p->uid_source, uid, (gpointer) &key, (gpointer) &value)) {
		g_hash_table_remove (p->uid_source, key);
	} else {
		camel_folder_change_info_add_uid (info, uid);
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
camel_folder_change_info_add_update_list (CamelFolderChangeInfo *info, const GPtrArray *list)
{
	gint i;

	g_assert (info != NULL);
	g_assert (list != NULL);

	for (i=0;i<list->len;i++)
		camel_folder_change_info_add_update (info, list->pdata[i]);
}

static void
change_info_remove (gchar *key, gpointer value, CamelFolderChangeInfo *info)
{
	struct _CamelFolderChangeInfoPrivate *p = info->priv;
	GPtrArray *olduids;
	gchar *olduid;

	if (g_hash_table_lookup_extended (p->uid_stored, key, (gpointer) &olduid, (gpointer) &olduids)) {
		/* if it was added/changed them removed, then remove it */
		if (olduids != info->uid_removed) {
			g_ptr_array_remove_fast (olduids, olduid);
			g_ptr_array_add (info->uid_removed, olduid);
			g_hash_table_insert (p->uid_stored, olduid, info->uid_removed);
		}
		return;
	}

	/* we dont need to copy this, as they've already been copied into our pool */
	g_ptr_array_add (info->uid_removed, key);
	g_hash_table_insert (p->uid_stored, key, info->uid_removed);
}

/**
 * camel_folder_change_info_build_diff:
 * @info: a #CamelFolderChangeInfo
 *
 * Compare the source uid set to the updated uid set and generate the
 * differences into the added and removed lists.
 **/
void
camel_folder_change_info_build_diff (CamelFolderChangeInfo *info)
{
	struct _CamelFolderChangeInfoPrivate *p;

	g_assert (info != NULL);

	p = info->priv;

	if (p->uid_source) {
		g_hash_table_foreach (p->uid_source, (GHFunc)change_info_remove, info);
		g_hash_table_destroy (p->uid_source);
		p->uid_source = NULL;
	}
}

static void
change_info_recent_uid (CamelFolderChangeInfo *info, const gchar *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	GPtrArray *olduids;
	gchar *olduid;

	p = info->priv;

	/* always add to recent, but dont let anyone else know */
	if (!g_hash_table_lookup_extended (p->uid_stored, uid, (gpointer *)&olduid, (gpointer *)&olduids)) {
		olduid = camel_mempool_strdup (p->uid_pool, uid);
	}
	g_ptr_array_add (info->uid_recent, olduid);
}

static void
change_info_filter_uid (CamelFolderChangeInfo *info, const gchar *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	GPtrArray *olduids;
	gchar *olduid;

	p = info->priv;

	/* always add to filter, but dont let anyone else know */
	if (!g_hash_table_lookup_extended (p->uid_stored, uid, (gpointer *)&olduid, (gpointer *)&olduids)) {
		olduid = camel_mempool_strdup (p->uid_pool, uid);
	}
	g_ptr_array_add (p->uid_filter, olduid);
}

static void
change_info_cat (CamelFolderChangeInfo *info, GPtrArray *source, void (*add)(CamelFolderChangeInfo *info, const gchar *uid))
{
	gint i;

	for (i=0;i<source->len;i++)
		add (info, source->pdata[i]);
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
camel_folder_change_info_cat (CamelFolderChangeInfo *info, CamelFolderChangeInfo *source)
{
	g_assert (info != NULL);
	g_assert (source != NULL);

	change_info_cat (info, source->uid_added, camel_folder_change_info_add_uid);
	change_info_cat (info, source->uid_removed, camel_folder_change_info_remove_uid);
	change_info_cat (info, source->uid_changed, camel_folder_change_info_change_uid);
	change_info_cat (info, source->uid_recent, change_info_recent_uid);
	change_info_cat (info, source->priv->uid_filter, change_info_filter_uid);
}

/**
 * camel_folder_change_info_add_uid:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 *
 * Add a new uid to the changeinfo.
 **/
void
camel_folder_change_info_add_uid (CamelFolderChangeInfo *info, const gchar *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	GPtrArray *olduids;
	gchar *olduid;

	g_assert (info != NULL);

	p = info->priv;

	if (g_hash_table_lookup_extended (p->uid_stored, uid, (gpointer) &olduid, (gpointer) &olduids)) {
		/* if it was removed then added, promote it to a changed */
		/* if it was changed then added, leave as changed */
		if (olduids == info->uid_removed) {
			g_ptr_array_remove_fast (olduids, olduid);
			g_ptr_array_add (info->uid_changed, olduid);
			g_hash_table_insert (p->uid_stored, olduid, info->uid_changed);
		}
		return;
	}

	olduid = camel_mempool_strdup (p->uid_pool, uid);
	g_ptr_array_add (info->uid_added, olduid);
	g_hash_table_insert (p->uid_stored, olduid, info->uid_added);
}

/**
 * camel_folder_change_info_remove_uid:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 *
 * Add a uid to the removed uid list.
 **/
void
camel_folder_change_info_remove_uid (CamelFolderChangeInfo *info, const gchar *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	GPtrArray *olduids;
	gchar *olduid;

	g_assert (info != NULL);

	p = info->priv;

	if (g_hash_table_lookup_extended (p->uid_stored, uid, (gpointer) &olduid, (gpointer) &olduids)) {
		/* if it was added/changed them removed, then remove it */
		if (olduids != info->uid_removed) {
			g_ptr_array_remove_fast (olduids, olduid);
			g_ptr_array_add (info->uid_removed, olduid);
			g_hash_table_insert (p->uid_stored, olduid, info->uid_removed);
		}
		return;
	}

	olduid = camel_mempool_strdup (p->uid_pool, uid);
	g_ptr_array_add (info->uid_removed, olduid);
	g_hash_table_insert (p->uid_stored, olduid, info->uid_removed);
}

/**
 * camel_folder_change_info_change_uid:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 *
 * Add a uid to the changed uid list.
 **/
void
camel_folder_change_info_change_uid (CamelFolderChangeInfo *info, const gchar *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	GPtrArray *olduids;
	gchar *olduid;

	g_assert (info != NULL);

	p = info->priv;

	if (g_hash_table_lookup_extended (p->uid_stored, uid, (gpointer) &olduid, (gpointer) &olduids)) {
		/* if we have it already, leave it as that */
		return;
	}

	olduid = camel_mempool_strdup (p->uid_pool, uid);
	g_ptr_array_add (info->uid_changed, olduid);
	g_hash_table_insert (p->uid_stored, olduid, info->uid_changed);
}

/**
 * camel_folder_change_info_recent_uid:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 *
 * Add a recent uid to the changedinfo.
 * This will also add the uid to the uid_filter array for potential
 * filtering
 **/
void
camel_folder_change_info_recent_uid (CamelFolderChangeInfo *info, const gchar *uid)
{
	g_assert (info != NULL);

	change_info_recent_uid (info, uid);
	change_info_filter_uid (info, uid);
}

/**
 * camel_folder_change_info_changed:
 * @info: a #CamelFolderChangeInfo
 *
 * Gets whether or not there have been any changes.
 *
 * Returns: %TRUE if the changeset contains any changes or %FALSE
 * otherwise
 **/
gboolean
camel_folder_change_info_changed (CamelFolderChangeInfo *info)
{
	g_assert (info != NULL);

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
camel_folder_change_info_clear (CamelFolderChangeInfo *info)
{
	struct _CamelFolderChangeInfoPrivate *p;

	g_assert (info != NULL);

	p = info->priv;

	g_ptr_array_set_size (info->uid_added, 0);
	g_ptr_array_set_size (info->uid_removed, 0);
	g_ptr_array_set_size (info->uid_changed, 0);
	g_ptr_array_set_size (info->uid_recent, 0);
	if (p->uid_source) {
		g_hash_table_destroy (p->uid_source);
		p->uid_source = NULL;
	}
	g_hash_table_destroy (p->uid_stored);
	p->uid_stored = g_hash_table_new (g_str_hash, g_str_equal);
	g_ptr_array_set_size (p->uid_filter, 0);
	camel_mempool_flush (p->uid_pool, TRUE);
}

/**
 * camel_folder_change_info_free:
 * @info: a #CamelFolderChangeInfo
 *
 * Free memory associated with the folder change info lists.
 **/
void
camel_folder_change_info_free (CamelFolderChangeInfo *info)
{
	struct _CamelFolderChangeInfoPrivate *p;

	g_assert (info != NULL);

	p = info->priv;

	if (p->uid_source)
		g_hash_table_destroy (p->uid_source);

	g_hash_table_destroy (p->uid_stored);
	g_ptr_array_free (p->uid_filter, TRUE);
	camel_mempool_destroy (p->uid_pool);
	g_slice_free (struct _CamelFolderChangeInfoPrivate, p);

	g_ptr_array_free (info->uid_added, TRUE);
	g_ptr_array_free (info->uid_removed, TRUE);
	g_ptr_array_free (info->uid_changed, TRUE);
	g_ptr_array_free (info->uid_recent, TRUE);
	g_slice_free (CamelFolderChangeInfo, info);
}
