/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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


#ifndef _CAMEL_UNMATCHED_FOLDER_H
#define _CAMEL_UNMATCHED_FOLDER_H

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#include <glib.h>
#include <camel/camel-folder.h>

#define CAMEL_UNMATCHED_FOLDER(obj)         CAMEL_CHECK_CAST (obj, camel_unmatched_folder_get_type (), CamelUnmatchedFolder)
#define CAMEL_UNMATCHED_FOLDER_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_unmatched_folder_get_type (), CamelUnmatchedFolderClass)
#define CAMEL_IS_UNMATCHED_FOLDER(obj)      CAMEL_CHECK_TYPE (obj, camel_unmatched_folder_get_type ())

typedef struct _CamelUnmatchedFolder      CamelUnmatchedFolder;
typedef struct _CamelUnmatchedFolderClass CamelUnmatchedFolderClass;

struct _CamelUnmatchedFolder {
	CamelVeeFolder parent;

	struct _CamelUnmatchedFolderPrivate *priv;

	/* table of all uid's, which are vfolder format uids "hash{8} real_uid", relative to the real source folder,
	   and the content is the count of matches this uid has */
	GHashTable uids;
};

struct _CamelUnmatchedFolderClass {
	CamelVeeFolderClass parent_class;
};

CamelType     camel_unmatched_folder_get_type		(void);
CamelFolder  *camel_unmatched_folder_new		(CamelStore *parent_store, const char *name, guint32 flags);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ! _CAMEL_UNMATCHED_FOLDER_H */
