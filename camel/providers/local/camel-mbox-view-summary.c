/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*-
 *
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

#include <string.h>

#include "camel-record.h"
#include "camel-mbox-view-summary.h"

/* NB, this is only for the messy iterator_get interface, which could be better hidden */
#include "libdb/dist/db.h"

#define io(x)
#define d(x) (printf("%s(%d): ", __FILE__, __LINE__),(x))

#define CVSD_CLASS(x) ((CamelViewSummaryDiskClass *)((CamelObject *)x)->klass)
#define CVS_CLASS(x) ((CamelViewSummaryClass *)((CamelObject *)x)->klass)

static CamelViewSummaryDiskClass *cmvs_parent;

/*
 * camel_mbox_view_summary_new:
 *
 * Create a new CamelMBOXViewSummary object.
 * 
 * Return value: A new CamelMBOXViewSummary widget.
 **/
CamelMBOXViewSummary *
camel_mbox_view_summary_new(const char *base, CamelException *ex)
{
	return (CamelMBOXViewSummary *)camel_view_summary_disk_construct(camel_object_new(camel_mbox_view_summary_get_type()), base, ex);
}

/* NB: must have write lock on folder */
guint32 camel_mbox_view_next_uid(CamelMBOXView *view)
{
	guint32 uid;

	uid = view->nextuid++;
	camel_view_changed((CamelView *)view);

	return uid;
}

/* NB: must have write lock on folder */
void camel_mbox_view_last_uid(CamelMBOXView *view, guint32 uid)
{
	uid++;
	if (uid > view->nextuid) {
		view->nextuid = uid;
		camel_view_changed((CamelView *)view);
	}
}

static int
mbox_view_decode(CamelViewSummaryDisk *s, CamelView *view, CamelRecordDecoder *crd)
{
	int tag, ver;

	((CamelViewSummaryDiskClass *)cmvs_parent)->decode(s, view, crd);

	if (strchr(view->vid, 1) == NULL) {
		camel_record_decoder_reset(crd);
		while ((tag = camel_record_decoder_next_section(crd, &ver)) != CR_SECTION_INVALID) {
			switch (tag) {
			case CVS_MBOX_SECTION_VIEWINFO:
				((CamelMBOXView *)view)->time = camel_record_decoder_timet(crd);
				((CamelMBOXView *)view)->folder_size = camel_record_decoder_sizet(crd);
				((CamelMBOXView *)view)->nextuid = camel_record_decoder_int32(crd);
				/* We can actually get the last uid in the database cheaply, perhaps
				   we should use that to get the nextuid, at each startup */
				break;
			}
		}
	}

	return 0;
}

static void
mbox_view_encode(CamelViewSummaryDisk *s, CamelView *view, CamelRecordEncoder *cre)
{
	((CamelViewSummaryDiskClass *)cmvs_parent)->encode(s, view, cre);

	/* We only store extra data on the root view */

	if (strchr(view->vid, 1) == NULL) {
		camel_record_encoder_start_section(cre, CVS_MBOX_SECTION_VIEWINFO, 0);
		camel_record_encoder_timet(cre, ((CamelMBOXView *)view)->time);
		camel_record_encoder_sizet(cre, ((CamelMBOXView *)view)->folder_size);
		camel_record_encoder_int32(cre, ((CamelMBOXView *)view)->nextuid);
		camel_record_encoder_end_section(cre);
	}
}

static void
camel_mbox_view_summary_init(CamelMBOXViewSummary *obj)
{
	struct _CamelFolderSummary *s = (CamelFolderSummary *)obj;

	s = s;
}

static void
camel_mbox_view_summary_finalise(CamelObject *obj)
{
	/*CamelMBOXViewSummary *mbs = CAMEL_MBOX_VIEW_SUMMARY(obj);*/
}

static void
camel_mbox_view_summary_class_init(CamelMBOXViewSummaryClass *klass)
{
	((CamelViewSummaryClass *)klass)->view_sizeof = sizeof(CamelMBOXView);

	((CamelViewSummaryDiskClass *)klass)->encode = mbox_view_encode;
	((CamelViewSummaryDiskClass *)klass)->decode = mbox_view_decode;
}

CamelType
camel_mbox_view_summary_get_type(void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		cmvs_parent = (CamelViewSummaryDiskClass *)camel_view_summary_disk_get_type();
		type = camel_type_register((CamelType)cmvs_parent, "CamelMBOXViewSummary",
					   sizeof (CamelMBOXViewSummary),
					   sizeof (CamelMBOXViewSummaryClass),
					   (CamelObjectClassInitFunc) camel_mbox_view_summary_class_init,
					   NULL,
					   (CamelObjectInitFunc) camel_mbox_view_summary_init,
					   (CamelObjectFinalizeFunc) camel_mbox_view_summary_finalise);
	}
	
	return type;
}
