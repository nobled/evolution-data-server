/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-nntp-folder.c : Class for a news folder
 *
 * Authors : Chris Toshok <toshok@ximian.com>
 *           Michael Zucchi <notzed@ximian.com>
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>

#include <libedataserver/e-data-server-util.h>

#include "camel-private.h"

#include "camel-nntp-folder.h"
#include "camel-nntp-private.h"
#include "camel-nntp-store.h"
#include "camel-nntp-store.h"
#include "camel-nntp-summary.h"

#define CAMEL_NNTP_FOLDER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_NNTP_FOLDER, CamelNNTPFolderPrivate))

G_DEFINE_TYPE (CamelNNTPFolder, camel_nntp_folder, CAMEL_TYPE_DISCO_FOLDER)

static void
nntp_folder_finalize (GObject *object)
{
	CamelNNTPFolder *nntp_folder = CAMEL_NNTP_FOLDER (object);

	camel_folder_summary_save_to_db (
		CAMEL_FOLDER (nntp_folder)->summary, NULL);

	g_mutex_free (nntp_folder->priv->search_lock);
	g_mutex_free (nntp_folder->priv->cache_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_nntp_folder_parent_class)->finalize (object);
}

gboolean
camel_nntp_folder_selected (CamelNNTPFolder *folder,
                            gchar *line,
                            GError **error)
{
	return camel_nntp_summary_check (
		(CamelNNTPSummary *)((CamelFolder *)folder)->summary,
		(CamelNNTPStore *)((CamelFolder *)folder)->parent_store,
		line, folder->changes, error);
}

static gboolean
nntp_folder_refresh_info_online (CamelFolder *folder,
                                 GError **error)
{
	CamelNNTPStore *nntp_store;
	CamelFolderChangeInfo *changes = NULL;
	CamelNNTPFolder *nntp_folder;
	gchar *line;
	gboolean success;

	nntp_store = (CamelNNTPStore *) folder->parent_store;
	nntp_folder = (CamelNNTPFolder *) folder;

	CAMEL_SERVICE_REC_LOCK(nntp_store, connect_lock);

	success = camel_nntp_command (
		nntp_store, error, nntp_folder, &line, NULL);

	if (camel_folder_change_info_changed(nntp_folder->changes)) {
		changes = nntp_folder->changes;
		nntp_folder->changes = camel_folder_change_info_new();
	}

	CAMEL_SERVICE_REC_UNLOCK(nntp_store, connect_lock);

	if (changes) {
		camel_object_trigger_event ((CamelObject *) folder, "folder_changed", changes);
		camel_folder_change_info_free (changes);
	}

	return success;
}

static gboolean
nntp_folder_sync_online (CamelFolder *folder, GError **error)
{
	gboolean success;

	CAMEL_SERVICE_REC_LOCK(folder->parent_store, connect_lock);
	success = camel_folder_summary_save_to_db (folder->summary, error);
	CAMEL_SERVICE_REC_UNLOCK(folder->parent_store, connect_lock);

	return success;
}

static gboolean
nntp_folder_sync_offline (CamelFolder *folder, GError **error)
{
	gboolean success;

	CAMEL_SERVICE_REC_LOCK(folder->parent_store, connect_lock);
	success = camel_folder_summary_save_to_db (folder->summary, error);
	CAMEL_SERVICE_REC_UNLOCK(folder->parent_store, connect_lock);

	return success;
}

static gchar *
nntp_get_filename (CamelFolder *folder, const gchar *uid, GError **error)
{
	CamelNNTPStore *nntp_store = (CamelNNTPStore *) folder->parent_store;
	gchar *article, *msgid;

	article = alloca(strlen(uid)+1);
	strcpy(article, uid);
	msgid = strchr (article, ',');
	if (msgid == NULL) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_SYSTEM,
			_("Internal error: UID in invalid format: %s"), uid);
		return NULL;
	}
	*msgid++ = 0;

	return camel_data_cache_get_filename (nntp_store->cache, "cache", msgid, error);
}

static CamelStream *
nntp_folder_download_message (CamelNNTPFolder *nntp_folder, const gchar *id, const gchar *msgid, GError **error)
{
	CamelNNTPStore *nntp_store = (CamelNNTPStore *) ((CamelFolder *) nntp_folder)->parent_store;
	CamelStream *stream = NULL;
	gint ret;
	gchar *line;

	ret = camel_nntp_command (nntp_store, error, nntp_folder, &line, "article %s", id);
	if (ret == 220) {
		stream = camel_data_cache_add (nntp_store->cache, "cache", msgid, NULL);
		if (stream) {
			if (camel_stream_write_to_stream ((CamelStream *) nntp_store->stream, stream, error) == -1)
				goto fail;
			if (camel_stream_reset (stream, error) == -1)
				goto fail;
		} else {
			stream = (CamelStream *) nntp_store->stream;
			g_object_ref (stream);
		}
	} else if (ret == 423 || ret == 430) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_UID,
			_("Cannot get message %s: %s"), msgid, line);
	} else if (ret != -1) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_SYSTEM,
			_("Cannot get message %s: %s"), msgid, line);
	}

	return stream;

fail:
	g_prefix_error (error, _("Cannot get message %s: "), msgid);

	return NULL;
}

static gboolean
nntp_folder_cache_message (CamelDiscoFolder *disco_folder,
                           const gchar *uid,
                           GError **error)
{
	CamelNNTPStore *nntp_store = (CamelNNTPStore *)((CamelFolder *) disco_folder)->parent_store;
	CamelStream *stream;
	gchar *article, *msgid;
	gboolean success = TRUE;

	article = alloca(strlen(uid)+1);
	strcpy(article, uid);
	msgid = strchr(article, ',');
	if (!msgid) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_SYSTEM,
			_("Internal error: UID in invalid format: %s"), uid);
		return FALSE;
	}
	*msgid++ = 0;

	CAMEL_SERVICE_REC_LOCK(nntp_store, connect_lock);

	stream = nntp_folder_download_message (
		(CamelNNTPFolder *) disco_folder, article, msgid, error);
	if (stream)
		g_object_unref (stream);
	else
		success = FALSE;

	CAMEL_SERVICE_REC_UNLOCK(nntp_store, connect_lock);

	return success;
}

static CamelMimeMessage *
nntp_folder_get_message (CamelFolder *folder, const gchar *uid, GError **error)
{
	CamelMimeMessage *message = NULL;
	CamelNNTPStore *nntp_store;
	CamelFolderChangeInfo *changes;
	CamelNNTPFolder *nntp_folder;
	CamelStream *stream = NULL;
	gchar *article, *msgid;

	nntp_store = (CamelNNTPStore *) folder->parent_store;
	nntp_folder = (CamelNNTPFolder *) folder;

	article = alloca(strlen(uid)+1);
	strcpy(article, uid);
	msgid = strchr (article, ',');
	if (msgid == NULL) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_SYSTEM,
			_("Internal error: UID in invalid format: %s"), uid);
		return NULL;
	}
	*msgid++ = 0;

	CAMEL_SERVICE_REC_LOCK(nntp_store, connect_lock);

	/* Lookup in cache, NEWS is global messageid's so use a global cache path */
	stream = camel_data_cache_get (nntp_store->cache, "cache", msgid, NULL);
	if (stream == NULL) {
		if (camel_disco_store_status ((CamelDiscoStore *) nntp_store) == CAMEL_DISCO_STORE_OFFLINE) {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_UNAVAILABLE,
				_("This message is not currently available"));
			goto fail;
		}

		stream = nntp_folder_download_message (nntp_folder, article, msgid, error);
		if (stream == NULL)
			goto fail;
	}

	message = camel_mime_message_new ();
	if (camel_data_wrapper_construct_from_stream ((CamelDataWrapper *) message, stream, error) == -1) {
		g_prefix_error (error, _("Cannot get message %s: "), uid);
		g_object_unref (message);
		message = NULL;
	}

	g_object_unref (stream);
fail:
	if (camel_folder_change_info_changed (nntp_folder->changes)) {
		changes = nntp_folder->changes;
		nntp_folder->changes = camel_folder_change_info_new ();
	} else {
		changes = NULL;
	}

	CAMEL_SERVICE_REC_UNLOCK(nntp_store, connect_lock);

	if (changes) {
		camel_object_trigger_event ((CamelObject *) folder, "folder_changed", changes);
		camel_folder_change_info_free (changes);
	}

	return message;
}

static GPtrArray*
nntp_folder_search_by_expression (CamelFolder *folder, const gchar *expression, GError **error)
{
	CamelNNTPFolder *nntp_folder = CAMEL_NNTP_FOLDER (folder);
	GPtrArray *matches;

	CAMEL_NNTP_FOLDER_LOCK(nntp_folder, search_lock);

	if (nntp_folder->search == NULL)
		nntp_folder->search = camel_folder_search_new ();

	camel_folder_search_set_folder (nntp_folder->search, folder);
	matches = camel_folder_search_search(nntp_folder->search, expression, NULL, error);

	CAMEL_NNTP_FOLDER_UNLOCK(nntp_folder, search_lock);

	return matches;
}

static guint32
nntp_folder_count_by_expression (CamelFolder *folder, const gchar *expression, GError **error)
{
	CamelNNTPFolder *nntp_folder = CAMEL_NNTP_FOLDER (folder);
	guint32 count;

	CAMEL_NNTP_FOLDER_LOCK(nntp_folder, search_lock);

	if (nntp_folder->search == NULL)
		nntp_folder->search = camel_folder_search_new ();

	camel_folder_search_set_folder (nntp_folder->search, folder);
	count = camel_folder_search_count(nntp_folder->search, expression, error);

	CAMEL_NNTP_FOLDER_UNLOCK(nntp_folder, search_lock);

	return count;
}

static GPtrArray *
nntp_folder_search_by_uids (CamelFolder *folder, const gchar *expression, GPtrArray *uids, GError **error)
{
	CamelNNTPFolder *nntp_folder = (CamelNNTPFolder *) folder;
	GPtrArray *matches;

	if (uids->len == 0)
		return g_ptr_array_new();

	CAMEL_NNTP_FOLDER_LOCK(folder, search_lock);

	if (nntp_folder->search == NULL)
		nntp_folder->search = camel_folder_search_new ();

	camel_folder_search_set_folder (nntp_folder->search, folder);
	matches = camel_folder_search_search(nntp_folder->search, expression, uids, error);

	CAMEL_NNTP_FOLDER_UNLOCK(folder, search_lock);

	return matches;
}

static void
nntp_folder_search_free (CamelFolder *folder, GPtrArray *result)
{
	CamelNNTPFolder *nntp_folder = CAMEL_NNTP_FOLDER (folder);

	CAMEL_NNTP_FOLDER_LOCK(nntp_folder, search_lock);
	camel_folder_search_free_result (nntp_folder->search, result);
	CAMEL_NNTP_FOLDER_UNLOCK(nntp_folder, search_lock);
}

static gboolean
nntp_folder_append_message_online (CamelFolder *folder,
                                   CamelMimeMessage *mime_message,
                                   const CamelMessageInfo *info,
                                   gchar **appended_uid,
                                   GError **error)
{
	CamelNNTPStore *nntp_store = (CamelNNTPStore *) folder->parent_store;
	CamelStream *stream = (CamelStream*)nntp_store->stream;
	CamelStream *filtered_stream;
	CamelMimeFilter *crlffilter;
	CamelMimePart *mime_part;
	GQueue *header_queue;
	GQueue save_queue = G_QUEUE_INIT;
	gint ret;
	guint u;
	gchar *group, *line;
	gboolean success = TRUE;

	CAMEL_SERVICE_REC_LOCK(nntp_store, connect_lock);

	/* send 'POST' command */
	ret = camel_nntp_command (nntp_store, error, NULL, &line, "post");
	if (ret != 340) {
		if (ret == 440) {
			g_set_error (
				error, CAMEL_FOLDER_ERROR,
				CAMEL_FOLDER_ERROR_INSUFFICIENT_PERMISSION,
				_("Posting failed: %s"), line);
			success = FALSE;
		} else if (ret != -1) {
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_SYSTEM,
				_("Posting failed: %s"), line);
			success = FALSE;
		}
		CAMEL_SERVICE_REC_UNLOCK(nntp_store, connect_lock);
		return success;
	}

	/* the 'Newsgroups: ' header */
	group = g_strdup_printf ("Newsgroups: %s\r\n", folder->full_name);

	/* setup stream filtering */
	crlffilter = camel_mime_filter_crlf_new (CAMEL_MIME_FILTER_CRLF_ENCODE, CAMEL_MIME_FILTER_CRLF_MODE_CRLF_DOTS);
	filtered_stream = camel_stream_filter_new (stream);
	camel_stream_filter_add (
		CAMEL_STREAM_FILTER (filtered_stream), crlffilter);
	g_object_unref (crlffilter);

	/* remove mail 'To', 'CC', and 'BCC' headers */
	mime_part = CAMEL_MIME_PART (mime_message);
	header_queue = camel_mime_part_get_raw_headers (mime_part);
	camel_header_raw_extract (header_queue, &save_queue, "To");
	camel_header_raw_extract (header_queue, &save_queue, "Cc");
	camel_header_raw_extract (header_queue, &save_queue, "Bcc");

	/* write the message */
	if (camel_stream_write(stream, group, strlen(group), error) == -1
	    || camel_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (mime_message), filtered_stream, error) == -1
	    || camel_stream_flush (filtered_stream, error) == -1
	    || camel_stream_write (stream, "\r\n.\r\n", 5, error) == -1
	    || (ret = camel_nntp_stream_line (nntp_store->stream, (guchar **)&line, &u, error)) == -1) {
		g_prefix_error (error, "Posting failed: ");
		success = FALSE;
	} else if (atoi(line) != 240) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_SYSTEM,
			_("Posting failed: %s"), line);
		success = FALSE;
	}

	g_object_unref (filtered_stream);
	g_free(group);

	camel_header_raw_append_queue (header_queue, &save_queue);

	CAMEL_SERVICE_REC_UNLOCK(nntp_store, connect_lock);

	return success;
}

static gboolean
nntp_folder_append_message_offline (CamelFolder *folder,
                                    CamelMimeMessage *mime_message,
                                    const CamelMessageInfo *info,
                                    gchar **appended_uid,
                                    GError **error)
{
	g_set_error (
		error, CAMEL_SERVICE_ERROR,
		CAMEL_SERVICE_ERROR_UNAVAILABLE,
		_("You cannot post NNTP messages while working offline!"));

	return FALSE;
}

/* I do not know what to do this exactly. Looking at the IMAP implementation for this, it
   seems to assume the message is copied to a folder on the same store. In that case, an
   NNTP implementation doesn't seem to make any sense. */
static gboolean
nntp_folder_transfer_message (CamelFolder *source,
                              GPtrArray *uids,
                              CamelFolder *dest,
                              GPtrArray **transferred_uids,
                              gboolean delete_orig,
                              GError **error)
{
	g_set_error (
		error, CAMEL_SERVICE_ERROR,
		CAMEL_SERVICE_ERROR_UNAVAILABLE,
		_("You cannot copy messages from a NNTP folder!"));

	return FALSE;
}

static void
camel_nntp_folder_class_init (CamelNNTPFolderClass *class)
{
	GObjectClass *object_class;
	CamelFolderClass *folder_class;
	CamelDiscoFolderClass *disco_folder_class;

	g_type_class_add_private (class, sizeof (CamelNNTPFolderPrivate));

	folder_class = CAMEL_FOLDER_CLASS (g_type_class_peek (CAMEL_TYPE_FOLDER));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = nntp_folder_finalize;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->get_message = nntp_folder_get_message;
	folder_class->search_by_expression = nntp_folder_search_by_expression;
	folder_class->count_by_expression = nntp_folder_count_by_expression;
	folder_class->search_by_uids = nntp_folder_search_by_uids;
	folder_class->search_free = nntp_folder_search_free;
	folder_class->get_filename = nntp_get_filename;

	disco_folder_class = CAMEL_DISCO_FOLDER_CLASS (class);
	disco_folder_class->sync_online = nntp_folder_sync_online;
	disco_folder_class->sync_resyncing = nntp_folder_sync_offline;
	disco_folder_class->sync_offline = nntp_folder_sync_offline;
	disco_folder_class->cache_message = nntp_folder_cache_message;
	disco_folder_class->append_online = nntp_folder_append_message_online;
	disco_folder_class->append_resyncing = nntp_folder_append_message_online;
	disco_folder_class->append_offline = nntp_folder_append_message_offline;
	disco_folder_class->transfer_online = nntp_folder_transfer_message;
	disco_folder_class->transfer_resyncing = nntp_folder_transfer_message;
	disco_folder_class->transfer_offline = nntp_folder_transfer_message;
	disco_folder_class->refresh_info_online = nntp_folder_refresh_info_online;
}

static void
camel_nntp_folder_init (CamelNNTPFolder *nntp_folder)
{
	nntp_folder->priv = CAMEL_NNTP_FOLDER_GET_PRIVATE (nntp_folder);

	nntp_folder->changes = camel_folder_change_info_new ();
	nntp_folder->priv->search_lock = g_mutex_new ();
	nntp_folder->priv->cache_lock = g_mutex_new ();
}

CamelFolder *
camel_nntp_folder_new (CamelStore *parent,
                       const gchar *folder_name,
                       GError **error)
{
	CamelFolder *folder;
	CamelNNTPFolder *nntp_folder;
	gchar *root;
	CamelService *service;
	CamelStoreInfo *si;
	gboolean subscribed = TRUE;

	service = (CamelService *) parent;
	root = camel_session_get_storage_path (service->session, service, error);
	if (root == NULL)
		return NULL;

	/* If this doesn't work, stuff wont save, but let it continue anyway */
	g_mkdir_with_parents (root, 0700);

	folder = g_object_new (CAMEL_TYPE_NNTP_FOLDER, NULL);
	nntp_folder = (CamelNNTPFolder *)folder;

	camel_folder_construct (folder, parent, folder_name, folder_name);
	folder->folder_flags |= CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY|CAMEL_FOLDER_HAS_SEARCH_CAPABILITY;

	nntp_folder->storage_path = g_build_filename (root, folder->full_name, NULL);
	g_free (root);

	root = g_strdup_printf ("%s.cmeta", nntp_folder->storage_path);
	camel_object_set(nntp_folder, NULL, CAMEL_OBJECT_STATE_FILE, root, NULL);
	camel_object_state_read(nntp_folder);
	g_free(root);

	root = g_strdup_printf("%s.ev-summary", nntp_folder->storage_path);
	folder->summary = (CamelFolderSummary *) camel_nntp_summary_new (folder, root);
	g_free(root);

	camel_folder_summary_load_from_db (folder->summary, NULL);

	si = camel_store_summary_path ((CamelStoreSummary *) ((CamelNNTPStore*) parent)->summary, folder_name);
	if (si) {
		subscribed = (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) != 0;
		camel_store_summary_info_free ((CamelStoreSummary *) ((CamelNNTPStore*) parent)->summary, si);
	}

	if (subscribed && !camel_folder_refresh_info (folder, error)) {
		g_object_unref (folder);
		folder = NULL;
        }

	return folder;
}
