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

struct _list_info {
	guint32 id;
	guint32 size;
	int index;		/* for progress reporting */
	char *uid;
	int err;
	struct _CamelPOP3Command *cmd;
};

struct _fetch_info {
	struct _fetch_info *next;
	struct _fetch_info *prev;

	char *uid;
	int refcount;
	size_t size;
	CamelPOP3Folder *folder;
	CamelStream *stream;
	CamelPOP3Command *cmd;
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
fetch_free(CamelPOP3Folder *pop3_folder, struct _fetch_info *fi)
{
	fi->refcount--;
	if (fi->refcount == 0) {
		if (fi->stream)
			camel_data_cache_abort(((CamelPOP3Store *)pop3_folder->folder.parent_store)->cache, fi->stream);
		camel_exception_clear(&fi->ex);
		g_free(fi->uid);
		g_free(fi);
	}
}

/* create a uid from md5 of 'top' output. */
static void
cmd_builduid(CamelPOP3Engine *pe, CamelPOP3Stream *stream, void *data)
{
	struct _list_info *fi = data;
	MD5Context md5;
	unsigned char digest[16];
	struct _camel_header_raw *h;
	CamelMimeParser *mp;

	/* TODO; somehow work out the limit and use that for proper progress reporting
	   We need a pointer to the folder perhaps? */
	camel_operation_progress_count(NULL, fi->id);

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
	fi->uid = camel_base64_encode_simple(digest, 16);

	d(printf("building uid for id '%d' = '%s'\n", fi->id, fi->uid));
}

static void
cmd_list(CamelPOP3Engine *pe, CamelPOP3Stream *stream, void *data)
{
	int ret;
	unsigned int len, id, size;
	unsigned char *line;
	CamelFolder *folder = data;
	CamelPOP3Store *pop3_store = CAMEL_POP3_STORE (folder->parent_store);
	struct _list_info *fi;

	do {
		ret = camel_pop3_stream_line(stream, &line, &len);
		if (ret>=0) {
			if (sscanf(line, "%u %u", &id, &size) == 2) {
				fi = g_malloc0(sizeof(*fi));
				fi->size = size;
				fi->id = id;
				fi->index = ((CamelPOP3Folder *)folder)->uids->len;
				if ((pop3_store->engine->capa & CAMEL_POP3_CAP_UIDL) == 0)
					fi->cmd = camel_pop3_engine_command_new(pe, CAMEL_POP3_COMMAND_MULTI, cmd_builduid, fi, "TOP %u 0\r\n", id);
				g_ptr_array_add(((CamelPOP3Folder *)folder)->uids, fi);
				g_hash_table_insert(((CamelPOP3Folder *)folder)->uids_id, GINT_TO_POINTER(id), fi);
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
	struct _list_info *fi;
	CamelPOP3Folder *folder = data;
	
	do {
		ret = camel_pop3_stream_line(stream, &line, &len);
		if (ret>=0) {
			if (strlen(line) > 1024)
				line[1024] = 0;
			if (sscanf(line, "%u %s", &id, uid) == 2) {
				fi = g_hash_table_lookup(folder->uids_id, GINT_TO_POINTER(id));
				if (fi) {
					camel_operation_progress(NULL, (fi->index+1) * 100 / folder->uids->len);
					fi->uid = g_strdup(uid);
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
	return CFS_CLASS(s)->uid_cmp(((struct _list_info **)ap)[0], ((struct _list_info **)bp)[0], s);
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
	int i;

	camel_operation_start (NULL, _("Retrieving POP summary"));

	LOCK_ENGINE(pop3_store);

	/* only used during setup */
	pop3_folder->uids = g_ptr_array_new ();
	pop3_folder->uids_id = g_hash_table_new(NULL, NULL);

	pcl = camel_pop3_engine_command_new(pop3_store->engine, CAMEL_POP3_COMMAND_MULTI, cmd_list, folder, "LIST\r\n");
	if (pop3_store->engine->capa & CAMEL_POP3_CAP_UIDL)
		pcu = camel_pop3_engine_command_new(pop3_store->engine, CAMEL_POP3_COMMAND_MULTI, cmd_uidl, folder, "UIDL\r\n");
	while ((i = camel_pop3_engine_iterate(pop3_store->engine, NULL)) > 0)
		;

	if (i == -1) {
		if (errno == EINTR)
			camel_exception_setv(ex, CAMEL_EXCEPTION_USER_CANCEL, _("User cancelled"));
		else
			camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
					      _("Cannot get POP summary: %s"),
					      g_strerror (errno));
	}

	/* TODO: check every id has a uid & commands returned OK too? */
	
	camel_pop3_engine_command_free(pop3_store->engine, pcl);
	
	if (pop3_store->engine->capa & CAMEL_POP3_CAP_UIDL) {
		camel_pop3_engine_command_free(pop3_store->engine, pcu);
	} else {
		for (i=0;i<pop3_folder->uids->len;i++) {
			struct _list_info *fi = pop3_folder->uids->pdata[i];
			if (fi->cmd) {
				camel_pop3_engine_command_free(pop3_store->engine, fi->cmd);
				fi->cmd = NULL;
			}
		}
	}

	/* Now, update the summary to match ... */
	// FIXME: exceptions
	g_qsort_with_data(pop3_folder->uids->pdata, pop3_folder->uids->len, sizeof(pop3_folder->uids->pdata[0]), pop3_info_cmp, folder->summary);
	iter = camel_folder_summary_search(folder->summary, NULL, NULL, NULL, NULL);
	iterinfo = camel_message_iterator_next(iter, NULL);
	for (i=0;i<pop3_folder->uids->len;i++) {
		struct _list_info *fi = pop3_folder->uids->pdata[i];

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
		} else {
			CamelPOP3MessageInfo *mi = camel_message_info_new(folder->summary);

			mi->info.uid = g_strdup(fi->uid);
			mi->size = fi->size;
			mi->id = fi->id;
			camel_folder_summary_add(folder->summary, (CamelMessageInfo *)mi);
		}
		g_free(fi);
	}

	while (iterinfo) {
		// FIXME: remove cache file?  summary should?
		camel_folder_summary_remove(folder->summary, (CamelMessageInfo *)iterinfo);
		iterinfo = camel_message_iterator_next(iter, NULL);
	}
	camel_message_iterator_free(iter);

	g_hash_table_destroy(pop3_folder->uids_id);
	g_ptr_array_free(pop3_folder->uids, TRUE);

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

			if ((mi->flags & CAMEL_MESSAGE_DELETED)
			    && (cmd = camel_pop3_engine_command_new(pop3_store->engine, 0, NULL, NULL, "DELE %u\r\n", mi->id)))
				g_ptr_array_add(cmds, cmd);
		}
		camel_message_iterator_free(iter);

		for (i=0;i<cmds->len;i++) {
			cmd = cmds->pdata[i];
			while (camel_pop3_engine_iterate(pop3_store->engine, cmd) > 0)
				;
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

static void
cmd_tocache(CamelPOP3Engine *pe, CamelPOP3Stream *stream, void *data)
{
	struct _fetch_info *fi = data;
	char buffer[2048];
	int w = 0, n;

	while ((n = camel_stream_read((CamelStream *)stream, buffer, sizeof(buffer))) > 0) {
		n = camel_stream_write(fi->stream, buffer, n);
		if (n == -1)
			break;

		w += n;
		if (w > fi->size)
			w = fi->size;
		if (fi->size != 0)
			camel_operation_progress(NULL, (w * 100) / fi->size);
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

	fi->folder->prefetch--;
	fi->stream = NULL;
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

		fi = g_malloc0(sizeof(*fi));
		fi->refcount = 1;
		fi->uid = g_strdup(uid);
		fi->size = mi->size;
		fi->folder = pop3_folder;
		fi->stream = rstream;
		e_dlist_addtail(&pop3_folder->fetches, (EDListNode *)fi);
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
					struct _fetch_info *nfi = g_malloc0(sizeof(*nfi));

					pop3_folder->prefetch++;
					nfi->refcount = 1;
					nfi->uid = iterinfo->uid;
					nfi->size = ((CamelPOP3MessageInfo *)iterinfo)->size;
					nfi->folder = pop3_folder;
					nfi->stream = rstream;
					e_dlist_addtail(&pop3_folder->fetches, (EDListNode *)fi);
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

		fetch_free(pop3_folder, fi);
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

#if 0
	fi = g_hash_table_lookup(pop3_folder->uids_uid, uid);
	if (fi == NULL) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				      _("No message with uid %s"), uid);
		return NULL;
	}

	/* Sigh, most of the crap in this function is so that the cancel button
	   returns the proper exception code.  Sigh. */

	camel_operation_start_transient(NULL, _("Retrieving POP message %d"), fi->id);

	/* If we have an oustanding retrieve message running, wait for that to complete
	   & then retrieve from cache, otherwise, start a new one, and similar */

	if (fi->cmd != NULL) {
		while ((i = camel_pop3_engine_iterate(pop3_store->engine, fi->cmd)) > 0)
			;

		if (i == -1)
			fi->err = errno;

		/* getting error code? */
		ok = fi->cmd->state == CAMEL_POP3_COMMAND_DATA;
		camel_pop3_engine_command_free(pop3_store->engine, fi->cmd);
		fi->cmd = NULL;

		if (fi->err != 0) {
			if (fi->err == EINTR)
				camel_exception_setv(ex, CAMEL_EXCEPTION_USER_CANCEL, _("User cancelled"));
			else
				camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
						      _("Cannot get message %s: %s"),
						      uid, g_strerror (fi->err));
			goto fail;
		}
	}
	
	/* check to see if we have safely written flag set */
	if (pop3_store->cache == NULL
	    || (stream = camel_data_cache_get(pop3_store->cache, "cache", fi->uid, NULL)) == NULL
	    || camel_stream_read(stream, buffer, 1) != 1
	    || buffer[0] != '#') {

		/* Initiate retrieval, if disk backing fails, use a memory backing */
		if (pop3_store->cache == NULL
		    || (stream = camel_data_cache_add(pop3_store->cache, "cache", fi->uid, NULL)) == NULL)
			stream = camel_stream_mem_new();

		/* ref it, the cache storage routine unref's when done */
		camel_object_ref((CamelObject *)stream);
		fi->stream = stream;
		fi->err = EIO;
		pcr = camel_pop3_engine_command_new(pop3_store->engine, CAMEL_POP3_COMMAND_MULTI, cmd_tocache, fi, "RETR %u\r\n", fi->id);

		/* Also initiate retrieval of some of the following messages, assume we'll be receiving them */
		if (pop3_store->cache != NULL) {
			/* This should keep track of the last one retrieved, also how many are still
			   oustanding incase of random access on large folders */
			i = fi->index+1;
			last = MIN(i+10, pop3_folder->uids->len);
			for (;i<last;i++) {
				struct _list_info *pfi = pop3_folder->uids->pdata[i];
				
				if (pfi->uid && pfi->cmd == NULL) {
					pfi->stream = camel_data_cache_add(pop3_store->cache, "cache", pfi->uid, NULL);
					if (pfi->stream) {
						pfi->err = EIO;
						pfi->cmd = camel_pop3_engine_command_new(pop3_store->engine, CAMEL_POP3_COMMAND_MULTI,
											 cmd_tocache, pfi, "RETR %u\r\n", pfi->id);
					}
				}
			}
		}

		/* now wait for the first one to finish */
		while ((i = camel_pop3_engine_iterate(pop3_store->engine, pcr)) > 0)
			;

		if (i == -1)
			fi->err = errno;

		/* getting error code? */
		ok = pcr->state == CAMEL_POP3_COMMAND_DATA;
		camel_pop3_engine_command_free(pop3_store->engine, pcr);
		camel_stream_reset(stream);

		/* Check to see we have safely written flag set */
		if (fi->err != 0) {
			if (fi->err == EINTR)
				camel_exception_setv(ex, CAMEL_EXCEPTION_USER_CANCEL, _("User cancelled"));
			else
				camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
						      _("Cannot get message %s: %s"),
						      uid, g_strerror (fi->err));
			goto done;
		}

		if (camel_stream_read(stream, buffer, 1) != 1 || buffer[0] != '#') {
			camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
					     _("Cannot get message %s: %s"), uid, _("Unknown reason"));
			goto done;
		}
	}

	message = camel_mime_message_new ();
	if (camel_data_wrapper_construct_from_stream((CamelDataWrapper *)message, stream) == -1) {
		if (errno == EINTR)
			camel_exception_setv(ex, CAMEL_EXCEPTION_USER_CANCEL, _("User cancelled"));
		else
			camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
					      _("Cannot get message %s: %s"),
					      uid, g_strerror (errno));
		camel_object_unref((CamelObject *)message);
		message = NULL;
	}
done:
	camel_object_unref((CamelObject *)stream);
fail:
	camel_operation_end(NULL);

	return message;
#endif
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
		while (camel_pop3_engine_iterate(pop3_store->engine, fi->cmd) > 0)
			;
		camel_pop3_engine_command_free(pop3_store->engine, fi->cmd);
		fetch_free(pop3_folder, fi);
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

