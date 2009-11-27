/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*-
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
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

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>

#include "camel-mh-folder.h"
#include "camel-mh-summary.h"

#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/

static gpointer parent_class;

static CamelLocalSummary *mh_create_summary(CamelLocalFolder *lf, const gchar *path, const gchar *folder, CamelIndex *index);

static gboolean mh_append_message(CamelFolder * folder, CamelMimeMessage * message, const CamelMessageInfo *info, gchar **appended_uid, GError **error);
static CamelMimeMessage *mh_get_message(CamelFolder * folder, const gchar * uid, GError **error);
static gchar * mh_get_filename (CamelFolder *folder, const gchar *uid, GError **error);

static void
mh_folder_class_init (CamelObjectClass *class)
{
	CamelFolderClass *folder_class;
	CamelLocalFolderClass *local_folder_class;

	parent_class = g_type_class_peek_parent (class);

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->append_message = mh_append_message;
	folder_class->get_message = mh_get_message;
	folder_class->get_filename = mh_get_filename;

	local_folder_class = CAMEL_LOCAL_FOLDER_CLASS (class);
	local_folder_class->create_summary = mh_create_summary;
}

GType
camel_mh_folder_get_type (void)
{
	static GType type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID))
		type = g_type_register_static_simple (
			CAMEL_TYPE_LOCAL_FOLDER,
			"CamelMhFolder",
			sizeof (CamelMhFolderClass),
			(GClassInitFunc) mh_folder_class_init,
			sizeof (CamelMhFolder),
			(GInstanceInitFunc) NULL,
			0);

	return type;
}

CamelFolder *
camel_mh_folder_new (CamelStore *parent_store,
                     const gchar *full_name,
                     guint32 flags,
                     GError **error)
{
	CamelFolder *folder;

	d(printf("Creating mh folder: %s\n", full_name));

	folder = g_object_new (CAMEL_TYPE_MH_FOLDER, NULL);
	folder = (CamelFolder *) camel_local_folder_construct (
		CAMEL_LOCAL_FOLDER (folder),
		parent_store, full_name, flags, error);

	return folder;
}

static CamelLocalSummary *
mh_create_summary (CamelLocalFolder *lf,
                   const gchar *path,
                   const gchar *folder,
                   CamelIndex *index)
{
	return (CamelLocalSummary *) camel_mh_summary_new (
		CAMEL_FOLDER (lf), path, folder, index);
}

static gboolean
mh_append_message (CamelFolder *folder,
                   CamelMimeMessage *message,
                   const CamelMessageInfo *info,
                   gchar **appended_uid,
                   GError **error)
{
	CamelLocalFolder *lf = (CamelLocalFolder *)folder;
	CamelStream *output_stream;
	CamelMessageInfo *mi;
	gchar *name;

	/* FIXME: probably needs additional locking (although mh doesn't appear do do it) */

	d(printf("Appending message\n"));

	/* If we can't lock, don't do anything */
	if (camel_local_folder_lock (lf, CAMEL_LOCK_WRITE, error) == -1)
		return FALSE;

	/* add it to the summary/assign the uid, etc */
	mi = camel_local_summary_add((CamelLocalSummary *)folder->summary, message, info, lf->changes, error);
	if (mi == NULL)
		goto check_changed;

	if ((camel_message_info_flags (mi) & CAMEL_MESSAGE_ATTACHMENTS) && !camel_mime_message_has_attachment (message))
		camel_message_info_set_flags (mi, CAMEL_MESSAGE_ATTACHMENTS, 0);

	d(printf("Appending message: uid is %s\n", camel_message_info_uid(mi)));

	/* write it out, use the uid we got from the summary */
	name = g_strdup_printf("%s/%s", lf->folder_path, camel_message_info_uid(mi));
	output_stream = camel_stream_fs_new_with_name(name, O_WRONLY|O_CREAT, 0600);
	if (output_stream == NULL)
		goto fail_write;

	if (camel_data_wrapper_write_to_stream ((CamelDataWrapper *)message, output_stream) == -1
	    || camel_stream_close (output_stream) == -1)
		goto fail_write;

	/* close this? */
	g_object_unref (CAMEL_OBJECT (output_stream));

	g_free(name);

	if (appended_uid)
		*appended_uid = g_strdup(camel_message_info_uid(mi));

	goto check_changed;

 fail_write:

	/* remove the summary info so we are not out-of-sync with the mh folder */
	camel_folder_summary_remove_uid (CAMEL_FOLDER_SUMMARY (folder->summary),
					 camel_message_info_uid (mi));

	if (errno == EINTR)
		g_set_error (
			error, CAMEL_ERROR,
			CAMEL_ERROR_USER_CANCEL,
			_("MH append message canceled"));
	else
		g_set_error (
			error, G_FILE_ERROR,
			g_file_error_from_errno (errno),
			_("Cannot append message to mh folder: %s: %s"),
			name, g_strerror (errno));

	if (output_stream) {
		g_object_unref (CAMEL_OBJECT (output_stream));
		unlink (name);
	}

	g_free (name);

 check_changed:
	camel_local_folder_unlock (lf);

	if (lf && camel_folder_change_info_changed (lf->changes)) {
		camel_object_trigger_event (CAMEL_OBJECT (folder), "folder_changed", lf->changes);
		camel_folder_change_info_clear (lf->changes);
	}

	return TRUE;
}

static gchar *
mh_get_filename (CamelFolder *folder,
                 const gchar *uid,
                 GError **error)
{
	CamelLocalFolder *lf = (CamelLocalFolder *)folder;

	return g_strdup_printf("%s/%s", lf->folder_path, uid);
}

static CamelMimeMessage *
mh_get_message (CamelFolder *folder,
                const gchar *uid,
                GError **error)
{
	CamelLocalFolder *lf = (CamelLocalFolder *)folder;
	CamelStream *message_stream = NULL;
	CamelMimeMessage *message = NULL;
	CamelMessageInfo *info;
	gchar *name = NULL;

	d(printf("getting message: %s\n", uid));

	if (camel_local_folder_lock (lf, CAMEL_LOCK_WRITE, error) == -1)
		return NULL;

	/* get the message summary info */
	if ((info = camel_folder_summary_uid(folder->summary, uid)) == NULL) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_UID,
			_("Cannot get message: %s from folder %s\n  %s"),
			uid, lf->folder_path, _("No such message"));
		goto fail;
	}

	/* we only need it to check the message exists */
	camel_message_info_free(info);

	name = g_strdup_printf("%s/%s", lf->folder_path, uid);
	if ((message_stream = camel_stream_fs_new_with_name(name, O_RDONLY, 0)) == NULL) {
		g_set_error (
			error, G_FILE_ERROR,
			g_file_error_from_errno (errno),
			_("Cannot get message: %s from folder %s\n  %s"),
			name, lf->folder_path, g_strerror (errno));
		goto fail;
	}

	message = camel_mime_message_new();
	if (camel_data_wrapper_construct_from_stream((CamelDataWrapper *)message, message_stream) == -1) {
		g_set_error (
			error, CAMEL_ERROR,
			CAMEL_ERROR_SYSTEM,
			_("Cannot get message: %s from folder %s\n  %s"),
			name, lf->folder_path,
			_("Message construction failed."));
		g_object_unref (message);
		message = NULL;

	}
	g_object_unref (message_stream);

 fail:
	g_free (name);

	camel_local_folder_unlock (lf);

	if (lf && camel_folder_change_info_changed (lf->changes)) {
		camel_object_trigger_event (CAMEL_OBJECT (folder), "folder_changed", lf->changes);
		camel_folder_change_info_clear (lf->changes);
	}

	return message;
}
