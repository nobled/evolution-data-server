/*
 * Copyright (C) 2005 Novell Inc.
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
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

#ifndef _CAMEL_POP3_SUMMARY_H
#define _CAMEL_POP3_SUMMARY_H

#include <camel/camel-folder-summary-disk.h>

struct _CamelFolder;

#define CAMEL_POP3_SUMMARY(obj)         CAMEL_CHECK_CAST (obj, camel_pop3_summary_get_type (), CamelPOP3Summary)
#define CAMEL_POP3_SUMMARY_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_pop3_summary_get_type (), CamelPOP3SummaryClass)
#define CAMEL_IS_POP3_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_pop3_summary_get_type ())

#define CAMEL_POP3_SERVER_FLAGS (CAMEL_MESSAGE_DELETED)

enum {
	CFS_POP3_SECTION_FOLDERINFO = CFSD_SECTION_LAST,
	CFS_POP3_SECTION_INFO,
	CFS_POP3_SECTION_LAST = CFSD_SECTION_LAST + 8
};

typedef struct _CamelPOP3SummaryClass CamelPOP3SummaryClass;
typedef struct _CamelPOP3Summary CamelPOP3Summary;

typedef struct _CamelPOP3MessageInfo {
	CamelMessageInfo info;

	guint32 id;
	guint32 size;
	guint32 flags;
	time_t date;		/* download date? */
} CamelPOP3MessageInfo;

struct _CamelPOP3Summary {
	CamelFolderSummaryDisk parent;
};

struct _CamelPOP3SummaryClass {
	CamelFolderSummaryDiskClass parent_class;

};

CamelType               camel_pop3_summary_get_type     (void);
CamelFolderSummary *camel_pop3_summary_new          (struct _CamelFolder *);

#endif /* ! _CAMEL_POP3_SUMMARY_H */

