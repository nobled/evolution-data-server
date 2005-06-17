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

#include "camel-record.h"

#include "libedataserver/md5-utils.h"
#include "camel-mime-utils.h"

#include "camel-session.h"
#include "camel-service.h"

#define d(x)

#define CDS_CLASS(x) ((CamelFolderSummaryDiskClass *)((CamelObject *)x)->klass)
#define CFS_CLASS(x) ((CamelFolderSummaryClass *)((CamelObject *)x)->klass)

static CamelFolderSummaryClass *camel_vee_summary_parent;

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
	guint32 old, diff, new;

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

static void vee_encode_header(CamelFolderSummaryDisk *cds, CamelRecordEncoder *cde)
{
	CamelVeeSummaryFolder *f;
	guint32 count;

	camel_record_encoder_start_section(cde, CVS_SECTION_FOLDERINFO, 0);
	camel_record_encoder_string(cde, ((CamelVeeSummary *)cds)->expr);

	/* a list of all folder uri's */
	f = (CamelVeeSummaryFolder *)((CamelVeeSummary *)cds)->folders.head;
	count = e_dlist_length(&((CamelVeeSummary *)cds)->folders);
	camel_record_encoder_int32(cde, count);
	while (f->next) {
		camel_record_encoder_string(cde, f->uri);
		f = f->next;
	}

	camel_record_encoder_end_section(cde);
}

static int vee_decode_header(CamelFolderSummaryDisk *cds, CamelRecordDecoder *crd)
{
	int tag, ver, count, i;

	camel_record_decoder_reset(crd);
	while ((tag = camel_record_decoder_next_section(crd, &ver)) != CR_SECTION_INVALID) {
		switch (tag) {
		case CVS_SECTION_FOLDERINFO:
			g_free(((CamelVeeSummary *)cds)->expr);
			((CamelVeeSummary *)cds)->expr = g_strdup(camel_record_decoder_string(crd));
			count = camel_record_decoder_int32(crd);
			for (i=0;i<count;i++) {
				const char *uri = camel_record_decoder_string(crd);

				camel_vee_summary_add_folder((CamelVeeSummary *)cds, uri, NULL);
			}
			break;
		}
	}

	return 0;
}

static void vee_encode(CamelFolderSummaryDisk *cds, CamelMessageInfoDisk *mi, CamelRecordEncoder *cde)
{
	/* NOOP - we store nothing but the uid and that is stored elsewhere */

	g_assert(strlen(mi->info.uid)>8);
}

static int vee_decode(CamelFolderSummaryDisk *cds, CamelMessageInfoDisk *mi, CamelRecordDecoder *cdd)
{
	/* FIXME: LOCK */
	CamelVeeSummaryFolder *f = (CamelVeeSummaryFolder *)((CamelVeeSummary *)cds)->folders.head;

	g_assert(strlen(mi->info.uid)>8);

	/* We try to convert the vuid into a a reference to the real folder.  If we can't
	   or we dont know about it, it must be a dead reference, the superclass should clean it up */

	/* TODO: This is of course ... rather expensive if we have a lot of folders */
	while (f->next && memcmp(f->hash, mi->info.uid, 8) != 0)
		f = f->next;

	if (f->next == NULL)
		return -1;
	if (f->folder == NULL)
		// FIXME: load the folder from the uri we have?
		return -1;
	if ((((CamelVeeMessageInfo *)mi)->real = camel_folder_get_message_info(f->folder, mi->info.uid+8)) == NULL)
		return -1;

	return 0;
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

#if 0
	((CamelFolderSummaryClass *)klass)->info_set_string = vee_info_set_string;
	((CamelFolderSummaryClass *)klass)->info_set_uint32 = vee_info_set_uint32;
	((CamelFolderSummaryClass *)klass)->info_set_time = vee_info_set_time;
	((CamelFolderSummaryClass *)klass)->info_set_references = vee_info_set_references;
#endif
	((CamelFolderSummaryClass *)klass)->info_set_user_flag = vee_info_set_user_flag;
	((CamelFolderSummaryClass *)klass)->info_set_user_tag = vee_info_set_user_tag;

	((CamelFolderSummaryClass *)klass)->info_set_flags = vee_info_set_flags;

//	((CamelFolderSummaryDiskClass *)klass)->encode_header = vee_encode_header;
//	((CamelFolderSummaryDiskClass *)klass)->decode_header = vee_decode_header;

	((CamelFolderSummaryDiskClass *)klass)->encode = vee_encode;
	((CamelFolderSummaryDiskClass *)klass)->decode = vee_decode;
}

static void
camel_vee_summary_init (CamelVeeSummary *s)
{
	e_dlist_init(&s->folders);
	s->changes = camel_folder_change_info_new();
}

CamelType
camel_vee_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		camel_vee_summary_parent = (CamelFolderSummaryClass *)camel_folder_summary_disk_get_type();

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
 *
 * This will create a new CamelVeeSummary object and read in the
 * summary data from disk, if it exists.
 *
 * Return value: A new CamelVeeSummary object.
 **/
CamelFolderSummary *
camel_vee_summary_new(CamelFolder *parent)
{
	CamelVeeSummary *s;

	s = (CamelVeeSummary *)camel_object_new(camel_vee_summary_get_type());

	camel_folder_summary_disk_construct((CamelFolderSummaryDisk *)s, parent);

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
cvs_remove_infos(CamelVeeSummary *s, CamelVeeSummaryFolder *f, CamelMessageIterator *iter)
{
	const CamelMessageInfo *mi;

	while ((mi = camel_message_iterator_next(iter, NULL)))
		cvs_remove_info(s, f, mi);

	camel_message_iterator_free(iter);
}

static void
cvs_add_info(CamelVeeSummary *s, CamelVeeSummaryFolder *f, const CamelMessageInfo *mi)
{
	const char *uid = camel_message_info_uid(mi);
	CamelVeeMessageInfo *vmi = camel_message_info_new((CamelFolderSummary *)s);

	vmi->info.uid = g_malloc(8+strlen(uid));
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
cvs_add_infos(CamelVeeSummary *s, CamelVeeSummaryFolder *f, CamelMessageIterator *iter)
{
	const CamelMessageInfo *mi;

	while ((mi = camel_message_iterator_next(iter, NULL)))
		cvs_add_info(s, f, mi);

	camel_message_iterator_free(iter);
}

static void
cvs_change_infos(CamelVeeSummary *s, CamelVeeSummaryFolder *f, CamelMessageIterator *all, CamelMessageIterator *match)
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

struct _cvs_changed_msg {
	CamelSessionThreadMsg msg;
	CamelFolderChangeInfo *changes;
	CamelVeeSummary *s;
	CamelFolder *sub;
};

static int
cvs_array_info_cmp(const void *ap, const void *bp, void *data)
{
	const CamelMessageInfo *a = ((const CamelMessageInfo **)ap)[0];
	const CamelMessageInfo *b = ((const CamelMessageInfo **)bp)[0];

	return CFS_CLASS(data)->info_cmp(a, b, data);
}

static void
cvs_changed_change(CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _cvs_changed_msg *m = (struct _cvs_changed_msg *)msg;
	CamelVeeSummaryFolder *f;
	CamelVeeSummary *s = m->s;
	CamelFolderChangeInfo *changes;

	/* FIXME: LOCKING! */

	/* Might have been removed while the thread job was in the queue */
	if ((f = find_folder(s, NULL, m->sub)) == NULL)
		return;

	/* Handle changed uid's, only if the search could change based on it */
	if (!s->is_static && m->changes->uid_changed) {
		GPtrArray *changed = m->changes->uid_changed;
		int i;

		if (FALSE /*(vf->flags & CAMEL_STORE_VEE_FOLDER_AUTO) == 0*/) {
			if (f->changes == NULL)
				f->changes = camel_folder_change_info_new();
			for (i=0;i<changed->len;i++)
				camel_folder_change_info_change_uid(f->changes, changed->pdata[i]);
		} else {
			GPtrArray *infos = g_ptr_array_new();
			CamelMessageInfo *mi;

			for (i=0;i<changed->len;i++)
				if ((mi = camel_folder_get_message_info(m->sub, changed->pdata[i])))
					g_ptr_array_add(infos, mi);

			g_qsort_with_data(infos->pdata, infos->len, sizeof(infos->pdata[0]), cvs_array_info_cmp, m->sub->summary);

			cvs_change_infos(s, f,
					 camel_message_iterator_infos_new(infos, FALSE),
					 camel_folder_search(m->sub, NULL, s->expr, camel_message_iterator_infos_new(infos, TRUE), NULL));
		}
	}

	/* Remove all removed items if we have them */
	if (m->changes->uid_removed->len)
		cvs_remove_infos(s, f, camel_message_iterator_uids_new(m->sub, m->changes->uid_removed, FALSE));

	/* Add newly matched */
	if (m->changes->uid_added->len)
		cvs_add_infos(s, f,
			      camel_folder_search(m->sub, NULL, s->expr,
						  camel_message_iterator_uids_new(m->sub, m->changes->uid_added, FALSE),
						  NULL));

	if (camel_folder_change_info_changed(s->changes)) {
		changes = s->changes;
		s->changes = camel_folder_change_info_new();
	} else
		changes = NULL;

	// FIXME unlocking!

	if (changes) {
		camel_object_trigger_event(((CamelFolderSummary *)s)->folder, "folder_changed", changes);
		camel_folder_change_info_free(changes);
	}
}

static void
cvs_changed_free(CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _cvs_changed_msg *m = (struct _cvs_changed_msg *)msg;

	camel_folder_change_info_free(m->changes);
	camel_object_unref(m->s);
	camel_object_unref(m->sub);
}

static CamelSessionThreadOps folder_changed_ops = {
	cvs_changed_change,
	cvs_changed_free,
};

static void
cvs_folder_changed(CamelFolder *f, CamelFolderChangeInfo *changes, CamelVeeSummary *s)
{
	struct _cvs_changed_msg *m;
	CamelSession *session = ((CamelService *)f->parent_store)->session;
	
	m = camel_session_thread_msg_new(session, &folder_changed_ops, sizeof(*m));
	m->changes = camel_folder_change_info_new();
	camel_folder_change_info_cat(m->changes, changes);
	m->sub = f;
	camel_object_ref(f);
	m->s = s;
	camel_object_ref(s);
	camel_session_thread_queue(session, &m->msg, 0);
}

static void
cvs_folder_deleted(CamelFolder *f, void *dummy, CamelVeeSummary *s)
{
	g_warning("deleted not implemented");
}

static void
cvs_folder_renamed(CamelFolder *f, const char *old, CamelVeeSummary *s)
{
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
			CamelURL *url;
			char *uri, *tmp;

			/* This will get our account-relative uri ... probably */
			uri = camel_session_get_uri(((CamelService *)folder->parent_store)->session, (CamelService *)folder->parent_store);
			url = camel_url_new(uri, NULL);
			g_assert(url);
			tmp = g_alloca(strlen(folder->full_name)+1);
			sprintf(tmp, "/%s", folder->full_name);
			camel_url_set_path(url, tmp);
			f->uri = camel_url_to_string(url, 0);
			camel_url_free(url);
			g_free(uri);
		}
		hash_uri(f->uri, f->hash);

		e_dlist_addtail(&s->folders, (EDListNode *)f);
	}

	if (f->folder == NULL && folder != NULL) {
		f->folder = folder;
		camel_object_ref(folder);

		printf("vsummary adding folder '%s' uri '%s'\n", folder->full_name, f->uri);

		f->changed_id = camel_object_hook_event(folder, "folder_changed", (CamelObjectEventHookFunc)cvs_folder_changed, s);
		f->deleted_id = camel_object_hook_event(folder, "deleted", (CamelObjectEventHookFunc)cvs_folder_deleted, s);
		f->renamed_id = camel_object_hook_event(folder, "renamed", (CamelObjectEventHookFunc)cvs_folder_renamed, s);

#if 0
		/* This updates the whole folder; it will pick up any changes we had while offline, EXCEPT deleted */
		cvs_change_infos(s, f,
				 camel_folder_search(folder, NULL, NULL, NULL),
				 camel_folder_search(folder, s->expr, NULL, NULL));
#endif
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
	CamelVeeSummaryFolder *f;

	if (s->expr && expr && strcmp(s->expr, expr) == 0)
		return;
	if (s->expr == NULL && expr == NULL)
		return;

	g_free(s->expr);
	s->expr = g_strdup(expr);

	//s->is_static = blah blah;

	printf("expression changed, re-calculating vfolders\n");
	f = (CamelVeeSummaryFolder *)s->folders.head;
	while (f->next) {
		cvs_change_infos(s, f,
				 camel_folder_search(f->folder, NULL, NULL, NULL, NULL),
				 camel_folder_search(f->folder, NULL, s->expr, NULL, NULL));
		f = f->next;
	}
}
