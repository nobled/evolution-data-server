/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>

#include "camel-folder.h"
#include "camel-offline-folder.h"
#include "camel-offline-store.h"
#include "camel-session.h"

static gpointer parent_class;

static void
offline_store_construct (CamelService *service,
                         CamelSession *session,
                         CamelProvider *provider,
                         CamelURL *url,
                         CamelException *ex)
{
	CamelOfflineStore *store = CAMEL_OFFLINE_STORE (service);
	CamelServiceClass *service_class;

	/* Chain up to parent's construct() method. */
	service_class = CAMEL_SERVICE_CLASS (parent_class);
	service_class->construct (service, session, provider, url, ex);

	if (camel_exception_is_set (ex))
		return;

	store->state = camel_session_is_online (session) ?
		CAMEL_OFFLINE_STORE_NETWORK_AVAIL :
		CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL;
}

static void
offline_store_class_init (CamelOfflineStoreClass *class)
{
	CamelServiceClass *service_class;

	parent_class = g_type_class_peek_parent (class);

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->construct = offline_store_construct;
}

static void
offline_store_init (CamelOfflineStore *store)
{
	store->state = CAMEL_OFFLINE_STORE_NETWORK_AVAIL;
}

GType
camel_offline_store_get_type (void)
{
	static GType type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID))
		type = g_type_register_static_simple (
			CAMEL_TYPE_STORE,
			"CamelOfflineStore",
			sizeof (CamelOfflineStoreClass),
			(GClassInitFunc) offline_store_class_init,
			sizeof (CamelOfflineStore),
			(GInstanceInitFunc) offline_store_init,
			0);

	return type;
}

/**
 * camel_offline_store_get_network_state:
 * @store: a #CamelOfflineStore object
 * @ex: a #CamelException
 *
 * Return the network state either #CAMEL_OFFLINE_STORE_NETWORK_AVAIL
 * or #CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL.
 **/
gint
camel_offline_store_get_network_state (CamelOfflineStore *store, CamelException *ex)
{
	return store->state;
}

/**
 * camel_offline_store_set_network_state:
 * @store: a #CamelOfflineStore object
 * @state: the network state
 * @ex: a #CamelException
 *
 * Set the network state to either #CAMEL_OFFLINE_STORE_NETWORK_AVAIL
 * or #CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL.
 **/
void
camel_offline_store_set_network_state (CamelOfflineStore *store, gint state, CamelException *ex)
{
	CamelException lex;
	CamelService *service = CAMEL_SERVICE (store);
	gboolean network_state = camel_session_get_network_state (service->session);

	if (store->state == state)
		return;

	camel_exception_init (&lex);
	if (store->state == CAMEL_OFFLINE_STORE_NETWORK_AVAIL) {
		/* network available -> network unavailable */
		if (network_state) {
			if (((CamelStore *) store)->folders) {
				GPtrArray *folders;
				CamelFolder *folder;
				gint i, sync;

				sync = camel_url_get_param (((CamelService *) store)->url, "sync_offline") != NULL;

				folders = camel_object_bag_list (((CamelStore *) store)->folders);
				for (i = 0; i < folders->len; i++) {
					folder = folders->pdata[i];

					if (G_TYPE_CHECK_INSTANCE_TYPE (folder, CAMEL_TYPE_OFFLINE_FOLDER)
					    && (sync || ((CamelOfflineFolder *) folder)->sync_offline)) {
						camel_offline_folder_downsync ((CamelOfflineFolder *) folder, NULL, &lex);
						camel_exception_clear (&lex);
					}

					g_object_unref (folder);
				}

				g_ptr_array_free (folders, TRUE);
			}

			camel_store_sync (CAMEL_STORE (store), FALSE, &lex);
			camel_exception_clear (&lex);
		}

		if (!camel_service_disconnect (CAMEL_SERVICE (store), network_state, ex))
			return;
	} else {
		store->state = state;
		/* network unavailable -> network available */
		if (!camel_service_connect (CAMEL_SERVICE (store), ex)) {
			store->state = CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL;
			return;
		}
	}

	store->state = state;
}

void
camel_offline_store_prepare_for_offline (CamelOfflineStore *store, CamelException *ex)
{
	CamelException lex;
	CamelService *service = CAMEL_SERVICE (store);
	gboolean network_state = camel_session_get_network_state (service->session);

	camel_exception_init (&lex);
	if (network_state) {
		if (store->state == CAMEL_OFFLINE_STORE_NETWORK_AVAIL) {
			if (((CamelStore *) store)->folders) {
				GPtrArray *folders;
				CamelFolder *folder;
				gint i, sync;

				sync = camel_url_get_param (((CamelService *) store)->url, "sync_offline") != NULL;

				folders = camel_object_bag_list (((CamelStore *) store)->folders);
				for (i = 0; i < folders->len; i++) {
					folder = folders->pdata[i];

					if (G_TYPE_CHECK_INSTANCE_TYPE (folder, CAMEL_TYPE_OFFLINE_FOLDER)
					    && (sync || ((CamelOfflineFolder *) folder)->sync_offline)) {
						camel_offline_folder_downsync ((CamelOfflineFolder *) folder, NULL, &lex);
						camel_exception_clear (&lex);
					}
					g_object_unref (folder);
				}
				g_ptr_array_free (folders, TRUE);
			}
		}

		camel_store_sync (CAMEL_STORE (store), FALSE, &lex);
		camel_exception_clear (&lex);

	}
}
