/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Authors:
 *   Dan Winship <danw@ximian.com>
 *   Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 2002 Ximian, Inc. (www.ximian.com)
 * Copyright (C) 2005 Novell, Inc. (www.novell.com)
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

#include <sys/time.h>
#include <errno.h>

/*
  Having a pop summary and a pop store and a pop folder ... is
  really pointless.  Unless we wanted it to behave
  like IMAP, which we really dont.

  We could possibly use another class of object, like CamelSpool,
  which along with CamelStore and CamelTransport, defines
  the remote mail environment.  But I suppose this will
  have to do for now.
*/

#include "camel-pop3-folder.h"
#include "camel-pop3-store.h"
#include "camel-pop3-summary.h"
#include "camel-exception.h"
#include "camel-stream-mem.h"
#include "camel-stream-filter.h"
#include "camel-mime-message.h"
#include "camel-operation.h"
#include "camel-data-cache.h"
#include "camel-i18n.h"

#include <libedataserver/md5-utils.h>

#include <stdlib.h>
#include <string.h>

#define d(x) 

/* how many messages to pre-fetch */
#define POP3_PREFETCH (5)

#define CF_CLASS(o) ((CamelFolderClass *)((CamelObject *)x)->klass)
#define CFS_CLASS(x) ((CamelFolderSummaryClass *)((CamelObject *)x)->klass)

struct _fetch_info {
	struct _fetch_info *next;
	struct _fetch_info *prev;

	CamelPOP3Folder *folder;

	int refcount;

	guint32 id;		/* list info */
	guint32 size;
	char *uid;

	int index;		/* for progress reporting, may be percentage if known */

	CamelPOP3Command *cmd;
	CamelStream *stream;
	CamelException ex;
};

static CamelFolderClass *parent_class;

CamelFolder *
camel_pop3_folder_new (CamelStore *parent, CamelException *ex)
{
	CamelFolder *folder;

	d(printf("opening pop3 INBOX folder\n"));
	
	folder = CAMEL_FOLDER (camel_object_new (CAMEL_POP3_FOLDER_TYPE));
	camel_folder_construct (folder, parent, "inbox", "inbox");
	
	/* Unlike other folders, for pop3 we want to force a
	   refresh at startup just to make sure we get everything we can */
	camel_folder_refresh_info (folder, ex);/* mt-ok */
	if (camel_exception_is_set (ex)) {
		camel_object_unref (CAMEL_OBJECT (folder));
		folder = NULL;
	}
	
	return folder;
}

static struct _fetch_info *
fetch_find(CamelPOP3Folder *pop3_folder, const char *uid)
{
	struct _fetch_info *fi;

	for (fi= (struct _fetch_info *)pop3_folder->fetches.head;fi->next;fi=fi->next)
		if (!strcmp(fi->uid, uid))
			return fi;

	return NULL;
}

static void
fetch_free(struct _fetch_info *fi)
{
	fi->refcount--;
	if (fi->refcount == 0) {
		e_dlist_remove((EDListNode *)fi);
		if (fi->stream)
			camel_data_cache_abort(((CamelPOP3Store *)fi->folder->folder.parent_store)->cache, fi->stream);
		camel_exception_clear(&fi->ex);
		g_free(fi->uid);
		g_free(fi);
	}
}

static struct _fetch_info *
fetch_alloc(CamelPOP3Folder *pop3_folder, EDList *list)
{
	struct _fetch_info *fi;

	fi = g_malloc0(sizeof(*fi));
	fi->folder = pop3_folder;
	fi->refcount = 1;

	e_dlist_addtail(list, (EDListNode *)fi);

	return fi;
}

static void
set_system_ex(CamelException *ex, const char *what)
{
	if (errno == EINTR)
		camel_exception_setv(ex, CAMEL_EXCEPTION_USER_CANCEL, _("Cancelled."));
	else
		camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, what, g_strerror(errno));
}

/* writes a stream to a cache stream/cleaning up */
static void
pop3_tocache(struct _fetch_info *fi, CamelStream *stream)
{
	char buffer[2048];
	int w = 0, n;

	while ((n = camel_stream_read(stream, buffer, sizeof(buffer))) > 0) {
		n = camel_stream_write(fi->stream, buffer, n);
		if (n == -1)
			break;

		if (fi->size !=0 ) {
			w += n;
			if (w > fi->size)
				w = fi->size;
			camel_operation_progress(NULL, (w * 100) / fi->size);
		}
	}

	if (n == -1) {
		if (errno == EINTR)
			camel_exception_setv(&fi->ex, CAMEL_EXCEPTION_USER_CANCEL, _("Cancelled."));
		else
			camel_exception_setv(&fi->ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot get message %s: %s"), fi->uid, g_strerror(errno));
		camel_data_cache_abort(((CamelPOP3Store *)fi->folder->folder.parent_store)->cache, fi->stream);
	} else {
		camel_data_cache_commit(((CamelPOP3Store *)fi->folder->folder.parent_store)->cache, fi->stream, &fi->ex);
	}
}

/* writes data in a fetch_info to a cache stream/committing, etc */
static void
cmd_tocache(CamelPOP3Engine *pe, CamelPOP3Stream *stream, void *data)
{
	struct _fetch_info *fi = data;

	pop3_tocache(fi, (CamelStream *)stream);

	fi->folder->prefetch--;
	fi->stream = NULL;
}

/* create a uid from md5 of 'top' output. */
static void
cmd_builduid(CamelPOP3Engine *pe, CamelPOP3Stream *stream, void *data)
{
	struct _fetch_info *fi = data;
	MD5Context md5;
	unsigned char digest[16];
	struct _camel_header_raw *h;
	CamelMimeParser *mp;
	char *uid;
	struct timeval tv;

	/* FIXME: We should create a summary entry here too, so we dont need to re-top later? */

	camel_operation_progress_count(NULL, fi->index * 100 / fi->folder->uids->len);

	md5_init(&md5);
	mp = camel_mime_parser_new();
	camel_mime_parser_init_with_stream(mp, (CamelStream *)stream);
	switch (camel_mime_parser_step(mp, NULL, NULL)) {
	case CAMEL_MIME_PARSER_STATE_HEADER:
	case CAMEL_MIME_PARSER_STATE_MESSAGE:
	case CAMEL_MIME_PARSER_STATE_MULTIPART:
		h = camel_mime_parser_headers_raw(mp);
		while (h) {
			if (strcasecmp(h->name, "status") != 0
			    && strcasecmp(h->name, "x-status") != 0) {
				md5_update(&md5, h->name, strlen(h->name));
				md5_update(&md5, h->value, strlen(h->value));
			}
			h = h->next;
		}
	default:
		break;
	}
	camel_object_unref(mp);
	md5_final(&md5, digest);
	uid = camel_base64_encode_simple(digest, 16);
	gettimeofday(&tv, NULL);
	fi->uid = g_strdup_printf("%s,%08x.%08x", uid, (unsigned int)tv.tv_sec, (unsigned int)tv.tv_usec);
	g_free(uid);

	d(printf("building uid for id '%d' = '%s'\n", fi->id, fi->uid));
}

/* This creates a messageinfo either from TOP or RETR.  RETR results are
   written to the cache first */
static void
cmd_buildinfo(CamelPOP3Engine *pe, CamelPOP3Stream *stream, void *data)
{
	struct _fetch_info *fi = data;
	CamelMimeParser *mp;
	CamelMessageInfo *mi;

	camel_operation_progress(NULL, fi->index);

	if (fi->stream) {
		CamelStream *cstream;

		pop3_tocache(fi, (CamelStream *)stream);
		if (camel_exception_is_set(&fi->ex))
			return;

		cstream = camel_data_cache_get(((CamelPOP3Store *)fi->folder->folder.parent_store)->cache, "cache", fi->uid, NULL, &fi->ex);
		if (!cstream)
			return;
		mp = camel_mime_parser_new();
		camel_mime_parser_init_with_stream(mp, cstream);
		camel_object_unref(cstream);
	} else {
		mp = camel_mime_parser_new();
		camel_mime_parser_init_with_stream(mp, (CamelStream *)stream);
	}

	mi = camel_message_info_new_from_parser(fi->folder->folder.summary, mp);
	camel_object_unref(mp);

	if (mi) {
		((CamelPOP3MessageInfo *)mi)->id = fi->id;
		((CamelMessageInfoBase *)mi)->size = fi->size;
		camel_folder_summary_add(fi->folder->folder.summary, mi);
	} else {
		set_system_ex(&fi->ex, _("Cannot get POP summary: %s"));
	}
}

static void
cmd_list(CamelPOP3Engine *pe, CamelPOP3Stream *stream, void *data)
{
	int ret;
	unsigned int len, id, size, count = 0;
	unsigned char *line;
	CamelPOP3Folder *folder = data;
	CamelPOP3Store *pop3_store = (CamelPOP3Store *)folder->folder.parent_store;
	struct _fetch_info *fi;

	do {
		ret = camel_pop3_stream_line(stream, &line, &len);
		if (ret>=0) {
			if (sscanf(line, "%u %u", &id, &size) == 2) {
				fetch_alloc(folder, &folder->lists);
				fi->size = size;
				fi->id = id;
				fi->index = count++;
				if ((pop3_store->engine->capa & CAMEL_POP3_CAP_UIDL) == 0)
					fi->cmd = camel_pop3_engine_command_new(pe, CAMEL_POP3_COMMAND_MULTI, cmd_builduid, fi, "TOP %u 0\r\n", id);
				g_ptr_array_add(folder->uids, fi);
				g_hash_table_insert(folder->uids_id, GINT_TO_POINTER(id), fi);
			}
		}
	} while (ret>0);
}

static void
cmd_uidl(CamelPOP3Engine *pe, CamelPOP3Stream *stream, void *data)
{
	int ret;
	unsigned int len;
	unsigned char *line;
	char uid[1025];
	unsigned int id;
	struct _fetch_info *fi;
	CamelPOP3Folder *folder = data;
	struct timeval tv;
	
	do {
		ret = camel_pop3_stream_line(stream, &line, &len);
		if (ret>=0) {
			if (strlen(line) > 1024)
				line[1024] = 0;
			if (sscanf(line, "%u %s", &id, uid) == 2) {
				fi = g_hash_table_lookup(folder->uids_id, GINT_TO_POINTER(id));
				if (fi) {
					camel_operation_progress(NULL, (fi->index+1) * 100 / folder->uids->len);
					gettimeofday(&tv, NULL);
					fi->uid = g_strdup_printf("%s,%08x.%08x", uid, (unsigned int)tv.tv_sec, (unsigned int)tv.tv_usec);
				} else {
					g_warning("ID %u (uid: %s) not in previous LIST output", id, uid);
				}
			}
		}
	} while (ret>0);
}

static int
pop3_info_cmp(const void *ap, const void *bp, void *s)
{
	return CFS_CLASS(s)->uid_cmp(((struct _fetch_info **)ap)[0], ((struct _fetch_info **)bp)[0], s);
}

static void 
pop3_refresh_info (CamelFolder *folder, CamelException *ex)
{
	CamelPOP3Store *pop3_store = CAMEL_POP3_STORE (folder->parent_store);
	CamelPOP3Folder *pop3_folder = (CamelPOP3Folder *) folder;
	CamelPOP3Command *pcl, *pcu = NULL;
	GCompareDataFunc uid_cmp = CFS_CLASS(folder->summary)->uid_cmp;
	CamelMessageIterator *iter;
	const CamelMessageInfo *iterinfo;
	CamelException x = { 0 };
	GPtrArray *fetches;
	int i;

	camel_operation_start (NULL, _("Retrieving POP summary"));

	LOCK_ENGINE(pop3_store);

	/* only used during setup */
	pop3_folder->uids = g_ptr_array_new ();
	pop3_folder->uids_id = g_hash_table_new(NULL, NULL);

	pcl = camel_pop3_engine_command_new(pop3_store->engine, CAMEL_POP3_COMMAND_MULTI, cmd_list, folder, "LIST\r\n");
	if (pop3_store->engine->capa & CAMEL_POP3_CAP_UIDL)
		pcu = camel_pop3_engine_command_new(pop3_store->engine, CAMEL_POP3_COMMAND_MULTI, cmd_uidl, folder, "UIDL\r\n");

	while ((i = camel_pop3_engine_iterate(pop3_store->engine, NULL)) > 0);
	if (i == -1)
		set_system_ex(ex, _("Cannot get POP summary: %s"));

	/* TODO: check every id has a uid & commands returned OK too? */
	
	camel_pop3_engine_command_free(pop3_store->engine, pcl);	
	if (pop3_store->engine->capa & CAMEL_POP3_CAP_UIDL) {
		camel_pop3_engine_command_free(pop3_store->engine, pcu);
	} else {
		for (i=0;i<pop3_folder->uids->len;i++) {
			struct _fetch_info *fi = pop3_folder->uids->pdata[i];

			if (fi->cmd) {
				camel_pop3_engine_command_free(pop3_store->engine, fi->cmd);
				fi->cmd = NULL;
			}
			camel_exception_clear(&fi->ex);
		}
	}

	if (camel_exception_is_set(ex))
		goto fail;

	/* Stage 2: Match the list against the summary */

	// FIXME: exceptions
	g_qsort_with_data(pop3_folder->uids->pdata, pop3_folder->uids->len, sizeof(pop3_folder->uids->pdata[0]), pop3_info_cmp, folder->summary);

	fetches = g_ptr_array_new();
	iter = camel_folder_summary_search(folder->summary, NULL, NULL, NULL, NULL);
	iterinfo = camel_message_iterator_next(iter, NULL);
	for (i=0;i<pop3_folder->uids->len;i++) {
		struct _fetch_info *fi = pop3_folder->uids->pdata[i];

		if (fi->uid == NULL) {
			/* This points to an error happening elsewhere ... *shrug* */
			g_free(fi);
			continue;
		}

		while (iterinfo && uid_cmp(iterinfo->uid, fi->uid, folder->summary) < 0) {
			// FIXME: remove cache file?  summary should?
			camel_folder_summary_remove(folder->summary, (CamelMessageInfo *)iterinfo);
			iterinfo = camel_message_iterator_next(iter, NULL);
		}

		if (iterinfo && uid_cmp(iterinfo->uid, fi->uid, folder->summary) == 0) {
			if (((CamelPOP3MessageInfo *)iterinfo)->id != fi->id) {
				((CamelPOP3MessageInfo *)iterinfo)->id = fi->id;
				camel_message_info_changed((CamelMessageInfo *)iterinfo, TRUE);
			}
			iterinfo = camel_message_iterator_next(iter, NULL);
			fetch_free(fi);
		} else {
			g_ptr_array_add(fetches, fi);
		}
	}

	while (iterinfo) {
		// FIXME: remove cache file?  summary should?
		camel_folder_summary_remove(folder->summary, (CamelMessageInfo *)iterinfo);
		iterinfo = camel_message_iterator_next(iter, NULL);
	}
	camel_message_iterator_free(iter);

	g_hash_table_destroy(pop3_folder->uids_id);
	g_ptr_array_free(pop3_folder->uids, TRUE);

	/* Because TOP is relatively expensive for small messages, if the message
	   is small, we just download the whole message right now.  If by
	   some chance it is already in the cache, we just use that - we
	   should probably overwrite it? */

	/* Stage 3: Download the top of all the new messages to add to the summary */
	for (i=0;i<fetches->len;i++) {
		struct _fetch_info *fi = fetches->pdata[i];

		fi->index = i*100/fetches->len;
		if (fi->size < 5120) {
			CamelStream *stream, *rstream;

			stream = camel_data_cache_get(pop3_store->cache, "cache", fi->uid, &rstream, NULL);
			if (stream) {
				cmd_buildinfo(pop3_store->engine, (CamelPOP3Stream *)stream, fi);
				camel_object_unref(stream);
			} else if (rstream) {
				fi->stream = rstream;
				fi->cmd = camel_pop3_engine_command_new(pop3_store->engine, CAMEL_POP3_COMMAND_MULTI, cmd_buildinfo, fi, "RETR %u\r\n", fi->id);
			}
		} else {
			fi->cmd = camel_pop3_engine_command_new(pop3_store->engine, CAMEL_POP3_COMMAND_MULTI, cmd_buildinfo, fi, "TOP %u 0\r\n", fi->id);
		}
	}

	while ((i = camel_pop3_engine_iterate(pop3_store->engine, NULL)) > 0);
	if (i == -1)
		set_system_ex(ex, _("Cannot get POP summary: %s"));

	for (i=0;i<fetches->len;i++) {
		struct _fetch_info *fi = fetches->pdata[i];
		if (fi->cmd) {
			camel_pop3_engine_command_free(pop3_store->engine, fi->cmd);
			fi->cmd = NULL;
		}
		fetch_free(fi);
	}
	g_ptr_array_free(fetches, TRUE);

fail:
	UNLOCK_ENGINE(pop3_store);
	
	camel_operation_end (NULL);

	camel_folder_summary_disk_sync((CamelFolderSummaryDisk *)folder->summary, &x);
	if (!camel_exception_is_set(ex))
		camel_exception_xfer(ex, &x);

	return;
}

static void
pop3_sync(CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	CamelPOP3Store *pop3_store;
	int i;
	CamelPOP3Command *cmd;
	const CamelMessageInfo *iterinfo;
	CamelMessageIterator *iter;
	CamelException x = { 0 };
	GPtrArray *cmds;

	if (expunge && FALSE /* FIXME keep on server */) {
		camel_operation_start(NULL, _("Expunging deleted messages"));
		cmds = g_ptr_array_new();

		LOCK_ENGINE(pop3_store);

		iter = camel_folder_summary_search(folder->summary, NULL, NULL, NULL, NULL);
		while ((iterinfo = camel_message_iterator_next(iter, NULL))) {
			CamelPOP3MessageInfo *mi = (CamelPOP3MessageInfo *)iterinfo;

			if ((((CamelMessageInfoBase *)mi)->flags & CAMEL_MESSAGE_DELETED)
			    && (cmd = camel_pop3_engine_command_new(pop3_store->engine, 0, NULL, NULL, "DELE %u\r\n", mi->id)))
				g_ptr_array_add(cmds, cmd);
		}
		camel_message_iterator_free(iter);

		for (i=0;i<cmds->len;i++) {
			cmd = cmds->pdata[i];
			while (camel_pop3_engine_iterate(pop3_store->engine, cmd) > 0)
				;

#warning " 			/* FIXME: remove successfully deleted items from the summary. */"

			camel_pop3_engine_command_free(pop3_store->engine, cmd);
			camel_operation_progress(NULL, (i+1) * 100 / cmds->len);
		}

		LOCK_ENGINE(pop3_store);

		g_ptr_array_free(cmds, TRUE);
		camel_operation_end(NULL);

		camel_pop3_store_expunge(pop3_store, ex);
	}

	camel_folder_summary_disk_sync((CamelFolderSummaryDisk *)folder->summary, &x);
	if (!camel_exception_is_set(ex))
		camel_exception_xfer(ex, &x);
}

static CamelMimeMessage *
pop3_get_message (CamelFolder *folder, const char *uid, CamelException *ex)
{
	CamelMimeMessage *message = NULL;
	CamelPOP3Store *pop3_store = CAMEL_POP3_STORE (folder->parent_store);
	CamelPOP3Folder *pop3_folder = (CamelPOP3Folder *)folder;
	CamelPOP3MessageInfo *mi;
	int i;
	CamelStream *stream = NULL, *rstream;
	struct _fetch_info *fi;
	GCompareDataFunc uid_cmp = CFS_CLASS(folder->summary)->uid_cmp;

	/*
	  Blah.  Why do even the simple things have to be so bloody complicated!

	  First, if we are currently loading a message, we process the i/o in
	  this loop until it is done, handle exceptions, get the stream.

	  If not, then reserve the key and queue a job.  Because pipelineing
	  adds so much performance, we then iterate through the summary and
	  start fetching the following few messages as well - since
	  those are probably the ones we're going to get next.
	*/

	mi = camel_folder_summary_get(folder->summary, uid);
	if (mi == NULL) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				     _("Cannot get message: %s from folder %s\n  %s"),
				     uid, _("Inbox"), _("No such message"));
		return NULL;
	}

	LOCK_ENGINE(pop3_store);

	fi = fetch_find(pop3_folder, uid);
	if (fi) {
		fi->refcount++;
	} else if ((stream = camel_data_cache_get(pop3_store->cache, "cache", uid, &rstream, ex)) == NULL && rstream != NULL) {
		const CamelMessageInfo *iterinfo;

		fi = fetch_alloc(pop3_folder, &pop3_folder->fetches);
		fi->uid = g_strdup(uid);
		fi->size = ((CamelMessageInfoBase *)mi)->size;
		fi->folder = pop3_folder;
		fi->stream = rstream;
		fi->cmd = camel_pop3_engine_command_new(pop3_store->engine, CAMEL_POP3_COMMAND_MULTI, cmd_tocache, fi, "RETR %u\r\n", mi->id);

		if (pop3_folder->iter == NULL) {
			pop3_folder->iter = camel_folder_summary_search(folder->summary, NULL, NULL, NULL, NULL);
			pop3_folder->iterinfo = camel_message_iterator_next(pop3_folder->iter, NULL);
		}

		iterinfo = pop3_folder->iterinfo;
		while (iterinfo && uid_cmp(iterinfo->uid, uid, folder->summary) < 0)
			iterinfo = camel_message_iterator_next(pop3_folder->iter, NULL);

		while (iterinfo && pop3_folder->prefetch < POP3_PREFETCH) {
			if (fetch_find(pop3_folder, iterinfo->uid) == NULL) {
				stream = camel_data_cache_get(pop3_store->cache, "cache", iterinfo->uid, &rstream, NULL);
				if (stream) {
					camel_object_unref(stream);
				} else if (rstream) {
					struct _fetch_info *nfi;

					nfi = fetch_alloc(pop3_folder, &pop3_folder->fetches);
					pop3_folder->prefetch++;
					nfi->uid = g_strdup(iterinfo->uid);
					nfi->size = ((CamelMessageInfoBase *)iterinfo)->size;
					nfi->folder = pop3_folder;
					nfi->stream = rstream;
					nfi->cmd = camel_pop3_engine_command_new(pop3_store->engine, CAMEL_POP3_COMMAND_MULTI, cmd_tocache, nfi, "RETR %u\r\n",
										 ((CamelPOP3MessageInfo *)iterinfo)->id);
				}
			}
			iterinfo = camel_message_iterator_next(pop3_folder->iter, NULL);
		}

		pop3_folder->iterinfo = iterinfo;
		stream = NULL;
	}

	if (fi) {
		while ((i = camel_pop3_engine_iterate(pop3_store->engine, fi->cmd)) > 0)
			;

		if (camel_exception_is_set(&fi->ex))
			camel_exception_xfer(ex, &fi->ex);
		else
			stream = camel_data_cache_get(pop3_store->cache, "cache", uid, NULL, ex);

		fetch_free(fi);
	}

	UNLOCK_ENGINE(pop3_store);

	if (stream) {
		message = camel_mime_message_new();
		if (camel_data_wrapper_construct_from_stream((CamelDataWrapper *)message, stream) == -1) {
			if (errno == EINTR)
				camel_exception_setv(ex, CAMEL_EXCEPTION_USER_CANCEL, _("Cancelled."));
			else
				camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot get message %s: %s"), uid, g_strerror(errno));
			camel_object_unref(message);
			message = NULL;
		}
		camel_object_unref(stream);
	}

	camel_message_info_free(mi);

	return message;
}

static void
pop3_init(CamelPOP3Folder *pop3_folder)
{
	((CamelFolder *)pop3_folder)->summary = camel_pop3_summary_new((CamelFolder *)pop3_folder);
	e_dlist_init(&pop3_folder->fetches);
}

static void
pop3_finalize (CamelObject *object)
{
	CamelPOP3Folder *pop3_folder = CAMEL_POP3_FOLDER (object);
	CamelPOP3Store *pop3_store = (CamelPOP3Store *)((CamelFolder *)pop3_folder)->parent_store;
	struct _fetch_info *fi, *fn;

	/* Clean up any outstanding fetches we're now aborting */

	fi = (struct _fetch_info *)pop3_folder->fetches.head;
	fn = fi->next;
	while (fn) {
		if (fi->cmd) {
			while (camel_pop3_engine_iterate(pop3_store->engine, fi->cmd) > 0)
				;
			camel_pop3_engine_command_free(pop3_store->engine, fi->cmd);
		}
		fetch_free(fi);
		fi = fn;
		fn = fn->next;
	}
}

static void
camel_pop3_folder_class_init (CamelPOP3FolderClass *camel_pop3_folder_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS(camel_pop3_folder_class);
	
	parent_class = CAMEL_FOLDER_CLASS(camel_folder_get_type());
	
	camel_folder_class->refresh_info = pop3_refresh_info;
	camel_folder_class->sync = pop3_sync;
	camel_folder_class->get_message = pop3_get_message;
}

CamelType
camel_pop3_folder_get_type (void)
{
	static CamelType camel_pop3_folder_type = CAMEL_INVALID_TYPE;
	
	if (!camel_pop3_folder_type) {
		camel_pop3_folder_type = camel_type_register (CAMEL_FOLDER_TYPE, "CamelPOP3Folder",
							      sizeof (CamelPOP3Folder),
							      sizeof (CamelPOP3FolderClass),
							      (CamelObjectClassInitFunc) camel_pop3_folder_class_init,
							      NULL,
							      (CamelObjectInitFunc) pop3_init,
							      (CamelObjectFinalizeFunc) pop3_finalize);
	}
	
	return camel_pop3_folder_type;
}

