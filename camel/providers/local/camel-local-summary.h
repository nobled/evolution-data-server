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

#ifndef _CAMEL_LOCAL_SUMMARY_H
#define _CAMEL_LOCAL_SUMMARY_H

#include <camel/camel-folder-summary-disk.h>
#include <camel/camel-folder.h>
#include <camel/camel-exception.h>
#include <camel/camel-index.h>

#define CAMEL_LOCAL_SUMMARY(obj)         CAMEL_CHECK_CAST (obj, camel_local_summary_get_type (), CamelLocalSummary)
#define CAMEL_LOCAL_SUMMARY_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_local_summary_get_type (), CamelLocalSummaryClass)
#define CAMEL_IS_LOCAL_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_local_summary_get_type ())

typedef struct _CamelLocalSummary      CamelLocalSummary;
typedef struct _CamelLocalSummaryClass CamelLocalSummaryClass;

/* extra summary flags */
enum {
	CAMEL_MESSAGE_FOLDER_NOXEV = 1<<17,
	CAMEL_MESSAGE_FOLDER_XEVCHANGE = 1<<18,
	CAMEL_MESSAGE_FOLDER_NOTSEEN = 1<<19, /* have we seen this in processing this loop? */
};

enum {
	CFS_LOCAL_SECTION_LAST = CFSD_SECTION_LAST + 8
};

typedef struct _CamelLocalMessageInfo CamelLocalMessageInfo;
struct _CamelLocalMessageInfo {
	CamelMessageInfoDisk info;
};

struct _CamelLocalSummary {
	CamelFolderSummaryDisk parent;

	char *folder_path;	/* name of matching folder */
	
	CamelIndex *index;
	unsigned int index_force:1; /* do we force index during creation? */
	unsigned int check_force:1; /* does a check force a full check? */
};

struct _CamelLocalSummaryClass {
	CamelFolderSummaryDiskClass parent_class;

	int (*check)(CamelLocalSummary *cls, CamelFolderChangeInfo *changeinfo, CamelException *ex);
	int (*sync)(CamelLocalSummary *cls, gboolean expunge, CamelFolderChangeInfo *changeinfo, CamelException *ex);
};

CamelType	camel_local_summary_get_type	(void);
void	camel_local_summary_construct	(CamelLocalSummary *new, CamelFolder *folder, const char *filename, const char *local_name, CamelIndex *index);

/* check for new/removed messages */
int camel_local_summary_check(CamelLocalSummary *cls, CamelFolderChangeInfo *, CamelException *ex);
/* perform a folder sync or expunge, if needed */
int camel_local_summary_sync(CamelLocalSummary *cls, gboolean expunge, CamelFolderChangeInfo *, CamelException *ex);

/* force the next check to be a full check/rebuild */
void camel_local_summary_check_force(CamelLocalSummary *cls);

#endif /* ! _CAMEL_LOCAL_SUMMARY_H */

