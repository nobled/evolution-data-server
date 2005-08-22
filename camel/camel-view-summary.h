/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2005 Novell Inc.
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

#ifndef _CAMEL_VIEW_SUMMARY_H
#define _CAMEL_VIEW_SUMMARY_H

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#include <glib.h>
#include <camel/camel-object.h>

typedef struct _CamelViewSummary CamelViewSummary;
typedef struct _CamelViewSummaryClass CamelViewSummaryClass;
typedef struct _CamelView CamelView;

struct _CamelView {
	CamelViewSummary *summary;

	char *vid;		/* consists of "folder name\01view name" */
	char *expr;

	/* ref them for threading purposes */
	guint32 refcount:29;

	/* set when we're deleted, so we can abort/do the right thing if we have open cursors */
	guint32 deleted:1;
	/* we must rebuild this view, the expression changed */
	guint32 rebuild:1;
	/* we must save this view, information in it has changed */
	guint32 changed:1;

	/* handy totals */
	guint32 total_count;
	guint32 visible_count;
	guint32 unread_count;
	guint32 deleted_count;
	guint32 junk_count;
};

struct _CamelViewSummary {
	CamelObject cobject;
};

struct _CamelViewSummaryClass {
	CamelObjectClass cobject_class;

	size_t view_sizeof;

	void (*add)(CamelViewSummary *, CamelView *, CamelException *ex);
	void (*remove)(CamelViewSummary *, CamelView *);
	void (*free)(CamelViewSummary *, CamelView *);
	void (*changed)(CamelViewSummary *, CamelView *);

	CamelView *(*get)(CamelViewSummary *, const char *);
	CamelIterator *(*search)(CamelViewSummary *, const char *root, const char *, CamelException *);
};

CamelType	camel_view_summary_get_type	(void);

CamelView *camel_view_summary_get(CamelViewSummary *s, const char *vid);
CamelIterator *camel_view_summary_search(CamelViewSummary *s, const char *root, const char *expr, CamelException *ex);

void camel_view_summary_add(CamelViewSummary *s, CamelView *, CamelException *ex);
void camel_view_summary_remove(CamelViewSummary *s, CamelView *);

CamelView *camel_view_new(CamelViewSummary *, const char *);
void camel_view_ref(CamelView *);
void camel_view_unref(CamelView *);
void camel_view_changed(CamelView *);

#if 0
const CamelView *camel_view_summary_view_create(CamelViewSummary *s, const char *vid, const char *expr, CamelException *ex);
CamelView *camel_view_summary_view_new(CamelViewSummary *s, const char *vid);
CamelView *camel_view_summary_view_lookup(CamelViewSummary *s, const char *vid);
void camel_view_summary_view_unref(CamelView *v);
void camel_view_summary_view_delete(CamelViewSummary *s, const char *vid);
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ! _CAMEL_VIEW_SUMMARY_VIEW_H */
