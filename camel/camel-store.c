/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-store.c : Abstract class for an email store */

/*
 * Authors:
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *  Dan Winship <danw@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>

#include "camel-db.h"
#include "camel-debug.h"
#include "camel-folder.h"
#include "camel-marshal.h"
#include "camel-session.h"
#include "camel-store.h"
#include "camel-vtrash-folder.h"

#define d(x)
#define w(x)

#define CAMEL_STORE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_STORE, CamelStorePrivate))

typedef struct _AsyncContext AsyncContext;
typedef struct _SignalData SignalData;

struct _CamelStorePrivate {
	GStaticRecMutex folder_lock;	/* for locking folder operations */
};

struct _AsyncContext {
	/* arguments */
	gchar *folder_name_1;
	gchar *folder_name_2;
	gboolean expunge;
	guint32 flags;

	/* results */
	CamelFolder *folder;
	CamelFolderInfo *folder_info;
};

struct _SignalData {
	CamelStore *store;
	CamelFolder *folder;
	CamelFolderInfo *folder_info;
	gchar *folder_name;
};

enum {
	FOLDER_CREATED,
	FOLDER_DELETED,
	FOLDER_OPENED,
	FOLDER_RENAMED,
	FOLDER_SUBSCRIBED,
	FOLDER_UNSUBSCRIBED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_ABSTRACT_TYPE (CamelStore, camel_store, CAMEL_TYPE_SERVICE)

static void
async_context_free (AsyncContext *async_context)
{
	g_free (async_context->folder_name_1);
	g_free (async_context->folder_name_2);

	if (async_context->folder != NULL)
		g_object_unref (async_context->folder);

	camel_folder_info_free (async_context->folder_info);

	g_slice_free (AsyncContext, async_context);
}

static void
signal_data_free (SignalData *data)
{
	if (data->store != NULL)
		g_object_unref (data->store);

	if (data->folder != NULL)
		g_object_unref (data->folder);

	if (data->folder_info != NULL)
		camel_folder_info_free (data->folder_info);

	g_free (data->folder_name);

	g_slice_free (SignalData, data);
}

static gboolean
store_emit_folder_created_cb (SignalData *data)
{
	g_signal_emit (
		data->store,
		signals[FOLDER_CREATED], 0,
		data->folder_info);

	return FALSE;
}

static gboolean
store_emit_folder_deleted_cb (SignalData *data)
{
	g_signal_emit (
		data->store,
		signals[FOLDER_DELETED], 0,
		data->folder_info);

	return FALSE;
}

static gboolean
store_emit_folder_opened_cb (SignalData *data)
{
	g_signal_emit (
		data->store,
		signals[FOLDER_OPENED], 0,
		data->folder);

	return FALSE;
}

static gboolean
store_emit_folder_renamed_cb (SignalData *data)
{
	g_signal_emit (
		data->store,
		signals[FOLDER_RENAMED], 0,
		data->folder_name,
		data->folder_info);

	return FALSE;
}

static gboolean
store_emit_folder_subscribed_cb (SignalData *data)
{
	g_signal_emit (
		data->store,
		signals[FOLDER_SUBSCRIBED], 0,
		data->folder_info);

	return FALSE;
}

static gboolean
store_emit_folder_unsubscribed_cb (SignalData *data)
{
	g_signal_emit (
		data->store,
		signals[FOLDER_UNSUBSCRIBED], 0,
		data->folder_info);

	return FALSE;
}

/**
 * ignore_no_such_table_exception:
 * Clears the exception 'ex' when it's the 'no such table' exception.
 **/
static void
ignore_no_such_table_exception (GError **error)
{
	if (error == NULL || *error == NULL)
		return;

	if (g_ascii_strncasecmp ((*error)->message, "no such table", 13) == 0)
		g_clear_error (error);
}

static CamelFolder *
store_get_special (CamelStore *store,
                   camel_vtrash_folder_t type)
{
	CamelFolder *folder;
	GPtrArray *folders;
	gint i;

	folder = camel_vtrash_folder_new (store, type);
	folders = camel_object_bag_list (store->folders);
	for (i=0;i<folders->len;i++) {
		if (!CAMEL_IS_VTRASH_FOLDER (folders->pdata[i]))
			camel_vee_folder_add_folder ((CamelVeeFolder *)folder, (CamelFolder *)folders->pdata[i]);
		g_object_unref (folders->pdata[i]);
	}
	g_ptr_array_free (folders, TRUE);

	return folder;
}

static void
store_finalize (GObject *object)
{
	CamelStore *store = CAMEL_STORE (object);

	if (store->folders != NULL)
		camel_object_bag_destroy (store->folders);

	g_static_rec_mutex_free (&store->priv->folder_lock);

	if (store->cdb_r != NULL) {
		camel_db_close (store->cdb_r);
		store->cdb_r = NULL;
	}

	if (store->cdb_w != NULL) {
		camel_db_close (store->cdb_w);
		store->cdb_w = NULL;
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_store_parent_class)->finalize (object);
}

static void
store_constructed (GObject *object)
{
	CamelStore *store;
	CamelStoreClass *class;

	store = CAMEL_STORE (object);
	class = CAMEL_STORE_GET_CLASS (store);

	if (class->hash_folder_name != NULL)
		store->folders = camel_object_bag_new (
			class->hash_folder_name,
			class->compare_folder_name,
			(CamelCopyFunc) g_strdup, g_free);
	else
		store->folders = NULL;
}

static gboolean
store_construct (CamelService *service,
                 CamelSession *session,
                 CamelProvider *provider,
                 CamelURL *url,
                 GError **error)
{
	CamelServiceClass *service_class;
	CamelStore *store = CAMEL_STORE (service);
	gchar *store_db_path, *store_path = NULL;

	/* Chain up to parent's construct() method. */
	service_class = CAMEL_SERVICE_CLASS (camel_store_parent_class);
	if (!service_class->construct (service, session, provider, url, error))
		return FALSE;

	store_db_path = g_build_filename (service->url->path, CAMEL_DB_FILE, NULL);

	if (!service->url->path || strlen (store_db_path) < 2) {
		store_path = camel_session_get_storage_path (session, service, error);

		g_free (store_db_path);
		store_db_path = g_build_filename (store_path, CAMEL_DB_FILE, NULL);
	}

	if (!g_file_test (service->url->path ? service->url->path : store_path, G_FILE_TEST_EXISTS)) {
		/* Cache might be blown. Recreate. */
		g_mkdir_with_parents (service->url->path ? service->url->path : store_path, S_IRWXU);
	}

	g_free (store_path);

	/* This is for reading from the store */
	store->cdb_r = camel_db_open (store_db_path, NULL);
	if (camel_debug("sqlite"))
		printf("store_db_path %s\n", store_db_path);
	if (store->cdb_r == NULL) {
		gchar *store_path;

		if (camel_debug("sqlite"))
			g_print ("Failure for store_db_path : [%s]\n", store_db_path);
		g_free (store_db_path);

		store_path =  camel_session_get_storage_path (session, service, NULL);
		store_db_path = g_build_filename (store_path, CAMEL_DB_FILE, NULL);
		g_free (store_path);

		store->cdb_r = camel_db_open (store_db_path, NULL);
		if (store->cdb_r == NULL) {
			g_print("Retry with %s failed\n", store_db_path);
			g_free (store_db_path);
			return FALSE;
		}
	}
	g_free (store_db_path);

	if (camel_db_create_folders_table (store->cdb_r, error)) {
		g_warning ("something went wrong terribly during db creation \n");
		return FALSE;
	}

	/* This is for writing to the store */
	store->cdb_w = camel_db_clone (store->cdb_r, error);

	if (camel_url_get_param(url, "filter"))
		store->flags |= CAMEL_STORE_FILTER_INBOX;

	return TRUE;
}

static gboolean
store_can_refresh_folder (CamelStore *store,
                          CamelFolderInfo *info,
                          GError **error)
{
	return ((info->flags & CAMEL_FOLDER_TYPE_MASK) == CAMEL_FOLDER_TYPE_INBOX);
}

static CamelFolder *
store_get_inbox_folder_sync (CamelStore *store,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelStoreClass *class;
	CamelFolder *folder;

	/* Assume the inbox's name is "inbox" and open with default flags. */
	class = CAMEL_STORE_GET_CLASS (store);
	folder = class->get_folder_sync (store, "inbox", 0, cancellable, error);
	CAMEL_CHECK_GERROR (store, get_folder_sync, folder != NULL, error);

	return folder;
}

static CamelFolder *
store_get_junk_folder_sync (CamelStore *store,
                            GCancellable *cancellable,
                            GError **error)
{
	return store_get_special (store, CAMEL_VTRASH_FOLDER_JUNK);
}

static CamelFolder *
store_get_trash_folder_sync (CamelStore *store,
                             GCancellable *cancellable,
                             GError **error)
{
	return store_get_special (store, CAMEL_VTRASH_FOLDER_TRASH);
}

static gboolean
store_synchronize_sync (CamelStore *store,
                        gboolean expunge,
                        GCancellable *cancellable,
                        GError **error)
{
	GPtrArray *folders;
	CamelFolder *folder;
	gboolean success = TRUE;
	gint i;
	GError *local_error = NULL;

	if (store->folders == NULL)
		return TRUE;

	if (expunge) {
		/* ensure all folders are used when expunging */
		CamelFolderInfo *root, *fi;

		folders = g_ptr_array_new ();
		root = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE | CAMEL_STORE_FOLDER_INFO_NO_VIRTUAL, NULL, NULL);
		fi = root;
		while (fi) {
			CamelFolderInfo *next;

			if ((fi->flags & CAMEL_FOLDER_NOSELECT) == 0) {
				CamelFolder *fldr;

				fldr = camel_store_get_folder_sync (store, fi->full_name, 0, NULL, NULL);
				if (fldr)
					g_ptr_array_add (folders, fldr);
			}

			/* pick the next */
			next = fi->child;
			if (!next)
				next = fi->next;
			if (!next) {
				next = fi->parent;
				while (next) {
					if (next->next) {
						next = next->next;
						break;
					}

					next = next->parent;
				}
			}

			fi = next;
		}

		if (root)
			camel_store_free_folder_info_full (store, root);
	} else {
		/* sync only folders opened until now */
		folders = camel_object_bag_list (store->folders);
	}

	/* We don't sync any vFolders, that is used to update certain
	 * vfolder queries mainly, and we're really only interested in
	 * storing/expunging the physical mails. */
	for (i=0;i<folders->len;i++) {
		folder = folders->pdata[i];
		if (!CAMEL_IS_VEE_FOLDER (folder)
		    && local_error == NULL) {
			camel_folder_synchronize_sync (
				folder, expunge, cancellable, &local_error);
			ignore_no_such_table_exception (&local_error);
		} else if (CAMEL_IS_VEE_FOLDER (folder))
			camel_vee_folder_sync_headers (folder, NULL); /* Literally don't care of vfolder exceptions */
		g_object_unref (folder);
	}

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		success = FALSE;
	}

	g_ptr_array_free (folders, TRUE);

	return success;
}

static gboolean
store_noop_sync (CamelStore *store,
                 GCancellable *cancellable,
                 GError **error)
{
	return TRUE;
}

static void
store_get_folder_thread (GSimpleAsyncResult *simple,
                         GObject *object,
                         GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	async_context->folder = camel_store_get_folder_sync (
		CAMEL_STORE (object), async_context->folder_name_1,
		async_context->flags, cancellable, &error);

	if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}
}

static void
store_get_folder (CamelStore *store,
                  const gchar *folder_name,
                  CamelStoreGetFolderFlags flags,
                  gint io_priority,
                  GCancellable *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->folder_name_1 = g_strdup (folder_name);
	async_context->flags = flags;

	simple = g_simple_async_result_new (
		G_OBJECT (store), callback, user_data, store_get_folder);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, store_get_folder_thread, io_priority, cancellable);

	g_object_unref (simple);
}

static CamelFolder *
store_get_folder_finish (CamelStore *store,
                         GAsyncResult *result,
                         GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (store), store_get_folder), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_object_ref (async_context->folder);
}

static void
store_get_folder_info_thread (GSimpleAsyncResult *simple,
                              GObject *object,
                              GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	async_context->folder_info = camel_store_get_folder_info_sync (
		CAMEL_STORE (object), async_context->folder_name_1,
		async_context->flags, cancellable, &error);

	if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}
}

static void
store_get_folder_info (CamelStore *store,
                       const gchar *top,
                       CamelStoreGetFolderInfoFlags flags,
                       gint io_priority,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->folder_name_1 = g_strdup (top);
	async_context->flags = flags;

	simple = g_simple_async_result_new (
		G_OBJECT (store), callback,
		user_data, store_get_folder_info);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, store_get_folder_info_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static CamelFolderInfo *
store_get_folder_info_finish (CamelStore *store,
                              GAsyncResult *result,
                              GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	CamelFolderInfo *folder_info;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (store), store_get_folder_info), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	folder_info = async_context->folder_info;
	async_context->folder_info = NULL;

	return folder_info;
}

static void
store_get_inbox_folder_thread (GSimpleAsyncResult *simple,
                               GObject *object,
                               GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	async_context->folder = camel_store_get_inbox_folder_sync (
		CAMEL_STORE (object), cancellable, &error);

	if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}
}

static void
store_get_inbox_folder (CamelStore *store,
                        gint io_priority,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);

	simple = g_simple_async_result_new (
		G_OBJECT (store), callback,
		user_data, store_get_inbox_folder);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, store_get_inbox_folder_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static CamelFolder *
store_get_inbox_folder_finish (CamelStore *store,
                               GAsyncResult *result,
                               GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (store), store_get_inbox_folder), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_object_ref (async_context->folder);
}

static void
store_get_junk_folder_thread (GSimpleAsyncResult *simple,
                              GObject *object,
                              GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	async_context->folder = camel_store_get_junk_folder_sync (
		CAMEL_STORE (object), cancellable, &error);

	if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}
}

static void
store_get_junk_folder (CamelStore *store,
                       gint io_priority,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);

	simple = g_simple_async_result_new (
		G_OBJECT (store), callback,
		user_data, store_get_junk_folder);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, store_get_junk_folder_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static CamelFolder *
store_get_junk_folder_finish (CamelStore *store,
                              GAsyncResult *result,
                              GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (store), store_get_folder), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_object_ref (async_context->folder);
}

static void
store_get_trash_folder_thread (GSimpleAsyncResult *simple,
                               GObject *object,
                               GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	async_context->folder = camel_store_get_trash_folder_sync (
		CAMEL_STORE (object), cancellable, &error);

	if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}
}

static void
store_get_trash_folder (CamelStore *store,
                        gint io_priority,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);

	simple = g_simple_async_result_new (
		G_OBJECT (store), callback,
		user_data, store_get_trash_folder);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, store_get_trash_folder_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static CamelFolder *
store_get_trash_folder_finish (CamelStore *store,
                               GAsyncResult *result,
                               GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (store), store_get_trash_folder), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_object_ref (async_context->folder);
}

static void
store_create_folder_thread (GSimpleAsyncResult *simple,
                            GObject *object,
                            GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	async_context->folder_info = camel_store_create_folder_sync (
		CAMEL_STORE (object), async_context->folder_name_1,
		async_context->folder_name_2, cancellable, &error);

	if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}
}

static void
store_create_folder (CamelStore *store,
                     const gchar *parent_name,
                     const gchar *folder_name,
                     gint io_priority,
                     GCancellable *cancellable,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->folder_name_1 = g_strdup (parent_name);
	async_context->folder_name_2 = g_strdup (folder_name);

	simple = g_simple_async_result_new (
		G_OBJECT (store), callback, user_data, store_create_folder);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, store_create_folder_thread, io_priority, cancellable);

	g_object_unref (simple);
}

static CamelFolderInfo *
store_create_folder_finish (CamelStore *store,
                            GAsyncResult *result,
                            GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	CamelFolderInfo *folder_info;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (store), store_create_folder_finish), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	folder_info = async_context->folder_info;
	async_context->folder_info = NULL;

	return folder_info;
}

static void
store_delete_folder_thread (GSimpleAsyncResult *simple,
                            GObject *object,
                            GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	camel_store_delete_folder_sync (
		CAMEL_STORE (object), async_context->folder_name_1,
		cancellable, &error);

	if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}
}

static void
store_delete_folder (CamelStore *store,
                     const gchar *folder_name,
                     gint io_priority,
                     GCancellable *cancellable,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->folder_name_1 = g_strdup (folder_name);

	simple = g_simple_async_result_new (
		G_OBJECT (store), callback, user_data, store_delete_folder);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, store_delete_folder_thread, io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
store_delete_folder_finish (CamelStore *store,
                            GAsyncResult *result,
                            GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (store), store_delete_folder), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
store_rename_folder_thread (GSimpleAsyncResult *simple,
                            GObject *object,
                            GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	camel_store_rename_folder_sync (
		CAMEL_STORE (object), async_context->folder_name_1,
		async_context->folder_name_2, cancellable, &error);

	if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}
}

static void
store_rename_folder (CamelStore *store,
                     const gchar *old_name,
                     const gchar *new_name,
                     gint io_priority,
                     GCancellable *cancellable,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->folder_name_1 = g_strdup (old_name);
	async_context->folder_name_2 = g_strdup (new_name);

	simple = g_simple_async_result_new (
		G_OBJECT (store), callback, user_data, store_rename_folder);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, store_rename_folder_thread, io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
store_rename_folder_finish (CamelStore *store,
                            GAsyncResult *result,
                            GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (store), store_rename_folder), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
store_subscribe_folder_thread (GSimpleAsyncResult *simple,
                               GObject *object,
                               GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	camel_store_subscribe_folder_sync (
		CAMEL_STORE (object), async_context->folder_name_1,
		cancellable, &error);

	if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}
}

static void
store_subscribe_folder (CamelStore *store,
                        const gchar *folder_name,
                        gint io_priority,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->folder_name_1 = g_strdup (folder_name);

	simple = g_simple_async_result_new (
		G_OBJECT (store), callback,
		user_data, store_subscribe_folder);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, store_subscribe_folder_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
store_subscribe_folder_finish (CamelStore *store,
                               GAsyncResult *result,
                               GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (store), store_subscribe_folder), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
store_unsubscribe_folder_thread (GSimpleAsyncResult *simple,
                                 GObject *object,
                                 GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	camel_store_unsubscribe_folder_sync (
		CAMEL_STORE (object), async_context->folder_name_1,
		cancellable, &error);

	if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}
}

static void
store_unsubscribe_folder (CamelStore *store,
                          const gchar *folder_name,
                          gint io_priority,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->folder_name_1 = g_strdup (folder_name);

	simple = g_simple_async_result_new (
		G_OBJECT (store), callback,
		user_data, store_unsubscribe_folder);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, store_unsubscribe_folder_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
store_unsubscribe_folder_finish (CamelStore *store,
                                 GAsyncResult *result,
                                 GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (store), store_unsubscribe_folder), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
store_synchronize_thread (GSimpleAsyncResult *simple,
                          GObject *object,
                          GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	camel_store_synchronize_sync (
		CAMEL_STORE (object), async_context->expunge,
		cancellable, &error);

	if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}
}

static void
store_synchronize (CamelStore *store,
                   gboolean expunge,
                   gint io_priority,
                   GCancellable *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->expunge = expunge;

	simple = g_simple_async_result_new (
		G_OBJECT (store), callback, user_data, store_synchronize);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, store_synchronize_thread, io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
store_synchronize_finish (CamelStore *store,
                          GAsyncResult *result,
                          GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (store), store_synchronize), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
store_noop_thread (GSimpleAsyncResult *simple,
                   GObject *object,
                   GCancellable *cancellable)
{
	GError *error = NULL;

	camel_store_noop_sync (CAMEL_STORE (object), cancellable, &error);

	if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}
}

static void
store_noop (CamelStore *store,
            gint io_priority,
            GCancellable *cancellable,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
	GSimpleAsyncResult *simple;

	simple = g_simple_async_result_new (
		G_OBJECT (store), callback, user_data, store_noop);

	g_simple_async_result_run_in_thread (
		simple, store_noop_thread, io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
store_noop_finish (CamelStore *store,
                   GAsyncResult *result,
                   GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (store), store_noop), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
camel_store_class_init (CamelStoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;

	g_type_class_add_private (class, sizeof (CamelStorePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = store_finalize;
	object_class->constructed = store_constructed;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->construct = store_construct;

	class->hash_folder_name = g_str_hash;
	class->compare_folder_name = g_str_equal;
	class->can_refresh_folder = store_can_refresh_folder;

	class->get_inbox_folder_sync = store_get_inbox_folder_sync;
	class->get_junk_folder_sync = store_get_junk_folder_sync;
	class->get_trash_folder_sync = store_get_trash_folder_sync;
	class->synchronize_sync = store_synchronize_sync;
	class->noop_sync = store_noop_sync;

	class->get_folder = store_get_folder;
	class->get_folder_finish = store_get_folder_finish;
	class->get_folder_info = store_get_folder_info;
	class->get_folder_info_finish = store_get_folder_info_finish;
	class->get_inbox_folder = store_get_inbox_folder;
	class->get_inbox_folder_finish = store_get_inbox_folder_finish;
	class->get_junk_folder = store_get_junk_folder;
	class->get_junk_folder_finish = store_get_junk_folder_finish;
	class->get_trash_folder = store_get_trash_folder;
	class->get_trash_folder_finish = store_get_trash_folder_finish;
	class->create_folder = store_create_folder;
	class->create_folder_finish = store_create_folder_finish;
	class->delete_folder = store_delete_folder;
	class->delete_folder_finish = store_delete_folder_finish;
	class->rename_folder = store_rename_folder;
	class->rename_folder_finish = store_rename_folder_finish;
	class->subscribe_folder = store_subscribe_folder;
	class->subscribe_folder_finish = store_subscribe_folder_finish;
	class->unsubscribe_folder = store_unsubscribe_folder;
	class->unsubscribe_folder_finish = store_unsubscribe_folder_finish;
	class->synchronize = store_synchronize;
	class->synchronize_finish = store_synchronize_finish;
	class->noop = store_noop;
	class->noop_finish = store_noop_finish;

	signals[FOLDER_CREATED] = g_signal_new (
		"folder-created",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelStoreClass, folder_created),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER);

	signals[FOLDER_DELETED] = g_signal_new (
		"folder-deleted",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelStoreClass, folder_deleted),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER);

	signals[FOLDER_OPENED] = g_signal_new (
		"folder-opened",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelStoreClass, folder_opened),
		NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE, 1,
		CAMEL_TYPE_FOLDER);

	signals[FOLDER_RENAMED] = g_signal_new (
		"folder-renamed",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelStoreClass, folder_renamed),
		NULL, NULL,
		camel_marshal_VOID__STRING_POINTER,
		G_TYPE_NONE, 2,
		G_TYPE_STRING,
		G_TYPE_POINTER);

	signals[FOLDER_SUBSCRIBED] = g_signal_new (
		"folder-subscribed",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelStoreClass, folder_subscribed),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER);

	signals[FOLDER_UNSUBSCRIBED] = g_signal_new (
		"folder-unsubscribed",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelStoreClass, folder_unsubscribed),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER);
}

static void
camel_store_init (CamelStore *store)
{
	store->priv = CAMEL_STORE_GET_PRIVATE (store);

	/* set vtrash and vjunk on by default */
	store->flags = CAMEL_STORE_VTRASH | CAMEL_STORE_VJUNK;
	store->mode = CAMEL_STORE_READ | CAMEL_STORE_WRITE;

	g_static_rec_mutex_init (&store->priv->folder_lock);
}

GQuark
camel_store_error_quark (void)
{
	static GQuark quark = 0;

	if (G_UNLIKELY (quark == 0)) {
		const gchar *string = "camel-store-error-quark";
		quark = g_quark_from_static_string (string);
	}

	return quark;
}

/* deletes folder/removes it from the folder cache, if it's there */
static void
cs_delete_cached_folder (CamelStore *store,
                         const gchar *folder_name)
{
	CamelFolder *folder;

	if (store->folders
	    && (folder = camel_object_bag_get (store->folders, folder_name))) {
		CamelVeeFolder *vfolder;

		if ((store->flags & CAMEL_STORE_VTRASH)
		    && (vfolder = camel_object_bag_get (store->folders, CAMEL_VTRASH_NAME))) {
			camel_vee_folder_remove_folder (vfolder, folder);
			g_object_unref (vfolder);
		}

		if ((store->flags & CAMEL_STORE_VJUNK)
		    && (vfolder = camel_object_bag_get (store->folders, CAMEL_VJUNK_NAME))) {
			camel_vee_folder_remove_folder (vfolder, folder);
			g_object_unref (vfolder);
		}

		camel_folder_delete (folder);

		camel_object_bag_remove (store->folders, folder);
		g_object_unref (folder);
	}
}

/**
 * camel_store_folder_created:
 * @store: a #CamelStore
 * @folder_info: information about the created folder
 *
 * Emits the #CamelStore::folder-created signal from an idle source on
 * the main loop.  The idle source's priority is #G_PRIORITY_DEFAULT_IDLE.
 *
 * This function is only intended for Camel providers.
 *
 * Since: 2.32
 **/
void
camel_store_folder_created (CamelStore *store,
                            CamelFolderInfo *folder_info)
{
	SignalData *data;

	g_return_if_fail (CAMEL_IS_STORE (store));
	g_return_if_fail (folder_info != NULL);

	data = g_slice_new0 (SignalData);
	data->store = g_object_ref (store);
	data->folder_info = camel_folder_info_clone (folder_info);

	g_idle_add_full (
		G_PRIORITY_DEFAULT_IDLE,
		(GSourceFunc) store_emit_folder_created_cb,
		data, (GDestroyNotify) signal_data_free);
}

/**
 * camel_store_folder_deleted:
 * @store: a #CamelStore
 * @folder_info: information about the deleted folder
 *
 * Emits the #CamelStore::folder-deleted signal from an idle source on
 * the main loop.  The idle source's priority is #G_PRIORITY_DEFAULT_IDLE.
 *
 * This function is only intended for Camel providers.
 *
 * Since: 2.32
 **/
void
camel_store_folder_deleted (CamelStore *store,
                            CamelFolderInfo *folder_info)
{
	SignalData *data;

	g_return_if_fail (CAMEL_IS_STORE (store));
	g_return_if_fail (folder_info != NULL);

	data = g_slice_new0 (SignalData);
	data->store = g_object_ref (store);
	data->folder_info = camel_folder_info_clone (folder_info);

	g_idle_add_full (
		G_PRIORITY_DEFAULT_IDLE,
		(GSourceFunc) store_emit_folder_deleted_cb,
		data, (GDestroyNotify) signal_data_free);
}

/**
 * camel_store_folder_opened:
 * @store: a #CamelStore
 * @folder: the #CamelFolder that was opened
 *
 * Emits the #CamelStore::folder-opened signal from an idle source on
 * the main loop.  The idle source's priority is #G_PRIORITY_DEFAULT_IDLE.
 *
 * This function is only intended for Camel providers.
 *
 * Since: 3.0
 **/
void
camel_store_folder_opened (CamelStore *store,
                           CamelFolder *folder)
{
	SignalData *data;

	g_return_if_fail (CAMEL_IS_STORE (store));
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	data = g_slice_new0 (SignalData);
	data->store = g_object_ref (store);
	data->folder = g_object_ref (folder);

	g_idle_add_full (
		G_PRIORITY_DEFAULT_IDLE,
		(GSourceFunc) store_emit_folder_opened_cb,
		data, (GDestroyNotify) signal_data_free);
}

/**
 * camel_store_folder_renamed:
 * @store: a #CamelStore
 * @old_name: the old name of the folder
 * @folder_info: information about the renamed folder
 *
 * Emits the #CamelStore::folder-renamed signal from an idle source on
 * the main loop.  The idle source's priority is #G_PRIORITY_DEFAULT_IDLE.
 *
 * This function is only intended for Camel providers.
 *
 * Since: 2.32
 **/
void
camel_store_folder_renamed (CamelStore *store,
                            const gchar *old_name,
                            CamelFolderInfo *folder_info)
{
	SignalData *data;

	g_return_if_fail (CAMEL_IS_STORE (store));
	g_return_if_fail (old_name != NULL);
	g_return_if_fail (folder_info != NULL);

	data = g_slice_new0 (SignalData);
	data->store = g_object_ref (store);
	data->folder_info = camel_folder_info_clone (folder_info);
	data->folder_name = g_strdup (old_name);

	g_idle_add_full (
		G_PRIORITY_DEFAULT_IDLE,
		(GSourceFunc) store_emit_folder_renamed_cb,
		data, (GDestroyNotify) signal_data_free);
}

/**
 * camel_store_folder_subscribed:
 * @store: a #CamelStore
 * @folder_info: information about the subscribed folder
 *
 * Emits the #CamelStore::folder-subscribed signal from an idle source on
 * the main loop.  The idle source's priority is #G_PRIORITY_DEFAULT_IDLE.
 *
 * This function is only intended for Camel providers.
 *
 * Since: 2.32
 **/
void
camel_store_folder_subscribed (CamelStore *store,
                               CamelFolderInfo *folder_info)
{
	SignalData *data;

	g_return_if_fail (CAMEL_IS_STORE (store));
	g_return_if_fail (folder_info != NULL);

	data = g_slice_new0 (SignalData);
	data->store = g_object_ref (store);
	data->folder_info = camel_folder_info_clone (folder_info);

	g_idle_add_full (
		G_PRIORITY_DEFAULT_IDLE,
		(GSourceFunc) store_emit_folder_subscribed_cb,
		data, (GDestroyNotify) signal_data_free);
}

/**
 * camel_store_folder_unsubscribed:
 * @store: a #CamelStore
 * @folder_info: information about the unsubscribed folder
 *
 * Emits the #CamelStore::folder-unsubscribed signal from an idle source on
 * the main loop.  The idle source's priority is #G_PRIORITY_DEFAULT_IDLE.
 *
 * This function is only intended for Camel providers.
 *
 * Since: 2.32
 **/
void
camel_store_folder_unsubscribed (CamelStore *store,
                                 CamelFolderInfo *folder_info)
{
	SignalData *data;

	g_return_if_fail (CAMEL_IS_STORE (store));
	g_return_if_fail (folder_info != NULL);

	data = g_slice_new0 (SignalData);
	data->store = g_object_ref (store);
	data->folder_info = camel_folder_info_clone (folder_info);

	g_idle_add_full (
		G_PRIORITY_DEFAULT_IDLE,
		(GSourceFunc) store_emit_folder_unsubscribed_cb,
		data, (GDestroyNotify) signal_data_free);
}

static void
add_special_info (CamelStore *store,
                  CamelFolderInfo *info,
                  const gchar *name,
                  const gchar *translated,
                  gboolean unread_count,
                  CamelFolderInfoFlags flags)
{
	CamelFolderInfo *fi, *vinfo, *parent;
	gchar *uri, *path;
	CamelURL *url;

	g_return_if_fail (info != NULL);

	parent = NULL;
	for (fi = info; fi; fi = fi->next) {
		if (!strcmp (fi->full_name, name))
			break;
		parent = fi;
	}

	/* create our vTrash/vJunk URL */
	url = camel_url_new (info->uri, NULL);
	if (((CamelService *) store)->provider->url_flags & CAMEL_URL_FRAGMENT_IS_PATH) {
		camel_url_set_fragment (url, name);
	} else {
		path = g_strdup_printf ("/%s", name);
		camel_url_set_path (url, path);
		g_free (path);
	}

	uri = camel_url_to_string (url, CAMEL_URL_HIDE_ALL);
	camel_url_free (url);

	if (fi) {
		/* We're going to replace the physical Trash/Junk folder with our vTrash/vJunk folder */
		vinfo = fi;
		g_free (vinfo->full_name);
		g_free (vinfo->name);
		g_free (vinfo->uri);
	} else {
		/* There wasn't a Trash/Junk folder so create a new folder entry */
		vinfo = camel_folder_info_new ();

		g_assert (parent != NULL);

		vinfo->flags |= CAMEL_FOLDER_NOINFERIORS | CAMEL_FOLDER_SUBSCRIBED;

		/* link it into the right spot */
		vinfo->next = parent->next;
		parent->next = vinfo;
	}

	/* Fill in the new fields */
	vinfo->flags |= flags;
	vinfo->full_name = g_strdup (name);
	vinfo->name = g_strdup (translated);
	vinfo->uri = uri;
	if (!unread_count)
		vinfo->unread = -1;
}

static void
dump_fi (CamelFolderInfo *fi, gint depth)
{
	gchar *s;

	s = g_alloca (depth+1);
	memset (s, ' ', depth);
	s[depth] = 0;

	while (fi) {
		printf("%suri: %s\n", s, fi->uri);
		printf("%sfull_name: %s\n", s, fi->full_name);
		printf("%sflags: %08x\n", s, fi->flags);
		dump_fi (fi->child, depth+2);
		fi = fi->next;
	}
}

/**
 * camel_store_free_folder_info:
 * @store: a #CamelStore
 * @fi: a #CamelFolderInfo as gotten via #camel_store_get_folder_info
 *
 * Frees the data returned by #camel_store_get_folder_info. If @fi is %NULL,
 * nothing is done, the routine simply returns.
 **/
void
camel_store_free_folder_info (CamelStore *store,
                              CamelFolderInfo *fi)
{
	CamelStoreClass *class;

	g_return_if_fail (CAMEL_IS_STORE (store));

	if (fi == NULL)
		return;

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_if_fail (class->free_folder_info != NULL);

	class->free_folder_info (store, fi);
}

/**
 * camel_store_free_folder_info_full:
 * @store: a #CamelStore
 * @fi: a #CamelFolderInfo as gotten via #camel_store_get_folder_info
 *
 * An implementation for #CamelStore::free_folder_info. Frees all
 * of the data.
 **/
void
camel_store_free_folder_info_full (CamelStore *store,
                                   CamelFolderInfo *fi)
{
	camel_folder_info_free (fi);
}

/**
 * camel_store_free_folder_info_nop:
 * @store: a #CamelStore
 * @fi: a #CamelFolderInfo as gotten via #camel_store_get_folder_info
 *
 * An implementation for #CamelStore::free_folder_info. Does nothing.
 **/
void
camel_store_free_folder_info_nop (CamelStore *store,
                                  CamelFolderInfo *fi)
{
	;
}

/**
 * camel_folder_info_free:
 * @fi: a #CamelFolderInfo
 *
 * Frees @fi.
 **/
void
camel_folder_info_free (CamelFolderInfo *fi)
{
	if (fi != NULL) {
		camel_folder_info_free (fi->next);
		camel_folder_info_free (fi->child);
		g_free (fi->name);
		g_free (fi->full_name);
		g_free (fi->uri);
		g_slice_free (CamelFolderInfo, fi);
	}
}

/**
 * camel_folder_info_new:
 *
 * Returns: a new empty CamelFolderInfo instance
 *
 * Since: 2.22
 **/
CamelFolderInfo *
camel_folder_info_new (void)
{
	return g_slice_new0 (CamelFolderInfo);
}

static gint
folder_info_cmp (gconstpointer ap,
                 gconstpointer bp)
{
	const CamelFolderInfo *a = ((CamelFolderInfo **)ap)[0];
	const CamelFolderInfo *b = ((CamelFolderInfo **)bp)[0];

	return strcmp (a->full_name, b->full_name);
}

/**
 * camel_folder_info_build:
 * @folders: an array of #CamelFolderInfo
 * @namespace: an ignorable prefix on the folder names
 * @separator: the hieararchy separator character
 * @short_names: %TRUE if the (short) name of a folder is the part after
 * the last @separator in the full name. %FALSE if it is the full name.
 *
 * This takes an array of folders and attaches them together according
 * to the hierarchy described by their full_names and @separator. If
 * @namespace is non-%NULL, then it will be ignored as a full_name
 * prefix, for purposes of comparison. If necessary,
 * #camel_folder_info_build will create additional #CamelFolderInfo with
 * %NULL urls to fill in gaps in the tree. The value of @short_names
 * is used in constructing the names of these intermediate folders.
 *
 * NOTE: This is deprected, do not use this.
 * FIXME: remove this/move it to imap, which is the only user of it now.
 *
 * Returns: the top level of the tree of linked folder info.
 **/
CamelFolderInfo *
camel_folder_info_build (GPtrArray *folders,
                         const gchar *namespace,
                         gchar separator,
                         gboolean short_names)
{
	CamelFolderInfo *fi, *pfi, *top = NULL, *tail = NULL;
	GHashTable *hash;
	gchar *p, *pname;
	gint i, nlen;

	if (!namespace)
		namespace = "";
	nlen = strlen (namespace);

	qsort (folders->pdata, folders->len, sizeof (folders->pdata[0]), folder_info_cmp);

	/* Hash the folders. */
	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	for (i = 0; i < folders->len; i++) {
		fi = folders->pdata[i];
		g_hash_table_insert (hash, g_strdup (fi->full_name), fi);
	}

	/* Now find parents. */
	for (i = 0; i < folders->len; i++) {
		fi = folders->pdata[i];
		if (!strncmp (namespace, fi->full_name, nlen)
		    && (p = strrchr (fi->full_name+nlen, separator))) {
			pname = g_strndup (fi->full_name, p - fi->full_name);
			pfi = g_hash_table_lookup (hash, pname);
			if (pfi) {
				g_free (pname);
			} else {
				/* we are missing a folder in the heirarchy so
				   create a fake folder node */
				const gchar *path;
				CamelURL *url;
				gchar *sep;

				pfi = camel_folder_info_new ();
				if (short_names) {
					pfi->name = strrchr (pname, separator);
					if (pfi->name)
						pfi->name = g_strdup (pfi->name + 1);
					else
						pfi->name = g_strdup (pname);
				} else
					pfi->name = g_strdup (pname);

				url = camel_url_new (fi->uri, NULL);
				if (url->fragment)
					path = url->fragment;
				else
					path = url->path + 1;

				sep = strrchr (path, separator);
				if (sep)
					*sep = '\0';
				else {
					d(g_warning ("huh, no \"%c\" in \"%s\"?", separator, fi->uri));
				}

				pfi->full_name = g_strdup (path);

				/* since this is a "fake" folder node, it is not selectable */
				camel_url_set_param (url, "noselect", "yes");
				pfi->uri = camel_url_to_string (url, 0);
				camel_url_free (url);

				g_hash_table_insert (hash, pname, pfi);
				g_ptr_array_add (folders, pfi);
			}
			tail = (CamelFolderInfo *)&pfi->child;
			while (tail->next)
				tail = tail->next;
			tail->next = fi;
			fi->parent = pfi;
		} else if (!top || !g_ascii_strcasecmp (fi->full_name, "Inbox"))
			top = fi;
	}
	g_hash_table_destroy (hash);

	/* Link together the top-level folders */
	tail = top;
	for (i = 0; i < folders->len; i++) {
		fi = folders->pdata[i];

		if (fi->child)
			fi->flags &= ~CAMEL_FOLDER_NOCHILDREN;

		if (fi->parent || fi == top)
			continue;
		if (tail == NULL) {
			tail = fi;
			top = fi;
		} else {
			tail->next = fi;
			tail = fi;
		}
	}

	return top;
}

static CamelFolderInfo *
folder_info_clone_rec (CamelFolderInfo *fi,
                       CamelFolderInfo *parent)
{
	CamelFolderInfo *info;

	info = camel_folder_info_new ();
	info->parent = parent;
	info->uri = g_strdup (fi->uri);
	info->name = g_strdup (fi->name);
	info->full_name = g_strdup (fi->full_name);
	info->unread = fi->unread;
	info->flags = fi->flags;

	if (fi->next)
		info->next = folder_info_clone_rec (fi->next, parent);
	else
		info->next = NULL;

	if (fi->child)
		info->child = folder_info_clone_rec (fi->child, info);
	else
		info->child = NULL;

	return info;
}

/**
 * camel_folder_info_clone:
 * @fi: a #CamelFolderInfo
 *
 * Clones @fi recursively.
 *
 * Returns: the cloned #CamelFolderInfo tree.
 **/
CamelFolderInfo *
camel_folder_info_clone (CamelFolderInfo *fi)
{
	if (fi == NULL)
		return NULL;

	return folder_info_clone_rec (fi, NULL);
}

/**
 * camel_store_supports_subscriptions:
 * @store: a #CamelStore
 *
 * Get whether or not @store supports subscriptions to folders.
 *
 * Returns: %TRUE if folder subscriptions are supported or %FALSE otherwise
 **/
gboolean
camel_store_supports_subscriptions (CamelStore *store)
{
	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);

	return (store->flags & CAMEL_STORE_SUBSCRIPTIONS);
}

/**
 * camel_store_folder_is_subscribed:
 * @store: a #CamelStore
 * @folder_name: full path of the folder
 *
 * Find out if a folder has been subscribed to.
 *
 * Returns: %TRUE if the folder has been subscribed to or %FALSE otherwise
 **/
gboolean
camel_store_folder_is_subscribed (CamelStore *store,
                                  const gchar *folder_name)
{
	CamelStoreClass *class;
	gboolean is_subscribed;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);
	g_return_val_if_fail (store->flags & CAMEL_STORE_SUBSCRIPTIONS, FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->folder_is_subscribed != NULL, FALSE);

	camel_store_lock (store, CAMEL_STORE_FOLDER_LOCK);

	is_subscribed = class->folder_is_subscribed (store, folder_name);

	camel_store_unlock (store, CAMEL_STORE_FOLDER_LOCK);

	return is_subscribed;
}

/**
 * camel_store_folder_uri_equal:
 * @store: a #CamelStore
 * @uri0: a folder uri
 * @uri1: another folder uri
 *
 * Compares two folder uris to check that they are equal.
 *
 * Returns: %TRUE if they are equal or %FALSE otherwise
 **/
gint
camel_store_folder_uri_equal (CamelStore *store,
                              const gchar *uri0,
                              const gchar *uri1)
{
	CamelStoreClass *class;
	CamelProvider *provider;
	CamelURL *url0, *url1;
	gint equal;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);
	g_return_val_if_fail (uri0 && uri1, FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->compare_folder_name != NULL, FALSE);

	provider = ((CamelService *) store)->provider;

	if (!(url0 = camel_url_new (uri0, NULL)))
		return FALSE;

	if (!(url1 = camel_url_new (uri1, NULL))) {
		camel_url_free (url0);
		return FALSE;
	}

	if ((equal = provider->url_equal (url0, url1))) {
		const gchar *name0, *name1;

		if (provider->url_flags & CAMEL_URL_FRAGMENT_IS_PATH) {
			name0 = url0->fragment;
			name1 = url1->fragment;
		} else {
			name0 = url0->path && url0->path[0] == '/' ? url0->path + 1 : url0->path;
			name1 = url1->path && url1->path[0] == '/' ? url1->path + 1 : url1->path;
		}

		if (name0 == NULL)
			g_warning("URI is badly formed, missing folder name: %s", uri0);

		if (name1 == NULL)
			g_warning("URI is badly formed, missing folder name: %s", uri1);

		equal = name0 && name1 && class->compare_folder_name (name0, name1);
	}

	camel_url_free (url0);
	camel_url_free (url1);

	return equal;
}

/**
 * camel_store_can_refresh_folder
 * @store: a #CamelStore
 * @info: a #CamelFolderInfo
 * @error: return location for a #GError, or %NULL
 *
 * Returns if this folder (param info) should be checked for new mail or not.
 * It should not look into sub infos (info->child) or next infos, it should
 * return value only for the actual folder info.
 * Default behavior is that all Inbox folders are intended to be refreshed.
 *
 * Returns: whether folder should be checked for new mails
 *
 * Since: 2.22
 **/
gboolean
camel_store_can_refresh_folder (CamelStore *store,
                                CamelFolderInfo *info,
                                GError **error)
{
	CamelStoreClass *class;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);
	g_return_val_if_fail (info != NULL, FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->can_refresh_folder != NULL, FALSE);

	return class->can_refresh_folder (store, info, error);
}

/**
 * camel_store_lock:
 * @store: a #CamelStore
 * @lock: lock type to lock
 *
 * Locks #store's #lock. Unlock it with camel_store_unlock().
 *
 * Since: 2.32
 **/
void
camel_store_lock (CamelStore *store,
                  CamelStoreLock lock)
{
	g_return_if_fail (CAMEL_IS_STORE (store));

	switch (lock) {
		case CAMEL_STORE_FOLDER_LOCK:
			g_static_rec_mutex_lock (&store->priv->folder_lock);
			break;
		default:
			g_return_if_reached ();
	}
}

/**
 * camel_store_unlock:
 * @store: a #CamelStore
 * @lock: lock type to unlock
 *
 * Unlocks #store's #lock, previously locked with camel_store_lock().
 *
 * Since: 2.32
 **/
void
camel_store_unlock (CamelStore *store,
                    CamelStoreLock lock)
{
	g_return_if_fail (CAMEL_IS_STORE (store));

	switch (lock) {
		case CAMEL_STORE_FOLDER_LOCK:
			g_static_rec_mutex_unlock (&store->priv->folder_lock);
			break;
		default:
			g_return_if_reached ();
	}
}

/**
 * camel_store_get_folder_sync:
 * @store: a #CamelStore
 * @folder_name: name of the folder to get
 * @flags: folder flags (create, save body index, etc)
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets a specific folder object from @store by name.
 *
 * Returns: the requested #CamelFolder object, or %NULL on error
 *
 * Since: 2.92
 **/
CamelFolder *
camel_store_get_folder_sync (CamelStore *store,
                             const gchar *folder_name,
                             CamelStoreGetFolderFlags flags,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelStoreClass *class;
	CamelFolder *folder = NULL;

	g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);
	g_return_val_if_fail (folder_name != NULL, NULL);

	class = CAMEL_STORE_GET_CLASS (store);

	/* O_EXCL doesn't make sense if we aren't requesting to also create the folder if it doesn't exist */
	if (!(flags & CAMEL_STORE_FOLDER_CREATE))
		flags &= ~CAMEL_STORE_FOLDER_EXCL;

	if (store->folders) {
		/* Try cache first. */
		folder = camel_object_bag_reserve (store->folders, folder_name);
		if (folder && (flags & CAMEL_STORE_FOLDER_EXCL)) {
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Cannot create folder '%s': folder exists"),
				folder_name);
			camel_object_bag_abort (store->folders, folder_name);
			g_object_unref (folder);
			return NULL;
		}
	}

	if (!folder) {

		if (flags & CAMEL_STORE_IS_MIGRATING) {
			if ((store->flags & CAMEL_STORE_VTRASH) && strcmp (folder_name, CAMEL_VTRASH_NAME) == 0) {
				if (store->folders)
					camel_object_bag_abort (store->folders, folder_name);
				return NULL;
			}

			if ((store->flags & CAMEL_STORE_VJUNK) && strcmp (folder_name, CAMEL_VJUNK_NAME) == 0) {
				if (store->folders)
						camel_object_bag_abort (store->folders, folder_name);
				return NULL;
			}
		}

		if ((store->flags & CAMEL_STORE_VTRASH) && strcmp (folder_name, CAMEL_VTRASH_NAME) == 0) {
			folder = class->get_trash_folder_sync (store, cancellable, error);
			CAMEL_CHECK_GERROR (store, get_trash_folder_sync, folder != NULL, error);
		} else if ((store->flags & CAMEL_STORE_VJUNK) && strcmp (folder_name, CAMEL_VJUNK_NAME) == 0) {
			folder = class->get_junk_folder_sync (store, cancellable, error);
			CAMEL_CHECK_GERROR (store, get_junk_folder_sync, folder != NULL, error);
		} else {
			folder = class->get_folder_sync (
				store, folder_name, flags, cancellable, error);
			CAMEL_CHECK_GERROR (store, get_folder_sync, folder != NULL, error);

			if (folder) {
				CamelVeeFolder *vfolder;

				if ((store->flags & CAMEL_STORE_VTRASH)
				    && (vfolder = camel_object_bag_get (store->folders, CAMEL_VTRASH_NAME))) {
					camel_vee_folder_add_folder (vfolder, folder);
					g_object_unref (vfolder);
				}

				if ((store->flags & CAMEL_STORE_VJUNK)
				    && (vfolder = camel_object_bag_get (store->folders, CAMEL_VJUNK_NAME))) {
					camel_vee_folder_add_folder (vfolder, folder);
					g_object_unref (vfolder);
				}
			}
		}

		if (store->folders) {
			if (folder)
				camel_object_bag_add (store->folders, folder_name, folder);
			else
				camel_object_bag_abort (store->folders, folder_name);
		}

		if (folder)
			camel_store_folder_opened (store, folder);
	}

	return folder;
}

/**
 * camel_store_get_folder:
 * @store: a #CamelStore
 * @folder_name: name of the folder to get
 * @flags: folder flags (create, save body index, etc)
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously gets a specific folder object from @store by name.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_store_get_folder_finish() to get the result of the operation.
 *
 * Since: 2.92
 **/
void
camel_store_get_folder (CamelStore *store,
                        const gchar *folder_name,
                        CamelStoreGetFolderFlags flags,
                        gint io_priority,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	CamelStoreClass *class;

	g_return_if_fail (CAMEL_IS_STORE (store));
	g_return_if_fail (folder_name != NULL);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_if_fail (class->get_folder != NULL);

	class->get_folder (
		store, folder_name, flags, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_store_get_folder_finish:
 * @store: a #CamelStore
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_store_get_folder().
 *
 * Returns: the requested #CamelFolder object, or %NULL on error
 *
 * Since: 2.92
 **/
CamelFolder *
camel_store_get_folder_finish (CamelStore *store,
                               GAsyncResult *result,
                               GError **error)
{
	CamelStoreClass *class;

	g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->get_folder_finish != NULL, NULL);

	return class->get_folder_finish (store, result, error);
}

/**
 * camel_store_get_folder_info_sync:
 * @store: a #CamelStore
 * @top: the name of the folder to start from
 * @flags: various CAMEL_STORE_FOLDER_INFO_* flags to control behavior
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * This fetches information about the folder structure of @store,
 * starting with @top, and returns a tree of #CamelFolderInfo
 * structures. If @flags includes %CAMEL_STORE_FOLDER_INFO_SUBSCRIBED,
 * only subscribed folders will be listed.   If the store doesn't support
 * subscriptions, then it will list all folders.  If @flags includes
 * %CAMEL_STORE_FOLDER_INFO_RECURSIVE, the returned tree will include
 * all levels of hierarchy below @top. If not, it will only include
 * the immediate subfolders of @top. If @flags includes
 * %CAMEL_STORE_FOLDER_INFO_FAST, the unread_message_count fields of
 * some or all of the structures may be set to %-1, if the store cannot
 * determine that information quickly.  If @flags includes
 * %CAMEL_STORE_FOLDER_INFO_NO_VIRTUAL, don't include special virtual
 * folders (such as vTrash or vJunk).
 *
 * The returned #CamelFolderInfo tree should be freed with
 * camel_store_free_folder_info().
 *
 * The CAMEL_STORE_FOLDER_INFO_FAST flag should be considered
 * deprecated; most backends will behave the same whether it is
 * supplied or not.  The only guaranteed way to get updated folder
 * counts is to both open the folder and invoke refresh_info() it.
 *
 * Returns: a #CamelFolderInfo tree, or %NULL on error
 *
 * Since: 2.92
 **/
CamelFolderInfo *
camel_store_get_folder_info_sync (CamelStore *store,
                                  const gchar *top,
                                  CamelStoreGetFolderInfoFlags flags,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelStoreClass *class;
	CamelFolderInfo *info;

	g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->get_folder_info_sync != NULL, NULL);

	info = class->get_folder_info_sync (
		store, top, flags, cancellable, error);
	if (!(flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED))
		CAMEL_CHECK_GERROR (
			store, get_folder_info_sync, info != NULL, error);

	if (info && (top == NULL || *top == '\0') && (flags & CAMEL_STORE_FOLDER_INFO_NO_VIRTUAL) == 0) {
		if (info->uri && (store->flags & CAMEL_STORE_VTRASH))
			/* the name of the Trash folder, used for deleted messages */
			add_special_info (store, info, CAMEL_VTRASH_NAME, _("Trash"), FALSE, CAMEL_FOLDER_VIRTUAL|CAMEL_FOLDER_SYSTEM|CAMEL_FOLDER_VTRASH|CAMEL_FOLDER_TYPE_TRASH);
		if (info->uri && (store->flags & CAMEL_STORE_VJUNK))
			/* the name of the Junk folder, used for spam messages */
			add_special_info (store, info, CAMEL_VJUNK_NAME, _("Junk"), TRUE, CAMEL_FOLDER_VIRTUAL|CAMEL_FOLDER_SYSTEM|CAMEL_FOLDER_VTRASH|CAMEL_FOLDER_TYPE_JUNK);
	} else if (!info && top && (flags & CAMEL_STORE_FOLDER_INFO_NO_VIRTUAL) == 0) {
		CamelFolderInfo *root_info = NULL;

		if ((store->flags & CAMEL_STORE_VTRASH) != 0 && g_str_equal (top, CAMEL_VTRASH_NAME)) {
			root_info = class->get_folder_info_sync (store, NULL, flags & (~CAMEL_STORE_FOLDER_INFO_RECURSIVE), cancellable, error);
			if (root_info && root_info->uri)
				add_special_info (store, root_info, CAMEL_VTRASH_NAME, _("Trash"), FALSE, CAMEL_FOLDER_VIRTUAL|CAMEL_FOLDER_SYSTEM|CAMEL_FOLDER_VTRASH|CAMEL_FOLDER_TYPE_TRASH);
		} else if ((store->flags & CAMEL_STORE_VJUNK) != 0 && g_str_equal (top, CAMEL_VJUNK_NAME)) {
			root_info = class->get_folder_info_sync (store, NULL, flags & (~CAMEL_STORE_FOLDER_INFO_RECURSIVE), cancellable, error);
			if (root_info && root_info->uri)
				add_special_info (store, root_info, CAMEL_VJUNK_NAME, _("Junk"), TRUE, CAMEL_FOLDER_VIRTUAL|CAMEL_FOLDER_SYSTEM|CAMEL_FOLDER_VTRASH|CAMEL_FOLDER_TYPE_JUNK);
		}

		if (root_info) {
			info = root_info->next;
			root_info->next = NULL;
			info->next = NULL;
			info->parent = NULL;

			camel_store_free_folder_info (store, root_info);
		}
	}

	if (camel_debug_start("store:folder_info")) {
		gchar *url = camel_url_to_string (((CamelService *)store)->url, CAMEL_URL_HIDE_ALL);
		printf("Get folder info(%p:%s, '%s') =\n", (gpointer) store, url, top?top:"<null>");
		g_free (url);
		dump_fi (info, 2);
		camel_debug_end ();
	}

	return info;
}

/**
 * camel_store_get_folder_info:
 * @store: a #CamelStore
 * @top: the name of the folder to start from
 * @flags: various CAMEL_STORE_FOLDER_INFO_* flags to control behavior
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously fetches information about the folder structure of @store,
 * starting with @top.  For details of the behavior, see
 * camel_store_get_folder_info_sync().
 *
 * When the operation is finished, @callback will be called.  You can
 * then call camel_store_get_folder_info_finish() to get the result of
 * the operation.
 *
 * Since: 2.92
 **/
void
camel_store_get_folder_info (CamelStore *store,
                             const gchar *top,
                             CamelStoreGetFolderInfoFlags flags,
                             gint io_priority,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
	CamelStoreClass *class;

	g_return_if_fail (CAMEL_IS_STORE (store));

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_if_fail (class->get_folder_info != NULL);

	class->get_folder_info (
		store, top, flags, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_store_get_folder_info_finish:
 * @store: a #CamelStore
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_store_get_folder_info().
 * The returned #CamelFolderInfo tree should be freed with
 * camel_store_free_folder_info().
 *
 * Returns: a #CamelFolderInfo tree, or %NULL on error
 *
 * Since: 2.92
 **/
CamelFolderInfo *
camel_store_get_folder_info_finish (CamelStore *store,
                                    GAsyncResult *result,
                                    GError **error)
{
	CamelStoreClass *class;

	g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->get_folder_info_finish != NULL, NULL);

	return class->get_folder_info_finish (store, result, error);
}

/**
 * camel_store_get_inbox_folder_sync:
 * @store: a #CamelStore
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets the folder in @store into which new mail is delivered.
 *
 * Returns: the inbox folder for @store, or %NULL on error or if no such
 * folder exists
 *
 * Since: 2.92
 **/
CamelFolder *
camel_store_get_inbox_folder_sync (CamelStore *store,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelStoreClass *class;
	CamelFolder *folder;

	g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->get_inbox_folder_sync != NULL, NULL);

	camel_store_lock (store, CAMEL_STORE_FOLDER_LOCK);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		camel_store_unlock (store, CAMEL_STORE_FOLDER_LOCK);
		return NULL;
	}

	folder = class->get_inbox_folder_sync (store, cancellable, error);
	CAMEL_CHECK_GERROR (
		store, get_inbox_folder_sync, folder != NULL, error);

	camel_store_unlock (store, CAMEL_STORE_FOLDER_LOCK);

	return folder;
}

/**
 * camel_store_get_inbox_folder:
 * @store: a #CamelStore
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously gets the folder in @store into which new mail is delivered.
 *
 * When the operation is finished, @callback will be called.  You can
 * then call camel_store_get_inbox_folder_finish() to get the result of
 * the operation.
 *
 * Since: 2.92
 **/
void
camel_store_get_inbox_folder (CamelStore *store,
                              gint io_priority,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	CamelStoreClass *class;

	g_return_if_fail (CAMEL_IS_STORE (store));

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_if_fail (class->get_inbox_folder != NULL);

	class->get_inbox_folder (
		store, io_priority, cancellable, callback, user_data);
}

/**
 * camel_store_get_inbox_folder_finish:
 * @store: a #CamelStore
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_store_get_inbox_folder().
 *
 * Returns: the inbox folder for @store, or %NULL on error or if no such
 * folder exists
 *
 * Since: 2.92
 **/
CamelFolder *
camel_store_get_inbox_folder_finish (CamelStore *store,
                                     GAsyncResult *result,
                                     GError **error)
{
	CamelStoreClass *class;

	g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->get_inbox_folder_finish != NULL, NULL);

	return class->get_inbox_folder_finish (store, result, error);
}

/**
 * camel_store_get_junk_folder_sync:
 * @store: a #CamelStore
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets the folder in @store into which junk is delivered.
 *
 * Returns: the junk folder for @store, or %NULL on error or if no such
 * folder exists
 *
 * Since: 2.92
 **/
CamelFolder *
camel_store_get_junk_folder_sync (CamelStore *store,
                                  GCancellable *cancellable,
                                  GError **error)
{
	g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);

	if ((store->flags & CAMEL_STORE_VJUNK) == 0) {
		CamelStoreClass *class;
		CamelFolder *folder;

		class = CAMEL_STORE_GET_CLASS (store);
		g_return_val_if_fail (class->get_junk_folder_sync != NULL, NULL);

		folder = class->get_junk_folder_sync (store, cancellable, error);
		CAMEL_CHECK_GERROR (
			store, get_junk_folder_sync, folder != NULL, error);

		return folder;
	}

	return camel_store_get_folder_sync (
		store, CAMEL_VJUNK_NAME, 0, cancellable, error);
}

/**
 * camel_store_get_junk_folder:
 * @store: a #CamelStore
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously gets the folder in @store into which junk is delivered.
 *
 * When the operation is finished, @callback will be called.  You can
 * then call camel_store_get_junk_folder_finish() to get the result of
 * the operation.
 *
 * Since: 2.92
 **/
void
camel_store_get_junk_folder (CamelStore *store,
                             gint io_priority,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
	CamelStoreClass *class;

	g_return_if_fail (CAMEL_IS_STORE (store));

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_if_fail (class->get_junk_folder != NULL);

	class->get_junk_folder (
		store, io_priority, cancellable, callback, user_data);
}

/**
 * camel_store_get_junk_folder_finish:
 * @store: a #CamelStore
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_store_get_junk_folder().
 *
 * Returns: the junk folder for @store, or %NULL on error or if no such
 * folder exists
 *
 * Since: 2.92
 **/
CamelFolder *
camel_store_get_junk_folder_finish (CamelStore *store,
                                    GAsyncResult *result,
                                    GError **error)
{
	CamelStoreClass *class;

	g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->get_junk_folder_finish != NULL, NULL);

	return class->get_junk_folder_finish (store, result, error);
}

/**
 * camel_store_get_trash_folder_sync:
 * @store: a #CamelStore
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets the folder in @store into which trash is delivered.
 *
 * Returns: the trash folder for @store, or %NULL on error or if no such
 * folder exists
 *
 * Since: 2.92
 **/
CamelFolder *
camel_store_get_trash_folder_sync (CamelStore *store,
                                   GCancellable *cancellable,
                                   GError **error)
{
	g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);

	if ((store->flags & CAMEL_STORE_VTRASH) == 0) {
		CamelStoreClass *class;
		CamelFolder *folder;

		class = CAMEL_STORE_GET_CLASS (store);
		g_return_val_if_fail (class->get_trash_folder_sync != NULL, NULL);

		folder = class->get_trash_folder_sync (
			store, cancellable, error);
		CAMEL_CHECK_GERROR (
			store, get_trash_folder_sync, folder != NULL, error);

		return folder;
	}

	return camel_store_get_folder_sync (
		store, CAMEL_VTRASH_NAME, 0, cancellable, error);
}

/**
 * camel_store_get_trash_folder:
 * @store: a #CamelStore
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously gets the folder in @store into which trash is delivered.
 *
 * When the operation is finished, @callback will be called.  You can
 * then call camel_store_get_trash_folder_finish() to get the result of
 * the operation.
 *
 * Since: 2.92
 **/
void
camel_store_get_trash_folder (CamelStore *store,
                              gint io_priority,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	CamelStoreClass *class;

	g_return_if_fail (CAMEL_IS_STORE (store));

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_if_fail (class->get_trash_folder != NULL);

	class->get_trash_folder (
		store, io_priority, cancellable, callback, user_data);
}

/**
 * camel_store_get_trash_folder_finish:
 * @store: a #CamelStore
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_store_get_trash_folder().
 *
 * Returns: the trash folder for @store, or %NULL on error or if no such
 * folder exists
 *
 * Since: 2.92
 **/
CamelFolder *
camel_store_get_trash_folder_finish (CamelStore *store,
                                     GAsyncResult *result,
                                     GError **error)
{
	CamelStoreClass *class;

	g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->get_trash_folder_finish != NULL, NULL);

	return class->get_trash_folder_finish (store, result, error);
}

/**
 * camel_store_create_folder_sync:
 * @store: a #CamelStore
 * @parent_name: name of the new folder's parent, or %NULL
 * @folder_name: name of the folder to create
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new folder as a child of an existing folder.
 * @parent_name can be %NULL to create a new top-level folder.
 * The returned #CamelFolderInfo struct should be freed with
 * camel_store_free_folder_info().
 *
 * Returns: info about the created folder, or %NULL on error
 *
 * Since: 2.92
 **/
CamelFolderInfo *
camel_store_create_folder_sync (CamelStore *store,
                                const gchar *parent_name,
                                const gchar *folder_name,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelStoreClass *class;
	CamelFolderInfo *fi;

	g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);
	g_return_val_if_fail (folder_name != NULL, NULL);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->create_folder_sync != NULL, NULL);

	if ((parent_name == NULL || parent_name[0] == 0)
	    && (((store->flags & CAMEL_STORE_VTRASH) && strcmp (folder_name, CAMEL_VTRASH_NAME) == 0)
		|| ((store->flags & CAMEL_STORE_VJUNK) && strcmp (folder_name, CAMEL_VJUNK_NAME) == 0))) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_INVALID,
			_("Cannot create folder: %s: folder exists"),
			folder_name);
		return NULL;
	}

	camel_store_lock (store, CAMEL_STORE_FOLDER_LOCK);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		camel_store_unlock (store, CAMEL_STORE_FOLDER_LOCK);
		return NULL;
	}

	fi = class->create_folder_sync (
		store, parent_name, folder_name, cancellable, error);
	CAMEL_CHECK_GERROR (store, create_folder_sync, fi != NULL, error);

	camel_store_unlock (store, CAMEL_STORE_FOLDER_LOCK);

	return fi;
}

/**
 * camel_store_create_folder:
 * @store: a #CamelStore
 * @parent_name: name of the new folder's parent, or %NULL
 * @folder_name: name of the folder to create
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously creates a new folder as a child of an existing folder.
 * @parent_name can be %NULL to create a new top-level folder.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_store_create_folder_finish() to get the result of the operation.
 *
 * Since: 2.92
 **/
void
camel_store_create_folder (CamelStore *store,
                           const gchar *parent_name,
                           const gchar *folder_name,
                           gint io_priority,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	CamelStoreClass *class;

	g_return_if_fail (CAMEL_IS_STORE (store));
	g_return_if_fail (folder_name != NULL);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_if_fail (class->create_folder != NULL);

	class->create_folder (
		store, parent_name, folder_name, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_store_create_folder_finish:
 * @store: a #CamelStore
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_store_create_folder().
 * The returned #CamelFolderInfo struct should be freed with
 * camel_store_free_folder_info().
 *
 * Returns: info about the created folder, or %NULL on error
 *
 * Since: 2.92
 **/
CamelFolderInfo *
camel_store_create_folder_finish (CamelStore *store,
                                  GAsyncResult *result,
                                  GError **error)
{
	CamelStoreClass *class;

	g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->create_folder_finish != NULL, NULL);

	return class->create_folder_finish (store, result, error);
}

/**
 * camel_store_delete_folder_sync:
 * @store: a #CamelStore
 * @folder_name: name of the folder to delete
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Deletes the folder described by @folder_name.  The folder must be empty.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 2.92
 **/
gboolean
camel_store_delete_folder_sync (CamelStore *store,
                                const gchar *folder_name,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelStoreClass *class;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->delete_folder_sync != NULL, FALSE);

	/* TODO: should probably be a parameter/bit on the storeinfo */
	if (((store->flags & CAMEL_STORE_VTRASH) && strcmp (folder_name, CAMEL_VTRASH_NAME) == 0)
	    || ((store->flags & CAMEL_STORE_VJUNK) && strcmp (folder_name, CAMEL_VJUNK_NAME) == 0)) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Cannot delete folder: %s: Invalid operation"),
			folder_name);
		return FALSE;
	}

	camel_store_lock (store, CAMEL_STORE_FOLDER_LOCK);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		camel_store_unlock (store, CAMEL_STORE_FOLDER_LOCK);
		return FALSE;
	}

	success = class->delete_folder_sync (
		store, folder_name, cancellable, &local_error);
	CAMEL_CHECK_GERROR (store, delete_folder_sync, success, &local_error);

	/* ignore 'no such table' errors */
	if (local_error != NULL &&
	    g_ascii_strncasecmp (local_error->message, "no such table", 13) == 0)
		g_clear_error (&local_error);

	if (local_error == NULL)
		cs_delete_cached_folder (store, folder_name);
	else
		g_propagate_error (error, local_error);

	camel_store_unlock (store, CAMEL_STORE_FOLDER_LOCK);

	return success;
}

/**
 * camel_store_delete_folder:
 * @store: a #CamelStore
 * @folder_name: name of the folder to delete
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously deletes the folder described by @folder_name.  The
 * folder must be empty.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_store_delete_folder_finish() to get the result of the operation.
 *
 * Since: 2.92
 **/
void
camel_store_delete_folder (CamelStore *store,
                           const gchar *folder_name,
                           gint io_priority,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	CamelStoreClass *class;

	g_return_if_fail (CAMEL_IS_STORE (store));
	g_return_if_fail (folder_name != NULL);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_if_fail (class->delete_folder != NULL);

	class->delete_folder (
		store, folder_name, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_store_delete_folder_finish:
 * @store: a #CamelStore
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_store_delete_folder().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 2.92
 **/
gboolean
camel_store_delete_folder_finish (CamelStore *store,
                                  GAsyncResult *result,
                                  GError **error)
{
	CamelStoreClass *class;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->delete_folder_finish != NULL, FALSE);

	return class->delete_folder_finish (store, result, error);
}

/**
 * camel_store_rename_folder_sync:
 * @store: a #CamelStore
 * @old_name: the current name of the folder
 * @new_name: the new name of the folder
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Renames the folder described by @old_name to @new_name.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 2.92
 **/
gboolean
camel_store_rename_folder_sync (CamelStore *store,
                                const gchar *old_namein,
                                const gchar *new_name,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelStoreClass *class;
	CamelFolder *folder;
	gint i, oldlen, namelen;
	GPtrArray *folders = NULL;
	gchar *old_name;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);
	g_return_val_if_fail (old_namein != NULL, FALSE);
	g_return_val_if_fail (new_name != NULL, FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->rename_folder_sync != NULL, FALSE);

	if (strcmp (old_namein, new_name) == 0)
		return TRUE;

	if (((store->flags & CAMEL_STORE_VTRASH) && strcmp (old_namein, CAMEL_VTRASH_NAME) == 0)
	    || ((store->flags & CAMEL_STORE_VJUNK) && strcmp (old_namein, CAMEL_VJUNK_NAME) == 0)) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Cannot rename folder: %s: Invalid operation"),
			old_namein);
		return FALSE;
	}

	/* need to save this, since old_namein might be folder->full_name, which could go away */
	old_name = g_strdup (old_namein);
	oldlen = strlen (old_name);

	camel_store_lock (store, CAMEL_STORE_FOLDER_LOCK);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		camel_store_unlock (store, CAMEL_STORE_FOLDER_LOCK);
		return FALSE;
	}

	/* If the folder is open (or any subfolders of the open folder)
	   We need to rename them atomically with renaming the actual folder path */
	if (store->folders) {
		folders = camel_object_bag_list (store->folders);
		for (i=0;i<folders->len;i++) {
			const gchar *full_name;

			folder = folders->pdata[i];
			full_name = camel_folder_get_full_name (folder);

			namelen = strlen (full_name);
			if ((namelen == oldlen &&
			     strcmp (full_name, old_name) == 0)
			    || ((namelen > oldlen)
				&& strncmp (full_name, old_name, oldlen) == 0
				&& full_name[oldlen] == '/')) {
				camel_folder_lock (folder, CAMEL_FOLDER_REC_LOCK);
			} else {
				g_ptr_array_remove_index_fast (folders, i);
				i--;
				g_object_unref (folder);
			}
		}
	}

	/* Now try the real rename (will emit renamed signal) */
	success = class->rename_folder_sync (
		store, old_name, new_name, cancellable, error);
	CAMEL_CHECK_GERROR (store, rename_folder_sync, success, error);

	/* If it worked, update all open folders/unlock them */
	if (folders) {
		if (success) {
			CamelStoreGetFolderInfoFlags flags;
			CamelFolderInfo *folder_info;

			flags = CAMEL_STORE_FOLDER_INFO_RECURSIVE;

			for (i=0;i<folders->len;i++) {
				const gchar *full_name;
				gchar *new;

				folder = folders->pdata[i];
				full_name = camel_folder_get_full_name (folder);

				new = g_strdup_printf("%s%s", new_name, full_name + strlen(old_name));
				camel_object_bag_rekey (store->folders, folder, new);
				camel_folder_rename (folder, new);
				g_free (new);

				camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);
				g_object_unref (folder);
			}

			/* Emit renamed signal */
			if (store->flags & CAMEL_STORE_SUBSCRIPTIONS)
				flags |= CAMEL_STORE_FOLDER_INFO_SUBSCRIBED;

			folder_info = class->get_folder_info_sync (
				store, new_name, flags, cancellable, error);
			CAMEL_CHECK_GERROR (store, get_folder_info, folder_info != NULL, error);

			if (folder_info != NULL) {
				camel_store_folder_renamed (store, old_name, folder_info);
				class->free_folder_info (store, folder_info);
			}
		} else {
			/* Failed, just unlock our folders for re-use */
			for (i=0;i<folders->len;i++) {
				folder = folders->pdata[i];
				camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);
				g_object_unref (folder);
			}
		}
	}

	camel_store_unlock (store, CAMEL_STORE_FOLDER_LOCK);

	g_ptr_array_free (folders, TRUE);
	g_free (old_name);

	return success;
}

/**
 * camel_store_rename_folder:
 * @store: a #CamelStore
 * @old_name: the current name of the folder
 * @new_name: the new name of the folder
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously renames the folder described by @old_name to @new_name.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_store_rename_folder_finish() to get the result of the operation.
 *
 * Since: 2.92
 **/
void
camel_store_rename_folder (CamelStore *store,
                           const gchar *old_name,
                           const gchar *new_name,
                           gint io_priority,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	CamelStoreClass *class;

	g_return_if_fail (CAMEL_IS_STORE (store));
	g_return_if_fail (old_name != NULL);
	g_return_if_fail (new_name != NULL);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_if_fail (class->rename_folder != NULL);

	class->rename_folder (
		store, old_name, new_name, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_store_rename_folder_finish:
 * @store: a #CamelStore
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_store_rename_folder().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 2.92
 **/
gboolean
camel_store_rename_folder_finish (CamelStore *store,
                                  GAsyncResult *result,
                                  GError **error)
{
	CamelStoreClass *class;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->rename_folder_finish != NULL, FALSE);

	return class->rename_folder_finish (store, result, error);
}

/**
 * camel_store_subscribe_folder_sync:
 * @store: a #CamelStore
 * @folder_name: full path of the folder
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Subscribes to the folder described by @folder_name.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 2.92
 **/
gboolean
camel_store_subscribe_folder_sync (CamelStore *store,
                                   const gchar *folder_name,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelStoreClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);
	g_return_val_if_fail (store->flags & CAMEL_STORE_SUBSCRIPTIONS, FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->subscribe_folder_sync != NULL, FALSE);

	camel_store_lock (store, CAMEL_STORE_FOLDER_LOCK);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		camel_store_unlock (store, CAMEL_STORE_FOLDER_LOCK);
		return FALSE;
	}

	success = class->subscribe_folder_sync (
		store, folder_name, cancellable, error);
	CAMEL_CHECK_GERROR (store, subscribe_folder_sync, success, error);

	camel_store_unlock (store, CAMEL_STORE_FOLDER_LOCK);

	return success;
}

/**
 * camel_store_subscribe_folder:
 * @store: a #CamelStore
 * @folder_name: full path of the folder
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously subscribes to the folder described by @folder_name.
 *
 * When the operation is finished, @callback will be called.  You can
 * then call camel_store_subscribe_folder_finish() to get the result of
 * the operation.
 *
 * Since: 2.92
 **/
void
camel_store_subscribe_folder (CamelStore *store,
                              const gchar *folder_name,
                              gint io_priority,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	CamelStoreClass *class;

	g_return_if_fail (CAMEL_IS_STORE (store));
	g_return_if_fail (folder_name != NULL);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_if_fail (class->subscribe_folder != NULL);

	class->subscribe_folder (
		store, folder_name, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_store_subscribe_folder_finish:
 * @store: a #CamelStore
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_store_subscribe_folder().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 2.92
 **/
gboolean
camel_store_subscribe_folder_finish (CamelStore *store,
                                     GAsyncResult *result,
                                     GError **error)
{
	CamelStoreClass *class;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->subscribe_folder_finish != NULL, FALSE);

	return class->subscribe_folder_finish (store, result, error);
}

/**
 * camel_store_unsubscribe_folder_sync:
 * @store: a #CamelStore
 * @folder_name: full path of the folder
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Unsubscribes from the folder described by @folder_name.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 2.92
 **/
gboolean
camel_store_unsubscribe_folder_sync (CamelStore *store,
                                     const gchar *folder_name,
                                     GCancellable *cancellable,
                                     GError **error)
{
	CamelStoreClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);
	g_return_val_if_fail (store->flags & CAMEL_STORE_SUBSCRIPTIONS, FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->unsubscribe_folder_sync != NULL, FALSE);

	camel_store_lock (store, CAMEL_STORE_FOLDER_LOCK);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		camel_store_unlock (store, CAMEL_STORE_FOLDER_LOCK);
		return FALSE;
	}

	success = class->unsubscribe_folder_sync (
		store, folder_name, cancellable, error);
	CAMEL_CHECK_GERROR (store, unsubscribe_folder_sync, success, error);

	if (success)
		cs_delete_cached_folder (store, folder_name);

	camel_store_unlock (store, CAMEL_STORE_FOLDER_LOCK);

	return success;
}

/**
 * camel_store_unsubscribe_folder:
 * @store: a #CamelStore
 * @folder_name: full path of the folder
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously unsubscribes from the folder described by @folder_name.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_store_unsubscribe_folder_finish() to get the result of the
 * operation.
 *
 * Since: 2.92
 **/
void
camel_store_unsubscribe_folder (CamelStore *store,
                                const gchar *folder_name,
                                gint io_priority,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	CamelStoreClass *class;

	g_return_if_fail (CAMEL_IS_STORE (store));
	g_return_if_fail (folder_name != NULL);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_if_fail (class->unsubscribe_folder != NULL);

	class->unsubscribe_folder (
		store, folder_name, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_store_unsubscribe_folder_finish:
 * @store: a #CamelStore
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_store_unsubscribe_folder().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 2.92
 **/
gboolean
camel_store_unsubscribe_folder_finish (CamelStore *store,
                                       GAsyncResult *result,
                                       GError **error)
{
	CamelStoreClass *class;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->unsubscribe_folder_finish != NULL, FALSE);

	return class->unsubscribe_folder_finish (store, result, error);
}

/**
 * camel_store_synchronize_sync:
 * @store: a #CamelStore
 * @expunge: whether to expunge after synchronizing
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Synchronizes any changes that have been made to @store and its folders
 * with the real store.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 2.92
 **/
gboolean
camel_store_synchronize_sync (CamelStore *store,
                              gboolean expunge,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelStoreClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->synchronize_sync != NULL, FALSE);

	success = class->synchronize_sync (store, expunge, cancellable, error);
	CAMEL_CHECK_GERROR (store, synchronize_sync, success, error);

	return success;
}

/**
 * camel_store_synchronize:
 * @store: a #CamelStore
 * @expunge: whether to expunge after synchronizing
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Synchronizes any changes that have been made to @store and its folders
 * with the real store asynchronously.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_store_synchronize_finish() to get the result of the operation.
 *
 * Since: 2.92
 **/
void
camel_store_synchronize (CamelStore *store,
                         gboolean expunge,
                         gint io_priority,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
	CamelStoreClass *class;

	g_return_if_fail (CAMEL_IS_STORE (store));

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_if_fail (class->synchronize != NULL);

	class->synchronize (
		store, expunge, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_store_synchronize_finish:
 * @store: a #CamelStore
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_store_synchronize().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 2.92
 **/
gboolean
camel_store_synchronize_finish (CamelStore *store,
                                GAsyncResult *result,
                                GError **error)
{
	CamelStoreClass *class;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->synchronize_finish != NULL, FALSE);

	return class->synchronize_finish (store, result, error);
}

/**
 * camel_store_noop_sync:
 * @store: a #CamelStore
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Pings @store so its connection doesn't time out.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 2.92
 **/
gboolean
camel_store_noop_sync (CamelStore *store,
                       GCancellable *cancellable,
                       GError **error)
{
	CamelStoreClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->noop_sync != NULL, FALSE);

	success = class->noop_sync (store, cancellable, error);
	CAMEL_CHECK_GERROR (store, noop_sync, success, error);

	return success;
}

/**
 * camel_store_noop:
 * @store: a #CamelStore
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Pings @store asynchronously so its connection doesn't time out.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_store_noop_finish() to get the result of the operation.
 *
 * Since: 2.92
 **/
void
camel_store_noop (CamelStore *store,
                  gint io_priority,
                  GCancellable *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
	CamelStoreClass *class;

	g_return_if_fail (CAMEL_IS_STORE (store));

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_if_fail (class->noop != NULL);

	class->noop (store, io_priority, cancellable, callback, user_data);
}

/**
 * camel_store_noop_finish:
 * @store: a #CamelStore
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_store_noop().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 2.92
 **/
gboolean
camel_store_noop_finish (CamelStore *store,
                         GAsyncResult *result,
                         GError **error)
{
	CamelStoreClass *class;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->noop_finish != NULL, FALSE);

	return class->noop_finish (store, result, error);
}
