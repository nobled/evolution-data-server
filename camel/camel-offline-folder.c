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

#include "camel-offline-folder.h"
#include "camel-operation.h"
#include "camel-service.h"
#include "camel-session.h"

static gpointer parent_class;

static GSList *offline_folder_props = NULL;

static CamelProperty offline_prop_list[] = {
	{ CAMEL_OFFLINE_FOLDER_SYNC_OFFLINE, "sync_offline", N_("Copy folder content locally for offline operation") },
};

struct _offline_downsync_msg {
	CamelSessionThreadMsg msg;

	CamelFolder *folder;
	CamelFolderChangeInfo *changes;
};

static void
offline_downsync_sync (CamelSession *session, CamelSessionThreadMsg *mm)
{
	struct _offline_downsync_msg *m = (struct _offline_downsync_msg *) mm;
	CamelMimeMessage *message;
	gint i;

	camel_operation_start (NULL, _("Downloading new messages for offline mode"));

	if (m->changes) {
		for (i = 0; i < m->changes->uid_added->len; i++) {
			gint pc = i * 100 / m->changes->uid_added->len;

			camel_operation_progress (NULL, pc);
			if ((message = camel_folder_get_message (m->folder, m->changes->uid_added->pdata[i], &mm->ex)))
				g_object_unref (message);
		}
	} else {
		camel_offline_folder_downsync ((CamelOfflineFolder *) m->folder, "(match-all)", &mm->ex);
	}

	camel_operation_end (NULL);
}

static void
offline_downsync_free (CamelSession *session, CamelSessionThreadMsg *mm)
{
	struct _offline_downsync_msg *m = (struct _offline_downsync_msg *) mm;

	if (m->changes)
		camel_folder_change_info_free (m->changes);

	g_object_unref (m->folder);
}

static CamelSessionThreadOps offline_downsync_ops = {
	offline_downsync_sync,
	offline_downsync_free,
};

static void
offline_folder_changed (CamelFolder *folder, CamelFolderChangeInfo *changes, gpointer dummy)
{
	CamelOfflineFolder *offline = (CamelOfflineFolder *) folder;
	CamelService *service = (CamelService *) folder->parent_store;

	if (changes->uid_added->len > 0 && (offline->sync_offline || camel_url_get_param (service->url, "sync_offline"))) {
		CamelSession *session = service->session;
		struct _offline_downsync_msg *m;

		m = camel_session_thread_msg_new (session, &offline_downsync_ops, sizeof (*m));
		m->changes = camel_folder_change_info_new ();
		camel_folder_change_info_cat (m->changes, changes);
		g_object_ref (folder);
		m->folder = folder;

		camel_session_thread_queue (session, &m->msg, 0);
	}
}

static gint
offline_folder_getv (CamelObject *object,
                     CamelException *ex,
                     CamelArgGetV *args)
{
	CamelArgGetV props;
	gint i, count = 0;
	guint32 tag;

	for (i = 0; i < args->argc; i++) {
		CamelArgGet *arg = &args->argv[i];

		tag = arg->tag;

		switch (tag & CAMEL_ARG_TAG) {
		case CAMEL_OBJECT_ARG_PERSISTENT_PROPERTIES:
		case CAMEL_FOLDER_ARG_PROPERTIES:
			props.argc = 1;
			props.argv[0] = *arg;
			((CamelObjectClass *) parent_class)->getv (object, ex, &props);
			*arg->ca_ptr = g_slist_concat (*arg->ca_ptr, g_slist_copy (offline_folder_props));
			break;
		case CAMEL_OFFLINE_FOLDER_ARG_SYNC_OFFLINE:
			*arg->ca_int = ((CamelOfflineFolder *) object)->sync_offline;
			break;
		default:
			count++;
			continue;
		}

		arg->tag = (tag & CAMEL_ARG_TYPE) | CAMEL_ARG_IGNORE;
	}

	if (count)
		return ((CamelObjectClass *) parent_class)->getv (object, ex, args);

	return 0;
}

static gint
offline_folder_setv (CamelObject *object,
                     CamelException *ex,
                     CamelArgV *args)
{
	CamelOfflineFolder *folder = (CamelOfflineFolder *) object;
	gboolean save = FALSE;
	guint32 tag;
	gint i;

	for (i = 0; i < args->argc; i++) {
		CamelArg *arg = &args->argv[i];

		tag = arg->tag;

		switch (tag & CAMEL_ARG_TAG) {
		case CAMEL_OFFLINE_FOLDER_ARG_SYNC_OFFLINE:
			if (folder->sync_offline != arg->ca_int) {
				folder->sync_offline = arg->ca_int;
				save = TRUE;
			}
			break;
		default:
			continue;
		}

		arg->tag = (tag & CAMEL_ARG_TYPE) | CAMEL_ARG_IGNORE;
	}

	if (save)
		camel_object_state_write (object);

	return ((CamelObjectClass *) parent_class)->setv (object, ex, args);
}

static void
offline_folder_downsync (CamelOfflineFolder *offline,
                         const gchar *expression,
                         CamelException *ex)
{
	CamelFolder *folder = (CamelFolder *) offline;
	GPtrArray *uids, *uncached_uids = NULL;
	gint i;

	camel_operation_start (NULL, _("Syncing messages in folder '%s' to disk"), folder->full_name);

	if (expression)
		uids = camel_folder_search_by_expression (folder, expression, ex);
	else
		uids = camel_folder_get_uids (folder);

	if (!uids)
		goto done;
	uncached_uids = camel_folder_get_uncached_uids(folder, uids, ex);
	if (uids) {
		if (expression)
			camel_folder_search_free (folder, uids);
		else
			camel_folder_free_uids (folder, uids);
	}

	if (!uncached_uids)
		goto done;

	for (i = 0; i < uncached_uids->len; i++) {
		gint pc = i * 100 / uncached_uids->len;
		camel_folder_sync_message (folder, uncached_uids->pdata[i], ex);
		camel_operation_progress (NULL, pc);
	}

done:
	if (uncached_uids)
		camel_folder_free_uids(folder, uncached_uids);

	camel_operation_end (NULL);
}

static void
offline_folder_class_init (CamelOfflineFolderClass *class)
{
	CamelObjectClass *camel_object_class;
	gint ii;

	parent_class = g_type_class_peek_parent (class);

	camel_object_class = CAMEL_OBJECT_CLASS (class);
	camel_object_class->getv = offline_folder_getv;
	camel_object_class->setv = offline_folder_setv;

	class->downsync = offline_folder_downsync;

	for (ii = 0; ii < G_N_ELEMENTS (offline_prop_list); ii++) {
		offline_prop_list[ii].description =
			_(offline_prop_list[ii].description);
		offline_folder_props = g_slist_prepend (
			offline_folder_props, &offline_prop_list[ii]);
	}

}

static void
offline_folder_init (CamelOfflineFolder *folder)
{
	camel_object_hook_event (
		folder, "folder_changed",
		(CamelObjectEventHookFunc) offline_folder_changed, NULL);
}

GType
camel_offline_folder_get_type (void)
{
	static GType type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID))
		type = g_type_register_static_simple (
			CAMEL_TYPE_FOLDER,
			"CamelOfflineFolder",
			sizeof (CamelOfflineFolderClass),
			(GClassInitFunc) offline_folder_class_init,
			sizeof (CamelOfflineFolder),
			(GInstanceInitFunc) offline_folder_init,
			0);

	return type;
}

/**
 * camel_offline_folder_downsync:
 * @offline: a #CamelOfflineFolder object
 * @expression: search expression describing which set of messages to downsync (%NULL for all)
 * @ex: a #CamelException
 *
 * Syncs messages in @offline described by the search @expression to
 * the local machine for offline availability.
 **/
void
camel_offline_folder_downsync (CamelOfflineFolder *offline,
                               const gchar *expression,
                               CamelException *ex)
{
	CamelOfflineFolderClass *class;

	g_return_if_fail (CAMEL_IS_OFFLINE_FOLDER (offline));

	class = CAMEL_OFFLINE_FOLDER_GET_CLASS (offline);
	g_return_if_fail (class->downsync != NULL);

	class->downsync (offline, expression, ex);
}
