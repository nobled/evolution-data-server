/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-groupwise-transport.c : class for an groupwise transport */

/* 
 * Authors: Sivaiah Nallagatla <snallagatla@novell.com>
 *
 * Copyright (C) 2004 Novell, Inc.
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

#include "camel-groupwise-transport.h"
#include "camel-groupwise-store.h"

#include "camel-i18n.h"
#include "camel-session.h" 
#include "camel-stream.h"
#include "camel-stream-mem.h"
#include "camel-medium.h"
#include "camel-data-wrapper.h"
#include "camel-mime-message.h"
#include "camel-multipart.h"
#include "camel-mime-utils.h"

#include <string.h>


static gboolean groupwise_send_to (CamelTransport *transport,
				  CamelMimeMessage *message,
				  CamelAddress *from,
				  CamelAddress *recipients,
				  CamelException *ex) ;

static gboolean groupwise_connect (CamelService *service, CamelException *ex) ;
static char *groupwise_transport_get_name (CamelService *service, gboolean brief) ;


static CamelTransportClass *parent_class = NULL ;



static void
camel_groupwise_transport_class_init (CamelGroupwiseTransportClass *camel_groupwise_transport_class)
{
	CamelTransportClass *camel_transport_class =
		CAMEL_TRANSPORT_CLASS (camel_groupwise_transport_class);

	CamelServiceClass *camel_service_class =
		CAMEL_SERVICE_CLASS (camel_groupwise_transport_class);
	
	camel_service_class->connect = groupwise_connect ;
	camel_service_class->get_name = groupwise_transport_get_name ;
	
	/* virtual method overload */
	camel_transport_class->send_to = groupwise_send_to ;
}

static void
camel_groupwise_transport_init (CamelTransport *transport)
{
	CamelGroupwiseTransport *gw_transport = CAMEL_GROUPWISE_TRANSPORT (transport) ;
}

CamelType
camel_groupwise_transport_get_type (void)
{
	static CamelType camel_groupwise_transport_type = CAMEL_INVALID_TYPE;

	if (camel_groupwise_transport_type == CAMEL_INVALID_TYPE) {
		camel_groupwise_transport_type =
			camel_type_register (CAMEL_TRANSPORT_TYPE,
					     "CamelGroupwiseTransport",
					     sizeof (CamelGroupwiseTransport),
					     sizeof (CamelGroupwiseTransportClass),
					     (CamelObjectClassInitFunc) camel_groupwise_transport_class_init,
					     NULL,
					     (CamelObjectInitFunc) camel_groupwise_transport_init,
					     NULL);
	}

	return camel_groupwise_transport_type;
}

static char *groupwise_transport_get_name (CamelService *service, gboolean brief)
{
	if (brief)
		return g_strdup_printf (_("Groupwise server %s"), service->url->host);
	else {
		return g_strdup_printf (_("Groupwise mail delivery via %s"),
				service->url->host);
	}
}


static gboolean
groupwise_connect (CamelService *service, CamelException *ex)
{
	CamelGroupwiseTransport *gw_transport = CAMEL_GROUPWISE_TRANSPORT (service) ;

	g_print ("|||in groupwise connect ||\n") ;

	return TRUE ;

}


static gboolean
groupwise_send_to (CamelTransport *transport, CamelMimeMessage *message,
		  CamelAddress *from, CamelAddress *recipients,
		  CamelException *ex)
{
	CamelMultipart *multipart ;
	CamelGroupwiseTransport *gw_transport = CAMEL_GROUPWISE_TRANSPORT (transport) ;
	CamelService *service = CAMEL_SERVICE(transport) ;

	CamelStore *store =  NULL ;
	
	CamelGroupwiseStore *groupwise_store = NULL;
	CamelGroupwiseStorePrivate *priv = NULL;

	
	gboolean has_8bit_parts ;
	EGwItem *item ;
	EGwConnection *cnc = NULL;
	EGwConnectionStatus status ;
	
	CamelInternetAddress *to_addr ;
	CamelInternetAddress *cc_addr ;
	CamelInternetAddress *bcc_addr ;
	CamelDataWrapper *dw ;
	CamelStream *stream ;
	CamelMimePart *mime_part = CAMEL_MIME_PART(message) ;
	
	guint part_count ;
	char buffer[23] = {0};
	GSList *list = NULL ;
	char *url = NULL ;
	int i ;

	url = camel_url_to_string (service->url,
			(CAMEL_URL_HIDE_PASSWORD|
			 CAMEL_URL_HIDE_PARAMS|
			 CAMEL_URL_HIDE_AUTH) );
	
	g_print ("||||||| Works Dude :%s||\n", url) ;

	/* Get a pointer to the store and the CNC. The idea is to get the session information,
	 * so that we neednt make a connection again.
	 */
	store = camel_session_get_store (service->session, url, ex ) ;
	if (!store) {
		g_print ("ERROR: Could not get a pointer to the store") ;
	}
	groupwise_store = CAMEL_GROUPWISE_STORE (store) ;
	priv = groupwise_store->priv ;

	cnc = cnc_lookup (priv) ;
	if (!cnc) {
		g_print ("||| Eh!!! Failure |||\n") ;		
		return FALSE ;
	}

	/*
	 * Get the mime parts from CamelMimemessge 
	 */
	dw = camel_medium_get_content_object (CAMEL_MEDIUM (mime_part));
	if(!dw) {
		g_print ("ERROR: Could not get Datawrapper") ;
		return FALSE ;
	}

	if (CAMEL_IS_MULTIPART (dw)) {
		part_count = camel_multipart_get_number (CAMEL_MULTIPART(dw)) ;
		g_print ("Multipart message : %d\n",part_count) ;
		for (i=0 ; i<part_count ; i++) {
			CamelMimePart *part = camel_multipart_get_part (CAMEL_MULTIPART(dw),i) ; 
			CamelContentType *type  ;
			
			type = camel_mime_part_get_content_type(CAMEL_MULTIPART(dw)) ;
		}

	} else {
		CamelContentType *type  ;
		CamelStream *stream = camel_stream_mem_new () ;
		int count, c1 ;
		char *buff = NULL ;
		
		type = camel_data_wrapper_get_mime_type_field(dw) ;
		g_print ("Does not contain multiple parts : %s\n",type->type) ;

		count = camel_data_wrapper_decode_to_stream (dw, stream) ;


	}

	return TRUE ;
	/*Populate the item structure to send it to the GW server*/
	g_print ("|| SUbject : %s |||\n", camel_mime_message_get_subject (message)) ;
	
	item = e_gw_item_new_empty () ;

	e_gw_item_set_item_type (item, E_GW_ITEM_TYPE_MAIL) ;
	e_gw_item_set_subject (item, camel_mime_message_get_subject(message)) ;

	
	e_gw_item_set_message (item, buffer );


	
	/*Send item*/
	status = e_gw_connection_send_item (cnc, item, &list) ;
	if (status != E_GW_CONNECTION_STATUS_OK) {
		g_print (" Error Sending mail") ;
		return FALSE ;
	}
	
	g_object_unref (item) ;
	return TRUE;
}
	


/**********************************
	dw = camel_data_wrapper_new ();
	stream = camel_stream_mem_new () ;

	mime_part = (CamelMimePart *)message ;
	dw = camel_medium_get_content_object (CAMEL_MEDIUM (mime_part)) ;

	count = camel_data_wrapper_write_to_stream (dw, stream) ;
		
	camel_stream_read (stream, buffer, count) ;
	if (!buffer) {
		g_print ("Error: Could not get buffer\n") ;
		return FALSE ;
	}
		
	g_print ("Buffer : \n %s \n", buffer) ;

	*********************************/
