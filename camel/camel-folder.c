/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/* camel-folder.c: Abstract class for an email folder */

/*
 * Author:
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright 1999, 2000 Helix Code, Inc. (http://www.helixcode.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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

#include <config.h>
#include <string.h>
#include "camel-folder.h"
#include "camel-exception.h"
#include "camel-store.h"
#include "camel-mime-message.h"
#include "string-utils.h"

static CamelObjectClass *parent_class = NULL;

/* Returns the class for a CamelFolder */
#define CF_CLASS(so) CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))


static void init (CamelFolder *folder, CamelStore *parent_store,
		  CamelFolder *parent_folder, const gchar *name,
		  gchar *separator, gboolean path_begins_with_sep,
		  CamelException *ex);

static void camel_folder_finalize (CamelObject *object);


static void folder_sync (CamelFolder *folder, gboolean expunge,
			 CamelException *ex);

static const gchar *get_name (CamelFolder *folder);
static const gchar *get_full_name (CamelFolder *folder);


static gboolean can_hold_folders (CamelFolder *folder);
static gboolean can_hold_messages (CamelFolder *folder);
static guint32 get_permanent_flags (CamelFolder *folder);
static guint32 get_message_flags (CamelFolder *folder, const char *uid);
static void set_message_flags (CamelFolder *folder, const char *uid,
			       guint32 flags, guint32 set);
static gboolean get_message_user_flag (CamelFolder *folder, const char *uid,
				       const char *name);
static void set_message_user_flag (CamelFolder *folder, const char *uid,
				   const char *name, gboolean value);


static GPtrArray *get_subfolder_names (CamelFolder *folder);
static void      free_subfolder_names (CamelFolder *folder,
				       GPtrArray *array);
static CamelFolder *get_subfolder     (CamelFolder *folder,
				       const gchar *folder_name,
				       gboolean create,
				       CamelException *ex);
static CamelFolder *get_parent_folder (CamelFolder *folder);
static CamelStore *get_parent_store   (CamelFolder *folder);

static gint get_message_count (CamelFolder *folder);
static gint get_unread_message_count (CamelFolder *folder);

static void expunge             (CamelFolder *folder,
				 CamelException *ex);


static void append_message (CamelFolder *folder, CamelMimeMessage *message,
			    const CamelMessageInfo *info, CamelException *ex);


static GPtrArray        *get_uids            (CamelFolder *folder);
static void              free_uids           (CamelFolder *folder,
					      GPtrArray *array);
static GPtrArray        *get_summary         (CamelFolder *folder);
static void              free_summary        (CamelFolder *folder,
					      GPtrArray *array);

static const gchar      *get_message_uid     (CamelFolder *folder,
					      CamelMimeMessage *message);

static CamelMimeMessage *get_message         (CamelFolder *folder,
					      const gchar *uid,
					      CamelException *ex);

static const CamelMessageInfo *get_message_info (CamelFolder *folder,
						 const char *uid);

static GPtrArray      *search_by_expression  (CamelFolder *folder,
					      const char *exp,
					      CamelException *ex);
static void            search_free           (CamelFolder * folder, 
					      GPtrArray * result);

static void            copy_message_to       (CamelFolder *source,
					      const char *uid,
					      CamelFolder *dest,
					      CamelException *ex);

static void            move_message_to       (CamelFolder *source,
					      const char *uid,
					      CamelFolder *dest,
					      CamelException *ex);

static void            freeze                (CamelFolder *folder);
static void            thaw                  (CamelFolder *folder);

static gboolean        folder_changed        (CamelObject *object,
					      /*int type*/gpointer event_data);
static gboolean        message_changed       (CamelObject *object,
					      /*const char *uid*/gpointer event_data);

static void
camel_folder_class_init (CamelFolderClass *camel_folder_class)
{
	CamelObjectClass *camel_object_class =
		CAMEL_OBJECT_CLASS (camel_folder_class);

	parent_class = camel_type_get_global_classfuncs (camel_object_get_type ());

	/* virtual method definition */
	camel_folder_class->init = init;
	camel_folder_class->sync = folder_sync;
	camel_folder_class->get_name = get_name;
	camel_folder_class->get_full_name = get_full_name;
	camel_folder_class->can_hold_folders = can_hold_folders;
	camel_folder_class->can_hold_messages = can_hold_messages;
	camel_folder_class->get_subfolder = get_subfolder;
	camel_folder_class->get_parent_folder = get_parent_folder;
	camel_folder_class->get_parent_store = get_parent_store;
	camel_folder_class->get_subfolder_names = get_subfolder_names;
	camel_folder_class->free_subfolder_names = free_subfolder_names;
	camel_folder_class->expunge = expunge;
	camel_folder_class->get_message_count = get_message_count;
	camel_folder_class->get_unread_message_count = get_unread_message_count;
	camel_folder_class->append_message = append_message;
	camel_folder_class->get_permanent_flags = get_permanent_flags;
	camel_folder_class->get_message_flags = get_message_flags;
	camel_folder_class->set_message_flags = set_message_flags;
	camel_folder_class->get_message_user_flag = get_message_user_flag;
	camel_folder_class->set_message_user_flag = set_message_user_flag;
	camel_folder_class->get_message = get_message;
	camel_folder_class->get_uids = get_uids;
	camel_folder_class->free_uids = free_uids;
	camel_folder_class->get_summary = get_summary;
	camel_folder_class->free_summary = free_summary;
	camel_folder_class->search_by_expression = search_by_expression;
	camel_folder_class->search_free = search_free;
	camel_folder_class->get_message_info = get_message_info;
	camel_folder_class->copy_message_to = copy_message_to;
	camel_folder_class->move_message_to = move_message_to;
	camel_folder_class->freeze = freeze;
	camel_folder_class->thaw = thaw;

	/* virtual method overload */
	camel_object_class_declare_event (camel_object_class, "folder_changed", folder_changed);
	camel_object_class_declare_event (camel_object_class, "message_changed", message_changed);

	/*
        signals[FOLDER_CHANGED] =
                gt_k_signal_new ("folder_changed",
                                GT_K_RUN_FIRST,
                                camel_object_class->type,
                                GT_K_SIGNAL_OFFSET (CamelFolderClass,
				folder_changed),
                                gt_k_marshal_NONE__INT,
                                GT_K_TYPE_NONE, 1, GT_K_TYPE_INT);

        signals[MESSAGE_CHANGED] =
                gt_k_signal_new ("message_changed",
                                GT_K_RUN_FIRST,
                                camel_object_class->type,
                                GT_K_SIGNAL_OFFSET (CamelFolderClass,
						   message_changed),
                                gt_k_marshal_NONE__STRING,
                                GT_K_TYPE_NONE, 1, GT_K_TYPE_STRING);

        camel_object_class_add_signals (camel_object_class, signals, LAST_SIGNAL);
	*/
}

static void
camel_folder_finalize (CamelObject *object)
{
	CamelFolder *camel_folder = CAMEL_FOLDER (object);
	GList *m;

	g_free (camel_folder->name);
	g_free (camel_folder->full_name);

	if (camel_folder->parent_store)
		camel_object_unref (CAMEL_OBJECT (camel_folder->parent_store));
	if (camel_folder->parent_folder)
		camel_object_unref (CAMEL_OBJECT (camel_folder->parent_folder));

	for (m = camel_folder->messages_changed; m; m = m->next)
		g_free (m->data);
	g_list_free (camel_folder->messages_changed);
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
							 NULL,
							 (CamelObjectFinalizeFunc) camel_folder_finalize );
	}

	return camel_folder_type;
}


/**
 * init: init the folder
 * @folder: folder object to initialize
 * @parent_store: parent store object of the folder
 * @parent_folder: parent folder of the folder (may be NULL)
 * @name: (short) name of the folder
 * @separator: separator between the parent folder name and this name
 * @ex: a CamelException
 *
 * Initalizes the folder by setting the parent store, parent folder,
 * and name.
 **/
static void
init (CamelFolder *folder, CamelStore *parent_store,
      CamelFolder *parent_folder, const gchar *name,
      gchar *separator, gboolean path_begins_with_sep,
      CamelException *ex)
{
	gchar *full_name;
	const gchar *parent_full_name;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (CAMEL_IS_STORE (parent_store));
	g_return_if_fail (parent_folder == NULL || CAMEL_IS_FOLDER (parent_folder));
	g_return_if_fail (folder->parent_store == NULL);

	folder->parent_store = parent_store;
	camel_object_ref (CAMEL_OBJECT (parent_store));

	folder->parent_folder = parent_folder;
	if (parent_folder)
		camel_object_ref (CAMEL_OBJECT (parent_folder));

	folder->separator = separator;
	folder->path_begins_with_sep = path_begins_with_sep;

	/* if the folder already has a name, free it */
	g_free (folder->name);
	g_free (folder->full_name);

	/* set those fields to NULL now, so that if an
	   exception occurs, they will be set anyway */
	folder->name = NULL;
	folder->full_name = NULL;

	if (folder->parent_folder) {
		parent_full_name = camel_folder_get_full_name(folder->parent_folder);

		full_name = g_strdup_printf("%s%s%s", parent_full_name, folder->separator, name);
	} else {
		if (path_begins_with_sep)
			full_name = g_strdup_printf("%s%s", folder->separator, name);
		else
			full_name = g_strdup(name);
	}

	folder->name = g_strdup(name);
	folder->full_name = full_name;

	folder->frozen = 0;
	folder->folder_changed = FALSE;
	folder->messages_changed = NULL;
}


static void
folder_sync (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	g_warning ("CamelFolder::sync not implemented for `%s'",
		   camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));
}

/**
 * camel_folder_sync:
 * @folder: The folder object
 * @expunge: whether or not to expunge deleted messages
 * @ex: exception object
 *
 * Sync changes made to a folder to its backing store, possibly expunging
 * deleted messages as well.
 **/
void camel_folder_sync(CamelFolder * folder, gboolean expunge, CamelException * ex)
{
	g_return_if_fail(CAMEL_IS_FOLDER(folder));

	CF_CLASS(folder)->sync(folder, expunge, ex);
}

static const gchar *get_name(CamelFolder * folder)
{
	return folder->name;
}

/**
 * camel_folder_get_name:
 * @folder: a folder
 *
 * Get the (short) name of the folder. The fully qualified name
 * can be obtained with the get_full_name method.
 *
 * Return value: name of the folder
 **/
const gchar *camel_folder_get_name(CamelFolder * folder)
{
	g_return_val_if_fail(CAMEL_IS_FOLDER(folder), NULL);

	return CF_CLASS(folder)->get_name(folder);
}

static const gchar *get_full_name(CamelFolder * folder)
{
	return folder->full_name;
}

/**
 * camel_folder_get_full_name:
 * @folder: a folder
 *
 * Get the (full) name of the folder.
 *
 * Return value: full name of the folder
 **/
const gchar *camel_folder_get_full_name(CamelFolder * folder)
{
	g_return_val_if_fail(CAMEL_IS_FOLDER(folder), NULL);

	return CF_CLASS(folder)->get_full_name(folder);
}

static gboolean can_hold_folders(CamelFolder * folder)
{
	return folder->can_hold_folders;
}

static gboolean can_hold_messages(CamelFolder * folder)
{
	return folder->can_hold_messages;
}

static CamelFolder *get_subfolder(CamelFolder * folder, const gchar * folder_name, gboolean create, CamelException * ex)
{
	CamelFolder *new_folder;
	gchar *full_name;
	const gchar *current_folder_full_name;

	g_return_val_if_fail(CAMEL_IS_STORE(folder->parent_store), NULL);

	current_folder_full_name = camel_folder_get_full_name(folder);

	full_name = g_strdup_printf("%s%s%s", current_folder_full_name, folder->separator, folder_name);
	new_folder = camel_store_get_folder(folder->parent_store, full_name, create, ex);
	g_free(full_name);

	return new_folder;
}

/**
 * camel_folder_get_subfolder:
 * @folder: a folder
 * @folder_name: subfolder path
 * @create: whether or not to create the folder if it doesn't exist
 * @ex: a CamelException
 *
 * This method returns a folder object. This folder is a subfolder of
 * the given folder. It is an error to ask for a folder whose name begins
 * with the folder separator character.
 *
 * Return value: the requested folder, or %NULL if the subfolder object
 * could not be obtained
 **/
CamelFolder *camel_folder_get_subfolder(CamelFolder * folder, const gchar * folder_name,
					gboolean create, CamelException * ex)
{
	g_return_val_if_fail(CAMEL_IS_FOLDER(folder), NULL);
	g_return_val_if_fail(folder_name != NULL, NULL);

	return CF_CLASS(folder)->get_subfolder(folder, folder_name, create, ex);
}

static CamelFolder *get_parent_folder(CamelFolder * folder)
{
	return folder->parent_folder;
}

/**
 * camel_folder_get_parent_folder:
 * @folder: folder to get the parent of
 *
 * Return value: the folder's parent
 **/
CamelFolder *camel_folder_get_parent_folder(CamelFolder * folder)
{
	g_return_val_if_fail(CAMEL_IS_FOLDER(folder), NULL);

	return CF_CLASS(folder)->get_parent_folder(folder);
}

static CamelStore *get_parent_store(CamelFolder * folder)
{
	return folder->parent_store;
}

/**
 * camel_folder_get_parent_store:
 * @folder: folder to get the parent of
 *
 * Return value: the parent store of the folder.
 **/
CamelStore *
camel_folder_get_parent_store (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	return CF_CLASS (folder)->get_parent_store (folder);
}


static GPtrArray *
get_subfolder_names (CamelFolder *folder)
{
	g_warning ("CamelFolder::get_subfolder_names not implemented for `%s'",
		   camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));
	return NULL;
}

/**
 * camel_folder_get_subfolder_names:
 * @folder: the folder
 *
 * Return value: an array containing the names of the folder's
 * subfolders. The array should not be modified and must be freed with
 * camel_folder_free_subfolder_names().
 **/
GPtrArray *
camel_folder_get_subfolder_names (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	return CF_CLASS (folder)->get_subfolder_names (folder);
}


static void
free_subfolder_names (CamelFolder *folder, GPtrArray *array)
{
       g_warning ("CamelFolder::free_subfolder_names not implemented "
                  "for `%s'", camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));
}

/**
 * camel_folder_free_subfolder_names:
 * @folder: folder object
 * @array: the array of subfolder names to free
 *
 * Frees the array of names returned by camel_folder_get_subfolder_names().
 **/
void
camel_folder_free_subfolder_names (CamelFolder *folder, GPtrArray *array)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	CF_CLASS (folder)->free_subfolder_names (folder, array);
}


static void
expunge (CamelFolder *folder, CamelException *ex)
{
	g_warning ("CamelFolder::expunge not implemented for `%s'",
		   camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));
}


/**
 * camel_folder_expunge:
 * @folder: the folder
 * @ex: a CamelException
 *
 * Delete messages which have been marked as "DELETED"
 **/
void
camel_folder_expunge (CamelFolder *folder, CamelException *ex)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	CF_CLASS (folder)->expunge (folder, ex);
}


static gint
get_message_count (CamelFolder *folder)
{
	g_warning ("CamelFolder::get_message_count not implemented "
		   "for `%s'", camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));
	return -1;
}

/**
 * camel_folder_get_message_count:
 * @folder: A CamelFolder object
 *
 * Return value: the number of messages in the folder, or -1 if unknown.
 **/
gint
camel_folder_get_message_count (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), -1);

	return CF_CLASS (folder)->get_message_count (folder);
}

static gint
get_unread_message_count (CamelFolder *folder)
{
	g_warning ("CamelFolder::get_unread_message_count not implemented "
		   "for `%s'", camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));
	return -1;
}

/**
 * camel_folder_unread_get_message_count:
 * @folder: A CamelFolder object
 *
 * Return value: the number of unread messages in the folder, or -1 if unknown.
 **/
gint
camel_folder_get_unread_message_count (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), -1);

	return CF_CLASS (folder)->get_unread_message_count (folder);
}


static void
append_message (CamelFolder *folder, CamelMimeMessage *message,
		const CamelMessageInfo *info, CamelException *ex)
{
	g_warning ("CamelFolder::append_message not implemented for `%s'",
		   camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));
	return;

}

/**
 * camel_folder_append_message: add a message to a folder
 * @folder: folder object to add the message to
 * @message: message object
 * @info: optional message info with additional flags/etc to set on new message.
 * @ex: exception object
 *
 * Add a message to a folder.
 **/
void camel_folder_append_message(CamelFolder * folder, CamelMimeMessage * message, const CamelMessageInfo *info, CamelException * ex)
{
	g_return_if_fail(CAMEL_IS_FOLDER(folder));

	CF_CLASS(folder)->append_message(folder, message, info, ex);
}

static guint32 get_permanent_flags(CamelFolder * folder)
{
	return folder->permanent_flags;
}

/**
 * camel_folder_get_permanent_flags:
 * @folder: a CamelFolder
 *
 * Return value: the set of CamelMessageFlags that can be permanently
 * stored on a message between sessions. If it includes %CAMEL_FLAG_USER,
 * then user-defined flags will be remembered.
 **/
guint32
camel_folder_get_permanent_flags (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);

	return CF_CLASS (folder)->get_permanent_flags (folder);
}


static guint32
get_message_flags (CamelFolder *folder, const char *uid)
{
	g_warning ("CamelFolder::get_message_flags not implemented for `%s'",
		   camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));
	return 0;
}

/**
 * camel_folder_get_message_flags:
 * @folder: a CamelFolder
 * @uid: the UID of a message in @folder
 *
 * Return value: the CamelMessageFlags that are set on the indicated
 * message.
 **/
guint32
camel_folder_get_message_flags (CamelFolder *folder, const char *uid)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);

	return CF_CLASS (folder)->get_message_flags (folder, uid);
}


static void
set_message_flags (CamelFolder *folder, const char *uid,
		   guint32 flags, guint32 set)
{
	g_warning ("CamelFolder::set_message_flags not implemented for `%s'",
		   camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));
}

/**
 * camel_folder_set_message_flags:
 * @folder: a CamelFolder
 * @uid: the UID of a message in @folder
 * @flags: a set of CamelMessageFlag values to set
 * @set: the mask of values in @flags to use.
 *
 * Sets those flags specified by @set to the values specified by @flags
 * on the indicated message. (This may or may not persist after the
 * folder or store is closed. See camel_folder_get_permanent_flags().)
 **/
void
camel_folder_set_message_flags (CamelFolder *folder, const char *uid,
				guint32 flags, guint32 set)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	CF_CLASS (folder)->set_message_flags (folder, uid, flags, set);
}


static gboolean
get_message_user_flag (CamelFolder *folder, const char *uid,
		       const char *name)
{
	g_warning ("CamelFolder::get_message_user_flag not implemented "
		   "for `%s'", camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));
	return FALSE;
}

/**
 * camel_folder_get_message_user_flag:
 * @folder: a CamelFolder
 * @uid: the UID of a message in @folder
 * @name: the name of a user flag
 *
 * Return value: whether or not the given user flag is set on the message.
 **/
gboolean
camel_folder_get_message_user_flag (CamelFolder *folder, const char *uid,
				    const char *name)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);

	return CF_CLASS (folder)->get_message_user_flag (folder, uid, name);
}


static void
set_message_user_flag (CamelFolder *folder, const char *uid,
		       const char *name, gboolean value)
{
	g_warning ("CamelFolder::set_message_user_flag not implemented "
		   "for `%s'", camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));
}

/**
 * camel_folder_set_message_user_flag:
 * @folder: a CamelFolder
 * @uid: the UID of a message in @folder
 * @name: the name of the user flag to set
 * @value: the value to set it to
 *
 * Sets the user flag specified by @name to the value specified by @value
 * on the indicated message. (This may or may not persist after the
 * folder or store is closed. See camel_folder_get_permanent_flags().)
 **/
void
camel_folder_set_message_user_flag (CamelFolder *folder, const char *uid,
				    const char *name, gboolean value)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	CF_CLASS (folder)->set_message_user_flag (folder, uid, name, value);
}


static const CamelMessageInfo *
get_message_info (CamelFolder *folder, const char *uid)
{
	g_warning ("CamelFolder::get_message_info not implemented for `%s'",
		   camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));
	return NULL;
}

/**
 * camel_folder_get_message_info:
 * @folder: a CamelFolder
 * @uid: the uid of a message
 *
 * Return value: the summary information for the indicated message
 **/
const CamelMessageInfo *
camel_folder_get_message_info (CamelFolder *folder, const char *uid)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	return CF_CLASS (folder)->get_message_info (folder, uid);
}


/* TODO: is this function required anyway? */
gboolean
camel_folder_has_summary_capability (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	return folder->has_summary_capability;
}


/* UIDs stuff */

static CamelMimeMessage *
get_message (CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	g_warning ("CamelFolder::get_message not implemented for `%s'",
		   camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));
	return NULL;
}

/**
 * camel_folder_get_message:
 * @folder: the folder object
 * @uid: the UID
 * @ex: a CamelException
 *
 * Get a message from its UID in the folder. Messages are cached
 * within a folder, that is, asking twice for the same UID returns the
 * same message object. (FIXME: is this true?)
 *
 * Return value: Message corresponding to the UID
 **/
CamelMimeMessage *
camel_folder_get_message (CamelFolder *folder, const gchar *uid,
			  CamelException *ex)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	return CF_CLASS (folder)->get_message (folder, uid, ex);
}


static GPtrArray *
get_uids (CamelFolder *folder)
{
	g_warning ("CamelFolder::get_uids not implemented for `%s'",
		   camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));
	return NULL;
}

/**
 * camel_folder_get_uids:
 * @folder: folder object
 *
 * Get the list of UIDs available in a folder. This routine is useful
 * for finding what messages are available when the folder does not
 * support summaries. The returned array shoudl not be modified, and
 * must be freed by passing it to camel_folder_free_uids().
 *
 * Return value: GPtrArray of UIDs corresponding to the messages
 * available in the folder.
 **/
GPtrArray *
camel_folder_get_uids (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	return CF_CLASS (folder)->get_uids (folder);
}


static void
free_uids (CamelFolder *folder, GPtrArray *array)
{
	g_warning ("CamelFolder::free_uids not implemented for `%s'",
		   camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));
}

/**
 * camel_folder_free_uids:
 * @folder: folder object
 * @array: the array of uids to free
 *
 * Frees the array of UIDs returned by camel_folder_get_uids().
 **/
void
camel_folder_free_uids (CamelFolder *folder, GPtrArray *array)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	CF_CLASS (folder)->free_uids (folder, array);
}


static GPtrArray *
get_summary (CamelFolder *folder)
{
	g_warning ("CamelFolder::get_summary not implemented for `%s'",
		   camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));
	return NULL;
}

/**
 * camel_folder_get_summary:
 * @folder: a folder object
 *
 * This returns the summary information for the folder. This array
 * should not be modified, and must be freed with
 * camel_folder_free_summary().
 *
 * Return value: an array of CamelMessageInfo
 **/
GPtrArray *
camel_folder_get_summary (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	return CF_CLASS (folder)->get_summary (folder);
}


static void
free_summary (CamelFolder *folder, GPtrArray *array)
{
	g_warning ("CamelFolder::free_summary not implemented for `%s'",
		   camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));
}

/**
 * camel_folder_free_summary:
 * @folder: folder object
 * @array: the summary array to free
 *
 * Frees the summary array returned by camel_folder_get_summary().
 **/
void camel_folder_free_summary(CamelFolder * folder, GPtrArray * array)
{
	g_return_if_fail(CAMEL_IS_FOLDER(folder));

	CF_CLASS(folder)->free_summary(folder, array);
}

/**
 * camel_folder_has_search_capability:
 * @folder: Folder object
 *
 * Checks if a folder supports searching.
 *
 * Return value: %TRUE if the folder supports searching
 **/
gboolean
camel_folder_has_search_capability (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	return folder->has_search_capability;
}

static GPtrArray *
search_by_expression (CamelFolder *folder, const char *expression,
		      CamelException *ex)
{
	g_warning ("CamelFolder::search_by_expression not implemented for "
		   "`%s'", camel_type_to_name (CAMEL_OBJECT_GET_TYPE (folder)));
	return NULL;
}

/**
 * camel_folder_search_by_expression:
 * @folder: Folder object
 * @expression: a search expression
 * @ex: a CamelException
 *
 * Searches the folder for messages matching the given search expression.
 *
 * Return value: a list of uids of matching messages. The caller must
 * free the list and each of the elements when it is done.
 **/
GPtrArray *
camel_folder_search_by_expression (CamelFolder *folder, const char *expression,
				   CamelException *ex)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (folder->has_search_capability, NULL);

	return CF_CLASS (folder)->search_by_expression (folder, expression, ex);
}

static void
search_free(CamelFolder * folder, GPtrArray * result)
{
	int i;

	for (i = 0; i < result->len; i++)
		g_free(g_ptr_array_index(result, i));
	g_ptr_array_free(result, TRUE);
}

/**
 * camel_folder_search_free:
 * @folder: 
 * @result: 
 * 
 * Free the result of a search.
 **/
void 
camel_folder_search_free(CamelFolder * folder, GPtrArray * result)
{
	g_return_if_fail(CAMEL_IS_FOLDER(folder));
	g_return_if_fail(folder->has_search_capability);

	return CF_CLASS(folder)->search_free(folder, result);
}


static void
copy_message_to (CamelFolder *source, const char *uid, CamelFolder *dest,
		 CamelException *ex)
{
	CamelMimeMessage *msg;
	const CamelMessageInfo *info;

	/* Default implementation. */
	
	msg = camel_folder_get_message (source, uid, ex);
	if (!msg)
		return;
	info = camel_folder_get_message_info (source, uid);
	camel_folder_append_message (dest, msg, info ? info->flags : 0, ex);
	camel_object_unref (CAMEL_OBJECT (msg));
	if (camel_exception_is_set (ex))
		return;
}

/**
 * camel_folder_copy_message_to:
 * @source: source folder
 * @uid: UID of message in @source
 * @dest: destination folder
 * @ex: a CamelException
 *
 * This copies a message from one folder to another. If the @source and
 * @dest folders have the same parent_store, this may be more efficient
 * than a camel_folder_append_message().
 **/
void
camel_folder_copy_message_to (CamelFolder *source, const char *uid,
			      CamelFolder *dest, CamelException *ex)
{
	g_return_if_fail (CAMEL_IS_FOLDER (source));
	g_return_if_fail (CAMEL_IS_FOLDER (dest));
	g_return_if_fail (uid != NULL);

	if (source->parent_store == dest->parent_store) {
		return CF_CLASS (source)->copy_message_to (source, uid,
							   dest, ex);
	} else
		return copy_message_to (source, uid, dest, ex);
}


static void
move_message_to (CamelFolder *source, const char *uid, CamelFolder *dest,
		 CamelException *ex)
{
	CamelMimeMessage *msg;
	const CamelMessageInfo *info;

	/* Default implementation. */
	
	msg = camel_folder_get_message (source, uid, ex);
	if (!msg)
		return;
	info = camel_folder_get_message_info (source, uid);
	camel_folder_append_message (dest, msg, info ? info->flags : 0, ex);
	camel_object_unref (CAMEL_OBJECT (msg));
	if (camel_exception_is_set (ex))
		return;
	camel_folder_delete_message (source, uid);
}

/**
 * camel_folder_move_message_to:
 * @source: source folder
 * @uid: UID of message in @source
 * @dest: destination folder
 * @ex: a CamelException
 *
 * This moves a message from one folder to another. If the @source and
 * @dest folders have the same parent_store, this may be more efficient
 * than a camel_folder_append_message() followed by
 * camel_folder_delete_message().
 **/
void camel_folder_move_message_to(CamelFolder * source, const char *uid, CamelFolder * dest, CamelException * ex)
{
	g_return_if_fail(CAMEL_IS_FOLDER(source));
	g_return_if_fail(CAMEL_IS_FOLDER(dest));
	g_return_if_fail(uid != NULL);

	if (source->parent_store == dest->parent_store) {
		return CF_CLASS(source)->move_message_to(source, uid, dest, ex);
	} else
		return move_message_to(source, uid, dest, ex);
}

static void freeze(CamelFolder * folder)
{
	folder->frozen++;
}

/**
 * camel_folder_freeze:
 * @folder: a folder
 *
 * Freezes the folder so that a series of operation can be performed
 * without "message_changed" and "folder_changed" signals being emitted.
 * When the folder is later thawed with camel_folder_thaw(), the
 * suppressed signals will be emitted.
 **/
void camel_folder_freeze(CamelFolder * folder)
{
	g_return_if_fail(CAMEL_IS_FOLDER(folder));

	CF_CLASS(folder)->freeze(folder);
}

static void thaw(CamelFolder * folder)
{
	GList *messages, *m;

	folder->frozen--;
	if (folder->frozen != 0)
		return;

	/* Clear messages_changed now in case the signal handler ends
	 * up calling freeze and thaw itself.
	 */
	messages = folder->messages_changed;
	folder->messages_changed = NULL;

	/* If the folder changed, emit that and ignore the individual
	 * messages (since the UIDs may no longer be valid).
	 */
	if (folder->folder_changed) {
		folder->folder_changed = FALSE;

		camel_object_trigger_event (CAMEL_OBJECT (folder), "folder_changed", GINT_TO_POINTER(0));
	} else if (folder->messages_changed) {
		/* FIXME: would be nice to not emit more than once for
		 * a given message
		 */
		for (m = messages; m; m = m->next) {
			camel_object_trigger_event (CAMEL_OBJECT (folder), "message_changed", m->data);
			g_free (m->data);
		}
		g_list_free (messages);
		return;
	}

	if (messages) {
		for (m = messages; m; m = m->next)
			g_free (m->data);
		g_list_free (messages);
	}
}

/**
 * camel_folder_thaw:
 * @folder: a folder
 *
 * Thaws the folder and emits any pending folder_changed or
 * message_changed signals.
 **/
void
camel_folder_thaw (CamelFolder *folder)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (folder->frozen != 0);

	CF_CLASS (folder)->thaw (folder);
}


/* Event hooks that block emission when frozen */
static gboolean folder_changed (CamelObject *obj, /*int type*/gpointer event_data)
{
	CamelFolder *folder = CAMEL_FOLDER (obj);

	if (folder->frozen) {
		folder->folder_changed = TRUE;
		return FALSE;
	}

	return TRUE;
}

static gboolean message_changed (CamelObject *obj, /*const char *uid*/gpointer event_data)
{
	CamelFolder *folder = CAMEL_FOLDER (obj);

	if (folder->frozen) {
		/*
		 *   if g_tk_signal_handler_pending (CAMEL_OBJECT (folder),
		 *				signals[MESSAGE_CHANGED],
		 *				FALSE)) {
		 */

		/* Only record the UID if it will be useful later. */
		if (!folder->folder_changed) {
			folder->messages_changed =
				g_list_prepend (folder->messages_changed,
						g_strdup ((gchar *)event_data));
		}

		return FALSE;
	}

	return TRUE;
}


/**
 * camel_folder_free_nop:
 * @folder: a folder
 * @array: an array of uids, subfolder names, or CamelMessageInfo
 *
 * "Frees" the provided array by doing nothing. Used by CamelFolder
 * subclasses as an implementation for free_uids, free_summary,
 * or free_subfolder_names when the returned array is "static"
 * information and should not be freed.
 **/
void camel_folder_free_nop(CamelFolder * folder, GPtrArray * array)
{
	;
}

/**
 * camel_folder_free_shallow:
 * @folder: a folder
 * @array: an array of uids, subfolder names, or CamelMessageInfo
 *
 * Frees the provided array but not its contents. Used by CamelFolder
 * subclasses as an implementation for free_uids, free_summary, or
 * free_subfolder_names when the returned array needs to be freed
 * but its contents come from "static" information.
 **/
void camel_folder_free_shallow(CamelFolder * folder, GPtrArray * array)
{
	g_ptr_array_free(array, TRUE);
}

/**
 * camel_folder_free_deep:
 * @folder: a folder
 * @array: an array of uids or subfolder names
 *
 * Frees the provided array and its contents. Used by CamelFolder
 * subclasses as an implementation for free_uids or
 * free_subfolder_names (but NOT free_summary) when the provided
 * information was created explicitly by the corresponding get_ call.
 **/
void camel_folder_free_deep(CamelFolder * folder, GPtrArray * array)
{
	int i;

	for (i = 0; i < array->len; i++)
		g_free(array->pdata[i]);
	g_ptr_array_free(array, TRUE);
}
