/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  Camel
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef CAMEL_IMAP4_SUMMARY_H
#define CAMEL_IMAP4_SUMMARY_H

#include <sys/types.h>
#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_IMAP4_SUMMARY \
	(camel_imap4_summary_get_type ())
#define CAMEL_IMAP4_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAP4_SUMMARY, CamelIMAP4Summary))
#define CAMEL_IMAP4_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAP4_SUMMARY, CamelIMAP4SummaryClass))
#define CAMEL_IS_IMAP4_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAP4_SUMMARY))
#define CAMEL_IS_IMAP4_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAP4_SUMMARY))
#define CAMEL_IMAP4_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_FOLDER_SUMMARY, CamelIMAP4SummaryClass))

G_BEGIN_DECLS

typedef struct _CamelIMAP4MessageInfo CamelIMAP4MessageInfo;
typedef struct _CamelIMAP4MessageContentInfo CamelIMAP4MessageContentInfo;

typedef struct _CamelIMAP4Summary CamelIMAP4Summary;
typedef struct _CamelIMAP4SummaryClass CamelIMAP4SummaryClass;

#define CAMEL_IMAP4_MESSAGE_RECENT (1 << 17)

enum {
	CAMEL_IMAP4_SUMMARY_HAVE_MLIST = (1 << 8)
};

struct _CamelIMAP4MessageInfo {
	CamelMessageInfoBase info;

	guint32 server_flags;
};

struct _CamelIMAP4MessageContentInfo {
	CamelMessageContentInfo info;

};

struct _CamelIMAP4Summary {
	CamelFolderSummary parent;

	guint32 version;

	guint32 exists;
	guint32 recent;
	guint32 unseen;

	guint32 uidvalidity;

	guint uidvalidity_changed:1;
	guint update_flags:1;
};

struct _CamelIMAP4SummaryClass {
	CamelFolderSummaryClass parent_class;

};

GType camel_imap4_summary_get_type (void);

CamelFolderSummary *camel_imap4_summary_new (CamelFolder *folder);

void camel_imap4_summary_set_exists (CamelFolderSummary *summary, guint32 exists);
void camel_imap4_summary_set_recent (CamelFolderSummary *summary, guint32 recent);
void camel_imap4_summary_set_unseen (CamelFolderSummary *summary, guint32 unseen);
void camel_imap4_summary_set_uidnext (CamelFolderSummary *summary, guint32 uidnext);

void camel_imap4_summary_set_uidvalidity (CamelFolderSummary *summary, guint32 uidvalidity);

void camel_imap4_summary_expunge (CamelFolderSummary *summary, gint seqid);

gint camel_imap4_summary_flush_updates (CamelFolderSummary *summary, GError **error);

G_END_DECLS

#endif /* CAMEL_IMAP4_SUMMARY_H */
