/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2001 Ximian, Inc. (www.ximian.com)
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
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "camel-digest-folder.h"

#include "camel-exception.h"
#include "camel-multipart.h"
#include "camel-mime-message.h"
#include "camel-folder-summary.h"

#define d(x)

#define _PRIVATE(o) (((CamelDigestFolder *)(o))->priv)

struct _CamelDigestFolderPrivate {
	CamelMimeMessage *message;
	GHashTable *info_hash;
	GPtrArray *summary;
	GPtrArray *uids;
};

static CamelFolderClass *parent_class = NULL;

static void digest_refresh_info (CamelFolder *folder, CamelException *ex);
static void digest_sync (CamelFolder *folder, guint32 flags, CamelException *ex);
static const char *digest_get_full_name (CamelFolder *folder);

static GPtrArray *digest_get_uids (CamelFolder *folder);
static void digest_free_uids (CamelFolder *folder, GPtrArray *uids);
static CamelMessageInfo *digest_get_message_info (CamelFolder *folder, const char *uid);

/* message manipulation */
static CamelMimeMessage *digest_get_message (CamelFolder *folder, const gchar *uid,
					     CamelException *ex);
static void digest_append_message (CamelFolder *folder, CamelMimeMessage *message,
				   const CamelMessageInfo *info, char **appended_uid, CamelException *ex);
static void digest_transfer_messages_to (CamelFolder *source, GPtrArray *uids,
					 CamelFolder *dest, GPtrArray **transferred_uids,
					 gboolean delete_originals, CamelException *ex);


static void
camel_digest_folder_class_init (CamelDigestFolderClass *camel_digest_folder_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS (camel_digest_folder_class);
	
	parent_class = CAMEL_FOLDER_CLASS (camel_type_get_global_classfuncs (camel_folder_get_type ()));
	
	/* virtual method definition */
	
	/* virtual method overload */
	camel_folder_class->refresh_info = digest_refresh_info;
	camel_folder_class->sync = digest_sync;
	camel_folder_class->get_full_name = digest_get_full_name;
	
	camel_folder_class->get_uids = digest_get_uids;
	camel_folder_class->free_uids = digest_free_uids;
	camel_folder_class->get_message_info = digest_get_message_info;
	
	camel_folder_class->get_message = digest_get_message;
	camel_folder_class->append_message = digest_append_message;
	camel_folder_class->transfer_messages_to = digest_transfer_messages_to;
}

static void
camel_digest_folder_init (gpointer object, gpointer klass)
{
	CamelDigestFolder *digest_folder = CAMEL_DIGEST_FOLDER (object);
	CamelFolder *folder = CAMEL_FOLDER (object);
	
	folder->folder_flags |= CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY;
	
	digest_folder->priv = g_new0 (struct _CamelDigestFolderPrivate, 1);
	digest_folder->priv->info_hash = g_hash_table_new (g_str_hash, g_str_equal);
}

static void           
digest_finalize (CamelObject *object)
{
	CamelDigestFolder *digest_folder = CAMEL_DIGEST_FOLDER (object);
	GPtrArray *summary;
	
	camel_object_unref (CAMEL_OBJECT (digest_folder->priv->message));
	
	g_hash_table_destroy (digest_folder->priv->info_hash);
	
	summary = digest_folder->priv->summary;
	if (summary) {
		int i;
		
		for (i = 0; i < summary->len; i++)
			camel_message_info_free (summary->pdata[i]);
		
		g_ptr_array_free (summary, TRUE);
	}
	
	if (digest_folder->priv->uids)
		g_ptr_array_free (digest_folder->priv->uids, TRUE);
	
	g_free (digest_folder->priv);
}

CamelType
camel_digest_folder_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (CAMEL_FOLDER_TYPE,
					    "CamelDigestFolder",
					    sizeof (CamelDigestFolder),
					    sizeof (CamelDigestFolderClass),
					    (CamelObjectClassInitFunc) camel_digest_folder_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_digest_folder_init,
					    (CamelObjectFinalizeFunc) digest_finalize);
	}
	
	return type;
}

static gboolean
multipart_contains_message_parts (CamelMultipart *multipart)
{
	gboolean has_message_parts = FALSE;
	CamelDataWrapper *wrapper;
	CamelMimePart *part;
	int i, parts;
	
	parts = camel_multipart_get_number (multipart);
	for (i = 0; i < parts && !has_message_parts; i++) {
		part = camel_multipart_get_part (multipart, i);
		wrapper = camel_medium_get_content_object (CAMEL_MEDIUM (part));
		if (CAMEL_IS_MULTIPART (wrapper)) {
			has_message_parts = multipart_contains_message_parts (CAMEL_MULTIPART (wrapper));
		} else if (CAMEL_IS_MIME_MESSAGE (wrapper)) {
			has_message_parts = TRUE;
		}
	}
	
	return has_message_parts;
}

CamelFolder *
camel_digest_folder_new (CamelStore *parent_store, CamelMimeMessage *message)
{
	CamelDigestFolder *digest_folder;
	CamelDataWrapper *wrapper;
	CamelFolder *folder;
	
	wrapper = camel_medium_get_content_object (CAMEL_MEDIUM (message));
	if (!wrapper || !CAMEL_IS_MULTIPART (wrapper))
		return NULL;
	
	/* Make sure we have a multipart/digest subpart or at least some message/rfc822 attachments... */
	if (!header_content_type_is (CAMEL_MIME_PART (message)->content_type, "multipart", "digest")) {
		if (!multipart_contains_message_parts (CAMEL_MULTIPART (wrapper)))
			return NULL;
	}
	
	folder = CAMEL_FOLDER (camel_object_new (camel_digest_folder_get_type ()));
	digest_folder = CAMEL_DIGEST_FOLDER (folder);
	
	camel_folder_construct (folder, parent_store, "folder_name", "short_name");
	
	camel_object_ref (CAMEL_OBJECT (message));
	digest_folder->priv->message = message;
	
	return folder;
}

static void
digest_refresh_info (CamelFolder *folder, CamelException *ex)
{
	
}

static void
digest_sync (CamelFolder *folder, guint32 flags, CamelException *ex)
{
	/* no-op */
}

static void
digest_add_multipart (CamelMultipart *multipart, GPtrArray *summary,
		      GPtrArray *uids, GHashTable *info_hash,
		      const char *preuid)
{
	CamelDataWrapper *wrapper;
	CamelMessageInfo *info;
	CamelMimePart *part;
	int parts, i;
	char *uid;
	
	parts = camel_multipart_get_number (multipart);
	for (i = 0; i < parts; i++) {
		part = camel_multipart_get_part (multipart, i);
		
		wrapper = camel_medium_get_content_object (CAMEL_MEDIUM (part));
		
		if (CAMEL_IS_MULTIPART (wrapper)) {
			uid = g_strdup_printf ("%s%d.", preuid, i);
			digest_add_multipart (CAMEL_MULTIPART (wrapper),
					      summary, uids, info_hash, uid);
			g_free (uid);
			continue;
		} else if (!CAMEL_IS_MIME_MESSAGE (wrapper)) {
			continue;
		}
		
		info = camel_message_info_new_from_header (CAMEL_MIME_PART (wrapper)->headers);
		
		uid = g_strdup_printf ("%s%d", preuid, i);
		camel_message_info_set_uid (info, g_strdup (uid));
		
		g_ptr_array_add (uids, uid);
		g_ptr_array_add (summary, info);
		g_hash_table_insert (info_hash, uid, info);
	}
}

static GPtrArray *
digest_get_uids (CamelFolder *folder)
{
	CamelDigestFolder *digest_folder = CAMEL_DIGEST_FOLDER (folder);
	CamelDataWrapper *wrapper;
	GHashTable *info_hash;
	GPtrArray *summary;
	GPtrArray *uids;
	
	if (digest_folder->priv->uids)
		return digest_folder->priv->uids;
	
	uids = g_ptr_array_new ();
	summary = g_ptr_array_new ();
	info_hash = digest_folder->priv->info_hash;
	
	wrapper = camel_medium_get_content_object (CAMEL_MEDIUM (digest_folder->priv->message));
	digest_add_multipart (CAMEL_MULTIPART (wrapper), summary, uids, info_hash, "");
	
	digest_folder->priv->uids = uids;
	digest_folder->priv->summary = summary;
	
	return uids;
}

static void
digest_free_uids (CamelFolder *folder, GPtrArray *uids)
{
	/* no-op */
}

static CamelMessageInfo *
digest_get_message_info (CamelFolder *folder, const char *uid)
{
	CamelDigestFolder *digest = CAMEL_DIGEST_FOLDER (folder);
	
	return g_hash_table_lookup (digest->priv->info_hash, uid);
}

static const char *
digest_get_full_name (CamelFolder *folder)
{
	return folder->full_name;
}

static void
digest_append_message (CamelFolder *folder, CamelMimeMessage *message,
		       const CamelMessageInfo *info, char **appended_uid,
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
digest_get_message (CamelFolder *folder, const char *uid, CamelException *ex)
{
	CamelDigestFolder *digest = CAMEL_DIGEST_FOLDER (folder);
	CamelDataWrapper *wrapper;
	CamelMimeMessage *message;
	CamelMimePart *part;
	char *subuid;
	int id;
	
	part = CAMEL_MIME_PART (digest->priv->message);
	wrapper = camel_medium_get_content_object (CAMEL_MEDIUM (part));
	
	do {
		id = strtoul (uid, &subuid, 10);
		if (!CAMEL_IS_MULTIPART (wrapper))
			return NULL;
		
		part = camel_multipart_get_part (CAMEL_MULTIPART (wrapper), id);
		wrapper = camel_medium_get_content_object (CAMEL_MEDIUM (part));
		uid = subuid + 1;
	} while (*subuid == '.');
	
	if (!CAMEL_IS_MIME_MESSAGE (wrapper))
		return NULL;
	
	message = CAMEL_MIME_MESSAGE (wrapper);
	camel_object_ref (CAMEL_OBJECT (message));
	
	return message;
}
