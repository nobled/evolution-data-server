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

#ifndef _CAMEL_MBOX_VIEW_SUMMARY_H
#define _CAMEL_MBOX_VIEW_SUMMARY_H

#include "camel-view-summary-disk.h"

typedef struct _CamelMBOXViewSummary      CamelMBOXViewSummary;
typedef struct _CamelMBOXViewSummaryClass CamelMBOXViewSummaryClass;

enum {
	CVS_MBOX_SECTION_VIEWINFO = CVSD_SECTION_LAST,
};

typedef struct _CamelMBOXView CamelMBOXView;

struct _CamelMBOXView {
	CamelViewDisk view;

	/* This data is only set on the root views ? */

	size_t folder_size;	/* size of the mbox file, last sync */
	time_t time;		/* time of mbox last sync */
	guint32 nextuid;	/* the next uid to assign */
};

struct _CamelMBOXViewSummary {
	CamelViewSummaryDisk parent;
};

struct _CamelMBOXViewSummaryClass {
	CamelViewSummaryDiskClass parent_class;
};

CamelType		camel_mbox_view_summary_get_type	(void);
CamelMBOXViewSummary      *camel_mbox_view_summary_new	(const char *base, CamelException *ex);

/* called on root view */
guint32 camel_mbox_view_next_uid(CamelMBOXView *view);
void camel_mbox_view_last_uid(CamelMBOXView *view, guint32 uid);

#endif /* ! _CAMEL_MBOX_VIEW_SUMMARY_H */

