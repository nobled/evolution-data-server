/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-disco-folder.c: abstract class for a disconnectable folder */

/* 
 * Authors: Dan Winship <danw@ximian.com>
 *
 * Copyright (C) 2001 Ximian, Inc.
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

#include "camel-disco-folder.h"
#include "camel-disco-store.h"
#include "camel-exception.h"

#define CF_CLASS(o) (CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS (o)))
#define CDF_CLASS(o) (CAMEL_DISCO_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS (o)))

static CamelFolderClass *parent_class = NULL;

static void disco_refresh_info (CamelFolder *folder, CamelException *ex);
static void disco_sync (CamelFolder *folder, guint32 flags, CamelException *ex);

static void disco_append_message (CamelFolder *folder, CamelMimeMessage *message,
				  const CamelMessageInfo *info, char **appended_uid, CamelException *ex);
static void disco_transfer_messages_to (CamelFolder *source, GPtrArray *uids,
					CamelFolder *destination,
					GPtrArray **transferred_uids,
					gboolean delete_originals,
					CamelException *ex);

static void disco_cache_message       (CamelDiscoFolder *disco_folder,
				       const char *uid, CamelException *ex);
static void disco_prepare_for_offline (CamelDiscoFolder *disco_folder,
				       const char *expression,
				       CamelException *ex);

static void
camel_disco_folder_class_init (CamelDiscoFolderClass *camel_disco_folder_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS (camel_disco_folder_class);

	parent_class = CAMEL_FOLDER_CLASS (camel_type_get_global_classfuncs (camel_folder_get_type ()));

	/* virtual method definition */
	camel_disco_folder_class->cache_message = disco_cache_message;
	camel_disco_folder_class->prepare_for_offline = disco_prepare_for_offline;

	/* virtual method overload */
	camel_folder_class->refresh_info = disco_refresh_info;
	camel_folder_class->sync = disco_sync;

	camel_folder_class->append_message = disco_append_message;
	camel_folder_class->transfer_messages_to = disco_transfer_messages_to;
}

CamelType
camel_disco_folder_get_type (void)
{
	static CamelType camel_disco_folder_type = CAMEL_INVALID_TYPE;

	if (camel_disco_folder_type == CAMEL_INVALID_TYPE) {
		camel_disco_folder_type = camel_type_register (
			CAMEL_FOLDER_TYPE, "CamelDiscoFolder",
			sizeof (CamelDiscoFolder),
			sizeof (CamelDiscoFolderClass),
			(CamelObjectClassInitFunc) camel_disco_folder_class_init,
			NULL, NULL, NULL);
	}

	return camel_disco_folder_type;
}


static void
disco_refresh_info (CamelFolder *folder, CamelException *ex)
{
	if (camel_disco_store_status (CAMEL_DISCO_STORE (folder->parent_store)) != CAMEL_DISCO_STORE_ONLINE)
		return;
	CDF_CLASS (folder)->refresh_info_online (folder, ex);
}

static void
disco_expunge_uids (CamelFolder *folder, GPtrArray *uids, CamelException *ex)
{
	CamelDiscoStore *disco = CAMEL_DISCO_STORE (folder->parent_store);

	if (uids->len == 0)
		return;

	switch (camel_disco_store_status (disco)) {
	case CAMEL_DISCO_STORE_ONLINE:
		CDF_CLASS (folder)->expunge_uids_online (folder, uids, ex);
		break;

	case CAMEL_DISCO_STORE_OFFLINE:
		CDF_CLASS (folder)->expunge_uids_offline (folder, uids, ex);
		break;

	case CAMEL_DISCO_STORE_RESYNCING:
		CDF_CLASS (folder)->expunge_uids_resyncing (folder, uids, ex);
		break;
	}
}

static void
disco_sync (CamelFolder *folder, guint32 flags, CamelException *ex)
{
	if (flags & CAMEL_STORE_SYNC_EXPUNGE) {
		GPtrArray *uids;
		int i, count;
		CamelMessageInfo *info;

		/* this code is a load of crap because expunge_uid's could expunge anything */
		uids = g_ptr_array_new ();
		count = camel_folder_summary_count (folder->summary);
		for (i = 0; i < count; i++) {
			info = camel_folder_summary_index (folder->summary, i);
			if (info->flags & CAMEL_MESSAGE_DELETED)
				g_ptr_array_add (uids, g_strdup (camel_message_info_uid (info)));
			camel_folder_summary_info_free (folder->summary, info);
		}

		disco_expunge_uids (folder, uids, ex);

		for (i = 0; i < uids->len; i++)
			g_free (uids->pdata[i]);
		g_ptr_array_free (uids, TRUE);
		if (camel_exception_is_set (ex))
			return;
	}

	switch (camel_disco_store_status (CAMEL_DISCO_STORE (folder->parent_store))) {
	case CAMEL_DISCO_STORE_ONLINE:
		CDF_CLASS (folder)->sync_online (folder, ex);
		break;

	case CAMEL_DISCO_STORE_OFFLINE:
		CDF_CLASS (folder)->sync_offline (folder, ex);
		break;

	case CAMEL_DISCO_STORE_RESYNCING:
		CDF_CLASS (folder)->sync_resyncing (folder, ex);
		break;
	}
}

static void
disco_append_message (CamelFolder *folder, CamelMimeMessage *message,
		      const CamelMessageInfo *info, char **appended_uid,
		      CamelException *ex)
{
	CamelDiscoStore *disco = CAMEL_DISCO_STORE (folder->parent_store);

	switch (camel_disco_store_status (disco)) {
	case CAMEL_DISCO_STORE_ONLINE:
		CDF_CLASS (folder)->append_online (folder, message, info,
						   appended_uid, ex);
		break;

	case CAMEL_DISCO_STORE_OFFLINE:
		CDF_CLASS (folder)->append_offline (folder, message, info,
						    appended_uid, ex);
		break;

	case CAMEL_DISCO_STORE_RESYNCING:
		CDF_CLASS (folder)->append_resyncing (folder, message, info,
						      appended_uid, ex);
		break;
	}
}

static void
disco_transfer_messages_to (CamelFolder *source, GPtrArray *uids,
			    CamelFolder *dest, GPtrArray **transferred_uids,
			    gboolean delete_originals, CamelException *ex)
{
	CamelDiscoStore *disco = CAMEL_DISCO_STORE (source->parent_store);

	switch (camel_disco_store_status (disco)) {
	case CAMEL_DISCO_STORE_ONLINE:
		CDF_CLASS (source)->transfer_online (source, uids,
						     dest, transferred_uids,
						     delete_originals, ex);
		break;

	case CAMEL_DISCO_STORE_OFFLINE:
		CDF_CLASS (source)->transfer_offline (source, uids,
						      dest, transferred_uids,
						      delete_originals, ex);
		break;

	case CAMEL_DISCO_STORE_RESYNCING:
		CDF_CLASS (source)->transfer_resyncing (source, uids,
							dest, transferred_uids,
							delete_originals, ex);
		break;
	}
}


/**
 * camel_disco_folder_expunge_uids:
 * @folder: a (disconnectable) folder
 * @uids: array of UIDs to expunge
 * @ex: a CamelException
 *
 * This expunges the messages in @uids from @folder. It should take
 * whatever steps are needed to avoid expunging any other messages,
 * although in some cases it may not be possible to avoid expunging
 * messages that are marked deleted by another client at the same time
 * as the expunge_uids call is running.
 **/
void
camel_disco_folder_expunge_uids (CamelFolder *folder, GPtrArray *uids,
				 CamelException *ex)
{
	disco_expunge_uids (folder, uids, ex);
}


static void
disco_cache_message (CamelDiscoFolder *disco_folder, const char *uid,
		     CamelException *ex)
{
	g_warning ("CamelDiscoFolder::cache_message not implemented for `%s'",
		   camel_type_to_name (CAMEL_OBJECT_GET_TYPE (disco_folder)));
}

/**
 * camel_disco_folder_cache_message:
 * @disco_folder: the folder
 * @uid: the UID of the message to cache
 * @ex: a CamelException
 *
 * Requests that @disco_folder cache message @uid to disk.
 **/
void
camel_disco_folder_cache_message (CamelDiscoFolder *disco_folder,
				  const char *uid, CamelException *ex)
{
	CDF_CLASS (disco_folder)->cache_message (disco_folder, uid, ex);
}


static void
disco_prepare_for_offline (CamelDiscoFolder *disco_folder,
			   const char *expression,
			   CamelException *ex)
{
	CamelFolder *folder = CAMEL_FOLDER (disco_folder);
	GPtrArray *uids;
	int i;

	camel_operation_start(NULL, _("Preparing folder '%s' for offline"), folder->full_name);

	if (expression)
		uids = camel_folder_search_by_expression (folder, expression, ex);
	else
		uids = camel_folder_get_uids (folder);

	if (!uids) {
		camel_operation_end(NULL);
		return;
	}

	for (i = 0; i < uids->len; i++) {
		int pc = i * 100 / uids->len;

		camel_disco_folder_cache_message (disco_folder, uids->pdata[i], ex);
		camel_operation_progress(NULL, pc);
		if (camel_exception_is_set (ex))
			break;
	}

	if (expression)
		camel_folder_search_free (folder, uids);
	else
		camel_folder_free_uids (folder, uids);

	camel_operation_end(NULL);
}

/**
 * camel_disco_folder_prepare_for_offline:
 * @disco_folder: the folder
 * @expression: an expression describing messages to synchronize, or %NULL
 * if all messages should be sync'ed.
 * @ex: a CamelException
 *
 * This prepares @disco_folder for offline operation, by downloading
 * the bodies of all messages described by @expression (using the
 * same syntax as camel_folder_search_by_expression() ).
 **/
void 
camel_disco_folder_prepare_for_offline (CamelDiscoFolder *disco_folder,
					const char *expression,
					CamelException *ex)
{
	g_return_if_fail (CAMEL_IS_DISCO_FOLDER (disco_folder));

	CDF_CLASS (disco_folder)->prepare_for_offline (disco_folder, expression, ex);
}
