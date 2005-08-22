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

#ifndef _CAMEL_FOLDER_SUMMARY_DISK_H
#define _CAMEL_FOLDER_SUMMARY_DISK_H

#include <camel/camel-folder-summary.h>

#include "libdb/dist/db.h"

struct _CamelRecordEncoder;
struct _CamelRecordDecoder;
struct _CamelViewSummaryDisk;

#define CAMEL_FOLDER_SUMMARY_DISK(obj)         CAMEL_CHECK_CAST (obj, camel_folder_summary_disk_get_type (), CamelSummaryFolderDisk)
#define CAMEL_FOLDER_SUMMARY_DISK_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_folder_summary_disk_get_type (), CamelSummaryFolderDiskClass)
#define CAMEL_IS_FOLDER_SUMMARY_DISK(obj)      CAMEL_CHECK_TYPE (obj, camel_folder_summary_disk_get_type ())

typedef struct _CamelFolderSummaryDisk      CamelFolderSummaryDisk;
typedef struct _CamelFolderSummaryDiskClass CamelFolderSummaryDiskClass;

typedef struct _CamelMessageInfoDisk CamelMessageInfoDisk;
typedef struct _CamelFolderViewDisk CamelFolderViewDisk;

struct _CamelMessageInfoDisk {
	CamelMessageInfoBase info;
};

enum {
	CFSD_SECTION_HEADERS,	/* message headers */
	CFSD_SECTION_FLAGS,	/* message flags */

	CFSD_SECTION_LAST = 32,
};

struct _CamelFolderViewDisk {
	CamelFolderView view;

	/* If this is the root view, then the db stores MessageInfo records, key'd by uid.
	   If this is a sub-view, then the db stores only the uid in the key, no data */
	DB *db;

	/* We keep track of open but unused db's using the unused list */
	int usecount:31;
	int unused:1;
	EDListNode ln;
};
/* Converts the list node pointer as stored in the unused list, to the folderview pointer */
#define CFSD_VIEW_FROM_UNUSED(x) ((CamelFolderViewDisk *)(((char *)x)-G_STRUCT_OFFSET(CamelFolderViewDisk, ln)))

struct _CamelFolderSummaryDisk {
	CamelFolderSummary summary;

	struct _CamelFolderSummaryDiskPrivate *priv;

	EDList unused;
};

struct _CamelFolderSummaryDiskClass {
	CamelFolderSummaryClass parent_class;

	/* compare db keys, db->user_data == summary */
	/* this will call CamelFolderSummary:uid_cmp by default so shouldn't normally need overriding */
	//int (*dbt_cmp)(struct _DB *db, const struct _DBT *a, const struct _DBT *b);

	void (*encode)(CamelFolderSummaryDisk *, struct _CamelMessageInfoDisk *mi, struct _CamelRecordEncoder *);
	int (*decode)(CamelFolderSummaryDisk *, struct _CamelMessageInfoDisk *mi, struct _CamelRecordDecoder *);

	/* changes are CamelMessageInfo's */
	void (*sync)(CamelFolderSummaryDisk *, GPtrArray *changes, CamelException *ex);
};

CamelType	camel_folder_summary_disk_get_type(void);

CamelFolderSummaryDisk *camel_folder_summary_disk_construct(CamelFolderSummaryDisk *cds, struct _CamelFolder *folder);
CamelFolderSummaryDisk *camel_folder_summary_disk_new(struct _CamelFolder *folder);

void camel_folder_summary_disk_sync(CamelFolderSummaryDisk *cds, CamelException *ex);

int camel_folder_summary_disk_rename(CamelFolderSummaryDisk *cds, const char *newname);

const CamelMessageInfo *camel_message_iterator_disk_get(void *mitin, guint32 flags0, guint32 flags1, CamelException *ex);

#endif /* ! _CAMEL_FOLDER_SUMMARY_DISK_H */
