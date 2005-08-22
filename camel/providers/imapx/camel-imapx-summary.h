/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors:
 *    Michael Zucchi <notzed@ximian.com>
 *    Dan Winship <danw@ximian.com>
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

#ifndef _CAMEL_IMAPX_SUMMARY_H
#define _CAMEL_IMAPX_SUMMARY_H

#include <camel/camel-folder-summary-disk.h>

struct _CamelFolder;

#define CAMEL_IMAPX_SUMMARY(obj)         CAMEL_CHECK_CAST (obj, camel_imapx_summary_get_type (), CamelIMAPXSummary)
#define CAMEL_IMAPX_SUMMARY_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_imapx_summary_get_type (), CamelIMAPXSummaryClass)
#define CAMEL_IS_IMAPX_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_imapx_summary_get_type ())

#define CAMEL_IMAPX_SERVER_FLAGS (CAMEL_MESSAGE_ANSWERED | \
				 CAMEL_MESSAGE_DELETED | \
				 CAMEL_MESSAGE_DRAFT | \
				 CAMEL_MESSAGE_FLAGGED | \
				 CAMEL_MESSAGE_SEEN)

#define CAMEL_IMAPX_MESSAGE_RECENT (1 << 8)

enum {
	CFS_IMAPX_SECTION_FOLDERINFO = CFSD_SECTION_LAST,
	CFS_IMAPX_SECTION_INFO,
	CFS_IMAPX_SECTION_LAST = CFSD_SECTION_LAST + 8
};

typedef struct _CamelIMAPXSummaryClass CamelIMAPXSummaryClass;
typedef struct _CamelIMAPXSummary CamelIMAPXSummary;

typedef struct _CamelIMAPXMessageInfo {
	CamelMessageInfoBase info;

	guint32 server_flags;
	struct _CamelFlag *server_user_flags;
} CamelIMAPXMessageInfo;

struct _CamelIMAPXSummary {
	CamelFolderSummaryDisk parent;

	/* NB: not used? */
	guint32 unseen;
	guint32 recent;
};

struct _CamelIMAPXSummaryClass {
	CamelFolderSummaryDiskClass parent_class;

};

CamelType               camel_imapx_summary_get_type     (void);
CamelFolderSummary *camel_imapx_summary_new          (struct _CamelFolder *);

#endif /* ! _CAMEL_IMAPX_SUMMARY_H */

