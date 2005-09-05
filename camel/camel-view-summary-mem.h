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

#ifndef _CAMEL_VIEW_SUMMARY_MEM_H
#define _CAMEL_VIEW_SUMMARY_MEM_H

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#include <camel/camel-view-summary.h>

typedef struct _CamelViewSummaryMem CamelViewSummaryMem;
typedef struct _CamelViewSummaryMemClass CamelViewSummaryMemClass;
typedef struct _CamelViewMem CamelViewMem;

struct _CamelViewMem {
	CamelView view;
};

struct _CamelViewSummaryMem {
	CamelViewSummary summary;

	/* <private> */
	GMutex *lock;
	/* GTree sucks, but what can you do eh?  We want an ordered hash, saves sorting each time */
	GTree *views;
};

struct _CamelViewSummaryMemClass {
	CamelViewSummaryClass summary_class;
};

CamelType	camel_view_summary_mem_get_type	(void);
CamelViewSummaryMem *camel_view_summary_mem_new(void);

#define CVSM_LOCK(x) (g_mutex_lock(((CamelViewSummaryMem *)x)->lock))
#define CVSM_UNLOCK(x) (g_mutex_unlock(((CamelViewSummaryMem *)x)->lock))

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ! _CAMEL_VIEW_SUMMARY_MEM_VIEW_H */
