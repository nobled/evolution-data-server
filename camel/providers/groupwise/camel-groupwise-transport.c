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

	CamelStore *store =  CAMEL_STORE (service) ;
	
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (store);
	CamelGroupwiseStorePrivate *priv = groupwise_store->priv;

	
	gboolean has_8bit_parts ;
	EGwItem *item ;
	EGwConnection *cnc = NULL;
	EGwConnectionStatus status ;
	
	CamelInternetAddress *to_addr ;
	CamelInternetAddress *cc_addr ;
	CamelInternetAddress *bcc_addr ;

	GSList *list ;
	g_print ("||||||| Works Dude ||\n") ;

	item = g_object_new (E_TYPE_GW_ITEM, NULL);

	cnc = cnc_lookup (priv) ;
	if (!cnc) {
		g_print ("||| Eh!!! Failure |||\n") ;		
	}
	g_print ("|| SUbject : %s |||\n", camel_mime_message_get_subject (message)) ;
	return FALSE ;

	status = e_gw_connection_send_item (cnc, item, &list) ;
	if (status != E_GW_CONNECTION_STATUS_OK) {
		g_print (" Error Sending mail") ;
		return FALSE ;
	}
	
	g_object_unref (item) ;
	return TRUE;
}
