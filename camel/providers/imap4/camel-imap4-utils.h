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

#ifndef CAMEL_IMAP4_UTILS_H
#define CAMEL_IMAP4_UTILS_H

#include <camel/camel.h>

G_BEGIN_DECLS

/* IMAP4 flag merging */
typedef struct {
	guint32 changed;
	guint32 bits;
} flags_diff_t;

void camel_imap4_flags_diff (flags_diff_t *diff, guint32 old, guint32 new);
guint32 camel_imap4_flags_merge (flags_diff_t *diff, guint32 flags);
guint32 camel_imap4_merge_flags (guint32 original, guint32 local, guint32 server);

struct _CamelFolderInfo;
struct _CamelIMAP4Engine;
struct _CamelIMAP4Command;
struct _CamelFolderSummary;
struct _camel_imap4_token_t;
struct _CamelIMAP4StoreSummary;
struct _CamelIMAP4NamespaceList;
struct _CamelIMAP4Namespace;

struct _CamelFolderInfo *camel_imap4_build_folder_info_tree (GPtrArray *array, const gchar *top);

void camel_imap4_namespace_clear (struct _CamelIMAP4Namespace **ns);
struct _CamelIMAP4NamespaceList *camel_imap4_namespace_list_copy (const struct _CamelIMAP4NamespaceList *nsl);
void camel_imap4_namespace_list_free (struct _CamelIMAP4NamespaceList *nsl);

gchar camel_imap4_get_path_delim (struct _CamelIMAP4StoreSummary *s, const gchar *full_name);

gint camel_imap4_get_uid_set (struct _CamelIMAP4Engine *engine, struct _CamelFolderSummary *summary, GPtrArray *infos, gint cur, gsize linelen, gchar **set);

void camel_imap4_utils_set_unexpected_token_error (GError **error, struct _CamelIMAP4Engine *engine, struct _camel_imap4_token_t *token);

gint camel_imap4_parse_flags_list (struct _CamelIMAP4Engine *engine, guint32 *flags, GError **error);

/* Note: make sure these don't clash with any bit flags in camel-store.h */
#define CAMEL_IMAP4_FOLDER_MARKED   (1 << 17)
#define CAMEL_IMAP4_FOLDER_UNMARKED (1 << 18)

typedef struct {
	guint32 flags;
	gchar delim;
	gchar *name;
} camel_imap4_list_t;

gint camel_imap4_untagged_list (struct _CamelIMAP4Engine *engine, struct _CamelIMAP4Command *ic,
			       guint32 index, struct _camel_imap4_token_t *token, GError **error);

enum {
	CAMEL_IMAP4_STATUS_UNKNOWN,
	CAMEL_IMAP4_STATUS_MESSAGES,
	CAMEL_IMAP4_STATUS_RECENT,
	CAMEL_IMAP4_STATUS_UIDNEXT,
	CAMEL_IMAP4_STATUS_UIDVALIDITY,
	CAMEL_IMAP4_STATUS_UNSEEN,
};

typedef struct _camel_imap4_status_attr {
	struct _camel_imap4_status_attr *next;
	guint32 type;
	guint32 value;
} camel_imap4_status_attr_t;

typedef struct {
	camel_imap4_status_attr_t *attr_list;
	gchar *mailbox;
} camel_imap4_status_t;

void camel_imap4_status_free (camel_imap4_status_t *status);

gint camel_imap4_untagged_status (struct _CamelIMAP4Engine *engine, struct _CamelIMAP4Command *ic,
				 guint32 index, struct _camel_imap4_token_t *token, GError **error);

G_END_DECLS

#endif /* CAMEL_IMAP4_UTILS_H */
