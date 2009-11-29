/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; -*- */
/*
 *
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>

#include "camel-data-wrapper.h"
#include "camel-mime-filter-basic.h"
#include "camel-mime-filter-crlf.h"
#include "camel-private.h"
#include "camel-stream-filter.h"
#include "camel-stream.h"

#define d(x)

#define CAMEL_DATA_WRAPPER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_DATA_WRAPPER, CamelDataWrapperPrivate))

static gpointer parent_class;

static void
data_wrapper_dispose (GObject *object)
{
	CamelDataWrapper *data_wrapper = CAMEL_DATA_WRAPPER (object);

	if (data_wrapper->mime_type != NULL) {
		camel_content_type_unref (data_wrapper->mime_type);
		data_wrapper->mime_type = NULL;
	}

	if (data_wrapper->stream != NULL) {
		g_object_unref (data_wrapper->stream);
		data_wrapper->stream = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
data_wrapper_finalize (GObject *object)
{
	CamelDataWrapper *data_wrapper = CAMEL_DATA_WRAPPER (object);

	pthread_mutex_destroy (&data_wrapper->priv->stream_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gssize
data_wrapper_write_to_stream (CamelDataWrapper *data_wrapper,
                              CamelStream *stream,
                              GError **error)
{
	gssize ret;

	if (data_wrapper->stream == NULL) {
		return -1;
	}

	CAMEL_DATA_WRAPPER_LOCK (data_wrapper, stream_lock);
	if (camel_stream_reset (data_wrapper->stream, error) == -1) {
		CAMEL_DATA_WRAPPER_UNLOCK (data_wrapper, stream_lock);
		return -1;
	}

	ret = camel_stream_write_to_stream (
		data_wrapper->stream, stream, error);

	CAMEL_DATA_WRAPPER_UNLOCK (data_wrapper, stream_lock);

	return ret;
}

static gssize
data_wrapper_decode_to_stream (CamelDataWrapper *data_wrapper,
                               CamelStream *stream,
                               GError **error)
{
	CamelMimeFilter *filter;
	CamelStream *fstream;
	gssize ret;

	fstream = camel_stream_filter_new (stream);

	switch (data_wrapper->encoding) {
	case CAMEL_TRANSFER_ENCODING_BASE64:
		filter = camel_mime_filter_basic_new (CAMEL_MIME_FILTER_BASIC_BASE64_DEC);
		camel_stream_filter_add (CAMEL_STREAM_FILTER (fstream), filter);
		g_object_unref (filter);
		break;
	case CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE:
		filter = camel_mime_filter_basic_new (CAMEL_MIME_FILTER_BASIC_QP_DEC);
		camel_stream_filter_add (CAMEL_STREAM_FILTER (fstream), filter);
		g_object_unref (filter);
		break;
	case CAMEL_TRANSFER_ENCODING_UUENCODE:
		filter = camel_mime_filter_basic_new (CAMEL_MIME_FILTER_BASIC_UU_DEC);
		camel_stream_filter_add (CAMEL_STREAM_FILTER (fstream), filter);
		g_object_unref (filter);
		break;
	default:
		break;
	}

	if (camel_content_type_is (data_wrapper->mime_type, "text", "*")) {
		filter = camel_mime_filter_crlf_new (CAMEL_MIME_FILTER_CRLF_DECODE,
						     CAMEL_MIME_FILTER_CRLF_MODE_CRLF_ONLY);
		camel_stream_filter_add (CAMEL_STREAM_FILTER (fstream), filter);
		g_object_unref (filter);
	}

	ret = camel_data_wrapper_write_to_stream (
		data_wrapper, fstream, error);

	camel_stream_flush (fstream, NULL);
	g_object_unref (fstream);

	return ret;
}

static void
data_wrapper_set_mime_type (CamelDataWrapper *data_wrapper,
                            const gchar *mime_type)
{
	if (data_wrapper->mime_type)
		camel_content_type_unref (data_wrapper->mime_type);
	data_wrapper->mime_type = camel_content_type_decode (mime_type);
}

static gchar *
data_wrapper_get_mime_type (CamelDataWrapper *data_wrapper)
{
	return camel_content_type_simple (data_wrapper->mime_type);
}

static CamelContentType *
data_wrapper_get_mime_type_field (CamelDataWrapper *data_wrapper)
{
	return data_wrapper->mime_type;
}

static void
data_wrapper_set_mime_type_field (CamelDataWrapper *data_wrapper,
                                  CamelContentType *mime_type)
{
	if (mime_type)
		camel_content_type_ref (mime_type);
	if (data_wrapper->mime_type)
		camel_content_type_unref (data_wrapper->mime_type);
	data_wrapper->mime_type = mime_type;
}

static gint
data_wrapper_construct_from_stream (CamelDataWrapper *data_wrapper,
                                    CamelStream *stream,
                                    GError **error)
{
	if (data_wrapper->stream)
		g_object_unref (data_wrapper->stream);

	data_wrapper->stream = stream;
	g_object_ref (stream);
	return 0;
}

static gboolean
data_wrapper_is_offline (CamelDataWrapper *data_wrapper)
{
	return data_wrapper->offline;
}

static void
data_wrapper_class_init (CamelDataWrapperClass *class)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (CamelDataWrapperPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = data_wrapper_dispose;
	object_class->finalize = data_wrapper_finalize;

	class->write_to_stream = data_wrapper_write_to_stream;
	class->decode_to_stream = data_wrapper_decode_to_stream;
	class->set_mime_type = data_wrapper_set_mime_type;
	class->get_mime_type = data_wrapper_get_mime_type;
	class->get_mime_type_field = data_wrapper_get_mime_type_field;
	class->set_mime_type_field = data_wrapper_set_mime_type_field;
	class->construct_from_stream = data_wrapper_construct_from_stream;
	class->is_offline = data_wrapper_is_offline;
}

static void
data_wrapper_init (CamelDataWrapper *data_wrapper)
{
	data_wrapper->priv = CAMEL_DATA_WRAPPER_GET_PRIVATE (data_wrapper);

	pthread_mutex_init (&data_wrapper->priv->stream_lock, NULL);

	data_wrapper->mime_type = camel_content_type_new (
		"application", "octet-stream");
	data_wrapper->encoding = CAMEL_TRANSFER_ENCODING_DEFAULT;
	data_wrapper->offline = FALSE;
}

GType
camel_data_wrapper_get_type (void)
{
	static GType type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID))
		type = g_type_register_static_simple (
			CAMEL_TYPE_OBJECT,
			"CamelDataWrapper",
			sizeof (CamelDataWrapperClass),
			(GClassInitFunc) data_wrapper_class_init,
			sizeof (CamelDataWrapper),
			(GInstanceInitFunc) data_wrapper_init,
			0);

	return type;
}

/**
 * camel_data_wrapper_new:
 *
 * Create a new #CamelDataWrapper object.
 *
 * Returns: a new #CamelDataWrapper object
 **/
CamelDataWrapper *
camel_data_wrapper_new (void)
{
	return g_object_new (CAMEL_TYPE_DATA_WRAPPER, NULL);
}

/**
 * camel_data_wrapper_write_to_stream:
 * @data_wrapper: a #CamelDataWrapper object
 * @stream: a #CamelStream for output
 * @error: return location for a #GError, or %NULL
 *
 * Writes the content of @data_wrapper to @stream in a machine-independent
 * format appropriate for the data. It should be possible to construct an
 * equivalent data wrapper object later by passing this stream to
 * #camel_data_wrapper_construct_from_stream.
 *
 * Returns: the number of bytes written, or %-1 on fail
 **/
gssize
camel_data_wrapper_write_to_stream (CamelDataWrapper *data_wrapper,
                                    CamelStream *stream,
                                    GError **error)
{
	CamelDataWrapperClass *class;

	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), -1);
	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);

	class = CAMEL_DATA_WRAPPER_GET_CLASS (data_wrapper);
	g_return_val_if_fail (class->write_to_stream != NULL, -1);

	return class->write_to_stream (data_wrapper, stream, error);
}

/**
 * camel_data_wrapper_decode_to_stream:
 * @data_wrapper: a #CamelDataWrapper object
 * @stream: a #CamelStream for decoded data to be written to
 * @error: return location for a #GError, or %NULL
 *
 * Writes the decoded data content to @stream.
 *
 * Returns: the number of bytes written, or %-1 on fail
 **/
gssize
camel_data_wrapper_decode_to_stream (CamelDataWrapper *data_wrapper,
                                     CamelStream *stream,
                                     GError **error)
{
	CamelDataWrapperClass *class;

	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), -1);
	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);

	class = CAMEL_DATA_WRAPPER_GET_CLASS (data_wrapper);
	g_return_val_if_fail (class->decode_to_stream != NULL, -1);

	return class->decode_to_stream (data_wrapper, stream, error);
}

/**
 * camel_data_wrapper_construct_from_stream:
 * @data_wrapper: a #CamelDataWrapper object
 * @stream: an input #CamelStream
 * @error: return location for a #GError, or %NULL
 *
 * Constructs the content of @data_wrapper from the supplied @stream.
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_data_wrapper_construct_from_stream (CamelDataWrapper *data_wrapper,
                                          CamelStream *stream,
                                          GError **error)
{
	CamelDataWrapperClass *class;

	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), -1);
	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);

	class = CAMEL_DATA_WRAPPER_GET_CLASS (data_wrapper);
	g_return_val_if_fail (class->construct_from_stream != NULL, -1);

	return class->construct_from_stream (data_wrapper, stream, error);
}

/**
 * camel_data_wrapper_set_mime_type:
 * @data_wrapper: a #CamelDataWrapper object
 * @mime_type: a MIME type
 *
 * This sets the data wrapper's MIME type.
 *
 * It might fail, but you won't know. It will allow you to set
 * Content-Type parameters on the data wrapper, which are meaningless.
 * You should not be allowed to change the MIME type of a data wrapper
 * that contains data, or at least, if you do, it should invalidate the
 * data.
 **/
void
camel_data_wrapper_set_mime_type (CamelDataWrapper *data_wrapper,
                                  const gchar *mime_type)
{
	CamelDataWrapperClass *class;

	g_return_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper));
	g_return_if_fail (mime_type != NULL);

	class = CAMEL_DATA_WRAPPER_GET_CLASS (data_wrapper);
	g_return_if_fail (class->set_mime_type);

	class->set_mime_type (data_wrapper, mime_type);
}

/**
 * camel_data_wrapper_get_mime_type:
 * @data_wrapper: a #CamelDataWrapper object
 *
 * Returns: the MIME type which must be freed by the caller
 **/
gchar *
camel_data_wrapper_get_mime_type (CamelDataWrapper *data_wrapper)
{
	CamelDataWrapperClass *class;

	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), NULL);

	class = CAMEL_DATA_WRAPPER_GET_CLASS (data_wrapper);
	g_return_val_if_fail (class->get_mime_type != NULL, NULL);

	return class->get_mime_type (data_wrapper);
}

/**
 * camel_data_wrapper_get_mime_type_field:
 * @data_wrapper: a #CamelDataWrapper object
 *
 * Returns: the parsed form of the data wrapper's MIME type
 **/
CamelContentType *
camel_data_wrapper_get_mime_type_field (CamelDataWrapper *data_wrapper)
{
	CamelDataWrapperClass *class;

	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), NULL);

	class = CAMEL_DATA_WRAPPER_GET_CLASS (data_wrapper);
	g_return_val_if_fail (class->get_mime_type_field != NULL, NULL);

	return class->get_mime_type_field (data_wrapper);
}

/**
 * camel_data_wrapper_set_mime_type_field:
 * @data_wrapper: a #CamelDataWrapper object
 * @mime_type: a #CamelContentType
 *
 * This sets the data wrapper's MIME type. It suffers from the same
 * flaws as #camel_data_wrapper_set_mime_type.
 **/
void
camel_data_wrapper_set_mime_type_field (CamelDataWrapper *data_wrapper,
                                        CamelContentType *mime_type)
{
	CamelDataWrapperClass *class;

	g_return_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper));
	g_return_if_fail (mime_type != NULL);

	class = CAMEL_DATA_WRAPPER_GET_CLASS (data_wrapper);
	g_return_if_fail (class->set_mime_type_field != NULL);

	class->set_mime_type_field (data_wrapper, mime_type);
}

/**
 * camel_data_wrapper_is_offline:
 * @data_wrapper: a #CamelDataWrapper object
 *
 * Returns: whether @data_wrapper is "offline" (data stored
 * remotely) or not. Some optional code paths may choose to not
 * operate on offline data.
 **/
gboolean
camel_data_wrapper_is_offline (CamelDataWrapper *data_wrapper)
{
	CamelDataWrapperClass *class;

	g_return_val_if_fail (CAMEL_IS_DATA_WRAPPER (data_wrapper), TRUE);

	class = CAMEL_DATA_WRAPPER_GET_CLASS (data_wrapper);
	g_return_val_if_fail (class->is_offline != NULL, TRUE);

	return class->is_offline (data_wrapper);
}
