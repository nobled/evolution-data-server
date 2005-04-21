/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2000-2004 Novell, Inc.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>

#include "e-cal-backend-exchange-calendar.h"

#include "e2k-cal-utils.h"
#include "e2k-freebusy.h"
#include "e2k-propnames.h"
#include "e2k-restriction.h"
#include "e2k-utils.h"
#include "e2k-xml-utils.h"
#include "e-folder-exchange.h"
#include "exchange-account.h"
#include "mapi.h"

struct ECalBackendExchangeCalendarPrivate {
	int dummy;
};

enum {
	EX_NO_RECEIPTS = 0,
	EX_DELIVERED_RECEIPTS,
	EX_READ_AND_DELIVERED,
	EX_ALL
};

#define PARENT_TYPE E_TYPE_CAL_BACKEND_EXCHANGE
static ECalBackendExchange *parent_class = NULL;

#define d(x)

static ECalBackendSyncStatus modify_object_with_href (ECalBackendSync *backend, EDataCal *cal, const char *calobj, CalObjModType mod, char **old_object, const char *href);

gboolean check_for_send_options (icalcomponent *icalcomp, E2kProperties *props);

static void
add_timezones_from_comp (ECalBackendExchange *cbex, icalcomponent *icalcomp)
{
	icalcomponent *subcomp;

	switch (icalcomponent_isa (icalcomp)) {
	case ICAL_VTIMEZONE_COMPONENT:
		e_cal_backend_exchange_add_timezone (cbex, icalcomp);
		break;

	case ICAL_VCALENDAR_COMPONENT:
		subcomp = icalcomponent_get_first_component (
			icalcomp, ICAL_VTIMEZONE_COMPONENT);
		while (subcomp) {
			e_cal_backend_exchange_add_timezone (cbex, subcomp);
			subcomp = icalcomponent_get_next_component (
				icalcomp, ICAL_VTIMEZONE_COMPONENT);
		}
		break;

	default:
		break;
	}
}

static gboolean
add_vevent (ECalBackendExchange *cbex,
	    const char *href, const char *lastmod,
	    icalcomponent *icalcomp)
{
	icalproperty *prop, *transp;
	ECalBackendSyncStatus status;

	/* We have to do this here, since if we do it inside the loop
	 * it will mess up the ICAL_X_PROPERTY iterator.
	 */
	transp = icalcomponent_get_first_property (icalcomp, ICAL_TRANSP_PROPERTY);

	/* Check all X-MICROSOFT-CDO properties to fix any needed stuff */
	prop = icalcomponent_get_first_property (icalcomp, ICAL_X_PROPERTY);
	while (prop) {
		const char *x_name, *x_val;
		struct icaltimetype itt;

		x_name = icalproperty_get_x_name (prop);
		x_val = icalproperty_get_x (prop);

		if (!strcmp (x_name, "X-MICROSOFT-CDO-ALLDAYEVENT") &&
		    !strcmp (x_val, "TRUE")) {
			/* All-day event. Fix DTSTART/DTEND to be DATE
			 * values rather than DATE-TIME.
			 */
			itt = icalcomponent_get_dtstart (icalcomp);
			itt.is_date = TRUE;
			itt.hour = itt.minute = itt.second = 0;
			icalcomponent_set_dtstart (icalcomp, itt);

			itt = icalcomponent_get_dtend (icalcomp);
			itt.is_date = TRUE;
			itt.hour = itt.minute = itt.second = 0;
			icalcomponent_set_dtend (icalcomp, itt);
		}

		if (!strcmp (x_name, "X-MICROSOFT-CDO-BUSYSTATUS")) {
			/* It seems OWA sometimes doesn't set the
			 * TRANSP property, so set it from the busy
			 * status.
			 */
			if (transp)
				icalcomponent_remove_property (icalcomp,transp);

			if (!strcmp (x_val, "BUSY"))
				transp = icalproperty_new_transp (ICAL_TRANSP_OPAQUE);
			else if (!strcmp (x_val, "FREE"))
				transp = icalproperty_new_transp (ICAL_TRANSP_TRANSPARENT);

			if (transp)
				icalcomponent_add_property (icalcomp, transp);
		}

		prop = icalcomponent_get_next_property (icalcomp, ICAL_X_PROPERTY);
	}

	/* OWA seems to be broken, and sets the component class to
	 * "CLASS:", by which it means PUBLIC. Evolution treats this
	 * as PRIVATE, so we have to work around.
	 */
	prop = icalcomponent_get_first_property (icalcomp, ICAL_CLASS_PROPERTY);
	if (!prop) {
		prop = icalproperty_new_class (ICAL_CLASS_PUBLIC);
		icalcomponent_add_property (icalcomp, prop);
	}

	/* Exchange sets an ORGANIZER on all events. RFC2445 says:
	 *
	 *   This property MUST NOT be specified in an iCalendar
	 *   object that specifies only a time zone definition or
	 *   that defines calendar entities that are not group
	 *   scheduled entities, but are entities only on a single
	 *   user's calendar.
	 */
	prop = icalcomponent_get_first_property (icalcomp, ICAL_ORGANIZER_PROPERTY);
	if (prop && !icalcomponent_get_first_property (icalcomp, ICAL_ATTENDEE_PROPERTY))
		icalcomponent_remove_property (icalcomp, prop);

	/* Now add to the cache */
	status = e_cal_backend_exchange_add_object (cbex, href, lastmod, icalcomp);
	return (status == GNOME_Evolution_Calendar_Success);
}

static gboolean
add_ical (ECalBackendExchange *cbex, const char *href, const char *lastmod, 
	  const char *uid, const char *body, int len, int receipts)
{
	const char *start, *end;
	char *ical_body;
	icalcomponent *icalcomp, *subcomp, *new_comp;
	icalcomponent_kind kind;
	icalproperty *icalprop;
	ECalComponent *ecomp;
	GSList *attachment_list = NULL;
	gboolean status;

	/* Check for attachments */
	if (uid)
		attachment_list = get_attachment (cbex, uid, body, len);

	start = g_strstr_len (body, len, "\nBEGIN:VCALENDAR");
	if (!start)
		return FALSE;
	start++;
	end = g_strstr_len (start, len - (start - body), "\nEND:VCALENDAR");
	if (!end)
		return FALSE;
	end += sizeof ("\nEND:VCALENDAR");

	ical_body = g_strndup (start, end - start);
	icalcomp = icalparser_parse_string (ical_body);
	g_free (ical_body);
	if (!icalcomp)
		return FALSE;

	kind = icalcomponent_isa (icalcomp);
	if (kind == ICAL_VEVENT_COMPONENT) {
		if (receipts) {
			icalprop = icalproperty_new_x (g_strdup (GINT_TO_POINTER (receipts)));
			icalproperty_set_x_name (icalprop, "X-EVOLUTION-OPTIONS-TRACKINFO");
			icalcomponent_add_property (icalcomp, icalprop);
		}
		if (attachment_list) {
			ecomp = e_cal_component_new ();
			e_cal_component_set_icalcomponent (ecomp, icalcomponent_new_clone (icalcomp));
			e_cal_component_set_attachment_list (ecomp, attachment_list);
			icalcomp = icalcomponent_new_clone (e_cal_component_get_icalcomponent (ecomp));
			g_object_unref (ecomp);
		}
		status = add_vevent (cbex, href, lastmod, icalcomp);
		icalcomponent_free (icalcomp);
		return status;
	} else if (kind != ICAL_VCALENDAR_COMPONENT) {
		icalcomponent_free (icalcomp);
		return FALSE;
	}

	add_timezones_from_comp (cbex, icalcomp);

	subcomp = icalcomponent_get_first_component (
		icalcomp, ICAL_VEVENT_COMPONENT);
	while (subcomp) {
		if (uid && !strcmp (uid, icalcomponent_get_uid (subcomp)) && attachment_list) {
			ecomp = e_cal_component_new ();
			e_cal_component_set_icalcomponent (ecomp, icalcomponent_new_clone (subcomp));
			e_cal_component_set_attachment_list (ecomp, attachment_list);
			new_comp = icalcomponent_new_clone (e_cal_component_get_icalcomponent (ecomp));
			g_object_unref (ecomp);
		} else {
			new_comp = icalcomponent_new_clone (subcomp);
		}

		if (new_comp) {
			add_vevent (cbex, href, lastmod, new_comp);
			icalcomponent_free (new_comp);
		}
		subcomp = icalcomponent_get_next_component (
			icalcomp, ICAL_VEVENT_COMPONENT);
	}
	icalcomponent_free (icalcomp);

	return TRUE;
}

static const char *event_properties[] = {
	E2K_PR_CALENDAR_UID,
	E2K_PR_DAV_LAST_MODIFIED,
	E2K_PR_HTTPMAIL_HAS_ATTACHMENT,
	PR_READ_RECEIPT_REQUESTED,
	PR_ORIGINATOR_DELIVERY_REPORT_REQUESTED	
};
static const int n_event_properties = G_N_ELEMENTS (event_properties);

static const char *new_event_properties[] = {
	PR_INTERNET_CONTENT,
	PR_READ_RECEIPT_REQUESTED,
	PR_ORIGINATOR_DELIVERY_REPORT_REQUESTED	
};
static const int n_new_event_properties = G_N_ELEMENTS (new_event_properties);

static guint
get_changed_events (ECalBackendExchange *cbex, const char *since)
{
	GPtrArray *hrefs;
	GHashTable *modtimes;
	GHashTable *attachments;
	E2kRestriction *rn;
	E2kResultIter *iter;
	E2kResult *result;
	const char *prop, *uid, *modtime, *attach_prop, *receipts;
	char *body;
	guint status;
	E2kContext *ctx;
	int i, status_tracking = EX_NO_RECEIPTS;

	g_return_val_if_fail (E_IS_CAL_BACKEND_EXCHANGE (cbex), SOUP_STATUS_CANCELLED);

	rn = e2k_restriction_andv (
		e2k_restriction_prop_string (E2K_PR_DAV_CONTENT_CLASS,
					     E2K_RELOP_EQ,
					     "urn:content-classes:appointment"),
		e2k_restriction_orv (
			e2k_restriction_prop_int (E2K_PR_CALENDAR_INSTANCE_TYPE,
						  E2K_RELOP_EQ, cdoSingle),
			e2k_restriction_prop_int (E2K_PR_CALENDAR_INSTANCE_TYPE,
						  E2K_RELOP_EQ, cdoMaster),
			NULL),
		NULL);
	if (cbex->private_item_restriction) {
		e2k_restriction_ref (cbex->private_item_restriction);
		rn = e2k_restriction_andv (rn,
					   cbex->private_item_restriction,
					   NULL);
	}
	if (since) {
		rn = e2k_restriction_andv (
			rn,
			e2k_restriction_prop_date (E2K_PR_DAV_LAST_MODIFIED,
						   E2K_RELOP_GT, since),
			NULL);
	} else
		e_cal_backend_exchange_cache_sync_start (cbex);

	iter = e_folder_exchange_search_start (cbex->folder, NULL,
					       event_properties,
					       n_event_properties,
					       rn, NULL, TRUE);
	e2k_restriction_unref (rn);

	hrefs = g_ptr_array_new ();
	modtimes = g_hash_table_new_full (g_str_hash, g_str_equal,
					  g_free, g_free);
	attachments = g_hash_table_new_full (g_str_hash, g_str_equal,
					  g_free, g_free);
	while ((result = e2k_result_iter_next (iter))) {
		uid = e2k_properties_get_prop (result->props,
						E2K_PR_CALENDAR_UID);
		if (!uid)
			continue;
		modtime = e2k_properties_get_prop (result->props,
						   E2K_PR_DAV_LAST_MODIFIED);

		attach_prop = e2k_properties_get_prop (result->props, 
						E2K_PR_HTTPMAIL_HAS_ATTACHMENT);

		receipts = e2k_properties_get_prop (result->props,
						PR_ORIGINATOR_DELIVERY_REPORT_REQUESTED);
		if (receipts && atoi (receipts))
			status_tracking = EX_DELIVERED_RECEIPTS;
	
		receipts = NULL;
		receipts = e2k_properties_get_prop (result->props,
						PR_READ_RECEIPT_REQUESTED);
		if (receipts && atoi (receipts)) {
			if (status_tracking == EX_DELIVERED_RECEIPTS)
				status_tracking = EX_ALL;
			else
				status_tracking = EX_READ_AND_DELIVERED;
		}

		if (!e_cal_backend_exchange_in_cache (cbex, uid, modtime, result->href)) {
			g_ptr_array_add (hrefs, g_strdup (result->href));
			g_hash_table_insert (modtimes, g_strdup (result->href),
					     g_strdup (modtime));
			if (attach_prop && atoi (attach_prop))
				g_hash_table_insert (attachments, g_strdup (result->href),
						g_strdup (uid));
		}
	}
	status = e2k_result_iter_free (iter);

	if (!SOUP_STATUS_IS_SUCCESSFUL (status)) {
		g_ptr_array_free (hrefs, TRUE);
		g_hash_table_destroy (modtimes);
		g_hash_table_destroy (attachments);
		return status;
	}

	if (!since)
		e_cal_backend_exchange_cache_sync_end (cbex);

	if (!hrefs->len) {
		g_ptr_array_free (hrefs, TRUE);
		g_hash_table_destroy (modtimes);
		g_hash_table_destroy (attachments);
		return SOUP_STATUS_OK;
	}

	/* Now get the full text of any that weren't already cached. */
	/* OWA usually sends the attachment and whole event body as part of
		PR_INTERNET_CONTENT property. Fetch events created from OWA */
	prop = PR_INTERNET_CONTENT;
	iter = e_folder_exchange_bpropfind_start (cbex->folder, NULL,
						  (const char **)hrefs->pdata,
						  hrefs->len,
						  new_event_properties, n_new_event_properties);
	for (i = 0; i < hrefs->len; i++)
		g_free (hrefs->pdata[i]);
	g_ptr_array_set_size (hrefs, 0);

	while ((result = e2k_result_iter_next (iter))) {
		GByteArray *ical_data;
		status_tracking = EX_NO_RECEIPTS;

		ical_data = e2k_properties_get_prop (result->props, PR_INTERNET_CONTENT);
		if (!ical_data) {
			/* We didn't get the body, so postpone. */
			g_ptr_array_add (hrefs, g_strdup (result->href));
			continue;
		}
		receipts = e2k_properties_get_prop (result->props,
					PR_ORIGINATOR_DELIVERY_REPORT_REQUESTED);
		if (receipts && atoi (receipts))
			status_tracking = EX_DELIVERED_RECEIPTS;
	
		receipts = NULL;
		receipts = e2k_properties_get_prop (result->props,
					PR_READ_RECEIPT_REQUESTED);
		if (receipts && atoi (receipts)) {
			if (status_tracking == EX_DELIVERED_RECEIPTS)
				status_tracking = EX_ALL;
			else
				status_tracking = EX_READ_AND_DELIVERED;
		}

		modtime = g_hash_table_lookup (modtimes, result->href);
		uid = g_hash_table_lookup (attachments, result->href);
		/* The icaldata already has the attachment. So no need to
			re-fetch it from the server. */
		add_ical (cbex, result->href, modtime, uid,
			  ical_data->data, ical_data->len, status_tracking);
	}
	status = e2k_result_iter_free (iter);

	if (!SOUP_STATUS_IS_SUCCESSFUL (status)) {
		g_ptr_array_free (hrefs, TRUE);
		g_hash_table_destroy (modtimes);
		return status;
	}
	if (!hrefs->len) {
		g_ptr_array_free (hrefs, TRUE);
		g_hash_table_destroy (modtimes);
		return SOUP_STATUS_OK;
	}

	/* Get the remaining ones the hard way */
	ctx = exchange_account_get_context (cbex->account);
	if (!ctx) {
		/* This either means we lost connection or we are in offline mode */
		return SOUP_STATUS_CANT_CONNECT;
	}
	for (i = 0; i < hrefs->len; i++) {
		int length;

		status = e2k_context_get (ctx, NULL, hrefs->pdata[i],
					  NULL, &body, &length);
		if (!SOUP_STATUS_IS_SUCCESSFUL (status))
			continue;
		modtime = g_hash_table_lookup (modtimes, hrefs->pdata[i]);

		uid = g_hash_table_lookup (attachments, hrefs->pdata[i]);

		add_ical (cbex, hrefs->pdata[i], modtime, uid, body, length, 0);
		g_free (body);
	}

	for (i = 0; i < hrefs->len; i++)
		g_free (hrefs->pdata[i]);
	g_ptr_array_free (hrefs, TRUE);
	g_hash_table_destroy (modtimes);
	g_hash_table_destroy (attachments);
	return status;
}

static ECalBackendSyncStatus
open_calendar (ECalBackendSync *backend, EDataCal *cal,
	       gboolean only_if_exists,
	       const char *username, const char *password)
{
	ECalBackendSyncStatus status;

	/* Do the generic part */
	status = E_CAL_BACKEND_SYNC_CLASS (parent_class)->open_sync (
		backend, cal, only_if_exists, username, password);
	if (status != GNOME_Evolution_Calendar_Success)
		return status;

	if (!e_cal_backend_exchange_is_online (E_CAL_BACKEND_EXCHANGE (backend))) {
		return GNOME_Evolution_Calendar_Success;
	}

	/* Now load the rest of the calendar items */
	status = get_changed_events (E_CAL_BACKEND_EXCHANGE (backend), NULL);
	if (status == E2K_HTTP_OK)
		return GNOME_Evolution_Calendar_Success;
	else
		return GNOME_Evolution_Calendar_OtherError; /* FIXME */
}

struct _cb_data {
	ECalBackendSync *be;
	icalcomponent *vcal_comp;
	EDataCal *cal;
};

static void
add_timezone_cb (icalparameter *param, void *data)
{
	struct _cb_data *cbdata = (struct _cb_data *) data;
	icalcomponent *vtzcomp;
	const char *tzid;
	char *izone = NULL;

	g_return_if_fail (cbdata != NULL);

	tzid = icalparameter_get_tzid (param);
	if (tzid == NULL)
		return;
	if (icalcomponent_get_timezone (cbdata->vcal_comp, tzid))
		return;

	get_timezone (cbdata->be, cbdata->cal, tzid, &izone);
	if (izone == NULL)
		return;

	vtzcomp = icalcomponent_new_from_string (izone);
	if (vtzcomp)
		icalcomponent_add_component (cbdata->vcal_comp, vtzcomp);
}

gboolean
check_for_send_options (icalcomponent *icalcomp, E2kProperties *props)
{
	icalproperty *icalprop;
	gboolean exists = FALSE;
	const char *x_name, *x_val;

	icalprop = icalcomponent_get_first_property (icalcomp, ICAL_X_PROPERTY);	
	while (icalprop && !exists) {
		x_name = icalproperty_get_x_name (icalprop);
		if (!strcmp(x_name, "X-EVOLUTION-OPTIONS-TRACKINFO")) {
			exists = TRUE;
			x_val = icalproperty_get_x (icalprop);
			switch (atoi (x_val)) {
				case EX_ALL: /* Track if delivered and opened */
				case EX_READ_AND_DELIVERED: /* Track if delivered and opened */
					e2k_properties_set_int (props, 
					PR_READ_RECEIPT_REQUESTED, 1);
					/* Fall Through */
				case EX_DELIVERED_RECEIPTS : /* Track if delivered */
					e2k_properties_set_int (props, 
					PR_ORIGINATOR_DELIVERY_REPORT_REQUESTED, 1);
					break;
				default : /* None */
					exists = FALSE;
					break; 
			}
		}
		icalprop = icalcomponent_get_next_property (icalcomp, ICAL_X_PROPERTY);
	}

	return exists;
}

static ECalBackendSyncStatus
create_object (ECalBackendSync *backend, EDataCal *cal,
	       char **calobj, char **uid)
{
	/* FIXME : Return some value in uid */
	ECalBackendExchangeCalendar *cbexc;
	icalcomponent *icalcomp, *real_icalcomp;
	icalcomponent_kind kind;
	icalproperty *icalprop;
	const char *temp_comp_uid;
	char *busystatus, *insttype, *allday, *importance, *lastmod;
	struct icaltimetype current, startt;
	char *location, *ru_header;
	ECalComponent *comp;
	char *body, *body_crlf, *msg;
	char *from, *date;
	const char *summary;
	char *attach_body = NULL;
	char *attach_body_crlf = NULL;
	char *boundary = NULL;
	E2kHTTPStatus http_status;
	E2kProperties *props = e2k_properties_new ();
	E2kContext *e2kctx;
	struct _cb_data *cbdata;
	gboolean send_options;
	
	cbexc =	E_CAL_BACKEND_EXCHANGE_CALENDAR (backend);
	
	g_return_val_if_fail (E_IS_CAL_BACKEND_EXCHANGE_CALENDAR (cbexc), GNOME_Evolution_Calendar_InvalidObject);
	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_InvalidObject);

	if (!e_cal_backend_exchange_is_online (E_CAL_BACKEND_EXCHANGE (backend))) {
		/* FIXME */
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	/* check for permission denied:: priv->writable??
	   ....
	 */
	
	icalcomp = icalparser_parse_string (*calobj);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;
	
	kind = icalcomponent_isa (icalcomp);
	
	if (kind != e_cal_backend_get_kind (E_CAL_BACKEND (backend))) {
		
		icalcomponent_free (icalcomp);
		return GNOME_Evolution_Calendar_InvalidObject;
	}
	
	temp_comp_uid = icalcomponent_get_uid (icalcomp);
	if (!temp_comp_uid) {
		icalcomponent_free (icalcomp);
		return GNOME_Evolution_Calendar_InvalidObject;
	}
	#if 0
	if (lookup_component (E_CAL_BACKEND_EXCHANGE (cbexc), comp_uid))
	{
		icalcomponent_free (icalcomp);
		return ;
	}
	#endif
	
	/* Send options */
	send_options = check_for_send_options (icalcomp, props);
	
	/*set X-MICROSOFT-CDO properties*/
	busystatus = "BUSY";
	icalprop = icalcomponent_get_first_property (icalcomp, 
						     ICAL_TRANSP_PROPERTY);
	if (icalprop) {
		icalproperty_transp transp_val = icalproperty_get_transp (icalprop);
		if (transp_val == ICAL_TRANSP_TRANSPARENT)
			busystatus = "FREE";
	}
	
	if (e_cal_util_component_has_recurrences (icalcomp))
		insttype = "1";
	else
		insttype = "0";
	
	startt = icalcomponent_get_dtstart (icalcomp);
	if(icaltime_is_date (startt))
		allday = "TRUE";
	else
		allday = "FALSE";
	
	importance = "1";
	icalprop = icalcomponent_get_first_property (icalcomp,
						     ICAL_PRIORITY_PROPERTY);
	if (icalprop) {
		int prio = icalproperty_get_priority (icalprop);
		importance = prio < 5 ? "2" : prio > 5 ? "0" : "1";
	}
	
	icalprop = icalproperty_new_x (busystatus);
	icalproperty_set_x_name (icalprop, "X-MICROSOFT-CDO-BUSYSTATUS");
	icalcomponent_add_property (icalcomp, icalprop);
	
	icalprop = icalproperty_new_x (insttype);
	icalproperty_set_x_name (icalprop, "X-MICROSOFT-CDO-INSTTYPE");
	icalcomponent_add_property (icalcomp, icalprop);
	
	icalprop = icalproperty_new_x (allday);
	icalproperty_set_x_name (icalprop, "X-MICROSOFT-CDO-ALLDAYEVENT");
	icalcomponent_add_property (icalcomp, icalprop);
	
	icalprop = icalproperty_new_x (importance);
	icalproperty_set_x_name (icalprop, "X-MICROSOFT-CDO-IMPORTANCE");
	icalcomponent_add_property (icalcomp, icalprop);

	/*set created and last_modified*/
	current = icaltime_from_timet (time (NULL), 0);
	icalcomponent_add_property (icalcomp, icalproperty_new_created (current));
	icalcomponent_add_property (icalcomp, icalproperty_new_lastmodified (current));
	
	/* Fetch summary */
	summary = icalcomponent_get_summary (icalcomp);
	if (!summary)
		summary = "";
	
	lastmod = e2k_timestamp_from_icaltime (current);
	
	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomp);	
	
	/* Check for attachments */
	if (e_cal_component_has_attachments (comp)) {
		d(printf ("This comp has attachments !!\n"));
		attach_body = build_msg (E_CAL_BACKEND_EXCHANGE (cbexc), comp, summary, &boundary);
		attach_body_crlf = e_cal_backend_exchange_lf_to_crlf (attach_body);	
	}

	cbdata = g_new0 (struct _cb_data, 1);
	cbdata->be = backend;
	cbdata->vcal_comp = e_cal_util_new_top_level ();
	cbdata->cal = cal;

	/* Remove X parameters from properties */
	/* This is specifically for X-EVOLUTION-END-DATE, 
	   but removing anything else is probably ok too */
	for (icalprop = icalcomponent_get_first_property (icalcomp, ICAL_ANY_PROPERTY);
	     icalprop != NULL;
	     icalprop = icalcomponent_get_next_property (icalcomp, ICAL_ANY_PROPERTY))
	{
		icalproperty_remove_parameter (icalprop, ICAL_X_PARAMETER);
	}
		
	/* add the timezones information and the component itself
	   to the VCALENDAR object */	
	e_cal_component_commit_sequence (comp);
	*calobj = e_cal_component_get_as_string (comp);
	if (!*calobj) {
		g_object_unref (comp);
		icalcomponent_free (cbdata->vcal_comp);
		g_free (cbdata);
		return GNOME_Evolution_Calendar_OtherError;
	}
	real_icalcomp = icalparser_parse_string (*calobj);

	icalcomponent_foreach_tzid (real_icalcomp, add_timezone_cb, cbdata);
	icalcomponent_add_component (cbdata->vcal_comp, real_icalcomp);	
	
	body = icalcomponent_as_ical_string (cbdata->vcal_comp);
	body_crlf = e_cal_backend_exchange_lf_to_crlf (body);	
		
	date = e_cal_backend_exchange_make_timestamp_rfc822 (time (NULL));
	from = e_cal_backend_exchange_get_from_string (backend, comp);

	if (attach_body) {
		msg = g_strdup_printf ("Subject: %s\r\n"
				       "Date: %s\r\n"
				       "MIME-Version: 1.0\r\n"
				       "Content-Type: multipart/mixed;\r\n"
				       "\tboundary=\"%s\";\r\n"
				       "X-MS_Has-Attach: yes\r\n"
				       "From: %s\r\n"
					"\r\n--%s\r\n"
				       "content-class: urn:content-classes:appointment\r\n"
				       "Content-Type: text/calendar;\r\n"
				       "\tmethod=REQUEST;\r\n"
				       "\tcharset=\"utf-8\"\r\n"
				       "Content-Transfer-Encoding: 8bit\r\n"
				       "Importance: normal\r\n"
				       "Priority: normal\r\n"
				       "\r\n%s\r\n%s", summary, date, boundary,
				       from ? from : "Evolution", boundary,
				       body_crlf, attach_body_crlf);
		g_free (boundary);

	} else {
		msg = g_strdup_printf ("Subject: %s\r\n"
				       "Date: %s\r\n"
				       "MIME-Version: 1.0\r\n"
				       "Content-Type: text/calendar;\r\n"
				       "\tmethod=REQUEST;\r\n"
				       "\tcharset=\"utf-8\"\r\n"
				       "Content-Transfer-Encoding: 8bit\r\n"
				       "content-class: urn:content-classes:appointment\r\n"
				       "Importance: normal\r\n"
				       "Priority: normal\r\n"
				       "From: %s\r\n"
				       "\r\n%s", summary, date,
				       from ? from : "Evolution",
				       body_crlf);
	}

	http_status = e_folder_exchange_put_new (E_CAL_BACKEND_EXCHANGE (cbexc)->folder, NULL, summary, 
						NULL, NULL, "message/rfc822", 
						msg, strlen(msg), &location, &ru_header);	

	if ((http_status == E2K_HTTP_CREATED) && send_options) {
		e2kctx = exchange_account_get_context (E_CAL_BACKEND_EXCHANGE (cbexc)->account);
		http_status = e2k_context_proppatch (e2kctx, NULL, location, props, FALSE, NULL);
	}

	g_free (date);
	g_free (from);
	g_free (body_crlf);
	g_free (msg); 
	icalcomponent_free (cbdata->vcal_comp); // not sure
	g_free (cbdata);

	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (http_status)) {
		g_object_unref (comp);
		g_free (location);
		g_free (lastmod);
		return GNOME_Evolution_Calendar_OtherError;
	}

	/*add object*/
	e_cal_backend_exchange_add_object (E_CAL_BACKEND_EXCHANGE (cbexc), location, lastmod, icalcomp);
		
	g_object_unref (comp);
	g_free (lastmod);
	g_free (location);
	e2k_properties_free (props);

	*uid = g_strdup (temp_comp_uid);	
	return GNOME_Evolution_Calendar_Success;
}

#define BUSYSTATUS 	0x01
#define INSTTYPE	0x02
#define ALLDAY		0x04
#define IMPORTANCE	0x08

static void
update_x_properties (ECalBackendExchange *cbex, ECalComponent *comp)
{
	icalcomponent *icalcomp;
	icalproperty *icalprop;
	const char *x_name, *x_val;
	ECalComponentTransparency transp;
	ECalComponentDateTime dtstart;
	int *priority;
	const char *busystatus, *insttype, *allday, *importance;
	int prop_set = 0;

	e_cal_component_get_transparency (comp, &transp);
	if (transp == E_CAL_COMPONENT_TRANSP_TRANSPARENT)
		busystatus = "FREE";
	else
		busystatus = "BUSY";

	if (e_cal_component_has_recurrences (comp))
		insttype = "1";
	else
		insttype = "0";

	e_cal_component_get_dtstart (comp, &dtstart);
	if (dtstart.value->is_date)
		allday = "TRUE";
	else
		allday = "FALSE";
	e_cal_component_free_datetime (&dtstart);

	e_cal_component_get_priority (comp, &priority);
	if (priority) {
		importance = *priority < 5 ? "2" : *priority > 5 ? "0" : "1";
		e_cal_component_free_priority (priority);
	} else
		importance = "1";

	/* Go through the existing X-MICROSOFT-CDO- properties first */
       	icalcomp = e_cal_component_get_icalcomponent (comp);
	icalprop = icalcomponent_get_first_property (icalcomp, ICAL_X_PROPERTY);
	while (icalprop) {
		x_name = icalproperty_get_x_name (icalprop);
		x_val = icalproperty_get_x (icalprop);

		if (!strcmp (x_name, "X-MICROSOFT-CDO-BUSYSTATUS")) {
			/* If TRANSP was TRANSPARENT, BUSYSTATUS must
			 * be FREE. But if it was OPAQUE, it can
			 * be BUSY, TENTATIVE, or OOF, so only change
			 * it if it was FREE.
			 */
			if (busystatus && strcmp (busystatus, "FREE") == 0)
				icalproperty_set_x (icalprop, "FREE");
			else if (strcmp (x_val, "FREE") == 0)
				icalproperty_set_x (icalprop, "BUSY");
			prop_set |= BUSYSTATUS;
		} else if (!strcmp (x_name, "X-MICROSOFT-CDO-INSTTYPE")) {
			icalproperty_set_x (icalprop, insttype);
			prop_set |= INSTTYPE;
		} else if (!strcmp (x_name, "X-MICROSOFT-CDO-ALLDAYEVENT")) {
			icalproperty_set_x (icalprop, allday);
			prop_set |= ALLDAY;
		} else if (!strcmp (x_name, "X-MICROSOFT-CDO-IMPORTANCE")) {
			icalproperty_set_x (icalprop, importance);
			prop_set |= IMPORTANCE;
		} else if (!strcmp (x_name, "X-MICROSOFT-CDO-MODPROPS"))
			icalcomponent_remove_property (icalcomp, icalprop);

		icalprop = icalcomponent_get_next_property (icalcomp, ICAL_X_PROPERTY);
	}

	/* Now set the ones that weren't set. */
	if (!(prop_set & BUSYSTATUS)) {
		icalprop = icalproperty_new_x (busystatus);
		icalproperty_set_x_name (icalprop, "X-MICROSOFT-CDO-BUSYSTATUS");
		icalcomponent_add_property (icalcomp, icalprop);
	}

	if (!(prop_set & INSTTYPE)) {
		icalprop = icalproperty_new_x (insttype);
		icalproperty_set_x_name (icalprop, "X-MICROSOFT-CDO-INSTTYPE");
		icalcomponent_add_property (icalcomp, icalprop);
	}

	if (!(prop_set & ALLDAY)) {
		icalprop = icalproperty_new_x (allday);
		icalproperty_set_x_name (icalprop, "X-MICROSOFT-CDO-ALLDAYEVENT");
		icalcomponent_add_property (icalcomp, icalprop);
	}

	if (!(prop_set & IMPORTANCE)) {
		icalprop = icalproperty_new_x (importance);
		icalproperty_set_x_name (icalprop, "X-MICROSOFT-CDO-IMPORTANCE");
		icalcomponent_add_property (icalcomp, icalprop);
	}
}



static ECalBackendSyncStatus
modify_object (ECalBackendSync *backend, EDataCal *cal,
	       const char *calobj, CalObjModType mod,
	       char **old_object, char **new_object)
{
	return modify_object_with_href (backend, cal, calobj, mod, old_object, NULL);
}

static ECalBackendSyncStatus
modify_object_with_href (ECalBackendSync *backend, EDataCal *cal,
	       const char *calobj, CalObjModType mod,
	       char **old_object, const char *href)
{
	ECalBackendExchangeCalendar *cbexc;
	ECalBackendExchangeComponent *ecomp;
	icalcomponent *icalcomp, *real_icalcomp, *updated_icalcomp;
	ECalComponent *real_ecomp, *cached_ecomp, *updated_ecomp;
	const char *comp_uid;
	char *updated_ecomp_str, *real_comp_str;
	char *body, *body_crlf, *msg;
	char *attach_body = NULL;
	char *attach_body_crlf = NULL;
	char *boundary = NULL;
	struct icaltimetype last_modified;
	icalcomponent_kind kind;
	ECalComponentDateTime dt;
	struct _cb_data *cbdata;
	icalproperty *icalprop;
	E2kHTTPStatus http_status;
	char *from, *date;
	const char *summary, *new_href;
	gboolean send_options;
	E2kContext *ctx;
	E2kProperties *props = e2k_properties_new ();

	cbexc =	E_CAL_BACKEND_EXCHANGE_CALENDAR (backend);

	g_return_val_if_fail (E_IS_CAL_BACKEND_EXCHANGE_CALENDAR (cbexc), GNOME_Evolution_Calendar_InvalidObject);
	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_InvalidObject);
	
	if (!e_cal_backend_exchange_is_online (E_CAL_BACKEND_EXCHANGE (backend))) {
                /* FIXME */
                return GNOME_Evolution_Calendar_InvalidObject;
        }

	icalcomp = icalparser_parse_string (calobj);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;
	
	kind = icalcomponent_isa (icalcomp);
	
	if (kind != e_cal_backend_get_kind (E_CAL_BACKEND (backend))) {
		icalcomponent_free (icalcomp);
		return GNOME_Evolution_Calendar_InvalidObject;
	}
	comp_uid = icalcomponent_get_uid (icalcomp);
	
	ecomp = get_exchange_comp (E_CAL_BACKEND_EXCHANGE (cbexc), comp_uid);
	
	if (!ecomp){
		icalcomponent_free (icalcomp);
		return GNOME_Evolution_Calendar_ObjectNotFound;
	}
	
	/* Fetch summary */
	summary = icalcomponent_get_summary (icalcomp);
	if (!summary)
		summary = "";
	
	updated_ecomp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (updated_ecomp, icalcomp);

	update_x_properties (E_CAL_BACKEND_EXCHANGE (cbexc), updated_ecomp);
	
	last_modified = icaltime_from_timet (time (NULL), 0);
	e_cal_component_set_last_modified (updated_ecomp, &last_modified);

	if (e_cal_component_has_attachments (updated_ecomp)) {
		d(printf ("This comp has attachments !!\n"));
		attach_body = build_msg (E_CAL_BACKEND_EXCHANGE (cbexc), updated_ecomp, summary, &boundary);
		attach_body_crlf = e_cal_backend_exchange_lf_to_crlf (attach_body);	
	}

	updated_ecomp_str = e_cal_component_get_as_string (updated_ecomp);
	updated_icalcomp = icalparser_parse_string (updated_ecomp_str);
	g_free (updated_ecomp_str);
	if (!updated_icalcomp) {
		g_object_unref (updated_ecomp);
		return GNOME_Evolution_Calendar_OtherError;
	}

	/* send options */
	send_options = check_for_send_options (updated_icalcomp, props);

	/* Remove X parameters from properties */
	/* This is specifically for X-EVOLUTION-END-DATE, 
	   but removing anything else is probably ok too */
	for (icalprop = icalcomponent_get_first_property (updated_icalcomp, ICAL_ANY_PROPERTY);
	     icalprop != NULL;
	     icalprop = icalcomponent_get_next_property (updated_icalcomp, ICAL_ANY_PROPERTY))
	{
		icalproperty_remove_parameter (icalprop, ICAL_X_PARAMETER);
	}

	real_ecomp = e_cal_component_new ();
	if (!e_cal_component_set_icalcomponent (real_ecomp, updated_icalcomp)) {
		g_object_unref (real_ecomp);
		g_object_unref (updated_ecomp);
		return GNOME_Evolution_Calendar_OtherError;
	}

	cbdata = g_new0 (struct _cb_data, 1);
	cbdata->be = backend;
	cbdata->vcal_comp = e_cal_util_new_top_level ();
	cbdata->cal = cal;	

	e_cal_component_get_dtstart (real_ecomp, &dt);
	if (dt.value->is_date) {
		icaltimezone *zone;

		zone = e_cal_backend_exchange_get_default_time_zone (backend);
		if (!zone)
			zone = icaltimezone_get_utc_timezone ();

		dt.value->is_date = FALSE;
		dt.value->is_utc = FALSE;
		dt.value->hour = dt.value->minute = dt.value->second = 0;
		dt.value->zone = zone;
		
		g_free ((char *)dt.tzid);
		dt.tzid = g_strdup (icaltimezone_get_tzid (zone));
		e_cal_component_set_dtstart (real_ecomp, &dt);
		e_cal_component_free_datetime (&dt);

		e_cal_component_get_dtend (real_ecomp, &dt);
		dt.value->is_date = FALSE;
		dt.value->is_utc = FALSE;
		dt.value->hour = dt.value->minute = dt.value->second = 0;
		dt.value->zone = zone;

		g_free ((char *)dt.tzid);
		dt.tzid = g_strdup (icaltimezone_get_tzid (zone));
		e_cal_component_set_dtend (real_ecomp, &dt);
	}
	e_cal_component_free_datetime (&dt);

	/* Fix UNTIL date in a simple recurrence */
	if (e_cal_component_has_recurrences (real_ecomp)
	    && e_cal_component_has_simple_recurrence (real_ecomp)) {
		GSList *rrule_list;
		struct icalrecurrencetype *r;
		
		e_cal_component_get_rrule_list (real_ecomp, &rrule_list);
		r = rrule_list->data;

		if (!icaltime_is_null_time (r->until) && r->until.is_date) {
			icaltimezone *from_zone, *to_zone;
			
			e_cal_component_get_dtstart (real_ecomp, &dt);

			if (dt.tzid == NULL)
				from_zone = icaltimezone_get_utc_timezone ();
			else {
				char *izone = NULL;
				get_timezone (backend, cal, dt.tzid, &izone);
				from_zone = icalcomponent_get_timezone (icalcomponent_new_from_string (izone),
											dt.tzid);
			}
			to_zone = icaltimezone_get_utc_timezone ();

			r->until.hour = dt.value->hour;
			r->until.minute = dt.value->minute;
			r->until.second = dt.value->second;
			r->until.is_date = FALSE;
			
			icaltimezone_convert_time (&r->until, from_zone, to_zone);
			r->until.is_utc = TRUE;
			
			e_cal_component_set_rrule_list (real_ecomp, rrule_list);			
			e_cal_component_free_datetime (&dt);
		}

		e_cal_component_free_recur_list (rrule_list);
	}


	
	/* add the timezones information and the component itself
	   to the VCALENDAR object */
	e_cal_component_commit_sequence (real_ecomp);
	real_comp_str = e_cal_component_get_as_string (real_ecomp);
	if (!real_comp_str) {
		g_object_unref (real_ecomp);
		g_object_unref (updated_ecomp);	
		icalcomponent_free (cbdata->vcal_comp);
		g_free (cbdata);
		return GNOME_Evolution_Calendar_OtherError;
	}
	real_icalcomp = icalparser_parse_string (real_comp_str);
	g_free (real_comp_str);

	icalcomponent_foreach_tzid (real_icalcomp, add_timezone_cb, cbdata);
	icalcomponent_add_component (cbdata->vcal_comp, real_icalcomp);

	body = icalcomponent_as_ical_string (cbdata->vcal_comp);
	body_crlf = e_cal_backend_exchange_lf_to_crlf (body);
	
	date = e_cal_backend_exchange_make_timestamp_rfc822 (time (NULL));
	from = e_cal_backend_exchange_get_from_string (backend, updated_ecomp);	
	
	if (attach_body) {
		msg = g_strdup_printf ("Subject: %s\r\n"
				       "Date: %s\r\n"
				       "MIME-Version: 1.0\r\n"
				       "Content-Type: multipart/mixed;\r\n"
				       "\tboundary=\"%s\";\r\n"
				       "X-MS_Has-Attach: yes\r\n"
				       "From: %s\r\n"
					"\r\n--%s\r\n"
				       "content-class: urn:content-classes:appointment\r\n"
				       "Content-Type: text/calendar;\r\n"
				       "\tmethod=REQUEST;\r\n"
				       "\tcharset=\"utf-8\"\r\n"
				       "Content-Transfer-Encoding: 8bit\r\n"
				       "Importance: normal\r\n"
				       "Priority: normal\r\n"
				       "\r\n%s\r\n%s", summary, date, boundary,
				       from ? from : "Evolution", boundary,
				       body_crlf, attach_body_crlf);
		g_free (boundary);

	} else {

		msg = g_strdup_printf ("Subject: %s\r\n"
				       "Date: %s\r\n"
				       "MIME-Version: 1.0\r\n"
				       "Content-Type: text/calendar;\r\n"
				       "\tmethod=REQUEST;\r\n"
				       "\tcharset=\"utf-8\"\r\n"
				       "Content-Transfer-Encoding: 8bit\r\n"
				       "content-class: urn:content-classes:appointment\r\n"
				       "Importance: normal\r\n"
				       "Priority: normal\r\n"
				       "From: %s\r\n"
				       "\r\n%s", summary, date,
				       from ? from : "Evolution",
				       body_crlf);
	}
	
	g_free (date);
	g_free (from);
	g_free (body_crlf);

	cached_ecomp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (cached_ecomp, icalcomponent_new_clone (ecomp->icomp));
	*old_object = e_cal_component_get_as_string (cached_ecomp);
	
	ctx = exchange_account_get_context (E_CAL_BACKEND_EXCHANGE (cbexc)->account);	
	
	/* PUT the iCal object in the Exchange server */
	if (href)
		new_href = href;
	else
		new_href = ecomp->href;

	http_status = e2k_context_put (ctx, NULL, new_href, "message/rfc822",
				       		msg, strlen (msg), NULL);

	if ((E2K_HTTP_STATUS_IS_SUCCESSFUL (http_status)) && send_options)
		http_status = e2k_context_proppatch (ctx, NULL, new_href, props, FALSE, NULL);

	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (http_status))
		e_cal_backend_exchange_modify_object (E_CAL_BACKEND_EXCHANGE (cbexc), 
							real_icalcomp, mod);
	
	g_free (msg);
	g_object_unref (real_ecomp);
	g_object_unref (updated_ecomp);
	g_object_unref (cached_ecomp);
	icalcomponent_free (cbdata->vcal_comp);
	g_free (cbdata);
	e2k_properties_free (props);

	return GNOME_Evolution_Calendar_Success;
}
 
static ECalBackendSyncStatus
remove_object (ECalBackendSync *backend, EDataCal *cal,
	       const char *uid, const char *rid, CalObjModType mod,
	       char **old_object, char **object)
{
	ECalBackendExchangeCalendar *cbexc;
	ECalBackendExchangeComponent *ecomp;
	E2kHTTPStatus status;
	E2kContext *ctx;
	ECalComponent *comp;
	char *calobj, *obj;
	struct icaltimetype time_rid;
	ECalBackendSyncStatus ebs_status;
	
	cbexc = E_CAL_BACKEND_EXCHANGE_CALENDAR (backend);

	g_return_val_if_fail (E_IS_CAL_BACKEND_EXCHANGE_CALENDAR (cbexc), GNOME_Evolution_Calendar_InvalidObject);

	if (!e_cal_backend_exchange_is_online (E_CAL_BACKEND_EXCHANGE (backend))) {
                /* FIXME */
                return GNOME_Evolution_Calendar_InvalidObject;
        }

	ecomp = get_exchange_comp (E_CAL_BACKEND_EXCHANGE (cbexc), uid);
	
	if (!ecomp)
		return GNOME_Evolution_Calendar_ObjectNotFound;
		
	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (ecomp->icomp));
	if (old_object)
		*old_object = e_cal_component_get_as_string (comp);
	
	switch (mod) {
		
		case CALOBJ_MOD_ALL:
			ctx = exchange_account_get_context (E_CAL_BACKEND_EXCHANGE (cbexc)->account);	
			
			status = e2k_context_delete (ctx, NULL, ecomp->href);			
			if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {		
				
				if (e_cal_backend_exchange_remove_object (E_CAL_BACKEND_EXCHANGE (cbexc), uid)) {
						g_object_unref (comp);
						return GNOME_Evolution_Calendar_Success;
				}
			}
			*object = NULL;
			break;
		case CALOBJ_MOD_THIS:
			/*remove_instance and modify */
			if (rid && *rid) {
				time_rid = icaltime_from_string (rid);
				e_cal_util_remove_instances (ecomp->icomp, time_rid, mod);
			}
			calobj  = (char *) icalcomponent_as_ical_string (ecomp->icomp);
			ebs_status = modify_object (backend, cal, calobj, mod, &obj, NULL);
			if (ebs_status != GNOME_Evolution_Calendar_Success)
				goto error;
			
			g_free (obj);
			return ebs_status;
		
		default:
			break;
	}

error:
	return GNOME_Evolution_Calendar_OtherError;
}

static ECalBackendSyncStatus
receive_objects (ECalBackendSync *backend, EDataCal *cal,
		 const char *calobj)
{
	ECalBackendExchangeCalendar *cbexc;
	ECalComponent *comp = NULL;
	GList *comps, *l;
	struct icaltimetype current;
	icalproperty_method method;
	icalcomponent *subcomp;
	ECalBackendSyncStatus status = GNOME_Evolution_Calendar_Success;	

	cbexc =	E_CAL_BACKEND_EXCHANGE_CALENDAR (backend);

	g_return_val_if_fail (E_IS_CAL_BACKEND_EXCHANGE_CALENDAR (cbexc), GNOME_Evolution_Calendar_InvalidObject);
	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_InvalidObject);
	
	if (!e_cal_backend_exchange_is_online (E_CAL_BACKEND_EXCHANGE (backend))) {
                /* FIXME */
                return GNOME_Evolution_Calendar_InvalidObject;
        }

	status = e_cal_backend_exchange_extract_components (calobj, &method, &comps);
	if (status != GNOME_Evolution_Calendar_Success)
		return GNOME_Evolution_Calendar_InvalidObject;

	for (l = comps; l; l= l->next) {
		const char *uid, *rid;
		char *calobj;
		char *object = NULL;
		
		subcomp = l->data;
		
		comp = e_cal_component_new ();
		e_cal_component_set_icalcomponent (comp, subcomp);
		
		/*create time and last modified*/
		current = icaltime_from_timet (time (NULL), 0);
		e_cal_component_set_created (comp, &current);
		e_cal_component_set_last_modified (comp, &current);
		
		e_cal_component_get_uid (comp, &uid);
		rid = e_cal_component_get_recurid_as_string (comp);

		switch (method) {
		case ICAL_METHOD_PUBLISH:
		case ICAL_METHOD_REQUEST:
		case ICAL_METHOD_REPLY:
			if (get_exchange_comp (E_CAL_BACKEND_EXCHANGE (cbexc), uid)) {
				char *old_object;
				
				calobj = (char *) icalcomponent_as_ical_string (subcomp);
				status = modify_object (backend, cal, calobj, CALOBJ_MOD_THIS, &old_object, NULL);
				if (status != GNOME_Evolution_Calendar_Success)
					goto error;

				e_cal_backend_notify_object_modified (E_CAL_BACKEND (backend), old_object, calobj);

				g_free (old_object);
			} else {
				char *returned_uid;

				calobj = (char *) icalcomponent_as_ical_string (subcomp);
				status = create_object (backend, cal, &calobj, &returned_uid);
				if (status != GNOME_Evolution_Calendar_Success)
					goto error;

				e_cal_backend_notify_object_created (E_CAL_BACKEND (backend), calobj);
			}
			break;
		case ICAL_METHOD_ADD:
			/* FIXME This should be doable once all the recurid stuff is done ??*/
			break;

		case ICAL_METHOD_CANCEL:
			calobj = (char *) icalcomponent_as_ical_string (subcomp);
			if (rid)
				status = remove_object (backend, cal, uid, rid, CALOBJ_MOD_THIS, &calobj, &object);
			else
				status = remove_object (backend, cal, uid, NULL, CALOBJ_MOD_ALL, &calobj, &object);
			if (status == GNOME_Evolution_Calendar_Success) 
				e_cal_backend_notify_object_removed (E_CAL_BACKEND (backend), uid, calobj, NULL);
			if (object) {
				g_free (object);
				object = NULL;
			}
			break;
		default:
			status = GNOME_Evolution_Calendar_UnsupportedMethod;
			goto error;
		}
		g_object_unref (comp);
	}
	g_list_free (comps);
	return status;
		
 error:	
	if (comp)
		g_object_unref (comp);
	return status;
}

typedef enum {
	E_CAL_BACKEND_EXCHANGE_BOOKING_OK,
	E_CAL_BACKEND_EXCHANGE_BOOKING_NO_SUCH_USER,
	E_CAL_BACKEND_EXCHANGE_BOOKING_BUSY,
	E_CAL_BACKEND_EXCHANGE_BOOKING_PERMISSION_DENIED,
	E_CAL_BACKEND_EXCHANGE_BOOKING_ERROR
} ECalBackendExchangeBookingResult;

												 
/* start_time and end_time are in e2k_timestamp format. */
static ECalBackendExchangeBookingResult
book_resource (ECalBackendExchange *cbex,
	       EDataCal *cal,
	       const char *resource_email,
	       ECalComponent *comp,
	       icalproperty_method method)
{
	E2kGlobalCatalog *gc;
	E2kGlobalCatalogEntry *entry;
	E2kGlobalCatalogStatus gcstatus;
	ECalBackendExchangeComponent *ecomp;
	ECalComponentText old_text, new_text;
	E2kHTTPStatus status = GNOME_Evolution_Calendar_Success;
	E2kResult *result;
	E2kResultIter *iter;
	ECalComponentDateTime dt;
	E2kContext *ctx;
	icaltimezone *izone;
	guint32 access = 0;
	time_t tt;
	const char *uid, *prop_name = PR_ACCESS;
	const char *access_prop = NULL, *meeting_prop = NULL, *cal_uid = NULL;
	gboolean bookable;
	char *top_uri = NULL, *cal_uri = NULL, *returned_uid = NULL, *rid = NULL;
	char *startz, *endz, *href = NULL, *old_object = NULL, *calobj = NULL;
	E2kRestriction *rn;
	int nresult;
	ECalBackendExchangeBookingResult retval = E_CAL_BACKEND_EXCHANGE_BOOKING_ERROR;
	const char *localfreebusy_path = "NON_IPM_SUBTREE/Freebusy%20Data/LocalFreebusy.EML";
												 
	g_object_ref (comp);
												 
	/* Look up the resource's mailbox */
	gc = exchange_account_get_global_catalog (cbex->account);
	if (!gc)
		goto cleanup;
												 
	gcstatus = e2k_global_catalog_lookup (
		gc, NULL, E2K_GLOBAL_CATALOG_LOOKUP_BY_EMAIL, resource_email,
		E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX, &entry);

	switch (gcstatus) {
		case E2K_GLOBAL_CATALOG_OK:
			break;
												 
		case E2K_GLOBAL_CATALOG_NO_SUCH_USER:
			retval = E_CAL_BACKEND_EXCHANGE_BOOKING_NO_SUCH_USER;
			goto cleanup;
												 
		default:
			retval = E_CAL_BACKEND_EXCHANGE_BOOKING_ERROR;
			goto cleanup;
	}
												 
	top_uri = exchange_account_get_foreign_uri (cbex->account,
						    entry, NULL);
	cal_uri = exchange_account_get_foreign_uri (cbex->account, entry,
						    E2K_PR_STD_FOLDER_CALENDAR);
	e2k_global_catalog_entry_free (gc, entry);
	if (!top_uri || !cal_uri) {
		retval = E_CAL_BACKEND_EXCHANGE_BOOKING_PERMISSION_DENIED;
		goto cleanup;
	}
	
	ctx = exchange_account_get_context (cbex->account);
	status = e2k_context_propfind (ctx, NULL, cal_uri,
					&prop_name, 1,
					&result, &nresult);
	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status) && nresult >= 1) {
		access_prop = e2k_properties_get_prop (result[0].props, PR_ACCESS);
		if (access_prop)
			access = atoi (access_prop);
	}
	e2k_results_free (result, nresult);

	if (!(access & MAPI_ACCESS_CREATE_CONTENTS)) {
		retval = E_CAL_BACKEND_EXCHANGE_BOOKING_PERMISSION_DENIED;
		goto cleanup;
	}
												 
	prop_name = PR_PROCESS_MEETING_REQUESTS;
	iter = e2k_context_bpropfind_start (ctx, NULL, top_uri,
						&localfreebusy_path, 1,
						&prop_name, 1);
	result = e2k_result_iter_next (iter);
	if (result && E2K_HTTP_STATUS_IS_SUCCESSFUL (result->status))  {
		meeting_prop = e2k_properties_get_prop (result[0].props, PR_PROCESS_MEETING_REQUESTS);
	}
	if (meeting_prop)
		bookable = atoi (meeting_prop);
	else
		bookable = FALSE;
	status = e2k_result_iter_free (iter);

	if ((!E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) || (!bookable)) {
		retval = E_CAL_BACKEND_EXCHANGE_BOOKING_PERMISSION_DENIED;
		goto cleanup;
	}

	e_cal_component_get_uid (E_CAL_COMPONENT (comp), &uid);
	href = g_strdup_printf ("%s/%s.EML", cal_uri, uid);
	ecomp = get_exchange_comp (E_CAL_BACKEND_EXCHANGE (cbex), uid);
												 
	if (method == ICAL_METHOD_CANCEL) {
		char *object = NULL;
		
		/* g_object_unref (comp); */
		/* If there is nothing to cancel, we're good */
		if (!ecomp) {
			retval = E_CAL_BACKEND_EXCHANGE_BOOKING_OK;
			goto cleanup;
		}
		
		/* Mark the cancellation properly in the resource's calendar */
		
		/* Mark the item as cancelled */
		e_cal_component_get_summary (E_CAL_COMPONENT (comp), &old_text);
		if (old_text.value)
			new_text.value = g_strdup_printf ("Cancelled: %s", old_text.value);
		else
			new_text.value = g_strdup_printf ("Cancelled");
		new_text.altrep = NULL;
		e_cal_component_set_summary (E_CAL_COMPONENT (comp), &new_text);

		e_cal_component_set_transparency (E_CAL_COMPONENT (comp), E_CAL_COMPONENT_TRANSP_TRANSPARENT);
		calobj = (char *) e_cal_component_get_as_string (comp);
		rid = (char *) e_cal_component_get_recurid_as_string (comp);
		status = remove_object (E_CAL_BACKEND_SYNC (cbex), cal, uid, rid, CALOBJ_MOD_THIS, &calobj, &object);
		e_cal_backend_notify_object_removed (E_CAL_BACKEND (cbex), uid, calobj, NULL);
		g_free (calobj); 
		g_free (object);
	} else {
		/* Check that the new appointment doesn't conflict with any
		 * existing appointment.
		 */
		e_cal_component_get_dtstart (E_CAL_COMPONENT (comp), &dt);
		izone = e_cal_backend_internal_get_timezone (E_CAL_BACKEND (cbex), dt.tzid);
		tt = icaltime_as_timet_with_zone (*dt.value, izone);
		e_cal_component_free_datetime (&dt);
		startz = e2k_make_timestamp (tt);
												 
		e_cal_component_get_dtend (E_CAL_COMPONENT (comp), &dt);
		izone = e_cal_backend_internal_get_timezone (E_CAL_BACKEND (cbex), dt.tzid);
		tt = icaltime_as_timet_with_zone (*dt.value, izone);
		e_cal_component_free_datetime (&dt);
		endz = e2k_make_timestamp (tt);
												 
		prop_name = E2K_PR_CALENDAR_UID;
		rn = e2k_restriction_andv (
			e2k_restriction_prop_bool (
				E2K_PR_DAV_IS_COLLECTION, E2K_RELOP_EQ, FALSE),
			e2k_restriction_prop_string (
				E2K_PR_DAV_CONTENT_CLASS, E2K_RELOP_EQ,
				"urn:content-classes:appointment"),
			e2k_restriction_prop_string (
				E2K_PR_CALENDAR_UID, E2K_RELOP_NE, uid),
			e2k_restriction_prop_date (
				E2K_PR_CALENDAR_DTEND, E2K_RELOP_GT, startz),
			e2k_restriction_prop_date (
				E2K_PR_CALENDAR_DTSTART, E2K_RELOP_LT, endz),
			e2k_restriction_prop_string (
				E2K_PR_CALENDAR_BUSY_STATUS, E2K_RELOP_NE, "FREE"),
			NULL);
												 
		iter = e2k_context_search_start (ctx, NULL, cal_uri,
						     &prop_name, 1, rn, NULL, FALSE);
		g_free (startz);
		g_free (endz);

		result = e2k_result_iter_next (iter);
		if (result) {
			cal_uid = e2k_properties_get_prop (result[0].props, E2K_PR_CALENDAR_UID);
		}
		if (result && cal_uid) {
			retval = E_CAL_BACKEND_EXCHANGE_BOOKING_BUSY;

			status = e2k_result_iter_free (iter);

			if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
				if (status == E2K_HTTP_UNAUTHORIZED) 
					retval = E_CAL_BACKEND_EXCHANGE_BOOKING_PERMISSION_DENIED;
				else 
					retval = E_CAL_BACKEND_EXCHANGE_BOOKING_ERROR;
			}
			e2k_restriction_unref (rn);
			goto cleanup;
		}
		e2k_restriction_unref (rn);
	}
												 
	/* We're good. Book it. */
	/* e_cal_component_set_href (comp, href); */
	e_cal_component_commit_sequence (comp);
	calobj = (char *) e_cal_component_get_as_string (comp);
												 
	/* status = e_cal_component_update (comp, method, FALSE  ); */
	if (ecomp) {
		/* This object is already present in the cache so update it. */
		status = modify_object_with_href (E_CAL_BACKEND_SYNC (cbex), cal, calobj, CALOBJ_MOD_THIS, &old_object, href);
		if (status == GNOME_Evolution_Calendar_Success) {
			e_cal_backend_notify_object_modified (E_CAL_BACKEND (cbex), old_object, calobj);
			retval = E_CAL_BACKEND_EXCHANGE_BOOKING_OK;
		}
		g_free (old_object);
	} else {
		status = create_object (E_CAL_BACKEND_SYNC (cbex), cal, &calobj, &returned_uid);
		if (status == GNOME_Evolution_Calendar_Success) {
			e_cal_backend_notify_object_created (E_CAL_BACKEND (cbex), calobj);
			retval = E_CAL_BACKEND_EXCHANGE_BOOKING_OK;
		}
	}

 cleanup:
	g_object_unref (comp);
	if (href)
		g_free (href);
	if (cal_uri)
		g_free (cal_uri);
	if (top_uri)
		g_free (top_uri);
	g_free (calobj);
					
	return retval;
}

static ECalBackendSyncStatus
send_objects (ECalBackendSync *backend, EDataCal *cal,
	      const char *calobj,
	      GList **users, char **modified_calobj)
{
	ECalBackendExchange *cbex = (ECalBackendExchange *) backend;
	ECalBackendSyncStatus retval = GNOME_Evolution_Calendar_Success;
	ECalBackendExchangeBookingResult result;
	ECalComponent *comp;
	icalcomponent *top_level, *icalcomp, *tzcomp;
	icalproperty *prop;
	icalproperty_method method;
												 
	g_return_val_if_fail (E_IS_CAL_BACKEND_EXCHANGE (cbex), 
				GNOME_Evolution_Calendar_InvalidObject);

	if (!e_cal_backend_exchange_is_online (E_CAL_BACKEND_EXCHANGE (cbex))) { 
		return GNOME_Evolution_Calendar_InvalidObject;
	}
												 
	*users = NULL;
	*modified_calobj = NULL;

	top_level = icalparser_parse_string (calobj);
	icalcomp = icalcomponent_get_inner (top_level);
												 
	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (E_CAL_COMPONENT (comp),
					 icalcomponent_new_clone (icalcomp));
												 
	method = icalcomponent_get_method (top_level);
	if (icalcomponent_isa (icalcomp) != ICAL_VEVENT_COMPONENT
	    || (method != ICAL_METHOD_REQUEST && method != ICAL_METHOD_CANCEL)) {
		*modified_calobj = g_strdup (calobj);
		retval = GNOME_Evolution_Calendar_Success;
		goto cleanup;
	}

	/* traverse all timezones to add them to the backend */
	tzcomp = icalcomponent_get_first_component (top_level,
						    ICAL_VTIMEZONE_COMPONENT);
	while (tzcomp) {
		e_cal_backend_exchange_add_timezone (cbex, tzcomp);
		tzcomp = icalcomponent_get_next_component (top_level,
							   ICAL_VTIMEZONE_COMPONENT);
	}
												 
	for (prop = icalcomponent_get_first_property (icalcomp, ICAL_ATTENDEE_PROPERTY);
	     prop != NULL;
	     prop = icalcomponent_get_next_property (icalcomp, ICAL_ATTENDEE_PROPERTY))
	{
		icalvalue *value;
		icalparameter *param;
		const char *attendee;
												 
		param = icalproperty_get_first_parameter (prop, ICAL_CUTYPE_PARAMETER);
		if (!param)
			continue;
		if (icalparameter_get_cutype (param) != ICAL_CUTYPE_RESOURCE)
			continue;
												 
		value = icalproperty_get_value (prop);
		if (!value)
			continue;
												 
		attendee = icalvalue_get_string (value);
		if (g_ascii_strncasecmp ("mailto:", attendee, 7))
			continue;
												 
		/* See if it recurs */
		if (icalcomponent_get_first_property (icalcomp, ICAL_RRULE_PROPERTY)
		    || icalcomponent_get_first_property (icalcomp, ICAL_RDATE_PROPERTY)) {
#if 0
			g_snprintf (error_msg, 256,
				_("Unable to schedule resource '%s' for recurring meetings.\n"
				      "You must book each meeting separately."),
				    attendee + 7);
#endif												 
			retval = GNOME_Evolution_Calendar_ObjectIdAlreadyExists;
			goto cleanup;
		}
												 
		result = book_resource (cbex, cal, attendee + 7, comp, method);
		switch (result) {
		case E_CAL_BACKEND_EXCHANGE_BOOKING_OK:
			param = icalproperty_get_first_parameter (prop, ICAL_PARTSTAT_PARAMETER);
			icalparameter_set_partstat (param, ICAL_PARTSTAT_ACCEPTED);
			*users = g_list_append (*users, g_strdup (attendee)) ;
			break;

		case E_CAL_BACKEND_EXCHANGE_BOOKING_BUSY:
#if 0
			g_snprintf (error_msg, 256,
				    _("The resource '%s' is busy during the selected time period."),
				    attendee + 7);
#endif							
			retval = GNOME_Evolution_Calendar_ObjectIdAlreadyExists;
			goto cleanup;
												 
		case E_CAL_BACKEND_EXCHANGE_BOOKING_PERMISSION_DENIED:
		case E_CAL_BACKEND_EXCHANGE_BOOKING_NO_SUCH_USER:
			/* Do nothing, we fallback to iMip */
			break;
		case E_CAL_BACKEND_EXCHANGE_BOOKING_ERROR:
			/* What should we do here? */
			retval = GNOME_Evolution_Calendar_PermissionDenied;
			goto cleanup;
		}
	}
												 
	retval = GNOME_Evolution_Calendar_Success;
	*modified_calobj = g_strdup (e_cal_component_get_as_string (comp));
												 
 cleanup:
	icalcomponent_free (top_level);
	g_object_unref (comp);
												 
	return retval;
}

#define THIRTY_MINUTES (30 * 60)
#define E2K_FBCHAR_TO_BUSYSTATUS(ch) ((ch) - '0')

static icalproperty *
create_freebusy_prop (E2kBusyStatus fbstatus, time_t start, time_t end)
{
	icaltimezone *utc = icaltimezone_get_utc_timezone ();
	icalproperty *prop;
	icalparameter *param;
	struct icalperiodtype ipt;

	switch (fbstatus) {
	case E2K_BUSYSTATUS_FREE:
		param = icalparameter_new_fbtype (ICAL_FBTYPE_FREE);
		break;
	case E2K_BUSYSTATUS_TENTATIVE:
		param = icalparameter_new_fbtype (ICAL_FBTYPE_BUSYTENTATIVE);
		break;
	case E2K_BUSYSTATUS_BUSY:
		param = icalparameter_new_fbtype (ICAL_FBTYPE_BUSY);
		break;
	case E2K_BUSYSTATUS_OOF:
		param = icalparameter_new_fbtype (ICAL_FBTYPE_BUSYUNAVAILABLE);
		break;
	default:
		return NULL;
	}

	ipt.start = icaltime_from_timet_with_zone (start, 0, utc);
	ipt.end = icaltime_from_timet_with_zone (end, 0, utc);
	prop = icalproperty_new_freebusy (ipt);
	icalproperty_add_parameter (prop, param);

	return prop;
}

static void
set_freebusy_info (icalcomponent *vfb, const char *data, time_t start)
{
	const char *span_start, *span_end;
	E2kBusyStatus busy;
	icalproperty *prop;
	time_t end;

	for (span_start = span_end = data, end = start;
	     *span_start;
	     span_start = span_end, start = end) {
		busy = E2K_FBCHAR_TO_BUSYSTATUS (*span_start);
		while (*span_end == *span_start) {
			span_end++;
			end += THIRTY_MINUTES;
		}

		prop = create_freebusy_prop (busy, start, end);
		if (prop)
			icalcomponent_add_property (vfb, prop);
	}
}

static ECalBackendSyncStatus
discard_alarm (ECalBackendSync *backend, EDataCal *cal,
		const char *uid, const char *auid)
{
	ECalBackendSyncStatus result = GNOME_Evolution_Calendar_Success;
	ECalBackendExchange *cbex = NULL;
	ECalBackendExchangeComponent *ecbexcomp;
	ECalComponent *ecomp;
	char *ecomp_str;
	icalcomponent *icalcomp;

	g_return_val_if_fail (E_IS_CAL_BACKEND_EXCHANGE_CALENDAR (backend),
					GNOME_Evolution_Calendar_InvalidObject);
	g_return_val_if_fail (E_IS_DATA_CAL (cal),
					GNOME_Evolution_Calendar_InvalidObject);
	g_return_val_if_fail (uid != NULL, GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (auid != NULL, GNOME_Evolution_Calendar_OtherError);

	d(printf("ecbe_discard_alarm(%p, %p, uid=%s, auid=%s)\n", backend, cal, uid, auid));

	if (!e_cal_backend_exchange_is_online (E_CAL_BACKEND_EXCHANGE (backend))) {
                /* FIXME */
                return GNOME_Evolution_Calendar_InvalidObject;
        }

	cbex = E_CAL_BACKEND_EXCHANGE (backend);

	ecbexcomp = get_exchange_comp (cbex, uid);

	if (!ecbexcomp) 
		return GNOME_Evolution_Calendar_ObjectNotFound;

	ecomp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (ecomp, icalcomponent_new_clone (ecbexcomp->icomp));
	if (!e_cal_component_has_recurrences (ecomp))
	{
		e_cal_component_remove_alarm (ecomp, auid);
		ecomp_str = e_cal_component_get_as_string (ecomp);
		icalcomp = icalparser_parse_string (ecomp_str);
		if (!e_cal_backend_exchange_modify_object ( cbex,
					icalcomp, CALOBJ_MOD_ALL)) {
			result = GNOME_Evolution_Calendar_OtherError;
		}
		g_free (ecomp_str);
	}
	g_object_unref (ecomp);

	return result;
}

static ECalBackendSyncStatus
get_free_busy (ECalBackendSync *backend, EDataCal *cal,
	       GList *users, time_t start, time_t end,
	       GList **freebusy)
{
	ECalBackendExchange *cbex = E_CAL_BACKEND_EXCHANGE (backend);
	char *start_str, *end_str;
	GList *l;
	GString *uri;
	char *body;
	int len;
	E2kHTTPStatus http_status;
	icaltimezone *utc = icaltimezone_get_utc_timezone ();
	xmlNode *recipients, *item;
	xmlDoc *doc;

	if (!e_cal_backend_exchange_is_online (E_CAL_BACKEND_EXCHANGE (backend))) {
                /* FIXME */
                return GNOME_Evolution_Calendar_InvalidObject;
        }

	/* The calendar component sets start to "exactly 24 hours
	 * ago". But since we're going to get the information in
	 * 30-minute intervals starting from "start", we want to round
	 * off to the nearest half hour.
	 */
	start = (start / THIRTY_MINUTES) * THIRTY_MINUTES;

	start_str = e2k_make_timestamp (start);
	end_str   = e2k_make_timestamp (end);

	uri = g_string_new (cbex->account->public_uri);
	g_string_append (uri, "/?Cmd=freebusy&start=");
	g_string_append (uri, start_str);
	g_string_append (uri, "&end=");
	g_string_append (uri, end_str);
	g_string_append (uri, "&interval=30");
	for (l = users; l; l = l->next) {
		g_string_append (uri, "&u=SMTP:");
		g_string_append (uri, (char *) l->data);
	}
	g_free (start_str);
	g_free (end_str);

	http_status = e2k_context_get_owa (exchange_account_get_context (cbex->account),
					   NULL, uri->str, TRUE, &body, &len);
	g_string_free (uri, TRUE);
	if (http_status != E2K_HTTP_OK)
		return GNOME_Evolution_Calendar_OtherError;

	/* Parse the XML free/busy response */
	doc = e2k_parse_xml (body, len);
	g_free (body);
	if (!doc)
		return GNOME_Evolution_Calendar_OtherError;

	recipients = e2k_xml_find (doc->children, "recipients");
	if (!recipients) {
		xmlFreeDoc (doc);
		return GNOME_Evolution_Calendar_OtherError;
	}

	*freebusy = NULL;
	for (item = e2k_xml_find_in (recipients, recipients, "item");
	     item;
	     item = e2k_xml_find_in (item, recipients, "item")) {
		icalcomponent *vfb;			
		icalproperty *organizer;
		xmlNode *node, *fbdata;
		char *org_uri, *calobj;

		fbdata = e2k_xml_find_in (item, item, "fbdata");
		if (!fbdata || !fbdata->children || !fbdata->children->content)
			continue;

		node = e2k_xml_find_in (item, item, "email");
		if (!node || !node->children || !node->children->content)
			continue;
		org_uri = g_strdup_printf ("MAILTO:%s", node->children->content);
		organizer = icalproperty_new_organizer (org_uri);
		g_free (org_uri);

		node = e2k_xml_find_in (item, item, "displayname");
		if (node && node->children && node->children->content) {
			icalparameter *cn;

			cn = icalparameter_new_cn (node->children->content);
			icalproperty_add_parameter (organizer, cn);
		}

		vfb = icalcomponent_new_vfreebusy ();
		icalcomponent_set_dtstart (vfb, icaltime_from_timet_with_zone (start, 0, utc));
		icalcomponent_set_dtend (vfb, icaltime_from_timet_with_zone (end, 0, utc));
		icalcomponent_add_property (vfb, organizer);

		set_freebusy_info (vfb, fbdata->children->content, start);

		calobj = icalcomponent_as_ical_string (vfb);
		*freebusy = g_list_prepend (*freebusy, g_strdup (calobj));
		icalcomponent_free (vfb);
	}
	xmlFreeDoc (doc);

	return GNOME_Evolution_Calendar_Success;
}

static void
init (ECalBackendExchangeCalendar *cbexc)
{
	cbexc->priv = g_new0 (ECalBackendExchangeCalendarPrivate, 1);
}

static void
dispose (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	ECalBackendExchangeCalendar *cbexc =
		E_CAL_BACKEND_EXCHANGE_CALENDAR (object);

	g_free (cbexc->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
class_init (ECalBackendExchangeCalendarClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ECalBackendSyncClass *sync_class = E_CAL_BACKEND_SYNC_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	sync_class->open_sync = open_calendar;
	sync_class->create_object_sync = create_object;
	sync_class->modify_object_sync = modify_object;
	sync_class->remove_object_sync = remove_object;
	sync_class->receive_objects_sync = receive_objects;
	sync_class->send_objects_sync = send_objects;
 	sync_class->get_freebusy_sync = get_free_busy;
	sync_class->discard_alarm_sync = discard_alarm;

	object_class->dispose = dispose;
	object_class->finalize = finalize;
}

E2K_MAKE_TYPE (e_cal_backend_exchange_calendar, ECalBackendExchangeCalendar, class_init, init, PARENT_TYPE)
