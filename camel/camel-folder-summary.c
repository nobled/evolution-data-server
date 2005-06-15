/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2003 Ximian Inc.
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
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

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

#include <libedataserver/e-iconv.h>

#include "camel-folder-summary.h"

/* for change events, perhaps we should just do them ourselves */
#include "camel-folder.h"

#include <camel/camel-mime-message.h>
#include <camel/camel-multipart.h>
#include <camel/camel-stream-mem.h>

#include <camel/camel-string-utils.h>
#include <camel/camel-exception.h>
#include <camel/camel-folder-search.h>

#include "libedataserver/md5-utils.h"
#include "libedataserver/e-memory.h"

#include "camel-private.h"

static pthread_mutex_t info_lock = PTHREAD_MUTEX_INITIALIZER;

/* this lock is ONLY for the standalone messageinfo stuff */
#define GLOBAL_INFO_LOCK(i) pthread_mutex_lock(&info_lock)
#define GLOBAL_INFO_UNLOCK(i) pthread_mutex_unlock(&info_lock)

#define CFS_CLASS(x) ((CamelFolderSummaryClass *)((CamelObject *)x)->klass)

#define d(x)
#define io(x)			/* io debug */

#define CAMEL_FOLDER_SUMMARY_VERSION (13)

#define _PRIVATE(o) (((CamelFolderSummary *)(o))->priv)

static CamelObjectClass *camel_folder_summary_parent;

static int
add(CamelFolderSummary *s, void *o)
{
	CamelMessageInfo *mi = o;
	guint32 flags;
	CamelFolderView *v = s->root_view;

	/* FIXME: lock */
	v->total_count++;
	flags = camel_message_info_flags(mi);
	if ((flags & (CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_JUNK)) == 0)
		v->visible_count++;
	if ((flags & (CAMEL_MESSAGE_SEEN|CAMEL_MESSAGE_JUNK|CAMEL_MESSAGE_DELETED)) == 0) /* FIXME: flags right? */
		v->unread_count++;
	if (flags & CAMEL_MESSAGE_DELETED)
		v->deleted_count++;
	if (flags & CAMEL_MESSAGE_JUNK)
		v->junk_count++;

	return 0;
}

static int
add_array(CamelFolderSummary *s, GPtrArray *mis)
{
	int i;

	for (i=0;i<mis->len;i++)
		camel_folder_summary_add(s, mis->pdata[i]);

	/* FIXME: rc */
	return 0;
}

static int
cfs_remove(CamelFolderSummary *s, void *o)
{
	CamelMessageInfo *mi = o;
	guint32 flags;
	CamelFolderView *v = s->root_view;

	/* FIXME: lock */
	v->total_count--;
	flags = camel_message_info_flags(mi);
	if ((flags & (CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_JUNK)) == 0)
		v->visible_count--;
	if ((flags & (CAMEL_MESSAGE_SEEN)) == 0) /* FIXME: flags right? */
		v->unread_count--;
	if (flags & CAMEL_MESSAGE_DELETED)
		v->deleted_count--;
	if (flags & CAMEL_MESSAGE_JUNK)
		v->junk_count--;

	return 0;
}

static int
remove_array(CamelFolderSummary *s, GPtrArray *mis)
{
	int i, res = 0;

	for (i=0;i<mis->len;i++)
		if (camel_folder_summary_remove(s, mis->pdata[i]) != 0)
			res = -1;

	return res;
}

static void
cfs_clear(CamelFolderSummary *s)
{
	CamelFolderView *v = s->root_view;

	v->total_count = 0;
	v->visible_count = 0;
	v->unread_count = 0;
	v->deleted_count = 0;
	v->junk_count = 0;

	/* what about all views? */
}

static CamelFolderView *
cfs_view_create(CamelFolderSummary *s, const char *vid, const char *expr, CamelException *ex)
{
	CamelFolderView *view;

	if (vid == NULL)
		printf("Creating root view\n");
	else
		printf("Creating view '%s' (%s)\n", vid, expr);

	view = g_malloc0(CFS_CLASS(s)->view_sizeof);
	view->refcount = 1;
	view->vid = g_strdup(vid);
	view->expr = g_strdup(expr);
	/* Hmm, refcounting of this would create a loop, but ... */
	view->summary = s;

	if (expr) {
		view->iter = camel_folder_search_search(s->search, view->expr, NULL, ex);
		view->is_static = camel_folder_search_is_static(s->search, view->expr, NULL);
	} else {
		view->iter = NULL;
		view->is_static = 1;
	}

	return view;
}

static void
cfs_view_delete(CamelFolderSummary *s, CamelFolderView *view)
{
	/* nothing, already removed elsewhere */
}

static void
cfs_view_free(CamelFolderSummary *s, CamelFolderView *view)
{
	if (view->iter)
		camel_message_iterator_free((CamelMessageIterator *)view->iter);
	if (view->changes)
		camel_folder_change_info_free(view->changes);
	g_free(view->vid);
	g_free(view->expr);
	g_free(view);
}

static void *
get(CamelFolderSummary *s, const char *uid)
{
	return NULL;
}

static GPtrArray *
get_array(CamelFolderSummary *s, const GPtrArray *uids)
{
	GPtrArray *out = g_ptr_array_new();
	int i;

	for (i=0;i<uids->len;i++) {
		CamelMessageInfo *mi = camel_folder_summary_get(s, uids->pdata[i]);

		if (mi)
			g_ptr_array_add(out, mi);
	}

	return out;
}

static CamelMessageIterator *
search(CamelFolderSummary *s, const char *vid, const char *expr, CamelMessageIterator *subset, CamelException *ex)
{
	/* don't translate, this is for programmers only */
	camel_exception_setv(ex, 1, "Search not implemented");
	return NULL;
}

static CamelMessageInfo *
message_info_alloc(CamelFolderSummary *s)
{
	return g_malloc0(sizeof(CamelMessageInfoBase));
}

static CamelMessageInfo *
message_info_new_from_header(CamelFolderSummary *s, struct _camel_header_raw *h)
{
	CamelMessageInfoBase *mi;
	const char *received;
	guchar digest[16];
	struct _camel_header_references *refs, *irt, *scan;
	char *msgid;
	int count;
	char *subject, *from, *to, *cc, *mlist;
	CamelContentType *ct = NULL;
	const char *content, *charset = NULL;

	mi = (CamelMessageInfoBase *)camel_message_info_new(s);

	if ((content = camel_header_raw_find(&h, "Content-Type", NULL))
	     && (ct = camel_content_type_decode(content))
	     && (charset = camel_content_type_param(ct, "charset"))
	     && (g_ascii_strcasecmp(charset, "us-ascii") == 0))
		charset = NULL;
	
	charset = charset ? e_iconv_charset_name (charset) : NULL;
	
	subject = camel_message_info_format_string(h, "subject", charset);
	from = camel_message_info_format_address(h, "from", charset);
	to = camel_message_info_format_address(h, "to", charset);
	cc = camel_message_info_format_address(h, "cc", charset);
	mlist = camel_header_raw_check_mailing_list(&h);

	if (ct)
		camel_content_type_unref(ct);

	mi->subject = camel_pstring_strdup(subject);
	mi->from = camel_pstring_strdup(from);
	mi->to = camel_pstring_strdup(to);
	mi->cc = camel_pstring_strdup(cc);
	mi->mlist = camel_pstring_strdup(mlist);

	g_free(subject);
	g_free(from);
	g_free(to);
	g_free(cc);
	g_free(mlist);

	mi->user_flags = NULL;
	mi->user_tags = NULL;
	mi->date_sent = camel_header_decode_date(camel_header_raw_find(&h, "date", NULL), NULL);
	received = camel_header_raw_find(&h, "received", NULL);
	if (received)
		received = strrchr(received, ';');
	if (received)
		mi->date_received = camel_header_decode_date(received + 1, NULL);
	else
		mi->date_received = 0;

	msgid = camel_header_msgid_decode(camel_header_raw_find(&h, "message-id", NULL));
	if (msgid) {
		md5_get_digest(msgid, strlen(msgid), digest);
		memcpy(mi->message_id.id.hash, digest, sizeof(mi->message_id.id.hash));
		g_free(msgid);
	}
	
	/* decode our references and in-reply-to headers */
	refs = camel_header_references_decode (camel_header_raw_find (&h, "references", NULL));
	irt = camel_header_references_inreplyto_decode (camel_header_raw_find (&h, "in-reply-to", NULL));
	if (refs || irt) {
		if (irt) {
			/* The References field is populated from the ``References'' and/or ``In-Reply-To''
			   headers. If both headers exist, take the first thing in the In-Reply-To header
			   that looks like a Message-ID, and append it to the References header. */
			
			if (refs)
				irt->next = refs;
			
			refs = irt;
		}
		
		count = camel_header_references_list_size(&refs);
		mi->references = g_malloc(sizeof(*mi->references) + ((count-1) * sizeof(mi->references->references[0])));
		count = 0;
		scan = refs;
		while (scan) {
			md5_get_digest(scan->id, strlen(scan->id), digest);
			if (memcmp(digest, mi->message_id.id.hash, sizeof(mi->message_id.id.hash)) != 0) {
				memcpy(mi->references->references[count].id.hash, digest, sizeof(mi->message_id.id.hash));
				count++;
			}
			scan = scan->next;
		}
		if (count == 0) {
			g_free(mi->references);
			mi->references = NULL;
		} else {
			mi->references->size = count;
		}
		camel_header_references_list_clear(&refs);
	}

	return (CamelMessageInfo *)mi;
}

/* are these even useful for anything??? */
static CamelMessageInfo *
message_info_new_from_parser(CamelFolderSummary *s, CamelMimeParser *mp)
{
	CamelMessageInfo *mi = NULL;
	int state;

	state = camel_mime_parser_state(mp);
	switch (state) {
	case CAMEL_MIME_PARSER_STATE_HEADER:
	case CAMEL_MIME_PARSER_STATE_MESSAGE:
	case CAMEL_MIME_PARSER_STATE_MULTIPART:
		if (s)
			mi = CFS_CLASS(s)->message_info_new_from_header(s, camel_mime_parser_headers_raw(mp));
		else
			mi = message_info_new_from_header(s, camel_mime_parser_headers_raw(mp));
		break;
	default:
		g_error("Invalid parser state");
	}

	return mi;
}

static CamelMessageInfo *
message_info_new_from_message(CamelFolderSummary *s, CamelMimeMessage *msg, const CamelMessageInfo *info)
{
	CamelMessageInfo *mi;
	if (s)
		mi = CFS_CLASS(s)->message_info_new_from_header(s, ((CamelMimePart *)msg)->headers);
	else
		mi = message_info_new_from_header(s, ((CamelMimePart *)msg)->headers);

	if (mi && info) {
		const CamelTag *tag = camel_message_info_user_tags(info);
		const CamelFlag *flag = camel_message_info_user_flags(info);

		while (flag) {
			camel_message_info_set_user_flag(mi, flag->name, TRUE);
			flag = flag->next;
		}

		while (tag) {
			camel_message_info_set_user_tag(mi, tag->name, tag->value);
			tag = tag->next;
		}

		camel_message_info_set_flags(mi, camel_message_info_flags(info), ~0);
	}

	return mi;
}

static void
message_info_free(CamelMessageInfo *info)
{
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *)info;

	g_free(mi->uid);
	camel_pstring_free(mi->subject);
	camel_pstring_free(mi->from);
	camel_pstring_free(mi->to);
	camel_pstring_free(mi->cc);
	camel_pstring_free(mi->mlist);
	g_free(mi->references);
	camel_flag_list_free(&mi->user_flags);
	camel_tag_list_free(&mi->user_tags);
	g_free(mi);
}

/* 'dumb' base implementation */
static void
message_info_free_array(CamelFolderSummary *s, GPtrArray *mis)
{
	int i;

	for (i=0;i<mis->len;i++)
		camel_message_info_free(mis->pdata[i]);
	g_ptr_array_free(mis, TRUE);
}

static CamelMessageInfo *
message_info_clone(const CamelMessageInfo *mi)
{
	CamelMessageInfoBase *to, *from = (CamelMessageInfoBase *)mi;
	CamelFlag *flag;
	CamelTag *tag;

	to = (CamelMessageInfoBase *)camel_message_info_new(mi->summary);

	to->flags = from->flags;
	to->size = from->size;
	to->date_sent = from->date_sent;
	to->date_received = from->date_received;
	to->refcount = 1;

	/* NB: We don't clone the uid */

	to->subject = camel_pstring_strdup(from->subject);
	to->from = camel_pstring_strdup(from->from);
	to->to = camel_pstring_strdup(from->to);
	to->cc = camel_pstring_strdup(from->cc);
	to->mlist = camel_pstring_strdup(from->mlist);
	memcpy(&to->message_id, &from->message_id, sizeof(to->message_id));

	if (from->references) {
		int len = sizeof(*from->references) + ((from->references->size-1) * sizeof(from->references->references[0]));

		to->references = g_malloc(len);
		memcpy(to->references, from->references, len);
	}

	flag = from->user_flags;
	while (flag) {
		camel_flag_set(&to->user_flags, flag->name, TRUE);
		flag = flag->next;
	}

	tag = from->user_tags;
	while (tag) {
		camel_tag_set(&to->user_tags, tag->name, tag->value);
		tag = tag->next;
	}

	return (CamelMessageInfo *)to;
}

static const void *
info_ptr(const CamelMessageInfo *mi, int id)
{
	switch (id) {
	case CAMEL_MESSAGE_INFO_SUBJECT:
		return ((const CamelMessageInfoBase *)mi)->subject;
	case CAMEL_MESSAGE_INFO_FROM:
		return ((const CamelMessageInfoBase *)mi)->from;
	case CAMEL_MESSAGE_INFO_TO:
		return ((const CamelMessageInfoBase *)mi)->to;
	case CAMEL_MESSAGE_INFO_CC:
		return ((const CamelMessageInfoBase *)mi)->cc;
	case CAMEL_MESSAGE_INFO_MLIST:
		return ((const CamelMessageInfoBase *)mi)->mlist;
	case CAMEL_MESSAGE_INFO_MESSAGE_ID:
		return &((const CamelMessageInfoBase *)mi)->message_id;
	case CAMEL_MESSAGE_INFO_REFERENCES:
		return ((const CamelMessageInfoBase *)mi)->references;
	case CAMEL_MESSAGE_INFO_USER_FLAGS:
		return ((const CamelMessageInfoBase *)mi)->user_flags;
	case CAMEL_MESSAGE_INFO_USER_TAGS:
		return ((const CamelMessageInfoBase *)mi)->user_tags;
	default:
		abort();
	}
}

static guint32
info_uint32(const CamelMessageInfo *mi, int id)
{
	switch (id) {
	case CAMEL_MESSAGE_INFO_FLAGS:
		return ((const CamelMessageInfoBase *)mi)->flags;
	case CAMEL_MESSAGE_INFO_SIZE:
		return ((const CamelMessageInfoBase *)mi)->size;
	default:
		abort();
	}
}

static time_t
info_time(const CamelMessageInfo *mi, int id)
{
	switch (id) {
	case CAMEL_MESSAGE_INFO_DATE_SENT:
		return ((const CamelMessageInfoBase *)mi)->date_sent;
	case CAMEL_MESSAGE_INFO_DATE_RECEIVED:
		return ((const CamelMessageInfoBase *)mi)->date_received;
	default:
		abort();
	}
}

static gboolean
info_user_flag(const CamelMessageInfo *mi, const char *id)
{
	return camel_flag_get(&((CamelMessageInfoBase *)mi)->user_flags, id);
}

static const char *
info_user_tag(const CamelMessageInfo *mi, const char *id)
{
	return camel_tag_get(&((CamelMessageInfoBase *)mi)->user_tags, id);
}

static void
cfs_changed(CamelMessageInfoBase *mi)
{
	if (mi->summary && mi->summary->folder && mi->uid) {
		CamelFolderChangeInfo *changes = camel_folder_change_info_new();

		camel_folder_change_info_change_uid(changes, mi->uid);
		camel_object_trigger_event(mi->summary->folder, "folder_changed", changes);
		camel_folder_change_info_free(changes);
	}
}

static gboolean
info_set_flags(CamelMessageInfo *info, guint32 mask, guint32 set)
{
	guint32 old, diff, new;
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *)info;
	CamelFolderView *v;

	/* TODO: locking? */

	old = mi->flags;
	mi->flags = (old & ~mask) | (set & mask);

	// FIXME: what constitutes 'visible' needs to be configurable per summary?

	v = info->summary->root_view;

	/* diff will contain which flags have changed, set will contain what they are now */
	new = set & mask;
	diff = (set ^ old) & mask;
	if (diff & (CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_JUNK)) {
		if ((new & (CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_JUNK)) == 0)
			v->visible_count++;
		else if ((old & (CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_JUNK)) == 0)
			v->visible_count--;
	}
	if (diff & CAMEL_MESSAGE_SEEN) {
		if (new & CAMEL_MESSAGE_SEEN)
			v->unread_count--;
		else
			v->unread_count++;
	}
	if (diff & CAMEL_MESSAGE_DELETED) {
		if (new & CAMEL_MESSAGE_DELETED)
			v->deleted_count++;
		else
			v->deleted_count--;
	}
	if (diff & CAMEL_MESSAGE_JUNK) {
		if (new & CAMEL_MESSAGE_JUNK)
			v->junk_count++;
		else
			v->junk_count--;
	}

	if (diff & ~CAMEL_MESSAGE_SYSTEM_MASK)
		cfs_changed(mi);

	return diff != 0;
}

static gboolean
info_set_user_flag(CamelMessageInfo *info, const char *name, gboolean value)
{
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *)info;
	int res;

	res = camel_flag_set(&mi->user_flags, name, value);
	if (res)
		cfs_changed(mi);

	return res;
}

static gboolean
info_set_user_tag(CamelMessageInfo *info, const char *name, const char *value)
{
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *)info;
	int res;

	res = camel_tag_set(&mi->user_tags, name, value);
	if (res)
		cfs_changed(mi);

	return res;
}

static void
camel_folder_summary_class_init (CamelFolderSummaryClass *klass)
{
	camel_folder_summary_parent = camel_type_get_global_classfuncs (camel_object_get_type ());

	klass->add = add;
	klass->add_array = add_array;
	klass->remove = cfs_remove;
	klass->remove_array = remove_array;
	klass->clear = cfs_clear;

	klass->view_create = cfs_view_create;
	klass->view_delete = cfs_view_delete;
	klass->view_free = cfs_view_free;

	klass->get = get;
	klass->get_array = get_array;

	klass->search = search;

	klass->message_info_alloc = message_info_alloc;
	klass->message_info_clone = message_info_clone;

	klass->message_info_free = message_info_free;
	klass->message_info_free_array = message_info_free_array;

	klass->message_info_new_from_header = message_info_new_from_header;
	klass->message_info_new_from_parser = message_info_new_from_parser;
	klass->message_info_new_from_message = message_info_new_from_message;

	klass->info_ptr = info_ptr;
	klass->info_uint32 = info_uint32;
	klass->info_time = info_time;
	klass->info_user_flag = info_user_flag;
	klass->info_user_tag = info_user_tag;

	klass->info_set_user_flag = info_set_user_flag;
	klass->info_set_user_tag = info_set_user_tag;
	klass->info_set_flags = info_set_flags;
}

static void
camel_folder_summary_init (CamelFolderSummary *s)
{
	struct _CamelFolderSummaryPrivate *p;

	p = _PRIVATE(s) = g_malloc0(sizeof(*p));

	e_dlist_init(&s->views);

	p->ref_lock = g_mutex_new();
}

static void
camel_folder_summary_finalize (CamelObject *obj)
{
	struct _CamelFolderSummaryPrivate *p;
	CamelFolderSummary *s = (CamelFolderSummary *)obj;

	p = _PRIVATE(obj);

	g_mutex_free(p->ref_lock);
	
	g_free(p);

	if (s->search)
		camel_object_unref(s->search);
}

CamelType
camel_folder_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_object_get_type (), "CamelFolderSummary",
					    sizeof (CamelFolderSummary),
					    sizeof (CamelFolderSummaryClass),
					    (CamelObjectClassInitFunc) camel_folder_summary_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_folder_summary_init,
					    (CamelObjectFinalizeFunc) camel_folder_summary_finalize);
	}
	
	return type;
}

int camel_folder_summary_rename(CamelFolderSummary *s, const char *newname)
{
	if (CFS_CLASS(s)->rename == NULL)
		return 0;

	return CFS_CLASS(s)->rename(s, newname);
}

/**
 * camel_folder_summary_free_array
 * @summary: a #CamelFolderSummary object
 * @array: array of #CamelMessageInfo items as returned from #camel_folder_summary_array
 * 
 * Free a folder summary array returned by various functions.
 **/
void
camel_folder_summary_free_array(CamelFolderSummary *s, GPtrArray *array)
{
	CFS_CLASS(s)->message_info_free_array(s, array);
}

/**
 * camel_folder_summary_get:
 * @summary: a #CamelFolderSummary object
 * @uid: a uid
 * 
 * Retrieve a summary item by uid.
 *
 * A referenced to the summary item is returned, which may be
 * ref'd or free'd as appropriate.
 * 
 * Returns the summary item, or %NULL if the uid @uid is not available
 **/
void *
camel_folder_summary_get(CamelFolderSummary *s, const char *uid)
{
	return CFS_CLASS(s)->get(s, uid);
}

/**
 * camel_folder_summary_get_array:
 * @summary: a #CamelFolderSummary object
 * @uid: a uid
 * 
 * Retrieve a list of summary items by uid.
 *
 * Free the list using camel_folder_summary_free_array(), although
 * individual items may be reffed and saved.
 **/
GPtrArray *
camel_folder_summary_get_array(CamelFolderSummary *s, const GPtrArray *array)
{
	return CFS_CLASS(s)->get_array(s, array);
}

/*
 * camel_folder_summary_add:
 * @summary: a #CamelFolderSummary object
 * @info: a #CamelMessageInfo
 * 
 * Adds a new @info record to the summary.  If @info->uid is %NULL,
 * then a new uid is automatically re-assigned by calling
 * #camel_folder_summary_next_uid_string.
 *
 * The @info record should have been generated by calling one of the
 * info_new_*() functions, as it will be free'd based on the summary
 * class.  And MUST NOT be allocated directly using malloc.
 *
 * Return: -1 on error (i.e. uid clash)
 **/
int
camel_folder_summary_add(CamelFolderSummary *s, void *info)
{
	if (info == NULL)
		return -1;

	g_assert(((CamelMessageInfo *)info)->summary == s);

	return CFS_CLASS(s)->add(s, info);
}

int
camel_folder_summary_add_array(CamelFolderSummary *s, GPtrArray *infos)
{
	return CFS_CLASS(s)->add_array(s, infos);
}

/**
 * camel_folder_summary_clear:
 * @summary: a #CamelFolderSummary object
 * 
 * Empty the summary contents.
 **/
void
camel_folder_summary_clear(CamelFolderSummary *s)
{
	CFS_CLASS(s)->clear(s);
}

/**
 * camel_folder_summary_remove:
 * @summary: a #CamelFolderSummary object
 * @info: a #CamelMessageInfo
 * 
 * Remove a specific @info record from the summary.
 **/
int
camel_folder_summary_remove(CamelFolderSummary *s, void *info)
{
	return CFS_CLASS(s)->remove(s, info);
}

int
camel_folder_summary_remove_array(CamelFolderSummary *s, GPtrArray *infos)
{
	return CFS_CLASS(s)->remove_array(s, infos);
}

CamelMessageIterator *
camel_folder_summary_search(CamelFolderSummary *s, const char *vid, const char *expr, CamelMessageIterator *subset, CamelException *ex)
{
	return CFS_CLASS(s)->search(s, vid, expr, subset, ex);
}

CamelFolderView *
camel_folder_summary_view_lookup(CamelFolderSummary *s, const char *vid)
{
	CamelFolderView *v;

	if (vid == NULL) {
		if (s->root_view)
			s->root_view->refcount++;
		return s->root_view;
	}

	v = (CamelFolderView *)s->views.head;

	/* FIXME: lock, refcount? */
	while (v->next && strcmp(v->vid, vid) != 0)
		v = v->next;

	if (v->next) {
		v->refcount++;
		return v;
	}
	return NULL;
}

void
camel_folder_summary_view_unref(CamelFolderView *v)
{
	v->refcount--;
	if (v->refcount == 0)
		CFS_CLASS(v->summary)->view_free(v->summary, v);
}

const CamelFolderView *camel_folder_summary_view_create(CamelFolderSummary *s, const char *vid, const char *expr, CamelException *ex)
{
	CamelFolderView *v = (CamelFolderView *)s->views.head;

	/* FIXME: locking? */
	if (vid) {
		while (v->next && strcmp(v->vid, vid) != 0)
			v = v->next;

		if (v->next) {
			if (strcmp(v->expr, expr) == 0)
				return NULL;
		}
	} else {
		if (s->root_view)
			return NULL;
	}

	v = CFS_CLASS(s)->view_create(s, vid, expr, ex);
	if (camel_exception_is_set(ex)) {
		camel_folder_summary_view_unref(v);
		v = NULL;
	} else {
		if (vid == NULL)
			s->root_view = v;
		else
			e_dlist_addtail(&s->views, (EDListNode *)v);
	}

	return v;
}

void camel_folder_summary_view_delete(CamelFolderSummary *s, const char *vid)
{
	CamelFolderView *v = (CamelFolderView *)s->views.head;

	/* FIXME: locking? */
	while (v->next && strcmp(v->vid, vid) != 0)
		v = v->next;

	if (v->next) {
		e_dlist_remove((EDListNode *)v);
		v->deleted = TRUE;
		CFS_CLASS(s)->view_delete(s, v);
		camel_folder_summary_view_unref(v);
	}
}

/**
 * camel_message_info_new:
 * @summary: a #CamelFolderSummary object or %NULL
 *
 * Create a new #CamelMessageInfo.
 *
 * Returns a new #CamelMessageInfo
 **/
void *
camel_message_info_new (CamelFolderSummary *s)
{
	CamelMessageInfo *info;

	if (s)
		info = CFS_CLASS(s)->message_info_alloc(s);
	else
		info = g_malloc0(sizeof(CamelMessageInfoBase));

	info->refcount = 1;
	info->summary = s;

	return info;
}

/**
 * camel_message_info_new_from_header:
 * @summary: a #CamelFolderSummary object or %NULL
 * @header: raw header
 *
 * Create a new #CamelMessageInfo pre-populated with info from
 * @header.
 *
 * Returns a new #CamelMessageInfo
 **/
void *
camel_message_info_new_from_header(CamelFolderSummary *s, struct _camel_header_raw *h)
{
	if (s)
		return ((CamelFolderSummaryClass *)((CamelObject *)s)->klass)->message_info_new_from_header(s, h);
	else
		return message_info_new_from_header(NULL, h);
}

/**
 * camel_message_info_new_new_from_parser:
 * @summary: a #CamelFolderSummary object, or null
 * @parser: a #CamelMimeParser object
 * 
 * Create a new info record from a parser.  If the parser cannot
 * determine a uid, then none will be assigned.
 *
 * Once complete, the parser will be positioned at the end of
 * the message.
 *
 * Returns the newly allocated record which must be freed with
 * #camel_message_info_free
 **/
void *
camel_message_info_new_from_parser(CamelFolderSummary *s, CamelMimeParser *mp)
{
	CamelMessageInfo *mi;
	char *buffer;
	size_t len;

	if (camel_mime_parser_step(mp, &buffer, &len) != CAMEL_MIME_PARSER_STATE_EOF) {
		if (s)
			mi = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_new_from_parser(s, mp);
		else
			mi = message_info_new_from_parser(NULL, mp);

		camel_mime_parser_unstep(mp);
	}

	return (CamelMessageInfo *)mi;
}

/**
 * camel_message_info_new_from_message:
 * @summary: a #CamelFolderSummary object
 * @message: a #CamelMimeMessage object
 * @base: CamelMessageInfo to provide flags for this message.
 *
 * Create a summary item from a message.
 * 
 * Returns the newly allocated record which must be freed using
 * #camel_message_info_free
 **/
void *
camel_message_info_new_from_message(CamelFolderSummary *s, CamelMimeMessage *msg, const CamelMessageInfo *base)
{
	CamelMessageInfo *mi;

	if (s)
		mi = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_new_from_message(s, msg, base);
	else
		mi = message_info_new_from_message(NULL, msg, base);

	return mi;
}

/**
 * camel_message_info_clone:
 * @info: a #CamelMessageInfo
 *
 * Duplicate a #CamelMessageInfo.
 *
 * Returns the duplicated #CamelMessageInfo
 **/
void *
camel_message_info_clone(const void *o)
{
	const CamelMessageInfo *mi = o;

	if (mi->summary)
		return ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->message_info_clone(mi);
	else
		return message_info_clone(mi);
}

/**
 * camel_message_info_ref:
 * @info: a #CamelMessageInfo
 * 
 * Reference an info.
 **/
void
camel_message_info_ref(void *o)
{
	CamelMessageInfo *mi = o;

	if (mi->summary) {
		CAMEL_SUMMARY_LOCK(mi->summary, ref_lock);
		g_assert(mi->refcount >= 1);
		mi->refcount++;
		CAMEL_SUMMARY_UNLOCK(mi->summary, ref_lock);
	} else {
		GLOBAL_INFO_LOCK(info);
		g_assert(mi->refcount >= 1);
		mi->refcount++;
		GLOBAL_INFO_UNLOCK(info);
	}
}

/**
 * camel_message_info_free:
 * @info: a #CamelMessageInfo
 *
 * Unref's and potentially frees a #CamelMessageInfo and its contents.
 **/
void
camel_message_info_free(void *o)
{
	CamelMessageInfo *mi = o;

	g_return_if_fail(mi != NULL);

	if (mi->summary) {
		CAMEL_SUMMARY_LOCK(mi->summary, ref_lock);

		g_assert(mi->refcount >= 1);
		mi->refcount--;
		if (mi->refcount > 0) {
			CAMEL_SUMMARY_UNLOCK(mi->summary, ref_lock);
			return;
		}

		CAMEL_SUMMARY_UNLOCK(mi->summary, ref_lock);

		CFS_CLASS(mi->summary)->message_info_free(mi);
	} else {
		GLOBAL_INFO_LOCK(info);
		mi->refcount--;
		if (mi->refcount > 0) {
			GLOBAL_INFO_UNLOCK(info);
			return;
		}
		GLOBAL_INFO_UNLOCK(info);

		message_info_free(mi);
	}
}

/* Get the folder for this messageinfo, will return NULL
   if it doesn't have one associated */
const CamelFolder *
camel_message_info_folder(const void *mi)
{
	if (((const CamelMessageInfo *)mi)->summary)
		return ((const CamelMessageInfo *)mi)->summary->folder;
	return NULL;
}

/**
 * camel_message_info_ptr:
 * @mi: a #CamelMessageInfo
 * @id: info to get
 *
 * Generic accessor method for getting pointer data.
 *
 * Returns the pointer data
 **/
const void *
camel_message_info_ptr(const CamelMessageInfo *mi, int id)
{
	if (mi->summary)
		return ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_ptr(mi, id);
	else
		return info_ptr(mi, id);
}

/**
 * camel_message_info_uint32:
 * @mi: a #CamelMessageInfo
 * @id: info to get
 *
 * Generic accessor method for getting 32bit int data.
 *
 * Returns the int data
 **/
guint32
camel_message_info_uint32(const CamelMessageInfo *mi, int id)
{
	if (mi->summary)
		return ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_uint32(mi, id);
	else
		return info_uint32(mi, id);
}

/**
 * camel_message_info_time:
 * @mi: a #CamelMessageInfo
 * @id: info to get
 *
 * Generic accessor method for getting time_t data.
 *
 * Returns the time_t data
 **/
time_t
camel_message_info_time(const CamelMessageInfo *mi, int id)
{
	if (mi->summary)
		return ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_time(mi, id);
	else
		return info_time(mi, id);
}

/**
 * camel_message_info_user_flag:
 * @mi: a #CamelMessageInfo
 * @id: user flag to get
 *
 * Get the state of a user flag named @id.
 *
 * Returns the state of the user flag
 **/
gboolean
camel_message_info_user_flag(const CamelMessageInfo *mi, const char *id)
{
	if (mi->summary)
		return ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_user_flag(mi, id);
	else
		return info_user_flag(mi, id);
}

/**
 * camel_message_info_user_tag:
 * @mi: a #CamelMessageInfo
 * @id: user tag to get
 *
 * Get the value of a user tag named @id.
 *
 * Returns the value of the user tag
 **/
const char *
camel_message_info_user_tag(const CamelMessageInfo *mi, const char *id)
{
	if (mi->summary)
		return ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_user_tag(mi, id);
	else
		return info_user_tag(mi, id);
}

/**
 * camel_message_info_set_flags:
 * @mi: a #CamelMessageInfo
 * @flags: mask of flags to change
 * @set: state the flags should be changed to
 *
 * Change the state of the system flags on the #CamelMessageInfo
 *
 * Returns %TRUE if any of the flags changed or %FALSE otherwise
 **/
gboolean
camel_message_info_set_flags(CamelMessageInfo *mi, guint32 flags, guint32 set)
{
	if (mi->summary)
		return ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_set_flags(mi, flags, set);
	else
		return info_set_flags(mi, flags, set);
}

/**
 * camel_message_info_set_user_flag:
 * @mi: a #CamelMessageInfo
 * @id: name of the user flag to set
 * @state: state to set the flag to
 *
 * Set the state of a user flag on a #CamelMessageInfo.
 *
 * Returns %TRUE if the state changed or %FALSE otherwise
 **/
gboolean
camel_message_info_set_user_flag(CamelMessageInfo *mi, const char *id, gboolean state)
{
	if (mi->summary)
		return ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_set_user_flag(mi, id, state);
	else
		return info_set_user_flag(mi, id, state);
}

/**
 * camel_message_info_set_user_tag:
 * @mi: a #CamelMessageInfo
 * @id: name of the user tag to set
 * @val: value to set
 *
 * Set the value of a user tag on a #CamelMessageInfo.
 *
 * Returns %TRUE if the value changed or %FALSE otherwise
 **/
gboolean
camel_message_info_set_user_tag(CamelMessageInfo *mi, const char *id, const char *val)
{
	if (mi->summary)
		return ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_set_user_tag(mi, id, val);
	else
		return info_set_user_tag(mi, id, val);
}

void
camel_message_info_dump (CamelMessageInfo *mi)
{
	if (mi == NULL) {
		printf("No message?\n");
		return;
	}

	printf("Subject: %s\n", camel_message_info_subject(mi));
	printf("To: %s\n", camel_message_info_to(mi));
	printf("Cc: %s\n", camel_message_info_cc(mi));
	printf("mailing list: %s\n", camel_message_info_mlist(mi));
	printf("From: %s\n", camel_message_info_from(mi));
	printf("UID: %s\n", camel_message_info_uid(mi));
	printf("Flags: %04x\n", camel_message_info_flags(mi));
}

/* ********************************************************************** */

char *
camel_message_info_format_address(struct _camel_header_raw *h, const char *name, const char *charset)
{
	struct _camel_header_address *addr;
	const char *text;
	char *ret;

	text = camel_header_raw_find (&h, name, NULL);
	addr = camel_header_address_decode (text, charset);
	if (addr) {
		ret = camel_header_address_list_format (addr);
		camel_header_address_list_clear (&addr);
	} else {
		ret = g_strdup (text);
	}
	
	return ret;
}

char *
camel_message_info_format_string(struct _camel_header_raw *h, const char *name, const char *charset)
{
	const char *text;
	
	text = camel_header_raw_find (&h, name, NULL);
	if (text) {
		/* FIXME: use camel_isspace */
		while (isspace ((unsigned) *text))
			text++;
		return camel_header_decode_string (text, charset);
	} else {
		return NULL;
	}
}

/* ********************************************************************** */

/**
 * camel_flag_get:
 * @list: the address of a #CamelFlag list
 * @name: name of the flag to get
 * 
 * Find the state of the flag @name in @list.
 * 
 * Returns the state of the flag (%TRUE or %FALSE)
 **/
gboolean
camel_flag_get(CamelFlag **list, const char *name)
{
	CamelFlag *flag;
	flag = *list;
	while (flag) {
		if (!strcmp(flag->name, name))
			return TRUE;
		flag = flag->next;
	}
	return FALSE;
}

/**
 * camel_flag_set:
 * @list: the address of a #CamelFlag list
 * @name: name of the flag to set or change
 * @value: the value to set on the flag
 * 
 * Set the state of a flag @name in the list @list to @value.
 *
 * Returns %TRUE if the value of the flag has been changed or %FALSE
 * otherwise
 **/
gboolean
camel_flag_set(CamelFlag **list, const char *name, gboolean value)
{
	CamelFlag *flag, *tmp;

	/* this 'trick' works because flag->next is the first element */
	flag = (CamelFlag *)list;
	while (flag->next) {
		tmp = flag->next;
		if (!strcmp(flag->next->name, name)) {
			if (!value) {
				flag->next = tmp->next;
				g_free(tmp);
			}
			return !value;
		}
		flag = tmp;
	}

	if (value) {
		tmp = g_malloc(sizeof(*tmp) + strlen(name));
		strcpy(tmp->name, name);
		tmp->next = 0;
		flag->next = tmp;
	}
	return value;
}

/**
 * camel_flag_list_size:
 * @list: the address of a #CamelFlag list
 * 
 * Get the length of the flag list.
 * 
 * Returns the number of flags in the list
 **/
int
camel_flag_list_size(CamelFlag **list)
{
	int count=0;
	CamelFlag *flag;

	flag = *list;
	while (flag) {
		count++;
		flag = flag->next;
	}
	return count;
}

/**
 * camel_flag_list_free:
 * @list: the address of a #CamelFlag list
 * 
 * Free the memory associated with the flag list @list.
 **/
void
camel_flag_list_free(CamelFlag **list)
{
	CamelFlag *flag, *tmp;
	flag = *list;
	while (flag) {
		tmp = flag->next;
		g_free(flag);
		flag = tmp;
	}
	*list = NULL;
}

/**
 * camel_flag_list_copy:
 * @to: the address of the #CamelFlag list to copy to
 * @from: the address of the #CamelFlag list to copy from
 * 
 * Copy a flag list.
 * 
 * Returns %TRUE if @to is changed or %FALSE otherwise
 **/
gboolean
camel_flag_list_copy(CamelFlag **to, CamelFlag **from)
{
	CamelFlag *flag, *tmp;
	int changed = FALSE;

	if (*to == NULL && from == NULL)
		return FALSE;

	/* Remove any now-missing flags */
	flag = (CamelFlag *)to;
	while (flag->next) {
		tmp = flag->next;
		if (!camel_flag_get(from, tmp->name)) {
			flag->next = tmp->next;
			g_free(tmp);
			changed = TRUE;
		} else {
			flag = tmp;
		}
	}

	/* Add any new flags */
	flag = *from;
	while (flag) {
		changed |= camel_flag_set(to, flag->name, TRUE);
		flag = flag->next;
	}

	return changed;
}

/**
 * camel_tag_get:
 * @list: the address of a #CamelTag list
 * @name: name of the tag to get
 *
 * Find the flag @name in @list and get the value.
 * 
 * Returns the value of the flag  or %NULL if unset
 **/
const char *
camel_tag_get(CamelTag **list, const char *name)
{
	CamelTag *tag;

	tag = *list;
	while (tag) {
		if (!strcmp(tag->name, name))
			return (const char *)tag->value;
		tag = tag->next;
	}
	return NULL;
}

/**
 * camel_tag_set:
 * @list: the address of a #CamelTag list
 * @name: name of the tag to set
 * @value: value to set on the tag
 * 
 * Set the tag @name in the tag list @list to @value.
 *
 * Returns %TRUE if the value on the tag changed or %FALSE otherwise
 **/
gboolean
camel_tag_set(CamelTag **list, const char *name, const char *value)
{
	CamelTag *tag, *tmp;

	/* this 'trick' works because tag->next is the first element */
	tag = (CamelTag *)list;
	while (tag->next) {
		tmp = tag->next;
		if (!strcmp(tmp->name, name)) {
			if (value == NULL) { /* clear it? */
				tag->next = tmp->next;
				g_free(tmp->value);
				g_free(tmp);
				return TRUE;
			} else if (strcmp(tmp->value, value)) { /* has it changed? */
				g_free(tmp->value);
				tmp->value = g_strdup(value);
				return TRUE;
			}
			return FALSE;
		}
		tag = tmp;
	}

	if (value) {
		tmp = g_malloc(sizeof(*tmp)+strlen(name));
		strcpy(tmp->name, name);
		tmp->value = g_strdup(value);
		tmp->next = 0;
		tag->next = tmp;
		return TRUE;
	}
	return FALSE;
}

/**
 * camel_tag_list_size:
 * @list: the address of a #CamelTag list
 * 
 * Get the number of tags present in the tag list @list.
 * 
 * Returns the number of tags
 **/
int
camel_tag_list_size(CamelTag **list)
{
	int count=0;
	CamelTag *tag;

	tag = *list;
	while (tag) {
		count++;
		tag = tag->next;
	}
	return count;
}

static void
rem_tag(char *key, char *value, CamelTag **to)
{
	camel_tag_set(to, key, NULL);
}

/**
 * camel_tag_list_copy:
 * @to: the address of the #CamelTag list to copy to
 * @from: the address of the #CamelTag list to copy from
 * 
 * Copy a tag list.
 * 
 * Returns %TRUE if @to is changed or %FALSE otherwise
 **/
gboolean
camel_tag_list_copy(CamelTag **to, CamelTag **from)
{
	int changed = FALSE;
	CamelTag *tag;
	GHashTable *left;

	if (*to == NULL && from == NULL)
		return FALSE;

	left = g_hash_table_new(g_str_hash, g_str_equal);
	tag = *to;
	while (tag) {
		g_hash_table_insert(left, tag->name, tag);
		tag = tag->next;
	}

	tag = *from;
	while (tag) {
		changed |= camel_tag_set(to, tag->name, tag->value);
		g_hash_table_remove(left, tag->name);
		tag = tag->next;
	}

	if (g_hash_table_size(left)>0) {
		g_hash_table_foreach(left, (GHFunc)rem_tag, to);
		changed = TRUE;
	}
	g_hash_table_destroy(left);

	return changed;
}

/**
 * camel_tag_list_free:
 * @list: the address of a #CamelTag list
 * 
 * Free the tag list @list.
 **/
void
camel_tag_list_free(CamelTag **list)
{
	CamelTag *tag, *tmp;
	tag = *list;
	while (tag) {
		tmp = tag->next;
		g_free(tag->value);
		g_free(tag);
		tag = tmp;
	}
	*list = NULL;
}

struct flag_names_t {
	char *name;
	guint32 value;
} flag_names[] = {
	{ "answered", CAMEL_MESSAGE_ANSWERED },
	{ "deleted", CAMEL_MESSAGE_DELETED },
	{ "draft", CAMEL_MESSAGE_DELETED },
	{ "flagged", CAMEL_MESSAGE_FLAGGED },
	{ "seen", CAMEL_MESSAGE_SEEN },
	{ "attachments", CAMEL_MESSAGE_ATTACHMENTS },
	{ "junk", CAMEL_MESSAGE_JUNK },
	{ "secure", CAMEL_MESSAGE_SECURE },
	{ NULL, 0 }
};

/**
 * camel_system_flag:
 * @name: name of a system flag
 * 
 * Returns the integer value of the system flag string
 **/
guint32
camel_system_flag (const char *name)
{
	struct flag_names_t *flag;
	
	g_return_val_if_fail (name != NULL, 0);
	
	for (flag = flag_names; *flag->name; flag++)
		if (!g_ascii_strcasecmp (name, flag->name))
			return flag->value;
	
	return 0;
}

/**
 * camel_system_flag_get:
 * @flags: bitwise system flags
 * @name: name of the flag to check for
 * 
 * Find the state of the flag @name in @flags.
 * 
 * Returns %TRUE if the named flag is set or %FALSE otherwise
 **/
gboolean
camel_system_flag_get (guint32 flags, const char *name)
{
	g_return_val_if_fail (name != NULL, FALSE);
	
	return flags & camel_system_flag (name);
}

/* ********************************************************************** */

void *camel_message_iterator_new(CamelMessageIteratorVTable *klass, size_t size)
{
	CamelMessageIterator *it;

	g_assert(size >= sizeof(CamelMessageIterator));

	it = g_malloc0(size);
	it->klass = klass;

	return it;
}

const struct _CamelMessageInfo *camel_message_iterator_next(void *it, CamelException *ex)
{
	return ((CamelMessageIterator *)it)->klass->next(it, ex);
}

void camel_message_iterator_reset(void *it)
{
	((CamelMessageIterator *)it)->klass->reset(it);
}

void camel_message_iterator_free(void *it)
{
	if (it) {
		((CamelMessageIterator *)it)->klass->free(it);
		g_free(it);
	}
}

/* ********************************************************************** */

struct _infos_iter {
	CamelMessageIterator iter;

	int index;
	GPtrArray *mis;
};

static const CamelMessageInfo *infos_iter_next(void *it, CamelException *ex)
{
	struct _infos_iter *ait = it;

	if (ait->index < ait->mis->len)
		return ait->mis->pdata[ait->index++];
	return NULL;
}

static void
infos_iter_reset(void *it)
{
	struct _infos_iter *ait = it;

	ait->index = 0;
}

static void
infos_iter_free(void *it)
{
	struct _infos_iter *ait = it;
	int i;

	for (i=0;i<ait->mis->len;i++)
		camel_message_info_free(ait->mis->pdata[i]);
	g_ptr_array_free(ait->mis, TRUE);
}

static CamelMessageIteratorVTable infos_iter_vtable = {
	infos_iter_free,
	infos_iter_next,
	infos_iter_reset,
};

/*
  Helper iterator that just wraps an infos of messageinfo's in an iterator interface.
  To be used sparingly, where values are already in memory only.
  The infos is not copied and is owned by the iterator afterwards (?) */
void *camel_message_iterator_infos_new(GPtrArray *mis, int freeit)
{
	struct _infos_iter *ait = camel_message_iterator_new(&infos_iter_vtable, sizeof(*ait));

	/* FIXME: should we sort this so the iterator is in canonical folder-summary order?
	   Yes almost certainly we should ... */

	if (freeit)
		ait->mis = mis;
	else {
		int i;

		ait->mis = g_ptr_array_new();
		for (i=0;i<mis->len;i++) {
			g_ptr_array_add(ait->mis, mis->pdata[i]);
			camel_message_info_ref(mis->pdata[i]);
		}
	}
	ait->index = 0;

	return ait;
}

/* ********************************************************************** */

struct _uids_iter {
	CamelMessageIterator iter;

	int index;
	GPtrArray *uids;

	CamelFolder *folder;
	CamelMessageInfo *current;
};

static const CamelMessageInfo *uids_iter_next(void *it, CamelException *ex)
{
	struct _uids_iter *ait = it;

	if (ait->current) {
		camel_message_info_free(ait->current);
		ait->current = NULL;
	}

	while (ait->current == NULL && ait->index < ait->uids->len)
		ait->current = camel_folder_get_message_info(ait->folder, ait->uids->pdata[ait->index++]);

	return ait->current;
}

static void
uids_iter_reset(void *it)
{
	struct _uids_iter *ait = it;

	ait->index = 0;
}

static void
uids_iter_free(void *it)
{
	struct _uids_iter *ait = it;
	int i;

	if (ait->current)
		camel_message_info_free(ait->current);
	for (i=0;i<ait->uids->len;i++)
		g_free(ait->uids->pdata[i]);
	g_ptr_array_free(ait->uids, TRUE);
}

static CamelMessageIteratorVTable uids_iter_vtable = {
	uids_iter_free,
	uids_iter_next,
	uids_iter_reset,
};

void *camel_message_iterator_uids_new(CamelFolder *source, GPtrArray *uids, int freeit)
{
	struct _uids_iter *ait = camel_message_iterator_new(&uids_iter_vtable, sizeof(*ait));

	if (freeit)
		ait->uids = uids;
	else {
		int i;

		ait->uids = g_ptr_array_new();
		for (i=0;i<uids->len;i++)
			g_ptr_array_add(ait->uids, g_strdup(uids->pdata[i]));
	}
	ait->index = 0;
	ait->folder = source;
	camel_object_ref(source);

	return ait;
}
