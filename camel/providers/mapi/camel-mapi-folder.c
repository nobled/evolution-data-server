/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Johnny Jacob <jjohnny@novell.com>
 *   
 * Copyright (C) 2007, Novell Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 3 of the GNU Lesser General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <pthread.h>
#include <string.h>
#include <time.h>

#include <camel/camel-folder-search.h>
#include <camel/camel-mime-part.h>
#include <camel/camel-mime-utils.h>
#include <camel/camel-string-utils.h>
#include <camel/camel-object.h>
#include <camel/camel-mime-message.h>
#include <camel/camel-data-wrapper.h>
#include <camel/camel-multipart.h>
#include <camel/camel-private.h>
#include <camel/camel-stream-buffer.h>
#include <camel/camel-stream-mem.h>
#include <camel/camel-debug.h>

#include <libmapi/libmapi.h>
#include <exchange-mapi-defs.h>
#include <exchange-mapi-utils.h>

#include "camel-mapi-store.h"
#include "camel-mapi-folder.h"
#include "camel-mapi-private.h"
#include "camel-mapi-summary.h"

#define DEBUG_FN( ) printf("----%u %s\n", (unsigned int)pthread_self(), __FUNCTION__);
#define d(x)

static CamelOfflineFolderClass *parent_class = NULL;

struct _CamelMapiFolderPrivate {
	
#ifdef ENABLE_THREADS
	GStaticMutex search_lock;	/* for locking the search object */
	GStaticRecMutex cache_lock;	/* for locking the cache object */
#endif

};

/*for syncing flags back to server*/
typedef struct {
	guint32 changed;
	guint32 bits;
} flags_diff_t;

static CamelMimeMessage *mapi_folder_item_to_msg( CamelFolder *folder, MapiItem *item, CamelException *ex );

static GPtrArray *
mapi_folder_search_by_expression (CamelFolder *folder, const char *expression, CamelException *ex)
{
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER(folder);
	GPtrArray *matches;

	CAMEL_MAPI_FOLDER_LOCK(mapi_folder, search_lock);
	camel_folder_search_set_folder (mapi_folder->search, folder);
	matches = camel_folder_search_search(mapi_folder->search, expression, NULL, ex);
	CAMEL_MAPI_FOLDER_UNLOCK(mapi_folder, search_lock);

	return matches;
}


static int
mapi_getv (CamelObject *object, CamelException *ex, CamelArgGetV *args)
{
	CamelFolder *folder = (CamelFolder *)object;
	int i, count = 0;
	guint32 tag;

	for (i=0 ; i<args->argc ; i++) {
		CamelArgGet *arg = &args->argv[i];

		tag = arg->tag;

		switch (tag & CAMEL_ARG_TAG) {

			case CAMEL_OBJECT_ARG_DESCRIPTION:
				if (folder->description == NULL) {
					CamelURL *uri = ((CamelService *)folder->parent_store)->url;

					folder->description = g_strdup_printf("%s@%s:%s", uri->user, uri->host, folder->full_name);
				}
				*arg->ca_str = folder->description;
				break;
			default:
				count++;
				continue;
		}

		arg->tag = (tag & CAMEL_ARG_TYPE) | CAMEL_ARG_IGNORE;
	}

	if (count)
		return ((CamelObjectClass *)parent_class)->getv(object, ex, args);

	return 0;

}

static void
mapi_refresh_info(CamelFolder *folder, CamelException *ex)
{
	CamelStoreInfo *si;
	/*
	 * Checking for the summary->time_string here since the first the a
	 * user views a folder, the read cursor is in progress, and the getQM
	 * should not interfere with the process
	 */
	//	if (summary->time_string && (strlen (summary->time_string) > 0))  {
	if(1){
		mapi_refresh_folder(folder, ex);
		si = camel_store_summary_path ((CamelStoreSummary *)((CamelMapiStore *)folder->parent_store)->summary, folder->full_name);

		if (si) {
			guint32 unread, total;
			camel_object_get (folder, NULL, CAMEL_FOLDER_TOTAL, &total, CAMEL_FOLDER_UNREAD, &unread, NULL);
			if (si->total != total || si->unread != unread) {
				si->total = total;
				si->unread = unread;
				camel_store_summary_touch ((CamelStoreSummary *)((CamelMapiStore *)folder->parent_store)->summary);
			}
			camel_store_summary_info_free ((CamelStoreSummary *)((CamelMapiStore *)folder->parent_store)->summary, si);
		}
		camel_folder_summary_save (folder->summary);
		camel_store_summary_save ((CamelStoreSummary *)((CamelMapiStore *)folder->parent_store)->summary);
	} else {
		/* We probably could not get the messages the first time. (get_folder) failed???!
		 * so do a get_folder again. And hope that it works
		 */
		g_print("Reloading folder...something wrong with the summary....\n");
	}
	//#endif

}

static void 
mapi_item_free (MapiItem *item)
{
	g_free (item->header.subject);
	g_free (item->header.from);
	g_free (item->header.to);
	g_free (item->header.cc);
	g_free (item->header.bcc);

	exchange_mapi_util_free_attachment_list (&item->attachments);
	exchange_mapi_util_free_stream_list (&item->generic_streams);
}
static gboolean
fetch_items_cb (struct mapi_SPropValue_array *array, const mapi_id_t fid, const mapi_id_t mid, 
		GSList *streams, GSList *recipients, GSList *attachments, gpointer data)
{
	//CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER(data);
	GSList **slist = (GSList **)data;

	long *flags;
	struct FILETIME *delivery_date;
	NTTIME ntdate;

	MapiItem *item = g_new0(MapiItem , 1);

	if (camel_debug_start("mapi:folder")) {
		exchange_mapi_debug_property_dump (array);
		camel_debug_end();
	}

	item->fid = fid;
	item->mid = mid;

	item->header.subject = g_strdup (find_mapi_SPropValue_data (array, PR_NORMALIZED_SUBJECT));
	item->header.to = g_strdup (find_mapi_SPropValue_data (array, PR_DISPLAY_TO));
	item->header.cc = g_strdup (find_mapi_SPropValue_data (array, PR_DISPLAY_CC));
	item->header.bcc = g_strdup (find_mapi_SPropValue_data (array, PR_DISPLAY_BCC));
	item->header.from = g_strdup (find_mapi_SPropValue_data (array, PR_SENT_REPRESENTING_NAME));
	item->header.size = *(glong *)(find_mapi_SPropValue_data (array, PR_MESSAGE_SIZE));

	delivery_date = (struct FILETIME *)find_mapi_SPropValue_data(array, PR_MESSAGE_DELIVERY_TIME);
	if (delivery_date) {
		ntdate = delivery_date->dwHighDateTime;
		ntdate = ntdate << 32;
		ntdate |= delivery_date->dwLowDateTime;
		item->header.recieved_time = nt_time_to_unix(ntdate);
	}

	flags = (long *)find_mapi_SPropValue_data (array, PR_MESSAGE_FLAGS);
	if ((*flags & MSGFLAG_READ) != 0)
		item->header.flags |= CAMEL_MESSAGE_SEEN;
	if ((*flags & MSGFLAG_HASATTACH) != 0)
		item->header.flags |= CAMEL_MESSAGE_ATTACHMENTS;

	*slist = g_slist_append (*slist, item);

	return TRUE;
}

static void
mapi_update_cache (CamelFolder *folder, GSList *list, CamelException *ex, gboolean uid_flag) 
{
	CamelMapiMessageInfo *mi = NULL;
	CamelMessageInfo *pmi = NULL;
	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE (folder->parent_store);

	guint32 status_flags = 0;
	CamelFolderChangeInfo *changes = NULL;
	gboolean exists = FALSE;
	GString *str = g_string_new (NULL);
	const gchar *folder_id = NULL;
	GSList *item_list = list;
	int total_items = g_slist_length (item_list), i=0;

	changes = camel_folder_change_info_new ();
	folder_id = camel_mapi_store_folder_id_lookup (mapi_store, folder->full_name);

	if (!folder_id) {
		d(printf("\nERROR - Folder id not present. Cannot refresh info\n"));
		camel_folder_change_info_free (changes);
		return;
	}

	camel_operation_start (NULL, _("Fetching summary information for new messages in %s"), folder->name);

	for ( ; item_list != NULL ; item_list = g_slist_next (item_list) ) {
		MapiItem *temp_item ;
		MapiItem *item;
		guint64 id;

		exists = FALSE;
		status_flags = 0;

		if (uid_flag == FALSE) {
 			temp_item = (MapiItem *)item_list->data;
			id = temp_item->mid;
			item = temp_item;
		}

		camel_operation_progress (NULL, (100*i)/total_items);

		/************************ First populate summary *************************/
		mi = NULL;
		pmi = NULL;
		char *msg_uid = exchange_mapi_util_mapi_ids_to_uid (item->fid, item->mid);
		pmi = camel_folder_summary_uid (folder->summary, msg_uid);

		if (pmi) {
			exists = TRUE;
			camel_message_info_ref (pmi);
			mi = (CamelMapiMessageInfo *)pmi;
		}

		if (!exists) {
			mi = (CamelMapiMessageInfo *)camel_message_info_new (folder->summary); 
			if (mi->info.content == NULL) {
				mi->info.content = camel_folder_summary_content_info_new (folder->summary);
				mi->info.content->type = camel_content_type_new ("multipart", "mixed");	
			}
		}
		
		mi->info.flags = item->header.flags;

		if (!exists) {
			mi->info.uid = g_strdup (exchange_mapi_util_mapi_ids_to_uid(item->fid, item->mid));
			mi->info.subject = camel_pstring_strdup(item->header.subject);
			mi->info.date_sent = mi->info.date_received = item->header.recieved_time;
			mi->info.from = camel_pstring_strdup (item->header.from);
			mi->info.to = camel_pstring_strdup (item->header.to);
			mi->info.size = (guint32) item->header.size;
		}

		if (exists) {
			camel_folder_change_info_change_uid (changes, mi->info.uid);
			camel_message_info_free (pmi);
		} else {
			camel_folder_summary_add (folder->summary,(CamelMessageInfo *)mi);
			camel_folder_change_info_add_uid (changes, mi->info.uid);
			camel_folder_change_info_recent_uid (changes, mi->info.uid);
		}

		/********************* Summary ends *************************/
		if (!strcmp (folder->full_name, "Junk Mail"))
			continue;

		g_free (msg_uid);
		i++;
	}
	camel_operation_end (NULL);

	g_string_free (str, TRUE);
	camel_object_trigger_event (folder, "folder_changed", changes);

	camel_folder_change_info_free (changes);
}

static void 
mapi_sync_summary (CamelFolder *folder, CamelException *ex)
{
	camel_folder_summary_save (folder->summary);
	camel_store_summary_touch ((CamelStoreSummary *)((CamelMapiStore *)folder->parent_store)->summary);
	camel_store_summary_save ((CamelStoreSummary *)((CamelMapiStore *)folder->parent_store)->summary);
}

static void
mapi_utils_do_flags_diff (flags_diff_t *diff, guint32 old, guint32 _new)
{
	diff->changed = old ^ _new;
	diff->bits = _new & diff->changed;
}

static void
mapi_sync (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE (folder->parent_store);
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER (folder);
	CamelMessageInfo *info = NULL;
	CamelMapiMessageInfo *mapi_info = NULL;

	GSList *read_items = NULL, *unread_items = NULL;
	flags_diff_t diff, unset_flags;
	const char *folder_id;
	mapi_id_t fid, deleted_items_fid;
	int count, i;

	GSList *deleted_items, *deleted_head;
	deleted_items = deleted_head = NULL;

	if (((CamelOfflineStore *) mapi_store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL || 
			((CamelService *)mapi_store)->status == CAMEL_SERVICE_DISCONNECTED) {
		mapi_sync_summary (folder, ex);
		return;
	}

	folder_id =  camel_mapi_store_folder_id_lookup (mapi_store, folder->full_name) ;
	exchange_mapi_util_mapi_id_from_string (folder_id, &fid);

	CAMEL_SERVICE_REC_LOCK (mapi_store, connect_lock);
	if (!camel_mapi_store_connected (mapi_store, ex)) {
		CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
		camel_exception_clear (ex);
		return;
	}
	CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);

	count = camel_folder_summary_count (folder->summary);
	CAMEL_MAPI_FOLDER_REC_LOCK (folder, cache_lock);
	for (i=0 ; i < count ; i++) {
		info = camel_folder_summary_index (folder->summary, i);
		mapi_info = (CamelMapiMessageInfo *) info;

		if (mapi_info && (mapi_info->info.flags & CAMEL_MESSAGE_FOLDER_FLAGGED)) {
			const char *uid;
			mapi_id_t *mid = g_new0 (mapi_id_t, 1); /* FIXME : */
			mapi_id_t temp_fid;

			uid = camel_message_info_uid (info);
			guint32 flags= camel_message_info_flags (info);

			/* Why are we getting so much noise here :-/ */
			if (!exchange_mapi_util_mapi_ids_from_uid (uid, &temp_fid, mid))
				continue;

			mapi_utils_do_flags_diff (&diff, mapi_info->server_flags, mapi_info->info.flags);
			mapi_utils_do_flags_diff (&unset_flags, flags, mapi_info->server_flags);

			diff.changed &= folder->permanent_flags;
			if (!diff.changed) {
				camel_message_info_free(info);
				continue;
			} else {
				if (diff.bits & CAMEL_MESSAGE_DELETED) {
					if (diff.bits & CAMEL_MESSAGE_SEEN) 
						read_items = g_slist_prepend (read_items, mid);
					if (deleted_items)
						deleted_items = g_slist_prepend (deleted_items, mid);
					else {
						g_slist_free (deleted_head);
						deleted_head = NULL;
						deleted_head = deleted_items = g_slist_prepend (deleted_items, mid);
					}

					CAMEL_SERVICE_REC_LOCK (mapi_store, connect_lock);

					}
				}
				
				if (diff.bits & CAMEL_MESSAGE_SEEN) {
					read_items = g_slist_prepend (read_items, mid);
				} else if (unset_flags.bits & CAMEL_MESSAGE_SEEN) {
					unread_items = g_slist_prepend (unread_items, mid);
				}
		}
		camel_message_info_free (info);
	}
	
	CAMEL_MAPI_FOLDER_REC_UNLOCK (folder, cache_lock);

	/* 
	   Sync up the READ changes before deleting the message. 
	   Note that if a message is marked as unread and then deleted,
	   Evo doesnt not take care of it, as I find that scenario to be impractical.
	*/

	if (read_items) {
		CAMEL_SERVICE_REC_LOCK (mapi_store, connect_lock);
		exchange_mapi_set_flags (0, fid, read_items, 0);
		CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
		g_slist_free (read_items);
	}

	if (deleted_items) {
		CAMEL_SERVICE_REC_LOCK (mapi_store, connect_lock);

		if (!strcmp (folder->full_name, "Deleted Items")) {
			exchange_mapi_remove_items (0, fid, deleted_items);
		} else {
			exchange_mapi_util_mapi_id_from_string (camel_mapi_store_folder_id_lookup (mapi_store, "Deleted Items"), &deleted_items_fid);
			exchange_mapi_move_items(fid, deleted_items_fid, deleted_items);
		}

		CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
	}
	/*Remove them from cache*/
	while (deleted_items) {
		char* deleted_msg_uid = g_strdup_printf ("%016llX%016llX", fid, *(mapi_id_t *)deleted_items->data);

		CAMEL_MAPI_FOLDER_REC_LOCK (folder, cache_lock);
		camel_folder_summary_remove_uid (folder->summary, deleted_msg_uid);
		camel_data_cache_remove(mapi_folder->cache, "cache", deleted_msg_uid, NULL);
		CAMEL_MAPI_FOLDER_REC_UNLOCK (folder, cache_lock);

		deleted_items = g_slist_next (deleted_items);
	}


	if (unread_items) {
		CAMEL_SERVICE_REC_LOCK (mapi_store, connect_lock);
		/* TODO */
		CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
		g_slist_free (unread_items);
	}

	if (expunge) {
		CAMEL_SERVICE_REC_LOCK (mapi_store, connect_lock);
		/* TODO */
		CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
	}

	CAMEL_SERVICE_REC_LOCK (mapi_store, connect_lock);
	mapi_sync_summary (folder, ex);
	CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
}


void
mapi_refresh_folder(CamelFolder *folder, CamelException *ex)
{

	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE (folder->parent_store);
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER (folder);

	gboolean is_proxy = folder->parent_store->flags & CAMEL_STORE_PROXY;
	gboolean is_locked = TRUE;
	gboolean status;
	GSList *item_list = NULL;
	const gchar *folder_id = NULL;

	const guint32 summary_prop_list[] = {
		PR_NORMALIZED_SUBJECT,
		PR_MESSAGE_SIZE,
		PR_MESSAGE_DELIVERY_TIME,
		PR_MESSAGE_FLAGS,
		PR_SENT_REPRESENTING_NAME,
		PR_DISPLAY_TO,
		PR_DISPLAY_CC,
		PR_DISPLAY_BCC
	};


	/* Sync-up the (un)read changes before getting updates,
	so that the getFolderList will reflect the most recent changes too */
	mapi_sync (folder, FALSE, ex);

	if (((CamelOfflineStore *) mapi_store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		g_warning ("In offline mode. Cannot refresh!!!\n");
		return;
	}

	//creating a copy
	folder_id = camel_mapi_store_folder_id_lookup (mapi_store, folder->full_name);
	if (!folder_id) {
		d(printf ("\nERROR - Folder id not present. Cannot refresh info for %s\n", folder->full_name));
		return;
	}

	if (camel_folder_is_frozen (folder) ) {
		mapi_folder->need_refresh = TRUE;
	}

	CAMEL_SERVICE_REC_LOCK (mapi_store, connect_lock);

	if (!camel_mapi_store_connected (mapi_store, ex))
		goto end1;

	/*Get the New Items*/
	if (!is_proxy) {
		mapi_id_t temp_folder_id;
		guint32 options = 0;
		CamelFolderInfo *fi = NULL;

		exchange_mapi_util_mapi_id_from_string (folder_id, &temp_folder_id);

		if (!camel_mapi_store_connected (mapi_store, ex)) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
					     _("This message is not available in offline mode."));
			goto end2;
		}

		fi = camel_store_get_folder_info (folder->parent_store, folder->full_name, 0, NULL);
		if (fi->flags & CAMEL_MAPI_FOLDER_PUBLIC)
			options |= MAPI_OPTIONS_USE_PFSTORE;

		status = exchange_mapi_connection_fetch_items  (temp_folder_id, NULL, 
								summary_prop_list, G_N_ELEMENTS (summary_prop_list), 
								NULL, NULL, 
								fetch_items_cb, &item_list, 
								options);

		if (!status) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Fetch items failed"));
			goto end2;
		}

		camel_folder_summary_touch (folder->summary);
		mapi_sync_summary (folder, ex);

		if (item_list)
			mapi_update_cache (folder, item_list, ex, FALSE);
	}


	CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
	is_locked = FALSE;

	g_slist_foreach (item_list, (GFunc) mapi_item_free, NULL);
	g_slist_free (item_list);
	item_list = NULL;
end2:
	//TODO:
end1:
	if (is_locked)
		CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
	return;

}

static const uint32_t camel_GetPropsList[] = {
	PR_FID, 
	PR_MID, 

	PR_MESSAGE_CLASS, 
	PR_MESSAGE_CLASS_UNICODE, 
	PR_MESSAGE_SIZE, 
	PR_MESSAGE_FLAGS, 
	PR_MESSAGE_DELIVERY_TIME, 
	PR_MSG_EDITOR_FORMAT, 

	PR_SUBJECT, 
	PR_SUBJECT_UNICODE, 
	PR_NORMALIZED_SUBJECT, 
	PR_NORMALIZED_SUBJECT_UNICODE, 
	PR_CONVERSATION_TOPIC, 
	PR_CONVERSATION_TOPIC_UNICODE, 

	PR_BODY, 
	PR_BODY_UNICODE, 
	PR_BODY_HTML, 
	PR_BODY_HTML_UNICODE, 

	PR_DISPLAY_TO, 
	PR_DISPLAY_TO_UNICODE, 
	PR_DISPLAY_CC, 
	PR_DISPLAY_CC_UNICODE, 
	PR_DISPLAY_BCC, 
	PR_DISPLAY_BCC_UNICODE, 

	PR_CREATION_TIME, 
	PR_LAST_MODIFICATION_TIME, 
	PR_PRIORITY, 
	PR_SENSITIVITY, 
	PR_START_DATE, 
	PR_END_DATE, 
	PR_RESPONSE_REQUESTED, 
	PR_OWNER_APPT_ID, 
	PR_PROCESSED, 

	PR_SENT_REPRESENTING_NAME, 
	PR_SENT_REPRESENTING_NAME_UNICODE, 
	PR_SENT_REPRESENTING_ADDRTYPE, 
	PR_SENT_REPRESENTING_ADDRTYPE_UNICODE, 
	PR_SENT_REPRESENTING_EMAIL_ADDRESS, 
	PR_SENT_REPRESENTING_EMAIL_ADDRESS_UNICODE, 

	PR_SENDER_NAME, 
	PR_SENDER_NAME_UNICODE, 
	PR_SENDER_ADDRTYPE, 
	PR_SENDER_ADDRTYPE_UNICODE, 
	PR_SENDER_EMAIL_ADDRESS, 
	PR_SENDER_EMAIL_ADDRESS_UNICODE, 

	PR_RCVD_REPRESENTING_NAME, 
	PR_RCVD_REPRESENTING_NAME_UNICODE, 
	PR_RCVD_REPRESENTING_ADDRTYPE, 
	PR_RCVD_REPRESENTING_ADDRTYPE_UNICODE, 
	PR_RCVD_REPRESENTING_EMAIL_ADDRESS, 
	PR_RCVD_REPRESENTING_EMAIL_ADDRESS_UNICODE
};

static gboolean 
camel_build_name_id (struct mapi_nameid *nameid, gpointer data)
{
	mapi_nameid_lid_add(nameid, 0x8501, PSETID_Common); 	// PT_LONG - ReminderMinutesBeforeStart
	mapi_nameid_lid_add(nameid, 0x8502, PSETID_Common); 	// PT_SYSTIME - ReminderTime
	mapi_nameid_lid_add(nameid, 0x8503, PSETID_Common); 	// PT_BOOLEAN - ReminderSet
	mapi_nameid_lid_add(nameid, 0x8506, PSETID_Common); 	// PT_BOOLEAN - Private
	mapi_nameid_lid_add(nameid, 0x8510, PSETID_Common); 	// PT_LONG - (context menu flags)
	mapi_nameid_lid_add(nameid, 0x8516, PSETID_Common); 	// PT_SYSTIME - CommonStart
	mapi_nameid_lid_add(nameid, 0x8517, PSETID_Common); 	// PT_SYSTIME - CommonEnd
	mapi_nameid_lid_add(nameid, 0x8560, PSETID_Common); 	// PT_SYSTIME - ReminderNextTime

	mapi_nameid_lid_add(nameid, 0x8201, PSETID_Appointment); 	// PT_LONG - ApptSequence
	mapi_nameid_lid_add(nameid, 0x8205, PSETID_Appointment); 	// PT_LONG - BusyStatus
	mapi_nameid_lid_add(nameid, 0x8208, PSETID_Appointment); 	// PT_UNICODE - Location
	mapi_nameid_lid_add(nameid, 0x820D, PSETID_Appointment); 	// PT_SYSTIME - Start/ApptStartWhole
	mapi_nameid_lid_add(nameid, 0x820E, PSETID_Appointment); 	// PT_SYSTIME - End/ApptEndWhole
	mapi_nameid_lid_add(nameid, 0x8213, PSETID_Appointment); 	// PT_LONG - Duration/ApptDuration
	mapi_nameid_lid_add(nameid, 0x8215, PSETID_Appointment); 	// PT_BOOLEAN - AllDayEvent (also called ApptSubType)
	mapi_nameid_lid_add(nameid, 0x8216, PSETID_Appointment); 	// PT_BINARY - (recurrence blob)
	mapi_nameid_lid_add(nameid, 0x8217, PSETID_Appointment); 	// PT_LONG - MeetingStatus
	mapi_nameid_lid_add(nameid, 0x8218, PSETID_Appointment); 	// PT_LONG - ResponseStatus
	mapi_nameid_lid_add(nameid, 0x8223, PSETID_Appointment); 	// PT_BOOLEAN - Recurring
	mapi_nameid_lid_add(nameid, 0x8224, PSETID_Appointment); 	// PT_LONG - IntendedBusyStatus
	mapi_nameid_lid_add(nameid, 0x8228, PSETID_Appointment); 	// PT_SYSTIME - RecurrenceBase
	mapi_nameid_lid_add(nameid, 0x8229, PSETID_Appointment); 	// PT_BOOLEAN - FInvited
	mapi_nameid_lid_add(nameid, 0x8231, PSETID_Appointment); 	// PT_LONG - RecurrenceType
	mapi_nameid_lid_add(nameid, 0x8232, PSETID_Appointment); 	// PT_STRING8 - RecurrencePattern
	mapi_nameid_lid_add(nameid, 0x8235, PSETID_Appointment); 	// PT_SYSTIME - (dtstart)(for recurring events UTC 12 AM of day of start)
	mapi_nameid_lid_add(nameid, 0x8236, PSETID_Appointment); 	// PT_SYSTIME - (dtend)(for recurring events UTC 12 AM of day of end)
	mapi_nameid_lid_add(nameid, 0x823A, PSETID_Appointment); 	// PT_BOOLEAN - AutoFillLocation
	mapi_nameid_lid_add(nameid, 0x8240, PSETID_Appointment); 	// PT_BOOLEAN - IsOnlineMeeting
	mapi_nameid_lid_add(nameid, 0x8257, PSETID_Appointment); 	// PT_BOOLEAN - ApptCounterProposal
	mapi_nameid_lid_add(nameid, 0x825E, PSETID_Appointment); 	// PT_BINARY - (timezone for dtstart)
	mapi_nameid_lid_add(nameid, 0x825F, PSETID_Appointment); 	// PT_BINARY - (timezone for dtend)

	mapi_nameid_lid_add(nameid, 0x0002, PSETID_Meeting); 		// PT_UNICODE - Where
	mapi_nameid_lid_add(nameid, 0x0003, PSETID_Meeting); 		// PT_BINARY - GlobalObjectId
	mapi_nameid_lid_add(nameid, 0x0005, PSETID_Meeting); 		// PT_BOOLEAN - IsRecurring
	mapi_nameid_lid_add(nameid, 0x000a, PSETID_Meeting); 		// PT_BOOLEAN - IsException 
	mapi_nameid_lid_add(nameid, 0x0023, PSETID_Meeting); 		// PT_BINARY - CleanGlobalObjectId
	mapi_nameid_lid_add(nameid, 0x0024, PSETID_Meeting); 		// PT_STRING8 - AppointmentMessageClass 
	mapi_nameid_lid_add(nameid, 0x0026, PSETID_Meeting); 		// PT_LONG - MeetingType

	/* These probably would never be used from Evolution */
//	mapi_nameid_lid_add(nameid, 0x8200, PSETID_Appointment); 	// PT_BOOLEAN - SendAsICAL
//	mapi_nameid_lid_add(nameid, 0x8202, PSETID_Appointment); 	// PT_SYSTIME - ApptSequenceTime
//	mapi_nameid_lid_add(nameid, 0x8214, PSETID_Appointment); 	// PT_LONG - Label
//	mapi_nameid_lid_add(nameid, 0x8234, PSETID_Appointment); 	// PT_STRING8 - display TimeZone
//	mapi_nameid_lid_add(nameid, 0x8238, PSETID_Appointment); 	// PT_STRING8 - AllAttendees
//	mapi_nameid_lid_add(nameid, 0x823B, PSETID_Appointment); 	// PT_STRING8 - ToAttendeesString (dupe PR_DISPLAY_TO)
//	mapi_nameid_lid_add(nameid, 0x823C, PSETID_Appointment); 	// PT_STRING8 - CCAttendeesString (dupe PR_DISPLAY_CC)

	mapi_nameid_lid_add(nameid, 0x8101, PSETID_Task); 	// PT_LONG - Status
	mapi_nameid_lid_add(nameid, 0x8102, PSETID_Task); 	// PT_DOUBLE - PercentComplete
	mapi_nameid_lid_add(nameid, 0x8103, PSETID_Task); 	// PT_BOOLEAN - TeamTask
	mapi_nameid_lid_add(nameid, 0x8104, PSETID_Task); 	// PT_SYSTIME - StartDate/TaskStartDate
	mapi_nameid_lid_add(nameid, 0x8105, PSETID_Task); 	// PT_SYSTIME - DueDate/TaskDueDate
	mapi_nameid_lid_add(nameid, 0x810F, PSETID_Task); 	// PT_SYSTIME - DateCompleted
//	mapi_nameid_lid_add(nameid, 0x8116, PSETID_Task); 	// PT_BINARY - (recurrence blob)
	mapi_nameid_lid_add(nameid, 0x811C, PSETID_Task); 	// PT_BOOLEAN - Complete
	mapi_nameid_lid_add(nameid, 0x811F, PSETID_Task); 	// PT_STRING8 - Owner
	mapi_nameid_lid_add(nameid, 0x8121, PSETID_Task); 	// PT_STRING8 - Delegator
	mapi_nameid_lid_add(nameid, 0x8126, PSETID_Task); 	// PT_BOOLEAN - IsRecurring/TaskFRecur
	mapi_nameid_lid_add(nameid, 0x8127, PSETID_Task); 	// PT_STRING8 - Role
	mapi_nameid_lid_add(nameid, 0x8129, PSETID_Task); 	// PT_LONG - Ownership
	mapi_nameid_lid_add(nameid, 0x812A, PSETID_Task); 	// PT_LONG - DelegationState

	/* These probably would never be used from Evolution */
//	mapi_nameid_lid_add(nameid, 0x8110, PSETID_Task); 	// PT_LONG - ActualWork/TaskActualEffort
//	mapi_nameid_lid_add(nameid, 0x8111, PSETID_Task); 	// PT_LONG - TotalWork/TaskEstimatedEffort

	/* These probably would never be used from Evolution */
//	mapi_nameid_lid_add(nameid, 0x8B00, PSETID_Note); 	// PT_LONG - Color

	return TRUE;
}

static gboolean
fetch_item_cb 	(struct mapi_SPropValue_array *array, mapi_id_t fid, mapi_id_t mid, 
		GSList *streams, GSList *recipients, GSList *attachments, gpointer data)
{
	long *flags;
	struct FILETIME *delivery_date;
	const char *msg_class;
	NTTIME ntdate;
	ExchangeMAPIStream *body;

	MapiItem *item = g_new0(MapiItem , 1);

	MapiItem **i = (MapiItem **)data;

	if (camel_debug_start("mapi:folder")) {
		exchange_mapi_debug_property_dump (array);
		camel_debug_end();
	}

	item->fid = fid;
	item->mid = mid;

	item->header.subject = g_strdup (exchange_mapi_util_find_array_propval (array, PR_NORMALIZED_SUBJECT));
	item->header.to = g_strdup (exchange_mapi_util_find_array_propval (array, PR_DISPLAY_TO));
	item->header.cc = g_strdup (exchange_mapi_util_find_array_propval (array, PR_DISPLAY_CC));
	item->header.bcc = g_strdup (exchange_mapi_util_find_array_propval (array, PR_DISPLAY_BCC));
	item->header.from = g_strdup (exchange_mapi_util_find_array_propval (array, PR_SENT_REPRESENTING_NAME));
	item->header.size = *(glong *)(find_mapi_SPropValue_data (array, PR_MESSAGE_SIZE));

	msg_class = (const char *) exchange_mapi_util_find_array_propval (array, PR_MESSAGE_CLASS);
	if (g_str_has_prefix (msg_class, IPM_SCHEDULE_MEETING_PREFIX)) {
		gchar *appointment_body_str = NULL;
		appointment_body_str = exchange_mapi_cal_util_camel_helper (array, streams, recipients, attachments);

		body = g_new0(ExchangeMAPIStream, 1);
		body->proptag = PR_BODY;
		body->value = g_byte_array_new ();
		body->value = g_byte_array_append (body->value, appointment_body_str, g_utf8_strlen (appointment_body_str, -1));

		item->msg.body_parts = g_slist_append (item->msg.body_parts, body);

		item->is_cal = TRUE;
	} else { 
		if (!((body = exchange_mapi_util_find_stream (streams, PR_BODY_HTML)) || 
		      (body = exchange_mapi_util_find_stream (streams, PR_HTML))))
			body = exchange_mapi_util_find_stream (streams, PR_BODY);

		item->msg.body_parts = g_slist_append (item->msg.body_parts, body);

		item->is_cal = FALSE;
	}

	delivery_date = (struct FILETIME *)find_mapi_SPropValue_data(array, PR_MESSAGE_DELIVERY_TIME);
	if (delivery_date) {
		ntdate = delivery_date->dwHighDateTime;
		ntdate = ntdate << 32;
		ntdate |= delivery_date->dwLowDateTime;
		item->header.recieved_time = nt_time_to_unix(ntdate);
	}

	flags = (long *)find_mapi_SPropValue_data (array, PR_MESSAGE_FLAGS);
	if ((*flags & MSGFLAG_READ) != 0)
		item->header.flags |= CAMEL_MESSAGE_SEEN;
	if ((*flags & MSGFLAG_HASATTACH) != 0)
		item->header.flags |= CAMEL_MESSAGE_ATTACHMENTS;

	item->attachments = attachments;

	*i = item;

	return TRUE;
}


static void
mapi_msg_set_recipient_list (CamelMimeMessage *msg, MapiItem *item)
{
	CamelInternetAddress *addr = NULL;
	{
		char *tmp_addr = NULL;
		int index, len;
		
		addr = camel_internet_address_new();
		for (index = 0; item->header.to[index]; index += len){
			if (item->header.to[index] == ';')
				index++;
			for (len = 0; item->header.to[index + len] &&
				     item->header.to[index + len] != ';'; len++)
				;
			tmp_addr = malloc(/* tmp_addr, */ len + 1);
			memcpy(tmp_addr, item->header.to + index, len);
			tmp_addr[len] = 0;
			if (len) camel_internet_address_add(addr, tmp_addr, tmp_addr);
		}
		if (index != 0)
			camel_mime_message_set_recipients(msg, "To", addr);
	}
        /* modifing cc */
	{
		char *tmp_addr = NULL;
		int index, len;
		
		addr = camel_internet_address_new();
		for (index = 0; item->header.cc[index]; index += len){
			if (item->header.cc[index] == ';')
				index++;
			for (len = 0; item->header.cc[index + len] &&
				     item->header.cc[index + len] != ';'; len++)
				;
			tmp_addr = malloc(/* tmp_addr, */ len + 1);
			memcpy(tmp_addr, item->header.cc + index, len);
			tmp_addr[len] = 0;
			if (len) camel_internet_address_add(addr, tmp_addr, tmp_addr);
		}
		if (index != 0)
			camel_mime_message_set_recipients(msg, "Cc", addr);
	}
}


static void
mapi_populate_details_from_item (CamelMimeMessage *msg, MapiItem *item)
{
	char *temp_str = NULL;
	time_t recieved_time;
	CamelInternetAddress *addr = NULL;

	temp_str = item->header.subject;
	if(temp_str) 
		camel_mime_message_set_subject (msg, temp_str);

	recieved_time = item->header.recieved_time;

	int offset = 0;
	time_t actual_time = camel_header_decode_date (ctime(&recieved_time), &offset);
	camel_mime_message_set_date (msg, actual_time, offset);

	if (item->header.from) {
		/* add reply to */
		addr = camel_internet_address_new();
		camel_internet_address_add(addr, item->header.from, item->header.from);
		camel_mime_message_set_reply_to(msg, addr);
		
		/* add from */
		addr = camel_internet_address_new();
		camel_internet_address_add(addr, item->header.from, item->header.from);
		camel_mime_message_set_from(msg, addr);
	}
}


static void
mapi_populate_msg_body_from_item (CamelMultipart *multipart, MapiItem *item, ExchangeMAPIStream *body)
{
	CamelMimePart *part;

	part = camel_mime_part_new ();
	camel_mime_part_set_encoding(part, CAMEL_TRANSFER_ENCODING_8BIT);
	const char* type = NULL;
	
	if (body) { 
		if (item->is_cal)
			camel_mime_part_set_content(part, body->value->data, body->value->len, "text/calendar");
		else {
			type = (body->proptag == PR_BODY || body->proptag == PR_BODY_UNICODE) ? 
				"text/plain" : "text/html";
			camel_mime_part_set_content(part, body->value->data, body->value->len, type );
		}
	} else
		camel_mime_part_set_content(part, " ", strlen(" "), "text/html");

	camel_multipart_set_boundary (multipart, NULL);
	camel_multipart_add_part (multipart, part);
	camel_object_unref (part);
}


static CamelMimeMessage *
mapi_folder_item_to_msg( CamelFolder *folder,
		MapiItem *item,
		CamelException *ex )
{
	CamelMimeMessage *msg = NULL;
	CamelMultipart *multipart = NULL;

	GSList *attach_list = NULL;
	int errno;
	/* char *body = NULL; */
	ExchangeMAPIStream *body = NULL;
	GSList *body_part_list = NULL;
	const char *uid = NULL;

	attach_list = item->attachments;

	msg = camel_mime_message_new ();

	multipart = camel_multipart_new ();

	camel_mime_message_set_message_id (msg, uid);
	body_part_list = item->msg.body_parts;
	while (body_part_list){
	       body = body_part_list->data;
	       mapi_populate_msg_body_from_item (multipart, item, body);	       
	       body_part_list = g_slist_next (body_part_list);
	}


	/*Set recipient details*/
	mapi_msg_set_recipient_list (msg, item);
	mapi_populate_details_from_item (msg, item);

	if (attach_list) {
		GSList *al = attach_list;
		for (al = attach_list; al != NULL; al = al->next) {
			ExchangeMAPIAttachment *attach = (ExchangeMAPIAttachment *)al->data;
			ExchangeMAPIStream *stream = NULL;
			const char *filename, *mime_type; 
			CamelMimePart *part;

			filename = (const char *) exchange_mapi_util_find_SPropVal_array_propval(attach->lpProps, PR_ATTACH_LONG_FILENAME);
			if (!(filename && *filename))
				filename = (const char *) exchange_mapi_util_find_SPropVal_array_propval(attach->lpProps, PR_ATTACH_FILENAME);

			mime_type = (const char *) exchange_mapi_util_find_SPropVal_array_propval(attach->lpProps, PR_ATTACH_MIME_TAG);

			stream = exchange_mapi_util_find_stream (attach->streams, PR_ATTACH_DATA_BIN);

			printf("%s(%d):%s:Attachment --\n\tFileName : %s \n\tMIME Tag : %s\n\tLength : %d\n",
			       __FILE__, __LINE__, __PRETTY_FUNCTION__, 
				 filename, mime_type, stream ? stream->value->len : 0);

			if (!stream || stream->value->len <= 0) {
				continue;
			}
			part = camel_mime_part_new ();

			camel_mime_part_set_filename(part, g_strdup(filename));
			//Auto generate content-id
			camel_mime_part_set_content_id (part, NULL);
			camel_mime_part_set_content(part, stream->value->data, stream->value->len, mime_type);
			camel_content_type_set_param (((CamelDataWrapper *) part)->mime_type, "name", filename);

			camel_multipart_set_boundary(multipart, NULL);
			camel_multipart_add_part (multipart, part);
			camel_object_unref (part);
			
		}
		exchange_mapi_util_free_attachment_list (&attach_list);
	}

	camel_medium_set_content_object(CAMEL_MEDIUM (msg), CAMEL_DATA_WRAPPER(multipart));
	camel_object_unref (multipart);

	if (body)
		g_free (body);

	return msg;
}


static CamelMimeMessage *
mapi_folder_get_message( CamelFolder *folder, const char *uid, CamelException *ex )
{
	CamelMimeMessage *msg = NULL;
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER(folder);
	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE(folder->parent_store);
	CamelMapiMessageInfo *mi = NULL;

	CamelStream *stream, *cache_stream;
	int errno;

	/* see if it is there in cache */

	mi = (CamelMapiMessageInfo *) camel_folder_summary_uid (folder->summary, uid);
	if (mi == NULL) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				_("Cannot get message: %s\n  %s"), uid, _("No such message"));
		return NULL;
	}
	cache_stream  = camel_data_cache_get (mapi_folder->cache, "cache", uid, ex);
	stream = camel_stream_mem_new ();
	if (cache_stream) {
		msg = camel_mime_message_new ();
		camel_stream_reset (stream);
		camel_stream_write_to_stream (cache_stream, stream);
		camel_stream_reset (stream);
		if (camel_data_wrapper_construct_from_stream ((CamelDataWrapper *) msg, stream) == -1) {
			if (errno == EINTR) {
				camel_exception_setv (ex, CAMEL_EXCEPTION_USER_CANCEL, _("User canceled"));
				camel_object_unref (msg);
				camel_object_unref (cache_stream);
				camel_object_unref (stream);
				camel_message_info_free (&mi->info);
				return NULL;
			} else {
				camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot get message %s: %s"),
						uid, g_strerror (errno));
				camel_object_unref (msg);
				msg = NULL;
			}
		}
		camel_object_unref (cache_stream);
	}
	camel_object_unref (stream);

	if (msg != NULL) {
		camel_message_info_free (&mi->info);
		return msg;
	}

	if (((CamelOfflineStore *) mapi_store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				_("This message is not available in offline mode."));
		camel_message_info_free (&mi->info);
		return NULL;
	}

	/* Check if we are really offline */
	if (!camel_mapi_store_connected (mapi_store, ex)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				_("This message is not available in offline mode."));
		camel_message_info_free (&mi->info);
		return NULL;
	}

	mapi_id_t id_folder;
	mapi_id_t id_message;
	MapiItem *item = NULL;
	guint32 options = 0;
	CamelFolderInfo *fi = NULL;

	options = MAPI_OPTIONS_FETCH_ALL | MAPI_OPTIONS_GETBESTBODY ;
	exchange_mapi_util_mapi_ids_from_uid (uid, &id_folder, &id_message);

	fi = camel_store_get_folder_info (folder->parent_store, folder->full_name, 0, NULL);
	if (fi->flags & CAMEL_MAPI_FOLDER_PUBLIC)
		options |= MAPI_OPTIONS_USE_PFSTORE;

	exchange_mapi_connection_fetch_item (id_folder, id_message, 
					camel_GetPropsList, G_N_ELEMENTS (camel_GetPropsList), 
					camel_build_name_id, NULL, 
					fetch_item_cb, &item, 
					options);

	if (item == NULL) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Could not get message"));
		camel_message_info_free (&mi->info);
		return NULL;
	}

	msg = mapi_folder_item_to_msg (folder, item, ex);

	g_free (item);

	if (!msg) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Could not get message"));
		camel_message_info_free (&mi->info);

		return NULL;
	}

	/* add to cache */
	CAMEL_MAPI_FOLDER_REC_LOCK (folder, cache_lock);
	if ((cache_stream = camel_data_cache_add (mapi_folder->cache, "cache", uid, NULL))) {
		if (camel_data_wrapper_write_to_stream ((CamelDataWrapper *) msg, cache_stream) == -1
				|| camel_stream_flush (cache_stream) == -1)
			camel_data_cache_remove (mapi_folder->cache, "cache", uid, NULL);
		camel_object_unref (cache_stream);
	}

	CAMEL_MAPI_FOLDER_REC_UNLOCK (folder, cache_lock);

	camel_message_info_free (&mi->info);

	return msg;
}

static void
mapi_folder_search_free (CamelFolder *folder, GPtrArray *uids)
{
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER(folder);

	g_return_if_fail (mapi_folder->search);

	CAMEL_MAPI_FOLDER_LOCK(mapi_folder, search_lock);

	camel_folder_search_free_result (mapi_folder->search, uids);

	CAMEL_MAPI_FOLDER_UNLOCK(mapi_folder, search_lock);

}

static void
camel_mapi_folder_finalize (CamelObject *object)
{
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER (object);

	if (mapi_folder->priv)
		g_free(mapi_folder->priv);
	if (mapi_folder->cache)
		camel_object_unref (mapi_folder->cache);
}


static CamelMessageInfo*
mapi_get_message_info(CamelFolder *folder, const char *uid)
{ 
#if 0
	CamelMessageInfo	*msg_info = NULL;
	CamelMessageInfoBase	*mi = (CamelMessageInfoBase *)msg ;
	int			status = 0;
	oc_message_headers_t	headers;

	if (folder->summary) {
		msg_info = camel_folder_summary_uid(folder->summary, uid);
	}
	if (msg_info != NULL) {
		mi = (CamelMessageInfoBase *)msg_info ;
		return (msg_info);
	}
	msg_info = camel_message_info_new(folder->summary);
	mi = (CamelMessageInfoBase *)msg_info ;
	//TODO :
/* 	oc_message_headers_init(&headers); */
/* 	oc_thread_connect_lock(); */
/* 	status = oc_message_headers_get_by_id(&headers, uid); */
/* 	oc_thread_connect_unlock(); */

	if (headers.subject) mi->subject = (char *)camel_pstring_strdup(headers.subject);
	if (headers.from) mi->from = (char *)camel_pstring_strdup(headers.from);
	if (headers.to) mi->to = (char *)camel_pstring_strdup(headers.to);
	if (headers.cc) mi->cc = (char *)camel_pstring_strdup(headers.cc);
	mi->flags = headers.flags;


	mi->user_flags = NULL;
	mi->user_tags = NULL;
	mi->date_received = 0;
	mi->date_sent = headers.send;
	mi->content = NULL;
	mi->summary = folder->summary;
	if (uid) mi->uid = g_strdup(uid);
	oc_message_headers_release(&headers);
	return (msg);
#endif
	return NULL;
}

static void
mapi_expunge (CamelFolder *folder, CamelException *ex)
{
	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE(folder->parent_store);
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER (folder);
	CamelMapiMessageInfo *minfo;
	CamelMessageInfo *info;
	CamelFolderChangeInfo *changes;

	mapi_id_t fid;

	int i, count;
	gboolean delete = FALSE, status = FALSE;
	gchar *folder_id;
	GSList *deleted_items, *deleted_head;
	GSList *deleted_items_uid, *deleted_items_uid_head;

	deleted_items = deleted_head = NULL;
	deleted_items_uid = deleted_items_uid_head = NULL;

	folder_id =  g_strdup (camel_mapi_store_folder_id_lookup (mapi_store, folder->full_name)) ;
	exchange_mapi_util_mapi_id_from_string (folder_id, &fid);

	if (!strcmp (folder->full_name, "Deleted Items")) {

		CAMEL_SERVICE_REC_LOCK (mapi_store, connect_lock);
		status = exchange_mapi_empty_folder (fid);
		CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);

		if (status) {
			camel_folder_freeze (folder);
			mapi_summary_clear (folder->summary, TRUE);
			camel_folder_thaw (folder);
		} else
			g_warning ("Could not Empty Trash\n");

		return;
	}

	changes = camel_folder_change_info_new ();
	count = camel_folder_summary_count (folder->summary);

	/*Collect UIDs of deleted messages.*/
	for (i = 0; i < count; i++) {
		info = camel_folder_summary_index (folder->summary, i);
		minfo = (CamelMapiMessageInfo *) info;
		if (minfo && (minfo->info.flags & CAMEL_MESSAGE_DELETED)) {
			const gchar *uid = camel_message_info_uid (info);
			mapi_id_t *mid = g_new0 (mapi_id_t, 1);

			if (!exchange_mapi_util_mapi_ids_from_uid (uid, &fid, mid))
				continue;
			
			if (deleted_items)
				deleted_items = g_slist_prepend (deleted_items, mid);
			else {
				g_slist_free (deleted_head);
				deleted_head = NULL;
				deleted_head = deleted_items = g_slist_prepend (deleted_items, mid);
			}
			deleted_items_uid = g_slist_prepend (deleted_items_uid, uid);
		}
		camel_message_info_free (info);
	}

	deleted_items_uid_head = deleted_items_uid;

	if (deleted_items) {
		CAMEL_SERVICE_REC_LOCK (mapi_store, connect_lock);

		status = exchange_mapi_remove_items(0, fid, deleted_items);

		CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);

		if (status) {
			while (deleted_items_uid) {
				const gchar *uid = (gchar *)deleted_items_uid->data;
				CAMEL_MAPI_FOLDER_REC_LOCK (folder, cache_lock);
				camel_folder_change_info_remove_uid (changes, uid);
				camel_folder_summary_remove_uid (folder->summary, uid);
				camel_data_cache_remove(mapi_folder->cache, "cache", uid, NULL);
				CAMEL_MAPI_FOLDER_REC_UNLOCK (folder, cache_lock);
				deleted_items_uid = g_slist_next (deleted_items_uid);
			}
		}
		delete = TRUE;

		g_slist_foreach (deleted_head, (GFunc)g_free, NULL);
		g_slist_free (deleted_head);
		g_slist_free (deleted_items_uid_head);
	}

	if (delete)
		camel_object_trigger_event (CAMEL_OBJECT (folder), "folder_changed", changes);

	g_free (folder_id);
	camel_folder_change_info_free (changes);
}

static void
mapi_transfer_messages_to (CamelFolder *source, GPtrArray *uids, 
		CamelFolder *destination, GPtrArray **transferred_uids, 
		gboolean delete_originals, CamelException *ex)
{
	mapi_id_t src_fid, dest_fid;

	CamelOfflineStore *offline = (CamelOfflineStore *) destination->parent_store;
	CamelMapiStore *mapi_store= CAMEL_MAPI_STORE(source->parent_store);
	CamelFolderChangeInfo *changes = NULL;

	char *folder_id = NULL;
	int i = 0;

	GSList *src_msg_ids = NULL;


	/* check for offline operation */
	if (offline->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		printf("%s(%d):%s:WARNING : offline op not implemented \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
		return;
	}

	folder_id =  camel_mapi_store_folder_id_lookup (mapi_store, source->full_name) ;
	exchange_mapi_util_mapi_id_from_string (folder_id, &src_fid);

	folder_id =  camel_mapi_store_folder_id_lookup (mapi_store, destination->full_name) ;
	exchange_mapi_util_mapi_id_from_string (folder_id, &dest_fid);

	for (i=0; i < uids->len; i++) {
		mapi_id_t *mid = g_new0 (mapi_id_t, 1); /* FIXME : */
		if (!exchange_mapi_util_mapi_ids_from_uid (g_ptr_array_index (uids, i), &src_fid, mid)) 
			continue;

		src_msg_ids = g_slist_prepend (src_msg_ids, mid);
	}

	if (delete_originals) {
		if (!exchange_mapi_move_items (src_fid, dest_fid, src_msg_ids)) {
			//TODO : Set exception. 
		} else {
			changes = camel_folder_change_info_new ();

			for (i=0; i < uids->len; i++) {
				camel_folder_summary_remove_uid (source->summary, uids->pdata[i]);
				camel_folder_change_info_remove_uid (changes, uids->pdata[i]);
			}
			camel_object_trigger_event (source, "folder_changed", changes);
			camel_folder_change_info_free (changes);

		}
	} else {
		if (!exchange_mapi_copy_items (src_fid, dest_fid, src_msg_ids)) {
			//TODO : Set exception. 
		}
	}

	g_slist_foreach (src_msg_ids, (GFunc) g_free, NULL);
	g_slist_free (src_msg_ids);

	return;
}

static void
camel_mapi_folder_class_init (CamelMapiFolderClass *camel_mapi_folder_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS (camel_mapi_folder_class);

	parent_class = CAMEL_OFFLINE_FOLDER_CLASS (camel_type_get_global_classfuncs (camel_offline_folder_get_type ()));

	((CamelObjectClass *) camel_mapi_folder_class)->getv = mapi_getv;

	camel_folder_class->get_message = mapi_folder_get_message;
/*  	camel_folder_class->rename = mapi_folder_rename; */
	camel_folder_class->search_by_expression = mapi_folder_search_by_expression;
/* 	camel_folder_class->get_message_info = mapi_get_message_info; */
/* 	camel_folder_class->search_by_uids = mapi_folder_search_by_uids;  */
	camel_folder_class->search_free = mapi_folder_search_free;
/* 	camel_folder_class->append_message = mapi_append_message; */
	camel_folder_class->refresh_info = mapi_refresh_info;
	camel_folder_class->sync = mapi_sync;
	camel_folder_class->expunge = mapi_expunge;
	camel_folder_class->transfer_messages_to = mapi_transfer_messages_to;
}

static void
camel_mapi_folder_init (gpointer object, gpointer klass)
{
	CamelMapiFolder *mapi_folder = CAMEL_MAPI_FOLDER (object);
	CamelFolder *folder = CAMEL_FOLDER (object);


	folder->permanent_flags = CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT | CAMEL_MESSAGE_FLAGGED | CAMEL_MESSAGE_SEEN;

	folder->folder_flags = CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY | CAMEL_FOLDER_HAS_SEARCH_CAPABILITY;

	mapi_folder->priv = g_malloc0 (sizeof(*mapi_folder->priv));

#ifdef ENABLE_THREADS
	g_static_mutex_init(&mapi_folder->priv->search_lock);
	g_static_rec_mutex_init(&mapi_folder->priv->cache_lock);
#endif

	mapi_folder->need_rescan = TRUE;
}

CamelType
camel_mapi_folder_get_type (void)
{
	static CamelType camel_mapi_folder_type = CAMEL_INVALID_TYPE;


	if (camel_mapi_folder_type == CAMEL_INVALID_TYPE) {
		camel_mapi_folder_type =
			camel_type_register (camel_offline_folder_get_type (),
					"CamelMapiFolder",
					sizeof (CamelMapiFolder),
					sizeof (CamelMapiFolderClass),
					(CamelObjectClassInitFunc) camel_mapi_folder_class_init,
					NULL,
					(CamelObjectInitFunc) camel_mapi_folder_init,
					(CamelObjectFinalizeFunc) camel_mapi_folder_finalize);
	}

	return camel_mapi_folder_type;
}

CamelFolder *
camel_mapi_folder_new(CamelStore *store, const char *folder_name, const char *folder_dir, guint32 flags, CamelException *ex)
{

	CamelFolder	*folder = NULL;
	CamelMapiFolder *mapi_folder;
	char *summary_file, *state_file;
	char *short_name;


	folder = CAMEL_FOLDER (camel_object_new(camel_mapi_folder_get_type ()) );

	mapi_folder = CAMEL_MAPI_FOLDER(folder);
	short_name = strrchr (folder_name, '/');
	if (short_name)
		short_name++;
	else
		short_name = (char *) folder_name;
	camel_folder_construct (folder, store, folder_name, short_name);

	summary_file = g_strdup_printf ("%s/%s/summary",folder_dir, folder_name);

	folder->summary = camel_mapi_summary_new(folder, summary_file);
	g_free(summary_file);

	if (!folder->summary) {
		camel_object_unref (CAMEL_OBJECT (folder));
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				_("Could not load summary for %s"),
				folder_name);
		return NULL;
	}

	/* set/load persistent state */
	state_file = g_strdup_printf ("%s/cmeta", g_strdup_printf ("%s/%s",folder_dir, folder_name));
	camel_object_set(folder, NULL, CAMEL_OBJECT_STATE_FILE, state_file, NULL);
	g_free(state_file);
	camel_object_state_read(folder);

	mapi_folder->cache = camel_data_cache_new (g_strdup_printf ("%s/%s",folder_dir, folder_name),0 ,ex);
	if (!mapi_folder->cache) {
		camel_object_unref (folder);
		return NULL;
	}

/* 	journal_file = g_strdup_printf ("%s/journal", g_strdup_printf ("%s-%s",folder_name, "dir")); */
/* 	mapi_folder->journal = camel_mapi_journal_new (mapi_folder, journal_file); */
/* 	g_free (journal_file); */
/* 	if (!mapi_folder->journal) { */
/* 		camel_object_unref (folder); */
/* 		return NULL; */
/* 	} */

	if (!strcmp (folder_name, "Mailbox")) {
		if (camel_url_get_param (((CamelService *) store)->url, "filter"))
			folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;
	}

	mapi_folder->search = camel_folder_search_new ();
	if (!mapi_folder->search) {
		camel_object_unref (folder);
		return NULL;
	}

	return folder;
}

