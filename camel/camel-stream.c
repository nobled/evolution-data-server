/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/* camel-stream.c : abstract class for a stream */

/*
 * Author:
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright 1999, 2000 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_FOPENCOOKIE
#ifndef _GNU_SOURCE
#define _GNU_SOURCE (1)
#endif
#include <stdio.h>
#endif

#include <string.h>
#include "camel-stream.h"

static CamelObjectClass *parent_class = NULL;

/* Returns the class for a CamelStream */
#define CS_CLASS(so) CAMEL_STREAM_CLASS(CAMEL_OBJECT_GET_CLASS(so))

/* default implementations, do very little */
static ssize_t   stream_read       (CamelStream *stream, char *buffer, size_t n) { return 0; }
static ssize_t   stream_write      (CamelStream *stream, const char *buffer, size_t n) { return n; }
static int       stream_close      (CamelStream *stream) { return 0; }
static int       stream_flush      (CamelStream *stream) { return 0; }
static gboolean  stream_eos        (CamelStream *stream) { return stream->eos; }
static int       stream_reset      (CamelStream *stream) { return 0; }

static void
camel_stream_class_init (CamelStreamClass *camel_stream_class)
{
	parent_class = camel_type_get_global_classfuncs( CAMEL_OBJECT_TYPE );

	/* virtual method definition */
	camel_stream_class->read = stream_read;
	camel_stream_class->write = stream_write;
	camel_stream_class->close = stream_close;
	camel_stream_class->flush = stream_flush;
	camel_stream_class->eos = stream_eos;
	camel_stream_class->reset = stream_reset;
}

CamelType
camel_stream_get_type (void)
{
	static CamelType camel_stream_type = CAMEL_INVALID_TYPE;

	if (camel_stream_type == CAMEL_INVALID_TYPE) {
		camel_stream_type = camel_type_register( CAMEL_OBJECT_TYPE,
							 "CamelStream",
							 sizeof( CamelStream ),
							 sizeof( CamelStreamClass ),
							 (CamelObjectClassInitFunc) camel_stream_class_init,
							 NULL,
							 NULL,
							 NULL );
	}

	return camel_stream_type;
}

/**
 * camel_stream_read:
 * @stream: a CamelStream.
 * @buffer: buffer where bytes pulled from the stream are stored.
 * @n: max number of bytes to read.
 *
 * Read at most @n bytes from the @stream object and stores them
 * in the buffer pointed at by @buffer.
 *
 * Return value: number of bytes actually read, or -1 on error and
 * set errno.
 **/
ssize_t
camel_stream_read (CamelStream *stream, char *buffer, size_t n)
{
	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);
	g_return_val_if_fail (n == 0 || buffer, -1);

	return CS_CLASS (stream)->read (stream, buffer, n);
}

/**
 * camel_stream_write:
 * @stream: a CamelStream object.
 * @buffer: buffer to write.
 * @n: number of bytes to write
 *
 * Write @n bytes from the buffer pointed at by @buffer into @stream.
 *
 * Return value: the number of bytes actually written to the stream,
 * or -1 on error.
 **/
ssize_t
camel_stream_write (CamelStream *stream, const char *buffer, size_t n)
{
	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);
	g_return_val_if_fail (n == 0 || buffer, -1);

	return CS_CLASS (stream)->write (stream, buffer, n);
}

/**
 * camel_stream_flush:
 * @stream: a CamelStream object
 *
 * Flushes the contents of the stream to its backing store. Only meaningful
 * on writable streams.
 *
 * Return value: -1 on error.
 **/
int
camel_stream_flush (CamelStream *stream)
{
	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);

	return CS_CLASS (stream)->flush (stream);
}

/**
 * camel_stream_close:
 * @stream: 
 * 
 * Close a stream.
 * 
 * Return value: -1 on error.
 **/
int
camel_stream_close (CamelStream *stream)
{
	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);

	return CS_CLASS (stream)->close (stream);
}

/**
 * camel_stream_eos:
 * @stream: a CamelStream object
 *
 * Test if there are bytes left to read on the @stream object.
 *
 * Return value: %TRUE if all the contents on the stream has been read, or
 * %FALSE if information is still available.
 **/
gboolean
camel_stream_eos (CamelStream *stream)
{
	g_return_val_if_fail (CAMEL_IS_STREAM (stream), TRUE);

	return CS_CLASS (stream)->eos (stream);
}

/**
 * camel_stream_reset: reset a stream
 * @stream: the stream object
 *
 * Reset a stream. That is, put it in a state where it can be read
 * from the beginning again. Not all streams in Camel are seekable,
 * but they must all be resettable.
 *
 * Return value: -1 on error.
 **/
int
camel_stream_reset (CamelStream *stream)
{
	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);

	return CS_CLASS (stream)->reset (stream);
}

/***************** Utility functions ********************/

/**
 * camel_stream_write_string:
 * @stream: a stream object
 * @string: a string
 *
 * Writes the string to the stream.
 *
 * Return value: the number of characters output, -1 on error.
 **/
ssize_t
camel_stream_write_string (CamelStream *stream, const char *string)
{
	return camel_stream_write (stream, string, strlen (string));
}

/* If we are using GNU libc we can take advantage of a neat stdio function
   that lets us format strings directly to our stream without having to
   go through that g_strdup_printf crap first (which is memory heavy) */

#ifdef HAVE_FOPENCOOKIE
static ssize_t cookie_read(void *cookie, char *buf, size_t len)
{
	/* Only using for printf() so we dont need read to work */
	return -1;
}

static ssize_t cookie_write(void *cookie, const char *buf, size_t len)
{
	CamelStream *stream = cookie;

	return camel_stream_write(stream, buf, len);
}

static int cookie_seek(void *cookie, off_t pos, int w)
{
	/* Only using for printf(), dont let seek work */
	return -1;
}

static int cookie_close(void *cookie)
{
	/* noop */
	return 0;
}

static cookie_io_functions_t cookie_iofunc = {
	cookie_read, cookie_write, cookie_seek, cookie_close,
};

#endif /* HAVE_FOPENCOOKIE */

/**
 * camel_stream_printf:
 * @stream: a stream object
 * @fmt: a printf-style format string
 *
 * This printfs the given data to @stream.
 *
 * Return value: the number of characters output, -1 on error.
 **/
ssize_t
camel_stream_printf (CamelStream *stream, const char *fmt, ... )
{
	va_list args;
	char *string;
	ssize_t ret = -1;
#ifdef HAVE_FOPENCOOKIE
	FILE *fp;
#endif

	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);

	va_start (args, fmt);

#ifdef HAVE_FOPENCOOKIE
	/* Should we cache this and use this always? */
	fp = fopencookie(stream, "w", cookie_iofunc);
	if (fp) {
		ret = vfprintf(fp, fmt, args);
		fclose(fp);
	} else {
#endif
		string = g_strdup_vprintf (fmt, args);
		if (string) {
			ret = camel_stream_write(stream, string, strlen(string));
			g_free (string);
		}
#ifdef HAVE_FOPENCOOKIE
	}
#endif
	va_end(args);

	return ret;
}

/**
 * camel_stream_write_to_stream:
 * @stream: Source CamelStream.
 * @output_stream: Destination CamelStream.
 *
 * Write all of a stream (until eos) into another stream, in a blocking
 * fashion.
 *
 * Return value: Returns -1 on error, or the number of bytes succesfully
 * copied across streams.
 **/
ssize_t
camel_stream_write_to_stream (CamelStream *stream, CamelStream *output_stream)
{
	char tmp_buf[4096];
	ssize_t total = 0;
	ssize_t nb_read;
	ssize_t nb_written;

	g_return_val_if_fail (CAMEL_IS_STREAM (stream), -1);
	g_return_val_if_fail (CAMEL_IS_STREAM (output_stream), -1);

	while (!camel_stream_eos (stream)) {
		nb_read = camel_stream_read (stream, tmp_buf, sizeof (tmp_buf));
		if (nb_read < 0)
			return -1;
		else if (nb_read > 0) {
			nb_written = 0;

			while (nb_written < nb_read) {
				ssize_t len = camel_stream_write (output_stream, tmp_buf + nb_written,
								  nb_read - nb_written);
				if (len < 0)
					return -1;
				nb_written += len;
			}
			total += nb_written;
		}
	}
	return total;
}
