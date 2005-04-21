/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2002-2004 Novell, Inc.
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
 */

/* ExchangeStorage: EStorage subclass that talks to an ExchangeAccount */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-storage.h"
#include "exchange-account.h"
#include "exchange-hierarchy.h"
#include "e-folder-exchange.h"

//#include <e-util/e-dialog-utils.h>

#include <stdlib.h>
#include <string.h>

struct _ExchangeStoragePrivate {
	ExchangeAccount *account;
	guint idle_id;
};

#define PARENT_TYPE E_TYPE_STORAGE
static EStorageClass *parent_class = NULL;

static void finalize (GObject *);

static void create_folder (EStorage *storage,
			   const char *path,
			   const char *type,
			   EStorageResultCallback callback,
			   gpointer user_data);
static void remove_folder (EStorage *storage,
			   const char *path,
			   EStorageResultCallback callback,
			   gpointer user_data);
static void xfer_folder   (EStorage *storage,
			   const char *source_path,
			   const char *destination_path,
			   const gboolean remove_source,
			   EStorageResultCallback callback,
			   gpointer user_data);
static void open_folder   (EStorage *storage,
			   const char *path,
			   EStorageDiscoveryCallback callback,
			   gpointer user_data);
static gboolean will_accept_folder (EStorage *storage,
				    EFolder *new_parent,
				    EFolder *source);

static void discover_shared_folder        (EStorage *storage,
					   const char *owner,
					   const char *folder_name,
					   EStorageDiscoveryCallback callback,
					   gpointer user_data);
static void cancel_discover_shared_folder (EStorage *storage,
					   const char *owner,
					   const char *folder_name);
static void remove_shared_folder          (EStorage *storage,
					   const char *path,
					   EStorageResultCallback callback,
					   gpointer user_data);

static void
class_init (GObjectClass *object_class)
{
	EStorageClass *e_storage_class = E_STORAGE_CLASS (object_class);

	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->finalize = finalize;

	e_storage_class->async_create_folder = create_folder;
	e_storage_class->async_remove_folder = remove_folder;
	e_storage_class->async_xfer_folder = xfer_folder;
	e_storage_class->async_open_folder = open_folder;
	e_storage_class->will_accept_folder = will_accept_folder;
	e_storage_class->async_discover_shared_folder = discover_shared_folder;
	e_storage_class->cancel_discover_shared_folder = cancel_discover_shared_folder;
	e_storage_class->async_remove_shared_folder = remove_shared_folder;
}

static void
init (GObject *object)
{
	ExchangeStorage *exstorage = EXCHANGE_STORAGE (object);

	exstorage->priv = g_new0 (ExchangeStoragePrivate, 1);
}

static void
finalize (GObject *object)
{
	ExchangeStorage *exstorage = EXCHANGE_STORAGE (object);

	if (exstorage->priv->idle_id)
		g_source_remove (exstorage->priv->idle_id);

	g_free (exstorage->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}


E2K_MAKE_TYPE (exchange_storage, ExchangeStorage, class_init, init, PARENT_TYPE)

static void
account_new_folder (ExchangeAccount *account, EFolder *folder,
		    EStorage *storage)
{
	const char *path = e_folder_exchange_get_path (folder);

	e_storage_new_folder (storage, path, folder);
	if (e_folder_exchange_get_has_subfolders (folder)) {
		e_storage_declare_has_subfolders (storage, path,
						  _("Searching..."));
	}
}

static void
account_removed_folder (ExchangeAccount *account, EFolder *folder,
			EStorage *storage)
{
	const char *path = e_folder_exchange_get_path (folder);

	e_storage_removed_folder (storage, path);
}

static EStorageResult
account_to_storage_result (ExchangeAccountFolderResult result)
{
	switch (result) {
	case EXCHANGE_ACCOUNT_FOLDER_OK:
		return E_STORAGE_OK;
	case EXCHANGE_ACCOUNT_FOLDER_ALREADY_EXISTS:
		return E_STORAGE_EXISTS;
	case EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST:
		return E_STORAGE_NOTFOUND;
	case EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED:
		return E_STORAGE_PERMISSIONDENIED;
	case EXCHANGE_ACCOUNT_FOLDER_OFFLINE:
		return E_STORAGE_NOTONLINE;
	case EXCHANGE_ACCOUNT_FOLDER_UNSUPPORTED_OPERATION:
		return E_STORAGE_UNSUPPORTEDOPERATION;
	default:
		return E_STORAGE_GENERICERROR;
	}
}

static void
create_folder (EStorage *storage,
	       const char *path, const char *type,
	       EStorageResultCallback callback,
	       gpointer user_data)
{
	ExchangeStorage *exstorage = EXCHANGE_STORAGE (storage);
	ExchangeAccount *account = exstorage->priv->account;
	ExchangeAccountFolderResult result;

	result = exchange_account_create_folder (account, path, type);
	callback (storage, account_to_storage_result (result), user_data);
}

static void
remove_folder (EStorage *storage, const char *path, 
	       EStorageResultCallback callback,
	       gpointer user_data)
{
	ExchangeStorage *exstorage = EXCHANGE_STORAGE (storage);
	ExchangeAccount *account = exstorage->priv->account;
	ExchangeAccountFolderResult result;

	result = exchange_account_remove_folder (account, path);
	callback (storage, account_to_storage_result (result), user_data);
}

static void
xfer_folder (EStorage *storage, 
	     const char *source_path, const char *dest_path,
	     const gboolean remove_source,
	     EStorageResultCallback callback,
	     gpointer user_data)
{
	ExchangeStorage *exstorage = EXCHANGE_STORAGE (storage);
	ExchangeAccount *account = exstorage->priv->account;
	ExchangeAccountFolderResult result;

	result = exchange_account_xfer_folder (account,
					       source_path, dest_path,
					       remove_source);
	callback (storage, account_to_storage_result (result), user_data);
}

struct open_folder_data {
	EStorage *storage;
	char *path;
	EStorageDiscoveryCallback callback;
	gpointer user_data;
};

static gboolean
idle_open_folder (gpointer user_data)
{
	struct open_folder_data *ofd = user_data;
	ExchangeStorage *exstorage = EXCHANGE_STORAGE (ofd->storage);
	ExchangeAccount *account = exstorage->priv->account;
	ExchangeAccountFolderResult result;
	E2kContext *ctx;

	if (!strcmp (ofd->path, "/")) {
		ctx = exchange_account_connect (account);
		result = ctx ?
			E_STORAGE_OK :
			E_STORAGE_NOTONLINE;
	} else
		result = exchange_account_open_folder (account, ofd->path);
	ofd->callback (ofd->storage, account_to_storage_result (result),
		       ofd->path, ofd->user_data);

	g_object_unref (ofd->storage);
	g_free (ofd->path);
	g_free (ofd);

	return FALSE;
}

static void
open_folder (EStorage *storage, const char *path,
	     EStorageDiscoveryCallback callback, gpointer user_data)
{
	struct open_folder_data *ofd;

	/* This needs to actually be done asynchronously, or ETree
	 * will mess up and duplicate nodes in the tree.
	 */

	ofd = g_new0 (struct open_folder_data, 1);
	ofd->storage = g_object_ref (storage);
	ofd->path = g_strdup (path);
	ofd->callback = callback;
	ofd->user_data = user_data;
	
	g_idle_add (idle_open_folder, ofd);
}

static gboolean
will_accept_folder (EStorage *storage,
		    EFolder *new_parent, EFolder *source)
{
	if (!E_IS_FOLDER_EXCHANGE (new_parent) ||
	    !E_IS_FOLDER_EXCHANGE (source))
		return FALSE;

	if (e_folder_exchange_get_hierarchy (new_parent) !=
	    e_folder_exchange_get_hierarchy (source))
		return FALSE;

	return E_STORAGE_CLASS (parent_class)->will_accept_folder (storage, new_parent, source);
}

static void
discover_shared_folder (EStorage *storage,
			const char *owner, const char *folder_name,
			EStorageDiscoveryCallback callback,
			gpointer user_data)
{
	ExchangeStorage *exstorage = EXCHANGE_STORAGE (storage);
	ExchangeAccount *account = exstorage->priv->account;
	ExchangeAccountFolderResult result;
	EFolder *folder = NULL;

	result = exchange_account_discover_shared_folder (account,
							  owner, folder_name,
							  &folder);
	callback (storage, account_to_storage_result (result),
		  folder ? e_folder_exchange_get_path (folder) : NULL,
		  user_data);
	if (folder)
		g_object_unref (folder);
}

static void
cancel_discover_shared_folder (EStorage *storage, const char *owner,
			       const char *folder_name)
{
	ExchangeStorage *exstorage = EXCHANGE_STORAGE (storage);
	ExchangeAccount *account = exstorage->priv->account;

	exchange_account_cancel_discover_shared_folder (account,
							owner, folder_name);
}

static void
remove_shared_folder (EStorage *storage, const char *path,
		      EStorageResultCallback callback, gpointer user_data)
{
	ExchangeStorage *exstorage = EXCHANGE_STORAGE (storage);
	ExchangeAccount *account = exstorage->priv->account;
	ExchangeAccountFolderResult result;

	result = exchange_account_remove_shared_folder (account, path);
	callback (storage, account_to_storage_result (result), user_data);
}

static gboolean
idle_fill_storage (gpointer user_data)
{
	ExchangeStorage *exstorage = user_data;
	EStorage *storage = user_data;
	ExchangeAccount *account = exstorage->priv->account;
	GPtrArray *folders;
	int i;

	exstorage->priv->idle_id = 0;

	if (!exchange_account_get_context (account)) {
		e_storage_declare_has_subfolders (storage, "/",
						  _("Connecting..."));
	} else {
		folders = exchange_account_get_folders (account);
		if (folders) {
			for (i = 0; i < folders->len; i++) {
				account_new_folder (account, folders->pdata[i],
						    storage);
			}
			g_ptr_array_free (folders, TRUE);
		}
	}

	g_signal_connect (account, "new_folder",
			  G_CALLBACK (account_new_folder), exstorage);
	g_signal_connect (account, "removed_folder",
			  G_CALLBACK (account_removed_folder), exstorage);

	g_object_unref (storage);

	return FALSE;
}

/**
 * exchange_storage_new:
 * @account: the account the storage will represent
 *
 * This creates a storage for @account.
 **/
EStorage *
exchange_storage_new (ExchangeAccount *account)
{
	ExchangeStorage *exstorage;
	EStorage *storage;
	EFolder *root_folder;

	exstorage = g_object_new (EXCHANGE_TYPE_STORAGE, NULL);
	storage = E_STORAGE (exstorage);

	root_folder = e_folder_new (account->account_name, "noselect", "");
	e_storage_construct (storage, account->account_name, root_folder);

	exstorage->priv->account = account;

	g_object_ref (exstorage);
	exstorage->priv->idle_id = g_idle_add (idle_fill_storage, exstorage);

	return storage;
}
