/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Author: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifndef CAMEL_LOCAL_FOLDER_H
#define CAMEL_LOCAL_FOLDER_H

#include <camel/camel.h>

#include "camel-local-summary.h"

/* Standard GObject macros */
#define CAMEL_TYPE_LOCAL_FOLDER \
	(camel_local_folder_get_type ())
#define CAMEL_LOCAL_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_LOCAL_FOLDER, CamelLocalFolder))
#define CAMEL_LOCAL_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_LOCAL_FOLDER, CamelLocalFolderClass))
#define CAMEL_IS_LOCAL_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_LOCAL_FOLDER))
#define CAMEL_IS_LOCAL_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_LOCAL_FOLDER))
#define CAMEL_LOCAL_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_LOCAL_FOLDER, CamelLocalFolderClass))

G_BEGIN_DECLS

enum {
	CAMEL_LOCAL_FOLDER_ARG_INDEX_BODY = CAMEL_FOLDER_ARG_LAST,

	CAMEL_LOCAL_FOLDER_ARG_LAST = CAMEL_FOLDER_ARG_LAST + 0x100
};

enum {
	CAMEL_LOCAL_FOLDER_INDEX_BODY = CAMEL_LOCAL_FOLDER_ARG_INDEX_BODY | CAMEL_ARG_BOO
};

typedef struct _CamelLocalFolder CamelLocalFolder;
typedef struct _CamelLocalFolderClass CamelLocalFolderClass;
typedef struct _CamelLocalFolderPrivate CamelLocalFolderPrivate;

struct _CamelLocalFolder {
	CamelFolder parent;
	CamelLocalFolderPrivate *priv;

	guint32 flags;		/* open mode flags */

	gint locked;		/* lock counter */
	CamelLockType locktype;	/* what type of lock we have */

	gchar *base_path;	/* base path of the local folder */
	gchar *folder_path;	/* the path to the folder itself */
	gchar *summary_path;	/* where the summary lives */
	gchar *index_path;	/* where the index file lives */

	CamelIndex *index;	   /* index for this folder */
	CamelFolderSearch *search; /* used to run searches, we just use the real thing (tm) */
	CamelFolderChangeInfo *changes;	/* used to store changes to the folder during processing */
};

struct _CamelLocalFolderClass {
	CamelFolderClass parent_class;

	/* Virtual methods */

	/* summary factory, only used at init */
	CamelLocalSummary *(*create_summary)(CamelLocalFolder *lf, const gchar *path, const gchar *folder, CamelIndex *index);

	/* Lock the folder for my operations */
	gint (*lock)(CamelLocalFolder *, CamelLockType type, GError **error);

	/* Unlock the folder for my operations */
	void (*unlock)(CamelLocalFolder *);
};

/* public methods */
/* flags are taken from CAMEL_STORE_FOLDER_* flags */
CamelLocalFolder *camel_local_folder_construct(CamelLocalFolder *lf, CamelStore *parent_store,
					       const gchar *full_name, guint32 flags, GError **error);

GType camel_local_folder_get_type(void);

/* Lock the folder for internal use.  May be called repeatedly */
/* UNIMPLEMENTED */
gint camel_local_folder_lock(CamelLocalFolder *lf, CamelLockType type, GError **error);
gint camel_local_folder_unlock(CamelLocalFolder *lf);

G_END_DECLS

#endif /* CAMEL_LOCAL_FOLDER_H */
