/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/* camel-mime-message.c : class for a mime_message */

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
#include "camel-mime-message.h"
#include <stdio.h>
#include "gmime-content-field.h"
#include "string-utils.h"
#include "hash-table-utils.h"

#define d(x)

/* these 2 below should be kept in sync */
typedef enum {
	HEADER_UNKNOWN,
	HEADER_FROM,
	HEADER_REPLY_TO,
	HEADER_SUBJECT,
	HEADER_TO,
	HEADER_CC,
	HEADER_BCC,
	HEADER_DATE
} CamelHeaderType;

static char *header_names[] = {
	/* dont include HEADER_UNKNOWN string */
	"From", "Reply-To", "Subject", "To", "Cc", "Bcc", "Date", NULL
};

static GHashTable *header_name_table;

static CamelMimePartClass *parent_class=NULL;

static char *recipient_names[] = {
	"To", "Cc", "Bcc", NULL
};

static int write_to_stream (CamelDataWrapper *data_wrapper, CamelStream *stream);
static void add_header (CamelMedium *medium, const char *header_name, const void *header_value);
static void set_header (CamelMedium *medium, const char *header_name, const void *header_value);
static void remove_header (CamelMedium *medium, const char *header_name);
static int construct_from_parser (CamelMimePart *, CamelMimeParser *);
static void g_lib_is_uber_crappy_shit(gpointer whocares, gpointer getlost, gpointer blah);

/* Returns the class for a CamelMimeMessage */
#define CMM_CLASS(so) CAMEL_MIME_MESSAGE_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CDW_CLASS(so) CAMEL_DATA_WRAPPER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CMD_CLASS(so) CAMEL_MEDIUM_CLASS (CAMEL_OBJECT_GET_CLASS(so))

static void
camel_mime_message_class_init (CamelMimeMessageClass *camel_mime_message_class)
{
	CamelDataWrapperClass *camel_data_wrapper_class = CAMEL_DATA_WRAPPER_CLASS (camel_mime_message_class);
	CamelMimePartClass *camel_mime_part_class = CAMEL_MIME_PART_CLASS (camel_mime_message_class);
	CamelMediumClass *camel_medium_class = CAMEL_MEDIUM_CLASS (camel_mime_message_class);
	int i;
	
	parent_class = CAMEL_MIME_PART_CLASS(camel_type_get_global_classfuncs (camel_mime_part_get_type ()));

	header_name_table = g_hash_table_new (g_strcase_hash, g_strcase_equal);
	for (i=0;header_names[i];i++)
		g_hash_table_insert (header_name_table, header_names[i], (gpointer)i+1);

	/* virtual method overload */
	camel_data_wrapper_class->write_to_stream = write_to_stream;

	camel_medium_class->add_header = add_header;
	camel_medium_class->set_header = set_header;
	camel_medium_class->remove_header = remove_header;
	
	camel_mime_part_class->construct_from_parser = construct_from_parser;
}




static void
camel_mime_message_init (gpointer object, gpointer klass)
{
	CamelMimeMessage *mime_message = (CamelMimeMessage *)object;
	int i;
	
	camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (object), "message/rfc822");

	mime_message->recipients =  g_hash_table_new(g_strcase_hash, g_strcase_equal);
	for (i=0;recipient_names[i];i++) {
		g_hash_table_insert(mime_message->recipients, recipient_names[i], camel_internet_address_new());
	}

	mime_message->subject = NULL;
	mime_message->reply_to = NULL;
	mime_message->from = NULL;
	mime_message->date = CAMEL_MESSAGE_DATE_CURRENT;
	mime_message->date_offset = 0;
	mime_message->date_str = NULL;
}

static void           
camel_mime_message_finalize (CamelObject *object)
{
	CamelMimeMessage *message = CAMEL_MIME_MESSAGE (object);
	
	g_free (message->date_str);
	g_free (message->subject);
	g_free (message->reply_to);
	g_free (message->from);

	g_hash_table_foreach (message->recipients, g_lib_is_uber_crappy_shit, NULL);
	g_hash_table_destroy(message->recipients);
}


CamelType
camel_mime_message_get_type (void)
{
	static CamelType camel_mime_message_type = CAMEL_INVALID_TYPE;
	
	if (camel_mime_message_type == CAMEL_INVALID_TYPE)	{
		camel_mime_message_type = camel_type_register (camel_mime_part_get_type(), "CamelMimeMessage",
							       sizeof (CamelMimeMessage),
							       sizeof (CamelMimeMessageClass),
							       (CamelObjectClassInitFunc) camel_mime_message_class_init,
							       NULL,
							       (CamelObjectInitFunc) camel_mime_message_init,
							       (CamelObjectFinalizeFunc) camel_mime_message_finalize);
	}
	
	return camel_mime_message_type;
}

/* annoying way to free objects in a hashtable, i mean, its not like anyone
   would want to store them in a hashtable, really */
/* peterw: somebody's not bitter :-) */
static void g_lib_is_uber_crappy_shit(gpointer whocares, gpointer getlost, gpointer blah)
{
	camel_object_unref((CamelObject *)getlost);
}



CamelMimeMessage *
camel_mime_message_new (void) 
{
	CamelMimeMessage *mime_message;
	mime_message = CAMEL_MIME_MESSAGE(camel_object_new (CAMEL_MIME_MESSAGE_TYPE));
	
	return mime_message;
}


/* **** Date: */

void
camel_mime_message_set_date(CamelMimeMessage *message,  time_t date, int offset)
{
	g_assert(message);
	if (date == CAMEL_MESSAGE_DATE_CURRENT) {
		struct tm *local;
		int tz;

		date = time(0);
		local = localtime(&date);
		offset = 0;
#if defined(HAVE_TIMEZONE)
		tz = timezone;
#elif defined(HAVE_TM_GMTOFF)
		tz = local->tm_gmtoff;
#endif
		offset = ((tz/60/60) * 100) + (tz/60 % 60);
	}
	message->date = date;
	message->date_offset = offset;
	g_free(message->date_str);
	message->date_str = header_format_date(date, offset);

	CAMEL_MEDIUM_CLASS(parent_class)->set_header((CamelMedium *)message, "Date", message->date_str);
}

void
camel_mime_message_get_date(CamelMimeMessage *message,  time_t *date, int *offset)
{
	if (message->date == CAMEL_MESSAGE_DATE_CURRENT)
		camel_mime_message_set_date(message, CAMEL_MESSAGE_DATE_CURRENT, 0);
	if (date)
		*date = message->date;
	if (offset)
		*offset = message->date_offset;
}

char *
camel_mime_message_get_date_string(CamelMimeMessage *message)
{
	if (message->date == CAMEL_MESSAGE_DATE_CURRENT)
		camel_mime_message_set_date(message, CAMEL_MESSAGE_DATE_CURRENT, 0);
	return message->date_str;
}

/* **** Reply-To: */

void
camel_mime_message_set_reply_to (CamelMimeMessage *mime_message, const gchar *reply_to)
{
	g_assert (mime_message);

	/* FIXME: check format of string, handle it nicer ... */

	g_free(mime_message->reply_to);
	mime_message->reply_to = g_strdup(reply_to);
	CAMEL_MEDIUM_CLASS(parent_class)->set_header((CamelMedium *)mime_message, "Reply-To", reply_to);
}

const gchar *
camel_mime_message_get_reply_to (CamelMimeMessage *mime_message)
{
	g_assert (mime_message);

	return mime_message->reply_to;
}

void
camel_mime_message_set_subject (CamelMimeMessage *mime_message,
				const gchar *subject)
{
	char *text;
	g_assert (mime_message);

	g_free(mime_message->subject);
	mime_message->subject = g_strstrip (g_strdup (subject));
	text = header_encode_string(subject);
	CAMEL_MEDIUM_CLASS(parent_class)->set_header((CamelMedium *)mime_message, "Subject", text);
	g_free(text);
}

const gchar *
camel_mime_message_get_subject (CamelMimeMessage *mime_message)
{
	g_assert (mime_message);

	return mime_message->subject;
}

/* *** From: */
void
camel_mime_message_set_from (CamelMimeMessage *mime_message, const gchar *from)
{
	g_assert (mime_message);

	g_free(mime_message->from);
	mime_message->from = g_strdup(from);
	CAMEL_MEDIUM_CLASS(parent_class)->set_header((CamelMedium *)mime_message, "From", from);
}

const gchar *
camel_mime_message_get_from (CamelMimeMessage *mime_message)
{
	g_assert (mime_message);

	return mime_message->from;
}

/*  ****  */

void
camel_mime_message_add_recipient (CamelMimeMessage *mime_message, 
				  const gchar *type, 
				  const gchar *name, const char *address)
{
	CamelInternetAddress *addr;
	char *text;

	g_assert (mime_message);

	addr = g_hash_table_lookup(mime_message->recipients, type);
	if (addr == NULL) {
		g_warning("trying to add a non-valid receipient type: %s = %s %s", type, name, address);
		return;
	}

	camel_internet_address_add(addr, name, address);

	/* FIXME: maybe this should be delayed till we're ready to write out? */
	text = camel_address_encode((CamelAddress*)addr);
	CAMEL_MEDIUM_CLASS(parent_class)->set_header((CamelMedium *)mime_message, type, text);
	g_free(text);
}

void
camel_mime_message_remove_recipient_address (CamelMimeMessage *mime_message,
					     const gchar *type,
					     const gchar *address)
{
	CamelInternetAddress *addr;
	int index;
	char *text;


	g_assert (mime_message);

	addr = g_hash_table_lookup(mime_message->recipients, type);
	if (addr == NULL) {
		g_warning("trying to remove a non-valid receipient type: %s = %s", type, address);
		return;
	}
	index = camel_internet_address_find_address(addr, address, NULL);
	if (index == -1) {
		g_warning("trying to remove address for nonexistand address: %s", address);
		return;
	}

	camel_address_remove((CamelAddress *)addr, index);

	/* FIXME: maybe this should be delayed till we're ready to write out? */
	text = camel_address_encode((CamelAddress *)addr);
	CAMEL_MEDIUM_CLASS(parent_class)->set_header((CamelMedium *)mime_message, type, text);
	g_free(text);
}

void
camel_mime_message_remove_recipient_name (CamelMimeMessage *mime_message,
					  const gchar *type,
					  const gchar *name)
{
	CamelInternetAddress *addr;
	int index;
	char *text;

	g_assert (mime_message);

	addr = g_hash_table_lookup(mime_message->recipients, type);
	if (addr == NULL) {
		g_warning("trying to remove a non-valid receipient type: %s = %s", type, name);
		return;
	}
	index = camel_internet_address_find_name(addr, name, NULL);
	if (index == -1) {
		g_warning("trying to remove address for nonexistand name: %s", name);
		return;
	}

	camel_address_remove((CamelAddress *)addr, index);

	/* FIXME: maybe this should be delayed till we're ready to write out? */
	text = camel_address_encode((CamelAddress *)addr);
	CAMEL_MEDIUM_CLASS(parent_class)->set_header((CamelMedium *)mime_message, type, text);
	g_free(text);
}

const CamelInternetAddress *
camel_mime_message_get_recipients (CamelMimeMessage *mime_message, 
				   const gchar *type)
{
	g_assert (mime_message);
	
	return g_hash_table_lookup(mime_message->recipients, type);
}



/* mime_message */
static int
construct_from_parser(CamelMimePart *dw, CamelMimeParser *mp)
{
	char *buf;
	int len;
	int state;
	int ret;

	d(printf("constructing mime-message\n"));

	d(printf("mime_message::construct_from_parser()\n"));

	/* let the mime-part construct the guts ... */
	ret = ((CamelMimePartClass *)parent_class)->construct_from_parser(dw, mp);

	if (ret == -1)
		return -1;

	/* ... then clean up the follow-on state */
	state = camel_mime_parser_step(mp, &buf, &len);
	switch (state) {
	case HSCAN_EOF: case HSCAN_FROM_END: /* these doesn't belong to us */
		camel_mime_parser_unstep(mp);
	case HSCAN_MESSAGE_END:
		break;
	default:
		g_error("Bad parser state: Expecing MESSAGE_END or EOF or EOM, got: %d", camel_mime_parser_state(mp));
		camel_mime_parser_unstep(mp);
		return -1;
	}

	d(printf("mime_message::construct_from_parser() leaving\n"));
#warning "return a real error code"
	return 0;
}

static int
write_to_stream (CamelDataWrapper *data_wrapper, CamelStream *stream)
{
	CamelMimeMessage *mm = CAMEL_MIME_MESSAGE (data_wrapper);

	/* force mandatory headers ... */
	if (mm->from == NULL) {
		g_warning("No from set for message");
		camel_mime_message_set_from(mm, "");
	}
	if (mm->date_str == NULL) {
		g_warning("Application did not set date, using 'now'");
		camel_mime_message_set_date(mm, CAMEL_MESSAGE_DATE_CURRENT, 0);
	}
	if (mm->subject == NULL) {
		g_warning("Application did not set subject, creating one");
		camel_mime_message_set_subject(mm, "No Subject");
	}

	/* FIXME: "To" header needs to be set explicitly as well ... */

	if (!camel_medium_get_header ((CamelMedium *)mm, "Mime-Version"))
		camel_medium_set_header((CamelMedium *)mm, "Mime-Version", "1.0");

	return CAMEL_DATA_WRAPPER_CLASS (parent_class)->write_to_stream (data_wrapper, stream);
}

static char *
format_address(const char *text)
{
	struct _header_address *addr;
	char *ret;

	addr = header_address_decode(text);
	if (addr) {
		ret = header_address_list_format(addr);
		header_address_list_clear(&addr);
	} else {
		ret = g_strdup(text);
	}
	return ret;
}

/* FIXME: check format of fields. */
static gboolean
process_header(CamelMedium *medium, const char *header_name, const char *header_value)
{
	CamelHeaderType header_type;
	CamelMimeMessage *message = CAMEL_MIME_MESSAGE (medium);
	CamelInternetAddress *addr;

	header_type = (CamelHeaderType) g_hash_table_lookup (header_name_table, header_name);
	switch (header_type) {
	case HEADER_FROM:
		g_free(message->from);
		message->from = format_address(header_value);
		break;
	case HEADER_REPLY_TO:
		g_free(message->reply_to);
		message->reply_to = format_address(header_value);
		break;
	case HEADER_SUBJECT:
		g_free(message->subject);
		message->subject = g_strstrip (header_decode_string(header_value));
		break;
	case HEADER_TO:
	case HEADER_CC:
	case HEADER_BCC:
		addr = g_hash_table_lookup(message->recipients, header_name);
		if (header_value)
			camel_address_decode((CamelAddress *)addr, header_value);
		else
			camel_address_remove((CamelAddress *)addr, -1);
		break;
	case HEADER_DATE:
		g_free(message->date_str);
		message->date_str = g_strdup(header_value);
		if (header_value) {
			message->date = header_decode_date(header_value, &message->date_offset);
		} else {
			message->date = CAMEL_MESSAGE_DATE_CURRENT;
		}
		break;
	default:
		return FALSE;
	}
	return TRUE;
}

static void
set_header(CamelMedium *medium, const char *header_name, const void *header_value)
{
	process_header(medium, header_name, header_value);
	parent_class->parent_class.set_header (medium, header_name, header_value);
}

static void
add_header(CamelMedium *medium, const char *header_name, const void *header_value)
{
	/* if we process it, then it must be forced unique as well ... */
	if (process_header(medium, header_name, header_value))
		parent_class->parent_class.set_header (medium, header_name, header_value);
	else
		parent_class->parent_class.add_header (medium, header_name, header_value);
}

static void
remove_header(CamelMedium *medium, const char *header_name)
{
	process_header(medium, header_name, NULL);
	parent_class->parent_class.remove_header (medium, header_name);
}

