/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*-
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *          Jeffrey Stedfast <fejj@ximian.com>
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

#include "camel-mbox-folder.h"
#include "camel-mbox-store.h"
#include "camel-stream-fs.h"
#include "camel-mbox-summary.h"
#include "camel-data-wrapper.h"
#include "camel-mime-message.h"
#include "camel-stream-filter.h"
#include "camel-mime-filter-from.h"
#include "camel-exception.h"
#include "camel-i18n.h"
#include "camel-mbox-view-summary.h"

#define d(x) (printf("%s(%d): ", __FILE__, __LINE__),(x))

static CamelLocalFolderClass *parent_class = NULL;

/* Returns the class for a CamelMboxFolder */
#define CMBOXF_CLASS(so) CAMEL_MBOX_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CF_CLASS(so) CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CMBOXS_CLASS(so) CAMEL_STORE_CLASS (CAMEL_OBJECT_GET_CLASS(so))

CamelFolder *
camel_mbox_folder_new(CamelStore *parent_store, const char *full_name, guint32 flags, CamelException *ex)
{
	CamelFolder *folder;

	d(printf("Creating mbox folder: %s in %s\n", full_name, camel_local_store_get_toplevel_dir((CamelLocalStore *)parent_store)));

	folder = (CamelFolder *)camel_object_new(CAMEL_MBOX_FOLDER_TYPE);
	folder = (CamelFolder *)camel_local_folder_construct((CamelLocalFolder *)folder,
							     parent_store, full_name, flags, ex);

	return folder;
}

static CamelLocalSummary *mbox_create_summary(CamelLocalFolder *lf, const char *path, const char *folder, CamelIndex *index)
{
	return (CamelLocalSummary *)camel_mbox_summary_new((CamelFolder *)lf, path, folder, index);
}

static int mbox_lock(CamelLocalFolder *lf, CamelLockType type, CamelException *ex)
{
	CamelMboxFolder *mf = (CamelMboxFolder *)lf;

	/* make sure we have matching unlocks for locks */
	g_assert(mf->lockfd == -1);

	mf->lockfd = open(lf->folder_path, O_RDWR, 0);
	if (mf->lockfd == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot create folder lock on %s: %s"),
				      lf->folder_path, g_strerror (errno));
		return -1;
	}

	/* These are internal mail folders, we dont really need to do this locking
	   anyway.  But just to make sure ... we still lock.

	   We do have one (significant) optimisation however, 'reads' are
	   NOT 'dot' locked.

	   This assumes writers only ever write to the end of a file,
	   and any updates to the folder write to a new file and link over it. */

	if (camel_lock_fcntl(mf->lockfd, type, ex) == 0) {
		if (camel_lock_flock(mf->lockfd, type, ex) == 0) {
			if (type == CAMEL_LOCK_READ)
				return 0;

			if (camel_lock_dot(lf->folder_path, ex) == 0)
				return 0;

			camel_unlock_flock(mf->lockfd);
		}
		camel_unlock_fcntl(mf->lockfd);
	}
	
	close (mf->lockfd);
	mf->lockfd = -1;
	
	return -1;
}

static void mbox_unlock(CamelLocalFolder *lf, CamelLockType type)
{
	CamelMboxFolder *mf = (CamelMboxFolder *)lf;

	g_assert(mf->lockfd != -1);

	if (type == CAMEL_LOCK_WRITE)
		camel_unlock_dot(lf->folder_path);
	camel_unlock_flock(mf->lockfd);
	camel_unlock_fcntl(mf->lockfd);

	close(mf->lockfd);
	mf->lockfd = -1;
}

static void
mbox_append_message(CamelFolder *folder, CamelMimeMessage * message, const CamelMessageInfo * info, char **appended_uid, CamelException *ex)
{
	CamelLocalFolder *lf = (CamelLocalFolder *)folder;
	CamelStream *output_stream = NULL, *filter_stream = NULL;
	CamelMimeFilter *filter_from = NULL;
	CamelMBOXView *root = (CamelMBOXView *)folder->summary->root_view->view;
	CamelMessageInfo *mi;
	char *fromline = NULL;
	int fd, retval;
	guint32 uid;
	struct stat st;
	char *xev;

	/* If we can't lock, dont do anything */
	if (camel_local_folder_lock(lf, CAMEL_LOCK_WRITE, ex) == -1)
		return;

	d(printf("Appending message\n"));

	/* first, check the summary is correct (updates folder_size too) */
	retval = camel_local_summary_check((CamelLocalSummary *)folder->summary, ex);
	if (retval == -1)
		goto fail;

	/* Create the messageinfo, set uid, create x-ev header */
	mi = camel_message_info_new_from_message(folder->summary, message, info);
	uid = camel_mbox_summary_next_uid((CamelMboxSummary *)folder->summary);
	mi->uid = g_strdup_printf("%d", uid);
	((CamelMboxMessageInfo *)mi)->frompos = root->folder_size;

	xev = camel_mbox_summary_encode_xev(mi->uid, ((CamelMessageInfoBase *)mi)->flags);
	camel_medium_set_header((CamelMedium *)message, "X-Evolution", xev);
	g_free(xev);

#ifdef STATUS_PINE
	if (((CamelMboxSummary *)folder->summary)->xstatus) {
		char status[8];

		camel_mbox_summary_encode_status(((CamelMessageInfoBase *)mi)->flags & CAMEL_MBOX_STATUS_STATUS, status);
		camel_medium_set_header((CamelMedium *)message, "Status", status);
		camel_mbox_summary_encode_status(((CamelMessageInfoBase *)mi)->flags & CAMEL_MBOX_STATUS_XSTATUS, status);
		camel_medium_set_header((CamelMedium *)message, "X-Status", status);
	}
#endif
	d(printf("Appending message: uid is %s\n", camel_message_info_uid(mi)));

	output_stream = camel_stream_fs_new_with_name(lf->folder_path, O_WRONLY|O_APPEND, 0600);
	if (output_stream == NULL) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot open mailbox: %s: %s\n"),
				      lf->folder_path, g_strerror (errno));
		goto fail;
	}

	/* we must write this to the non-filtered stream ... */
	fromline = camel_mime_message_build_mbox_from(message);
	if (camel_stream_write(output_stream, fromline, strlen(fromline)) == -1)
		goto fail_write;

	/* and write the content to the filtering stream, that translates '\nFrom' into '\n>From' */
	filter_stream = (CamelStream *) camel_stream_filter_new_with_stream(output_stream);
	filter_from = (CamelMimeFilter *) camel_mime_filter_from_new();
	camel_stream_filter_add((CamelStreamFilter *) filter_stream, filter_from);
	if (camel_data_wrapper_write_to_stream((CamelDataWrapper *)message, filter_stream) == -1
	    || camel_stream_write(filter_stream, "\n", 1) == -1
	    || camel_stream_close(filter_stream) == -1)
		goto fail_write;

	/* filter stream ref's the output stream itself, so we need to unref it too */
	camel_object_unref((CamelObject *)filter_from);
	camel_object_unref((CamelObject *)filter_stream);
	camel_object_unref((CamelObject *)output_stream);
	g_free(fromline);

	/* now we 'fudge' the summary  to tell it its uptodate, because its idea of uptodate has just changed */
	/* the stat really shouldn't fail, we just wrote to it */
	if (stat(lf->folder_path, &st) == 0) {
		root->folder_size = st.st_size;
		root->time = st.st_mtime;
		camel_view_changed((CamelView *)root);
	}

	/* now update the summary */
	((CamelMessageInfoBase *)mi)->size = root->folder_size - ((CamelMboxMessageInfo *)mi)->frompos;
	camel_folder_summary_add(folder->summary, mi);

	camel_local_folder_unlock(lf, CAMEL_LOCK_WRITE);

	if (appended_uid)
		*appended_uid = g_strdup(camel_message_info_uid(mi));

	camel_message_info_free(mi);

	return;

fail_write:
	if (errno == EINTR)
		camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
				     _("Mail append cancelled"));
	else
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot append message to mbox file: %s: %s"),
				      lf->folder_path, g_strerror (errno));
	
	if (filter_stream)
		camel_object_unref(CAMEL_OBJECT(filter_stream));

	if (output_stream)
		camel_object_unref(CAMEL_OBJECT(output_stream));

	if (filter_from)
		camel_object_unref(CAMEL_OBJECT(filter_from));

	g_free(fromline);

	/* reset the file to original size */
	fd = open(lf->folder_path, O_WRONLY, 0600);
	if (fd != -1) {
		ftruncate(fd, root->folder_size);
		close(fd);
	}
	
	/* and tell the summary its uptodate */
	if (stat(lf->folder_path, &st) == 0) {
		root->folder_size = st.st_size;
		root->time = st.st_mtime;
		camel_view_changed((CamelView *)root);
	}
	
fail:
	/* make sure we unlock the folder - before we start triggering events into appland */
	camel_local_folder_unlock(lf, CAMEL_LOCK_WRITE);
}

static CamelMimeMessage *
mbox_get_message(CamelFolder *folder, const gchar * uid, CamelException *ex)
{
	CamelLocalFolder *lf = (CamelLocalFolder *)folder;
	CamelMimeMessage *message = NULL;
	CamelMboxMessageInfo *info = NULL;
	CamelMimeParser *parser;
	CamelMBOXView *root = (CamelMBOXView *)folder->summary->root_view;
	int fd, retry=0;
	off_t frompos;

	d(printf("Getting message %s\n", uid));

	/* we use an fd instead of a normal stream here - the reason is subtle, camel_mime_part will cache
	   the whole message in memory if the stream is non-seekable (which it is when built from a parser
	   with no stream).  This means we dont have to lock the mbox for the life of the message, but only
	   while it is being created. */

	do {
		camel_exception_clear(ex);

		info = (CamelMboxMessageInfo *)camel_folder_summary_get(folder->summary, uid);
		if (info == NULL) {
			camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
					     _("Cannot get message: %s from folder %s\n  %s"),
					     uid, lf->folder_path, _("No such message"));
			return NULL;
		}

		frompos = info->frompos;
		camel_message_info_free(info);
		g_assert(frompos != -1);

		if (camel_local_folder_lock(lf, CAMEL_LOCK_READ, ex) != 0)
			break;

		fd = open(lf->folder_path, O_RDONLY);
		if (fd == -1) {
			camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
					     _("Cannot get message: %s from folder %s\n  %s"),
					     uid, lf->folder_path, g_strerror (errno));
			camel_local_folder_unlock(lf, CAMEL_LOCK_READ);
			break;
		}

		parser = camel_mime_parser_new();
		camel_mime_parser_init_with_fd(parser, fd);
		camel_mime_parser_scan_from(parser, TRUE);

		camel_mime_parser_seek(parser, frompos, SEEK_SET);
		if (camel_mime_parser_step(parser, NULL, NULL) != CAMEL_MIME_PARSER_STATE_FROM
		    || camel_mime_parser_tell_start_from(parser) != frompos) {

			g_warning("Summary doesn't match the folder contents!  eek!\n"
				  "  expecting offset %ld got %ld, state = %d", (long int)frompos,
				  (long int)camel_mime_parser_tell_start_from(parser),
				  camel_mime_parser_state(parser));

			root->folder_size = 0;

			camel_object_unref((CamelObject *)parser);
			camel_local_folder_unlock(lf, CAMEL_LOCK_READ);

			camel_folder_refresh_info(folder, ex);
			if (camel_exception_is_set(ex))
				break;

			camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID,
					     _("Cannot get message: %s from folder %s\n  %s"), uid, lf->folder_path,
					     _("The folder appears to be irrecoverably corrupted."));
		} else {
			int res;

			message = camel_mime_message_new();
			res = camel_mime_part_construct_from_parser((CamelMimePart *)message, parser);
			camel_object_unref(parser);
			camel_local_folder_unlock(lf, CAMEL_LOCK_READ);

			if (res == -1) {
				camel_exception_setv(ex, errno==EINTR?CAMEL_EXCEPTION_USER_CANCEL:CAMEL_EXCEPTION_SYSTEM,
						     _("Cannot get message: %s from folder %s\n  %s"), uid, lf->folder_path,
						     _("Message construction failed."));
				camel_object_unref(message);
				message = NULL;
				break;
			}
		}
	} while (message == NULL && retry++ < 1);

	if (message)
		camel_medium_remove_header((CamelMedium *)message, "X-Evolution");
	
	return message;
}

static CamelIterator *
mbox_get_folders(CamelFolder *folder, const char *pattern, CamelException *ex)
{
	char *path;
	CamelIterator *iter;

	path = g_strdup_printf("%s.sbd", ((CamelLocalFolder *)folder)->folder_path);
	iter = camel_mbox_store_get_folders(folder->parent_store, pattern, path, ex);
	g_free(path);

	return iter;
}

static void
mbox_init(gpointer object, gpointer klass)
{
	/*CamelFolder *folder = object;*/
	CamelMboxFolder *mbox_folder = object;

	mbox_folder->lockfd = -1;
}

static void
mbox_finalise(CamelObject * object)
{
	CamelMboxFolder *mbox_folder = (CamelMboxFolder *)object;

	g_assert(mbox_folder->lockfd == -1);
}

static void
camel_mbox_folder_class_init(CamelMboxFolderClass * camel_mbox_folder_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS(camel_mbox_folder_class);
	CamelLocalFolderClass *lclass = (CamelLocalFolderClass *)camel_mbox_folder_class;

	parent_class = (CamelLocalFolderClass *)camel_type_get_global_classfuncs(camel_local_folder_get_type());

	/* virtual method definition */

	/* virtual method overload */
	camel_folder_class->append_message = mbox_append_message;
	camel_folder_class->get_message = mbox_get_message;

	camel_folder_class->get_folders = mbox_get_folders;

	lclass->create_summary = mbox_create_summary;
	lclass->lock = mbox_lock;
	lclass->unlock = mbox_unlock;
}

CamelType camel_mbox_folder_get_type(void)
{
	static CamelType camel_mbox_folder_type = CAMEL_INVALID_TYPE;

	if (camel_mbox_folder_type == CAMEL_INVALID_TYPE) {
		camel_mbox_folder_type = camel_type_register(CAMEL_LOCAL_FOLDER_TYPE, "CamelMboxFolder",
							     sizeof(CamelMboxFolder),
							     sizeof(CamelMboxFolderClass),
							     (CamelObjectClassInitFunc) camel_mbox_folder_class_init,
							     NULL,
							     (CamelObjectInitFunc) mbox_init,
							     (CamelObjectFinalizeFunc) mbox_finalise);
	}

	return camel_mbox_folder_type;
}
