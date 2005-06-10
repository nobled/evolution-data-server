
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "camel-record.h"

CamelRecordEncoder *camel_record_encoder_new(void)
{
	CamelRecordEncoder *cde = g_malloc(sizeof(*cde));

	cde->out = g_byte_array_new();
	cde->section = -1;

	return cde;
}

void camel_record_encoder_free(CamelRecordEncoder *cde)
{
	g_byte_array_free(cde->out, TRUE);
	g_free(cde);
}

void camel_record_encoder_reset(CamelRecordEncoder *cde)
{
	g_byte_array_set_size(cde->out, 0);
}

void camel_record_encoder_start_section(CamelRecordEncoder *cde, int tag, int version)
{
	char bytes[4];

#ifdef BE
	/* set top bit of tag?  store endian-ness somewhere else? */
#endif
	bytes[0] = tag;
	bytes[1] = version;
	bytes[2] = 0xff;
	bytes[3] = 0xff;

	cde->section = cde->out->len;
	g_byte_array_append(cde->out, bytes, 4);
}

void camel_record_encoder_end_section(CamelRecordEncoder *cde)
{
	guint16 len;

	g_assert(cde->section >= 0 && cde->section < cde->out->len);

	len = cde->out->len - cde->section - 4;
	memcpy(&cde->out->data[cde->section+2], &len, 2);
	cde->section = -1;
}

void camel_record_encoder_string(CamelRecordEncoder *cde, const char *s)
{
	if (s == NULL)
		s = "";
	g_byte_array_append(cde->out, s, strlen(s)+1);
}

void camel_record_encoder_int8(CamelRecordEncoder *cde, guint8 v)
{
	g_byte_array_append(cde->out, &v, sizeof(v));
}

void camel_record_encoder_int32(CamelRecordEncoder *cde, guint32 v)
{
	g_byte_array_append(cde->out, (const guint8 *)&v, sizeof(v));
}

void camel_record_encoder_int64(CamelRecordEncoder *cde, guint64 v)
{
	g_byte_array_append(cde->out, (const guint8 *)&v, sizeof(v));
}

void camel_record_encoder_timet(CamelRecordEncoder *cde, time_t v)
{
	g_byte_array_append(cde->out, (const guint8 *)&v, sizeof(v));
}

void camel_record_encoder_sizet(CamelRecordEncoder *cde, size_t v)
{
	g_byte_array_append(cde->out, (const guint8 *)&v, sizeof(v));
}

CamelRecordDecoder *camel_record_decoder_new(const unsigned char *data, int len)
{
	CamelRecordDecoder *cdd;

	cdd = g_malloc(sizeof(*cdd));
	cdd->data = data;
	cdd->dataend = data + len;

	cdd->pos = NULL;
	cdd->end = NULL;

	return cdd;
}

void camel_record_decoder_free(CamelRecordDecoder *cdd)
{
	g_free(cdd);
}

void camel_record_decoder_reset(CamelRecordDecoder *cdd)
{
	cdd->pos = NULL;
	cdd->end = NULL;
}

int camel_record_decoder_next_section(CamelRecordDecoder *cdd, int *verp)
{
	const unsigned char *p;
	int tag, ver;
	guint16 len;

	if (cdd->pos)
		p = cdd->end;
	else
		p = cdd->data;

	if (cdd->dataend - p < 4)
		return CR_SECTION_INVALID;

	tag = p[0];
	ver = (int)p[1];
	memcpy(&len, p+2, 2);
	p+=4;

	cdd->end = p + len;
	cdd->pos = p;

	if (verp)
		*verp = ver;

	if (cdd->end > cdd->dataend)
		cdd->end = cdd->dataend;

	return tag;
}

guint8 camel_record_decoder_int8(CamelRecordDecoder *cdd)
{
	if (cdd->pos < cdd->end)
		return *(cdd->pos++);
	else
		return 0;
}

guint32 camel_record_decoder_int32(CamelRecordDecoder *cdd)
{
	guint32 v;

	if (cdd->pos+sizeof(v)-1 < cdd->end) {
		memcpy(&v, cdd->pos, sizeof(v));
		cdd->pos += sizeof(v);
		return v;
	} else
		return 0;
}

guint64 camel_record_decoder_int64(CamelRecordDecoder *cdd)
{
	guint64 v;

	if (cdd->pos+sizeof(v)-1 < cdd->end) {
		memcpy(&v, cdd->pos, sizeof(v));
		cdd->pos += sizeof(v);
		return v;
	} else
		return 0;
}

time_t camel_record_decoder_timet(CamelRecordDecoder *cdd)
{
	time_t v;

	if (cdd->pos+sizeof(v)-1 < cdd->end) {
		memcpy(&v, cdd->pos, sizeof(v));
		cdd->pos += sizeof(v);
		return v;
	} else
		return 0;
}

size_t camel_record_decoder_sizet(CamelRecordDecoder *cdd)
{
	size_t v;

	if (cdd->pos+sizeof(v)-1 < cdd->end) {
		memcpy(&v, cdd->pos, sizeof(v));
		cdd->pos += sizeof(v);
		return v;
	} else
		return 0;
}

const char *camel_record_decoder_string(CamelRecordDecoder *cdd)
{
	const unsigned char *s, *p;

	p = s = cdd->pos;
	while (p<cdd->end && *p)
		p++;
	if (p<cdd->end) {
		cdd->pos = p+1;
		return (const char *)s;
	} else {
		cdd->pos = p;
		return "";
	}
}
