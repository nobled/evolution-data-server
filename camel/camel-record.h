/*
 * Copyright (C) 2005 Novell Inc.
 *
 * Authors: Michael Zucchi <notzed@novell.com>
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

/*
 * Utilities for creating variable-sized disk records
 *
 * This is somewhat similar to the camel_file_util_encode* functions,
 * but also different:
 *  Performance over size of generated stream
 *  Data alignment is native
 *  Records broken into subsections, allowing easier versioning and
 *  backward compatbility.
 *  Memory based, not stdio.
 */

#ifndef _CAMEL_RECORD_H
#define _CAMEL_RECORD_H

#include <glib.h>
#include <sys/types.h>

#define CR_SECTION_INVALID (0x100)

typedef struct _CamelRecordEncoder CamelRecordEncoder;
struct _CamelRecordEncoder {
	GByteArray *out;
	int section;
};

CamelRecordEncoder *camel_record_encoder_new(void);
void camel_record_encoder_free(CamelRecordEncoder *cde);
void camel_record_encoder_reset(CamelRecordEncoder *cde);
void camel_record_encoder_start_section(CamelRecordEncoder *cde, int tag, int version);
void camel_record_encoder_end_section(CamelRecordEncoder *cde);

void camel_record_encoder_string(CamelRecordEncoder *cde, const char *s);
void camel_record_encoder_int8(CamelRecordEncoder *cde, guint8 v);
void camel_record_encoder_int32(CamelRecordEncoder *cde, guint32 v);
void camel_record_encoder_int64(CamelRecordEncoder *cde, guint64 v);
void camel_record_encoder_timet(CamelRecordEncoder *cde, time_t v);
void camel_record_encoder_sizet(CamelRecordEncoder *cde, size_t v);

typedef struct _CamelRecordDecoder CamelRecordDecoder;
struct _CamelRecordDecoder {
	const unsigned char *data;
	const unsigned char *dataend;

	const unsigned char *pos;
	const unsigned char *end;
};

CamelRecordDecoder *camel_record_decoder_new(const unsigned char *data, int len);
void camel_record_decoder_free(CamelRecordDecoder *cdd);
void camel_record_decoder_reset(CamelRecordDecoder *cdd);
int camel_record_decoder_next_section(CamelRecordDecoder *cdd, int *verp);

const char *camel_record_decoder_string(CamelRecordDecoder *cdd);
guint8 camel_record_decoder_int8(CamelRecordDecoder *cdd);
guint32 camel_record_decoder_int32(CamelRecordDecoder *cdd);
guint64 camel_record_decoder_int64(CamelRecordDecoder *cdd);
time_t camel_record_decoder_timet(CamelRecordDecoder *cdd);
size_t camel_record_decoder_sizet(CamelRecordDecoder *cdd);

#endif /* !_CAMEL_RECORD_H */
