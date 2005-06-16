/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*-
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999, 2003 Ximian Inc.
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

#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#include "camel-maildir-folder.h"
#include "camel-maildir-store.h"
#include "camel-stream-fs.h"
#include "camel-maildir-summary.h"
#include "camel-data-wrapper.h"
#include "camel-mime-message.h"
#include "camel-exception.h"
#include "camel-i18n.h"

#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/

static CamelLocalFolderClass *parent_class = NULL;

/* Returns the class for a CamelMaildirFolder */
#define CMAILDIRF_CLASS(so) CAMEL_MAILDIR_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CF_CLASS(so) CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CMAILDIRS_CLASS(so) CAMEL_STORE_CLASS (CAMEL_OBJECT_GET_CLASS(so))

static int
maildir_folder_getv(CamelObject *object, CamelException *ex, CamelArgGetV *args)
{
	CamelFolder *folder = (CamelFolder *)object;
	int i;
	guint32 tag;

	for (i=0;i<args->argc;i++) {
		CamelArgGet *arg = &args->argv[i];

		tag = arg->tag;

		switch (tag & CAMEL_ARG_TAG) {
		case CAMEL_FOLDER_ARG_NAME:
			if (!strcmp(folder->full_name, "."))
				*arg->ca_str = _("Inbox");
			else
				*arg->ca_str = folder->name;
			break;
		default:
			continue;
		}

		arg->tag = (tag & CAMEL_ARG_TYPE) | CAMEL_ARG_IGNORE;
	}

	return ((CamelObjectClass *)parent_class)->getv(object, ex, args);
}

CamelFolder *
camel_maildir_folder_new(CamelStore *parent_store, const char *full_name, guint32 flags, CamelException *ex)
{
	CamelFolder *folder;

	d(printf("Creating maildir folder: %s\n", full_name));

	folder = (CamelFolder *)camel_object_new(CAMEL_MAILDIR_FOLDER_TYPE);

	if (parent_store->flags & CAMEL_STORE_FILTER_INBOX
	    && strcmp(full_name, ".") == 0)
		folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;

	folder = (CamelFolder *)camel_local_folder_construct((CamelLocalFolder *)folder,
							     parent_store, full_name, flags, ex);

	return folder;
}

static CamelLocalSummary *maildir_create_summary(CamelLocalFolder *lf, const char *path, const char *folder, CamelIndex *index)
{
	return (CamelLocalSummary *)camel_maildir_summary_new((CamelFolder *)lf, path, folder, index);
}

static void
maildir_append_message (CamelFolder *folder, CamelMimeMessage *message, const CamelMessageInfo *info, char **appended_uid, CamelException *ex)
{
	CamelLocalFolder *lf = (CamelLocalFolder *)folder;
	CamelStream *stream;
	CamelMessageInfo *mi;
	int retry = 0, res;
	char *dest = NULL, *uid = NULL;
	GString *name;
	struct stat st;

	d(printf("Appending message\n"));

	/*
	  Delivered per details at http://www.dataloss.nl/docs/maildir/

	  We do however, deliver straight to 'cur', bypassing 'new'.  Otherwise
	  we can't retain the incoming flags.  Given we are a client I dont
	  think this is an issue, it just saves us 1 step */

	name = g_string_new("");
	do {
		if (uid) {
			g_free(uid);
			sleep(2);
		}
		uid = camel_maildir_summary_next_uid((CamelMaildirSummary *)folder->summary);
		g_string_printf(name, "%s/tmp/%s", lf->folder_path, uid);
		res = stat(name->str, &st);
	} while ((res == 0 || errno != ENOENT) && retry++<5);

	if (res == 0 || errno != ENOENT) {
		g_free(uid);
		if (res == 0)
			errno = EEXIST;
		res = -1;
		goto fail;
	}
	res = -1;

	mi = camel_message_info_new_from_message(folder->summary, message, info);
	mi->uid = uid;
	dest = g_strdup_printf("%s/cur/%s:%s", lf->folder_path, uid, ((CamelMaildirMessageInfo *)mi)->ext);

	if ((stream = camel_stream_fs_new_with_name(name->str, O_WRONLY|O_CREAT, 0666)) == NULL
	    || camel_data_wrapper_write_to_stream((CamelDataWrapper *)message, stream) == -1
	    || camel_stream_flush(stream) == -1
	    || camel_stream_close(stream) == -1
	    || link(name->str, dest) == -1) {
	fail:
		if (errno == EINTR)
			camel_exception_set(ex, CAMEL_EXCEPTION_USER_CANCEL, _("Cancelled."));
		else
			camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot append message to maildir folder: %s: %s"), name, g_strerror(errno));
	} else {
		if (appended_uid)
			*appended_uid = g_strdup(uid);
	}

	if (stream) {
		camel_object_unref(stream);
		unlink(name->str);
	}
	g_free(dest);
	if (mi)
		camel_message_info_free(mi);

	g_string_free(name, TRUE);
}

static CamelMimeMessage *maildir_get_message(CamelFolder * folder, const gchar * uid, CamelException * ex)
{
	CamelLocalFolder *lf = (CamelLocalFolder *)folder;
	CamelMimeMessage *message = NULL;
	CamelMessageInfo *info;
	GString *name;
	int retry = 0;

	d(printf("getting message: %s\n", uid));

	/* Why do things have to be so bloody complicated?  Maildir works great - if you've
	   got any number of delivery agents and only one user agent.
	   But we have to worry about other clients changing flags and making messages
	   harder to find.

	   We use maildir's sync to re-check the filenames.  Perhaps it should be
	   refresh_info, but that checks for new mail too?  Are they any different? */

	name = g_string_new("");
	do {
		CamelStream *stream;

		if ((info = camel_folder_summary_get(folder->summary, uid)) == NULL) {
			camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
					     _("Cannot get message: %s from folder %s\n  %s"),
					     uid, lf->folder_path, _("No such message"));
			return NULL;
		}

		if (((CamelMaildirMessageInfo *)info)->ext && ((CamelMaildirMessageInfo *)info)->ext[0])
			g_string_append_printf(name, "%s/cur/%s:%s", lf->folder_path, info->uid, ((CamelMaildirMessageInfo *)info)->ext);
		else
			g_string_append_printf(name, "%s/cur/%s", lf->folder_path, info->uid);
		camel_message_info_free(info);

		if ((stream = camel_stream_fs_new_with_name(name->str, O_RDONLY, 0)) != NULL) {
			message = camel_mime_message_new();
			if (camel_data_wrapper_construct_from_stream((CamelDataWrapper *)message, stream) == -1) {
				camel_object_unref(message);
				message = NULL;
				if (errno == EINTR)
					camel_exception_setv(ex, CAMEL_EXCEPTION_USER_CANCEL, _("Cancelled."));
				else
					camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot get message: %s from folder %s\n  %s"),
							     uid, lf->folder_path, _("Invalid message contents"));
			}
			camel_object_unref(stream);
		} else if (errno == ENOENT && retry == 0) {
			camel_folder_sync(folder, FALSE, ex);
		} else {
			camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot get message: %s from folder %s\n  %s"),
					     uid, lf->folder_path, g_strerror(errno));
		}
	} while (retry++ < 2 && !camel_exception_is_set(ex) && message == NULL);

	g_string_free(name, TRUE);

	return message;
}

static void camel_maildir_folder_class_init(CamelObjectClass * camel_maildir_folder_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS(camel_maildir_folder_class);
	CamelLocalFolderClass *lclass = (CamelLocalFolderClass *)camel_maildir_folder_class;

	parent_class = CAMEL_LOCAL_FOLDER_CLASS (camel_type_get_global_classfuncs(camel_local_folder_get_type()));

	/* virtual method definition */

	/* virtual method overload */
	((CamelObjectClass *)camel_folder_class)->getv = maildir_folder_getv;

	camel_folder_class->append_message = maildir_append_message;
	camel_folder_class->get_message = maildir_get_message;

	lclass->create_summary = maildir_create_summary;
}

static void maildir_init(gpointer object, gpointer klass)
{
	/*CamelFolder *folder = object;
	  CamelMaildirFolder *maildir_folder = object;*/
}

static void maildir_finalize(CamelObject * object)
{
	/*CamelMaildirFolder *maildir_folder = CAMEL_MAILDIR_FOLDER(object);*/
}

CamelType camel_maildir_folder_get_type(void)
{
	static CamelType camel_maildir_folder_type = CAMEL_INVALID_TYPE;

	if (camel_maildir_folder_type == CAMEL_INVALID_TYPE) {
		camel_maildir_folder_type = camel_type_register(CAMEL_LOCAL_FOLDER_TYPE, "CamelMaildirFolder",
							   sizeof(CamelMaildirFolder),
							   sizeof(CamelMaildirFolderClass),
							   (CamelObjectClassInitFunc) camel_maildir_folder_class_init,
							   NULL,
							   (CamelObjectInitFunc) maildir_init,
							   (CamelObjectFinalizeFunc) maildir_finalize);
	}
 
	return camel_maildir_folder_type;
}
