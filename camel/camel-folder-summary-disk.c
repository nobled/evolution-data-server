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
	/* do we have a sync job already launched? */
	volatile int sync_queued;

	/* These use the ref_lock */
	GHashTable *cache;
	GHashTable *changed;
};

typedef struct _CamelMessageIteratorDisk CamelMessageIteratorDisk;
struct _CamelMessageIteratorDisk {
	CamelMessageIterator iter;

	CamelFolderView *view;
	char *expr;

	GPtrArray *mis;
	int mis_index;

	DBC *cursor;
	DBT key;
	DBT data;

	CamelMessageInfo *current;

	int reset:1;
};

typedef struct _CamelFolderViewDisk CamelFolderViewDisk;
struct _CamelFolderViewDisk {
	CamelFolderView view;

	/* So ... libdb has a tendency to deadlock a lot if we try to
	   do 'unusual' things, like write a record in 1 thread, while
	   we have a cursor opened and around the same spot(!) in another(!).

	   So to get around this, we do all our own locking and just
	   run it single threaded.  This is a pretty major i/o
	   performance hit, but it works, i hope. */

	struct _camel_disk_env *env;
	DB *db;

	/* we need to build this view, i.e. it didn't exist at startup */
	int build:1;
};

struct _camel_disk_env {
	int refcount;

	char *path;
	DB_ENV *env;

	/* EVERY call to libdb must be locked */
	/* Never lock another lock whilst holding this one */
	void *lock;
};

static CamelFolderSummaryClass *cds_parent;

#define LOCK_VIEW(v) g_mutex_lock(((CamelFolderViewDisk *)v)->env->lock)
#define UNLOCK_VIEW(v) g_mutex_unlock(((CamelFolderViewDisk *)v)->env->lock)

/* FIXME: Short term hack alert ... we just get one dbenv,
   based on the first folder opened :) */

static struct _camel_disk_env *
get_env(CamelService *s)
{
	char *base;
	static struct _camel_disk_env *env;
	int err;

	if (env) {
		env->refcount++;
		return env;
	}

	env = g_malloc0(sizeof(*env));
	env->refcount = 1;
	env->lock = g_mutex_new();

	base = camel_session_get_storage_path(s->session, s, NULL);
	env->path = g_build_filename(base, ".folders.db", NULL);
	g_free(base);

	printf("Creating database environment for store at %s\n", env->path);

	if (db_env_create(&env->env, 0) != 0) {
		printf("env create failed\n");
		return NULL;
	}

	if ((err = env->env->open(env->env, env->path, DB_INIT_LOG|DB_INIT_MPOOL|DB_INIT_TXN|DB_CREATE|DB_RECOVER /* |DB_PRIVATE */, 0666)) != 0) {
		env->env->err(env->env, err, "cannot create db");
		return NULL;
	}

	return env;
}

static int
cds_load_header(CamelFolderSummaryDisk *cds)
{
	CamelFolderViewDisk *view = (CamelFolderViewDisk *)((CamelFolderSummary *)cds)->root_view;
	DBT key = { 0 }, data = { 0 };
	int res = -1;

	key.data = HEADER_KEY;
	key.size = HEADER_KEY_LEN;
	data.flags = DB_DBT_MALLOC;

	LOCK_VIEW(view);
	res = view->db->get(view->db, NULL, &key, &data, 0);
	UNLOCK_VIEW(view);

	if (res == 0) {
		CamelRecordDecoder *crd;

		crd = camel_record_decoder_new(data.data, data.size);
		res = CDS_CLASS(cds)->decode_header(cds, crd);
		camel_record_decoder_free(crd);
	} else if (res == DB_NOTFOUND) {
		printf("entry not found?\n");
		res = 0;
	} else
		// TODO find out if this is safe to call unlocked?
		view->db->err(view->db, res, "getting header fialed");

	if (data.data)
		free(data.data);

	return res;
}

static int
cds_save_header(CamelFolderSummaryDisk *cds)
{
	CamelFolderViewDisk *view = (CamelFolderViewDisk *)((CamelFolderSummary *)cds)->root_view;
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

	// TODO: transactions?
	LOCK_VIEW(view);
	res = view->db->put(view->db, NULL, &key, &data, 0);
	UNLOCK_VIEW(view);

	if (res != 0)
		view->db->err(view->db, res, "saving header fialed");

	if (data.data)
		free(data.data);

	return res;
}

static int cds_save_info(CamelFolderSummaryDisk *s, CamelMessageInfo *mi, guint32 flags)
{
	CamelFolderViewDisk *view = (CamelFolderViewDisk *)((CamelFolderSummary *)s)->root_view;
	CamelRecordEncoder *cre = camel_record_encoder_new();
	DBT key = { 0 }, data = { 0 };
	int err;

	CDS_CLASS(s)->encode((CamelFolderSummaryDisk *)s, (CamelMessageInfoDisk *)mi, cre);

	key.data = mi->uid;
	key.size = strlen(mi->uid);
	data.data = cre->out->data;
	data.size = cre->out->len;

	/* transaction? */
	LOCK_VIEW(view);
	err = view->db->put(view->db, NULL, &key, &data, flags);
	if (err != 0 && err == DB_KEYEXIST)
		view->db->err(view->db, err, "putting info '%s' into database", mi->uid);
	UNLOCK_VIEW(view);

	camel_record_encoder_free(cre);

	return err;
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

static int
cds_dbt_cmp(DB *db, const DBT *a, const DBT *b)
{
	char *au, *bu;
	CamelFolderView *view;

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

	view = db->app_private;
	return CFS_CLASS(view->summary)->uid_cmp(au, bu, view->summary);
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

	/* we can have uid-less 'cloned' infos from the app */
	if (mi->uid) {
		CAMEL_SUMMARY_LOCK(mi->summary, ref_lock);
		g_hash_table_remove(p->cache, mi->uid);
		CAMEL_SUMMARY_UNLOCK(mi->summary, ref_lock);
	}

	cds_parent->message_info_free(mi);
}

/* These functions handle changed infos, added infos and removed ones
   respectively.  The update all of the embedded view indices as appropriate. */

// FIXME: We must use transactions for them all!!
// Their scope must cover the original put too!

// FIXME: some locking wouldn't go astray either

/* TODO:
   If we know what flags have changed, we can highly optimise trash and junk
   views, but whatever eh */

static int
cds_update_view_add(CamelFolderViewDisk *view, const CamelMessageInfo *mi)
{
	int res = 0;

	if (camel_folder_search_match(view->view.iter, mi, NULL)) {
		DBT key = { 0 }, data = { 0 };

		key.data = (void *)camel_message_info_uid(mi);
		key.size = strlen(key.data);

		LOCK_VIEW(view);
		res = view->db->put(view->db, NULL, &key, &data, DB_NOOVERWRITE);
		if (res == 0) {
			d(printf("  just added a new match\n"));
			view->view.total_count++;
			// FIXME: update counts
		}
		UNLOCK_VIEW(view);
	}

	return res;
}

static int
cds_update_view_remove(CamelFolderViewDisk *view, const CamelMessageInfo *mi)
{
	int res;
	DBT key = { 0 };

	key.data = (void *)camel_message_info_uid(mi);
	key.size = strlen(key.data);

	LOCK_VIEW(view);
	res = view->db->del(view->db, NULL, &key, 0);
	if (res == 0) {
		d(printf("  we had it, bye byte\n"));
		view->view.total_count--;
		// FIXME: update counts
	} else if (res != DB_NOTFOUND) {
		/* I can fill some data integrity issues coming on ... */
	}
	UNLOCK_VIEW(view);

	return res;
}

static int cds_update_views_change(CamelFolderSummaryDisk *cds, CamelMessageInfo *mi)
{
	CamelFolderViewDisk *view;
	int res = 0;

	for (view = (CamelFolderViewDisk *)((CamelFolderSummary *)cds)->views.head;
	     view->view.next;
	     view = (CamelFolderViewDisk *)view->view.next) {
		d(printf("updating uid '%s' for secondary index '%s'\n", camel_message_info_uid(mi), view->view.vid));

		/* static searches, we only scan immutable data, so no changes need to be re-checked */
		if (!view->view.is_static) {
			if (camel_folder_search_match(view->view.iter, mi, NULL))
				res = cds_update_view_add(view, mi);
			else
				res = cds_update_view_remove(view, mi);
		}
	}

	res = 0;

	return res;
}

static int cds_update_views_add(CamelFolderSummaryDisk *cds, CamelMessageInfo *mi)
{
	CamelFolderViewDisk *view;
	int res;

	view = (CamelFolderViewDisk *)((CamelFolderSummary *)cds)->views.head;
	while (((CamelFolderView *)view)->next) {
		res = cds_update_view_add(view, mi);
		view = (CamelFolderViewDisk *)view->view.next;
	}

	res = 0;

	return res;
}

static int cds_update_views_remove(CamelFolderSummaryDisk *cds, CamelMessageInfo *mi)
{
	CamelFolderViewDisk *view;
	int res = 0;

	view = (CamelFolderViewDisk *)((CamelFolderSummary *)cds)->views.head;
	while (((CamelFolderView *)view)->next) {
		res = cds_update_view_remove(view, mi);
		view = (CamelFolderViewDisk *)view->view.next;
	}

	res = 0;

	return res;
}

static int cds_add(CamelFolderSummary *s, void *o)
{
	CamelMessageInfo *mi = o;
	struct _CamelFolderSummaryDiskPrivate *p = _PRIVATE(s);
	int res;

	/* NOOVERWRITE ensures we dont get duplicates */

	res = cds_save_info((CamelFolderSummaryDisk *)s, mi, DB_NOOVERWRITE);

	if (res == 0) {
		CAMEL_SUMMARY_LOCK(s, ref_lock);
		g_hash_table_insert(p->cache, mi->uid, mi);
		CAMEL_SUMMARY_UNLOCK(s, ref_lock);
		cds_parent->add(s, mi);
		cds_update_views_add((CamelFolderSummaryDisk *)s, mi);
	}

	/* TODO: if it failed, or maybe even otherwise, put it in a queue for later adding? */

	return res;
}

static int cds_remove(CamelFolderSummary *s, void *o)
{
	CamelMessageInfo *mi = o, *oldmi;
	CamelFolderViewDisk *view = (CamelFolderViewDisk *)s->root_view;
	struct _CamelFolderSummaryDiskPrivate *p = _PRIVATE(s);
	DBT key = { 0 };
	int res;

	CAMEL_SUMMARY_LOCK(s, ref_lock);
	g_hash_table_remove(p->cache, mi->uid);
	oldmi = g_hash_table_lookup(p->changed, mi->uid);
	if (oldmi) {
		g_hash_table_remove(p->changed, mi->uid);
		g_assert(oldmi == mi);
		g_assert(mi->refcount > 1);
		mi->refcount--;
	}
	CAMEL_SUMMARY_UNLOCK(s, ref_lock);

	/* Should foldersummary.remove() return some NOTFOUND code? */

	// FIXME: transactions with view update?

	key.data = mi->uid;
	key.size = strlen(mi->uid);
	LOCK_VIEW(view);
	res = view->db->del(view->db, NULL, &key, 0);
	UNLOCK_VIEW(view);

	/* We never had it anyway, ignore it */
	if (res == DB_NOTFOUND)
		return -1;

	if (res == 0) {
		res = cds_update_views_remove((CamelFolderSummaryDisk *)s, mi);
	} else {
		/* failed, now what? */ ;
		view->db->err(view->db, res, "removing info '%s' from database", mi->uid);
		res = 0;
	}

	cds_parent->remove(s, mi);

	return res;
}

static void cds_clear(CamelFolderSummary *s)
{
	struct _CamelFolderSummaryDiskPrivate *p = _PRIVATE(s);
	CamelFolderViewDisk *view = (CamelFolderViewDisk *)s->root_view;

	LOCK_VIEW(view);

	view->db->truncate(view->db, NULL, NULL, 0);

	/* FIXME: other views? */
	/* FIXME: counts? */

	UNLOCK_VIEW(s->root_view);

	CAMEL_SUMMARY_LOCK(s, ref_lock);

	g_hash_table_destroy(p->cache);
	p->cache = g_hash_table_new(g_str_hash, g_str_equal);
	g_hash_table_destroy(p->changed);
	p->changed = g_hash_table_new(g_str_hash, g_str_equal);

	CAMEL_SUMMARY_UNLOCK(s, ref_lock);

	cds_parent->clear(s);
}

static void *cds_get(CamelFolderSummary *s, const char *uid)
{
	struct _CamelFolderSummaryDiskPrivate *p = _PRIVATE(s);
	CamelFolderViewDisk *view = (CamelFolderViewDisk *)s->root_view;
	DBT key = { 0 }, data = { 0 };
	CamelMessageInfo *mi;
	int res;

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

	LOCK_VIEW(view);
	res = view->db->get(view->db, NULL, &key, &data, 0);
	UNLOCK_VIEW(view);

	if (res == 0) {
		mi = cds_get_record((CamelFolderSummaryDisk *)s, &key, &data);
		if (data.data)
			free(data.data);
	}

	return mi;
}

/* Search & iterators, flags0 is flags to use on first key, and flags1 what to use after that
   if we need to go again - i.e. we got the header key or secondary index mismatch */
static const CamelMessageInfo *cds_iterator_step(void *mitin, guint32 flags0, guint32 flags1, CamelException *ex)
{
	CamelMessageIteratorDisk *mit = mitin;
	CamelFolderViewDisk *root = (CamelFolderViewDisk *)mit->view->summary->root_view;
	CamelMessageInfo *mi;
	int res;

	if (mit->cursor == NULL)
		return NULL;

	if (mit->current) {
		camel_message_info_free(mit->current);
		mit->current = NULL;
	}

	/* Iterates over all keys in either the primary or secondary index.
	   For secondary indices we then also have to lookup in the main
	   index for the actual content.  Easy peasy. */

	/* We lock the root 'view', but all views are the same environment anyway */
	LOCK_VIEW(root);

	do {
	nextkey:
		res = mit->cursor->c_get(mit->cursor, &mit->key, &mit->data, flags0);
		if (res == DB_NOTFOUND)
			goto done;
		else if (res != 0)
			goto done_fail;

		flags0 = flags1;

		if ((CamelFolderViewDisk *)mit->view != root) {
			res = root->db->get(root->db, NULL, &mit->key, &mit->data, 0);
			if (res == DB_NOTFOUND) {
				g_warning("Secondary index mismatch!  Eek!");
				goto nextkey;
			} else if (res != 0)
				goto done_fail;
		}
	} while (mit->key.size == HEADER_KEY_LEN
		 && memcmp(mit->key.data, HEADER_KEY, HEADER_KEY_LEN) == 0);

	UNLOCK_VIEW(root);

	mi = cds_get_record((CamelFolderSummaryDisk *)mit->view->summary, &mit->key, &mit->data);
	mit->current = mi;

	return mi;

done_fail:
	// FIXME: find a good translator string
	camel_exception_setv(ex, 1, "operation failed: %s", db_strerror(res));
done:
	UNLOCK_VIEW(root);

	return NULL;
}

static const CamelMessageInfo *cds_iterator_next(void *mitin, CamelException *ex)
{
	CamelMessageIteratorDisk *mit = mitin;
	const CamelMessageInfo *mi;

	mi = cds_iterator_step(mitin, mit->reset?DB_FIRST:DB_NEXT, DB_NEXT, ex);
	mit->reset = 0;

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
		LOCK_VIEW(mit->view);
		mit->cursor->c_close(mit->cursor);
		UNLOCK_VIEW(mit->view);
		if (mit->data.data)
			free(mit->data.data);
		if (mit->key.data)
			free(mit->key.data);
	}
	
	if (mit->current)
		camel_message_info_free(mit->current);

	/* FIXME: This could free the view before we unref it ... */
	camel_object_ref(mit->view->summary->folder);
	camel_folder_summary_view_unref(mit->view);
}

static CamelMessageIteratorVTable cds_iterator_vtable = {
	cds_iterator_free,
	cds_iterator_next,
	cds_iterator_reset,
};

static struct _CamelMessageIterator *cds_search(CamelFolderSummary *s, const char *vid, const char *expr, CamelMessageIterator *subset, CamelException *ex)
{
	CamelMessageIteratorDisk *mit;
	CamelFolderViewDisk *view;

	/* We find the right index to search on then use that.  If this view
	   isn't defined or available, then we return an empty set */

	view = (CamelFolderViewDisk *)camel_folder_summary_view_lookup(s, vid);
	if (view == NULL)
		return camel_message_iterator_infos_new(g_ptr_array_new(), TRUE);

	/* We assume subset is still in the summary, we should probably re-check? */

	if (subset) {
		/* we also assume subset is in the view ... */
		camel_folder_summary_view_unref((CamelFolderView *)view);
		if (expr && expr[0])
			return (CamelMessageIterator *)camel_folder_search_search(s->search, expr, subset, ex);
		else
			return subset;
	} else {
		/* The summary has no ref on the folder so we need to ref that instead */

		mit = camel_message_iterator_new(&cds_iterator_vtable, sizeof(*mit));
		mit->view = (CamelFolderView *)view;
		camel_object_ref(s->folder);

		LOCK_VIEW(view);

		if (view->db->cursor(view->db, NULL, &mit->cursor, 0) != 0)
			mit->cursor = NULL;
		else {
			mit->key.flags = DB_DBT_REALLOC;
			mit->data.flags = DB_DBT_REALLOC;
		}

		UNLOCK_VIEW(view);

		if (expr && expr[0])
			return (CamelMessageIterator *)camel_folder_search_search(s->search, expr, (CamelMessageIterator *)mit, ex);
		else
			return (CamelMessageIterator *)mit;
	}
}

#if 0
// see comments in view_build_all 
static void
cds_view_rebuild(CamelFolderSummary *s, CamelFolderViewDisk *view, CamelException *ex)
{
	CamelMessageIterator *iter;
	const CamelMessageInfo *info;
	DBT key = { 0 }, data = { 0 };
	int res;

	printf("building secondary index for new view '%s'\n", ((CamelFolderView *)view)->vid);

	/* Rebuild a secondary index.  We just store empty records,
	   with the matching keys only */

	iter = camel_folder_summary_search(s, NULL, view->view.expr, NULL, ex);
	if (iter == NULL)
		return;

	// This should be transaction protected

	while ((info = camel_message_iterator_next(iter, ex))) {
		key.data = (void *)camel_message_info_uid(info);
		key.size = strlen(key.data);

		LOCK_VIEW(view);
		res = view->db->put(view->db, NULL, &key, &data, 0);
		if (res == 0) {
			view->view.total_count++;
			// FIXME: update other counts
		}
		UNLOCK_VIEW(view);
	}
	camel_message_iterator_free(iter);

	printf(" %d total matches\n", view->view.total_count);
}
#endif

static void
cds_view_build_all(CamelFolderSummaryDisk *cds, CamelException *ex)
{
	CamelFolderViewDisk *view, *root = (CamelFolderViewDisk *)cds->summary.root_view;
	GPtrArray *build = g_ptr_array_new();
	int i;

	/* So, two possibilities here:
	   We iterator over all messages and match every view one by one
	   or
	   We search each view and add them.

	   The first should be better because we only iterate the full
	   set once, and should have good locality of reference for
	   everything anyway.

	   The second might be better, if we can somehow optimise
	   the search better than iterating over each message anyway.
	   Currently we don't, so the first should be better, and that is what
	   we're doing here.  (in reality they're probably the same).  Got that?!
	*/

	printf("Checking for views to rebuild:\n");

	/* FIXME: lockit */
	for (view = (CamelFolderViewDisk *)((CamelFolderSummary *)cds)->views.head;
	     ((CamelFolderView *)view)->next;
	     view = (CamelFolderViewDisk *)((CamelFolderView *)view)->next) {
		if (view->build) {
			printf(" %s\n", view->view.vid);
			// FIXME: ref it
			g_ptr_array_add(build, view);
			view->build = 0;
		}
	}

	if (build->len) {
		CamelMessageIterator *iter;
		const CamelMessageInfo *info;

		camel_operation_start(NULL, ngettext("Building view", "Building views", build->len));

		iter = camel_folder_summary_search((CamelFolderSummary *)cds, NULL, NULL, NULL, ex);
		while ((info = camel_message_iterator_next(iter, ex))) {
			camel_operation_progress(NULL, (i++)*100/root->view.total_count);

			for (i=0;i<build->len;i++)
				cds_update_view_add(build->pdata[i], info);
		}
		camel_message_iterator_free(iter);

		camel_operation_end(NULL);

		// fixme unref 'em
	} else {
		printf(" none!\n");
	}

	g_ptr_array_free(build, TRUE);
}

static CamelFolderView *
cds_view_create(CamelFolderSummary *s, const char *vid, const char *expr, CamelException *ex)
{
	CamelFolderViewDisk *view;
	char *name;
	int err;

	view = (CamelFolderViewDisk *)cds_parent->view_create(s, vid, expr, ex);
	if (camel_exception_is_set(ex))
		return (CamelFolderView *)view;

	if (vid == NULL) {
		g_assert(s->root_view == NULL);
		name = s->folder->full_name;
	} else {
		name = g_alloca(strlen(s->folder->full_name)+strlen(vid)+2);
		sprintf(name, "%s:%s", s->folder->full_name, vid);
	}

	printf("opening view db '%s'\n", name);

	view->env = get_env((CamelService *)s->folder->parent_store);
	g_assert(view->env);

	LOCK_VIEW(view);

	db_create(&view->db, view->env->env, 0);
	view->db->app_private = view;
	view->db->set_bt_compare(view->db, cds_dbt_cmp);

	if (vid == NULL) {
		/* root db, we just open it with create */
		err = view->db->open(view->db, NULL, "folders", name, DB_BTREE, DB_CREATE, 0666);
		UNLOCK_VIEW(view);

		if (err != 0)
			camel_exception_setv(ex, 1, "creating database failed", db_strerror(err));
	} else {
		/* secondary db, if this is the first time we've opened it, we need to re-index */
		err = view->db->open(view->db, NULL, "folders", name, DB_BTREE, 0, 0666);
		// FIXME: must check we're getting the right return code ...
		if (err != 0) {
			view->db->close(view->db, 0);
			view->db = NULL;
			db_create(&view->db, view->env->env, 0);
			view->db->app_private = view;
			view->db->set_bt_compare(view->db, cds_dbt_cmp);
			err = view->db->open(view->db, NULL, "folders", name, DB_BTREE, DB_CREATE, 0666);
			UNLOCK_VIEW(view);
			if (err != 0)
				camel_exception_setv(ex, 1, "creating database failed", db_strerror(err));
			else
				// TODO: If this was a view that code other than load_header
				// created, we need to fire off a job to rebuild it?
				view->build = 1;
		} else
			UNLOCK_VIEW(view);

	}

	return (CamelFolderView *)view;
}

static void
cds_view_delete(CamelFolderSummary *s, CamelFolderView *view)
{
	/* We don't do anything here yet, we will check the 'delete' flag when we unref, so
	   we can cleanly destroy the database */
	cds_parent->view_delete(s, view);
}

static void
cds_view_free(CamelFolderSummary *s, CamelFolderView *view)
{
	printf("freeing view/closing db '%s'\n", view->vid?view->vid:"root view");

	LOCK_VIEW(view);

	((CamelFolderViewDisk *)view)->db->close(((CamelFolderViewDisk *)view)->db, 0);

	if (view->deleted) {
		DB *db;
		char *name;

		name = g_alloca(strlen(s->folder->full_name)+strlen(view->vid)+2);
		sprintf(name, "%s:%s", s->folder->full_name, view->vid);

		printf("The view '%s' was deleted, doing it\n", name);

		db_create(&db, ((CamelFolderViewDisk *)view)->env->env, 0);
		db->remove(db, "folders", name, 0);
	}

	UNLOCK_VIEW(view);

	cds_parent->view_free(s, view);
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
	/* FIXME: must poll with a cancellation fd actually */
	sleep(2);

	p->sync_queued = FALSE;

	printf(" sync thread timeout, flushing changes\n");
	camel_folder_summary_disk_sync(m->summary, &msg->ex);
}

static void
cds_sync_free(CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _sync_msg *m = (struct _sync_msg *)msg;

	camel_object_unref(((CamelFolderSummary *)m->summary)->folder);
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

	printf("%p: uid %s changed\n", mi, camel_message_info_uid(mi));

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

		/* we have to ref the folder as it is the owner of the summary too */
		m = camel_session_thread_msg_new(session, &cds_sync_ops, sizeof(*m));
		m->summary = (CamelFolderSummaryDisk *)mi->summary;
		camel_object_ref(((CamelFolderSummary *)m->summary)->folder);
		camel_session_thread_queue(session, &m->msg, 0);
	}
}

/* we use the base implementation but check for changes so we can flush them at some point */
static gboolean cds_info_set_user_flag(CamelMessageInfo *mi, const char *id, gboolean state)
{
	int res = cds_parent->info_set_user_flag(mi, id, state);

	if (res && mi->uid)
		cds_change_info(mi);

	return res;
}

static gboolean cds_info_set_user_tag(CamelMessageInfo *mi, const char *id, const char *val)
{
	int res = cds_parent->info_set_user_tag(mi, id, val);

	if (res && mi->uid)
		cds_change_info(mi);

	return res;
}

static gboolean cds_info_set_flags(CamelMessageInfo *mi, guint32 mask, guint32 set)
{
	int res = cds_parent->info_set_flags(mi, mask, set);

	if (res && mi->uid)
		cds_change_info(mi);

	return res;
}

static void
cds_encode_view(CamelFolderSummaryDisk *cds, CamelFolderView *view, CamelRecordEncoder *cde)
{
	camel_record_encoder_start_section(cde, CFSD_SECTION_FOLDERINFO, 0);

	camel_record_encoder_string(cde, view->vid);
	camel_record_encoder_string(cde, view->expr);

	camel_record_encoder_int32(cde, view->total_count);
	camel_record_encoder_int32(cde, view->visible_count);
	camel_record_encoder_int32(cde, view->unread_count);
	camel_record_encoder_int32(cde, view->deleted_count);
	camel_record_encoder_int32(cde, view->junk_count);

	camel_record_encoder_end_section(cde);
}

/* Do we need locking on this stuff?  matching the summary locking on them? */
static void cds_encode_header(CamelFolderSummaryDisk *cds, CamelRecordEncoder *cde)
{
	CamelFolderView *view;

	cds_encode_view(cds, ((CamelFolderSummary *)cds)->root_view, cde);

	view = (CamelFolderView *)((CamelFolderSummary *)cds)->views.head;
	while (view->next) {
		cds_encode_view(cds, view, cde);
		view = view->next;
	}
}

static int cds_decode_header(CamelFolderSummaryDisk *cds, CamelRecordDecoder *crd)
{
	int tag, ver;

	camel_record_decoder_reset(crd);
	while ((tag = camel_record_decoder_next_section(crd, &ver)) != CR_SECTION_INVALID) {
		switch (tag) {
		case CFSD_SECTION_FOLDERINFO: {
			CamelFolderView *view;
			char *vid;
			const char *tmp;

			tmp = camel_record_decoder_string(crd);
			if (tmp[0] == 0) {
				view = ((CamelFolderSummary *)cds)->root_view;
				/* expression is discarded for root view */
				tmp = camel_record_decoder_string(crd);
			} else {
				CamelException ex = { 0 };

				vid = g_strdup(tmp);
				tmp = camel_record_decoder_string(crd);
				/* view already exists/or can't be created, skip the rest */
				view = (CamelFolderView *)camel_folder_summary_view_create((CamelFolderSummary *)cds, vid, tmp, &ex);
				g_free(vid);
				camel_exception_clear(&ex);
				if (view == NULL)
					break;
			}

			view->total_count = camel_record_decoder_int32(crd);
			view->visible_count = camel_record_decoder_int32(crd);
			view->unread_count = camel_record_decoder_int32(crd);
			view->deleted_count = camel_record_decoder_int32(crd);
			view->junk_count = camel_record_decoder_int32(crd); 
			break; }
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
	CamelFolderViewDisk *root = (CamelFolderViewDisk *)((CamelFolderSummary *)cds)->root_view;
	int i;

	/* TODO: use transactions? */
	/* TODO: what to do about failures? */

	camel_operation_start(NULL, _("Storing changes"));

	for (i=0;i<infos->len;i++) {
		camel_operation_progress(NULL, i*100/infos->len);
		cds_save_info(cds, (CamelMessageInfo *)infos->pdata[i], 0);
		cds_update_views_change(cds, (CamelMessageInfo *)infos->pdata[i]);
	}

	cds_save_header(cds);

	LOCK_VIEW(root);
	root->db->sync(root->db, 0);
	UNLOCK_VIEW(root);

	camel_operation_end(NULL);
}

static void
camel_folder_summary_disk_class_init(CamelFolderSummaryDiskClass *klass)
{
	cds_parent = (CamelFolderSummaryClass *)camel_folder_summary_get_type();

	((CamelFolderSummaryClass *)klass)->view_sizeof = sizeof(CamelFolderViewDisk);

	((CamelFolderSummaryClass *)klass)->uid_cmp = cds_uid_cmp;
	((CamelFolderSummaryClass *)klass)->info_cmp = cds_info_cmp;

	((CamelFolderSummaryClass *)klass)->rename = cds_rename;

	((CamelFolderSummaryClass *)klass)->add = cds_add;
	((CamelFolderSummaryClass *)klass)->remove = cds_remove;
	((CamelFolderSummaryClass *)klass)->clear = cds_clear;
	((CamelFolderSummaryClass *)klass)->message_info_free = cds_message_info_free;

	((CamelFolderSummaryClass *)klass)->get = cds_get;

	((CamelFolderSummaryClass *)klass)->search = cds_search;

	((CamelFolderSummaryClass *)klass)->view_create = cds_view_create;
	((CamelFolderSummaryClass *)klass)->view_delete = cds_view_delete;
	((CamelFolderSummaryClass *)klass)->view_free = cds_view_free;

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

CamelFolderSummaryDisk *
camel_folder_summary_disk_construct(CamelFolderSummaryDisk *cds, struct _CamelFolder *folder)
{
	CamelFolderSummary *s = (CamelFolderSummary *)cds;
	CamelException ex = { 0 };

	s->folder = folder;

	/* todo: make this overridable, setup in init func probably */
	s->search = camel_folder_search_new();

	printf("init root view\n");
	camel_folder_summary_view_create(s, NULL, NULL, &ex);
	// check ex

	printf("constructing summary, loading header\n");
	cds_load_header(cds);

	/* We init these extra views after loading the header data, if we already had them, this means
	   they will just be ignored now, if we set them up before, it means we'd lose the counts */

	printf("init trash view\n");
	camel_folder_summary_view_create(s, "#.trash", "(system-flag \"Deleted\")", &ex);
	camel_exception_clear(&ex);
	printf("init junk view\n");
	camel_folder_summary_view_create(s, "#.junk", "(system-flag \"Junk\")", &ex);
	camel_exception_clear(&ex);

	/* add some 'vfolders' */
	camel_folder_summary_view_create(s, "unread", "(not (system-flag \"Seen\"))", &ex);
	camel_exception_clear(&ex);
	camel_folder_summary_view_create(s, "evolution-hackers", "(header-contains \"subject\" \"[evolution-hackers]\")", &ex);
	camel_exception_clear(&ex);
	camel_folder_summary_view_create(s, "evolution", "(header-contains \"subject\" \"[hackers]\")", &ex);
	camel_exception_clear(&ex);
	camel_folder_summary_view_create(s, "to.notzed", "(header-contains \"to\" \"notzed\")", &ex);
	camel_exception_clear(&ex);
	camel_folder_summary_view_create(s, "from.notzed", "(header-contains \"from\" \"notzed\")", &ex);
	camel_exception_clear(&ex);

	/* Now build any views we need to at all at once */
	cds_view_build_all(cds, &ex);
	camel_exception_clear(&ex);

	/* sync? */

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

/* Internal accessor for an iterator from this summary.

   This is only valid for iterators that came from a search on this
   summary, with no expression and no subset iterator.  It can be used
   only by subclasses for finer control of the cursor.

   flags0 are the flags to go to the next record, and flags1 to
   go to the next one after that, if the first one is empty/invalid.

   e.g. use DB_LAST, DB_PREV to get the last record

   This needs more thought, we should be able to skip to any record as well
*/

const CamelMessageInfo *camel_message_iterator_disk_get(void *mitin, guint32 flags0, guint32 flags1, CamelException *ex)
{
	return cds_iterator_step(mitin, flags0, flags1, ex);
}
