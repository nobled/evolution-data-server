
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>

#include <glib.h>

#include "camel-view-summary.h"

#define CVS_CLASS(x) ((CamelViewSummaryClass *)((CamelObject *)x)->klass)

static CamelObjectClass *cvs_parent;

CamelView *
camel_view_new(CamelViewSummary *s, const char *vid)
{
	CamelView *view;

	view = g_malloc0(CVS_CLASS(s)->view_sizeof);
	view->refcount = 1;
	view->summary = s;
	view->vid = g_strdup(vid);

	return view;
}

void
camel_view_ref(CamelView *v)
{
	g_assert(v->refcount > 0);

	v->refcount++;
}

void
camel_view_unref(CamelView *v)
{
	g_assert(v->refcount > 0);

	v->refcount--;
	if (v->refcount == 0)
		CVS_CLASS(v->summary)->free(v->summary, v);
}

void
camel_view_changed(CamelView *v)
{
	if (!v->deleted)
		CVS_CLASS(v->summary)->changed(v->summary, v);
}

void camel_view_summary_add(CamelViewSummary *s, CamelView *view, CamelException *ex)
{
	CVS_CLASS(s)->add(s, view, ex);
}

void camel_view_summary_remove(CamelViewSummary *s, CamelView *view)
{
	view->deleted = 1;
	CVS_CLASS(s)->remove(s, view);
}

CamelView *camel_view_summary_get(CamelViewSummary *s, const char *vid)
{
	return CVS_CLASS(s)->get(s, vid);
}

CamelIterator *camel_view_summary_search(CamelViewSummary *s, const char *root, const char *expr, CamelException *ex)
{
	return CVS_CLASS(s)->search(s, root, expr, ex);
}

/* ********************************************************************** */
#if 0
static void
cvs_view_add(CamelViewSummary *s, CamelViewRoot *root, CamelView *view, CamelException *ex)
{
	if (root) {
		CamelViewView *vview = (CamelViewView *)view;

		g_assert(!view->root);
		e_dlist_addtail(&root->views, (EDListNode *)view);

		vview->iter = camel_folder_search_search(s->search, vview->expr, NULL, ex);
		vview->is_static = camel_folder_search_is_static(s->search, vview->expr, NULL);
	} else {
		g_assert(view->root);
		e_dlist_addtail(&s->views, (EDListNode *)view);
	}
}

static void
cvs_view_delete(CamelViewSummary *s, CamelView *view)
{
	/* nothing, already removed elsewhere */
}

static void
cvs_view_free(CamelViewSummary *s, CamelView *view)
{
	if (!view->root) {
		CamelViewView *vview = (CamelViewView *)view;

		if (vview->iter)
			camel_iterator_free((CamelIterator *)vview->iter);
		g_free(vview->expr);
	}

	if (view->changes)
		camel_folder_change_info_free(view->changes);
	g_free(view->vid);
	g_free(view);
}
#endif

static void
cvs_add(CamelViewSummary *s, CamelView *view, CamelException *ex)
{
}

static void
cvs_free(CamelViewSummary *s, CamelView *view)
{
	g_free(view->vid);
	g_free(view->expr);
	g_free(view);
}

static void
cvs_changed(CamelViewSummary *s, CamelView *view)
{
//	printf("view '%s' changed\n", view->vid);
}

static void
camel_view_summary_class_init(CamelViewSummaryClass *klass)
{
	klass->view_sizeof = sizeof(CamelView);
	klass->add = cvs_add;
	klass->free = cvs_free;
	klass->changed = cvs_changed;
}

static void
camel_view_summary_init(CamelViewSummary *s)
{
}

static void
camel_view_summary_finalise(CamelObject *obj)
{
	CamelViewSummary *s = (CamelViewSummary *)obj;

	s = s;
}

CamelType
camel_view_summary_get_type(void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		cvs_parent = camel_object_get_type();
		type = camel_type_register(cvs_parent, "CamelViewSummary",
					   sizeof(CamelViewSummary),
					   sizeof(CamelViewSummaryClass),
					   (CamelObjectClassInitFunc)camel_view_summary_class_init,
					   NULL,
					   (CamelObjectInitFunc)camel_view_summary_init,
					   (CamelObjectFinalizeFunc)camel_view_summary_finalise);
	}
	
	return type;
}
