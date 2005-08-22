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

#ifndef _CAMEL_VIEW_SUMMARY_DISK_H
#define _CAMEL_VIEW_SUMMARY_DISK_H

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#include <camel/camel-view-summary.h>
#include "libdb/dist/db.h"

struct _CamelRecordEncoder;
struct _CamelRecordDecoder;

typedef struct _CamelViewSummaryDisk CamelViewSummaryDisk;
typedef struct _CamelViewSummaryDiskClass CamelViewSummaryDiskClass;
typedef struct _CamelViewDisk CamelViewDisk;

enum {
	CVSD_SECTION_VIEWINFO = 1,
	CVSD_SECTION_LAST = 16
};

struct _CamelViewDisk {
	CamelView view;
};

struct _CamelViewSummaryDisk {
	CamelViewSummary summary;

	/* libdb environment path */
	char *path;

	/* we lock every lib db call on this environment through this lock; every one! */
	GMutex *lock;
	DB *views;
	DB_ENV *env;

	/* private */
	GHashTable *cache;
	GHashTable *changed;
};

struct _CamelViewSummaryDiskClass {
	CamelViewSummaryClass summary_class;

	void (*encode)(CamelViewSummaryDisk *, struct _CamelView *, struct _CamelRecordEncoder *);
	int (*decode)(CamelViewSummaryDisk *, struct _CamelView *, struct _CamelRecordDecoder *);
};

CamelType	camel_view_summary_disk_get_type	(void);

CamelViewSummaryDisk *camel_view_summary_disk_construct(CamelViewSummaryDisk *cvsd, const char *base, CamelException *ex);
CamelViewSummaryDisk *camel_view_summary_disk_new(const char *base, CamelException *ex);

void camel_view_summary_disk_sync(CamelViewSummaryDisk *cds, CamelException *ex);

#define CVSD_LOCK_ENV(s) (g_mutex_lock(((CamelViewSummaryDisk *)s)->lock))
#define CVSD_UNLOCK_ENV(s) (g_mutex_unlock(((CamelViewSummaryDisk *)s)->lock))

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ! _CAMEL_VIEW_SUMMARY_DISK_VIEW_H */
