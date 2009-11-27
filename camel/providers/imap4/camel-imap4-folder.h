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

#ifndef CAMEL_IMAP4_FOLDER_H
#define CAMEL_IMAP4_FOLDER_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_IMAP4_FOLDER \
	(camel_imap4_folder_get_type ())
#define CAMEL_IMAP4_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAP4_FOLDER, CamelIMAP4Folder))
#define CAMEL_IMAP4_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAP4_FOLDER, CamelIMAP4FolderClass))
#define CAMEL_IS_IMAP4_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAP4_FOLDER))
#define CAMEL_IS_IMAP4_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAP4_FOLDER))
#define CAMEL_IMAP4_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAP4_FOLDER, CamelIMAP4FolderClass))

G_BEGIN_DECLS

typedef struct _CamelIMAP4Folder CamelIMAP4Folder;
typedef struct _CamelIMAP4FolderClass CamelIMAP4FolderClass;

struct _CamelIMAP4Journal;

enum {
	CAMEL_IMAP4_FOLDER_ARG_ENABLE_MLIST = CAMEL_OFFLINE_FOLDER_ARG_LAST,
	CAMEL_IMAP4_FOLDER_ARG_EXPIRE_ACCESS,
	CAMEL_IMAP4_FOLDER_ARG_EXPIRE_AGE,
	CAMEL_IMAP4_FOLDER_ARG_LAST = CAMEL_OFFLINE_FOLDER_ARG_LAST + 0x100
};

enum {
	CAMEL_IMAP4_FOLDER_ENABLE_MLIST = CAMEL_IMAP4_FOLDER_ARG_ENABLE_MLIST | CAMEL_ARG_BOO,
	CAMEL_IMAP4_FOLDER_EXPIRE_ACCESS = CAMEL_IMAP4_FOLDER_ARG_EXPIRE_ACCESS | CAMEL_ARG_INT,
	CAMEL_IMAP4_FOLDER_EXPIRE_AGE = CAMEL_IMAP4_FOLDER_ARG_EXPIRE_AGE | CAMEL_ARG_INT,
};

struct _CamelIMAP4Folder {
	CamelOfflineFolder parent;

	CamelFolderSearch *search;

	CamelOfflineJournal *journal;
	CamelDataCache *cache;

	gchar *cachedir;
	gchar *utf7_name;

	guint read_only:1;
	guint enable_mlist:1;
};

struct _CamelIMAP4FolderClass {
	CamelOfflineFolderClass parent_class;

};

GType camel_imap4_folder_get_type (void);

CamelFolder *camel_imap4_folder_new (CamelStore *store, const gchar *full_name, GError **error);

const gchar *camel_imap4_folder_utf7_name (CamelIMAP4Folder *folder);

G_END_DECLS

#endif /* CAMEL_IMAP4_FOLDER_H */
