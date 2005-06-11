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

struct _CamelRecordEncoder;
struct _CamelRecordDecoder;

#define CAMEL_FOLDER_SUMMARY_DISK(obj)         CAMEL_CHECK_CAST (obj, camel_folder_summary_disk_get_type (), CamelSummaryFolderDisk)
#define CAMEL_FOLDER_SUMMARY_DISK_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_folder_summary_disk_get_type (), CamelSummaryFolderDiskClass)
#define CAMEL_IS_FOLDER_SUMMARY_DISK(obj)      CAMEL_CHECK_TYPE (obj, camel_folder_summary_disk_get_type ())

typedef struct _CamelFolderSummaryDisk      CamelFolderSummaryDisk;
typedef struct _CamelFolderSummaryDiskClass CamelFolderSummaryDiskClass;

typedef struct _CamelMessageInfoDisk CamelMessageInfoDisk;
struct _CamelMessageInfoDisk {
	CamelMessageInfoBase info;
};

enum {
	CFSD_SECTION_HEADERS,	/* message headers */
	CFSD_SECTION_FLAGS,	/* message flags */

	CFSD_SECTION_FOLDERINFO=16,	/* folder header */

	CFSD_SECTION_LAST = 32,
};

struct _CamelFolderSummaryDisk {
	CamelFolderSummary parent;

	struct _CamelFolderSummaryDiskPrivate *priv;
};

struct _CamelFolderSummaryDiskClass {
	CamelFolderSummaryClass parent_class;

	void (*encode_header)(CamelFolderSummaryDisk *, struct _CamelRecordEncoder *);
	int (*decode_header)(CamelFolderSummaryDisk *, struct _CamelRecordDecoder *);

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

#endif /* ! _CAMEL_FOLDER_SUMMARY_DISK_H */
