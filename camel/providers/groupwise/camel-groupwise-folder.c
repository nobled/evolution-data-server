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
	CamelGroupwiseMessageInfo *mi = NULL ;
	char *temp_name, *folder_name, *container_id, *body ;
	CamelInternetAddress *from_addr, *to_addr, *cc_addr, *bcc_addr ;

	GSList *recipient_list, *attach_list ;
	
	EGwItemOrganizer *org ;
	EGwItemType type ;
	EGwConnectionStatus status ;
	EGwConnection *cnc ;
	EGwItem *item ;
	EGwItemRecipient *recp ;

	CamelMultipart *multipart ;

	/*FIXME: Currently supports only 2 levels of folder heirarchy*/
	folder_name = g_strdup(folder->name) ;
	temp_name = strchr (folder_name,'/') ;
	if(temp_name == NULL) {
		container_id =  g_strdup (container_id_lookup (priv,g_strdup(folder_name))) ;
	}
	else {
		temp_name++ ;
		container_id =  g_strdup (container_id_lookup (priv,g_strdup(temp_name))) ;
	}

	
	/*Create and populate the MIME Message structure*/
	msg = camel_mime_message_new () ;

	multipart = camel_multipart_new () ;

	mi = (CamelMessageInfo *) camel_folder_summary_uid (folder->summary, uid);
	if (mi == NULL) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				_("Cannot get message: %s\n  %s"), uid, _("No such message"));
		return NULL;
	}

	cnc = cnc_lookup (priv) ;

	status = e_gw_connection_get_item (cnc, container_id, uid, "recipient message attachments", &item) ;
	if (status != E_GW_CONNECTION_STATUS_OK) {
		 camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Could not get message"));
		                 return NULL;
	}
	
	type = e_gw_item_get_item_type(item) ;
	if (type == E_GW_ITEM_TYPE_MAIL) {
		printf ("MAIL\n") ;

	} else if (type == E_GW_ITEM_TYPE_APPOINTMENT) {
		printf ("APPOINTMENT\n") ;

	} else if (type == E_GW_ITEM_TYPE_TASK) {
		printf ("TASK\n") ;

	} else if (type == E_GW_ITEM_TYPE_UNKNOWN) {
		printf ("UNKNOWN\n") ;
		/*XXX: Free memory allocations*/
		return NULL ;
	}
	
	org = e_gw_item_get_organizer (item) ;
	recipient_list = e_gw_item_get_recipient_list (item) ;

	/*Addresses*/
	from_addr = camel_internet_address_new () ;
	to_addr = camel_internet_address_new () ;
	cc_addr = camel_internet_address_new () ;
	bcc_addr = camel_internet_address_new () ;

	if (recipient_list) {
		GSList *rl ;
		
		for (rl = recipient_list ; rl != NULL ; rl = rl->next) {
			EGwItemRecipient *recp = (EGwItemRecipient *) rl->data;

			if (recp->type == E_GW_ITEM_RECIPIENT_TO) {
				camel_internet_address_add (to_addr, recp->display_name, recp->email ) ;
				
			} else if (recp->type == E_GW_ITEM_RECIPIENT_CC) {
				camel_internet_address_add (cc_addr, recp->display_name, recp->email ) ;
				
			} else if (recp->type == E_GW_ITEM_RECIPIENT_BC) {
				camel_internet_address_add (bcc_addr, recp->display_name, recp->email ) ;

			}
		}
		
		camel_mime_message_set_recipients (msg, "To", to_addr) ;
		camel_mime_message_set_recipients (msg, "Cc", cc_addr) ;
		camel_mime_message_set_recipients (msg, "Bcc", bcc_addr) ;
	}
	if (org)
		camel_internet_address_add (from_addr,org->display_name,org->email) ;
	

	/*Content and content-type*/
	body = g_strdup(e_gw_item_get_message(item));
	if (body) {
		CamelMimePart *part ;
		part = camel_mime_part_new () ;

		camel_mime_part_set_encoding(part, CAMEL_TRANSFER_ENCODING_8BIT);
		camel_mime_part_set_content(part, body, strlen(body), e_gw_item_get_msg_content_type (item)) ;
		camel_multipart_add_part (multipart, part) ;
		camel_object_unref (part) ;

	} else {
		CamelMimePart *part ;
		part = camel_mime_part_new () ;
		
		camel_mime_part_set_encoding(part, CAMEL_TRANSFER_ENCODING_8BIT);
		camel_mime_part_set_content(part, " ", strlen(" "),"text/html") ;
		camel_multipart_add_part (multipart, part) ;

		camel_object_unref (part) ;
	}
	
	camel_mime_message_set_subject (msg, e_gw_item_get_subject(item) ) ;
	camel_mime_message_set_date (msg, e_gw_connection_get_date_from_string (e_gw_item_get_creation_date(item)), 0 ) ;
	camel_mime_message_set_from (msg, from_addr) ;
	

	/*Attachments*/
	attach_list = e_gw_item_get_attach_id_list (item) ;
	if (attach_list) {
		GSList *al ;
		int count = g_slist_length (attach_list) ;
		

		printf ("||Attachments present : %d ||\n", count) ;

		for (al = attach_list ; al != NULL ; al = al->next) {
			EGwItemAttachment *attach = (EGwItemAttachment *)al->data ;
			char *attachment ;
			int len ;
			CamelMimePart *part ;

			status = e_gw_connection_get_attachment (cnc, g_strdup(attach->id), 0, -1, &attachment, &len) ;
			if (status != E_GW_CONNECTION_STATUS_OK) {
				camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Could not get message"));
				return NULL;
			}
			if (attach && (len !=0) ) {
				part = camel_mime_part_new () ;

				camel_data_wrapper_set_mime_type(CAMEL_DATA_WRAPPER (multipart), "multipart/digest") ;
				camel_multipart_set_boundary(multipart, NULL);

				camel_mime_part_set_filename(part, g_strdup(attach->name)) ;
				camel_mime_part_set_content(part, attachment, len, attach->contentType) ;

				camel_multipart_add_part (multipart, part) ;

				camel_object_unref (part) ;
			}
			g_free (attachment) ;
		}
		

	}
	camel_medium_set_content_object(CAMEL_MEDIUM (msg), CAMEL_DATA_WRAPPER(multipart));

	camel_object_unref (multipart) ;

	
	if (body)
		g_free (body) ;

	return msg ;

}


GPtrArray *groupwise_folder_get_summary ( CamelFolder *folder )
{
	printf("||| Getting the summary ||| \n") ;
	return NULL ;
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
	folder->summary = camel_groupwise_summary_new(folder, summary_file) ;
	camel_folder_summary_clear (folder->summary) ;
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
	CamelGroupwiseMessageInfo *info, *mi ;
	int summary_count, first, seq ;
	GPtrArray *msg ;
	GList **temp_list = &item_list;
	GSList *attach_list ;
	guint32 uidval ;

	//CAMEL_SERVICE_ASSERT_LOCKED (gw_store, connect_lock);

	summary_count = camel_folder_summary_count (folder->summary) ;
	camel_folder_summary_clear (folder->summary) ;

	first = summary_count + 1 ; 

	if (summary_count > 0) {
		mi = (CamelGroupwiseMessageInfo *)camel_folder_summary_index (folder->summary, summary_count-1) ;
		uidval = strtoul(camel_message_info_uid (mi), NULL, 10);
		//camel_folder_summary_info_free (folder->summary, mi);
		camel_message_info_free (&mi->info);
	
	} else
		uidval = 0 ;

	msg = g_ptr_array_new () ;
	for ( ; item_list != NULL ; item_list = g_list_next (item_list) ) {
		EGwItem *item = (EGwItem *)item_list->data ;
		EGwItemType type ;
		EGwItemOrganizer *org ;
		char *date = NULL, *temp_date = NULL ;
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

			//camel_folder_summary_info_free(folder->summary, info);
			camel_folder_info_free(info);
			break;


		}*/
		//camel_folder_summary_info_new (folder->summary) ;
		mi = camel_message_info_new (NULL) ; 
		if (mi->info.content == NULL) {
			mi->info.content = camel_folder_summary_content_info_new (folder->summary);
			mi->info.content->type = camel_content_type_new ("multipart", "mixed");
		}

		type = e_gw_item_get_item_type (item) ;

		if (type == E_GW_ITEM_TYPE_MAIL) {
		//	printf ("Its an e-mail\n") ;
			
		} else if (type == E_GW_ITEM_TYPE_APPOINTMENT) {
		//	printf ("Its an appointment\n") ;

		} else if (type == E_GW_ITEM_TYPE_TASK) {
		//	printf ("Its a task\n") ;

		} else if (type == E_GW_ITEM_TYPE_CONTACT) {
		//	printf ("Its a contact\n") ;
			continue ;
		
		} else if (type == E_GW_ITEM_TYPE_UNKNOWN) {
			printf ("UNKNOWN ITEM...\n") ;
			continue ;

		}
		
		attach_list = e_gw_item_get_attach_id_list (item) ;
		if (attach_list) 
			mi->info.flags |= CAMEL_MESSAGE_ATTACHMENTS;

		org = e_gw_item_get_organizer (item) ; 
		if (org) {
			mi->info.from = g_strconcat(org->display_name,"<",org->email,">",NULL) ;
			mi->info.to = g_strdup(e_gw_item_get_to (item)) ;
		/*	camel_message_info_set_from (mi, g_strconcat(org->display_name,"<",org->email,">",NULL)) ;
			camel_message_info_set_to (mi,g_strdup(e_gw_item_get_to (item))) ;*/
		}
		
		temp_date = e_gw_item_get_creation_date(item) ;
		if (temp_date) {
			date = e_gw_connection_format_date_string(temp_date) ;
			mi->info.date_sent = mi->info.date_received = e_gw_connection_get_date_from_string (date) ;
			/*	mi->date_sent = camel_header_decode_date(date,NULL) ;
				mi->date_received = camel_header_decode_date(date,NULL) ;*/
		}
		/*camel_message_info_set_uid (mi,g_strdup(e_gw_item_get_id(item) ) ) ;
		camel_message_info_set_subject (mi,g_strdup(e_gw_item_get_subject(item))) ;*/
		mi->info.uid = g_strdup (e_gw_item_get_id (item)) ;
		mi->info.subject = g_strdup (e_gw_item_get_subject(item)) ;


		camel_folder_summary_add (folder->summary,(CamelMessageInfo *)mi) ;
		g_ptr_array_add (msg, mi) ;
		g_free(date) ;
	}

/*	for (seq=0 ; seq<msg->len ; seq++) {
		if ( (mi = msg->pdata[seq]) )
			//camel_folder_summary_info_free(folder->summary, mi);
			camel_folder_info_free(mi);
	} */
	g_ptr_array_free (msg, TRUE) ;


}

static int
uid_compar (const void *va, const void *vb)
{
	const char **sa = (const char **)va, **sb = (const char **)vb;
	unsigned long a, b;

	a = strtoul (*sa, NULL, 10);
	b = strtoul (*sb, NULL, 10);
	if (a < b)
		return -1;
	else if (a == b)
		return 0;
	else
		return 1;
}



void groupwise_transfer_messages_to ( CamelFolder *source, GPtrArray *uids, CamelFolder *destination, GPtrArray **transferred_uids, gboolean delete_originals, CamelException *ex)
{
	CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE (source->parent_store) ;
	int count ;


	count = camel_folder_summary_count (destination->summary) ;
	printf ("|| Transferring messages called : %d||\n", count) ;

	qsort (uids->pdata, uids->len, sizeof (void *), uid_compar) ;


}


static void
camel_groupwise_folder_class_init (CamelGroupwiseFolderClass *camel_groupwise_folder_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS (camel_groupwise_folder_class);

	camel_folder_class->get_message = groupwise_folder_get_message ;
	camel_folder_class->rename = groupwise_folder_rename ;
	camel_folder_class->refresh_info = groupwise_refresh_info ;
	camel_folder_class->transfer_messages_to = groupwise_transfer_messages_to ;
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
