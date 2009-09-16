/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "camel-digest-folder.h"
#include "camel-digest-summary.h"
#include "camel-exception.h"
#include "camel-folder-search.h"
#include "camel-mime-message.h"
#include "camel-multipart.h"
#include "camel-string-utils.h"

#define d(x)

#define CAMEL_DIGEST_FOLDER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_DIGEST_FOLDER, CamelDigestFolderPrivate))

struct _CamelDigestFolderPrivate {
	CamelMimeMessage *message;
	CamelFolderSearch *search;
	GMutex *search_lock;
};

#define CAMEL_DIGEST_FOLDER_LOCK(f, l) (g_mutex_lock(((CamelDigestFolder *)f)->priv->l))
#define CAMEL_DIGEST_FOLDER_UNLOCK(f, l) (g_mutex_unlock(((CamelDigestFolder *)f)->priv->l))

static gpointer parent_class;

static void digest_refresh_info (CamelFolder *folder, CamelException *ex);
static void digest_sync (CamelFolder *folder, gboolean expunge, CamelException *ex);
static const gchar *digest_get_full_name (CamelFolder *folder);
static void digest_expunge (CamelFolder *folder, CamelException *ex);

/* message manipulation */
static CamelMimeMessage *digest_get_message (CamelFolder *folder, const gchar *uid,
					     CamelException *ex);
static void digest_append_message (CamelFolder *folder, CamelMimeMessage *message,
				   const CamelMessageInfo *info, gchar **appended_uid, CamelException *ex);
static void digest_transfer_messages_to (CamelFolder *source, GPtrArray *uids,
					 CamelFolder *dest, GPtrArray **transferred_uids,
					 gboolean delete_originals, CamelException *ex);

static GPtrArray *digest_search_by_expression (CamelFolder *folder, const gchar *expression,
					       CamelException *ex);

static GPtrArray *digest_search_by_uids (CamelFolder *folder, const gchar *expression,
					 GPtrArray *uids, CamelException *ex);

static void digest_search_free (CamelFolder *folder, GPtrArray *result);

static void
digest_dispose (GObject *object)
{
	CamelDigestFolderPrivate *priv;
	CamelFolder *folder;

	folder = CAMEL_FOLDER (object);
	priv = CAMEL_DIGEST_FOLDER_GET_PRIVATE (object);

	if (folder->summary != NULL) {
		g_object_unref (folder->summary);
		folder->summary = NULL;
	}

	if (priv->message != NULL) {
		g_object_unref (priv->message);
		priv->message = NULL;
	}

	if (priv->search != NULL) {
		g_object_unref (priv->search);
		priv->search = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
digest_finalize (GObject *object)
{
	CamelDigestFolderPrivate *priv;

	priv = CAMEL_DIGEST_FOLDER_GET_PRIVATE (object);

	g_mutex_free (priv->search_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
digest_folder_class_init (CamelDigestFolderClass *class)
{
	GObjectClass *object_class;
	CamelFolderClass *folder_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (CamelDigestFolderPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = digest_dispose;
	object_class->finalize = digest_finalize;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->refresh_info = digest_refresh_info;
	folder_class->sync = digest_sync;
	folder_class->expunge = digest_expunge;
	folder_class->get_full_name = digest_get_full_name;
	folder_class->get_message = digest_get_message;
	folder_class->append_message = digest_append_message;
	folder_class->transfer_messages_to = digest_transfer_messages_to;
	folder_class->search_by_expression = digest_search_by_expression;
	folder_class->search_by_uids = digest_search_by_uids;
	folder_class->search_free = digest_search_free;
}

static void
digest_folder_init (CamelDigestFolder *digest_folder)
{
	CamelFolder *folder = CAMEL_FOLDER (digest_folder);

	folder->folder_flags |= CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY | CAMEL_FOLDER_HAS_SEARCH_CAPABILITY;

	folder->summary = camel_digest_summary_new ();

	digest_folder->priv = CAMEL_DIGEST_FOLDER_GET_PRIVATE (digest_folder);
	digest_folder->priv->message = NULL;
	digest_folder->priv->search = NULL;
	digest_folder->priv->search_lock = g_mutex_new ();
}

GType
camel_digest_folder_get_type (void)
{
	static GType type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID))
		type = g_type_register_static_simple (
			CAMEL_TYPE_FOLDER,
			"CamelDigestFolder",
			sizeof (CamelDigestFolderClass),
			(GClassInitFunc) digest_folder_class_init,
			sizeof (CamelDigestFolder),
			(GInstanceInitFunc) digest_folder_init,
			0);

	return type;
}

static gboolean
multipart_contains_message_parts (CamelMultipart *multipart)
{
	gboolean has_message_parts = FALSE;
	CamelDataWrapper *wrapper;
	CamelMimePart *part;
	gint i, parts;

	parts = camel_multipart_get_number (multipart);
	for (i = 0; i < parts && !has_message_parts; i++) {
		part = camel_multipart_get_part (multipart, i);
		wrapper = camel_medium_get_content (CAMEL_MEDIUM (part));
		if (CAMEL_IS_MULTIPART (wrapper)) {
			has_message_parts = multipart_contains_message_parts (CAMEL_MULTIPART (wrapper));
		} else if (CAMEL_IS_MIME_MESSAGE (wrapper)) {
			has_message_parts = TRUE;
		}
	}

	return has_message_parts;
}

static void
digest_add_multipart (CamelFolder *folder, CamelMultipart *multipart, const gchar *preuid)
{
	CamelDataWrapper *wrapper;
	CamelMessageInfo *info;
	CamelMimePart *part;
	gint parts, i;
	gchar *uid;

	parts = camel_multipart_get_number (multipart);
	for (i = 0; i < parts; i++) {
		gchar *tmp;
		part = camel_multipart_get_part (multipart, i);

		wrapper = camel_medium_get_content (CAMEL_MEDIUM (part));

		if (CAMEL_IS_MULTIPART (wrapper)) {
			uid = g_strdup_printf ("%s%d.", preuid, i);
			digest_add_multipart (folder, CAMEL_MULTIPART (wrapper), uid);
			g_free (uid);
			continue;
		} else if (!CAMEL_IS_MIME_MESSAGE (wrapper)) {
			continue;
		}

		info = camel_folder_summary_info_new_from_message (folder->summary, CAMEL_MIME_MESSAGE (wrapper), NULL);
		camel_pstring_free(info->uid);
		tmp = g_strdup_printf ("%s%d", preuid, i);
		info->uid = camel_pstring_strdup (tmp);
		g_free(tmp);
		camel_folder_summary_add (folder->summary, info);
	}
}

static void
construct_summary (CamelFolder *folder, CamelMultipart *multipart)
{
	digest_add_multipart (folder, multipart, "");
}

CamelFolder *
camel_digest_folder_new (CamelStore *parent_store, CamelMimeMessage *message)
{
	CamelDigestFolder *digest_folder;
	CamelDataWrapper *wrapper;
	CamelFolder *folder;

	wrapper = camel_medium_get_content (CAMEL_MEDIUM (message));
	if (!wrapper || !CAMEL_IS_MULTIPART (wrapper))
		return NULL;

	/* Make sure we have a multipart/digest subpart or at least some message/rfc822 attachments... */
	if (!camel_content_type_is (CAMEL_DATA_WRAPPER (message)->mime_type, "multipart", "digest")) {
		if (!multipart_contains_message_parts (CAMEL_MULTIPART (wrapper)))
			return NULL;
	}

	folder = g_object_new (CAMEL_TYPE_DIGEST_FOLDER, NULL);
	digest_folder = CAMEL_DIGEST_FOLDER (folder);

	camel_folder_construct (folder, parent_store, "folder_name", "short_name");

	g_object_ref (message);
	digest_folder->priv->message = message;

	construct_summary (folder, CAMEL_MULTIPART (wrapper));

	return folder;
}

static void
digest_refresh_info (CamelFolder *folder, CamelException *ex)
{

}

static void
digest_sync (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	/* no-op */
}

static void
digest_expunge (CamelFolder *folder, CamelException *ex)
{
	/* no-op */
}

static const gchar *
digest_get_full_name (CamelFolder *folder)
{
	return folder->full_name;
}

static void
digest_append_message (CamelFolder *folder, CamelMimeMessage *message,
		       const CamelMessageInfo *info, gchar **appended_uid,
		       CamelException *ex)
{
	/* no-op */
	if (appended_uid)
		*appended_uid = NULL;
}

static void
digest_transfer_messages_to (CamelFolder *source, GPtrArray *uids,
			     CamelFolder *dest, GPtrArray **transferred_uids,
			     gboolean delete_originals, CamelException *ex)
{
	/* no-op */
	if (transferred_uids)
		*transferred_uids = NULL;
}

static CamelMimeMessage *
digest_get_message (CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	CamelDigestFolder *digest = CAMEL_DIGEST_FOLDER (folder);
	CamelDataWrapper *wrapper;
	CamelMimeMessage *message;
	CamelMimePart *part;
	gchar *subuid;
	gint id;

	part = CAMEL_MIME_PART (digest->priv->message);
	wrapper = camel_medium_get_content (CAMEL_MEDIUM (part));

	do {
		id = strtoul (uid, &subuid, 10);
		if (!CAMEL_IS_MULTIPART (wrapper))
			return NULL;

		part = camel_multipart_get_part (CAMEL_MULTIPART (wrapper), id);
		wrapper = camel_medium_get_content (CAMEL_MEDIUM (part));
		uid = subuid + 1;
	} while (*subuid == '.');

	if (!CAMEL_IS_MIME_MESSAGE (wrapper))
		return NULL;

	message = CAMEL_MIME_MESSAGE (wrapper);
	g_object_ref (message);

	return message;
}

static GPtrArray *
digest_search_by_expression (CamelFolder *folder, const gchar *expression, CamelException *ex)
{
	CamelDigestFolder *df = (CamelDigestFolder *) folder;
	GPtrArray *matches;

	CAMEL_DIGEST_FOLDER_LOCK (folder, search_lock);

	if (!df->priv->search)
		df->priv->search = camel_folder_search_new ();

	camel_folder_search_set_folder (df->priv->search, folder);
	matches = camel_folder_search_search(df->priv->search, expression, NULL, ex);

	CAMEL_DIGEST_FOLDER_UNLOCK (folder, search_lock);

	return matches;
}

static GPtrArray *
digest_search_by_uids (CamelFolder *folder, const gchar *expression, GPtrArray *uids, CamelException *ex)
{
	CamelDigestFolder *df = (CamelDigestFolder *) folder;
	GPtrArray *matches;

	if (uids->len == 0)
		return g_ptr_array_new();

	CAMEL_DIGEST_FOLDER_LOCK (folder, search_lock);

	if (!df->priv->search)
		df->priv->search = camel_folder_search_new ();

	camel_folder_search_set_folder (df->priv->search, folder);
	matches = camel_folder_search_search(df->priv->search, expression, uids, ex);

	CAMEL_DIGEST_FOLDER_UNLOCK (folder, search_lock);

	return matches;
}

static void
digest_search_free (CamelFolder *folder, GPtrArray *result)
{
	CamelDigestFolder *digest_folder = CAMEL_DIGEST_FOLDER (folder);

	CAMEL_DIGEST_FOLDER_LOCK (folder, search_lock);

	camel_folder_search_free_result (digest_folder->priv->search, result);

	CAMEL_DIGEST_FOLDER_UNLOCK (folder, search_lock);
}
