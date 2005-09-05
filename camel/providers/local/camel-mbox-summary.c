/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*-
 *
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "camel-mbox-folder.h"
#include "camel-mbox-summary.h"
#include "camel/camel-file-utils.h"
#include "camel/camel-mime-message.h"
#include "camel/camel-operation.h"
#include "camel-i18n.h"
#include "camel-record.h"

#include "camel-mbox-view-summary.h"

/* NB, this is only for the messy iterator_get interface, which could be better hidden */
#include "libdb/dist/db.h"

#define io(x)
#define d(x) (printf("%s(%d): ", __FILE__, __LINE__),(x))

#define CFS_CLASS(x) ((CamelFolderSummaryClass *)((CamelObject *)x)->klass)
#define CFS(x) ((CamelFolderSummary *)x)

static CamelLocalSummaryClass *camel_mbox_summary_parent;

#ifdef STATUS_PINE
static struct {
	char tag;
	guint32 flag;
} status_flags[] = {
	{ 'F', CAMEL_MESSAGE_FLAGGED },
	{ 'A', CAMEL_MESSAGE_ANSWERED },
	{ 'D', CAMEL_MESSAGE_DELETED },
	{ 'R', CAMEL_MESSAGE_SEEN },
};

void
camel_mbox_summary_encode_status(guint32 flags, char status[8])
{
	char *p;
	int i;

	p = status;
	for (i=0;i<sizeof(status_flags)/sizeof(status_flags[0]);i++)
		if (status_flags[i].flag & flags)
			*p++ = status_flags[i].tag;
	*p++ = 'O';
	*p=0;
}

static guint32
decode_status(const char *status)
{
	const char *p;
	char c;
	guint32 flags = 0;
	int i;

	p = status;
	while ((c = *p++)) {
		for (i=0;i<sizeof(status_flags)/sizeof(status_flags[0]);i++)
			if (status_flags[i].tag == *p)
				flags |= status_flags[i].flag;
	}

	return flags;
}
#endif /* STATUS_PINE */

static int mbox_add(CamelFolderSummary *s, void *o)
{
	CamelMessageInfo *mi = o;
	int res;

	res = ((CamelFolderSummaryClass *)camel_mbox_summary_parent)->add(s, mi);
	if (res == 0) {
		guint32 uid = strtoul(camel_message_info_uid(mi), NULL, 10);

		camel_mbox_summary_last_uid((CamelMboxSummary *)s, uid);
	}

	return res;
}

static void
mbox_sync_changes(CamelFolderSummaryDisk *cds, GPtrArray *changes, CamelException *ex)
{
	CamelMimeParser *mp = NULL;
	int i;
	int fd = -1, pfd;
	const char *xev;
	int len;
	off_t lastpos;
	struct stat st;

	/* Do we even care anymore?
	   Should flags never be stored in the mailbox, unless the mailbox is being exported?? */

	/* Try to also store the changes in the mbox.  If something goes wrong -
	   tough luck, its stored in the summary ok */

#ifdef STATUS_PINE
	/* Storing status headers anyway?  Dont even bother - wait till the next sync */
	if (((CamelMboxSummary *)cds)->xstatus) {
		((CamelMboxSummary *)cds)->xstatus_changed = 1;
		goto done;
	}
#endif
	printf("attempting to store '%d changes to mbox file\n", changes->len);

	if (camel_local_folder_lock((CamelLocalFolder *)((CamelFolderSummary *)cds)->folder, CAMEL_LOCK_WRITE, NULL) == -1)
		goto done;

	camel_operation_start(NULL, _("Storing folder"));

	fd = open(((CamelLocalSummary *)cds)->folder_path, O_RDWR);
	if (fd == -1)
		//camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Could not open file: %s: %s"), cls->folder_path, g_strerror (errno));
		goto fail;

	/* need to dup since mime parser closes its fd once it is finalised */
	pfd = dup(fd);
	if (pfd == -1)
		//camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, _("Could not store folder: %s"), g_strerror(errno));
		goto fail;

	mp = camel_mime_parser_new();
	camel_mime_parser_scan_from(mp, TRUE);
	camel_mime_parser_scan_pre_from(mp, TRUE);
	camel_mime_parser_init_with_fd(mp, pfd);

	for (i = 0; i < changes->len; i++) {
		CamelMboxMessageInfo *info = changes->pdata[i];
		int xevoffset;
		int pc = (i+1)*100/changes->len;
		char *uid = NULL, *xevnew = NULL;
		guint32 flags;

		camel_operation_progress(NULL, pc);

		/* We do an anally amount of testing to make sure we're updating the
		   right message ... its all cheap at this point so why not */

		camel_mime_parser_seek(mp, info->frompos, SEEK_SET);

		/* wrong spot?  well try the rest */
		if (camel_mime_parser_step(mp, 0, 0) != CAMEL_MIME_PARSER_STATE_FROM)
			//camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, _("Summary and folder mismatch, even after a sync"));
			goto nextmsg2;

		if (camel_mime_parser_tell_start_from(mp) != info->frompos) {
			printf("frompos changed\n");
			goto nextmsg1;
		}

		if (camel_mime_parser_step(mp, 0, 0) == CAMEL_MIME_PARSER_STATE_FROM_END) {
			printf("message truncated\n");
			goto nextmsg1;
		}

		xev = camel_mime_parser_header(mp, "X-Evolution", &xevoffset);
		if (xev == NULL || (uid = camel_mbox_summary_decode_xev(xev, &flags)) == NULL) {
			printf("no x-evolution or bad format\n");
			goto nextmsg;
		}

		if (strcmp(uid, camel_message_info_uid(info)) != 0) {
			printf("wrong uid, got %s expecting %s?\n", uid, camel_message_info_uid(info));
			goto nextmsg;
		}

		/* the raw header contains a leading ' ', so (dis)count that too */
		xevnew = camel_mbox_summary_encode_xev(uid, camel_message_info_flags(info));
		if (strlen(xev)-1 != strlen(xevnew)) {
			printf("xev changed sized was '%s' now '%s'\n", xev, xevnew);
			goto nextmsg;
		}

		lastpos = lseek(fd, 0, SEEK_CUR);
		lseek(fd, xevoffset+strlen("X-Evolution: "), SEEK_SET);
		do {
			len = write(fd, xevnew, strlen(xevnew));
		} while (len == -1 && errno == EINTR);
		lseek(fd, lastpos, SEEK_SET);
	nextmsg:
		g_free(xevnew);
		g_free(uid);
	nextmsg1:
		camel_mime_parser_drop_step(mp);
	nextmsg2:
		camel_mime_parser_drop_step(mp);
		/* dunno if this will leave it in the right state in case of failures? */
	}

	d(printf("Closing folders\n"));

	/* all ok, update the mtime */
	if (fstat(fd, &st) == 0)
		((CamelMBOXView *)CFS(cds)->root_view->view)->time = st.st_mtime;
fail:
	if (fd != -1)
		close(fd);
	if (mp)
		camel_object_unref((CamelObject *)mp);

	camel_operation_end(NULL);

	camel_local_folder_unlock((CamelLocalFolder *)((CamelFolderSummary *)cds)->folder, CAMEL_LOCK_WRITE);
done:
	/* always remove the flagged flag, its used just to trigger us indirectly */
	for (i = 0; i < changes->len; i++)
		((CamelMessageInfoBase *)changes->pdata[i])->flags &= ~CAMEL_MESSAGE_FOLDER_FLAGGED;

	/* FIXME: we actually wnat to sync the db first ...  then update the mbox/mtime, then
	   re-save the header */
	((CamelFolderSummaryDiskClass *)camel_mbox_summary_parent)->sync(cds, changes, ex);
}

void
camel_mbox_summary_construct(CamelMboxSummary *new, struct _CamelFolder *folder, const char *filename, const char *mbox_name, CamelIndex *index)
{
	const CamelMessageInfo *mi;
	CamelIterator *iter;
	CamelMBOXView *root;

	camel_local_summary_construct((CamelLocalSummary *)new, folder, filename, mbox_name, index);

	root = (CamelMBOXView *)CFS(new)->root_view->view;

	iter = camel_folder_summary_search((CamelFolderSummary *)new, NULL, NULL, NULL, NULL);
	mi = camel_message_iterator_disk_get(iter, DB_LAST, DB_PREV, NULL);
	if (mi) {
		guint32 uid = strtoul(camel_message_info_uid(mi), NULL, 10);

		printf("Last uid in database %s is %d, last in header is %d\n", folder->full_name, (guint32)uid, (guint32)root->nextuid);
		camel_mbox_view_last_uid(root, uid);
	} else {
		printf("Nothing in the database %s, last uid in header is %d\n", folder->full_name, (guint32)root->nextuid);
	}
	camel_iterator_free(iter);
}

/**
 * camel_mbox_summary_new:
 *
 * Create a new CamelMboxSummary object.
 * 
 * Return value: A new CamelMboxSummary widget.
 **/
CamelMboxSummary *
camel_mbox_summary_new(struct _CamelFolder *folder, const char *filename, const char *mbox_name, CamelIndex *index)
{
	CamelMboxSummary *new = (CamelMboxSummary *)camel_object_new(camel_mbox_summary_get_type());

	camel_mbox_summary_construct(new, folder, filename, mbox_name, index);

	return new;
}

void camel_mbox_summary_xstatus(CamelMboxSummary *mbs, int state)
{
	mbs->xstatus = state;
}

/* NB: must have write lock on folder */
guint32 camel_mbox_summary_next_uid(CamelMboxSummary *mbs)
{
	guint32 uid;
	CamelMBOXView *view = (CamelMBOXView *)CFS(mbs)->root_view->view;

	uid = view->nextuid++;
	camel_view_changed((CamelView *)view);

	return uid;
}

/* NB: must have write lock on folder */
void camel_mbox_summary_last_uid(CamelMboxSummary *mbs, guint32 uid)
{
	CamelMBOXView *view = (CamelMBOXView *)CFS(mbs)->root_view->view;

	uid++;
	if (uid > view->nextuid) {
		view->nextuid = uid;
		camel_view_changed((CamelView *)view);
	}
}

char *
camel_mbox_summary_decode_xev(const char *xev, guint32 *flagsp)
{
	guint32 uid, flags;
	char *header;
	char uidstr[20];

	uidstr[0] = 0;

	/* check for uid/flags */
	header = camel_header_token_decode(xev);
	if (header && strlen(header) == strlen("00000000-0000")
	    && sscanf(header, "%08x-%04x", &uid, &flags) == 2) {
		sprintf(uidstr, "%u", uid);
	} else {
		g_free(header);
		return NULL;
	}
	g_free(header);

	if (flagsp)
		*flagsp = flags;

	return g_strdup(uidstr);
}

char *
camel_mbox_summary_encode_xev(const char *uidstr, guint32 flags)
{
	guint32 uid;

	if (sscanf(uidstr, "%u", &uid) == 1)
		return g_strdup_printf("%08x-%04x", uid, flags & 0xffff);
	else
		return g_strdup_printf("%s-%04x", uidstr, flags & 0xffff);
}

static CamelMessageInfo *
message_info_new_from_header(CamelFolderSummary *s, struct _camel_header_raw *h)
{
	CamelMboxMessageInfo *mi;
	CamelMboxSummary *mbs = (CamelMboxSummary *)s;

	mi = (CamelMboxMessageInfo *)((CamelFolderSummaryClass *)camel_mbox_summary_parent)->message_info_new_from_header(s, h);
	if (mi) {
#ifdef STATUS_PINE
		const char *status = NULL, *xstatus = NULL;
		guint32 flags = 0;

		if (mbs->xstatus) {
			/* check for existance of status & x-status headers */
			status = camel_header_raw_find(&h, "Status", NULL);
			if (status)
				flags = decode_status(status);
			xstatus = camel_header_raw_find(&h, "X-Status", NULL);
			if (xstatus)
				flags |= decode_status(xstatus);

			if (status)
				((CamelMessageInfoBase *)mi)->flags =
					(((CamelMessageInfoBase *)mi)->flags & ~(CAMEL_MBOX_STATUS_STATUS))
					| (flags & CAMEL_MBOX_STATUS_STATUS);
			if (xstatus)
				((CamelMessageInfoBase *)mi)->flags =
					(((CamelMessageInfoBase *)mi)->flags & ~(CAMEL_MBOX_STATUS_XSTATUS))
					| (flags & CAMEL_MBOX_STATUS_XSTATUS);
		}
#endif
		mi->frompos = -1;
	}
	
	return (CamelMessageInfo *)mi;
}

static CamelMessageInfo *
message_info_new_from_parser(CamelFolderSummary *s, CamelMimeParser *mp)
{
	CamelMessageInfo *mi;

	abort();

	mi = ((CamelFolderSummaryClass *)camel_mbox_summary_parent)->message_info_new_from_parser(s, mp);
	if (mi)
		((CamelMboxMessageInfo *)mi)->frompos = camel_mime_parser_tell_start_from(mp);
	
	return mi;
}

static int
message_info_decode(CamelFolderSummaryDisk *s, CamelMessageInfoDisk *mi, CamelRecordDecoder *crd)
{
	int tag, ver, res = -1;

	io(printf("loading mbox message info\n"));

	/* FIXME: check returns */
	if (((CamelFolderSummaryDiskClass *)camel_mbox_summary_parent)->decode(s, mi, crd) != 0)
		return res;

	camel_record_decoder_reset(crd);
	while ((tag = camel_record_decoder_next_section(crd, &ver)) != CR_SECTION_INVALID) {
		switch (tag) {
		case CFS_MBOX_SECTION_INFO:
			((CamelMboxMessageInfo *)mi)->frompos = camel_record_decoder_sizet(crd);
			res = 0;
			break;
		}
	}

	return res;
}

static void
message_info_encode(CamelFolderSummaryDisk *s, CamelMessageInfoDisk *mi, CamelRecordEncoder *cre)
{
	io(printf("saving mbox message info\n"));

	((CamelFolderSummaryDiskClass *)camel_mbox_summary_parent)->encode(s, mi, cre);

	camel_record_encoder_start_section(cre, CFS_MBOX_SECTION_INFO, 0);
	camel_record_encoder_sizet(cre, ((CamelMboxMessageInfo *)mi)->frompos);
	camel_record_encoder_end_section(cre);
}

static int
summary_update(CamelFolderSummary *s, off_t offset, CamelChangeInfo *changes, CamelException *ex)
{
	CamelMimeParser *mp;
	CamelIterator *iter;
	const CamelMessageInfo *iterinfo = NULL;
	int fd;
	int res = 0;
	guint32 lastuid = 0;
	struct stat st;
	off_t size = 0;
	GCompareDataFunc uid_cmp = CFS_CLASS(s)->uid_cmp;

	d(printf("Calling summary update, from pos %d\n", (int)offset));

	//cls->index_force = FALSE;

	camel_operation_start(NULL, _("Checking for new mail"));

	fd = open(((CamelLocalSummary *)s)->folder_path, O_RDWR);
	if (fd == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not open folder: %s: %s"),
				      ((CamelLocalSummary *)s)->folder_path, g_strerror (errno));
		camel_operation_end(NULL);
		return -1;
	}

	/* size == 0, could optimise ... */
	if (fstat(fd, &st) == 0)
		size = st.st_size;

	mp = camel_mime_parser_new();
	camel_mime_parser_init_with_fd(mp, fd);
	camel_mime_parser_scan_from(mp, TRUE);

	/* If the offset > 0, it must mean we're scanning from the end of the known universe
	   in our summary, so assume every message is new */

	if (offset > 0) {
		camel_mime_parser_seek(mp, offset, SEEK_SET);
		if (camel_mime_parser_step(mp, NULL, NULL) == CAMEL_MIME_PARSER_STATE_FROM
		    && camel_mime_parser_tell_start_from(mp) == offset) {
			camel_mime_parser_unstep(mp);
		} else {
			g_warning("The next message didn't start where I expected, building summary from start");
			camel_mime_parser_drop_step(mp);
			offset = 0;
			camel_mime_parser_seek(mp, offset, SEEK_SET);
		}
	}

	if (offset > 0) {
		iter = NULL;
		iterinfo = NULL;
	} else {
		// failures?
		iter = camel_folder_summary_search(s, NULL, NULL, NULL, NULL);
		iterinfo = camel_iterator_next(iter, NULL);
	}

	while (res == 0 && camel_mime_parser_step(mp, NULL, NULL) == CAMEL_MIME_PARSER_STATE_FROM) {
		CamelMessageInfo *info;
		off_t pc = camel_mime_parser_tell_start_from (mp) + 1;
		struct _camel_header_raw *h;
		char *uid = NULL;
		const char *xev;
		int xevoffset;
		guint32 thisuid, flags = 0;
		int fixxev = 0;
		off_t frompos;

		camel_operation_progress (NULL, (int) (((float) pc / size) * 100));

		if (camel_mime_parser_step(mp, NULL, NULL) == CAMEL_MIME_PARSER_STATE_FROM_END) {
			camel_exception_setv(ex, 1, _("Fatal mail parser error near position %ld in folder %s"),
					     camel_mime_parser_tell(mp), ((CamelLocalSummary *)s)->folder_path);
			res = -1;
			break;
		}

		frompos = camel_mime_parser_tell_start_from(mp);

		h = camel_mime_parser_headers_raw(mp);
		xev = camel_header_raw_find(&h, "x-evolution", &xevoffset);
		if (xev && (uid = camel_mbox_summary_decode_xev(xev, &flags))) {
			thisuid = strtoul(uid, NULL, 10);
			camel_mbox_summary_last_uid((CamelMboxSummary *)s, thisuid);

			if (thisuid < lastuid) {
				printf("the uid is out of order, renumbering it\n");
				/* Ok, so the mailbox violates the strict ordering requirements.
				   Some other client must have updated the folder, and re-ordered
				   messages in it.  Lets re-number anything we need to */
				fixxev = 1;
				goto add_message;
			} else {
				lastuid = thisuid;
			}

			while (iterinfo && uid_cmp(iterinfo->uid, uid, s) < 0) {
				printf("Message %s vanished\n", iterinfo->uid);
				camel_change_info_remove(changes, iterinfo);
				camel_folder_summary_remove(s, (CamelMessageInfo *)iterinfo);
				iterinfo = camel_iterator_next(iter, NULL);
			}

			if (iterinfo && uid_cmp(iterinfo->uid, uid, s) == 0) {
				info = (CamelMessageInfo *)iterinfo;
				camel_message_info_ref(info);
				iterinfo = camel_iterator_next(iter, NULL);
				g_free(uid);
				goto have_message;
			}
			/* We are also adding this message, but it has a valid
			   & validly numbered xev already */
		} else {
		add_message:
			g_free(uid);
			uid = NULL;
		}

		/* What about checking size? */

		info = camel_message_info_new_from_header(s, h);
		((CamelMboxMessageInfo *)info)->frompos = frompos;
		if (uid == NULL) {
			lastuid = camel_mbox_summary_next_uid((CamelMboxSummary *)s);
			uid = g_strdup_printf("%d", lastuid);
			printf("all new message, adding uid\n");
		}

		info->uid = uid;
		// FIXME: handle x-status/status flags properly
		((CamelMessageInfoBase *)info)->flags |= flags;

		if (fixxev) {
			char *xevnew = camel_mbox_summary_encode_xev(uid, ((CamelMessageInfoBase *)info)->flags);
			off_t lastpos;
			ssize_t len;

			printf("renumbering msg to %s\n", uid);

			/* We update any xev's we need to re-number.
			   We dont worry about writing new x-evolution's here, they
			   can be detected at sync time */

			lastpos = lseek(fd, 0, SEEK_CUR);
			lseek(fd, xevoffset+strlen("X-Evolution: "), SEEK_SET);
			do {
				len = write(fd, xevnew, strlen(xevnew));
			} while (len == -1 && errno == EINTR);
			lseek(fd, lastpos, SEEK_SET);
			g_free(xevnew);
		}

		camel_folder_summary_add(s, info);

		camel_change_info_add(changes, info);
		// FIXME: set RECENT flag ...
		if (xev == NULL)
			camel_change_info_recent(changes, info);
	have_message:

		// FIXME: check/perform indexing here


		if (((CamelMboxMessageInfo *)info)->frompos != frompos) {
			// FIXME: This will trigger not only a save of the messageinfo, but a
			// re-store of the flags on the message, even though they haven't changed
			((CamelMboxMessageInfo *)info)->frompos = frompos;
			camel_message_info_set_flags(info, CAMEL_MESSAGE_FOLDER_FLAGGED, ~0);
		}

		camel_message_info_free(info);

		/* Scan to next from header */
		camel_mime_parser_drop_step(mp);
		camel_mime_parser_drop_step(mp);

		/* Do we really want to do this?  It could make rather a pigs-breakfast of the db? */
		if (camel_operation_cancel_check(NULL)) {
			camel_exception_setv(ex, CAMEL_EXCEPTION_USER_CANCEL, "");
			res = -1;
			break;
		}
	}


	camel_object_unref(mp);

	if (res == 0) {
		while (iterinfo) {
			printf("trailing message '%s' removed too\n", iterinfo->uid);
			camel_folder_summary_remove(s, (CamelMessageInfo *)iterinfo);
			camel_change_info_remove(changes, iterinfo);
			iterinfo = camel_iterator_next(iter, NULL);
		}
	}
	camel_iterator_free(iter);

	/* update the file size/mtime in the summary */
	if (res == 0) {
		if (stat(((CamelLocalSummary *)s)->folder_path, &st) == 0) {
			CamelMBOXView *root = (CamelMBOXView *)s->root_view->view;

			root->folder_size = st.st_size;
			root->time = st.st_mtime;
			camel_view_changed((CamelView *)root);
		}
	}

	camel_operation_end(NULL);

	return res;
}

static int
mbox_summary_check(CamelLocalSummary *cls, CamelChangeInfo *changes, CamelException *ex)
{
	CamelFolderSummary *s = (CamelFolderSummary *)cls;
	struct stat st;
	int res = 0;
	CamelMBOXView *root = (CamelMBOXView *)s->root_view->view;

	d(printf("Checking summary\n"));

	/* check if the summary is up-to-date */
	if (stat(cls->folder_path, &st) == -1) {
		camel_folder_summary_clear(s);
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot check folder: %s: %s"),
				      cls->folder_path, g_strerror (errno));
		return -1;
	}

	printf("size %d, summary size %d, time %d, summary time %d\n",
	       (int)st.st_size, (int)root->folder_size, (int)st.st_mtime, (int)root->time);

	if (st.st_size != root->folder_size || st.st_mtime != root->time) {
		if (root->folder_size < st.st_size) {
			/* this will automatically rescan from 0 if there is a problem */
			d(printf("folder grew, attempting to check from %d\n", (int)root->folder_size));
			res = summary_update(s, root->folder_size, changes, ex);
		} else {
			d(printf("folder shrank!  checking from start\n"));
			res = summary_update(s, 0, changes, ex);
		}
	} else {
		d(printf("Folder unchanged, do nothing\n"));
	}

	return 0;
}

/* perform a full sync */
static int
mbox_summary_sync_full(CamelMboxSummary *mbs, gboolean expunge, CamelChangeInfo *changeinfo, CamelException *ex)
{
	CamelLocalSummary *cls = (CamelLocalSummary *)mbs;
	int fd = -1, fdout = -1;
	char *tmpname = NULL, *oldname = NULL;
	guint32 flags = (expunge?1:0);
	struct stat st;

	d(printf("performing full summary/sync\n"));

	camel_operation_start(NULL, _("Storing folder"));

	fd = open(cls->folder_path, O_RDONLY);
	if (fd == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not open file: %s: %s"),
				      cls->folder_path, g_strerror (errno));
		camel_operation_end(NULL);
		return -1;
	}

	/* We need to use names that can't be used!  Uh, who knows ... */

	tmpname = g_alloca(strlen(cls->folder_path) + 5);
	sprintf(tmpname, "%s.tmp", cls->folder_path);

	oldname = g_alloca(strlen(cls->folder_path) + 5);
	sprintf(oldname, "%s.old", cls->folder_path);

	if (stat(tmpname, &st) == 0
	    || stat(oldname, &st) == 0) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Found temporary mailbox `%s' left over from a failed expunge operation.  "
				       "Refusing to expunge mailbox until you repair it manually."),
				     stat(tmpname, &st) == 0?tmpname:oldname);
		return -1;
	}

	d(printf("Writing tmp file to %s\n", tmpname));
	fdout = open(tmpname, O_WRONLY|O_CREAT|O_TRUNC, 0600);
	if (fdout == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot open temporary mailbox: %s"),
				      g_strerror (errno));
		goto error;
	}

	if (camel_mbox_summary_sync_mbox((CamelMboxSummary *)cls, flags, changeinfo, fd, fdout, ex) == -1)
		goto error;

	d(printf("Closing folders\n"));

	if (close(fd) == -1) {
		g_warning("Cannot close source folder: %s", strerror (errno));
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not close source folder %s: %s"),
				      cls->folder_path, g_strerror (errno));
		fd = -1;
		close(fdout);
		goto error;
	}

	if (close(fdout) == -1) {
		g_warning("Cannot close tmp folder: %s", strerror (errno));
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not close temp folder: %s"),
				      g_strerror (errno));
		fdout = -1;
		goto error;
	}

	/* We use link/unlink rather than rename, so that anyone who is still
	   reading the mailbox doesn't suddenly get the content ripped from
	   under them.  It isn't quite as atomic, so the user may have to clean
	   up after us, if their box gets hit by lightning in a thunderstorm
	   on the forth of july under a stampede of elephants ...

	   There is however a race in which the old name doesn't exist
	   for a short period of time, which could cause problems
	   with procmail and other things - but we dont support that for
	   mbox, so tough! */

	if (link(cls->folder_path, oldname) == -1
	    || unlink(cls->folder_path) == -1
	    || link(tmpname, cls->folder_path) == -1) {
		/* TODO: we should probably check folder_path wasn't created 'anyway',
		   on nfs ... */
		g_warning("Cannot rename folder: %s", strerror (errno));
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not rename folder: %s"),
				      g_strerror (errno));
		tmpname = NULL;
		goto error;
	} else {
		unlink(oldname);
		unlink(tmpname);
	}

	camel_operation_end(NULL);
		
	return 0;
 error:
	if (fd != -1)
		close(fd);
	
	if (fdout != -1)
		close(fdout);
	
	if (tmpname)
		unlink(tmpname);

	camel_operation_end(NULL);

	return -1;
}

/* perform a quick sync - only system flags have changed */
static int
mbox_summary_sync_quick(CamelMboxSummary *mbs, gboolean expunge, CamelChangeInfo *changeinfo, CamelException *ex)
{
#if 0
	CamelLocalSummary *cls = (CamelLocalSummary *)mbs;
	CamelFolderSummary *s = (CamelFolderSummary *)mbs;
	CamelMimeParser *mp = NULL;
	int i, count;
	CamelMboxMessageInfo *info = NULL;
	int fd = -1, pfd;
	char *xevnew, *xevtmp;
	const char *xev;
	int len;
	off_t lastpos;

	d(printf("Performing quick summary sync\n"));
#endif
	return 0;
}

static int
mbox_summary_sync(CamelLocalSummary *cls, gboolean expunge, CamelChangeInfo *changeinfo, CamelException *ex)
{
	struct stat st;
	CamelMboxSummary *mbs = (CamelMboxSummary *)cls;
	CamelMBOXView *root = (CamelMBOXView *)CFS(cls)->root_view->view;
	int res;

	/* We auto-sync all the time, so we only really care about expunging,
	   or if we're storing pine headers.  We should probably save xstatus in
	   the summary header, but ... who cares really */

	/* We should probably find out if we pending/running an auto-sync ...
	   but ... that should just sort itself out automatically. */

	if (expunge
#ifdef STATUS_PINE
	    || mbs->xstatus_changed
#endif
		) {
		res = ((CamelMboxSummaryClass *)((CamelObject *)cls)->klass)->sync_full(mbs, expunge, changeinfo, ex);
		if (res == 0) {
#ifdef STATUS_PINE
			mbs->xstatus_changed = 0;
#endif
			if (stat(cls->folder_path, &st) == 0
			    && (root->folder_size != st.st_size || root->time != st.st_mtime)) {
				root->time = st.st_mtime;
				root->folder_size = st.st_size;
				camel_view_changed((CamelView *)root);
			}
		}
	}

	return res;
}

static void
mbox_build_headers(CamelMboxSummary *mbs, GString *out, struct _camel_header_raw *header, CamelMboxMessageInfo *info)
{
	char *xev;
#ifdef STATUS_PINE
	char status[8];
#endif

	while (header) {
		if (strcmp(header->name, "X-Evolution") != 0
#ifdef STATUS_PINE
		    && (!mbs->xstatus
			|| (strcmp(header->name, "Status") != 0
			    && strcmp(header->name, "X-Status") != 0))
#endif
			) {
			g_string_append(out, header->name);
			g_string_append_c(out, ':');
			g_string_append(out, header->value);
			g_string_append_c(out, '\n');
		}
		header = header->next;
	}

#ifdef STATUS_PINE
	if (mbs->xstatus) {
		camel_mbox_summary_encode_status(((CamelMessageInfoBase *)info)->flags & CAMEL_MBOX_STATUS_STATUS, status);
		g_string_append(out, "Status: ");
		g_string_append(out, status);
		g_string_append_c(out, '\n');

		camel_mbox_summary_encode_status(((CamelMessageInfoBase *)info)->flags & CAMEL_MBOX_STATUS_XSTATUS, status);
		g_string_append(out, "X-Status: ");
		g_string_append(out, status);
		g_string_append_c(out, '\n');
	}
#endif

	g_string_append(out, "X-Evolution: ");
	xev = camel_mbox_summary_encode_xev(((CamelMessageInfoBase *)info)->uid, ((CamelMessageInfoBase *)info)->flags);
	g_string_append(out, xev);
	g_free(xev);
	g_string_append(out, "\n\n");
}

int
camel_mbox_summary_sync_mbox(CamelMboxSummary *cls, guint32 inflags, CamelChangeInfo *changes, int fd, int fdout, CamelException *ex)
{
	CamelMboxSummary *mbs = (CamelMboxSummary *)cls;
	CamelFolderSummary *s = (CamelFolderSummary *)mbs;
	CamelMimeParser *mp = NULL;
	int count = 0, res = -1;
	CamelIterator *iter;
	const CamelMessageInfo *iterinfo;
	char *buffer;
	size_t bufferlen;
	ssize_t len;
	GCompareDataFunc uid_cmp = CFS_CLASS(s)->uid_cmp;
	GString *headers;
	guint32 lastuid = 0, thisuid;

	/* The new improved 'sync and expunge an mbox' code.

	We make no assumptions about what we have anymore.  Or about
	what is on the disk.  We never fail because of mismatches.
	If we return successful, then the mailbox will be in a guaranteed
	increasing uid order, and all messages will have a valid
	X-Evolution header.

	Messages may move location - we just update the frompos
	Out of order - we renumber them
	New messages - we add them
	*/

	printf("Syncing mailbox with expunge!?\n");

	/* need to dup this because the mime-parser owns the fd after we give it to it */
	fd = dup(fd);
	if (fd == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not store folder: %s"),
				      g_strerror (errno));
		return -1;
	}

	headers = g_string_new("");

	mp = camel_mime_parser_new();
	camel_mime_parser_scan_from(mp, TRUE);
	camel_mime_parser_scan_pre_from(mp, TRUE);
	camel_mime_parser_init_with_fd(mp, fd);

	/* FIXME: handle exceptions on the iterator? */

	iter = camel_folder_summary_search(s, NULL, NULL, NULL, ex);
	iterinfo = camel_iterator_next(iter, NULL);
	while (camel_mime_parser_step(mp, &buffer, &len) != CAMEL_MIME_PARSER_STATE_EOF) {
		CamelMboxMessageInfo *info;
		struct _camel_header_raw *h;
		const char *xev;
		char *uid;
		guint32 flags;

		if (s->root_view->view->total_count)
			camel_operation_progress(NULL, (++count)*100 / s->root_view->view->total_count);

		/* The from line is only valid in the FROM state, so grab it here,
		   we use it later if this isn't a deleted message.  I */
		g_string_truncate(headers, 0);
		if (camel_mime_parser_state(mp) == CAMEL_MIME_PARSER_STATE_FROM)
			g_string_append(headers, camel_mime_parser_from_line(mp));

		/* Read next message */
		if (camel_mime_parser_state(mp) != CAMEL_MIME_PARSER_STATE_FROM
		    || camel_mime_parser_step(mp, &buffer, &len) == CAMEL_MIME_PARSER_STATE_FROM_END) {
			g_warning("Expected a From line here, didn't get it");
			camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
					     _("Unreadable mailbox"));
			goto fail;
		}

		h = camel_mime_parser_headers_raw(mp);
		xev = camel_header_raw_find(&h, "x-evolution", NULL);
		if (xev && (uid = camel_mbox_summary_decode_xev(xev, &flags))) {
			thisuid = strtoul(uid, NULL, 10);
			camel_mbox_summary_last_uid((CamelMboxSummary *)s, thisuid);

			if (thisuid < lastuid) {
				printf("the uid is out of order, renumbering it\n");
				/* Ok, so the mailbox violates the strict ordering requirements.
				   Some other client must have updated the folder, and re-ordered
				   messages in it.  Lets re-number anything we need to */
				goto add_message;
			} else {
				lastuid = thisuid;
			}

			while (iterinfo && uid_cmp(camel_message_info_uid(iterinfo), uid, s) < 0) {
				printf("Message %s vanished\n", iterinfo->uid);
				camel_change_info_remove(changes, iterinfo);
				camel_folder_summary_remove(s, (CamelMessageInfo *)iterinfo);
				iterinfo = camel_iterator_next(iter, NULL);
			}

			if (iterinfo && uid_cmp(iterinfo->uid, uid, s) == 0) {
				info = (CamelMboxMessageInfo *)iterinfo;
				camel_message_info_ref(info);
				iterinfo = camel_iterator_next(iter, NULL);
				goto have_message;
			}
		} else {
			printf("how silly, a new message with no xev header got added by someone\n");
		}
	add_message:
		printf("how odd, found an unkown message at %ld\n", camel_mime_parser_tell_start_from(mp));

		/* A message we didn't know about, just create a new xev for it and write it out */
		lastuid = camel_mbox_summary_next_uid((CamelMboxSummary *)s);
		uid = g_strdup_printf("%d", lastuid);

		info = camel_message_info_new_from_header(s, h);
		((CamelMboxMessageInfo *)info)->frompos = camel_mime_parser_tell_start_from(mp);
		((CamelMessageInfoBase *)info)->uid = uid;

		camel_folder_summary_add(s, (CamelMessageInfo *)info);
		camel_change_info_add(changes, (CamelMessageInfo *)info);
		// FIXME: set recent flag for recent?
		if (xev == NULL)
			camel_change_info_recent(changes, (CamelMessageInfo *)info);

	have_message:
		if ((inflags & 1) && ((CamelMessageInfoBase *)info)->flags & CAMEL_MESSAGE_DELETED) {
			printf("%p: expunging deleted message %s\n", info, camel_message_info_uid(info));
			camel_change_info_remove(changes, (CamelMessageInfo *)info);
			camel_folder_summary_remove(s, (CamelMessageInfo *)info);
			camel_mime_parser_drop_step(mp);
			camel_mime_parser_drop_step(mp);
			while (camel_mime_parser_step(mp, &buffer, &bufferlen) == CAMEL_MIME_PARSER_STATE_PRE_FROM)
				;
			camel_mime_parser_unstep(mp);
		} else {
			off_t frompos;
			size_t size;

			frompos = lseek(fdout, 0, SEEK_CUR);
			printf("copying message from %ld to %ld\n", camel_mime_parser_tell_start_from(mp), frompos);

			/* Copy From_ + headers to destination, fixing status markers */
			mbox_build_headers(mbs, headers, h, info);
			do {
				len = write(fdout, headers->str, headers->len);
			} while (len == -1 && errno == EINTR);

			/* Copy the whole message content */
			camel_mime_parser_drop_step(mp);
			camel_mime_parser_drop_step(mp);
			while (camel_mime_parser_step(mp, &buffer, &bufferlen) == CAMEL_MIME_PARSER_STATE_PRE_FROM) {
				do {
					len = write(fdout, buffer, bufferlen);
				} while (len == -1 && errno == EINTR);

				if (len == -1)
					goto io_error;
			}

			do {
				len = write(fdout, "\n", 1);
			} while (len == -1 && errno == EINTR);

			if (len == -1)
				goto io_error;

			size = lseek(fdout, 0, SEEK_CUR) - frompos;

			/* Update offsets/size if they've changed & force the summary to save it */
			if (frompos != info->frompos || ((CamelMessageInfoBase *)info)->size != size) {
				info->frompos = frompos;
				((CamelMessageInfoBase *)info)->size = size;
				camel_message_info_set_flags((CamelMessageInfo *)info, CAMEL_MESSAGE_FOLDER_FLAGGED, ~0);
			}

			camel_mime_parser_unstep(mp);
		}

		camel_message_info_free(info);
	}

	/* Anything else still in the summary isn't really there anymore, so discard it.
	   Also happens if we renumbered messages */
	while (iterinfo) {
		camel_change_info_remove(changes, iterinfo);
		camel_folder_summary_remove(s, (CamelMessageInfo *)iterinfo);
		iterinfo = camel_iterator_next(iter, NULL);
	}
	res = 0;
fail:
	camel_iterator_free(iter);
	camel_object_unref(mp);
	g_string_free(headers, TRUE);
	
	return res;

io_error:
	/* FIXME: find another matching string */
	camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
			     _("System failure writing new mailbox: %s"), g_strerror(errno));
	goto fail;
}

static void
camel_mbox_summary_init(CamelMboxSummary *obj)
{
	struct _CamelFolderSummary *s = (CamelFolderSummary *)obj;

	s = s;
}

static void
camel_mbox_summary_finalise(CamelObject *obj)
{
	/*CamelMboxSummary *mbs = CAMEL_MBOX_SUMMARY(obj);*/
}

static void
camel_mbox_summary_class_init(CamelMboxSummaryClass *klass)
{
	CamelFolderSummaryClass *sklass = (CamelFolderSummaryClass *)klass;
	CamelLocalSummaryClass *lklass = (CamelLocalSummaryClass *)klass;
	
	camel_mbox_summary_parent = (CamelLocalSummaryClass *)camel_type_get_global_classfuncs(camel_local_summary_get_type());

	((CamelFolderSummaryClass *)klass)->messageinfo_sizeof = sizeof(CamelMboxMessageInfo);

	((CamelFolderSummaryDiskClass *)klass)->encode = message_info_encode;
	((CamelFolderSummaryDiskClass *)klass)->decode = message_info_decode;

	((CamelFolderSummaryDiskClass *)klass)->sync = mbox_sync_changes;

	sklass->add = mbox_add;

	sklass->message_info_new_from_header  = message_info_new_from_header;
	sklass->message_info_new_from_parser = message_info_new_from_parser;

	lklass->check = mbox_summary_check;
	lklass->sync = mbox_summary_sync;

	klass->sync_quick = mbox_summary_sync_quick;
	klass->sync_full = mbox_summary_sync_full;
}

CamelType
camel_mbox_summary_get_type(void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register(camel_local_summary_get_type(), "CamelMboxSummary",
					   sizeof (CamelMboxSummary),
					   sizeof (CamelMboxSummaryClass),
					   (CamelObjectClassInitFunc) camel_mbox_summary_class_init,
					   NULL,
					   (CamelObjectInitFunc) camel_mbox_summary_init,
					   (CamelObjectFinalizeFunc) camel_mbox_summary_finalise);
	}
	
	return type;
}
