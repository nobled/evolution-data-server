/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/*
 * Copyright (C) 2005 Novell Inc.
 *
 * Authors: Michael Zucchi <notzed@novell.com>
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

#include <string.h>
#include <unistd.h>

#include "camel-folder-summary-disk.h"
#include "camel-i18n.h"
#include "camel-record.h"
#include "camel-string-utils.h"

#include "camel-session.h"
#include "camel-service.h"
#include "camel-folder.h"
#include "camel-folder-search.h"

#include "camel-private.h"

#include "db.h"

#define w(x)
#define io(x)
#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/

#define CDS_CLASS(x) ((CamelFolderSummaryDiskClass *)((CamelObject *)x)->klass)
#define CFS_CLASS(x) ((CamelFolderSummaryClass *)((CamelObject *)x)->klass)

#define _PRIVATE(x) (((CamelFolderSummaryDisk *)x)->priv)

#define HEADER_KEY "__headerinfo"
#define HEADER_KEY_LEN (12)

struct _CamelFolderSummaryDiskPrivate {
	DB *db;

	/* do we have a sync job already launched? */
	volatile int sync_queued;

	/* These use the ref_lock */
	GHashTable *cache;
	GHashTable *changed;

	// Needs a lock!
	CamelFolderSearch *search;
};

typedef struct _CamelMessageIteratorDisk CamelMessageIteratorDisk;
struct _CamelMessageIteratorDisk {
	CamelMessageIterator iter;

	CamelFolderSummary *summary;
	char *expr;

	GPtrArray *mis;
	int mis_index;

	DBC *cursor;
	DBT key;
	DBT data;

	CamelMessageInfo *current;

	int reset:1;
};

static CamelFolderSummaryClass *cds_parent;

static int
cds_load_header(CamelFolderSummaryDisk *cds)
{
	struct _CamelFolderSummaryDiskPrivate *p = _PRIVATE(cds);
	DBT key = { 0 }, data = { 0 };
	int res = -1;

	key.data = HEADER_KEY;
	key.size = HEADER_KEY_LEN;
	data.flags = DB_DBT_MALLOC;

	res = p->db->get(p->db, NULL, &key, &data, 0);
	if (res == DB_NOTFOUND) {
		printf("entry not found?\n");
		return 0;
	}

	if (res == 0) {
		CamelRecordDecoder *crd;

		crd = camel_record_decoder_new(data.data, data.size);
		res = CDS_CLASS(cds)->decode_header(cds, crd);
		camel_record_decoder_free(crd);
	} else
		p->db->err(p->db, res, "getting header fialed");

	if (data.data)
		free(data.data);

	return res;
}

static int
cds_save_header(CamelFolderSummaryDisk *cds)
{
	struct _CamelFolderSummaryDiskPrivate *p = _PRIVATE(cds);
	DBT key = { 0 }, data = { 0 };
	CamelRecordEncoder *cre;
	int res = -1;

	printf("SAVING HEADER\n");

	cre = camel_record_encoder_new();
	CDS_CLASS(cds)->encode_header(cds, cre);

	key.data = HEADER_KEY;
	key.size = HEADER_KEY_LEN;
	data.data = cre->out->data;
	data.size = cre->out->len;

	// TODO: transactions
	res = p->db->put(p->db, NULL, &key, &data, 0);
	if (res == DB_NOTFOUND) {
		printf("  SAVING: db not found!? \n");
		return 0;
	} else if (res != 0)
		p->db->err(p->db, res, "saving header fialed");

	if (data.data)
		free(data.data);

	return res;
}

static int cds_save_info(CamelFolderSummaryDisk *s, CamelMessageInfo *mi, guint32 flags)
{
	struct _CamelFolderSummaryDiskPrivate *p = _PRIVATE(s);
	CamelRecordEncoder *cre = camel_record_encoder_new();
	DBT key = { 0 }, data = { 0 };
	int err;

	CDS_CLASS(s)->encode((CamelFolderSummaryDisk *)s, (CamelMessageInfoDisk *)mi, cre);

	key.data = mi->uid;
	key.size = strlen(mi->uid);
	data.data = cre->out->data;
	data.size = cre->out->len;

	/* transaction? */
	err = p->db->put(p->db, NULL, &key, &data, flags);
	if (err == DB_KEYEXIST)
		err = -1;
	else if (err != 0) {
		p->db->err(p->db, err, "putting info '%s' into database", mi->uid);
		err = -1;
	}
	/* else?? */

	camel_record_encoder_free(cre);

	return err;
}

#if 0
/* These have base implementations, it is only to override them or
   performance that they can be altered */
static int cds_add_array(CamelFolderSummary *s, GPtrArray *mis)
{
}

static void cds_remove_array(CamelFolderSummary *s, GPtrArray *mis)
{
}

static GPtrArray *cds_get_array(CamelFolderSummary *s, const GPtrArray *uids)
{
}


static CamelMessageInfo *cds_message_info_alloc(CamelFolderSummary *s)
{
}

static void cds_message_info_free_array(CamelFolderSummary *s, GPtrArray *mis)
{
}

static CamelMessageInfo *cds_message_info_clone(CamelFolderSummary *s, const CamelMessageInfo *mi)
{
}

static CamelMessageInfo *cds_message_info_new_from_header(CamelFolderSummary *s, struct _camel_header_raw *h)
{
}

/* FIXME: must put back indexing crap into camelfoldersummary? */
static CamelMessageInfo *cds_message_info_new_from_parser(CamelFolderSummary *s, struct _CamelMimeParser *mp)
{
}

/* FIXME: must put back indexing crap into camelfoldersummary? */
static CamelMessageInfo *cds_message_info_new_from_message(CamelFolderSummary *s, struct _CamelMimeMessage *msg)
{
}

/* virtual accessors on messageinfo's */
static const void *cds_info_ptr(const CamelMessageInfo *mi, int id)
{
}

static guint32 cds_info_uint32(const CamelMessageInfo *mi, int id)
{
}

static time_t cds_info_time(const CamelMessageInfo *mi, int id)
{
}

static gboolean cds_info_user_flag(const CamelMessageInfo *mi, const char *id)
{
}

static const char cds_info_user_tag(const CamelMessageInfo *mi, const char *id)
{
}
#endif

/* This is only useful for mh and mbox ... */
static int
cds_uid_cmp(const void *ap, const void *bp, void *data)
{
	const char *a = ap, *b = bp;
	unsigned long av, bv;

	av = strtoul(a, NULL, 10);
	bv = strtoul(b, NULL, 10);

	if (av < bv)
		return -1;
	else if (av > bv)
		return 1;
	else
		return 0;
}

static int
cds_info_cmp(const void *ap, const void *bp, void *data)
{
	const CamelMessageInfo *a = ap;
	const CamelMessageInfo *b = bp;

	return CFS_CLASS(data)->uid_cmp(a->uid, b->uid, data);
}

static int
cds_dbt_cmp(DB *db, const DBT *a, const DBT *b)
{
	char *au, *bu;

	/* we need to special-case the header node, always at the top */
	if (a->size == HEADER_KEY_LEN && b->size == HEADER_KEY_LEN
	    && memcmp(a->data, b->data, HEADER_KEY_LEN) == 0)
		return 0;
	else if (a->size == HEADER_KEY_LEN
	    && memcmp(a->data, HEADER_KEY, HEADER_KEY_LEN) == 0)
		return -1;
	else if (b->size == HEADER_KEY_LEN
		 && memcmp(b->data, HEADER_KEY, HEADER_KEY_LEN) == 0)
		return 1;

	/* this costs, but saves us duplicating key compare code */
	au = g_alloca(a->size+1);
	memcpy(au, a->data, a->size);
	au[a->size] = 0;

	bu = g_alloca(b->size+1);
	memcpy(bu, b->data, b->size);
	bu[b->size] = 0;

	return CFS_CLASS(db->app_private)->uid_cmp(au, bu, db->app_private);
}

static int cds_rename(CamelFolderSummary *s, const char *new)
{
	g_warning("rename not implemented");
	abort();
}

/* need to track a free to force a flush? */
/* wrong!  its already too late, a child class may have freed items */
static void cds_message_info_free(CamelMessageInfo *mi)
{
	struct _CamelFolderSummaryDiskPrivate *p = _PRIVATE(mi->summary);

	CAMEL_SUMMARY_LOCK(mi->summary, ref_lock);
	g_hash_table_remove(p->cache, mi->uid);
	CAMEL_SUMMARY_UNLOCK(mi->summary, ref_lock);

	cds_parent->message_info_free(mi);
}

static int cds_add(CamelFolderSummary *s, CamelMessageInfo *mi)
{
	struct _CamelFolderSummaryDiskPrivate *p = _PRIVATE(s);
	int res;

	/* NOOVERWRITE ensures we dont get duplicates */

	res = cds_save_info((CamelFolderSummaryDisk *)s, mi, DB_NOOVERWRITE);
	if (res == 0) {
		CAMEL_SUMMARY_LOCK(s, ref_lock);
		g_hash_table_insert(p->cache, mi->uid, mi);
		CAMEL_SUMMARY_UNLOCK(s, ref_lock);
		cds_parent->add(s, mi);
	}

	/* TODO: if it failed, or maybe even otherwise, put it in a queue for later adding? */

	return res;
}

static int cds_remove(CamelFolderSummary *s, CamelMessageInfo *mi)
{
	struct _CamelFolderSummaryDiskPrivate *p = _PRIVATE(s);
	DBT key;
	int res;

	CAMEL_SUMMARY_LOCK(s, ref_lock);
	g_hash_table_remove(p->cache, mi->uid);
	CAMEL_SUMMARY_UNLOCK(s, ref_lock);

	/* Should foldersummary.remove() return some NOTFOUND code? */

	key.data = mi->uid;
	key.size = strlen(mi->uid);
	res = p->db->del(p->db, NULL, &key, 0);
	if (res == DB_NOTFOUND)
		return -1;
	else if (res != 0) {
		/* failed, now what? */ ;
		p->db->err(p->db, res, "removing info '%s' from database", mi->uid);
		res = 0;
	}

	cds_parent->remove(s, mi);

	return res;
}

static void cds_clear(CamelFolderSummary *s)
{
	struct _CamelFolderSummaryDiskPrivate *p = _PRIVATE(s);

	p->db->truncate(p->db, NULL, NULL, DB_AUTO_COMMIT);

	CAMEL_SUMMARY_LOCK(s, ref_lock);

	g_hash_table_destroy(p->cache);
	p->cache = g_hash_table_new(g_str_hash, g_str_equal);
	g_hash_table_destroy(p->changed);
	p->changed = g_hash_table_new(g_str_hash, g_str_equal);

	CAMEL_SUMMARY_UNLOCK(s, ref_lock);

	cds_parent->clear(s);
}

static CamelMessageInfo *cds_get_record(CamelFolderSummaryDisk *s, DBT *key, DBT *data)
{
	struct _CamelFolderSummaryDiskPrivate *p = _PRIVATE(s);
	char *uid;
	CamelMessageInfo *mi, *info;

	/* We are nominally converting a loaded disk record into a messageinfo.
	   We need to check if the particular record has already been loaded though.
	   And things are complicated by having to check if someone else has loaded
	   it while we were loading it - we could just use more locks? */

	uid = g_strndup(key->data, key->size);

	CAMEL_SUMMARY_LOCK(s, ref_lock);
	mi = g_hash_table_lookup(p->cache, uid);
	if (mi && mi->refcount != 0) {
		mi->refcount++;
		CAMEL_SUMMARY_UNLOCK(s, ref_lock);
		g_free(uid);
	} else {
		CamelRecordDecoder *crd;

		CAMEL_SUMMARY_UNLOCK(s, ref_lock);

		mi = camel_message_info_new((CamelFolderSummary *)s);
		mi->uid = uid;

		crd = camel_record_decoder_new(data->data, data->size);
		if (CDS_CLASS(s)->decode((CamelFolderSummaryDisk *)s, (CamelMessageInfoDisk *)mi, crd) != 0) {
			/* I guess we should then forcibly remove the record if we can't grok it */
			camel_record_decoder_free(crd);
			camel_message_info_free(mi);
			return NULL;
		}
		camel_record_decoder_free(crd);

		CAMEL_SUMMARY_LOCK(s, ref_lock);
		info = g_hash_table_lookup(p->cache, uid);
		if (info && info->refcount != 0) {
			info->refcount++;
		} else {
			info = NULL;
			g_hash_table_insert(p->cache, mi->uid, mi);
		}
		CAMEL_SUMMARY_UNLOCK(s, ref_lock);

		if (info) {
			camel_message_info_free(mi);
			mi = info;
		}
	}

	return mi;
}

static CamelMessageInfo *cds_get(CamelFolderSummary *s, const char *uid)
{
	struct _CamelFolderSummaryDiskPrivate *p = _PRIVATE(s);
	DBT key = { 0 }, data = { 0 };
	CamelMessageInfo *mi;

	CAMEL_SUMMARY_LOCK(s, ref_lock);
	mi = g_hash_table_lookup(p->cache, uid);
	if (mi && mi->refcount != 0) {
		mi->refcount++;
		CAMEL_SUMMARY_UNLOCK(s, ref_lock);
		return mi;
	} else {
		mi = NULL;
	}
	CAMEL_SUMMARY_UNLOCK(s, ref_lock);

	key.data = (char *)uid;
	key.size = strlen(uid);
	data.flags = DB_DBT_MALLOC;

	if (p->db->get(p->db, NULL, &key, &data, 0) == 0) {
		mi = cds_get_record((CamelFolderSummaryDisk *)s, &key, &data);
		if (data.data)
			free(data.data);
	}

	return mi;
}

/* Search & iterators */
static const CamelMessageInfo *cds_iterator_next(void *mitin, CamelException *ex)
{
	CamelMessageIteratorDisk *mit = mitin;
	CamelMessageInfo *mi = NULL;

	if (mit->cursor) {
		int res;

		/* TODO: we read the whole record even if we dont need it ... */
		do {
			res = mit->cursor->c_get(mit->cursor, &mit->key, &mit->data, mit->reset?DB_FIRST:DB_NEXT);
			if (res == DB_NOTFOUND)
				return NULL;
			else if (res != 0) {
				// FIXMEL find a similar string
				camel_exception_setv(ex, 1, "operation failed: %s", db_strerror(res));
				return NULL;
			}
		} while (mit->key.size == HEADER_KEY_LEN
			 && memcmp(mit->key.data, HEADER_KEY, HEADER_KEY_LEN) == 0);

		if (mit->current)
			camel_message_info_free(mit->current);

		mi = cds_get_record((CamelFolderSummaryDisk *)mit->summary, &mit->key, &mit->data);
		mit->current = mi;
		mit->reset = 0;
	}

	return mi;
}

static void cds_iterator_reset(void *mitin)
{
	CamelMessageIteratorDisk *mit = mitin;

	mit->reset = 1;
}

static void cds_iterator_free(void *mitin)
{
	CamelMessageIteratorDisk *mit = mitin;

	if (mit->cursor) {
		mit->cursor->c_close(mit->cursor);
		if (mit->data.data)
			free(mit->data.data);
		if (mit->key.data)
			free(mit->key.data);
	}
	
	if (mit->current)
		camel_message_info_free(mit->current);

	camel_object_unref(mit->summary);
}

static CamelMessageIteratorVTable cds_iterator_vtable = {
	cds_iterator_free,
	cds_iterator_next,
	cds_iterator_reset,
};

static struct _CamelMessageIterator *cds_search(CamelFolderSummary *s, const char *expr, CamelMessageIterator *subset, CamelException *ex)
{
	struct _CamelFolderSummaryDiskPrivate *p = _PRIVATE(s);
	CamelMessageIteratorDisk *mit;

	/* We assume subset is still in the summary, we should probably re-check? */

	if (subset) {
		if (expr && expr[0])
			return (CamelMessageIterator *)camel_folder_search_search(p->search, expr, subset, ex);
		else
			return subset;
	} else {
		mit = camel_message_iterator_new(&cds_iterator_vtable, sizeof(*mit));
		mit->summary = s;
		camel_object_ref(s);

		if (p->db->cursor(p->db, NULL, &mit->cursor, 0) != 0)
			mit->cursor = NULL;
		else {
			mit->key.flags = DB_DBT_REALLOC;
			mit->data.flags = DB_DBT_REALLOC;
		}

		if (expr && expr[0])
			return (CamelMessageIterator *)camel_folder_search_search(p->search, expr, (CamelMessageIterator *)mit, ex);
		else
			return (CamelMessageIterator *)mit;
	}
}

/* ********************************************************************** */
/* After we have some changes, we run a job in another thread to flush them away
   We sleep for a bit to see if any more changes are pending, then go for it */

/* So ... This is all well and good, but has no connection to the storage mechanism,
   Where the storage mechanism also stores these values.
   i.e. mbox x-evolution header, maildir filename ... */

struct _sync_msg {
	CamelSessionThreadMsg msg;

	CamelFolderSummaryDisk *summary;
};

static void
cds_sync_run(CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _sync_msg *m = (struct _sync_msg *)msg;
	struct _CamelFolderSummaryDiskPrivate *p = _PRIVATE(m->summary);

	/* hmm, i like threads! */
	sleep(2);

	p->sync_queued = FALSE;

	printf(" sync thread timeout, flushing changes\n");
	camel_folder_summary_disk_sync(m->summary, &msg->ex);
}

static void
cds_sync_free(CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _sync_msg *m = (struct _sync_msg *)msg;

	camel_object_unref(m->summary);
}

static CamelSessionThreadOps cds_sync_ops = {
	cds_sync_run,
	cds_sync_free,
};

static void
cds_change_info(CamelMessageInfo *mi)
{
	struct _CamelFolderSummaryDiskPrivate *p = _PRIVATE(mi->summary);
	int dosync = 0;

	CAMEL_SUMMARY_LOCK(mi->summary, ref_lock);
	if (g_hash_table_lookup(p->changed, mi->uid) == NULL) {
		mi->refcount++;
		g_hash_table_insert(p->changed, mi->uid, mi);
		if (!p->sync_queued)
			p->sync_queued = dosync = 1;
	}
	CAMEL_SUMMARY_UNLOCK(mi->summary, ref_lock);

	if (dosync) {
		struct _sync_msg *m;
		CamelSession *session = ((CamelService *)mi->summary->folder->parent_store)->session;

		printf("queuing sync job\n");

		m = camel_session_thread_msg_new(session, &cds_sync_ops, sizeof(*m));
		m->summary = (CamelFolderSummaryDisk *)mi->summary;
		camel_object_ref(m->summary);
		camel_session_thread_queue(session, &m->msg, 0);
	}
}

/* we use the base implementation but check for changes so we can flush them at some point */
static gboolean cds_info_set_user_flag(CamelMessageInfo *mi, const char *id, gboolean state)
{
	int res = cds_parent->info_set_user_flag(mi, id, state);

	if (res)
		cds_change_info(mi);

	return res;
}

static gboolean cds_info_set_user_tag(CamelMessageInfo *mi, const char *id, const char *val)
{
	int res = cds_parent->info_set_user_tag(mi, id, val);

	if (res)
		cds_change_info(mi);

	return res;
}

static gboolean cds_info_set_flags(CamelMessageInfo *mi, guint32 mask, guint32 set)
{
	int res = cds_parent->info_set_flags(mi, mask, set);

	if (res)
		cds_change_info(mi);

	return res;
}

/* Do we need locking on this stuff?  matching the summary locking on them? */
static void cds_encode_header(CamelFolderSummaryDisk *cds, CamelRecordEncoder *cde)
{
	camel_record_encoder_start_section(cde, CFSD_SECTION_FOLDERINFO, 0);

	camel_record_encoder_int32(cde, ((CamelFolderSummary *)cds)->total_count);
	camel_record_encoder_int32(cde, ((CamelFolderSummary *)cds)->visible_count);
	camel_record_encoder_int32(cde, ((CamelFolderSummary *)cds)->unread_count);
	camel_record_encoder_int32(cde, ((CamelFolderSummary *)cds)->deleted_count);
	camel_record_encoder_int32(cde, ((CamelFolderSummary *)cds)->junk_count);

	camel_record_encoder_end_section(cde);
}

static int cds_decode_header(CamelFolderSummaryDisk *cds, CamelRecordDecoder *crd)
{
	int tag, ver;

	camel_record_decoder_reset(crd);
	while ((tag = camel_record_decoder_next_section(crd, &ver)) != CR_SECTION_INVALID) {
		switch (tag) {
		case CFSD_SECTION_FOLDERINFO:
			((CamelFolderSummary *)cds)->total_count = camel_record_decoder_int32(crd);
			((CamelFolderSummary *)cds)->visible_count = camel_record_decoder_int32(crd);
			((CamelFolderSummary *)cds)->unread_count = camel_record_decoder_int32(crd);
			((CamelFolderSummary *)cds)->deleted_count = camel_record_decoder_int32(crd);
			((CamelFolderSummary *)cds)->junk_count = camel_record_decoder_int32(crd); 
			break;
		}
	}

	return 0;
}

static void cds_encode(CamelFolderSummaryDisk *cds, CamelMessageInfoDisk *mi, CamelRecordEncoder *cde)
{
	const CamelSummaryReferences *refs;
	const CamelSummaryMessageID *id;
	int i;
	CamelTag *tag;
	CamelFlag *flag;

	/* read-only header section.
	   Untyped stream of binary represents each item in a code-known order.

	   New field entries MUST only ever be added to the end of a
	   given section, this lets the data structure automatically
	   be backward & forward compatible.  If fields become
	   obsolete they should be written as empty/0 rather than
	   removed, until a version bump (and even then usually).
	*/

	camel_record_encoder_start_section(cde, CFSD_SECTION_HEADERS, 0);

	camel_record_encoder_int32(cde, camel_message_info_size(mi));
	camel_record_encoder_timet(cde, camel_message_info_date_sent(mi));
	camel_record_encoder_timet(cde, camel_message_info_date_received(mi));
	camel_record_encoder_string(cde, camel_message_info_subject(mi));
	camel_record_encoder_string(cde, camel_message_info_from(mi));
	camel_record_encoder_string(cde, camel_message_info_to(mi));
	camel_record_encoder_string(cde, camel_message_info_cc(mi));
	camel_record_encoder_string(cde, camel_message_info_mlist(mi));

	refs = camel_message_info_references(mi);
	id = camel_message_info_message_id(mi);
	
	camel_record_encoder_int32(cde, refs?refs->size + 1: 1);
	camel_record_encoder_int64(cde, id?id->id.id:0);
	if (refs) {
		for (i=0;i<refs->size;i++)
			camel_record_encoder_int64(cde, refs->references[i].id.id);
	}

	camel_record_encoder_end_section(cde);

	/* variable size/content flags section */
	camel_record_encoder_start_section(cde, CFSD_SECTION_FLAGS, 0);
	camel_record_encoder_int32(cde, camel_message_info_flags(mi));

	flag = (CamelFlag *)camel_message_info_user_flags(mi);
	if (flag) {
		camel_record_encoder_int32(cde, camel_flag_list_size(&flag));
		for (;flag;flag=flag->next)
			camel_record_encoder_string(cde, flag->name);
	} else {
		camel_record_encoder_int32(cde, 0);
	}

	tag = (CamelTag *)camel_message_info_user_tags(mi);
	if (tag) {
		camel_record_encoder_int32(cde, camel_tag_list_size(&tag));
		for (;tag;tag=tag->next) {
			camel_record_encoder_string(cde, tag->name);
			camel_record_encoder_string(cde, tag->value);
		}
	} else {
		camel_record_encoder_int32(cde, 0);
	}

	camel_record_encoder_end_section(cde);
}

static int cds_decode(CamelFolderSummaryDisk *cds, CamelMessageInfoDisk *mi, CamelRecordDecoder *cdd)
{
	int tag, ver, count, i;
	const char *s, *v;
	int res = -1;

	camel_record_decoder_reset(cdd);
	while ((tag = camel_record_decoder_next_section(cdd, &ver)) != CR_SECTION_INVALID) {
		switch (tag) {
		case CFSD_SECTION_HEADERS:
			/* decode header/static data */
			mi->info.size = camel_record_decoder_int32(cdd);
			mi->info.date_sent = camel_record_decoder_timet(cdd);
			mi->info.date_received = camel_record_decoder_timet(cdd);
			mi->info.subject = camel_pstring_strdup(camel_record_decoder_string(cdd));
			mi->info.from = camel_pstring_strdup(camel_record_decoder_string(cdd));
			mi->info.to = camel_pstring_strdup(camel_record_decoder_string(cdd));
			mi->info.cc = camel_pstring_strdup(camel_record_decoder_string(cdd));
			mi->info.mlist = camel_pstring_strdup(camel_record_decoder_string(cdd));
			count = camel_record_decoder_int32(cdd);
			if (count>0)
				mi->info.message_id.id.id = camel_record_decoder_int64(cdd);
			if (count>1) {
				count--;
				mi->info.references = g_malloc(sizeof(*mi->info.references) + (sizeof(CamelSummaryMessageID) * count));
				mi->info.references->size = count;
				for (i=0;i<count;i++) {
					mi->info.references->references[i].id.id = camel_record_decoder_int64(cdd);
					g_assert(mi->info.references->references[i].id.id != mi->info.message_id.id.id);
				}
			}
			/* we only need CDS_HEADERS for a valid struct */
			res = 0;
			break;
		case CFSD_SECTION_FLAGS:
			/* decode flags/dynamic data */
			mi->info.flags = camel_record_decoder_int32(cdd);
			count = camel_record_decoder_int32(cdd);
			for (i=0;i<count;i++) {
				s = camel_record_decoder_string(cdd);
				if (*s)
					camel_flag_set(&mi->info.user_flags, s, 1);
			}
			count = camel_record_decoder_int32(cdd);
			for (i=0;i<count;i++) {
				s = camel_record_decoder_string(cdd);
				v = camel_record_decoder_string(cdd);
				if (*s)
					camel_tag_set(&mi->info.user_tags, s, v);
			}
			break;
		}
	}

	return res;
}

static void
cds_sync(CamelFolderSummaryDisk *cds, GPtrArray *infos, CamelException *ex)
{
	struct _CamelFolderSummaryDiskPrivate *p = _PRIVATE(cds);
	int i;

	/* TODO: use transactions? */

	for (i=0;i<infos->len;i++) {
		d(printf("Saving message '%s'\n", camel_message_info_uid(infos->pdata[i])));
		cds_save_info(cds, (CamelMessageInfo *)infos->pdata[i], 0);
	}
	cds_save_header(cds);

	p->db->sync(p->db, 0);
}

static void
camel_folder_summary_disk_class_init(CamelFolderSummaryDiskClass *klass)
{
	cds_parent = (CamelFolderSummaryClass *)camel_folder_summary_get_type();

	((CamelFolderSummaryClass *)klass)->uid_cmp = cds_uid_cmp;
	((CamelFolderSummaryClass *)klass)->info_cmp = cds_info_cmp;

	((CamelFolderSummaryClass *)klass)->rename = cds_rename;

	((CamelFolderSummaryClass *)klass)->add = cds_add;
	((CamelFolderSummaryClass *)klass)->remove = cds_remove;
	((CamelFolderSummaryClass *)klass)->clear = cds_clear;
	((CamelFolderSummaryClass *)klass)->message_info_free = cds_message_info_free;

	((CamelFolderSummaryClass *)klass)->get = cds_get;

	((CamelFolderSummaryClass *)klass)->search = cds_search;

	((CamelFolderSummaryClass *)klass)->info_set_user_flag = cds_info_set_user_flag;
	((CamelFolderSummaryClass *)klass)->info_set_user_tag = cds_info_set_user_tag;
	((CamelFolderSummaryClass *)klass)->info_set_flags = cds_info_set_flags;

	klass->encode_header = cds_encode_header;
	klass->decode_header = cds_decode_header;

	klass->encode = cds_encode;
	klass->decode = cds_decode;

	klass->sync = cds_sync;
}

static void
camel_folder_summary_disk_init(CamelFolderSummaryDisk *o)
{
	o->priv = g_malloc0(sizeof(*o->priv));
	o->priv->cache = g_hash_table_new(g_str_hash, g_str_equal);
	o->priv->changed = g_hash_table_new(g_str_hash, g_str_equal);
}

static void
camel_folder_summary_disk_finalise(CamelObject *obj)
{
	CamelFolderSummaryDisk *cds = (CamelFolderSummaryDisk *)obj;
	struct _CamelFolderSummaryDiskPrivate *p = _PRIVATE(cds);

	camel_object_unref(p->search);

	/* FIXME: empty cache? */
	g_hash_table_destroy(p->cache);
	g_free(p);
}

CamelType
camel_folder_summary_disk_get_type(void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register(camel_folder_summary_get_type(), "CamelFolderSummaryDisk",
					   sizeof (CamelFolderSummaryDisk),
					   sizeof (CamelFolderSummaryDiskClass),
					   (CamelObjectClassInitFunc) camel_folder_summary_disk_class_init,
					   NULL,
					   (CamelObjectInitFunc) camel_folder_summary_disk_init,
					   (CamelObjectFinalizeFunc) camel_folder_summary_disk_finalise);
	}
	
	return type;
}

static DB_ENV *
get_env(CamelService *s)
{
	char *base, *path;
	static DB_ENV *dbenv;
	int err;

	if (dbenv)
		return dbenv;

	base = camel_session_get_storage_path(s->session, s, NULL);
	path = g_build_filename(base, ".folders.db", NULL);
	g_free(base);

	printf("Creating database environment for store at %s\n", path);

	if (db_env_create(&dbenv, 0) != 0) {
		printf("env create failed\n");
		g_free(path);
		return NULL;
	}

	if ((err = dbenv->open(dbenv, path, DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_MPOOL|DB_INIT_TXN|DB_CREATE|DB_THREAD|DB_RECOVER /* |DB_PRIVATE */, 0666)) != 0) {
		dbenv->err(dbenv, err, "cannot create db");
		g_free(path);
		return NULL;
	}

	return dbenv;
}

CamelFolderSummaryDisk *
camel_folder_summary_disk_construct(CamelFolderSummaryDisk *cds, struct _CamelFolder *folder)
{
	struct _CamelFolderSummaryDiskPrivate *p = _PRIVATE(cds);
	int err;

	((CamelFolderSummary *)cds)->folder = folder;

	// TODO: have this optional based on implementation?
	p->search = camel_folder_search_new();

	db_create(&p->db, get_env((CamelService *)folder->parent_store), 0);
	p->db->app_private = cds;

	p->db->set_bt_compare(p->db, cds_dbt_cmp);
	//? db->set_flags(db, DB_RECNUM);

	if ((err = p->db->open(p->db, NULL, "folders", folder->full_name, DB_BTREE, DB_CREATE|DB_THREAD, 0666)) != 0)
		p->db->err(p->db, err, "opening folder summary '%s'", folder->full_name);

	printf("constructing summary, loading header\n");

	cds_load_header(cds);

	return cds;
}

CamelFolderSummaryDisk *
camel_folder_summary_disk_new(struct _CamelFolder *folder)
{
	CamelFolderSummaryDisk *cds = (CamelFolderSummaryDisk *)camel_object_new(camel_folder_summary_disk_get_type());

	return camel_folder_summary_disk_construct(cds, folder);
}

static int
cds_get_changed(void *k, void *v, void *d)
{
	GPtrArray *infos = d;

	g_ptr_array_add(infos, v);

	return TRUE;
}

static int
cds_array_info_cmp(const void *ap, const void *bp, void *data)
{
	const CamelMessageInfo *a = ((const CamelMessageInfo **)ap)[0];
	const CamelMessageInfo *b = ((const CamelMessageInfo **)bp)[0];

	return CFS_CLASS(data)->info_cmp(a, b, data);
}

void
camel_folder_summary_disk_sync(CamelFolderSummaryDisk *cds, CamelException *ex)
{
	struct _CamelFolderSummaryDiskPrivate *p = _PRIVATE(cds);
	GPtrArray *infos;
	int i;

	printf("syncing db summary\n");

	infos = g_ptr_array_new();
	CAMEL_SUMMARY_LOCK(cds, ref_lock);
	g_hash_table_foreach_remove(p->changed, cds_get_changed, infos);
	CAMEL_SUMMARY_UNLOCK(cds, ref_lock);

	/* sorted for your convenience */
	g_qsort_with_data(infos->pdata, infos->len, sizeof(infos->pdata[0]), cds_array_info_cmp, cds);

	CDS_CLASS(cds)->sync(cds, infos, ex);

	for (i=0;i<infos->len;i++)
		camel_message_info_free(infos->pdata[i]);
	g_ptr_array_free(infos, TRUE);
}
