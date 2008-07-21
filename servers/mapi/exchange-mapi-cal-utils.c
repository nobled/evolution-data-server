/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: 
 *    Suman Manjunath <msuman@novell.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of version 2 of the GNU Lesser General Public
 *  License as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public 
 *  License along with this program; if not, write to: 
 *  Free Software Foundation, 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 *
 */



#ifndef O_BINARY
#define O_BINARY 0
#endif

#include <glib/gstdio.h>
#include <fcntl.h>
#include <libecal/e-cal-util.h>
#include "exchange-mapi-cal-utils.h"

#define d(x) 

static void appt_build_name_id (struct mapi_nameid *nameid);
static void task_build_name_id (struct mapi_nameid *nameid);
static void note_build_name_id (struct mapi_nameid *nameid);

static icalparameter_role
get_role_from_type (OlMailRecipientType type)
{
	switch (type) {
		case olCC   : return ICAL_ROLE_OPTPARTICIPANT;
		case olOriginator : 
		case olTo   : 
		case olBCC  : 
		default     : return ICAL_ROLE_REQPARTICIPANT;
	}
}

static OlMailRecipientType
get_type_from_role (icalparameter_role role)
{
	switch (role) {
		case ICAL_ROLE_OPTPARTICIPANT 	: return olCC;
		case ICAL_ROLE_CHAIR 		:
		case ICAL_ROLE_REQPARTICIPANT 	:
		case ICAL_ROLE_NONPARTICIPANT 	: 
		default 			: return olTo;
	}
} 

static icalparameter_partstat
get_partstat_from_trackstatus (uint32_t trackstatus)
{
	switch (trackstatus) {
		case olMeetingTentative : return ICAL_PARTSTAT_TENTATIVE;
		case olMeetingAccepted  : return ICAL_PARTSTAT_ACCEPTED;
		case olMeetingDeclined  : return ICAL_PARTSTAT_DECLINED;
		default 		: return ICAL_PARTSTAT_NEEDSACTION;
	}
}

static uint32_t 
get_trackstatus_from_partstat (icalparameter_partstat partstat)
{
	switch (partstat) {
		case ICAL_PARTSTAT_ACCEPTED 	: return olMeetingAccepted;
		case ICAL_PARTSTAT_DECLINED 	: return olMeetingDeclined;
		case ICAL_PARTSTAT_TENTATIVE 	: 
		default 			: return olMeetingTentative;
	}
}

static icalproperty_transp
get_transp_from_prop (uint32_t prop) 
{
	/* FIXME: is this mapping correct ? */
	switch (prop) {
		case olFree 		:
		case olTentative 	: return ICAL_TRANSP_TRANSPARENT;
		case olBusy 		:
		case olOutOfOffice 	:
		default 		: return ICAL_TRANSP_OPAQUE;
	}
}

static uint32_t 
get_prop_from_transp (icalproperty_transp transp)
{
	/* FIXME: is this mapping correct ? */
	switch (transp) {
		case ICAL_TRANSP_TRANSPARENT 		:
		case ICAL_TRANSP_TRANSPARENTNOCONFLICT 	: return olFree; 
		case ICAL_TRANSP_OPAQUE 		: 
		case ICAL_TRANSP_OPAQUENOCONFLICT 	:
		default 				: return olBusy;
	}
}

static icalproperty_status
get_taskstatus_from_prop (uint32_t prop)
{
	/* FIXME: is this mapping correct ? */
	switch (prop) {
		case olTaskComplete 	: return ICAL_STATUS_COMPLETED;
		case olTaskWaiting 	:
		case olTaskInProgress 	: return ICAL_STATUS_INPROCESS;
		case olTaskDeferred 	: return ICAL_STATUS_CANCELLED;
		case olTaskNotStarted 	: 
		default 		: return ICAL_STATUS_NEEDSACTION;
	}
}

static uint32_t
get_prop_from_taskstatus (icalproperty_status status)
{
	/* FIXME: is this mapping correct ? */
	switch (status) {
		case ICAL_STATUS_INPROCESS 	: return olTaskInProgress;
		case ICAL_STATUS_COMPLETED 	: return olTaskComplete;
		case ICAL_STATUS_CANCELLED 	: return olTaskDeferred;
		default 			: return olTaskNotStarted;
	}
}

static icalproperty_class
get_class_from_prop (uint32_t prop)
{
	/* FIXME: is this mapping correct ? */
	switch (prop) {
		case olPersonal 	:
		case olPrivate 		: return ICAL_CLASS_PRIVATE;
		case olConfidential 	: return ICAL_CLASS_CONFIDENTIAL;
		case olNormal 		: 
		default 		: return ICAL_CLASS_PUBLIC;
	}
}

static uint32_t 
get_prop_from_class (icalproperty_class class)
{
	/* FIXME: is this mapping correct ? */
	switch (class) {
		case ICAL_CLASS_PRIVATE 	: return olPrivate;
		case ICAL_CLASS_CONFIDENTIAL 	: return olConfidential;
		default 			: return olNormal;
	}
}

static int
get_priority_from_prop (uint32_t prop)
{
	switch (prop) {
		case PRIORITY_LOW 	: return 7;
		case PRIORITY_HIGH 	: return 1;
		case PRIORITY_NORMAL 	: 
		default 		: return 5;
	}
}

static uint32_t
get_prop_from_priority (int priority)
{
	if (priority > 0 && priority <= 4)
		return PRIORITY_HIGH;
	else if (priority > 5 && priority <= 9)
		return PRIORITY_LOW;
	else
		return PRIORITY_NORMAL;
}

void
exchange_mapi_cal_util_fetch_attachments (ECalComponent *comp, GSList **attach_list, const char *local_store_uri)
{
	GSList *comp_attach_list = NULL, *new_attach_list = NULL;
	GSList *l;
	const char *uid;

	e_cal_component_get_attachment_list (comp, &comp_attach_list);
	e_cal_component_get_uid (comp, &uid);

	for (l = comp_attach_list; l ; l = l->next) {
		gchar *sfname_uri = (gchar *) l->data;
		gchar *sfname = g_filename_from_uri (sfname_uri, NULL, NULL);
		gchar *filename;
		GMappedFile *mapped_file;
		GError *error = NULL;

		mapped_file = g_mapped_file_new (sfname, FALSE, &error);
		filename = g_path_get_basename (sfname);

		if (mapped_file && g_str_has_prefix (filename, uid)) {
			ExchangeMAPIAttachment *attach_item;
			gchar *attach_crlf = exchange_lf_to_crlf (g_mapped_file_get_contents (mapped_file));
			guint filelength = strlen (attach_crlf);
			const gchar *split_name = (filename + strlen (uid) + strlen ("-"));

			new_attach_list = g_slist_append (new_attach_list, g_strdup (sfname_uri));

			attach_item = g_new0 (ExchangeMAPIAttachment, 1);
			attach_item->filename = g_strdup(split_name);
			attach_item->value = g_byte_array_sized_new (filelength);
			attach_item->value = g_byte_array_append (attach_item->value, attach_crlf, filelength + 1);
			*attach_list = g_slist_append (*attach_list, attach_item);

			g_mapped_file_free (mapped_file);
			g_free (attach_crlf);
		} else {
			g_message ("DEBUG: could not map %s: %s\n", sfname, error->message);
			g_error_free (error);
		}

		g_free (filename);
	}

	e_cal_component_set_attachment_list (comp, new_attach_list);

	for (l = new_attach_list; l != NULL; l = l->next)
		g_free (l->data);
	g_slist_free (new_attach_list);
}

#define RECIP_SENDABLE  0x1
#define RECIP_ORGANIZER 0x2

void
exchange_mapi_cal_util_fetch_recipients (ECalComponent *comp, GSList **recip_list)
{
	GSList *al = NULL, *l;
	ECalComponentOrganizer organizer;

	e_cal_component_get_attendee_list (comp, &al);
	e_cal_component_get_organizer (comp, &organizer);

	for (l = al; l != NULL; l = l->next) {
		ECalComponentAttendee *attendee = (ECalComponentAttendee *)(l->data);
		ExchangeMAPIRecipient *recipient = g_new0 (ExchangeMAPIRecipient, 1);
		uint32_t val = 0;
//		const char *str = NULL;
		if (attendee->value && !g_ascii_strncasecmp (attendee->value, "mailto:", 7)) 
			recipient->email_id = (attendee->value) + 7;
		else 
			recipient->email_id = (attendee->value);
		recipient->in.req_lpProps = g_new0 (struct SPropValue, 5);
		recipient->in.req_cValues = 5;
		val = 0;
		set_SPropValue_proptag (&(recipient->in.req_lpProps[0]), PR_SEND_INTERNET_ENCODING, (const void *)&val);
		val = RECIP_SENDABLE | (!g_ascii_strcasecmp(recipient->email_id, organizer.value) ? RECIP_ORGANIZER : 0);
		set_SPropValue_proptag (&(recipient->in.req_lpProps[1]), PR_RECIPIENTS_FLAGS, (const void *)&val);
		val = get_trackstatus_from_partstat (attendee->status);
		set_SPropValue_proptag (&(recipient->in.req_lpProps[2]), PR_RECIPIENT_TRACKSTATUS, (const void *)&val);
		val = get_type_from_role (attendee->role);
		set_SPropValue_proptag (&(recipient->in.req_lpProps[3]), PR_RECIPIENT_TYPE, (const void *) &val);
/*		if (attendee->cn && *(attendee->cn))
			str = attendee->cn;
		else 
			str = "";
		set_SPropValue_proptag (&(recipient->in.req_lpProps[4]), PR_RECIPIENT_DISPLAY_NAME, (const void *)(str));
*/
		*recip_list = g_slist_append (*recip_list, recipient);
	}

	e_cal_component_free_attendee_list (al);
}

static void
set_attachments_to_cal_component (ECalComponent *comp, GSList *attach_list, const char *local_store_uri)
{
	GSList *comp_attach_list = NULL, *l;
	const char *uid;

	e_cal_component_get_uid (comp, &uid);
	for (l = attach_list; l ; l = l->next) {
		ExchangeMAPIAttachment *attach_item = (ExchangeMAPIAttachment *) (l->data);
		gchar *attach_file_url, *filename;
		guint len;
		int fd = -1;
		gchar *attach_lf = exchange_crlf_to_lf((const char *)attach_item->value->data);

		len = (attach_lf != NULL) ? strlen (attach_lf) : 0;
		attach_file_url = g_strconcat (local_store_uri, G_DIR_SEPARATOR_S, uid, "-", attach_item->filename, NULL);
		filename = g_filename_from_uri (attach_file_url, NULL, NULL);

		fd = g_open (filename, O_RDWR|O_CREAT|O_TRUNC|O_BINARY, 0600);
		if (fd == -1) { 
			/* skip gracefully */
			g_message ("DEBUG: could not open %s for writing\n", filename);
		} else if (len && write (fd, attach_lf, len) == -1) {
			/* skip gracefully */
			g_message ("DEBUG: attachment write failed.\n");
		}
		if (fd != -1) {
			close (fd);
			comp_attach_list = g_slist_append (comp_attach_list, g_strdup (attach_file_url));
		}

		g_free (filename);
		g_free (attach_file_url);
		g_free (attach_lf);
	}

	e_cal_component_set_attachment_list (comp, comp_attach_list);
}

static void 
ical_attendees_from_props (icalcomponent *ical_comp, GSList *recipients, gboolean rsvp)
{
	GSList *l;
	for (l=recipients; l; l=l->next) {
		ExchangeMAPIRecipient *recip = (ExchangeMAPIRecipient *)(l->data);
		icalproperty *prop = NULL;
		icalparameter *param;
		gchar *val;
		const uint32_t *ui32;
		const char *str;
		const uint32_t *flags; 

		if (recip->email_id)
			val = g_strdup_printf ("MAILTO:%s", recip->email_id);
		else 
			continue;

		flags = (const uint32_t *) get_SPropValue(recip->out.all_lpProps, PR_RECIPIENTS_FLAGS);

		if (flags && (*flags & RECIP_ORGANIZER)) {
			prop = icalproperty_new_organizer (val);

			/* CN */
			str = (const char *) exchange_mapi_util_find_SPropVal_array_propval(recip->out.all_lpProps, PR_RECIPIENT_DISPLAY_NAME);
			if (!str)
				str = (const char *) exchange_mapi_util_find_SPropVal_array_propval(recip->out.all_lpProps, PR_DISPLAY_NAME);
			if (str) {
				param = icalparameter_new_cn (str);
				icalproperty_add_parameter (prop, param);
			}
		} else {
			prop = icalproperty_new_attendee (val);

			/* CN */
			str = (const char *) exchange_mapi_util_find_SPropVal_array_propval(recip->out.all_lpProps, PR_RECIPIENT_DISPLAY_NAME);
			if (!str)
				str = (const char *) exchange_mapi_util_find_SPropVal_array_propval(recip->out.all_lpProps, PR_DISPLAY_NAME);
			if (str) {
				param = icalparameter_new_cn (str);
				icalproperty_add_parameter (prop, param);
			}
			/* RSVP */
			param = icalparameter_new_rsvp (rsvp ? ICAL_RSVP_TRUE : ICAL_RSVP_FALSE);
			icalproperty_add_parameter (prop, param);
			/* PARTSTAT */
			ui32 = (const uint32_t *) get_SPropValue(recip->out.all_lpProps, PR_RECIPIENT_TRACKSTATUS);
			if (ui32) {
				param = icalparameter_new_partstat (get_partstat_from_trackstatus (*ui32));
				icalproperty_add_parameter (prop, param);
			}
			/* ROLE */
			ui32 = (const uint32_t *) get_SPropValue(recip->out.all_lpProps, PR_RECIPIENT_TYPE);
			if (ui32) {
				param = icalparameter_new_role (get_role_from_type (*ui32));
				icalproperty_add_parameter (prop, param);
			}
#if 0
			/* CALENDAR USER TYPE */
			param = icalparameter_new_cutype ();
			icalproperty_add_parameter (prop, param);
#endif
		}

		if (prop)
			icalcomponent_add_property (ical_comp, prop);

		g_free (val);
	}
}

static const uint8_t GID_START_SEQ[] = {
	0x04, 0x00, 0x00, 0x00, 0x82, 0x00, 0xe0, 0x00,
	0x74, 0xc5, 0xb7, 0x10, 0x1a, 0x82, 0xe0, 0x08
};

void
exchange_mapi_cal_util_generate_globalobjectid (gboolean is_clean, const char *uid, struct SBinary *sb)
{
	GByteArray *ba;
	guint32 flag32;
	guchar *buf = NULL;
	gsize len;
	d(guint32 i);

	ba = g_byte_array_new ();

	ba = g_byte_array_append (ba, GID_START_SEQ, (sizeof (GID_START_SEQ) / sizeof (GID_START_SEQ[0])));

	/* FIXME for exceptions */
	if (is_clean || TRUE) {
		flag32 = 0;
		ba = g_byte_array_append (ba, &flag32, sizeof (guint32));
	}

	/* creation time - may be all 0's  */
	flag32 = 0;
	ba = g_byte_array_append (ba, &flag32, sizeof (guint32));
	flag32 = 0;
	ba = g_byte_array_append (ba, &flag32, sizeof (guint32));

	/* RESERVED - should be all 0's  */
	flag32 = 0;
	ba = g_byte_array_append (ba, &flag32, sizeof (guint32));
	flag32 = 0;
	ba = g_byte_array_append (ba, &flag32, sizeof (guint32));

	/* FIXME: cleanup the UID first */

	/* We put Evolution's UID in base64 here */
	buf = g_base64_decode (uid, &len);
	if (len % 2 != 0)
		--len;
	flag32 = len;

	/* Size in bytes of the following data */
	ba = g_byte_array_append (ba, &flag32, sizeof (guint32));
	/* Data */
	ba = g_byte_array_append (ba, buf, flag32);
	g_free (buf);

	sb->lpb = ba->data;
	sb->cb = ba->len;

	d(g_message ("New GlobalObjectId.. Length: %d bytes.. Hex-data follows:", ba->len));
	d(for (i = 0; i < ba->len; i++) 
		g_print("0x%.2X ", ba->data[i]));

	g_byte_array_free (ba, FALSE);
}

static gchar *
id_to_string (GByteArray *ba)
{
	guint8 *ptr;
	guint len;
	gchar *buf = NULL;
	guint32 flag32, i, j;

	g_return_val_if_fail (ba != NULL, NULL);
	/* MSDN docs: the globalID must have an even number of bytes */
	if ((ba->len)%2 != 0)
		return NULL;

	ptr = ba->data;
	len = ba->len;

	/* starting seq - len = 16 bytes */
	for (i = 0, j = 0;(i < len) && (j < sizeof (GID_START_SEQ)); ++i, ++ptr, ++j)
		if (*ptr != GID_START_SEQ[j])
			return NULL;

	/* FIXME: for exceptions - len = 4 bytes */
	flag32 = *((guint32 *)ptr);
	i += sizeof (guint32);
	if (!(i < len) || flag32 != 0)
		return NULL;
	ptr += sizeof (guint32);

	/* Creation time - len = 8 bytes - skip it */
	flag32 = *((guint32 *)ptr);
	i += sizeof (guint32);
	if (!(i < len))
		return NULL;
	ptr += sizeof (guint32);

	flag32 = *((guint32 *)ptr);
	i += sizeof (guint32);
	if (!(i < len))
		return NULL;
	ptr += sizeof (guint32);

	/* Reserved bytes - len = 8 bytes */
	flag32 = *((guint32 *)ptr);
	i += sizeof (guint32);
	if (!(i < len) || flag32 != 0)
		return NULL;
	ptr += sizeof (guint32);

	flag32 = *((guint32 *)ptr);
	i += sizeof (guint32);
	if (!(i < len) || flag32 != 0)
		return NULL;
	ptr += sizeof (guint32);

	/* This is the real data */
	flag32 = *((guint32 *)ptr);
	i += sizeof (guint32);
	if (!(i < len) || flag32 != (len - i))
		return NULL;
	ptr += sizeof (guint32);

	buf = g_base64_encode (ptr, flag32);

	return buf;
}

ECalComponent *
exchange_mapi_cal_util_mapi_props_to_comp (icalcomponent_kind kind, const gchar *mid, struct mapi_SPropValue_array *properties, 
					   GSList *streams, GSList *recipients, GSList *attachments, 
					   const char *local_store_uri, const icaltimezone *default_zone)
{
	ECalComponent *comp = NULL;
	struct timeval t;
	const gchar *subject = NULL;
	const uint32_t *ui32;
	const bool *b;
	icalcomponent *ical_comp;
	icalproperty *prop = NULL;
	icalparameter *param = NULL;
	ExchangeMAPIStream *body;
	const icaltimezone *utc_zone;

	switch (kind) {
		case ICAL_VEVENT_COMPONENT:
		case ICAL_VTODO_COMPONENT:
		case ICAL_VJOURNAL_COMPONENT:
			comp = e_cal_component_new ();
			ical_comp = icalcomponent_new (kind);
			e_cal_component_set_icalcomponent (comp, ical_comp);
			icalcomponent_set_uid (ical_comp, mid);
			e_cal_component_set_uid (comp, mid);
			break;
		default:
			return NULL;
	}

	utc_zone = icaltimezone_get_utc_timezone ();

	subject = (const gchar *)exchange_mapi_util_find_array_propval(properties, PR_SUBJECT);
	if (!subject)
		subject = (const gchar *)exchange_mapi_util_find_array_propval(properties, PR_NORMALIZED_SUBJECT);
	if (!subject)
		subject = (const gchar *)exchange_mapi_util_find_array_propval(properties, PR_CONVERSATION_TOPIC);

	body = exchange_mapi_util_find_stream (streams, PR_BODY);
	if (!body)
		body = exchange_mapi_util_find_stream (streams, PR_BODY_HTML);
	if (!body)
		body = exchange_mapi_util_find_stream (streams, PR_HTML);

	/* set dtstamp - in UTC */
	if (get_mapi_SPropValue_array_date_timeval (&t, properties, PR_CREATION_TIME) == MAPI_E_SUCCESS)
		icalcomponent_set_dtstamp (ical_comp, icaltime_from_timet_with_zone (t.tv_sec, 0, utc_zone));

	/* created - in UTC */
	prop = icalproperty_new_created (icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ()));
	icalcomponent_add_property (ical_comp, prop);
	
	/* last modified - in UTC */
	if (get_mapi_SPropValue_array_date_timeval (&t, properties, PR_LAST_MODIFICATION_TIME) == MAPI_E_SUCCESS) {
		prop = icalproperty_new_lastmodified (icaltime_from_timet_with_zone (t.tv_sec, 0, utc_zone));
		icalcomponent_add_property (ical_comp, prop);
	}

	if (subject && *subject)
		icalcomponent_set_summary (ical_comp, subject);
	if (body)
		icalcomponent_set_description (ical_comp, (const char *) body->value->data);

	if (icalcomponent_isa (ical_comp) == ICAL_VEVENT_COMPONENT) {
		const char *location = NULL;
		const gchar *dtstart_tz = NULL, *dtend_tz = NULL;
		ExchangeMAPIStream *stream;

		/* CleanGlobalObjectId */
		stream = exchange_mapi_util_find_stream (streams, PROP_TAG(PT_BINARY, 0x0023));
		if (stream) {
			gchar *value = id_to_string (stream->value);
			prop = icalproperty_new_x (value);
			icalproperty_set_x_name (prop, "X-EVOLUTION-MAPI-CLEAN-GLOBALID");
			icalcomponent_add_property (ical_comp, prop);
			g_free (value);
		}

		/* GlobalObjectId */
		stream = exchange_mapi_util_find_stream (streams, PROP_TAG(PT_BINARY, 0x0003));
		if (stream) {
			gchar *value = id_to_string (stream->value);
			prop = icalproperty_new_x (value);
			icalproperty_set_x_name (prop, "X-EVOLUTION-MAPI-GLOBALID");
			icalcomponent_add_property (ical_comp, prop);
			g_free (value);
		}

		location = (const char *)exchange_mapi_util_find_array_propval(properties, PROP_TAG(PT_STRING8, 0x8208));
		if (location && *location)
			icalcomponent_set_location (ical_comp, location);

		b = (const bool *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_BOOLEAN, 0x8215));

		stream = exchange_mapi_util_find_stream (streams, PROP_TAG(PT_BINARY, 0x825E));
		if (stream) {
			gchar *buf = exchange_mapi_cal_util_bin_to_mapi_tz (stream->value);
			dtstart_tz = exchange_mapi_cal_tz_util_get_ical_equivalent (buf);
			g_free (buf);
		}

		if (get_mapi_SPropValue_array_date_timeval (&t, properties, PROP_TAG(PT_SYSTIME, 0x820D)) == MAPI_E_SUCCESS) {
			icaltimezone *zone = dtstart_tz ? icaltimezone_get_builtin_timezone_from_tzid (dtstart_tz) : default_zone;
			prop = icalproperty_new_dtstart (icaltime_from_timet_with_zone (t.tv_sec, (b && *b), zone));
			icalproperty_add_parameter (prop, icalparameter_new_tzid(dtstart_tz));
			icalcomponent_add_property (ical_comp, prop);
		}

		stream = exchange_mapi_util_find_stream (streams, PROP_TAG(PT_BINARY, 0x825F));
		if (stream) {
			gchar *buf = exchange_mapi_cal_util_bin_to_mapi_tz (stream->value);
			dtend_tz = exchange_mapi_cal_tz_util_get_ical_equivalent (buf);
			g_free (buf);
		}

		if (get_mapi_SPropValue_array_date_timeval (&t, properties, PROP_TAG(PT_SYSTIME, 0x820E)) == MAPI_E_SUCCESS) {
			icaltimezone *zone = dtend_tz ? icaltimezone_get_builtin_timezone_from_tzid (dtend_tz) : default_zone;
			prop = icalproperty_new_dtend (icaltime_from_timet_with_zone (t.tv_sec, (b && *b), zone));
			icalproperty_add_parameter (prop, icalparameter_new_tzid(dtend_tz));
			icalcomponent_add_property (ical_comp, prop);
		}

		ui32 = (const uint32_t *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_LONG, 0x8205));
		if (ui32) {
			prop = icalproperty_new_transp (get_transp_from_prop (*ui32));
			icalcomponent_add_property (ical_comp, prop);
		}

		b = (const bool *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_BOOLEAN, 0x8223));
		if (b && *b) {
			/* FIXME: recurrence */
			g_warning ("Encountered a recurring event.");
/*			stream = exchange_mapi_util_find_stream (streams, PROP_TAG(PT_BINARY, 0x8216));
			if (stream) {
				e_cal_backend_mapi_util_bin_to_rrule (stream->value, comp);
			}
*/		} 

		if (recipients) {
			b = (const bool *)find_mapi_SPropValue_data(properties, PR_RESPONSE_REQUESTED);
			ical_attendees_from_props (ical_comp, recipients, (b && *b));
			if (icalcomponent_get_first_property (ical_comp, ICAL_ORGANIZER_PROPERTY) == NULL) {
				gchar *val;
//				const char *sender_name = (const char *) exchange_mapi_util_find_array_propval (properties, PR_SENDER_NAME);
				const char *sender_email_type = (const char *) exchange_mapi_util_find_array_propval (properties, PR_SENDER_ADDRTYPE);
				const char *sender_email = (const char *) exchange_mapi_util_find_array_propval (properties, PR_SENDER_EMAIL_ADDRESS);
				const char *sent_name = (const char *) exchange_mapi_util_find_array_propval (properties, PR_SENT_REPRESENTING_NAME);
				const char *sent_email_type = (const char *) exchange_mapi_util_find_array_propval (properties, PR_SENT_REPRESENTING_ADDRTYPE);
				const char *sent_email = (const char *) exchange_mapi_util_find_array_propval (properties, PR_SENT_REPRESENTING_EMAIL_ADDRESS);

				if (!g_utf8_collate (sender_email_type, "EX"))
					sender_email = exchange_mapi_util_ex_to_smtp (sender_email);
				if (!g_utf8_collate (sent_email_type, "EX"))
					sent_email = exchange_mapi_util_ex_to_smtp (sent_email);

				val = g_strdup_printf ("MAILTO:%s", sent_email);
				prop = icalproperty_new_organizer (val);
				g_free (val);
				/* CN */
				param = icalparameter_new_cn (sent_name);
				icalproperty_add_parameter (prop, param);
				/* SENTBY */
				if (g_utf8_collate (sent_email, sender_email)) {
					val = g_strdup_printf ("MAILTO:%s", sender_email);
					param = icalparameter_new_sentby (val);
					icalproperty_add_parameter (prop, param);
					g_free (val);
				}

				icalcomponent_add_property (ical_comp, prop);
			}
		}

		b = (const bool *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_BOOLEAN, 0x8503));
		if (b && *b) {
			struct timeval start, displaytime;

			if ((get_mapi_SPropValue_array_date_timeval (&start, properties, PROP_TAG(PT_SYSTIME, 0x8502)) == MAPI_E_SUCCESS) 
			 && (get_mapi_SPropValue_array_date_timeval (&displaytime, properties, PROP_TAG(PT_SYSTIME, 0x8560)) == MAPI_E_SUCCESS)) {
				ECalComponentAlarm *e_alarm = e_cal_component_alarm_new ();
				ECalComponentAlarmTrigger trigger;

				trigger.type = E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START;
				trigger.u.rel_duration = icaltime_subtract (icaltime_from_timet_with_zone (displaytime.tv_sec, 0, 0), 
									    icaltime_from_timet_with_zone (start.tv_sec, 0, 0));

				e_cal_component_alarm_set_action (e_alarm, E_CAL_COMPONENT_ALARM_DISPLAY);
				e_cal_component_alarm_set_trigger (e_alarm, trigger);

				e_cal_component_add_alarm (comp, e_alarm);
			}
		} else
			e_cal_component_remove_all_alarms (comp);

	} else if (icalcomponent_isa (ical_comp) == ICAL_VTODO_COMPONENT) {
		const double *complete = 0;

		/* NOTE: Exchange tasks are DATE values, not DATE-TIME values, but maybe someday, we could expect Exchange to support it ;) */
		if (get_mapi_SPropValue_array_date_timeval (&t, properties, PROP_TAG(PT_SYSTIME, 0x8104)) == MAPI_E_SUCCESS)
			icalcomponent_set_dtstart (ical_comp, icaltime_from_timet_with_zone (t.tv_sec, 1, utc_zone));
		if (get_mapi_SPropValue_array_date_timeval (&t, properties, PROP_TAG(PT_SYSTIME, 0x8105)) == MAPI_E_SUCCESS)
			icalcomponent_set_due (ical_comp, icaltime_from_timet_with_zone (t.tv_sec, 1, utc_zone));

		ui32 = (const uint32_t *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_LONG, 0x8101));
		if (ui32) {
			icalcomponent_set_status (ical_comp, get_taskstatus_from_prop(*ui32));
			if (*ui32 == olTaskComplete 
			&& get_mapi_SPropValue_array_date_timeval (&t, properties, PROP_TAG(PT_SYSTIME, 0x810F)) == MAPI_E_SUCCESS) {
				prop = icalproperty_new_completed (icaltime_from_timet_with_zone (t.tv_sec, 1, utc_zone));
				icalcomponent_add_property (ical_comp, prop);
			}
		}

		complete = (const double *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_DOUBLE, 0x8102));
		if (complete) {
			prop = icalproperty_new_percentcomplete ((int)(*complete * 100));
			icalcomponent_add_property (ical_comp, prop);
		}

		b = (const bool *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_BOOLEAN, 0x8126));
		if (b && *b) {
			/* FIXME: Evolution does not support recurring tasks */
			g_warning ("Encountered a recurring task.");
		}

		b = (const bool *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_BOOLEAN, 0x8503));
		if (b && *b) {
			struct timeval abs;

			if (get_mapi_SPropValue_array_date_timeval (&abs, properties, PROP_TAG(PT_SYSTIME, 0x8502)) == MAPI_E_SUCCESS) {
				ECalComponentAlarm *e_alarm = e_cal_component_alarm_new ();
				ECalComponentAlarmTrigger trigger;

				trigger.type = E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE;
				trigger.u.abs_time = icaltime_from_timet_with_zone (abs.tv_sec, 0, 0);

				e_cal_component_alarm_set_action (e_alarm, E_CAL_COMPONENT_ALARM_DISPLAY);
				e_cal_component_alarm_set_trigger (e_alarm, trigger);

				e_cal_component_add_alarm (comp, e_alarm);
			}
		} else
			e_cal_component_remove_all_alarms (comp);

	} else if (icalcomponent_isa (ical_comp) == ICAL_VJOURNAL_COMPONENT) {
		if (get_mapi_SPropValue_array_date_timeval (&t, properties, PR_LAST_MODIFICATION_TIME) == MAPI_E_SUCCESS)
			icalcomponent_set_dtstart (ical_comp, icaltime_from_timet_with_zone (t.tv_sec, 1, default_zone));
	}

	if (icalcomponent_isa (ical_comp) == ICAL_VEVENT_COMPONENT || icalcomponent_isa (ical_comp) == ICAL_VTODO_COMPONENT) {
		/* priority */
		ui32 = (const uint32_t *)find_mapi_SPropValue_data(properties, PR_PRIORITY);
		if (ui32) {
			prop = icalproperty_new_priority (get_priority_from_prop (*ui32));
			icalcomponent_add_property (ical_comp, prop);
		}
	}

	/* classification */
	ui32 = (const uint32_t *)find_mapi_SPropValue_data(properties, PR_SENSITIVITY);
	if (ui32) {
		prop = icalproperty_new_class (get_class_from_prop (*ui32));
		icalcomponent_add_property (ical_comp, prop);
	}

	/* FIXME: categories */

	set_attachments_to_cal_component (comp, attachments, local_store_uri);

	e_cal_component_rescan (comp);

	return comp;
}

char *
exchange_mapi_cal_util_camel_helper (struct mapi_SPropValue_array *properties, 
				   GSList *streams, GSList *recipients, GSList *attachments)
{
	ECalComponent *comp;
	struct cbdata cbdata;
	GSList *myrecipients = NULL;
	GSList *myattachments = NULL;
	mapi_id_t mid = 0;
	char *str = NULL;
	char *tmp;
	icalcomponent *icalcomp = NULL;

	comp = exchange_mapi_cal_util_mapi_props_to_comp (ICAL_VEVENT_COMPONENT, e_cal_component_gen_uid(), 
						properties, streams, recipients, NULL, NULL, 
						NULL);

	cbdata.comp = comp;
	cbdata.username = (const char *) exchange_mapi_util_find_array_propval (properties, PR_SENDER_NAME);
	cbdata.useridtype = (const char *) exchange_mapi_util_find_array_propval (properties, PR_SENDER_ADDRTYPE);
	cbdata.userid = (const char *) exchange_mapi_util_find_array_propval (properties, PR_SENDER_EMAIL_ADDRESS);
	cbdata.ownername = (const char *) exchange_mapi_util_find_array_propval (properties, PR_SENT_REPRESENTING_NAME);
	cbdata.owneridtype = (const char *) exchange_mapi_util_find_array_propval (properties, PR_SENT_REPRESENTING_ADDRTYPE);
	cbdata.ownerid = (const char *) exchange_mapi_util_find_array_propval (properties, PR_SENT_REPRESENTING_EMAIL_ADDRESS);
	cbdata.is_modify = FALSE;
	cbdata.msgflags = MSGFLAG_READ;
	cbdata.meeting_type = MEETING_OBJECT_RCVD;
	cbdata.appt_seq = (*(const uint32_t *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_LONG, 0x8201)));
	cbdata.appt_id = (*(const uint32_t *)find_mapi_SPropValue_data(properties, PR_OWNER_APPT_ID));
	cbdata.globalid = (const struct SBinary *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_BINARY, 0x0003));
	cbdata.cleanglobalid = (const struct SBinary *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_BINARY, 0x0023));

	exchange_mapi_cal_util_fetch_recipients (comp, &myrecipients);
	myattachments = attachments;
	mid = exchange_mapi_create_item (olFolderCalendar, 0, 
					exchange_mapi_cal_util_build_name_id, GINT_TO_POINTER(ICAL_VEVENT_COMPONENT),
					exchange_mapi_cal_util_build_props, &cbdata, 
					myrecipients, myattachments, NULL, MAPI_OPTIONS_DONT_SUBMIT);
	g_free (cbdata.props);
	exchange_mapi_util_free_recipient_list (&myrecipients);

	tmp = exchange_mapi_util_mapi_id_to_string (mid);
	e_cal_component_set_uid (comp, tmp);
	g_free (tmp);

	icalcomp = e_cal_util_new_top_level ();
	icalcomponent_set_method (icalcomp, ICAL_METHOD_REQUEST);
	icalcomponent_add_component (icalcomp, e_cal_component_get_icalcomponent(comp));
	str = icalcomponent_as_ical_string (icalcomp);
	g_object_unref (comp);

	return str;
}


#define COMMON_NAMED_PROPS_N 8

typedef enum 
{
	I_COMMON_REMMINS = 0 , 
	I_COMMON_REMTIME , 
	I_COMMON_REMSET , 
	I_COMMON_ISPRIVATE , 
	I_COMMON_SIDEEFFECTS , 
	I_COMMON_START , 
	I_COMMON_END , 
	I_COMMON_REMNEXTTIME 
} CommonNamedPropsIndex;

gboolean
exchange_mapi_cal_util_build_name_id (struct mapi_nameid *nameid, gpointer data)
{
	icalcomponent_kind kind = GPOINTER_TO_INT (data);

	/* NOTE: Avoid using mapi_nameid_OOM_add because: 
	 * a) its inefficient (uses strcmp) 
	 * b) names may vary in different server/libmapi versions 
	 */

	mapi_nameid_lid_add(nameid, 0x8501, PSETID_Common); 	// PT_LONG - ReminderMinutesBeforeStart
	mapi_nameid_lid_add(nameid, 0x8502, PSETID_Common); 	// PT_SYSTIME - ReminderTime
	mapi_nameid_lid_add(nameid, 0x8503, PSETID_Common); 	// PT_BOOLEAN - ReminderSet
	mapi_nameid_lid_add(nameid, 0x8506, PSETID_Common); 	// PT_BOOLEAN - Private
	mapi_nameid_lid_add(nameid, 0x8510, PSETID_Common); 	// PT_LONG - (context menu flags)
	mapi_nameid_lid_add(nameid, 0x8516, PSETID_Common); 	// PT_SYSTIME - CommonStart
	mapi_nameid_lid_add(nameid, 0x8517, PSETID_Common); 	// PT_SYSTIME - CommonEnd
	mapi_nameid_lid_add(nameid, 0x8560, PSETID_Common); 	// PT_SYSTIME - ReminderNextTime

	if (kind == ICAL_VEVENT_COMPONENT) 
		appt_build_name_id (nameid);
	else if (kind == ICAL_VTODO_COMPONENT)
		task_build_name_id (nameid);
	else if (kind == ICAL_VJOURNAL_COMPONENT)
		note_build_name_id (nameid);

	return TRUE;
}

/**
 * NOTE: The enumerations '(Appt/Task/Note)NamedPropsIndex' have been defined 
 * only to make life a little easier for developers. Here's the logic 
 * behind the definition:
     1) The first element is initialized with 'COMMON_NAMED_PROPS_N' : When 
	adding named props, we add the common named props first and then the 
	specific named props. So.. the index of the first specific 
	named property = COMMON_NAMED_PROPS_N
     2) The order in the enumeration 'must' be the same as that in the routine 
	which adds the specific named props - (appt/task/note)_build_name_id
     3) If a specific named prop is added/deleted, an index needs to
	be created/deleted at the correct position. [Don't forget to update 
	(APPT/TASK/NOTE)_NAMED_PROPS_N]. 

 * To summarize the pros: 
     1) Addition/deletion of a common-named-prop would not affect the indexes 
	of the specific named props once COMMON_NAMED_PROPS_N is updated. 
     2) Values of named props can be added in any order. 
 */


#define APPT_NAMED_PROPS_N  30
#define DEFAULT_APPT_REMINDER_MINS 15

typedef enum 
{
	I_APPT_SEQ = COMMON_NAMED_PROPS_N , 
	I_APPT_BUSYSTATUS , 
	I_APPT_LOCATION , 
	I_APPT_START , 
	I_APPT_END , 
	I_APPT_DURATION , 
	I_APPT_ALLDAY , 
/**/	I_APPT_RECURBLOB , 
	I_APPT_MEETINGSTATUS , 
	I_APPT_RESPONSESTATUS , 
	I_APPT_RECURRING , 
	I_APPT_INTENDEDBUSY , 
/**/	I_APPT_RECURBASE , 
	I_APPT_INVITED , 
/**/	I_APPT_RECURTYPE , 
/**/	I_APPT_RECURPATTERN , 
	I_APPT_CLIPSTART , 
	I_APPT_CLIPEND , 
	I_APPT_AUTOLOCATION , 
	I_APPT_ISONLINEMEET , 
	I_APPT_COUNTERPROPOSAL , 
	I_APPT_STARTTZBLOB , 
	I_APPT_ENDTZBLOB ,

	I_MEET_WHERE , 
	I_MEET_GUID , 
	I_MEET_ISRECURRING , 
	I_MEET_ISEXCEPTION , 
	I_MEET_CLEANGUID , 
	I_MEET_APPTMSGCLASS , 
	I_MEET_TYPE

//	I_SENDASICAL , 
//	I_APPT_SEQTIME , 
//	I_APPT_LABEL , 
//	I_APPT_DISPTZ 
//	I_APPT_ALLATTENDEES , 
//	I_APPT_TOATTENDEES , 
//	I_APPT_CCATTENDEES , 
} ApptNamedPropsIndex;

static void 
appt_build_name_id (struct mapi_nameid *nameid)
{
	mapi_nameid_lid_add(nameid, 0x8201, PSETID_Appointment); 	// PT_LONG - ApptSequence
	mapi_nameid_lid_add(nameid, 0x8205, PSETID_Appointment); 	// PT_LONG - BusyStatus
	mapi_nameid_lid_add(nameid, 0x8208, PSETID_Appointment); 	// PT_STRING8 - Location
	mapi_nameid_lid_add(nameid, 0x820D, PSETID_Appointment); 	// PT_SYSTIME - Start/ApptStartWhole
	mapi_nameid_lid_add(nameid, 0x820E, PSETID_Appointment); 	// PT_SYSTIME - End/ApptEndWhole
	mapi_nameid_lid_add(nameid, 0x8213, PSETID_Appointment); 	// PT_LONG - Duration/ApptDuration
	mapi_nameid_lid_add(nameid, 0x8215, PSETID_Appointment); 	// PT_BOOLEAN - AllDayEvent (also called ApptSubType)
	mapi_nameid_lid_add(nameid, 0x8216, PSETID_Appointment); 	// PT_BINARY - (recurrence blob)
	mapi_nameid_lid_add(nameid, 0x8217, PSETID_Appointment); 	// PT_LONG - MeetingStatus
	mapi_nameid_lid_add(nameid, 0x8218, PSETID_Appointment); 	// PT_LONG - ResponseStatus
	mapi_nameid_lid_add(nameid, 0x8223, PSETID_Appointment); 	// PT_BOOLEAN - Recurring
	mapi_nameid_lid_add(nameid, 0x8224, PSETID_Appointment); 	// PT_LONG - IntendedBusyStatus
	mapi_nameid_lid_add(nameid, 0x8228, PSETID_Appointment); 	// PT_SYSTIME - RecurrenceBase
	mapi_nameid_lid_add(nameid, 0x8229, PSETID_Appointment); 	// PT_BOOLEAN - FInvited
	mapi_nameid_lid_add(nameid, 0x8231, PSETID_Appointment); 	// PT_LONG - RecurrenceType
	mapi_nameid_lid_add(nameid, 0x8232, PSETID_Appointment); 	// PT_STRING8 - RecurrencePattern
	mapi_nameid_lid_add(nameid, 0x8235, PSETID_Appointment); 	// PT_SYSTIME - (dtstart)(for recurring events UTC 12 AM of day of start)
	mapi_nameid_lid_add(nameid, 0x8236, PSETID_Appointment); 	// PT_SYSTIME - (dtend)(for recurring events UTC 12 AM of day of end)
	mapi_nameid_lid_add(nameid, 0x823A, PSETID_Appointment); 	// PT_BOOLEAN - AutoFillLocation
	mapi_nameid_lid_add(nameid, 0x8240, PSETID_Appointment); 	// PT_BOOLEAN - IsOnlineMeeting
	mapi_nameid_lid_add(nameid, 0x8257, PSETID_Appointment); 	// PT_BOOLEAN - ApptCounterProposal
	mapi_nameid_lid_add(nameid, 0x825E, PSETID_Appointment); 	// PT_BINARY - (timezone for dtstart)
	mapi_nameid_lid_add(nameid, 0x825F, PSETID_Appointment); 	// PT_BINARY - (timezone for dtend)

	mapi_nameid_lid_add(nameid, 0x0002, PSETID_Meeting); 		// PT_STRING8 - Where
	mapi_nameid_lid_add(nameid, 0x0003, PSETID_Meeting); 		// PT_BINARY - GlobalObjectId
	mapi_nameid_lid_add(nameid, 0x0005, PSETID_Meeting); 		// PT_BOOLEAN - IsRecurring
	mapi_nameid_lid_add(nameid, 0x000a, PSETID_Meeting); 		// PT_BOOLEAN - IsException 
	mapi_nameid_lid_add(nameid, 0x0023, PSETID_Meeting); 		// PT_BINARY - CleanGlobalObjectId
	mapi_nameid_lid_add(nameid, 0x0024, PSETID_Meeting); 		// PT_STRING8 - AppointmentMessageClass 
	mapi_nameid_lid_add(nameid, 0x0026, PSETID_Meeting); 		// PT_LONG - MeetingType

	/* These probably would never be used from Evolution */
//	mapi_nameid_lid_add(nameid, 0x8200, PSETID_Appointment); 	// PT_BOOLEAN - SendAsICAL
//	mapi_nameid_lid_add(nameid, 0x8202, PSETID_Appointment); 	// PT_SYSTIME - ApptSequenceTime
//	mapi_nameid_lid_add(nameid, 0x8214, PSETID_Appointment); 	// PT_LONG - Label
//	mapi_nameid_lid_add(nameid, 0x8234, PSETID_Appointment); 	// PT_STRING8 - display TimeZone
//	mapi_nameid_lid_add(nameid, 0x8238, PSETID_Appointment); 	// PT_STRING8 - AllAttendees
//	mapi_nameid_lid_add(nameid, 0x823B, PSETID_Appointment); 	// PT_STRING8 - ToAttendeesString (dupe PR_DISPLAY_TO)
//	mapi_nameid_lid_add(nameid, 0x823C, PSETID_Appointment); 	// PT_STRING8 - CCAttendeesString (dupe PR_DISPLAY_CC)
}

#define TASK_NAMED_PROPS_N 13
#define DEFAULT_TASK_REMINDER_MINS 1080

typedef enum 
{
	I_TASK_STATUS = COMMON_NAMED_PROPS_N , 
	I_TASK_PERCENT , 
	I_TASK_ISTEAMTASK , 
	I_TASK_START , 
	I_TASK_DUE , 
	I_TASK_COMPLETED , 
//	I_TASK_RECURBLOB , 
	I_TASK_ISCOMPLETE , 
	I_TASK_OWNER , 
	I_TASK_DELEGATOR , 
	I_TASK_ISRECURRING , 
	I_TASK_ROLE , 
	I_TASK_OWNERSHIP , 
	I_TASK_DELEGATIONSTATE , 
//	I_TASK_ACTUALWORK , 
//	I_TASK_TOTALWORK 
} TaskNamedPropsIndex;

static void 
task_build_name_id (struct mapi_nameid *nameid)
{
	mapi_nameid_lid_add(nameid, 0x8101, PSETID_Task); 	// PT_LONG - Status
	mapi_nameid_lid_add(nameid, 0x8102, PSETID_Task); 	// PT_DOUBLE - PercentComplete
	mapi_nameid_lid_add(nameid, 0x8103, PSETID_Task); 	// PT_BOOLEAN - TeamTask
	mapi_nameid_lid_add(nameid, 0x8104, PSETID_Task); 	// PT_SYSTIME - StartDate/TaskStartDate
	mapi_nameid_lid_add(nameid, 0x8105, PSETID_Task); 	// PT_SYSTIME - DueDate/TaskDueDate
	mapi_nameid_lid_add(nameid, 0x810F, PSETID_Task); 	// PT_SYSTIME - DateCompleted
//	mapi_nameid_lid_add(nameid, 0x8116, PSETID_Task); 	// PT_BINARY - (recurrence blob)
	mapi_nameid_lid_add(nameid, 0x811C, PSETID_Task); 	// PT_BOOLEAN - Complete
	mapi_nameid_lid_add(nameid, 0x811F, PSETID_Task); 	// PT_STRING8 - Owner
	mapi_nameid_lid_add(nameid, 0x8121, PSETID_Task); 	// PT_STRING8 - Delegator
	mapi_nameid_lid_add(nameid, 0x8126, PSETID_Task); 	// PT_BOOLEAN - IsRecurring/TaskFRecur
	mapi_nameid_lid_add(nameid, 0x8127, PSETID_Task); 	// PT_STRING8 - Role
	mapi_nameid_lid_add(nameid, 0x8129, PSETID_Task); 	// PT_LONG - Ownership
	mapi_nameid_lid_add(nameid, 0x812A, PSETID_Task); 	// PT_LONG - DelegationState

	/* These probably would never be used from Evolution */
//	mapi_nameid_lid_add(nameid, 0x8110, PSETID_Task); 	// PT_LONG - ActualWork/TaskActualEffort
//	mapi_nameid_lid_add(nameid, 0x8111, PSETID_Task); 	// PT_LONG - TotalWork/TaskEstimatedEffort
}


#define NOTE_NAMED_PROPS_N 0

/*
typedef enum 
{
//	I_NOTE_COLOR 
} NoteNamedPropsIndex;
*/

static void 
note_build_name_id (struct mapi_nameid *nameid)
{
	/* These probably would never be used from Evolution */
//	mapi_nameid_lid_add(nameid, 0x8B00, PSETID_Note); 	// PT_LONG - Color
}

#define MINUTES_IN_HOUR 60
#define SECS_IN_MINUTE 60

/** 
 * NOTE: When a new regular property (PR_***) is added, 'REGULAR_PROPS_N' 
 * should be updated. 
 */
#define REGULAR_PROPS_N    21

int
exchange_mapi_cal_util_build_props (struct SPropValue **value, struct SPropTagArray *proptag_array, gpointer data)
{
	struct cbdata *cbdata = (struct cbdata *) data;
	ECalComponent *comp = cbdata->comp;
	icalcomponent *ical_comp = e_cal_component_get_icalcomponent (comp);
	icalcomponent_kind  kind = icalcomponent_isa (ical_comp);
	gboolean has_attendees = e_cal_component_has_attendees (comp);
	struct SPropValue *props = NULL;
	int i=0;
	uint32_t flag32;
	bool b;
	icalproperty *prop;
	struct icaltimetype dtstart, dtend, utc_dtstart, utc_dtend;
	const icaltimezone *utc_zone;
	const char *dtstart_tzid, *dtend_tzid, *text = NULL;
	const char *uid = NULL;
	struct timeval t;

	switch (kind) {
		case ICAL_VEVENT_COMPONENT:
			props = g_new0 (struct SPropValue, REGULAR_PROPS_N + COMMON_NAMED_PROPS_N + APPT_NAMED_PROPS_N);
			g_print ("\nAllocating space for %d props\n", REGULAR_PROPS_N + COMMON_NAMED_PROPS_N + APPT_NAMED_PROPS_N);
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_MEET_APPTMSGCLASS], (const void *) "IPM.Appointment");
			if (has_attendees) {
				if (cbdata->meeting_type & MEETING_OBJECT)
					set_SPropValue_proptag(&props[i++], PR_MESSAGE_CLASS, (const void *) "IPM.Appointment");
				else if (cbdata->meeting_type & MEETING_REQUEST)
					set_SPropValue_proptag(&props[i++], PR_MESSAGE_CLASS, (const void *) "IPM.Schedule.Meeting.Request");
				else 
					set_SPropValue_proptag(&props[i++], PR_MESSAGE_CLASS, (const void *) "IPM.Appointment");
			} else
				set_SPropValue_proptag(&props[i++], PR_MESSAGE_CLASS, (const void *) "IPM.Appointment");
			break;
		case ICAL_VTODO_COMPONENT:
			props = g_new0 (struct SPropValue, REGULAR_PROPS_N + COMMON_NAMED_PROPS_N + TASK_NAMED_PROPS_N);
			g_print ("\nAllocating space for %d props\n", REGULAR_PROPS_N + COMMON_NAMED_PROPS_N + TASK_NAMED_PROPS_N);
			set_SPropValue_proptag(&props[i++], PR_MESSAGE_CLASS, (const void *) "IPM.Task");
			break;
		case ICAL_VJOURNAL_COMPONENT:
			props = g_new0 (struct SPropValue, REGULAR_PROPS_N + COMMON_NAMED_PROPS_N + NOTE_NAMED_PROPS_N);
			g_print ("\nAllocating space for %d props\n", REGULAR_PROPS_N + COMMON_NAMED_PROPS_N + NOTE_NAMED_PROPS_N);
			set_SPropValue_proptag(&props[i++], PR_MESSAGE_CLASS, (const void *) "IPM.StickyNote");
			break;
		default:
			return 0;
	} 											/* prop count: 1 */

	utc_zone = icaltimezone_get_utc_timezone ();

	dtstart = icalcomponent_get_dtstart (ical_comp);

	/* For VEVENTs */
	if (icalcomponent_get_first_property (ical_comp, ICAL_DTEND_PROPERTY) != 0)
		dtend = icalcomponent_get_dtend (ical_comp);
	/* For VTODOs */
	else if (icalcomponent_get_first_property (ical_comp, ICAL_DUE_PROPERTY) != 0)
		dtend = icalcomponent_get_due (ical_comp);
	else 
		dtend = icalcomponent_get_dtstart (ical_comp);

	dtstart_tzid = icaltime_get_tzid (dtstart);
	dtend_tzid = icaltime_get_tzid (dtend);

	utc_dtstart = icaltime_convert_to_zone (dtstart, utc_zone);
	utc_dtend = icaltime_convert_to_zone (dtend, utc_zone);

	/* FIXME: convert to unicode */
	text = icalcomponent_get_summary (ical_comp);
	if (!(text && *text)) 
		text = "";
	set_SPropValue_proptag(&props[i++], PR_SUBJECT, 					/* prop count: 2 */ 
					(const void *) text);
	set_SPropValue_proptag(&props[i++], PR_NORMALIZED_SUBJECT, 				/* prop count: 3 */ 
					(const void *) text);
	set_SPropValue_proptag(&props[i++], PR_CONVERSATION_TOPIC, 				/* prop count: 4 */
					(const void *) text);
	text = NULL;

	/* we don't support HTML event/task/memo editor */
	flag32 = olEditorText;
	set_SPropValue_proptag(&props[i++], PR_MSG_EDITOR_FORMAT, &flag32); 			/* prop count: 5 */

	/* it'd be better to convert, then set it in unicode */
	text = icalcomponent_get_description (ical_comp);
	if (!(text && *text)) 
		text = "";
	set_SPropValue_proptag(&props[i++], PR_BODY, 						/* prop count: 6 */
					(const void *) text);
	text = NULL;

	/* Priority */
	flag32 = PRIORITY_NORMAL; 	/* default */
	prop = icalcomponent_get_first_property (ical_comp, ICAL_PRIORITY_PROPERTY);
	if (prop) 
		flag32 = get_prop_from_priority (icalproperty_get_priority (prop));
	set_SPropValue_proptag(&props[i++], PR_PRIORITY, (const void *) &flag32); 		/* prop count: 7 */

	set_SPropValue_proptag(&props[i++], PR_SENT_REPRESENTING_NAME, 
		(const void *) cbdata->ownername);
	set_SPropValue_proptag(&props[i++], PR_SENT_REPRESENTING_ADDRTYPE, 
		(const void *) cbdata->owneridtype);
	set_SPropValue_proptag(&props[i++], PR_SENT_REPRESENTING_EMAIL_ADDRESS, 
		(const void *) cbdata->ownerid);
	set_SPropValue_proptag(&props[i++], PR_SENDER_NAME, 
		(const void *) cbdata->username);
	set_SPropValue_proptag(&props[i++], PR_SENDER_ADDRTYPE, 
		(const void *) cbdata->useridtype);
	set_SPropValue_proptag(&props[i++], PR_SENDER_EMAIL_ADDRESS, 
		(const void *) cbdata->userid); 						/* prop count: 13 */

	flag32 = cbdata->msgflags;
	set_SPropValue_proptag(&props[i++], PR_MESSAGE_FLAGS, (const void *) &flag32); 		/* prop count: 14 */

	flag32 = 0x0;
	b = e_cal_component_has_alarms (comp);
	if (b) {
		/* We know there would be only a single alarm of type:DISPLAY [static properties of the backend] */
		GList *alarm_uids = e_cal_component_get_alarm_uids (comp);
		ECalComponentAlarm *alarm = e_cal_component_get_alarm (comp, (const char *)(alarm_uids->data));
		ECalComponentAlarmAction action;
		e_cal_component_alarm_get_action (alarm, &action);
		if (action == E_CAL_COMPONENT_ALARM_DISPLAY) {
			ECalComponentAlarmTrigger trigger;
			e_cal_component_alarm_get_trigger (alarm, &trigger);
			switch (trigger.type) {
			case E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START :
				flag32 = (icaldurationtype_as_int (trigger.u.rel_duration)) / SECS_IN_MINUTE;
			/* we cannot set an alarm to popup after the start of an appointment on Exchange */
				flag32 = (flag32 < 0) ? -(flag32) : 0;
				break;
			default :
				break;
			}
		}
		e_cal_component_alarm_free (alarm);
		cal_obj_uid_list_free (alarm_uids);
	} 
	if (!flag32)
		switch (kind) {
			case ICAL_VEVENT_COMPONENT:
				flag32 = DEFAULT_APPT_REMINDER_MINS;
				break;
			case ICAL_VTODO_COMPONENT:
				flag32 = DEFAULT_TASK_REMINDER_MINS;
				break;
			default:
				break;
		}
	set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_COMMON_REMSET], (const void *) &b);
	set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_COMMON_REMMINS], (const void *) &flag32);
	t.tv_sec = icaltime_as_timet (utc_dtstart);
	t.tv_usec = 0;
	set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[I_COMMON_REMTIME], &t);
	t.tv_sec = icaltime_as_timet (utc_dtstart) - (flag32 * SECS_IN_MINUTE);
	t.tv_usec = 0;
	/* ReminderNextTime: FIXME for recurrence */
	set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[I_COMMON_REMNEXTTIME], &t);
												/* prop count: 14 (no regular props added) */
	/* Sensitivity, Private */
	flag32 = olNormal; 	/* default */
	b = 0; 			/* default */
	prop = icalcomponent_get_first_property (ical_comp, ICAL_CLASS_PROPERTY);
	if (prop) 
		flag32 = get_prop_from_class (icalproperty_get_class (prop));
	if (flag32 == olPrivate || flag32 == olConfidential)
		b = 1;
	set_SPropValue_proptag(&props[i++], PR_SENSITIVITY, (const void *) &flag32); 		/* prop count: 15 */
	set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_COMMON_ISPRIVATE], (const void *) &b);

	t.tv_sec = icaltime_as_timet (utc_dtstart);
	t.tv_usec = 0;
	set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[I_COMMON_START], &t);
	set_SPropValue_proptag_date_timeval(&props[i++], PR_START_DATE, &t); 			/* prop count: 16 */

	t.tv_sec = icaltime_as_timet (utc_dtend);
	t.tv_usec = 0;
	set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[I_COMMON_END], &t);
	set_SPropValue_proptag_date_timeval(&props[i++], PR_END_DATE, &t); 			/* prop count: 17 */

	b = 1;
	set_SPropValue_proptag(&props[i++], PR_RESPONSE_REQUESTED, (const void *) &b); 		/* prop count: 18 */

	/* PR_OWNER_APPT_ID needs to be set in certain cases only */				/* prop count: 19 */
	/* PR_ICON_INDEX needs to be set appropriately */					/* prop count: 20 */

	b = 0;
	set_SPropValue_proptag(&props[i++], PR_RTF_IN_SYNC, (const void *) &b); 		/* prop count: 21 */

	if (kind == ICAL_VEVENT_COMPONENT) {
		const char *mapi_tzid;
		struct SBinary start_tz, end_tz; 

		/* Busy Status */
		flag32 = olBusy; 	/* default */
		prop = icalcomponent_get_first_property (ical_comp, ICAL_TRANSP_PROPERTY);
		if (prop)
			flag32 = get_prop_from_transp (icalproperty_get_transp (prop));
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_INTENDEDBUSY], (const void *) &flag32);
		if (cbdata->meeting_type == MEETING_REQUEST || cbdata->meeting_type == MEETING_OBJECT_RCVD) {
			flag32 = olTentative;
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_BUSYSTATUS], (const void *) &flag32);
		} else 
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_BUSYSTATUS], (const void *) &flag32);

		/* Location */
		text = icalcomponent_get_location (ical_comp);
		if (!(text && *text)) 
			text = "";
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_LOCATION], (const void *) text);
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_MEET_WHERE], (const void *) text);
		text = NULL;
		/* Auto-Location is always FALSE - Evolution doesn't work that way */
		b = 0; 
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_AUTOLOCATION], (const void *) &b);

		/* Start */
		t.tv_sec = icaltime_as_timet (utc_dtstart);
		t.tv_usec = 0;
		set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[I_APPT_START], &t);
		/* FIXME: for recurrence */
		set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[I_APPT_CLIPSTART], &t);

		/* Start TZ */
		mapi_tzid = exchange_mapi_cal_tz_util_get_mapi_equivalent ((dtstart_tzid && *dtstart_tzid) ? dtstart_tzid : "UTC");
		if (mapi_tzid && *mapi_tzid) {
			exchange_mapi_cal_util_mapi_tz_to_bin (mapi_tzid, &start_tz);
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_STARTTZBLOB], (const void *) &start_tz);
		}

		/* End */
		t.tv_sec = icaltime_as_timet (utc_dtend);
		t.tv_usec = 0;
		set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[I_APPT_END], &t);
		/* FIXME: for recurrence */
		set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[I_APPT_CLIPEND], &t);

		/* End TZ */
		mapi_tzid = exchange_mapi_cal_tz_util_get_mapi_equivalent ((dtend_tzid && *dtend_tzid) ? dtend_tzid : "UTC");
		if (mapi_tzid && *mapi_tzid) {
			exchange_mapi_cal_util_mapi_tz_to_bin (mapi_tzid, &end_tz);
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_ENDTZBLOB], (const void *) &end_tz);
		}

		/* Duration */
		flag32 = icaldurationtype_as_int (icaltime_subtract (dtend, dtstart));
		flag32 /= MINUTES_IN_HOUR;
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_DURATION], (const void *) &flag32);

		/* All-day event */
		b = (icaltime_is_date (dtstart) && icaltime_is_date (dtend));
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_ALLDAY], (const void *) &b);

		/* FIXME: for RecurrenceType */
		flag32 = rectypeNone ;
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_RECURTYPE], (const void *) &flag32);

		flag32 = cbdata->appt_id;
		set_SPropValue_proptag(&props[i++], PR_OWNER_APPT_ID, (const void *) &flag32);

		flag32 = cbdata->appt_seq;
		set_SPropValue_proptag(&props[i++],  proptag_array->aulPropTag[I_APPT_SEQ], (const void *) &flag32);

		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_MEET_CLEANGUID], (const void *) cbdata->cleanglobalid);
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_MEET_GUID], (const void *) cbdata->globalid);

		switch (cbdata->meeting_type) {
		case MEETING_OBJECT :
			flag32 = e_cal_component_has_recurrences (comp) ? RecurMeet : SingleMeet; 
			set_SPropValue_proptag(&props[i++], PR_ICON_INDEX, (const void *) &flag32);

			flag32 = 0x0171;
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_COMMON_SIDEEFFECTS], (const void *) &flag32);

			flag32 = olMeeting;
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_MEETINGSTATUS], (const void *) &flag32);

			flag32 = mtgRequest; 
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_MEET_TYPE], (const void *) &flag32);

			flag32 = olResponseOrganized;
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_RESPONSESTATUS], (const void *) &flag32);

			b = 0;
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_INVITED], (const void *) &b);

			break;
		case MEETING_OBJECT_RCVD :
			flag32 = e_cal_component_has_recurrences (comp) ? RecurMeet : SingleMeet; 
			set_SPropValue_proptag(&props[i++], PR_ICON_INDEX, (const void *) &flag32);

			flag32 = 0x0171;
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_COMMON_SIDEEFFECTS], (const void *) &flag32);

			flag32 = olMeetingReceived;
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_MEETINGSTATUS], (const void *) &flag32);

			flag32 = mtgRequest; 
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_MEET_TYPE], (const void *) &flag32);

			flag32 = olResponseNone;
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_RESPONSESTATUS], (const void *) &flag32);

			b = 1;
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_INVITED], (const void *) &b);

			break;
		case MEETING_REQUEST :
			flag32 = 0xFFFFFFFF;  /* no idea why this has to be -1, but that's what the docs say */
			set_SPropValue_proptag(&props[i++], PR_ICON_INDEX, (const void *) &flag32);

			flag32 = 0x1C61;
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_COMMON_SIDEEFFECTS], (const void *) &flag32);

			flag32 = olMeetingReceived;
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_MEETINGSTATUS], (const void *) &flag32);

			flag32 = (cbdata->appt_seq == 0) ? mtgRequest : mtgFull; 
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_MEET_TYPE], (const void *) &flag32);

			flag32 = olResponseNotResponded;
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_RESPONSESTATUS], (const void *) &flag32);

			b = 1;
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_INVITED], (const void *) &b);

			break;
		case MEETING_RESPONSE : 
			flag32 = RespAccept; 
			set_SPropValue_proptag(&props[i++], PR_ICON_INDEX, (const void *) &flag32);

			flag32 = 0x0171;
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_COMMON_SIDEEFFECTS], (const void *) &flag32);
/*
			flag32 = olMeeting;
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_MEETINGSTATUS], (const void *) &flag32);
*/
			flag32 = mtgRequest; 
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_MEET_TYPE], (const void *) &flag32);

			flag32 = olResponseAccepted;
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_RESPONSESTATUS], (const void *) &flag32);

			break;
		case NOT_A_MEETING :
		default :
			flag32 = e_cal_component_has_recurrences (comp) ? RecurAppt : SingleAppt; 
			set_SPropValue_proptag(&props[i++], PR_ICON_INDEX, (const void *) &flag32);

			flag32 = 0x0171;
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_COMMON_SIDEEFFECTS], (const void *) &flag32);

			flag32 = olNonMeeting;
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_MEETINGSTATUS], (const void *) &flag32);

			flag32 = olResponseNone;
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_RESPONSESTATUS], (const void *) &flag32);

			b = 0;
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_INVITED], (const void *) &b);

			break;
		}

		/* FIXME: Recurring */
		b = e_cal_component_has_recurrences (comp) && FALSE; b = 0;
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_RECURRING], (const void *) &b);
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_MEET_ISRECURRING], (const void *) &b);
		b = e_cal_component_has_exceptions (comp) && FALSE; b = 0;
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_MEET_ISEXCEPTION], (const void *) &b);

		/* Online Meeting : we probably would never support this */
		b = 0;
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_ISONLINEMEET], (const void *) &b);

		/* Counter Proposal for appointments : not supported */
		b = 0;
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_COUNTERPROPOSAL], (const void *) &b);

	} else if (kind == ICAL_VTODO_COMPONENT) {
		double d;

		/* Context menu flags */ /* FIXME: for assigned tasks */
		flag32 = 0x0110; 
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_COMMON_SIDEEFFECTS], (const void *) &flag32);

		/* Status, Percent complete, IsComplete */
		flag32 = olTaskNotStarted; 	/* default */
		b = 0; 				/* default */
		d = 0.0;
		prop = icalcomponent_get_first_property (ical_comp, ICAL_PERCENTCOMPLETE_PROPERTY);
		if (prop)
			d = 0.01 * icalproperty_get_percentcomplete (prop);

		flag32 = get_prop_from_taskstatus (icalcomponent_get_status (ical_comp));
		if (flag32 == olTaskComplete) {
			b = 1;
			d = 1.0;
		}

		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_TASK_STATUS], (const void *) &flag32);
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_TASK_PERCENT], (const void *) &d);
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_TASK_ISCOMPLETE], (const void *) &b);

		/* Date completed */
		if (b) {
			struct icaltimetype completed;
			prop = icalcomponent_get_first_property (ical_comp, ICAL_COMPLETED_PROPERTY);
			completed = icalproperty_get_completed (prop);

			t.tv_sec = icaltime_as_timet (completed);
			t.tv_usec = 0;
			set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[I_TASK_COMPLETED], &t);
		}

		/* Start */
		t.tv_sec = icaltime_as_timet (dtstart);
		t.tv_usec = 0;
		set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[I_TASK_START], &t);

		/* Due */
		t.tv_sec = icaltime_as_timet (dtend);
		t.tv_usec = 0;
		set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[I_TASK_DUE], &t);

		/* FIXME: Evolution does not support recurring tasks */
		b = 0;
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_TASK_ISRECURRING], (const void *) &b);

	} else if (kind == ICAL_VJOURNAL_COMPONENT) {
		/* Context menu flags */
		flag32 = 0x0110; 
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_COMMON_SIDEEFFECTS], (const void *) &flag32);

		flag32 = 0x0300; 
		set_SPropValue_proptag(&props[i++], PR_ICON_INDEX, (const void *) &flag32);
	}

	*value = props;
	/* Free this memory at the backends. */
	cbdata->props = props;

	g_print ("\nEnded up setting %d props\n", i);

	return i;
}

