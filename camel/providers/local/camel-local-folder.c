/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*-
 * 
 * Authors: Michael Zucchi <notzed@helixcode.com>
 *
 * Copyright (C) 1999, 2000 Helix Code Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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

#include <config.h>

#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#include "camel-local-folder.h"
#include "camel-local-store.h"
#include "string-utils.h"
#include "camel-stream-fs.h"
#include "camel-local-summary.h"
#include "camel-data-wrapper.h"
#include "camel-mime-message.h"
#include "camel-stream-filter.h"
#include "camel-mime-filter-from.h"
#include "camel-exception.h"

#include "camel-local-private.h"

#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/

static CamelFolderClass *parent_class = NULL;

/* Returns the class for a CamelLocalFolder */
#define CLOCALF_CLASS(so) CAMEL_LOCAL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CF_CLASS(so) CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CLOCALS_CLASS(so) CAMEL_STORE_CLASS (CAMEL_OBJECT_GET_CLASS(so))

static int local_lock(CamelLocalFolder *lf, CamelLockType type, CamelException *ex);
static void local_unlock(CamelLocalFolder *lf);

static void local_sync(CamelFolder *folder, gboolean expunge, CamelException *ex);
static void local_expunge(CamelFolder *folder, CamelException *ex);

static GPtrArray *local_search_by_expression(CamelFolder *folder, const char *expression, CamelException *ex);
static void local_search_free(CamelFolder *folder, GPtrArray * result);

static void local_finalize(CamelObject * object);

static void
camel_local_folder_class_init(CamelLocalFolderClass * camel_local_folder_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS(camel_local_folder_class);

	parent_class = CAMEL_FOLDER_CLASS(camel_type_get_global_classfuncs(camel_folder_get_type()));

	/* virtual method definition */

	/* virtual method overload */
	camel_folder_class->sync = local_sync;
	camel_folder_class->expunge = local_expunge;

	camel_folder_class->search_by_expression = local_search_by_expression;
	camel_folder_class->search_free = local_search_free;

	camel_local_folder_class->lock = local_lock;
	camel_local_folder_class->unlock = local_unlock;
}

static void
local_init(gpointer object, gpointer klass)
{
	CamelFolder *folder = object;
	CamelLocalFolder *local_folder = object;

	folder->has_summary_capability = TRUE;
	folder->has_search_capability = TRUE;

	folder->permanent_flags = CAMEL_MESSAGE_ANSWERED |
	    CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_DRAFT |
	    CAMEL_MESSAGE_FLAGGED | CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_USER;

	folder->summary = NULL;
	local_folder->search = NULL;

	local_folder->priv = g_malloc0(sizeof(*local_folder->priv));
#ifdef ENABLE_THREADS
	local_folder->priv->search_lock = g_mutex_new();
#endif
}

static void
local_finalize(CamelObject * object)
{
	CamelLocalFolder *local_folder = CAMEL_LOCAL_FOLDER(object);
	CamelFolder *folder = (CamelFolder *)object;

	if (folder->summary) {
		camel_local_summary_sync((CamelLocalSummary *)folder->summary, FALSE, local_folder->changes, NULL);
		camel_object_unref((CamelObject *)folder->summary);
		folder->summary = NULL;
	}

	if (local_folder->search) {
		camel_object_unref((CamelObject *)local_folder->search);
	}

	/* must free index after summary, since it isn't refcounted */
	if (local_folder->index)
		ibex_close(local_folder->index);

	while (local_folder->locked> 0)
		camel_local_folder_unlock(local_folder);

	g_free(local_folder->base_path);
	g_free(local_folder->folder_path);
	g_free(local_folder->summary_path);
	g_free(local_folder->index_path);

	camel_folder_change_info_free(local_folder->changes);

#ifdef ENABLE_THREADS
	g_mutex_free(local_folder->priv->search_lock);
#endif
	g_free(local_folder->priv);
}

CamelType camel_local_folder_get_type(void)
{
	static CamelType camel_local_folder_type = CAMEL_INVALID_TYPE;

	if (camel_local_folder_type == CAMEL_INVALID_TYPE) {
		camel_local_folder_type = camel_type_register(CAMEL_FOLDER_TYPE, "CamelLocalFolder",
							     sizeof(CamelLocalFolder),
							     sizeof(CamelLocalFolderClass),
							     (CamelObjectClassInitFunc) camel_local_folder_class_init,
							     NULL,
							     (CamelObjectInitFunc) local_init,
							     (CamelObjectFinalizeFunc) local_finalize);
	}

	return camel_local_folder_type;
}

CamelLocalFolder *
camel_local_folder_construct(CamelLocalFolder *lf, CamelStore *parent_store, const char *full_name, guint32 flags, CamelException *ex)
{
	CamelFolder *folder;
	const char *root_dir_path, *name;
	struct stat st;
	int forceindex;

	folder = (CamelFolder *)lf;

	name = strrchr(full_name, '/');
	if (name)
		name++;
	else
		name = full_name;

	camel_folder_construct(folder, parent_store, full_name, name);

	root_dir_path = camel_local_store_get_toplevel_dir(CAMEL_LOCAL_STORE(folder->parent_store));

	lf->base_path = g_strdup(root_dir_path);
	lf->folder_path = g_strdup_printf("%s/%s", root_dir_path, full_name);
	lf->summary_path = g_strdup_printf("%s/%s.ev-summary", root_dir_path, full_name);
	lf->index_path = g_strdup_printf("%s/%s.ibex", root_dir_path, full_name);

	lf->changes = camel_folder_change_info_new();

	/* if we have no index file, force it */
	forceindex = stat(lf->index_path, &st) == -1;
	if (flags & CAMEL_STORE_FOLDER_BODY_INDEX) {

		lf->index = ibex_open(lf->index_path, O_CREAT | O_RDWR, 0600);
		if (lf->index == NULL) {
			/* yes, this isn't fatal at all */
			g_warning("Could not open/create index file: %s: indexing not performed", strerror(errno));
			forceindex = FALSE;
			/* record that we dont have an index afterall */
			flags &= ~CAMEL_STORE_FOLDER_BODY_INDEX;
		}
	} else {
		/* if we do have an index file, remove it */
		if (forceindex == FALSE) {
			unlink(lf->index_path);
		}
		forceindex = FALSE;
	}

	lf->flags = flags;

	folder->summary = (CamelFolderSummary *)CLOCALF_CLASS(lf)->create_summary(lf->summary_path, lf->folder_path, lf->index);
	if (camel_local_summary_load((CamelLocalSummary *)folder->summary, forceindex, ex) == -1) {
		camel_object_unref (CAMEL_OBJECT (folder));
		return NULL;
	}

	return lf;
}

/* lock the folder, may be called repeatedly (with matching unlock calls),
   with type the same or less than the first call */
int camel_local_folder_lock(CamelLocalFolder *lf, CamelLockType type, CamelException *ex)
{
	if (lf->locked > 0) {
		/* lets be anal here - its important the code knows what its doing */
		g_assert(lf->locktype == type || lf->locktype == CAMEL_LOCK_WRITE);
	} else {
		if (CLOCALF_CLASS(lf)->lock(lf, type, ex) == -1)
			return -1;
		lf->locktype = type;
	}

	lf->locked++;

	return 0;
}

/* unlock folder */
int camel_local_folder_unlock(CamelLocalFolder *lf)
{
	g_assert(lf->locked>0);
	lf->locked--;
	if (lf->locked == 0)
		CLOCALF_CLASS(lf)->unlock(lf);

	return 0;
}

static int
local_lock(CamelLocalFolder *lf, CamelLockType type, CamelException *ex)
{
	return 0;
}

static void
local_unlock(CamelLocalFolder *lf)
{
	/* nothing */
}

static void
local_sync(CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	CamelLocalFolder *lf = CAMEL_LOCAL_FOLDER(folder);

	d(printf("local sync, expunge=%s\n", expunge?"true":"false"));

	if (camel_local_folder_lock(lf, CAMEL_LOCK_WRITE, ex) == -1)
		return;

	/* if sync fails, we'll pass it up on exit through ex */
	camel_local_summary_sync((CamelLocalSummary *)folder->summary, expunge, lf->changes, ex);
	camel_local_folder_unlock(lf);

	if (camel_folder_change_info_changed(lf->changes)) {
		camel_object_trigger_event(CAMEL_OBJECT(folder), "folder_changed", lf->changes);
		camel_folder_change_info_clear(lf->changes);
	}

	/* force save of metadata */
	if (lf->index)
		ibex_save(lf->index);
	if (folder->summary)
		camel_folder_summary_save(folder->summary);
}

static void
local_expunge(CamelFolder *folder, CamelException *ex)
{
	d(printf("expunge\n"));

	/* Just do a sync with expunge, serves the same purpose */
	/* call the callback directly, to avoid locking problems */
	CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(folder))->sync(folder, TRUE, ex);
}

static GPtrArray *
local_search_by_expression(CamelFolder *folder, const char *expression, CamelException *ex)
{
	CamelLocalFolder *local_folder = CAMEL_LOCAL_FOLDER(folder);
	GPtrArray *summary, *matches;

	/* NOTE: could get away without the search lock by creating a new
	   search object each time */

	CAMEL_LOCAL_FOLDER_LOCK(folder, search_lock);

	if (local_folder->search == NULL)
		local_folder->search = camel_folder_search_new();

	camel_folder_search_set_folder(local_folder->search, folder);
	camel_folder_search_set_body_index(local_folder->search, local_folder->index);
	summary = camel_folder_get_summary(folder);
	camel_folder_search_set_summary(local_folder->search, summary);

	matches = camel_folder_search_execute_expression(local_folder->search, expression, ex);

	CAMEL_LOCAL_FOLDER_UNLOCK(folder, search_lock);

	camel_folder_free_summary(folder, summary);

	return matches;
}

static void
local_search_free(CamelFolder *folder, GPtrArray * result)
{
	CamelLocalFolder *local_folder = CAMEL_LOCAL_FOLDER(folder);

	/* we need to lock this free because of the way search_free_result works */
	/* FIXME: put the lock inside search_free_result */
	CAMEL_LOCAL_FOLDER_LOCK(folder, search_lock);

	camel_folder_search_free_result(local_folder->search, result);

	CAMEL_LOCAL_FOLDER_UNLOCK(folder, search_lock);
}
