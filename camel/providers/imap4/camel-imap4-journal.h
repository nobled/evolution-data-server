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

#ifndef CAMEL_IMAP4_JOURNAL_H
#define CAMEL_IMAP4_JOURNAL_H

#include <stdarg.h>
#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_IMAP4_JOURNAL \
	(camel_imap4_journal_get_type ())
#define CAMEL_IMAP4_JOURNAL(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAP4_JOURNAL, CamelIMAP4Journal))
#define CAMEL_IMAP4_JOURNAL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAP4_JOURNAL, CamelIMAP4JournalClass))
#define CAMEL_IS_IMAP4_JOURNAL(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAP4_JOURNAL))
#define CAMEL_IS_IMAP4_JOURNAL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAP4_JOURNAL))
#define CAMEL_IMAP4_JOURNAL_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAP4_JOURNAL, CamelIMAP4JournalClass))

G_BEGIN_DECLS

typedef struct _CamelIMAP4Journal CamelIMAP4Journal;
typedef struct _CamelIMAP4JournalClass CamelIMAP4JournalClass;
typedef struct _CamelIMAP4JournalEntry CamelIMAP4JournalEntry;

struct _CamelIMAP4Folder;

enum {
	CAMEL_IMAP4_JOURNAL_ENTRY_APPEND,
};

struct _CamelIMAP4JournalEntry {
	CamelDListNode node;

	gint type;

	union {
		gchar *append_uid;
	} v;
};

struct _CamelIMAP4Journal {
	CamelOfflineJournal parent;

	GPtrArray *failed;
};

struct _CamelIMAP4JournalClass {
	CamelOfflineJournalClass parent_class;

};

GType camel_imap4_journal_get_type (void);

CamelOfflineJournal *camel_imap4_journal_new (struct _CamelIMAP4Folder *folder, const gchar *filename);

void camel_imap4_journal_readd_failed (CamelIMAP4Journal *journal);

/* interfaces for adding a journal entry */
void camel_imap4_journal_append (CamelIMAP4Journal *journal, CamelMimeMessage *message, const CamelMessageInfo *mi,
				 gchar **appended_uid, GError **error);

G_END_DECLS

#endif /* CAMEL_IMAP4_JOURNAL_H */
