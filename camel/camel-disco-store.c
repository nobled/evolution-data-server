/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-disco-store.c: abstract class for a disconnectable store */

/*
 *  Authors: Dan Winship <danw@ximian.com>
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

#include <glib/gi18n-lib.h>

#include "camel-disco-diary.h"
#include "camel-disco-folder.h"
#include "camel-disco-store.h"
#include "camel-exception.h"
#include "camel-session.h"

#define d(x)

static gpointer parent_class;

static void disco_construct (CamelService *service, CamelSession *session,
			     CamelProvider *provider, CamelURL *url,
			     CamelException *ex);
static gboolean disco_connect (CamelService *service, CamelException *ex);
static void disco_cancel_connect (CamelService *service);
static gboolean disco_disconnect (CamelService *service, gboolean clean, CamelException *ex);
static CamelFolder *disco_get_folder (CamelStore *store, const gchar *name,
				      guint32 flags, CamelException *ex);
static CamelFolderInfo *disco_get_folder_info (CamelStore *store,
					       const gchar *top, guint32 flags,
					       CamelException *ex);
static void set_status (CamelDiscoStore *disco_store,
			CamelDiscoStoreStatus status,
			CamelException *ex);
static gboolean can_work_offline (CamelDiscoStore *disco_store);

static gint disco_setv (CamelObject *object, CamelException *ex, CamelArgV *args);
static gint disco_getv (CamelObject *object, CamelException *ex, CamelArgGetV *args);

static void
disco_store_class_init (CamelDiscoStoreClass *class)
{
	CamelObjectClass *camel_object_class;
	CamelServiceClass *service_class;
	CamelStoreClass *store_class;

	parent_class = g_type_class_peek_parent (class);

	camel_object_class = CAMEL_OBJECT_CLASS (class);
	camel_object_class->setv = disco_setv;
	camel_object_class->getv = disco_getv;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->construct = disco_construct;
	service_class->connect = disco_connect;
	service_class->disconnect = disco_disconnect;
	service_class->cancel_connect = disco_cancel_connect;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->get_folder = disco_get_folder;
	store_class->get_folder_info = disco_get_folder_info;

	class->set_status = set_status;
	class->can_work_offline = can_work_offline;
}

GType
camel_disco_store_get_type (void)
{
	static GType type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID))
		type = g_type_register_static_simple (
			CAMEL_TYPE_STORE,
			"CamelDiscoStore",
			sizeof (CamelDiscoStoreClass),
			(GClassInitFunc) disco_store_class_init,
			sizeof (CamelDiscoStore),
			NULL,
			0);

	return type;
}

static gint
disco_setv (CamelObject *object, CamelException *ex, CamelArgV *args)
{
	/* CamelDiscoStore doesn't currently have anything to set */
	return CAMEL_OBJECT_CLASS (parent_class)->setv (object, ex, args);
}

static gint
disco_getv (CamelObject *object, CamelException *ex, CamelArgGetV *args)
{
	/* CamelDiscoStore doesn't currently have anything to get */
	return CAMEL_OBJECT_CLASS (parent_class)->getv (object, ex, args);
}

static void
disco_construct (CamelService *service, CamelSession *session,
		 CamelProvider *provider, CamelURL *url,
		 CamelException *ex)
{
	CamelDiscoStore *disco = CAMEL_DISCO_STORE (service);

	CAMEL_SERVICE_CLASS (parent_class)->construct (service, session, provider, url, ex);
	if (camel_exception_is_set (ex))
		return;

	disco->status = camel_session_is_online (session) ?
		CAMEL_DISCO_STORE_ONLINE : CAMEL_DISCO_STORE_OFFLINE;
}

static gboolean
disco_connect (CamelService *service, CamelException *ex)
{
	CamelDiscoStore *store = CAMEL_DISCO_STORE (service);
	CamelDiscoStoreStatus status;
	struct _CamelDiscoDiary *diary;

	status = camel_disco_store_status (store);
	if (status != CAMEL_DISCO_STORE_OFFLINE) {
		if (!CAMEL_SERVICE_CLASS (parent_class)->connect (service, ex)) {
			status = camel_disco_store_status (store);
			if (status != CAMEL_DISCO_STORE_OFFLINE)
				return FALSE;
			camel_exception_clear (ex);
		}
	}

	switch (status) {
	case CAMEL_DISCO_STORE_ONLINE:
	case CAMEL_DISCO_STORE_RESYNCING:
		if (!CAMEL_DISCO_STORE_GET_CLASS (service)->connect_online (service, ex))
			return FALSE;

		if (!store->diary)
			return TRUE;

		d(printf(" diary is %s\n", camel_disco_diary_empty(store->diary)?"empty":"not empty"));
		if (camel_disco_diary_empty (store->diary))
			return TRUE;

		/* Need to resync.  Note we do the ref thing since during the replay
		   disconnect could be called, which will remove store->diary and unref it */
		store->status = CAMEL_DISCO_STORE_RESYNCING;
		diary = store->diary;
		g_object_ref (diary);
		camel_disco_diary_replay(diary, ex);
		g_object_unref (diary);
		store->status = CAMEL_DISCO_STORE_ONLINE;
		if (camel_exception_is_set (ex))
			return FALSE;

		if (!camel_service_disconnect (service, TRUE, ex))
			return FALSE;
		return camel_service_connect (service, ex);

	case CAMEL_DISCO_STORE_OFFLINE:
		return CAMEL_DISCO_STORE_GET_CLASS (service)->connect_offline (service, ex);
	}

	g_assert_not_reached ();
	return FALSE;
}

static void
disco_cancel_connect (CamelService *service)
{
	CamelDiscoStore *store = CAMEL_DISCO_STORE (service);

	/* Fall back */
	store->status = CAMEL_DISCO_STORE_OFFLINE;
	CAMEL_SERVICE_CLASS (parent_class)->cancel_connect (service);
}

static gboolean
disco_disconnect (CamelService *service, gboolean clean, CamelException *ex)
{
	CamelDiscoStore *store = CAMEL_DISCO_STORE (service);

	switch (camel_disco_store_status (store)) {
	case CAMEL_DISCO_STORE_ONLINE:
	case CAMEL_DISCO_STORE_RESYNCING:
		if (!CAMEL_DISCO_STORE_GET_CLASS (service)->disconnect_online (service, clean, ex))
			return FALSE;
		break;

	case CAMEL_DISCO_STORE_OFFLINE:
		if (!CAMEL_DISCO_STORE_GET_CLASS (service)->disconnect_offline (service, clean, ex))
			return FALSE;
		break;

	}

	return CAMEL_SERVICE_CLASS (parent_class)->disconnect (service, clean, ex);
}

static CamelFolder *
disco_get_folder (CamelStore *store, const gchar *name,
		  guint32 flags, CamelException *ex)
{
	CamelDiscoStore *disco_store = CAMEL_DISCO_STORE (store);

	switch (camel_disco_store_status (disco_store)) {
	case CAMEL_DISCO_STORE_ONLINE:
		return CAMEL_DISCO_STORE_GET_CLASS (store)->get_folder_online (store, name, flags, ex);

	case CAMEL_DISCO_STORE_OFFLINE:
		return CAMEL_DISCO_STORE_GET_CLASS (store)->get_folder_offline (store, name, flags, ex);

	case CAMEL_DISCO_STORE_RESYNCING:
		return CAMEL_DISCO_STORE_GET_CLASS (store)->get_folder_resyncing (store, name, flags, ex);
	}

	g_assert_not_reached ();
	return NULL;
}

static CamelFolderInfo *
disco_get_folder_info (CamelStore *store, const gchar *top,
		       guint32 flags, CamelException *ex)
{
	CamelDiscoStore *disco_store = CAMEL_DISCO_STORE (store);

	switch (camel_disco_store_status (disco_store)) {
	case CAMEL_DISCO_STORE_ONLINE:
		return CAMEL_DISCO_STORE_GET_CLASS (store)->get_folder_info_online (store, top, flags, ex);

	case CAMEL_DISCO_STORE_OFFLINE:
		/* Can't edit subscriptions while offline */
		if ((store->flags & CAMEL_STORE_SUBSCRIPTIONS) &&
		    !(flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED)) {
			camel_disco_store_check_online (disco_store, ex);
			return NULL;
		}

		return CAMEL_DISCO_STORE_GET_CLASS (store)->get_folder_info_offline (store, top, flags, ex);

	case CAMEL_DISCO_STORE_RESYNCING:
		return CAMEL_DISCO_STORE_GET_CLASS (store)->get_folder_info_resyncing (store, top, flags, ex);
	}

	g_assert_not_reached ();
	return NULL;
}

/**
 * camel_disco_store_status:
 * @store: a disconnectable store
 *
 * Return value: the current online/offline status of @store.
 **/
CamelDiscoStoreStatus
camel_disco_store_status (CamelDiscoStore *store)
{
	CamelService *service = CAMEL_SERVICE (store);

	g_return_val_if_fail (CAMEL_IS_DISCO_STORE (store), CAMEL_DISCO_STORE_ONLINE);

	if (store->status != CAMEL_DISCO_STORE_OFFLINE
	    && !camel_session_is_online (service->session))
		store->status = CAMEL_DISCO_STORE_OFFLINE;

	return store->status;
}

static void
set_status(CamelDiscoStore *disco_store, CamelDiscoStoreStatus status, CamelException *ex)
{
	CamelException x;
	CamelService *service = CAMEL_SERVICE (disco_store);
	gboolean network_state = camel_session_get_network_state (service->session);

	if (disco_store->status == status)
		return;

	camel_exception_init(&x);
	/* Sync the folder fully if we've been told to sync online for this store or this folder
	   and we're going offline */

	if (network_state) {
		if (disco_store->status == CAMEL_DISCO_STORE_ONLINE
		    && status == CAMEL_DISCO_STORE_OFFLINE) {
			if (((CamelStore *)disco_store)->folders) {
				GPtrArray *folders;
				CamelFolder *folder;
				gint i, sync;

				sync =  camel_url_get_param(((CamelService *)disco_store)->url, "offline_sync") != NULL;

				folders = camel_object_bag_list(((CamelStore *)disco_store)->folders);
				for (i=0;i<folders->len;i++) {
					folder = folders->pdata[i];
					if (G_TYPE_CHECK_INSTANCE_TYPE(folder, CAMEL_TYPE_DISCO_FOLDER)
					    && (sync || ((CamelDiscoFolder *)folder)->offline_sync)) {
						camel_disco_folder_prepare_for_offline((CamelDiscoFolder *)folder, "", &x);
						camel_exception_clear(&x);
					}
					g_object_unref (folder);
				}
				g_ptr_array_free(folders, TRUE);
			}
		}

		camel_store_sync(CAMEL_STORE (disco_store), FALSE, &x);
		camel_exception_clear(&x);
	}

	if (!camel_service_disconnect (CAMEL_SERVICE (disco_store), network_state, ex))
		return;

	disco_store->status = status;
	camel_service_connect (CAMEL_SERVICE (disco_store), ex);
}

/**
 * camel_disco_store_set_status:
 * @store: a disconnectable store
 * @status: the new status
 * @ex: a CamelException
 *
 * Sets @store to @status. If an error occurrs and the status cannot
 * be set to @status, @ex will be set.
 **/
void
camel_disco_store_set_status (CamelDiscoStore *store,
			      CamelDiscoStoreStatus status,
			      CamelException *ex)
{
	d(printf("disco store set status: %s\n", status == CAMEL_DISCO_STORE_ONLINE?"online":"offline"));

	CAMEL_DISCO_STORE_GET_CLASS (store)->set_status (store, status, ex);
}

static gboolean
can_work_offline (CamelDiscoStore *disco_store)
{
	g_warning ("CamelDiscoStore::can_work_offline not implemented for '%s'",
		   G_OBJECT_CLASS_NAME (G_OBJECT_TYPE (disco_store)));
	return FALSE;
}

/**
 * camel_disco_store_can_work_offline:
 * @store: a disconnectable store
 *
 * Return value: whether or not @store can be used offline. (Will be
 * %FALSE if the store is not caching data to local disk, for example.)
 **/
gboolean
camel_disco_store_can_work_offline (CamelDiscoStore *store)
{
	return CAMEL_DISCO_STORE_GET_CLASS (store)->can_work_offline (store);
}

/**
 * camel_disco_store_check_online:
 * @store: a disconnectable store
 * @ex: a CamelException
 *
 * This checks that @store is online, and sets @ex if it is not. This
 * can be used as a simple way to set a generic error message in @ex
 * for operations that won't work offline.
 *
 * Return value: whether or not @store is online.
 **/
gboolean
camel_disco_store_check_online (CamelDiscoStore *store, CamelException *ex)
{
	if (camel_disco_store_status (store) != CAMEL_DISCO_STORE_ONLINE) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				     _("You must be working online to "
				       "complete this operation"));
		return FALSE;
	}

	return TRUE;
}

void
camel_disco_store_prepare_for_offline(CamelDiscoStore *disco_store, CamelException *ex)
{
	CamelException x;
	CamelService *service = CAMEL_SERVICE (disco_store);
	gboolean network_state = camel_session_get_network_state (service->session);

	camel_exception_init(&x);
	/* Sync the folder fully if we've been told to sync online for this store or this folder */

	if (network_state) {
		if (disco_store->status == CAMEL_DISCO_STORE_ONLINE) {
			if (((CamelStore *)disco_store)->folders) {
				GPtrArray *folders;
				CamelFolder *folder;
				gint i, sync;

				sync =  camel_url_get_param(((CamelService *)disco_store)->url, "offline_sync") != NULL;

				folders = camel_object_bag_list(((CamelStore *)disco_store)->folders);
				for (i=0;i<folders->len;i++) {
					folder = folders->pdata[i];
					if (G_TYPE_CHECK_INSTANCE_TYPE(folder, CAMEL_TYPE_DISCO_FOLDER)
					    && (sync || ((CamelDiscoFolder *)folder)->offline_sync)) {
						camel_disco_folder_prepare_for_offline((CamelDiscoFolder *)folder, "(match-all)", &x);
						camel_exception_clear(&x);
					}
					g_object_unref (folder);
				}
				g_ptr_array_free(folders, TRUE);
			}
		}

		camel_store_sync(CAMEL_STORE (disco_store), FALSE, &x);
		camel_exception_clear(&x);
	}
}

