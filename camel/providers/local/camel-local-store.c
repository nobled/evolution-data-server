/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 2000 Ximian, Inc.
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

#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include <glib.h>

#include "camel-private.h"

#include "camel-local-store.h"
#include "camel-exception.h"
#include "camel-url.h"

#include "camel-local-folder.h"
#include <camel/camel-text-index.h>

#define d(x)

/* Returns the class for a CamelLocalStore */
#define CLOCALS_CLASS(so) CAMEL_LOCAL_STORE_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CF_CLASS(so) CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))

static void construct (CamelService *service, CamelSession *session, CamelProvider *provider, CamelURL *url, CamelException *ex);
static CamelFolder *get_folder(CamelStore * store, const char *folder_name, guint32 flags, CamelException * ex);
static char *get_name(CamelService *service, gboolean brief);
static CamelFolder *get_inbox (CamelStore *store, CamelException *ex);
static void rename_folder(CamelStore *store, const char *old_name, const char *new_name, CamelException *ex);
static CamelFolderInfo *get_folder_info (CamelStore *store, const char *top, guint32 flags, CamelException *ex);
static void delete_folder(CamelStore *store, const char *folder_name, CamelException *ex);
static void rename_folder(CamelStore *store, const char *old, const char *new, CamelException *ex);
static CamelFolderInfo *create_folder(CamelStore *store, const char *parent_name, const char *folder_name, CamelException *ex);

static CamelStoreClass *parent_class = NULL;

static void
camel_local_store_class_init (CamelLocalStoreClass *camel_local_store_class)
{
	CamelStoreClass *camel_store_class = CAMEL_STORE_CLASS (camel_local_store_class);
	CamelServiceClass *camel_service_class = CAMEL_SERVICE_CLASS (camel_local_store_class);
	
	parent_class = CAMEL_STORE_CLASS (camel_type_get_global_classfuncs (camel_store_get_type ()));

	/* virtual method overload */
	camel_service_class->construct = construct;
	camel_service_class->get_name = get_name;
	camel_store_class->get_folder = get_folder;
	camel_store_class->get_inbox = get_inbox;
	camel_store_class->get_folder_info = get_folder_info;
	camel_store_class->free_folder_info = camel_store_free_folder_info_full;

	camel_store_class->create_folder = create_folder;
	camel_store_class->delete_folder = delete_folder;
	camel_store_class->rename_folder = rename_folder;
}

static void
camel_local_store_finalize (CamelLocalStore *local_store)
{
	if (local_store->toplevel_dir)
		g_free (local_store->toplevel_dir);
}

CamelType
camel_local_store_get_type (void)
{
	static CamelType camel_local_store_type = CAMEL_INVALID_TYPE;
	
	if (camel_local_store_type == CAMEL_INVALID_TYPE)	{
		camel_local_store_type = camel_type_register (CAMEL_STORE_TYPE, "CamelLocalStore",
							     sizeof (CamelLocalStore),
							     sizeof (CamelLocalStoreClass),
							     (CamelObjectClassInitFunc) camel_local_store_class_init,
							     NULL,
							     NULL,
							     (CamelObjectFinalizeFunc) camel_local_store_finalize);
	}
	
	return camel_local_store_type;
}

static void
construct (CamelService *service, CamelSession *session, CamelProvider *provider, CamelURL *url, CamelException *ex)
{
	CamelLocalStore *local_store = CAMEL_LOCAL_STORE (service);
	int len;

	CAMEL_SERVICE_CLASS (parent_class)->construct (service, session, provider, url, ex);
	if (camel_exception_is_set (ex))
		return;

	len = strlen (service->url->path);
	if (service->url->path[len - 1] != '/')
		local_store->toplevel_dir = g_strdup_printf ("%s/", service->url->path);
	else
		local_store->toplevel_dir = g_strdup (service->url->path);
}

const char *
camel_local_store_get_toplevel_dir (CamelLocalStore *store)
{
	return store->toplevel_dir;
}

static CamelFolder *
get_folder(CamelStore * store, const char *folder_name, guint32 flags, CamelException * ex)
{
	struct stat st;
	char *path = ((CamelLocalStore *)store)->toplevel_dir;
	char *sub, *slash;

	if (path[0] != '/') {
		camel_exception_setv(ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
				     _("Store root %s is not an absolute path"), path);
		return NULL;
	}

	if (stat(path, &st) == 0) {
		if (!S_ISDIR(st.st_mode)) {
			camel_exception_setv(ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
					     _("Store root %s is not a regular directory"), path);
		}
		return NULL;
	}

	if (errno != ENOENT
	    || (flags & CAMEL_STORE_FOLDER_CREATE) == 0) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
				     _("Cannot get folder: %s: %s"), path, strerror(errno));
		return NULL;
	}

	/* need to create the dir heirarchy */
	sub = alloca(strlen(path)+1);
	strcpy(sub, path);
	slash = sub;
	do {
		slash = strchr(slash+1, '/');
		if (slash)
			*slash = 0;
		if (stat(sub, &st) == -1) {
			if (errno != ENOENT
			    || mkdir(sub, 0700) == -1) {
				camel_exception_setv(ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
						     _("Cannot get folder: %s: %s"), path, strerror(errno));
				return NULL;
			}
		}
		if (slash)
			*slash = '/';
	} while (slash);

	return NULL;
}

static CamelFolder *
get_inbox(CamelStore *store, CamelException *ex)
{
	camel_exception_set(ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
			    _("Local stores do not have an inbox"));
	return NULL;
}

static char *
get_name (CamelService *service, gboolean brief)
{
	char *dir = ((CamelLocalStore*)service)->toplevel_dir;

	if (brief)
		return g_strdup (dir);
	else
		return g_strdup_printf (_("Local mail file %s"), dir);
}

static CamelFolderInfo *
get_folder_info (CamelStore *store, const char *top,
		 guint32 flags, CamelException *ex)
{
	/* FIXME: This is broken, but it corresponds to what was
	 * there before.
	 */

	d(printf("-- LOCAL STRE -- get folder info: %s\n", top));

	return NULL;
}

static CamelFolderInfo *
create_folder(CamelStore *store, const char *parent_name, const char *folder_name, CamelException *ex)
{
	char *path = ((CamelLocalStore *)store)->toplevel_dir;
	char *name;
	CamelFolder *folder;
	CamelFolderInfo *info = NULL;
	struct stat st;

	/* This is a pretty hacky version of create folder, but should basically work */

	/* FIXME: The strings here are funny because of string freeze */

	if (path[0] != '/') {
		camel_exception_setv(ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
				     _("Store root %s is not an absolute path"), path);
		return NULL;
	}

	name = g_strdup_printf("%s/%s/%s", path, parent_name, folder_name);

	if (stat(name, &st) == 0 || errno != ENOENT) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
				     _("Cannot get folder: %s: %s"), name, strerror(errno));
		g_free(name);
		return NULL;
	}

	g_free(name);

	name = g_strdup_printf("%s/%s", parent_name, folder_name);

	folder = ((CamelStoreClass *)((CamelObject *)store)->classfuncs)->get_folder(store, name, CAMEL_STORE_FOLDER_CREATE, ex);
	if (folder) {
		camel_object_unref((CamelObject *)folder);
		info = ((CamelStoreClass *)((CamelObject *)store)->classfuncs)->get_folder_info(store, name, 0, ex);

		/* get_folder(CREATE) will emit a folder_created event for us */
		/*if (info)
		  camel_object_trigger_event((CamelObject *)store, "folder_created", info);*/
	}

	g_free(name);

	return info;
}

static int xrename(const char *oldp, const char *newp, const char *prefix, const char *suffix, int missingok, CamelException *ex)
{
	struct stat st;
	char *old = g_strconcat(prefix, oldp, suffix, 0);
	char *new = g_strconcat(prefix, newp, suffix, 0);
	int ret = -1;
	int err = 0;

	d(printf("renaming %s%s to %s%s\n", oldp, suffix, newp, suffix));

	if (stat(old, &st) == -1) {
		if (missingok && errno == ENOENT) {
			ret = 0;
		} else {
			err = errno;
			ret = -1;
		}
	} else if (S_ISDIR(st.st_mode)) { /* use rename for dirs */
		if (rename(old, new) == 0
		    || stat(new, &st) == 0) {
			ret = 0;
		} else {
			err = errno;
			ret = -1;
		}
	} else if (link(old, new) == 0 /* and link for files */
		   || (stat(new, &st) == 0 && st.st_nlink == 2)) {
		if (unlink(old) == 0) {
			ret = 0;
		} else {
			err = errno;
			unlink(new);
			ret = -1;
		}
	} else {
		err = errno;
		ret = -1;
	}

	if (ret == -1) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Could not rename folder %s to %s: %s"),
				     old, new, strerror(err));
	}

	g_free(old);
	g_free(new);
	return ret;
}

/* default implementation, rename all */
static void
rename_folder(CamelStore *store, const char *old, const char *new, CamelException *ex)
{
	char *path = CAMEL_LOCAL_STORE (store)->toplevel_dir;
	CamelLocalFolder *folder;
	char *newibex = g_strdup_printf("%s%s.ibex", path, new);
	char *oldibex = g_strdup_printf("%s%s.ibex", path, old);

	/* try to rollback failures, has obvious races */

	d(printf("local rename folder '%s' '%s'\n", old, new));

	CAMEL_STORE_LOCK(store, cache_lock);
	folder = g_hash_table_lookup(store->folders, old);
	if (folder && folder->index) {
		if (folder->index && camel_index_rename(folder->index, newibex) == -1)
			goto ibex_failed;
	} else {
		/* TODO: camel_text_index_rename should find out if we have an active index itself? */
		if (camel_text_index_rename(oldibex, newibex) == -1)
			goto ibex_failed;
	}

	if (xrename(old, new, path, ".ev-summary", TRUE, ex))
		goto summary_failed;

	if (xrename(old, new, path, "", FALSE, ex))
		goto base_failed;

	CAMEL_STORE_UNLOCK(store, cache_lock);

	g_free(newibex);
	g_free(oldibex);

	return;

base_failed:
	xrename(new, old, path, ".ev-summary", TRUE, ex);

summary_failed:
	if (folder) {
		if (folder->index)
			camel_index_rename(folder->index, oldibex);
	} else
		camel_text_index_rename(newibex, oldibex);
ibex_failed:
	CAMEL_STORE_UNLOCK(store, cache_lock);
	g_free(newibex);
	g_free(oldibex);
}

/* default implementation, only delete metadata */
static void
delete_folder(CamelStore *store, const char *folder_name, CamelException *ex)
{
	CamelFolderInfo *fi;
	char *name;
	char *str;

	/* remove metadata only */
	name = g_strdup_printf("%s%s", CAMEL_LOCAL_STORE(store)->toplevel_dir, folder_name);
	str = g_strdup_printf("%s.ev-summary", name);
	if (unlink(str) == -1 && errno != ENOENT) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Could not delete folder summary file `%s': %s"),
				     str, strerror(errno));
		g_free(str);
		g_free (name);
		return;
	}
	g_free(str);
	str = g_strdup_printf("%s.ibex", name);
	if (camel_text_index_remove(str) == -1 && errno != ENOENT) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Could not delete folder index file `%s': %s"),
				     str, strerror(errno));
		g_free(str);
		g_free (name);
		return;
	}
	g_free(str);
	g_free(name);
	
	fi = g_new0 (CamelFolderInfo, 1);
	fi->full_name = g_strdup (folder_name);
	fi->name = g_strdup (g_basename (folder_name));
	fi->url = g_strdup_printf ("%s%s", CAMEL_LOCAL_STORE(store)->toplevel_dir, folder_name);
	fi->unread_message_count = -1;
	camel_folder_info_build_path(fi, '/');

	camel_object_trigger_event (CAMEL_OBJECT (store),
				    "folder_deleted", fi);
	
	camel_folder_info_free (fi);
}
