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

#ifndef _CAMEL_MBOX_SUMMARY_H
#define _CAMEL_MBOX_SUMMARY_H

#include "camel-local-summary.h"

/* Enable the use of elm/pine style "Status" & "X-Status" headers */
#define STATUS_PINE (1)

#define CAMEL_MBOX_SUMMARY(obj)         CAMEL_CHECK_CAST (obj, camel_mbox_summary_get_type (), CamelMboxSummary)
#define CAMEL_MBOX_SUMMARY_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_mbox_summary_get_type (), CamelMboxSummaryClass)
#define CAMEL_IS_MBOX_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_mbox_summary_get_type ())

typedef struct _CamelMboxSummary      CamelMboxSummary;
typedef struct _CamelMboxSummaryClass CamelMboxSummaryClass;

enum {
	CFS_MBOX_SECTION_FOLDERINFO = CFS_LOCAL_SECTION_LAST,
	CFS_MBOX_SECTION_INFO,
	CFS_MBOX_SECTION_LAST = CFS_LOCAL_SECTION_LAST+8
};

typedef struct _CamelMboxMessageInfo {
	CamelLocalMessageInfo info;

	off_t frompos;
} CamelMboxMessageInfo;

struct _CamelMboxSummary {
	CamelLocalSummary parent;

	CamelFolderChangeInfo *changes;	/* used to build change sets */

	size_t folder_size;	/* size of the mbox file, last sync */
	time_t time;		/* time of mbox last sync */
	guint32 nextuid;	/* the next uid to assign */

	unsigned int xstatus:1;	/* do we store/honour xstatus/status headers */
	unsigned int xstatus_changed:1;	/* fun, next sync requires a full sync */
};

struct _CamelMboxSummaryClass {
	CamelLocalSummaryClass parent_class;

	/* sync in-place */
	int (*sync_quick)(CamelMboxSummary *cls, gboolean expunge, CamelFolderChangeInfo *changeinfo, CamelException *ex);
	/* sync requires copy */
	int (*sync_full)(CamelMboxSummary *cls, gboolean expunge, CamelFolderChangeInfo *changeinfo, CamelException *ex);
};

CamelType		camel_mbox_summary_get_type	(void);
CamelMboxSummary      *camel_mbox_summary_new	(struct _CamelFolder *, const char *filename, const char *mbox_name, CamelIndex *index);
void camel_mbox_summary_construct(CamelMboxSummary *new, struct _CamelFolder *folder, const char *filename, const char *mbox_name, CamelIndex *index);

/* do we honour/use xstatus headers, etc */
void camel_mbox_summary_xstatus(CamelMboxSummary *mbs, int state);

char *camel_mbox_summary_encode_xev(const char *uidstr, guint32 flags);
char *camel_mbox_summary_decode_xev(const char *xev, guint32 *flagsp);

/* build a new mbox from an existing mbox storing summary information */
int camel_mbox_summary_sync_mbox(CamelMboxSummary *cls, guint32 flags, CamelFolderChangeInfo *changeinfo, int fd, int fdout, CamelException *ex);

guint32 camel_mbox_summary_next_uid(CamelMboxSummary *);
void camel_mbox_summary_last_uid(CamelMboxSummary *, guint32 uid);

#ifdef STATUS_PINE

/* For pine/elm/etc headers, we can store various bits in various of the status headers,
   These flags define which bits are relevant for each of "Status" and "X-Status"
   respectively */

#define CAMEL_MBOX_STATUS_XSTATUS (CAMEL_MESSAGE_FLAGGED|CAMEL_MESSAGE_ANSWERED|CAMEL_MESSAGE_DELETED)
#define CAMEL_MBOX_STATUS_STATUS (CAMEL_MESSAGE_SEEN)

void camel_mbox_summary_encode_status(guint32 flags, char status[8]);
#endif

#endif /* ! _CAMEL_MBOX_SUMMARY_H */

