

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>

#include <glib.h>

#include "camel-view-summary-disk.h"
#include "camel-record.h"
#include "camel-exception.h"

#define CVSD_CLASS(x) ((CamelViewSummaryDiskClass *)((CamelObject *)x)->klass)
#define CVSD(x) ((CamelViewSummaryDisk *)x)
#define CVS(x) ((CamelViewSummary *)x)

#define v(x) x

static int cvsd_save_view(CamelViewSummaryDisk *cds, CamelViewDisk *view, DB_TXN *txn, guint32 flags, CamelException *ex);

static CamelViewSummaryClass *cvsd_parent;

/* ********************************************************************** */

CamelViewSummaryDisk *camel_view_summary_disk_construct(CamelViewSummaryDisk *cvsd, const char *base, CamelException *ex)
{
	int err;

	printf("Creating db environment for store at '%s'\n", base);

	cvsd->path = g_strdup(base);

	if ((err = db_env_create(&cvsd->env, 0)) != 0) {
		camel_exception_setv(ex, 2, "env create failed: %s", db_strerror(err));
		goto fail;
	}

	if ((err = cvsd->env->open(cvsd->env, base, DB_INIT_LOG|DB_INIT_MPOOL|DB_INIT_TXN|DB_CREATE|DB_RECOVER /* |DB_PRIVATE */, 0666)) != 0) {
		camel_exception_setv(ex, 2, "env open failed: %s", db_strerror(err));
		goto fail;
	}

	/* Open the folder list 'table' */
	db_create(&cvsd->views, cvsd->env, 0);
	if ((err = cvsd->views->open(cvsd->views, NULL, "views", ".#views", DB_BTREE, DB_CREATE, 0666)) != 0) {
		camel_exception_setv(ex, 2, "folder list database create failed: %s", db_strerror(err));
		goto fail;
	}

	return cvsd;
fail:
	camel_object_unref(cvsd);
	return NULL;
}

CamelViewSummaryDisk *camel_view_summary_disk_new(const char *base, CamelException *ex)
{
	CamelViewSummaryDisk *cvsd = camel_object_new(camel_view_summary_disk_get_type());

	return camel_view_summary_disk_construct(cvsd, base, ex);
}


static int
cvsd_get_changed(void *k, void *v, void *d)
{
	GPtrArray *infos = d;

	g_ptr_array_add(infos, v);

	return TRUE;
}

static int
cvsd_array_view_cmp(const void *ap, const void *bp, void *data)
{
	const CamelView *a = ((const CamelView **)ap)[0];
	const CamelView *b = ((const CamelView **)bp)[0];

	return strcmp(a->vid, b->vid);
}

void camel_view_summary_disk_sync(CamelViewSummaryDisk *cds, CamelException *ex)
{
	GPtrArray *infos;
	int i;

	printf("syncing this stupid crap!!!  but wtf does it go eh???\n");

	infos = g_ptr_array_new();
	CVSD_LOCK_ENV(cds);
	g_hash_table_foreach_remove(cds->changed, cvsd_get_changed, infos);
	CVSD_UNLOCK_ENV(cds);

	/* sorted for your convenience */
	g_qsort_with_data(infos->pdata, infos->len, sizeof(infos->pdata[0]), cvsd_array_view_cmp, cds);

	/* Should call virtual sync? */
	//CDS_CLASS(cds)->sync(cds, infos, ex);

	for (i=0;!camel_exception_is_set(ex) && i<infos->len;i++) {
		CamelViewDisk *view = infos->pdata[i];
		cvsd_save_view(cds, view, NULL, 0, ex);
		camel_view_unref((CamelView *)view);
	}

	g_ptr_array_free(infos, TRUE);

	CVSD_LOCK_ENV(cds);
	cds->views->sync(cds->views, 0);
	CVSD_UNLOCK_ENV(cds);
}

/* ******************************************************************************** */

static int
cvsd_save_view(CamelViewSummaryDisk *cds, CamelViewDisk *view, DB_TXN *txn, guint32 flags, CamelException *ex)
{
	CamelRecordEncoder *cre;
	DBT key = { 0 }, data = { 0 };
	int res;

	cre = camel_record_encoder_new();
	CVSD_CLASS(cds)->encode(cds, (CamelView *)view, cre);

	key.data = view->view.vid;
	key.size = strlen(key.data);
	data.data = cre->out->data;
	data.size = cre->out->len;

	v(printf("saving view '%s'\n", (char *)key.data));

	CVSD_LOCK_ENV(cds);
	res = cds->views->put(cds->views, txn, &key, &data, flags);
	if (res == 0) {
		view->view.changed = 0;
		if (flags == DB_NOOVERWRITE)
			g_hash_table_insert(cds->cache, view->view.vid, view);
	} else
		camel_exception_setv(ex, 1, "creating view database failed: %s", db_strerror(res));

	CVSD_UNLOCK_ENV(cds);
	camel_record_encoder_free(cre);

	return res;
}

static CamelView *cvsd_get_record(CamelViewSummaryDisk *s, DBT *key, DBT *data)
{
	CamelView *v;
	CamelRecordDecoder *crd;
	char *vid;

	vid = g_alloca(key->size+1);
	memcpy(vid, key->data, key->size);
	vid[key->size] = 0;

	v = camel_view_new((CamelViewSummary *)s, vid);
	crd = camel_record_decoder_new(data->data, data->size);
	if (CVSD_CLASS(s)->decode((CamelViewSummaryDisk *)s, v, crd) != 0) {
		camel_view_unref(v);
		v = NULL;
	} else {
		g_hash_table_insert(s->cache, v->vid, v);
	}
	camel_record_decoder_free(crd);

	return v;
}

/* ********************************************************************** */

static void cvsd_changed(CamelViewSummary *s, CamelView *v)
{
	// FIXME: if its changed we need to queue up a sync/save job?

	CVSD_LOCK_ENV(s);
	if (g_hash_table_lookup(CVSD(s)->changed, v->vid) == NULL) {
		v->refcount++;
		g_hash_table_insert(CVSD(s)->changed, v->vid, v);
	}
	CVSD_UNLOCK_ENV(s);

	cvsd_parent->changed(s, v);
}

static void cvsd_add(CamelViewSummary *s, CamelView *v, CamelException *ex)
{
	int res;

	/* NOOVERWRITE ensures we dont get duplicates */

	res = cvsd_save_view((CamelViewSummaryDisk *)s, (CamelViewDisk *)v, NULL, DB_NOOVERWRITE, ex);

	if (res == 0)
		cvsd_parent->add(s, v, ex);
}

static void cvsd_remove(CamelViewSummary *s, CamelView *v)
{
	DBT key = { 0 };
	int res;

	key.data = v->vid;
	key.size = strlen(v->vid);

	CVSD_LOCK_ENV(s);
	g_hash_table_remove(((CamelViewSummaryDisk *)s)->cache, v->vid);
	// FIXME:L need to unref it if we remove from changed
	//g_hash_table_remove(((CamelViewSummaryDisk *)s)->changed, v->vid);
	res = CVSD(s)->views->del(CVSD(s)->views, NULL, &key, 0);
	CVSD_UNLOCK_ENV(s);

	cvsd_parent->remove(s, v);
}

static void cvsd_free(CamelViewSummary *s, CamelView *view)
{
	v(printf("freeing view/closing db '%s'\n", view->vid?view->vid:"root view"));
	cvsd_parent->free(s, view);
}

static CamelView *cvsd_get(CamelViewSummary *s, const char *vid)
{
	DBT key = { 0 }, data = { 0 };
	int res;
	CamelView *view;

	CVSD_LOCK_ENV(s);
	view = g_hash_table_lookup(CVSD(s)->cache, vid);
	if (view && view->refcount != 0) {
		view->refcount++;
	} else {
		key.data = (char *)vid;
		key.size = strlen(vid);
		data.flags = DB_DBT_MALLOC;

		res = CVSD(s)->views->get(CVSD(s)->views, NULL, &key, &data, 0);
		if (res == 0)
			view = cvsd_get_record(CVSD(s), &key, &data);
		else
			view = NULL;
	}
	CVSD_UNLOCK_ENV(s);

	if (data.data)
		free(data.data);

	return view;
}

struct _cvsd_iterator {
	CamelIterator iter;

	CamelViewSummary *summary;

	char *root;

	DBC *cursor;
	DBT key;
	DBT data;

	CamelView *current;

	int reset:1;
};

static void cvsd_search_free(void *it)
{
	struct _cvsd_iterator *iter = it;

	if (iter->cursor) {
		CVSD_LOCK_ENV(iter->summary);
		iter->cursor->c_close(iter->cursor);
		CVSD_UNLOCK_ENV(iter->summary);
		if (iter->data.data)
			free(iter->data.data);
		if (iter->key.data)
			free(iter->key.data);
	}
	
	if (iter->current)
		camel_view_unref(iter->current);

	camel_object_unref(iter->summary);
	g_free(iter->root);
}

static const void * cvsd_search_next(void *it, CamelException *ex)
{
	struct _cvsd_iterator *iter = it;
	int len, res;

	if (iter->cursor == NULL)
		return NULL;

	if (iter->current){
		camel_view_unref(iter->current);
		iter->current = NULL;
	}

	if (iter->root)
		len = strlen(iter->root);

	/* We lock while we're doing some cpu-bound processing; which we should
	   avoid, but it is fast and it simplifies get_record considerably */

	CVSD_LOCK_ENV(iter->summary);

	if (iter->reset && iter->root) {
		/* NB: realloc, not g_realloc, for libdb */
		iter->key.data = realloc(iter->key.data, len);
		memcpy(iter->key.data, iter->root, len);
		iter->key.size = len;
		res = iter->cursor->c_get(iter->cursor, &iter->key, &iter->data, DB_SET);
	} else {
		res = iter->cursor->c_get(iter->cursor, &iter->key, &iter->data, iter->reset?DB_FIRST:DB_NEXT);
		if (res == 0)
			printf("got record: %.*s\n", iter->key.size, iter->key.data);
		if (res == 0
		    && iter->root != NULL
		    && (iter->key.size < len
			|| memcmp(iter->root, iter->key.data, len) != 0
			|| iter->key.size <= len
			|| ((char *)iter->key.data)[len] != 1))
			res = DB_NOTFOUND;
	}

	iter->reset = 0;

	if (res == 0) {
		char *vid = g_alloca(iter->key.size+1);

		memcpy(vid, iter->key.data, iter->key.size);
		vid[iter->key.size] = 0;
		iter->current = g_hash_table_lookup(CVSD(iter->summary)->cache, vid);
		if (iter->current && iter->current->refcount != 0)
			iter->current->refcount++;
		else
			iter->current = cvsd_get_record(CVSD(iter->summary), &iter->key, &iter->data);
	} else if (res != DB_NOTFOUND)
		/* set exception */
		printf("Got error walking view cursor: %s\n", db_strerror(res));

	CVSD_UNLOCK_ENV(iter->summary);

	printf("next view '%s' = %s (%s)\n", iter->root?iter->root:"<all>", iter->current?iter->current->vid:"<none>", db_strerror(res));

	return iter->current;
}

static void cvsd_search_reset(void *it)
{
	struct _cvsd_iterator *iter = it;

	iter->reset = 1;
}

static CamelIteratorVTable cvsd_search_vtable = {
	cvsd_search_free,
	cvsd_search_next,
	cvsd_search_reset,
};

static CamelIterator *cvsd_search(CamelViewSummary *s, const char *root, const char *expr, CamelException *ex)
{
	struct _cvsd_iterator *iter;
	CamelViewSummaryDisk *cvsd = (CamelViewSummaryDisk *)s;

	iter = camel_iterator_new(&cvsd_search_vtable, sizeof(*iter));
	iter->summary = s;
	camel_object_ref(s);
	iter->reset = 1;
	iter->root = g_strdup(root);

	if (cvsd->views->cursor(cvsd->views, NULL, &iter->cursor, 0) == 0) {
		iter->key.flags = DB_DBT_REALLOC;
		iter->data.flags = DB_DBT_REALLOC;
	}

	return (CamelIterator *)iter;
}

static void
cvsd_encode(CamelViewSummaryDisk *cds, CamelView *view, CamelRecordEncoder *cde)
{
	camel_record_encoder_start_section(cde, CVSD_SECTION_VIEWINFO, 0);

	camel_record_encoder_string(cde, view->expr);

	camel_record_encoder_int32(cde, view->total_count);
	camel_record_encoder_int32(cde, view->visible_count);
	camel_record_encoder_int32(cde, view->unread_count);
	camel_record_encoder_int32(cde, view->deleted_count);
	camel_record_encoder_int32(cde, view->junk_count);

	camel_record_encoder_end_section(cde);
}

static int
cvsd_decode(CamelViewSummaryDisk *cds, CamelView *view, CamelRecordDecoder *crd)
{
	int tag, ver;

	camel_record_decoder_reset(crd);
	while ((tag = camel_record_decoder_next_section(crd, &ver)) != CR_SECTION_INVALID) {
		switch (tag) {
		case CVSD_SECTION_VIEWINFO: {
			const char *tmp;

			tmp = camel_record_decoder_string(crd);
			if (tmp[0] != 0)
				view->expr = g_strdup(tmp);

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

/* ********************************************************************** */

static void
camel_view_summary_disk_class_init(CamelViewSummaryClass *klass)
{
	klass->view_sizeof = sizeof(CamelViewDisk);

	klass->add = cvsd_add;
	klass->remove = cvsd_remove;
	klass->free = cvsd_free;
	klass->changed = cvsd_changed;

	klass->get = cvsd_get;
	klass->search = cvsd_search;

	((CamelViewSummaryDiskClass *)klass)->encode = cvsd_encode;
	((CamelViewSummaryDiskClass *)klass)->decode = cvsd_decode;
}

static void
camel_view_summary_disk_init(CamelViewSummaryDisk *cvsd)
{
	cvsd->lock = g_mutex_new();

	cvsd->cache = g_hash_table_new(g_str_hash, g_str_equal);
	cvsd->changed = g_hash_table_new(g_str_hash, g_str_equal);
}

static void
camel_view_summary_disk_finalise(CamelObject *obj)
{
	CamelViewSummaryDisk *cvsd = (CamelViewSummaryDisk *)obj;

	g_mutex_free(cvsd->lock);

	/* FIXME: expunge any opened stuff */
	/* FIXME: free cache and changed tables */
}

CamelType
camel_view_summary_disk_get_type(void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		cvsd_parent = (CamelViewSummaryClass *)camel_view_summary_get_type();
		type = camel_type_register((CamelType)cvsd_parent, "CamelViewSummaryDisk",
					   sizeof(CamelViewSummaryDisk),
					   sizeof(CamelViewSummaryDiskClass),
					   (CamelObjectClassInitFunc)camel_view_summary_disk_class_init,
					   NULL,
					   (CamelObjectInitFunc)camel_view_summary_disk_init,
					   (CamelObjectFinalizeFunc)camel_view_summary_disk_finalise);
	}
	
	return type;
}
