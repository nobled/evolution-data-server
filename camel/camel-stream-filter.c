/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/*
 *  Copyright (C) 2000 Helix Code Inc.
 *
 *  Authors: Michael Zucchi <notzed@helixcode.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public License
 *  as published by the Free Software Foundation; either version 2 of
 *  the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "camel-stream-filter.h"

struct _filter {
	struct _filter *next;
	int id;
	CamelMimeFilter *filter;
};

struct _CamelStreamFilterPrivate {
	struct _filter *filters;
	int filterid;		/* next filter id */
	
	char *realbuffer;	/* buffer - READ_PAD */
	char *buffer;		/* READ_SIZE bytes */

	char *filtered;		/* the filtered data */
	size_t filteredlen;

	int last_was_read;	/* was the last op read or write? */
};

#define READ_PAD (64)		/* bytes padded before buffer */
#define READ_SIZE (4096)

#define _PRIVATE(o) (((CamelStreamFilter *)(o))->priv)

static void camel_stream_filter_class_init (CamelStreamFilterClass *klass);
static void camel_stream_filter_init       (CamelStreamFilter *obj);

static	ssize_t   do_read       (CamelStream *stream, char *buffer, size_t n);
static	ssize_t   do_write      (CamelStream *stream, const char *buffer, size_t n);
static	int       do_flush      (CamelStream *stream);
static	int       do_close      (CamelStream *stream);
static	gboolean  do_eos        (CamelStream *stream);
static	int       do_reset      (CamelStream *stream);

static CamelStreamClass *camel_stream_filter_parent;

static void
camel_stream_filter_class_init (CamelStreamFilterClass *klass)
{
	CamelStreamClass *camel_stream_class = (CamelStreamClass *) klass;

	camel_stream_filter_parent = CAMEL_STREAM_CLASS (camel_type_get_global_classfuncs (camel_stream_get_type ()));

	camel_stream_class->read = do_read;
	camel_stream_class->write = do_write;
	camel_stream_class->flush = do_flush;
	camel_stream_class->close = do_close;
	camel_stream_class->eos = do_eos; 
	camel_stream_class->reset = do_reset;

}

static void
camel_stream_filter_init (CamelStreamFilter *obj)
{
	struct _CamelStreamFilterPrivate *p;
	
	_PRIVATE(obj) = p = g_malloc0(sizeof(*p));
	p->realbuffer = g_malloc(READ_SIZE + READ_PAD);
	p->buffer = p->realbuffer + READ_PAD;
	p->last_was_read = TRUE;
}

static void
camel_stream_filter_finalize(CamelObject *o)
{
	CamelStreamFilter *filter = (CamelStreamFilter *)o;
	struct _CamelStreamFilterPrivate *p = _PRIVATE(filter);
	struct _filter *fn, *f;

	f = p->filters;
	while (f) {
		fn = f->next;
		camel_object_unref((CamelObject *)f->filter);
		g_free(f);
		f = fn;
	}
	g_free(p->realbuffer);
	g_free(p);
	camel_object_unref((CamelObject *)filter->source);
}


CamelType
camel_stream_filter_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (CAMEL_STREAM_TYPE, "CamelStreamFilter",
					    sizeof (CamelStreamFilter),
					    sizeof (CamelStreamFilterClass),
					    (CamelObjectClassInitFunc) camel_stream_filter_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_stream_filter_init,
					    (CamelObjectFinalizeFunc) camel_stream_filter_finalize);
	}
	
	return type;
}


/**
 * camel_stream_filter_new:
 *
 * Create a new CamelStreamFilter object.
 * 
 * Return value: A new CamelStreamFilter object.
 **/
CamelStreamFilter *
camel_stream_filter_new_with_stream(CamelStream *stream)
{
	CamelStreamFilter *new = CAMEL_STREAM_FILTER ( camel_object_new (camel_stream_filter_get_type ()));

	new->source = stream;
	camel_object_ref ((CamelObject *)stream);

	return new;
}


/**
 * camel_stream_filter_add:
 * @filter: Initialised CamelStreamFilter.
 * @mf:  Filter to perform processing on stream.
 * 
 * Add a new CamelMimeFilter to execute during the processing of this
 * stream.  Each filter added is processed after the previous one.
 *
 * Note that a filter should only be added to a single stream
 * at a time, otherwise unpredictable results may occur.
 * 
 * Return value: A filter id for this CamelStreamFilter.
 **/
int
camel_stream_filter_add(CamelStreamFilter *filter, CamelMimeFilter *mf)
{
	struct _CamelStreamFilterPrivate *p = _PRIVATE(filter);
	struct _filter *fn, *f;

	fn = g_malloc(sizeof(*fn));
	fn->id = p->filterid++;
	fn->filter = mf;
	camel_object_ref((CamelObject *)mf);

	/* sure, we could use a GList, but we wouldn't save much */
	f = (struct _filter *)&p->filters;
	while (f->next)
		f = f->next;
	f->next = fn;
	fn->next = NULL;
	return fn->id;
}

/**
 * camel_stream_filter_remove:
 * @filter: Initialised CamelStreamFilter.
 * @id: Filter id, as returned from camel_stream_filter_add().
 * 
 * Remove a processing filter from the stream, by id.
 **/
void
camel_stream_filter_remove(CamelStreamFilter *filter, int id)
{
	struct _CamelStreamFilterPrivate *p = _PRIVATE(filter);
	struct _filter *fn, *f;

	f = (struct _filter *)&p->filters;
	while (f && f->next) {
		fn = f->next;
		if (fn->id == id) {
			f->next = fn->next;
			camel_object_unref((CamelObject *)fn->filter);
			g_free(fn);
		}
		f = f->next;
	}
}

static ssize_t
do_read (CamelStream *stream, char *buffer, size_t n)
{
	CamelStreamFilter *filter = (CamelStreamFilter *)stream;
	struct _CamelStreamFilterPrivate *p = _PRIVATE(filter);
	ssize_t size;
	struct _filter *f;

	p->last_was_read = TRUE;

	if (p->filteredlen<=0) {
		int presize = READ_SIZE;

		size = camel_stream_read(filter->source, p->buffer, READ_SIZE);
		if (size <= 0) {
			/* this is somewhat untested */
			if (camel_stream_eos(filter->source)) {
				f = p->filters;
				p->filtered = p->buffer;
				p->filteredlen = 0;
				while (f) {
					camel_mime_filter_complete(f->filter, p->filtered, p->filteredlen,
								   presize, &p->filtered, &p->filteredlen, &presize);
					f = f->next;
				}
				size = p->filteredlen;
			}
			if (size <= 0)
				return size;
		} else {
			f = p->filters;
			p->filtered = p->buffer;
			p->filteredlen = size;
			while (f) {
				camel_mime_filter_filter(f->filter, p->filtered, p->filteredlen, presize,
							 &p->filtered, &p->filteredlen, &presize);
				f = f->next;
			}
		}
	}

	size = MIN(n, p->filteredlen);
	memcpy(buffer, p->filtered, size);
	p->filteredlen -= size;
	p->filtered += size;

	return size;
}

static ssize_t
do_write (CamelStream *stream, const char *buf, size_t n)
{
	CamelStreamFilter *filter = (CamelStreamFilter *)stream;
	struct _CamelStreamFilterPrivate *p = _PRIVATE(filter);
	struct _filter *f;
	int presize;
	char *buffer = (char *)buf;

	p->last_was_read = FALSE;

	f = p->filters;
	presize = 0;
	while (f) {
		camel_mime_filter_filter(f->filter, buffer, n, presize, &buffer, &n, &presize);
		f = f->next;
	}

	return camel_stream_write(filter->source, buffer, n);
}

static int
do_flush (CamelStream *stream)
{
	CamelStreamFilter *filter = (CamelStreamFilter *)stream;
	struct _CamelStreamFilterPrivate *p = _PRIVATE(filter);
	struct _filter *f;
	char *buffer;
	int len, presize;

	if (p->last_was_read) {
		g_warning("Flushing a filter stream without writing to it");
		return 0;
	}

	buffer = "";
	len = 0;
	presize = 0;
	f = p->filters;
	while (f) {
		camel_mime_filter_complete(f->filter, buffer, len, presize, &buffer, &len, &presize);
		f = f->next;
	}
	if (len > 0 && camel_stream_write(filter->source, buffer, len) == -1)
		return -1;
	return camel_stream_flush(filter->source);
}

static int
do_close (CamelStream *stream)
{
	CamelStreamFilter *filter = (CamelStreamFilter *)stream;
	struct _CamelStreamFilterPrivate *p = _PRIVATE(filter);

	if (!p->last_was_read) {
		do_flush(stream);
	}
	return camel_stream_close(filter->source);
}

static gboolean
do_eos (CamelStream *stream)
{
	CamelStreamFilter *filter = (CamelStreamFilter *)stream;
	struct _CamelStreamFilterPrivate *p = _PRIVATE(filter);

	if (p->filteredlen > 0)
		return FALSE;

	return camel_stream_eos(filter->source);
}

static int
do_reset (CamelStream *stream)
{
	CamelStreamFilter *filter = (CamelStreamFilter *)stream;
	struct _CamelStreamFilterPrivate *p = _PRIVATE(filter);
	struct _filter *f;

	p->filteredlen = 0;

	/* and reset filters */
	f = p->filters;
	while (f) {
		camel_mime_filter_reset(f->filter);
		f = f->next;
	}

	return camel_stream_reset(filter->source);
}

