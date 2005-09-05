

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>

#include <glib.h>

#include "camel-view-summary-mem.h"
#include "camel-exception.h"

#define CVSM_CLASS(x) ((CamelViewSummaryMemClass *)((CamelObject *)x)->klass)
#define CVSM(x) ((CamelViewSummaryMem *)x)
#define CVS(x) ((CamelViewSummary *)x)

#define v(x) x

static CamelViewSummaryClass *cvsd_parent;

/* ********************************************************************** */

CamelViewSummaryMem *camel_view_summary_mem_new(void)
{
	CamelViewSummaryMem *cvsd = camel_object_new(camel_view_summary_mem_get_type());

	return cvsd;
}

/* ******************************************************************************** */

static void cvsd_add(CamelViewSummary *s, CamelView *v, CamelException *ex)
{
	CVSM_LOCK(s);

	if (g_tree_lookup(CVSM(s)->views, v->vid) == NULL)
		g_tree_insert(CVSM(s)->views, v->vid, v);

	CVSM_UNLOCK(s);
}

static void cvsd_remove(CamelViewSummary *s, CamelView *v)
{
	CVSM_LOCK(s);

	g_tree_remove(CVSM(s)->views, v->vid);

	CVSM_UNLOCK(s);
}

static CamelView *cvsd_get(CamelViewSummary *s, const char *vid)
{
	CamelView *view;

	CVSM_LOCK(s);

	view = g_tree_lookup(CVSM(s)->views, vid);
	if (view)
		view->refcount++;

	CVSM_UNLOCK(s);

	return view;
}

struct _cvsd_iterator {
	CamelIterator iter;

	CamelViewSummary *summary;

	char *root;
	char *expr;

	int index;
	GPtrArray *views;
};

static void cvsd_search_free(void *it)
{
	struct _cvsd_iterator *iter = it;
	int i;

	CVSM_LOCK(iter->summary);

	for (i=0;i<iter->views->len;i++)
		camel_view_unref(iter->views->pdata[i]);

	CVSM_UNLOCK(iter->summary);

	g_free(iter->root);
	g_free(iter->expr);
	g_ptr_array_free(iter->views, TRUE);
	camel_object_unref(iter->summary);
}

static const void * cvsd_search_next(void *it, CamelException *ex)
{
	struct _cvsd_iterator *iter = it;
	int len;
	CamelView *view;

	if (iter->root)
		len = strlen(iter->root);

	while (iter->index < iter->views->len) {
		view = iter->views->pdata[iter->index++];
		if (iter->root == NULL
		    || (strlen(view->vid) >= len
			&& memcmp(view->vid, iter->root, len) == 0
			&& (view->vid[len] == 0 || view->vid[len] == 1)))
			return view;
	}

	return NULL;
}

static void cvsd_search_reset(void *it)
{
	struct _cvsd_iterator *iter = it;

	iter->index = 0;
}

static CamelIteratorVTable cvsd_search_vtable = {
	cvsd_search_free,
	cvsd_search_next,
	cvsd_search_reset,
};

static int
cvsm_get_views(void *k, void *v, void *d)
{
	struct _cvsd_iterator *iter = d;

	camel_view_ref(v);
	g_ptr_array_add(iter->views, v);

	return FALSE;
}

static CamelIterator *cvsd_search(CamelViewSummary *s, const char *root, const char *expr, CamelException *ex)
{
	struct _cvsd_iterator *iter;

	iter = camel_iterator_new(&cvsd_search_vtable, sizeof(*iter));
	iter->root = g_strdup(root);
	iter->expr = g_strdup(expr);
	iter->summary = s;
	camel_object_ref(s);
	iter->index = 0;
	iter->views = g_ptr_array_new();

	CVSM_LOCK(s);
	g_tree_foreach(CVSM(s)->views, cvsm_get_views, iter);
	CVSM_UNLOCK(s);

	return (CamelIterator *)iter;
}

/* ********************************************************************** */

static void
camel_view_summary_mem_class_init(CamelViewSummaryClass *klass)
{
	klass->view_sizeof = sizeof(CamelView);

	klass->add = cvsd_add;
	klass->remove = cvsd_remove;

	klass->get = cvsd_get;
	klass->search = cvsd_search;
}

static int
cvsm_cmp(const void *ap, const void *bp)
{
	return strcmp(ap, bp);
}

static void
camel_view_summary_mem_init(CamelViewSummaryMem *cvsd)
{
	cvsd->lock = g_mutex_new();
	cvsd->views = g_tree_new(cvsm_cmp);
}

static int
cvsm_free_views(void *k, void *v, void *d)
{
	camel_view_unref(v);
	return FALSE;
}

static void
camel_view_summary_mem_finalise(CamelObject *obj)
{
	CamelViewSummaryMem *cvsd = (CamelViewSummaryMem *)obj;

	g_mutex_free(cvsd->lock);
	g_tree_foreach(cvsd->views, cvsm_free_views, NULL);
	g_tree_destroy(cvsd->views);
}

CamelType
camel_view_summary_mem_get_type(void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		cvsd_parent = (CamelViewSummaryClass *)camel_view_summary_get_type();
		type = camel_type_register((CamelType)cvsd_parent, "CamelViewSummaryMem",
					   sizeof(CamelViewSummaryMem),
					   sizeof(CamelViewSummaryMemClass),
					   (CamelObjectClassInitFunc)camel_view_summary_mem_class_init,
					   NULL,
					   (CamelObjectInitFunc)camel_view_summary_mem_init,
					   (CamelObjectFinalizeFunc)camel_view_summary_mem_finalise);
	}
	
	return type;
}
