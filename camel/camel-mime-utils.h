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

#ifndef _CAMEL_MIME_UTILS_H
#define _CAMEL_MIME_UTILS_H

struct _header_param {
	struct _header_param *next;
	char *name;
	char *value;
};

/* FIXME: this should probably be refcounted */
struct _header_content_type {
	char *type;
	char *subtype;
	struct _header_param *params;
};

/* a raw rfc822 header */
/* the value MUST be US-ASCII */
struct _header_raw {
	struct _header_raw *next;
	char *name;
	char *value;
	int offset;		/* in file, if known */
};

struct _header_content_type *header_content_type_new(const char *type, const char *subtype);
struct _header_content_type *header_content_type_decode(const char *in);
void header_content_type_free(struct _header_content_type *ct);
const char *header_content_type_param(struct _header_content_type *t, const char *name);
void header_content_type_set_param(struct _header_content_type *t, const char *name, const char *value);
int header_content_type_is(struct _header_content_type *t, char *type, char *subtype);

char *header_param(struct _header_param *p, char *name);
struct _header_param *header_set_param(struct _header_param **l, const char *name, const char *value);

/* decode the contents of a content-encoding header */
char *header_content_encoding_decode(const char *in);

/* working with lists of headers */
void header_raw_append(struct _header_raw **list, const char *name, const char *value, int offset);
void header_raw_append_parse(struct _header_raw **list, const char *header, int offset);
const char *header_raw_find(struct _header_raw **list, const char *name, int *ofset);
void header_raw_replace(struct _header_raw **list, const char *name, const char *value, int offset);
void header_raw_remove(struct _header_raw **list, const char *name);
void header_raw_clear(struct _header_raw **list);

/* raw header parsing functions */
char *header_decode_token(const char **in);

/* do incremental base64/quoted-printable (de/en)coding */
int base64_decode_step(unsigned char *in, int len, unsigned char *out, int *state, unsigned int *save);

int base64_encode_step(unsigned char *in, int len, unsigned char *out, int *state, int *save);
int base64_encode_close(unsigned char *in, int inlen, unsigned char *out, int *state, int *save);

int quoted_decode_step(unsigned char *in, int len, unsigned char *out, int *savestate, int *saveme);

int quoted_encode_step(unsigned char *in, int len, unsigned char *out, int *state, int *save);
int quoted_encode_close(unsigned char *in, int len, unsigned char *out, int *state, int *save);

#endif /* ! _CAMEL_MIME_UTILS_H */
