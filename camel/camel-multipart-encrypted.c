/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>

#include <glib/gi18n-lib.h>

#include "camel-mime-filter-crlf.h"
#include "camel-mime-part.h"
#include "camel-mime-utils.h"
#include "camel-multipart-encrypted.h"
#include "camel-stream-filter.h"
#include "camel-stream-fs.h"
#include "camel-stream-mem.h"

static gpointer parent_class;

static void
multipart_encrypted_dispose (GObject *object)
{
	CamelMultipartEncrypted *multipart;

	multipart = CAMEL_MULTIPART_ENCRYPTED (object);

	if (multipart->decrypted) {
		g_object_unref (multipart->decrypted);
		multipart->decrypted = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
multipart_encrypted_finalize (GObject *object)
{
	CamelMultipartEncrypted *multipart;

	multipart = CAMEL_MULTIPART_ENCRYPTED (object);

	g_free (multipart->protocol);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* we snoop the mime type to get the protocol */
static void
multipart_encrypted_set_mime_type_field (CamelDataWrapper *data_wrapper,
                                         CamelContentType *mime_type)
{
	CamelMultipartEncrypted *multipart;
	CamelDataWrapperClass *data_wrapper_class;

	multipart = CAMEL_MULTIPART_ENCRYPTED (data_wrapper);

	if (mime_type != NULL) {
		const gchar *protocol;

		protocol = camel_content_type_param (mime_type, "protocol");
		g_free (multipart->protocol);
		multipart->protocol = g_strdup (protocol);
	}

	/* Chain up to parent's set_mime_type_field() method. */
	data_wrapper_class = CAMEL_DATA_WRAPPER_CLASS (parent_class);
	data_wrapper_class->set_mime_type_field (data_wrapper, mime_type);
}

static void
multipart_encrypted_class_init (CamelMultipartEncryptedClass *class)
{
	GObjectClass *object_class;
	CamelDataWrapperClass *data_wrapper_class;

	parent_class = g_type_class_peek_parent (class);

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = multipart_encrypted_dispose;
	object_class->finalize = multipart_encrypted_finalize;

	data_wrapper_class = CAMEL_DATA_WRAPPER_CLASS (class);
	data_wrapper_class->set_mime_type_field =
		multipart_encrypted_set_mime_type_field;
}

static void
multipart_encrypted_init (CamelMultipartEncrypted *multipart)
{
	camel_data_wrapper_set_mime_type (
		CAMEL_DATA_WRAPPER (multipart), "multipart/encrypted");

	multipart->decrypted = NULL;
}

GType
camel_multipart_encrypted_get_type (void)
{
	static GType type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID))
		type = g_type_register_static_simple (
			CAMEL_TYPE_MULTIPART,
			"CamelMultipartEncrypted",
			sizeof (CamelMultipartEncryptedClass),
			(GClassInitFunc) multipart_encrypted_class_init,
			sizeof (CamelMultipartEncrypted),
			(GInstanceInitFunc) multipart_encrypted_init,
			0);

	return type;
}

/**
 * camel_multipart_encrypted_new:
 *
 * Create a new #CamelMultipartEncrypted object.
 *
 * A MultipartEncrypted should be used to store and create parts of
 * type "multipart/encrypted".
 *
 * Returns: a new #CamelMultipartEncrypted object
 **/
CamelMultipartEncrypted *
camel_multipart_encrypted_new (void)
{
	return g_object_new (CAMEL_TYPE_MULTIPART_ENCRYPTED, NULL);
}
