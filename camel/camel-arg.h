/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Author:
 *  Michael Zucchi <notzed@ximian.com>
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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_ARG_H
#define CAMEL_ARG_H

#include <glib.h>
#include <stdarg.h>

G_BEGIN_DECLS

enum camel_arg_t {
	CAMEL_ARG_END = 0,
	CAMEL_ARG_IGNORE = 1,	/* override/ignore an arg in-place */

	CAMEL_ARG_FIRST = 1024,	/* 1024 args reserved for arg system */

	CAMEL_ARG_TYPE = 0xf0000000, /* type field for tags */
	CAMEL_ARG_TAG = 0x0fffffff, /* tag field for args */

	CAMEL_ARG_OBJ = 0x00000000, /* object */
	CAMEL_ARG_INT = 0x10000000, /* gint */
	CAMEL_ARG_DBL = 0x20000000, /* gdouble */
	CAMEL_ARG_STR = 0x30000000, /* c string */
	CAMEL_ARG_PTR = 0x40000000, /* ptr */
	CAMEL_ARG_BOO = 0x50000000  /* bool */
};

typedef struct _CamelArg CamelArg;
typedef struct _CamelArgV CamelArgV;

typedef struct _CamelArgGet CamelArgGet;
typedef struct _CamelArgGetV CamelArgGetV;

struct _CamelArg {
	guint32 tag;
	union {
		gpointer ca_object;
		gint ca_int;
		gdouble ca_double;
		gchar *ca_str;
		gpointer ca_ptr;
	} u;
};
struct _CamelArgGet {
	guint32 tag;
	union {
		gpointer *ca_object;
		gint *ca_int;
		gdouble *ca_double;
		gchar **ca_str;
		gpointer *ca_ptr;
	} u;
};

#define ca_object u.ca_object
#define ca_int u.ca_int
#define ca_double u.ca_double
#define ca_str u.ca_str
#define ca_ptr u.ca_ptr

#define CAMEL_ARGV_MAX (20)

struct _CamelArgV {
	va_list ap;
	gint argc;
	CamelArg argv[CAMEL_ARGV_MAX];
};

struct _CamelArgGetV {
	va_list ap;
	gint argc;
	CamelArgGet argv[CAMEL_ARGV_MAX];
};

#define camel_argv_start(tv, last) va_start((tv)->ap, last)
#define camel_argv_end(tv) va_end((tv)->ap)
gint camel_argv_build(CamelArgV *tv);
gint camel_arggetv_build(CamelArgGetV *tv);

/* set an arg ignored */
#define camel_argv_ignore(tv, i) ((tv)->argv[i].tag = ((tv)->argv[i].tag & CAMEL_ARG_TYPE) | CAMEL_ARG_IGNORE)

/* 'self-describing' property list */
typedef struct _CamelProperty CamelProperty;

struct _CamelProperty {
	guint32 tag;
	const gchar *name;
	const gchar *description;
};

G_END_DECLS

#endif /* CAMEL_ARG_H */
