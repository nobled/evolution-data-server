/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-stream-mem.c: memory buffer based stream */

/*
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 *	    Michael Zucchi <notzed@helixcode.com>
 *
 * Copyright 1999, 2000 Helix Code, Inc. (http://www.helixcode.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <config.h>
#include "camel-stream-mem.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

static CamelSeekableStreamClass *parent_class = NULL;

/* Returns the class for a CamelStreamMem */
#define CSM_CLASS(so) CAMEL_STREAM_MEM_CLASS(CAMEL_OBJECT_GET_CLASS(so))

static ssize_t stream_read (CamelStream *stream, char *buffer, size_t n);
static ssize_t stream_write (CamelStream *stream, const char *buffer, size_t n);
static gboolean stream_eos (CamelStream *stream);
static off_t stream_seek (CamelSeekableStream *stream, off_t offset,
			  CamelStreamSeekPolicy policy);

static void camel_stream_mem_finalize (CamelObject *object);

static void
camel_stream_mem_class_init (CamelStreamMemClass *camel_stream_mem_class)
{
	CamelSeekableStreamClass *camel_seekable_stream_class =
		CAMEL_SEEKABLE_STREAM_CLASS (camel_stream_mem_class);
	CamelStreamClass *camel_stream_class =
		CAMEL_STREAM_CLASS (camel_stream_mem_class);

	parent_class = CAMEL_SEEKABLE_STREAM_CLASS( camel_type_get_global_classfuncs( CAMEL_SEEKABLE_STREAM_TYPE ) );

	/* virtual method overload */
	camel_stream_class->read = stream_read;
	camel_stream_class->write = stream_write;
	camel_stream_class->eos = stream_eos;

	camel_seekable_stream_class->seek = stream_seek;
}

static void
camel_stream_mem_init (CamelObject *object)
{
	CamelStreamMem *stream_mem = CAMEL_STREAM_MEM (object);

	stream_mem->owner = FALSE;
	stream_mem->buffer = 0;
}

CamelType
camel_stream_mem_get_type (void)
{
	static CamelType camel_stream_mem_type = CAMEL_INVALID_TYPE;

	if (camel_stream_mem_type == CAMEL_INVALID_TYPE) {
		camel_stream_mem_type = camel_type_register( CAMEL_SEEKABLE_STREAM_TYPE,
							     "CamelStreamMem",
							     sizeof( CamelStreamMem ),
							     sizeof( CamelStreamMemClass ),
							     (CamelObjectClassInitFunc) camel_stream_mem_class_init,
							     NULL,
							     (CamelObjectInitFunc) camel_stream_mem_init,
							     (CamelObjectFinalizeFunc) camel_stream_mem_finalize );
	}

	return camel_stream_mem_type;
}


CamelStream *
camel_stream_mem_new (void)
{
	return camel_stream_mem_new_with_byte_array (g_byte_array_new ());
}

CamelStream *
camel_stream_mem_new_with_buffer (const char *buffer, size_t len)
{
	GByteArray *ba;

	ba = g_byte_array_new ();
	g_byte_array_append (ba, (const guint8 *)buffer, len);
	return camel_stream_mem_new_with_byte_array (ba);
}

CamelStream *
camel_stream_mem_new_with_byte_array (GByteArray *byte_array)
{
	CamelStreamMem *stream_mem;

	stream_mem = CAMEL_STREAM_MEM( camel_object_new (CAMEL_STREAM_MEM_TYPE) );
	stream_mem->buffer = byte_array;
	stream_mem->owner = TRUE;

	return CAMEL_STREAM (stream_mem);
}

/* note: with these functions the caller is the 'owner' of the buffer */
void camel_stream_mem_set_byte_array (CamelStreamMem *s, GByteArray *buffer)
{
	if (s->buffer && s->owner)
		g_byte_array_free(s->buffer, TRUE);
	s->owner = FALSE;
	s->buffer = buffer;
}

void camel_stream_mem_set_buffer (CamelStreamMem *s, const char *buffer,
				  size_t len)
{
	GByteArray *ba;

	ba = g_byte_array_new ();
	g_byte_array_append (ba, (const guint8 *)buffer, len);
	camel_stream_mem_set_byte_array(s, ba);
}

static void
camel_stream_mem_finalize (CamelObject *object)
{
	CamelStreamMem *stream_mem = CAMEL_STREAM_MEM (object);

	if (stream_mem->buffer && stream_mem->owner)
		g_byte_array_free (stream_mem->buffer, TRUE);

	/* Will be called automagically in the Camel Type System!
	 * Wheeee!
	 * G_TK_OBJECT_CLASS (parent_class)->finalize (object);
	 */
}

static ssize_t
stream_read (CamelStream *stream, char *buffer, size_t n)
{
	CamelStreamMem *camel_stream_mem = CAMEL_STREAM_MEM (stream);
	CamelSeekableStream *seekable = CAMEL_SEEKABLE_STREAM (stream);
	ssize_t nread;

	if (seekable->bound_end != CAMEL_STREAM_UNBOUND)
		n = MIN(seekable->bound_end - seekable->position, n);

	nread = MIN (n, camel_stream_mem->buffer->len - seekable->position);
	if (nread > 0) {
		memcpy (buffer, camel_stream_mem->buffer->data +
			seekable->position, nread);
		seekable->position += nread;
	} else
		nread = -1;

	return nread;
}

static ssize_t
stream_write (CamelStream *stream, const char *buffer, size_t n)
{
	CamelStreamMem *stream_mem = CAMEL_STREAM_MEM (stream);
	CamelSeekableStream *seekable = CAMEL_SEEKABLE_STREAM (stream);
	ssize_t nwrite = n;
	
	if (seekable->bound_end != CAMEL_STREAM_UNBOUND)
		nwrite = MIN(seekable->bound_end - seekable->position, n);

#warning "g_byte_arrays use g_malloc and so are totally unsuitable for this object"
	if (seekable->position == stream_mem->buffer->len) {
		stream_mem->buffer =
			g_byte_array_append (stream_mem->buffer, (const guint8 *)buffer, nwrite);
	} else {
		g_byte_array_set_size (stream_mem->buffer,
				       nwrite + stream_mem->buffer->len);
		memcpy (stream_mem->buffer->data + seekable->position, buffer, nwrite);
	}
	seekable->position += nwrite;

	return nwrite;
}

static gboolean
stream_eos (CamelStream *stream)
{
	CamelStreamMem *stream_mem = CAMEL_STREAM_MEM (stream);
	CamelSeekableStream *seekable_stream = CAMEL_SEEKABLE_STREAM (stream);

	return stream_mem->buffer->len <= seekable_stream->position;
}

static off_t
stream_seek (CamelSeekableStream *stream, off_t offset,
	     CamelStreamSeekPolicy policy)
{
	off_t position;
	CamelStreamMem *stream_mem = CAMEL_STREAM_MEM (stream);

	switch  (policy) {
	case CAMEL_STREAM_SET:
		position = offset;
		break;
	case CAMEL_STREAM_CUR:
		position = stream->position + offset;
		break;
	case CAMEL_STREAM_END:
		position = (stream_mem->buffer)->len + offset;
		break;
	default:
		position = offset;
		break;
	}

	if (stream->bound_end != CAMEL_STREAM_UNBOUND)
		position = MIN (position, stream->bound_end);
	if (stream->bound_start != CAMEL_STREAM_UNBOUND)
		position = MAX (position, 0);
	else
		position = MAX (position, stream->bound_start);

	if (position > stream_mem->buffer->len) {
		int oldlen = stream_mem->buffer->len;
		g_byte_array_set_size (stream_mem->buffer, position);
		memset (stream_mem->buffer->data + oldlen, 0,
			position - oldlen);
	}

	stream->position = position;

	return position;
}
