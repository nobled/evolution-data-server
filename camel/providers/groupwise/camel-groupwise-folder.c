/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-groupwise-folder.c: class for an groupwise folder */

/* 
 * Authors:
 *  Sivaiah Nallagatla <snallagatla@novell.com>
 *   
 *
 * Copyright (C) 2004, Novell Inc.
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

#include "camel-groupwise-folder.h"
#include "camel-groupwise-store.h"
#include "camel-folder.h"
#include "camel-folder-search.h"
#include <e-util/e-msgport.h>
#include "camel-groupwise-store.h"
#include "camel-groupwise-summary.h"
#include "camel-i18n.h" 

#include "sspm.h" 

#include <e-gw-connection.h>
#include <e-gw-item.h>

#include <string.h>

static CamelFolderClass *parent_class = NULL;

struct _CamelGroupwiseFolderPrivate {

/*#ifdef ENABLE_THREADS
	EMutex *search_lock;    // for locking the search object 
	EMutex *cache_lock;     // for locking the cache object 
#endif*/
};

/*Prototypes*/
void gw_folder_selected (CamelFolder *folder, GList *item_list, int summary_count, CamelException *ex) ;

/******************/

#define d(x) x


static CamelMimeMessage 
*groupwise_folder_get_message( CamelFolder *folder,
				const char *uid,
				CamelException *ex )
{
	CamelMimeMessage *msg = NULL ;
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER(folder) ;
	CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE(folder->parent_store) ;
	CamelGroupwiseStorePrivate  *priv = gw_store->priv;
	CamelMessageInfo *mi = NULL ;
	CamelStream *stream = NULL ;
	char *temp_name, *folder_name, *container_id, *body ;
	CamelInternetAddress *from_addr, *to_addr ;
	EGwItemOrganizer *org ;

	EGwConnectionStatus status ;
	EGwConnection *cnc ;
	EGwItem *item ;
	char *test ="From: Harry Lu <Harry.Lu@Sun.COM>\nSubject: Re: [evolution-patches] patch related with shell\nIn-reply-to: <1099387728.4470.38.camel@lostzed.mmc.com.au>\nTo: patches <evolution-patches@lists.ximian.com>\nContent-type: multipart/alternative;" ;


	printf("\n|||| GET MESSAGE: %s |||\n",folder->name) ;
	folder_name = g_strdup(folder->name) ;
	temp_name = strchr (folder_name,'/') ;
	if(temp_name == NULL) {
		container_id =  g_strdup (container_id_lookup (priv,g_strdup(folder_name))) ;
	}
	else {
		temp_name++ ;
		container_id =  g_strdup (container_id_lookup (priv,g_strdup(temp_name))) ;
	}


	msg = camel_mime_message_new () ;

	mi = camel_folder_summary_uid (folder->summary, uid);
	if (mi == NULL) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				_("Cannot get message: %s\n  %s"), uid, _("No such message"));
		return NULL;
	}
	printf ("|| Now connecting to e-d-s |||\n") ;

	cnc = cnc_lookup (priv) ;
	status = e_gw_connection_get_item (cnc, container_id, uid, "message attachments", &item) ;
	if (status != E_GW_CONNECTION_STATUS_OK) {
		 camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Could not get message"));
		                 return NULL;
	}

	org = e_gw_item_get_organizer (item) ;
	from_addr = camel_internet_address_new () ;
	to_addr = camel_internet_address_new () ;
	camel_internet_address_add (from_addr,org->display_name,org->email) ;
	camel_internet_address_add (to_addr, e_gw_item_get_to (item), e_gw_item_get_to (item) ) ;
	
	body = g_strdup(e_gw_item_get_message(item));
	if (body) {
		camel_mime_message_set_source (msg, body ) ;
		camel_mime_part_set_encoding((CamelMimePart *)msg, CAMEL_TRANSFER_ENCODING_8BIT);
		camel_mime_part_set_content((CamelMimePart *)msg, body, strlen(body),"text/html") ;

		stream = camel_stream_mem_new_with_buffer (body, strlen(body) ) ; 

		/*	if (camel_data_wrapper_construct_from_stream (CAMEL_DATA_WRAPPER (msg),stream) == -1) {
			printf ("|| NOT RIGHT!!!! \n") ;
			}*/
	printf ("||| MESSAGE:%d |||\n %s\n",strlen(body), body) ;
	} else {
		camel_mime_message_set_source (msg, "No Message" ) ;
		camel_mime_part_set_encoding((CamelMimePart *)msg, CAMEL_TRANSFER_ENCODING_8BIT);
		camel_mime_part_set_content((CamelMimePart *)msg, "No Message", strlen("No Message"),"text/html") ;
	}
	
	camel_mime_message_set_subject (msg, e_gw_item_get_subject(item) ) ;
	camel_mime_message_set_date (msg,  e_gw_connection_format_date_string(e_gw_item_get_creation_date(item)), 0 ) ;
	camel_mime_message_set_from (msg, from_addr) ;
	



	if (body)
		g_free (body) ;

	return msg ;

}


GPtrArray *groupwise_folder_get_summary ( CamelFolder *folder )
{
	printf("||| Getting the summary ||| \n") ;
}

void groupwise_folder_free_summary ( CamelFolder *folder, GPtrArray *summary)
{
	printf("||| Freeing the summary - is this necessary |||\n") ;
}

static void
groupwise_folder_rename (CamelFolder *folder, const char *new)
{
	printf("Renaming folder....\n") ;
}

static GPtrArray *
groupwise_folder_search_by_expression (CamelFolder *folder, const char *expression, CamelException *ex)
{
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER(folder) ;
	GPtrArray *matches ;
	
	/* FOLDER LOCK */
	camel_folder_search_set_folder (gw_folder->search, folder);
	matches = camel_folder_search_search(gw_folder->search, expression, NULL, ex);
	/* FOLDER UNLOCK */
	
	return matches ;
}

static GPtrArray *
groupwise__folder_search_by_uids(CamelFolder *folder, const char *expression, GPtrArray *uids, CamelException *ex)
{
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER(folder) ;
	GPtrArray *matches ;

	if (uids->len == 0)
		return g_ptr_array_new() ;
	
	/*LOCK FOLDER*/
	camel_folder_search_set_folder(gw_folder->search, folder);
	matches = camel_folder_search_search(gw_folder->search, expression, uids, ex);
	/*UNLCOK FOLDER*/

	return matches ;
}

static void
groupwise_folder_search_free (CamelFolder *folder, GPtrArray *uids)
{
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER(folder) ;
	
	g_return_if_fail (gw_folder->search);

	/*LOCK FOLDER*/
	camel_folder_search_free_result (gw_folder->search, uids);

	/*UNLOCK FOLDER*/
}


CamelFolder *
camel_gw_folder_new(CamelStore *store, const char *folder_dir, const char *folder_name, CamelException *ex) 
{
	CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE (store) ;
	CamelFolder *folder ;
	CamelGroupwiseFolder *gw_folder ;
	char *summary_file, *state_file ;

	printf(" Opening Groupwise Folder.\n") ;

	folder = CAMEL_FOLDER (camel_object_new(camel_groupwise_folder_get_type ()) ) ;

	gw_folder = CAMEL_GROUPWISE_FOLDER(folder) ;

	camel_folder_construct (folder, store, folder_name, folder_name) ;

	summary_file = g_strdup_printf ("%s/summary",folder_dir) ;
	folder->summary = camel_groupwise_summary_new(summary_file) ;
	g_free(summary_file) ;
	if (!folder->summary) {
		camel_object_unref (CAMEL_OBJECT (folder));
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				_("Could not load summary for %s"),
				folder_name);
		return NULL;
	}

	/* set/load persistent state */
	state_file = g_strdup_printf ("%s/cmeta", folder_dir);
	camel_object_set(folder, NULL, CAMEL_OBJECT_STATE_FILE, state_file, NULL);
	g_free(state_file);
	camel_object_state_read(folder);

	gw_folder = CAMEL_GROUPWISE_FOLDER (folder) ;
	gw_folder->cache = camel_data_cache_new (folder_dir,0 ,ex) ;
	if (!gw_folder->cache) {
		camel_object_unref (folder) ;
		return NULL ;
	}

	
	return folder ;
}


static void groupwise_refresh_info(CamelFolder *folder, CamelException *ex)
{
/*	CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE (folder->parent_store) ;
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER (folder) ;
	CamelGroupwiseStorePrivate *priv = gw_store->priv ;
	int status ;
	GList *list ;
	int summary_count = camel_folder_summary_count(folder->summary) ;*/
	
	printf(">>> Refresh Info Called <<<\n") ; 

/*	if (camel_folder_is_frozen (folder) ) {
		gw_folder->need_refresh = TRUE ;
	}

	status = e_gw_connection_get_items (priv->cnc, container_id, NULL, NULL, &list) ;
	if (status != E_GW_CONNECTION_STATUS_OK) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Authentication failed"));
		return NULL;
	}
	
	CAMEL_SERVICE_LOCK (gw_store, connect_lock);

	if (gw_folder->current_folder != folder) {
		gw_folder_selected (folder, list, summary_count, ex) ;
	} else if(gw_folder->need_rescan) {
	//	gw_rescan (folder, summary_count, ex) ;
	}
	

	CAMEL_SERVICE_UNLOCK (gw_store, connect_lock);*/

	return ;
}

void
gw_folder_selected (CamelFolder *folder, GList *item_list, int summary_count, CamelException *ex)
{
	CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE (folder->parent_store) ;
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER (folder) ;
	CamelGroupwiseSummary *gw_summary = CAMEL_GROUPWISE_SUMMARY (folder->summary) ;
	CamelMessageInfo *info ;
	int i ;
	unsigned long exist = 0 ;

	for ( ; item_list != NULL ; g_list_next (item_list) ) {
	}

}

void
gw_update_summary ( CamelFolder *folder, GList *item_list,CamelException *ex) 
{
	CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE (folder->parent_store) ;
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER (folder) ;
	CamelMessageInfo *info, *mi ;
	int summary_count, first, seq ;
	GPtrArray *msg ;
	GList **temp_list = &item_list;
	guint32 uidval ;

	//CAMEL_SERVICE_ASSERT_LOCKED (gw_store, connect_lock);

	summary_count = camel_folder_summary_count (folder->summary) ;

	first = summary_count + 1 ; 

	if (summary_count > 0) {
		mi = camel_folder_summary_index (folder->summary, summary_count-1) ;
		uidval = strtoul(camel_message_info_uid (mi), NULL, 10);
		camel_folder_summary_info_free (folder->summary, mi);
	
	} else
		uidval = 0 ;

	msg = g_ptr_array_new () ;
	for ( ; item_list != NULL ; item_list = g_list_next (item_list) ) {
		EGwItem *item = (EGwItem *)item_list->data ;
		EGwItemType type ;
		EGwItemOrganizer *org ;
		char *date ;
	/*	char *uid ;

		uid = g_strdup(e_gw_item_get_id(item) ) ;
		info = camel_folder_summary_uid (folder->summary, uid) ;
		if (info) {
			for (seq = 0; seq < camel_folder_summary_count (folder->summary); seq++) {
				if (folder->summary->messages->pdata[seq] == info)
					break;
			}
			g_warning("Message already present? %s", camel_message_info_uid(mi));
			camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
					_("Unexpected server response: Identical UIDs provided for messages %d "),
					seq + 1 );

			camel_folder_summary_info_free(folder->summary, info);
			break;


		}*/
		//mi = camel_message_info_new () ;
		mi = camel_message_info_new () ; camel_folder_summary_info_new (folder->summary) ;
		if (mi->content == NULL) {
			mi->content = camel_folder_summary_content_info_new (folder->summary);
			mi->content->type = camel_content_type_new ("multipart", "mixed");
		}

		type = e_gw_item_get_item_type (item) ;

		if (type == E_GW_ITEM_TYPE_MAIL) {
			printf ("Its an e-mail\n") ;
			
		} else if (type == E_GW_ITEM_TYPE_APPOINTMENT) {
			printf ("Its an appointment\n") ;

		} else if (type == E_GW_ITEM_TYPE_TASK) {
			printf ("Its a task\n") ;

		} else if (type == E_GW_ITEM_TYPE_UNKNOWN) {
			printf ("UNKNOWN ITEM...\n") ;
			continue ;

		}
		
		org = e_gw_item_get_organizer (item) ; 
		if (org) {
			camel_message_info_set_from (mi, g_strconcat(org->display_name,"<",org->email,">",NULL)) ;
			camel_message_info_set_to (mi,g_strdup(e_gw_item_get_to (item))) ;
		}
		
		date = e_gw_connection_format_date_string(e_gw_item_get_creation_date(item)) ;
		mi->date_sent = mi->date_received = e_gw_connection_get_date_from_string (date) ;
	/*	mi->date_sent = camel_header_decode_date(date,NULL) ;
		mi->date_received = camel_header_decode_date(date,NULL) ;*/
		camel_message_info_set_uid (mi,g_strdup(e_gw_item_get_id(item) ) ) ;
		camel_message_info_set_subject (mi,g_strdup(e_gw_item_get_subject(item))) ;


		camel_folder_summary_add (folder->summary, mi) ;
		g_ptr_array_add (msg, mi) ;
		g_free(date) ;
	}

/*	for (seq=0 ; seq<msg->len ; seq++) {
		if ( (mi = msg->pdata[seq]) )
			camel_folder_summary_info_free(folder->summary, mi);
	} */
	g_ptr_array_free (msg, TRUE) ;


}

static void
camel_groupwise_folder_class_init (CamelGroupwiseFolderClass *camel_groupwise_folder_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS (camel_groupwise_folder_class);

	camel_folder_class->get_message = groupwise_folder_get_message ;
	camel_folder_class->rename = groupwise_folder_rename ;
	camel_folder_class->refresh_info = groupwise_refresh_info ;
/*	camel_folder_class->search_by_expression = groupwise_folder_search_by_expression ;
	camel_folder_class->search_by_uids = groupwise_folder_search_by_uids ; 
	camel_folder_class->search_free = groupwise_folder_search_free ;*/
	
/*	camel_folder_class->get_summary = groupwise_folder_get_summary ;
	camel_folder_class->free_summary = groupwise_folder_free_summary ;*/
	
}

static void
camel_groupwise_folder_init (gpointer object, gpointer klass)
{
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER (object);
	CamelFolder *folder = CAMEL_FOLDER (object);

	folder->permanent_flags = CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT | CAMEL_MESSAGE_FLAGGED | CAMEL_MESSAGE_SEEN;

	folder->folder_flags |= (CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY ) ; 
//			CAMEL_FOLDER_HAS_SEARCH_CAPABILITY);

	gw_folder->priv = g_malloc0(sizeof(*gw_folder->priv));
/*#ifdef ENABLE_THREADS
	gw_folder->priv->search_lock = e_mutex_new(E_MUTEX_SIMPLE);
	gw_folder->priv->cache_lock = e_mutex_new(E_MUTEX_REC);
#endif*/

	gw_folder->need_rescan = TRUE;

	printf("Came Groupwise folder init" ) ;

}

static void
camel_groupwise_folder_finalize (CamelObject *object)
{
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER (object);
	if (gw_folder->priv)
		g_free(gw_folder->priv) ;
	if (gw_folder->cache)
		camel_object_unref ( CAMEL_OBJECT (gw_folder->cache)) ;

}

CamelType
camel_groupwise_folder_get_type (void)
{
	static CamelType camel_groupwise_folder_type = CAMEL_INVALID_TYPE;

	printf("||| CAMEL GW FOLDER GET TYPE |||\n") ;
	
	if (camel_groupwise_folder_type == CAMEL_INVALID_TYPE) {
			camel_groupwise_folder_type =
			camel_type_register (CAMEL_FOLDER_TYPE, "CamelGroupwiseFolder",
					     sizeof (CamelGroupwiseFolder),
					     sizeof (CamelGroupwiseFolderClass),
					     (CamelObjectClassInitFunc) camel_groupwise_folder_class_init,
					     NULL,
					     (CamelObjectInitFunc) camel_groupwise_folder_init,
					     (CamelObjectFinalizeFunc) camel_groupwise_folder_finalize);
	}
	
	return camel_groupwise_folder_type;
}

/*
static void
gw_rescan (CamelFolder *folder, int exists, CamelException *ex) 
{
	CamelGroupwiseFolder *gw_folder = CAMEL_GROUPWISE_FOLDER (folder) ;
	CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE (folder->parent_class) ;

	return ;
}*/
