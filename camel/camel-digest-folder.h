/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_DISABLE_DEPRECATED

#ifndef CAMEL_DIGEST_FOLDER_H
#define CAMEL_DIGEST_FOLDER_H

#include <camel/camel-store.h>
#include <camel/camel-folder.h>
#include <camel/camel-mime-message.h>

/* Standard GObject macros */
#define CAMEL_TYPE_DIGEST_FOLDER \
	(camel_digest_folder_get_type ())
#define CAMEL_DIGEST_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_DIGEST_FOLDER, CamelDigestFolder))
#define CAMEL_DIGEST_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_DIGEST_FOLDER, CamelDigestFolderClass))
#define CAMEL_IS_DIGEST_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_DIGEST_FOLDER))
#define CAMEL_IS_DIGEST_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_DIGEST_FOLDER))
#define CAMEL_DIGEST_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_DIGEST_FOLDER, CamelDigestFolderClass))

G_BEGIN_DECLS

typedef struct _CamelDigestFolder CamelDigestFolder;
typedef struct _CamelDigestFolderClass CamelDigestFolderClass;
typedef struct _CamelDigestFolderPrivate CamelDigestFolderPrivate;

struct _CamelDigestFolder {
	CamelFolder parent;
	CamelDigestFolderPrivate *priv;
};

struct _CamelDigestFolderClass {
	CamelFolderClass parent_class;
};

GType    camel_digest_folder_get_type (void);

CamelFolder *camel_digest_folder_new      (CamelStore *parent_store, CamelMimeMessage *message);

G_END_DECLS

#endif /* CAMEL_DIGEST_FOLDER_H */

#endif /* CAMEL_DISABLE_DEPRECATED */
