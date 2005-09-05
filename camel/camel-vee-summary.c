/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
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

#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "camel-vee-summary.h"
#include "camel-folder.h"
#include "camel-folder-search.h"

#include "camel-record.h"

#include "libedataserver/md5-utils.h"
#include "camel-mime-utils.h"

#include "camel-session.h"
#include "camel-service.h"
#include "camel-view-summary-mem.h"

#define d(x)

#define CFS_CLASS(x) ((CamelFolderSummaryClass *)((CamelObject *)x)->klass)
#define CFS(x) ((CamelFolderSummary *)x)

static CamelFolderSummaryClass *camel_vee_summary_parent;

/* Maps a messageinfo in folder f, to a messageinfo in summary s */
static CamelMessageInfo *
vee_info_map(CamelVeeSummary *s, CamelVeeSummaryFolder *f, CamelMessageInfo *mi)
{
	char *uid;
	CamelVeeMessageInfo *vmi;

	uid = g_alloca(strlen(camel_message_info_uid(mi))+9);
	memcpy(uid, f->hash, 8);
	strcpy(uid+8, camel_message_info_uid(mi));

	/* FIXME: if we already have this uid in memory, we need to access that instead */

	vmi = camel_message_info_new((CamelFolderSummary *)s);
	vmi->real = mi;
	camel_message_info_ref(mi);
	vmi->info.uid = g_strdup(uid);

	return (CamelMessageInfo *)vmi;
}

static int
vee_uid_cmp(const void *ap, const void *bp, void *data)
{
	const char *a = ap, *b = bp;
	int res;

	/* First we just memcmp the hash to get overall order.
	   Then things get tricky if we're on the same folder.

	   We Look up the originating folder and use its comparison
	   function.  If we dont have that folder (anymore?) ... we're
	   kind of screwed and guess.  Guessing is a very bad thing
	   for a btree. */

	g_assert(strlen(a) > 8);
	g_assert(strlen(b) > 8);

	res = memcmp(a, b, 8);
	if (res == 0) {
		/* FIXME: LOCK */
		CamelVeeSummary *cvs = data;
		CamelVeeSummaryFolder *f = (CamelVeeSummaryFolder *)cvs->folders.head;

		while (f->next && memcmp(f->hash, a, 8) != 0)
			f = f->next;

		if (f->next)
			res = CFS_CLASS(f->folder->summary)->uid_cmp(a+8, b+8, f->folder->summary);
		else
			res = ((CamelFolderSummaryClass *)camel_vee_summary_parent)->uid_cmp(a+8, b+8, data);
	}

	return res;
}

static int
vee_info_cmp(const void *ap, const void *bp, void *data)
{
	const CamelVeeMessageInfo *a = ap;
	const CamelVeeMessageInfo *b = bp;
	int res;

	/* If we have messageinfo's the comparison case is much simpler */
	res = memcmp(a->info.uid, b->info.uid, 8);
	if (res == 0)
		res = CFS_CLASS(a->info.summary)->info_cmp(a, b, a->info.summary);

	return res;
}

static void *
vee_get(CamelFolderSummary *s, const char *uid)
{
	CamelVeeSummaryFolder *f;
	CamelMessageInfo *mi, *vmi;

	if (strlen(uid) <=8)
		return NULL;

	f = (CamelVeeSummaryFolder *)((CamelVeeSummary *)s)->folders.head;
	while (f->next && memcmp(f->hash, uid, 8) != 0)
		f = f->next;

	if (f->next == NULL || (mi = camel_folder_get_message_info(f->folder, uid+8)) == NULL)
		return NULL;

	vmi = vee_info_map((CamelVeeSummary *)s, f, mi);
	camel_message_info_free(mi);

	return vmi;
}

static CamelMessageInfo *
vee_message_info_alloc(CamelFolderSummary *s)
{
	return g_malloc0(sizeof(CamelVeeMessageInfo));
}

static void
vee_message_info_free(CamelMessageInfo *info)
{
	CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)info;

	g_free(info->uid);
	camel_message_info_free(mi->real);

	g_free(info);
}

static CamelMessageInfo *
vee_message_info_clone(const CamelMessageInfo *mi)
{
	CamelVeeMessageInfo *to;
	const CamelVeeMessageInfo *from = (const CamelVeeMessageInfo *)mi;

	to = (CamelVeeMessageInfo *)camel_message_info_new(mi->summary);

	to->real = camel_message_info_clone(from->real);
	to->info.summary = mi->summary;

	return (CamelMessageInfo *)to;
}

static const void *
vee_info_ptr(const CamelMessageInfo *mi, int id)
{
	return camel_message_info_ptr(((CamelVeeMessageInfo *)mi)->real, id);
}

static guint32
vee_info_uint32(const CamelMessageInfo *mi, int id)
{
	return camel_message_info_uint32(((CamelVeeMessageInfo *)mi)->real, id);
}

static time_t
vee_info_time(const CamelMessageInfo *mi, int id)
{
	return camel_message_info_time(((CamelVeeMessageInfo *)mi)->real, id);
}

static gboolean
vee_info_user_flag(const CamelMessageInfo *mi, const char *id)
{
	return camel_message_info_user_flag(((CamelVeeMessageInfo *)mi)->real, id);
}

static const char *
vee_info_user_tag(const CamelMessageInfo *mi, const char *id)
{
	return camel_message_info_user_tag(((CamelVeeMessageInfo *)mi)->real, id);
}

static gboolean
vee_info_set_user_flag(CamelMessageInfo *mi, const char *name, gboolean value)
{
	int res = FALSE;

	res = camel_message_info_set_user_flag(((CamelVeeMessageInfo *)mi)->real, name, value);

	return res;
}

static gboolean
vee_info_set_user_tag(CamelMessageInfo *mi, const char *name, const char *value)
{
	int res = FALSE;

	res = camel_message_info_set_user_tag(((CamelVeeMessageInfo *)mi)->real, name, value);

	return res;
}

static gboolean
vee_info_set_flags(CamelMessageInfo *mi, guint32 flags, guint32 set)
{
	int res = FALSE;
	guint32 old;
//	guint32 old, diff, new;

	old = camel_message_info_flags(((CamelVeeMessageInfo *)mi)->real);
	res = camel_message_info_set_flags(((CamelVeeMessageInfo *)mi)->real, flags, set);
#if 0
	new = set & flags;
	diff = (set ^ old) & flags;
	if (diff & (CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_JUNK)) {
		if ((new & (CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_JUNK)) == 0)
			mi->summary->visible_count++;
		else if ((old & (CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_JUNK)) == 0)
			mi->summary->visible_count--;
	}
	if (diff & CAMEL_MESSAGE_SEEN) {
		if (new & CAMEL_MESSAGE_SEEN)
			mi->summary->unread_count--;
		else
			mi->summary->unread_count++;
	}
	if (diff & CAMEL_MESSAGE_DELETED) {
		if (new & CAMEL_MESSAGE_DELETED)
			mi->summary->deleted_count++;
		else
			mi->summary->deleted_count--;
	}
	if (diff & CAMEL_MESSAGE_JUNK) {
		if (new & CAMEL_MESSAGE_JUNK)
			mi->summary->junk_count++;
		else
			mi->summary->junk_count--;
	}
#endif

	return res;
}

typedef struct _CamelIteratorVee CamelIteratorVee;
struct _CamelIteratorVee {
	CamelIterator iter;

	CamelVeeSummary *summary;

	char *expr;

	GPtrArray *mis;
	int mis_index;

	CamelMessageInfo *current;

	CamelVeeSummaryFolder *subfolder;
	CamelIterator *subiter;

	int reset:1;
};

/* Search each subfolder's view in turn */
static const void *cvs_iterator_next(void *mitin, CamelException *ex)
{
	CamelIteratorVee *mit = mitin;
	const CamelMessageInfo *mi;

	if (mit->current) {
		camel_message_info_free(mit->current);
		mit->current = NULL;
	}

nextiter:
	if (mit->subiter) {
		mi = camel_iterator_next(mit->subiter, ex);
		if (mi) {
			mit->current = vee_info_map(mit->summary, mit->subfolder->prev, (CamelMessageInfo *)mi);
			return mit->current;
		}
		camel_iterator_free(mit->subiter);
		mit->subiter = NULL;
	}

	/* FIXME: subsearches? */
	while (mit->subfolder->next) {
		mit->subiter = camel_folder_search(mit->subfolder->folder, mit->summary->vid, mit->expr, NULL, ex);
		mit->subfolder = mit->subfolder->next;
		if (mit->subiter)
			goto nextiter;
	}

	return NULL;
}

static void cvs_iterator_reset(void *mitin)
{
	CamelIteratorVee *mit = mitin;

	if (mit->subiter) {
		camel_iterator_free(mit->subiter);
		mit->subiter = NULL;
	}

	mit->subfolder = (CamelVeeSummaryFolder *)mit->summary->folders.head;
}

static void cvs_iterator_free(void *mitin)
{
	CamelIteratorVee *mit = mitin;

	if (mit->current)
		camel_message_info_free(mit->current);

	if (mit->subiter)
		camel_iterator_free(mit->subiter);

	camel_object_unref(((CamelFolderSummary *)mit->summary)->folder);
}

static CamelIteratorVTable cvs_iterator_vtable = {
	cvs_iterator_free,
	cvs_iterator_next,
	cvs_iterator_reset,
};

static struct _CamelIterator *cvs_search(CamelFolderSummary *s, const char *vid, const char *expr, CamelIterator *subset, CamelException *ex)
{
	CamelIteratorVee *mit;

	/* 'view' folders don't support sub-views */
	if (vid != NULL)
		return camel_message_iterator_infos_new(g_ptr_array_new(), TRUE);

	/* We assume subset is still in the summary, we should probably re-check? */

	if (subset) {
		/* we also assume subset is in the view ... */
		if (expr && expr[0])
			return (CamelIterator *)camel_folder_search_search(s->search, expr, subset, ex);
		else
			return subset;
	} else {
		/* The summary has no ref on the folder so we need to ref that instead */
		mit = camel_iterator_new(&cvs_iterator_vtable, sizeof(*mit));
		mit->summary = (CamelVeeSummary *)s;
		camel_object_ref(s->folder);
		mit->subfolder = (CamelVeeSummaryFolder *)((CamelVeeSummary *)s)->folders.head;

		if (expr && expr[0])
			return (CamelIterator *)camel_folder_search_search(s->search, expr, (CamelIterator *)mit, ex);
		else
			return (CamelIterator *)mit;
	}
}

static void
camel_vee_summary_class_init (CamelVeeSummaryClass *klass)
{
	((CamelFolderSummaryClass *)klass)->uid_cmp = vee_uid_cmp;
	((CamelFolderSummaryClass *)klass)->info_cmp = vee_info_cmp;

	((CamelFolderSummaryClass *)klass)->message_info_alloc = vee_message_info_alloc;
	((CamelFolderSummaryClass *)klass)->message_info_clone = vee_message_info_clone;
	((CamelFolderSummaryClass *)klass)->message_info_free = vee_message_info_free;

	((CamelFolderSummaryClass *)klass)->info_ptr = vee_info_ptr;
	((CamelFolderSummaryClass *)klass)->info_uint32 = vee_info_uint32;
	((CamelFolderSummaryClass *)klass)->info_time = vee_info_time;
	((CamelFolderSummaryClass *)klass)->info_user_flag = vee_info_user_flag;
	((CamelFolderSummaryClass *)klass)->info_user_tag = vee_info_user_tag;

	((CamelFolderSummaryClass *)klass)->search = cvs_search;

	((CamelFolderSummaryClass *)klass)->get = vee_get;

#if 0
	((CamelFolderSummaryClass *)klass)->info_set_string = vee_info_set_string;
	((CamelFolderSummaryClass *)klass)->info_set_uint32 = vee_info_set_uint32;
	((CamelFolderSummaryClass *)klass)->info_set_time = vee_info_set_time;
	((CamelFolderSummaryClass *)klass)->info_set_references = vee_info_set_references;
#endif
	((CamelFolderSummaryClass *)klass)->info_set_user_flag = vee_info_set_user_flag;
	((CamelFolderSummaryClass *)klass)->info_set_user_tag = vee_info_set_user_tag;

	((CamelFolderSummaryClass *)klass)->info_set_flags = vee_info_set_flags;
}

static void
camel_vee_summary_init (CamelVeeSummary *s)
{
	e_dlist_init(&s->folders);
}

CamelType
camel_vee_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		camel_vee_summary_parent = (CamelFolderSummaryClass *)camel_folder_summary_get_type();

		type = camel_type_register(
			(CamelType)camel_vee_summary_parent, "CamelVeeSummary",
			sizeof (CamelVeeSummary),
			sizeof (CamelVeeSummaryClass),
			(CamelObjectClassInitFunc) camel_vee_summary_class_init,
			NULL,
			(CamelObjectInitFunc) camel_vee_summary_init,
			NULL);
	}

	return type;
}

/**
 * camel_vee_summary_new:
 * @parent: Folder its attached to.
 * @vid: View id this vee folder is looking at.
 *
 * This will create a new CamelVeeSummary object and read in the
 * summary data from disk, if it exists.
 *
 * Return value: A new CamelVeeSummary object.
 **/
CamelFolderSummary *
camel_vee_summary_new(CamelFolder *parent, const char *vid)
{
	CamelVeeSummary *s;
	CamelView *view;
	CamelFolderView *fview;

	s = (CamelVeeSummary *)camel_object_new(camel_vee_summary_get_type());
	((CamelFolderSummary *)s)->folder = parent;
	s->vid = g_strdup(vid);

	CFS(s)->search = camel_folder_search_new();
	CFS(s)->view_summary = (CamelViewSummary *)camel_view_summary_mem_new();

	/* FIXME: refcounts? */
	view = camel_view_new(CFS(s)->view_summary, "");
	camel_view_summary_add(CFS(s)->view_summary, view, NULL);
	fview = camel_folder_view_new(CFS(s), view);
	camel_folder_view_add(CFS(s), fview, NULL);

	return (CamelFolderSummary *)s;
}

static CamelVeeSummaryFolder *
find_folder(CamelVeeSummary *s, const char *uri, CamelFolder *folder)
{
	CamelVeeSummaryFolder *fw, *fn;

	fw = (CamelVeeSummaryFolder *)s->folders.head;
	fn = fw->next;
	while (fn) {
		if ((uri && !strcmp(fw->uri, uri))
		    || (fw->folder && fw->folder == folder))
			return fw;
		fw = fn;
		fn = fn->next;
	}

	return NULL;
}

static void
hash_uri(const char *uri, char buffer[8])
{
	MD5Context ctx;
	unsigned char digest[16];
	unsigned int state = 0, save = 0;
	int i;

	md5_init(&ctx);
	md5_update(&ctx, uri, strlen(uri));
	md5_final(&ctx, digest);
	camel_base64_encode_close(digest, 6, FALSE, buffer, &state, &save);

	for (i=0;i<8;i++) {
		if (buffer[i] == '+')
			buffer[i] = '.';
		if (buffer[i] == '/')
			buffer[i] = '_';
	}
}

#if 0
static void
cvs_remove_info(CamelVeeSummary *s, CamelVeeSummaryFolder *f, const CamelMessageInfo *mi)
{
	const char *uid = camel_message_info_uid(mi);
	char *vuid;
	CamelMessageInfo *info;

	vuid = g_alloca(strlen(uid)+8);
	memcpy(vuid, f->hash, 8);
	strcpy(vuid+8, uid);

	info = camel_folder_summary_get((CamelFolderSummary *)s, vuid);
	if (info) {
		if (camel_folder_summary_remove((CamelFolderSummary *)s, info) == 0)
			camel_folder_change_info_remove_uid(s->changes, vuid);
		camel_message_info_free(info);
	}
}

static void
cvs_remove_infos(CamelVeeSummary *s, CamelVeeSummaryFolder *f, CamelIterator *iter)
{
	const CamelMessageInfo *mi;

	while ((mi = camel_iterator_next(iter, NULL)))
		cvs_remove_info(s, f, mi);

	camel_iterator_free(iter);
}

static void
cvs_add_info(CamelVeeSummary *s, CamelVeeSummaryFolder *f, const CamelMessageInfo *mi)
{
	const char *uid = camel_message_info_uid(mi);
	CamelVeeMessageInfo *vmi = camel_message_info_new((CamelFolderSummary *)s);

	vmi->info.uid = g_malloc(9+strlen(uid));
	memcpy(vmi->info.uid, f->hash, 8);
	strcpy(vmi->info.uid+8, uid);
	vmi->real = (CamelMessageInfo *)mi;
	camel_message_info_ref(vmi->real);

	/* This handles the case of re-adding things we shouldn't have,
	   but atomically and with potentially less effort than if we tried to do it */
	if (camel_folder_summary_add((CamelFolderSummary *)s, (CamelMessageInfo *)vmi) == 0)
		camel_folder_change_info_add_uid(s->changes, vmi->info.uid);
	else /* if EEXIST ... */
		camel_folder_change_info_change_uid(s->changes, vmi->info.uid);

	camel_message_info_free(vmi);
}

static void
cvs_add_infos(CamelVeeSummary *s, CamelVeeSummaryFolder *f, CamelIterator *iter)
{
	const CamelMessageInfo *mi;

	while ((mi = camel_iterator_next(iter, NULL)))
		cvs_add_info(s, f, mi);

	camel_iterator_free(iter);
}

static void
cvs_change_infos(CamelVeeSummary *s, CamelVeeSummaryFolder *f, CamelIterator *all, CamelIterator *match)
{
	const CamelMessageInfo *cmatch, *call;

	/* All and matches must be in the same order.
	   We remove everything in all but not in match
	   Add add everthing in match */

	printf("adding changed matches for search '%s' from folder '%s'\n", s->expr, f->folder->full_name);

	call = camel_message_iterator_next(all, NULL);
	cmatch = camel_message_iterator_next(match, NULL);
	while (call && cmatch) {
		int cmp = CFS_CLASS(call->summary)->info_cmp(call, cmatch, call->summary);

		if (cmp == 0) {
			cvs_add_info(s, f, cmatch);
			call = camel_message_iterator_next(all, NULL);
			cmatch = camel_message_iterator_next(match, NULL);
		} else if (cmp < 0) {
			cvs_remove_info(s, f, call);
			call = camel_message_iterator_next(all, NULL);
		} else {
			/* Out of order!!  Well, the match-set could be
			   bigger than the all-set due to changes in the
			   folder we can't control. */
			cvs_add_info(s, f, cmatch);
			cmatch = camel_message_iterator_next(match, NULL);
		}
	}

	while (call) {
		cvs_remove_info(s, f, call);
		call = camel_message_iterator_next(all, NULL);
	}

	/* This shouldn't normally happen either ... */
	while (cmatch) {
		cvs_add_info(s, f, cmatch);
		cmatch = camel_message_iterator_next(match, NULL);
	}

	camel_message_iterator_free(all);
	camel_message_iterator_free(match);
}
#endif

static void
cvs_folder_changed(CamelFolder *folder, CamelChangeInfo *changes, CamelVeeSummaryFolder *f)
{
	CamelChangeInfo *new;
	int i;

	/* FIXME: view lock? */

	new = ((CamelFolderSummary *)f->summary)->root_view->changes;

	/* Root view: removals always propagated */
	if (changes->vid == NULL) {
		for (i=0;i<changes->removed->len;i++) {
			CamelMessageInfo *vmi = vee_info_map(f->summary, f, changes->removed->pdata[i]);

			camel_change_info_remove(new, vmi);
			camel_message_info_free(vmi);
		}
	}

	if (changes->vid && strcmp(changes->vid, f->summary->vid) == 0) {
		for (i=0;i<changes->added->len;i++) {
			CamelMessageInfo *vmi = vee_info_map(f->summary, f, changes->added->pdata[i]);

			camel_change_info_add(new, vmi);
			camel_message_info_free(vmi);
		}
		for (i=0;i<changes->changed->len;i++) {
			CamelMessageInfo *vmi = vee_info_map(f->summary, f, changes->changed->pdata[i]);

			camel_change_info_change(new, vmi);
			camel_message_info_free(vmi);
		}
		// FIXME: auto-update folders?
		for (i=0;i<changes->removed->len;i++) {
			CamelMessageInfo *vmi = vee_info_map(f->summary, f, changes->removed->pdata[i]);

			camel_change_info_remove(new, vmi);
			camel_message_info_free(vmi);
		}
	}

	if (camel_change_info_changed(new)) {
		camel_object_trigger_event(((CamelFolderSummary *)f->summary)->folder, "folder_changed", new);
		camel_change_info_clear(new);
	}
}

static void
cvs_folder_deleted(CamelFolder *folder, void *dummy, CamelVeeSummaryFolder *f)
{
	camel_vee_summary_remove_folder(f->summary, folder);
}

static void
cvs_folder_renamed(CamelFolder *folder, const char *old, CamelVeeSummaryFolder *f)
{
	/* FIXME: We need to change all the uid's since the hash will have changed */
	g_warning("renamed not implemented");
}

void camel_vee_summary_add_folder(CamelVeeSummary *s, const char *uriin, struct _CamelFolder *folder)
{
	CamelVeeSummaryFolder *f;

	/* FIXME: locking! */

	f = find_folder(s, uriin, folder);
	if (f == NULL) {
		f = g_malloc0(sizeof(*f));
		if (uriin) {
			f->uri = g_strdup(uriin);
		} else {
			camel_object_get(folder, NULL, CAMEL_FOLDER_URI, &f->uri, 0);
		}
		hash_uri(f->uri, f->hash);

		e_dlist_addtail(&s->folders, (EDListNode *)f);
	}

	if (f->folder == NULL && folder != NULL) {
		f->folder = folder;
		f->summary = s;
		camel_object_ref(folder);

		printf("vsummary adding folder '%s' uri '%s'\n", folder->full_name, f->uri);

		f->changed_id = camel_object_hook_event(folder, "folder_changed", (CamelObjectEventHookFunc)cvs_folder_changed, f);
		f->deleted_id = camel_object_hook_event(folder, "deleted", (CamelObjectEventHookFunc)cvs_folder_deleted, f);
		f->renamed_id = camel_object_hook_event(folder, "renamed", (CamelObjectEventHookFunc)cvs_folder_renamed, f);

		{
			CamelIterator *iter;
			const CamelMessageInfo *mi;
			CamelMessageInfo *vmi;
			CamelChangeInfo *new;

			iter = camel_folder_search(folder, s->vid, NULL, NULL, NULL);
			while ((mi = camel_iterator_next(iter, NULL))) {
				vmi = vee_info_map(s, f, (CamelMessageInfo *)mi);
				camel_folder_summary_add((CamelFolderSummary *)s, vmi);
				camel_message_info_free(vmi);
			}
			camel_iterator_free(iter);

			new = ((CamelFolderSummary *)s)->root_view->changes;
			if (camel_change_info_changed(new)) {
				camel_object_trigger_event(((CamelFolderSummary *)s)->folder, "folder_changed", new);
				camel_change_info_clear(new);
			}
		}
	}
}

void camel_vee_summary_remove_folder(CamelVeeSummary *s, struct _CamelFolder *folder)
{
	CamelVeeSummaryFolder *f;

	f = find_folder(s, NULL, folder);
	if (f) {
		e_dlist_remove((EDListNode *)f);

		/* do something with it? */

		camel_object_remove_event(f->folder, f->renamed_id);
		camel_object_remove_event(f->folder, f->deleted_id);
		camel_object_remove_event(f->folder, f->changed_id);

		/* FIXME: race clearing stuff? */
		{
			CamelIterator *iter;
			const CamelMessageInfo *mi;
			CamelMessageInfo *vmi;
			CamelChangeInfo *new;

			iter = camel_folder_search(folder, s->vid, NULL, NULL, NULL);
			while ((mi = camel_iterator_next(iter, NULL))) {
				vmi = vee_info_map(s, f, (CamelMessageInfo *)mi);
				camel_folder_summary_remove((CamelFolderSummary *)s, vmi);
				camel_message_info_free(vmi);
			}
			camel_iterator_free(iter);

			new = ((CamelFolderSummary *)s)->root_view->changes;
			if (camel_change_info_changed(new)) {
				camel_object_trigger_event(((CamelFolderSummary *)s)->folder, "folder_changed", new);
				camel_change_info_clear(new);
			}
		}

		camel_object_unref(f->folder);

		g_free(f);
	}
}

void camel_vee_summary_set_folders(CamelVeeSummary *s, GList *folders)
{
	while (!e_dlist_empty(&s->folders)) {
		CamelVeeSummaryFolder *f = (CamelVeeSummaryFolder *)s->folders.head;

		camel_vee_summary_remove_folder(s, f->folder);
	}

	while (folders) {
		camel_vee_summary_add_folder(s, NULL, folders->data);
		folders = folders->next;
	}
}

void camel_vee_summary_set_expression(CamelVeeSummary *s, const char *expr)
{
	/* how to re-calculate the changed expression?  Do we, or do we just
	   remove/add the folder? */
}
