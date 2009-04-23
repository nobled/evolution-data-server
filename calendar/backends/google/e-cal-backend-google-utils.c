/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  Ebby Wiselyn <ebbyw@gnome.org>
 *  Philip Withnall <philip@tecnocode.co.uk>
 *
 * Copyright (C) 1999-2009 Novell, Inc. (www.novell.com)
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
 * * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>
#include <glib/gprintf.h>

#include <libedataserver/e-data-server-util.h>
#include <libedataserver/e-xml-hash-utils.h>

#include <libedata-cal/e-cal-backend-cache.h>
#include <libedata-cal/e-cal-backend-util.h>
#include <libedata-cal/e-cal-backend-sexp.h>

#include <libecal/e-cal-recur.h>
#include <libecal/e-cal-time-util.h>
#include <libecal/e-cal-util.h>
#include <libecal/e-cal-component.h>
#include <libecal/e-cal-time-util.h>


#include <libical/ical.h>
#include <libsoup/soup-misc.h>

#include "e-cal-backend-google.h"
#include "e-cal-backend-google-utils.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define CACHE_REFRESH_INTERVAL 10000

/****************************************************** Google Connection Helper Functions ***********************************************/

static gboolean gd_timeval_to_ical (EGoItem *item, GTimeVal *timeval, struct icaltimetype *itt, ECalComponentDateTime *dt, icaltimezone *default_zone);
static void get_timeval (ECalComponentDateTime dt, GTimeVal *timeval);
static gint utils_compare_ids (gconstpointer cache_id, gconstpointer modified_cache_id);
static gchar * utils_form_query (const gchar *query);
static gboolean get_deltas_timeout (gpointer cbgo);
static void utils_update_insertion (ECalBackendGoogle *cbgo, ECalBackendCache *cache, EGoItem *item, GSList *cache_keys);
static void utils_update_deletion (ECalBackendGoogle *cbgo, ECalBackendCache *cache, GSList *cache_keys);

/**
 *
 * e_cal_backend_google_utils_populate_cache:
 * @cbgo ECalBackendGoogle Object
 * Populates the cache with intial values
 *
 **/
static void
e_cal_backend_google_utils_populate_cache (ECalBackendGoogle *cbgo)
{
	ECalComponent *comp=NULL;
	ECalBackendCache *cache;
	EGoItem *item;
	ECalBackendGooglePrivate *priv;
	icalcomponent_kind kind;
	icaltimetype temp;
	GList *entries = NULL, *list = NULL;

	cache = e_cal_backend_google_get_cache (cbgo);
	kind = e_cal_backend_get_kind (E_CAL_BACKEND(cbgo));
	temp = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());

 	item = e_cal_backend_google_get_item (cbgo);
	entries = gdata_feed_get_entries (item->feed);
	priv = cbgo->priv;

	for (list = entries; list != NULL; list = list->next) {
		item->entry = GDATA_ENTRY(list->data);
		comp = e_go_item_to_cal_component (item, cbgo);
		if (comp && E_IS_CAL_COMPONENT(comp)) {
			gchar *comp_str;
			e_cal_component_commit_sequence (comp);
			comp_str = e_cal_component_get_as_string (comp);

			e_cal_backend_notify_object_created (E_CAL_BACKEND(cbgo), (const char *)comp_str);
			e_cal_backend_cache_put_component (cache, comp);
			g_object_unref (comp);
			g_free (comp_str);
		}
	}

	e_cal_backend_notify_view_done (E_CAL_BACKEND(cbgo), GNOME_Evolution_Calendar_Success);
}


/**
 *
 * e_cal_backend_google_utils_create_cache:
 * @cbgo: ECalBackendGoogle
 * Creates / Updates Cache
 *
 **/
static gpointer
e_cal_backend_google_utils_create_cache (ECalBackendGoogle *cbgo)
{
	ESource *source;
	int x;
	const gchar *refresh_interval = NULL;
	ECalBackendCache *cache;

	source = e_cal_backend_get_source (E_CAL_BACKEND (cbgo));
	refresh_interval = e_source_get_property (source, "refresh");

	cache = e_cal_backend_google_get_cache (cbgo);
	if (e_cal_backend_cache_get_marker (cache)) {
		e_cal_backend_google_utils_populate_cache (cbgo);
		e_cal_backend_cache_set_marker (cache);
	} else
		get_deltas_timeout (cbgo);

	if (refresh_interval)
		x = atoi (refresh_interval);
	else
		x = 30;

	if (!e_cal_backend_google_get_timeout_id (cbgo)) {
		guint timeout_id;

		timeout_id = g_timeout_add (x * 60000,
					  (GSourceFunc) get_deltas_timeout,
					  (gpointer)cbgo);
		e_cal_backend_google_set_timeout_id (cbgo, timeout_id);
	}

	return GINT_TO_POINTER (GNOME_Evolution_Calendar_Success);
}


/**
 * e_cal_backend_google_utils_update:
 *
 * @handle:
 * Call this to update changes made to the calendar.
 *
 * Return value: %TRUE if update is successful, %FALSE otherwise.
 **/
gpointer
e_cal_backend_google_utils_update (gpointer handle)
{
	ECalBackendGoogle *cbgo;
	ECalBackendGooglePrivate *priv;
	EGoItem *item;

	ECalBackendCache *cache;

	GDataService *service;
	static GStaticMutex updating = G_STATIC_MUTEX_INIT;
	icalcomponent_kind kind;

	GList *entries_list = NULL, *iter_list = NULL, *ids_list = NULL;
	GSList *uid_list = NULL, *remove = NULL, *cache_keys = NULL;
	gboolean needs_to_insert = FALSE;
	gchar *uri;

	if (!handle || !E_IS_CAL_BACKEND_GOOGLE (handle)) {
		g_critical ("\n Invalid handle %s", G_STRLOC);
		return NULL;
	}

	g_static_mutex_lock (&updating);

	cbgo = (ECalBackendGoogle *)handle;
	priv = cbgo->priv;

	cache = e_cal_backend_google_get_cache (cbgo);
	item =  e_cal_backend_google_get_item (cbgo);
	service = GDATA_SERVICE (e_cal_backend_google_get_service (cbgo));
	uri = e_cal_backend_google_get_uri (cbgo);

	item->feed = gdata_service_query (GDATA_SERVICE(service), uri, NULL, GDATA_TYPE_CALENDAR_EVENT, NULL, NULL, NULL, NULL);
	entries_list = gdata_feed_get_entries (item->feed);
	cache_keys = e_cal_backend_cache_get_keys (cache);
	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbgo));

	for (iter_list = entries_list; iter_list != NULL; iter_list = iter_list->next) {
		const gchar *id;
		id = gdata_entry_get_id (GDATA_ENTRY(iter_list->data));
		ids_list = g_list_prepend (ids_list, (gchar*) id);
	}

	/* Find the Removed Item */
	iter_list = NULL;
	for (iter_list = ids_list; iter_list != NULL; iter_list = iter_list->next) {
		GCompareFunc func = NULL;
		GSList *remove = NULL;

		func = (GCompareFunc)utils_compare_ids;

		if (!(remove = g_slist_find_custom (cache_keys, iter_list->data, func))) {
			uid_list = g_slist_prepend (uid_list, g_strdup ((gchar *)iter_list->data));
			needs_to_insert = TRUE;
		}else {
			cache_keys = g_slist_remove_link (cache_keys, remove);
		}

		if (remove)
			g_slist_free (remove);
	}

	/* Update the deleted entries */
	utils_update_deletion (cbgo, cache, cache_keys);

	/* Update the inserted entries */
	if (needs_to_insert) {
		utils_update_insertion (cbgo, cache, item, uid_list);
		needs_to_insert = FALSE;
	}

	if (ids_list) {
		g_list_free (ids_list);
	}

	if (uid_list) {
		/*FIXME could crash while freeing*/
		g_slist_free (uid_list);
	}

	if (entries_list) {
		/* FIXME could crash while freeing */
		g_list_free (entries_list);
	}

	if (remove) {
		g_slist_free (remove);
	}

	g_static_mutex_unlock (&updating);
	return NULL;
}

ECalBackendSyncStatus
e_cal_backend_google_utils_connect (ECalBackendGoogle *cbgo)
{
	ECalBackendCache *cache;
	EGoItem *item;
	ESource *source;
	GDataFeed *feed;
	GDataCalendarService *service;

	ECalSourceType source_type;
	icalcomponent_kind kind;
	icaltimezone *default_zone;
	GError *error = NULL;
	GThread *thread;
	gchar *username, *password;
	guint timeout_id;
	gboolean mode_changed;
	gchar *uri, *suri;

	source = e_cal_backend_get_source (E_CAL_BACKEND(cbgo));

	service = gdata_calendar_service_new ("evolution-client-0.0.2");
	e_cal_backend_google_set_service (cbgo, service);

	suri = e_source_get_uri (source);
	uri = utils_form_query (suri);
	e_cal_backend_google_set_uri (cbgo, uri);

	g_free (suri);

	username = e_cal_backend_google_get_username (cbgo);
	password = e_cal_backend_google_get_password (cbgo);
	if (!gdata_service_authenticate (GDATA_SERVICE(service), username, password, NULL, NULL)) {
		g_critical ("%s, Authentication Failed \n ", G_STRLOC);
		if (username || password)
			return GNOME_Evolution_Calendar_AuthenticationFailed;
		return GNOME_Evolution_Calendar_AuthenticationRequired;
	}

	feed = gdata_service_query (GDATA_SERVICE(service), uri, NULL, GDATA_TYPE_CALENDAR_EVENT, NULL, NULL, NULL, NULL);

	item = g_new0 (EGoItem, 1);
	item->entry = e_cal_backend_google_get_entry (cbgo);
	item->feed = feed;

	cache = e_cal_backend_google_get_cache (cbgo);
	service = e_cal_backend_google_get_service (cbgo);

	e_cal_backend_google_set_item (cbgo, item);

	/* For event sync */
	if (cache && service) {

		/* FIXME Get the current mode */
		mode_changed = FALSE;
		timeout_id = e_cal_backend_google_get_timeout_id (cbgo);

		if (!mode_changed && !timeout_id) {
			GThread *t1;

			/*FIXME Set the mode to be changed */
			t1 = g_thread_create ((GThreadFunc)e_cal_backend_google_utils_update, cbgo, FALSE, NULL);
			if (!t1) {
				e_cal_backend_notify_error (E_CAL_BACKEND (cbgo), _("Could not create thread for getting deltas"));
				return GNOME_Evolution_Calendar_OtherError;
			}

			timeout_id = g_timeout_add (CACHE_REFRESH_INTERVAL, (GSourceFunc) get_deltas_timeout, (gpointer)cbgo);
			e_cal_backend_google_set_timeout_id (cbgo, timeout_id);
		}

		return GNOME_Evolution_Calendar_Success;
	}
	/* FIXME Set the mode to be changed */
	kind = e_cal_backend_get_kind (E_CAL_BACKEND(cbgo));
	switch (kind) {
		case ICAL_VEVENT_COMPONENT:
			source_type = E_CAL_SOURCE_TYPE_EVENT;
			break;
		case ICAL_VTODO_COMPONENT:
			source_type = E_CAL_SOURCE_TYPE_TODO;
			break;
		case ICAL_VJOURNAL_COMPONENT:
			source_type = E_CAL_SOURCE_TYPE_JOURNAL;
			break;
		default:
			source_type = E_CAL_SOURCE_TYPE_EVENT;
	}

	/* Creating cache when in remote  */
	if (GDATA_IS_CALENDAR_SERVICE (service)) {
		cache = e_cal_backend_cache_new (e_cal_backend_get_uri (E_CAL_BACKEND (cbgo)),source_type);
		e_cal_backend_google_set_cache (cbgo, cache);
	}
	if (!cache) {
		e_cal_backend_notify_error (E_CAL_BACKEND(cbgo), _("Could not create cache file"));
		return GNOME_Evolution_Calendar_OtherError;
	}

	default_zone = e_cal_backend_google_get_default_zone (cbgo);
	e_cal_backend_cache_put_default_timezone (cache, default_zone);
	e_cal_backend_google_utils_create_cache (cbgo);
	thread = g_thread_create ((GThreadFunc)e_cal_backend_google_utils_create_cache, (gpointer) cbgo, FALSE, &error);

	if (!thread) {
		g_warning (G_STRLOC ": %s", error->message);
		g_error_free (error);

		e_cal_backend_notify_error (E_CAL_BACKEND (cbgo), _("Could not create thread for populating cache"));
		return GNOME_Evolution_Calendar_OtherError;
	}

	return GNOME_Evolution_Calendar_Success;

}

/*************************************************** EGoItem Functions*********************************************/

/**
 * e_go_item_to_cal_component:
 * @item: an #EGoItem
 * @cbgo: an #ECalBackendGoogle
 *
 * Creates an #EGoItem from an #ECalComponent
 **/

ECalComponent *
e_go_item_to_cal_component (EGoItem *item, ECalBackendGoogle *cbgo)
{
	ECalComponent *comp;
	ECalComponentText text;
	ECalComponentDateTime dt;
	ECalComponentOrganizer *org = NULL;
	icaltimezone *default_zone;
	const char *description, *uid, *temp, *location = NULL;
	GTimeVal timeval, timeval2;
	struct icaltimetype itt;
	GList *category_ids;
	GList *go_attendee_list = NULL, *go_location_list = NULL, *l = NULL;
	GSList *attendee_list = NULL;

	comp = e_cal_component_new ();
	default_zone = e_cal_backend_google_get_default_zone (cbgo);

	if (!default_zone)
		g_message("Critical Default zone not set %s", G_STRLOC);

	e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);

	/* Description*/
	description = gdata_entry_get_content (item->entry);
	if (description) {
		GSList l;
		text.value = description;
		text.altrep = NULL;
		l.data = &text;
		l.next = NULL;
		e_cal_component_set_description_list (comp, &l);
	}

	/* Creation/Last update */
	gdata_entry_get_published (item->entry, &timeval);
	if (gd_timeval_to_ical (item, &timeval, &itt, &dt, default_zone))
		e_cal_component_set_created (comp, &itt);

	gdata_entry_get_updated (item->entry, &timeval);
	if (gd_timeval_to_ical (item, &timeval, &itt, &dt, default_zone))
		e_cal_component_set_dtstamp (comp, &itt);

	/* Start/End times */
	/* TODO: deal with multiple time periods */
	gdata_calendar_event_get_primary_time (GDATA_CALENDAR_EVENT(item->entry), &timeval, &timeval2, NULL);
	if (gd_timeval_to_ical (item, &timeval, &itt, &dt, default_zone))
		e_cal_component_set_dtstart (comp, &dt);
	if (gd_timeval_to_ical (item, &timeval2, &itt, &dt, default_zone))
		e_cal_component_set_dtend (comp, &dt);

	/* Summary of the Entry */
	text.value = gdata_entry_get_title (item->entry);
	text.altrep = NULL;
	if (text.value != NULL)
		e_cal_component_set_summary (comp, &text);

	/* Categories or Kinds */
	category_ids = NULL;
	category_ids = gdata_entry_get_categories (item->entry);

	uid = gdata_entry_get_id (item->entry);

	/* Classification or Visibility */
	temp = NULL;
	temp = gdata_calendar_event_get_visibility (GDATA_CALENDAR_EVENT(item->entry));

	if (strcmp (temp, "public") == 0)
		e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_PUBLIC);
	else
		e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_NONE);

	/* Specific properties */
	temp = NULL;

	/* Transparency */
	temp = gdata_calendar_event_get_transparency (GDATA_CALENDAR_EVENT(item->entry));
	if (strcmp (temp, "http://schemas.google.com/g/2005#event.opaque") == 0)
		e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_OPAQUE);
	else if (strcmp (temp, "http://schemas.google.com/g/2005#event.transparent") == 0)
		e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_TRANSPARENT);
	else
		e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_UNKNOWN);

	/* Attendees */
	go_attendee_list = gdata_calendar_event_get_people (GDATA_CALENDAR_EVENT(item->entry));

	for (l = go_attendee_list; l != NULL; l = l->next) {
		GDataGDWho *go_attendee;
		ECalComponentAttendee *attendee;

		go_attendee = (GDataGDWho *)l->data;

		attendee = g_new0 (ECalComponentAttendee, 1);
#if 0
		_print_attendee ((Attendee *)l->data);
#endif
		attendee->value = g_strconcat ("MAILTO:", go_attendee->email, NULL);
		attendee->cn = g_strdup (go_attendee->value_string);
		/* TODO: This could be made less static once the GData API's in place */
		attendee->role = ICAL_ROLE_OPTPARTICIPANT;
		attendee->status = ICAL_PARTSTAT_NEEDSACTION;
		attendee->cutype =  ICAL_CUTYPE_INDIVIDUAL;

		/* Check for Organizer */
		if (go_attendee->rel) {
			gchar *val;
			val = strstr ((const gchar *)go_attendee->rel, (const gchar *)"organizer");
			if (val != NULL && !strcmp ("organizer", val)) {	
				org = g_new0 (ECalComponentOrganizer, 1);	

				if (go_attendee->email) 
					org->value = g_strconcat ("MAILTO:", go_attendee->email, NULL);
				if (go_attendee->value_string) 
					org->cn = g_strdup (go_attendee->value_string);
			}	
		}
		
		attendee_list = g_slist_prepend (attendee_list, attendee);	
	}
	e_cal_component_set_attendee_list (comp, attendee_list);

	/* Set the organizer if any */
	if (org)
		e_cal_component_set_organizer (comp, org);

	/* Location */
	go_location_list = gdata_calendar_event_get_places (GDATA_CALENDAR_EVENT(item->entry));

	for (l = go_location_list; l != NULL; l = l->next) {
		GDataGDWhere *go_location;

		go_location = (GDataGDWhere *)l->data;

		if (go_location->rel == NULL || strcmp (go_location->rel, "http://schemas.google.com/g/2005#event") == 0)
			location = go_location->value_string;
	}
	e_cal_component_set_location (comp, location);

#if 0
	/* temp hack to see how recurrence work */
	ECalComponentRange *recur_id;
	recur_id = g_new0 (ECalComponentRange, 1);
	recur_id->datetime = dt;
	recur_id->type = E_CAL_COMPONENT_RANGE_THISFUTURE;
	e_cal_component_set_recurid (comp, recur_id);
	e_cal_component_set_dtend (comp, &dt);
#endif

	uid = gdata_entry_get_id (item->entry);
	e_cal_component_set_uid (comp, (const char *)uid);
	e_cal_component_commit_sequence (comp);

	return comp;
}


/**
 *
 * e_go_item_from_cal_component:
 * @cbgo a ECalBackendGoogle
 * @comp a ECalComponent object
 * Creates a ECalComponent from EGoItem
 *
 **/
EGoItem *
e_go_item_from_cal_component (ECalBackendGoogle *cbgo, ECalComponent *comp)
{
	ECalBackendGooglePrivate *priv;
	EGoItem *item;
	ECalComponentText text;
	ECalComponentDateTime dt;
	gchar *term = NULL;
	icaltimezone *default_zone;
	icaltimetype itt;
	GTimeVal timeval, timeval2;
	const char *uid;
	const char *location;
	GSList *list = NULL;
	GDataCalendarEvent *entry;
	GDataCategory *category;
	GDataGDWhen *when;
	ECalComponentText *t;
	GSList *attendee_list = NULL, *l = NULL;

	priv = cbgo->priv;

	e_cal_component_get_uid (comp, &uid);

	item = g_new0 (EGoItem, 1);
	entry = gdata_calendar_event_new (uid);

	/* Summary */
	e_cal_component_get_summary (comp, &text);
	if (text.value != NULL)
		gdata_entry_set_title (GDATA_ENTRY(entry), text.value);

	default_zone = e_cal_backend_google_get_default_zone (cbgo);

	/* Start/End times */
	e_cal_component_get_dtstart (comp, &dt);
	itt = icaltime_convert_to_zone (*dt.value, default_zone);
	dt.value = &itt;
	get_timeval (dt, &timeval);

	e_cal_component_get_dtend (comp, &dt);
	itt = icaltime_convert_to_zone (*dt.value, default_zone);
	dt.value = &itt;
	get_timeval (dt, &timeval2);

	when = gdata_gd_when_new (&timeval, &timeval2, NULL, NULL);
	gdata_calendar_event_add_time (entry, when);

	/* Content / Description */
	e_cal_component_get_description_list (comp, &list);
	if (list != NULL) {
		t = (ECalComponentText *)list->data;
		gdata_entry_set_content (GDATA_ENTRY (entry), t->value);
	} else
		gdata_entry_set_content (GDATA_ENTRY (entry), "");

	/* Location */
	e_cal_component_get_location (comp, &location);
	if (location) {
		GDataGDWhere *where;

		where = gdata_gd_where_new (NULL, location, NULL);
		gdata_calendar_event_add_place (entry, where);
	}

	if (e_cal_backend_get_kind (E_CAL_BACKEND(cbgo)) == ICAL_VEVENT_COMPONENT)
		term = "http://schemas.google.com/g/2005#event";

	category = gdata_category_new (term, "http://schemas.google.com/g/2005#kind", "label");
	gdata_entry_add_category (GDATA_ENTRY(entry), category);

	/* Attendee */
	e_cal_component_get_attendee_list (comp, &attendee_list);

	for (l = attendee_list; l != NULL; l = l->next) {
		ECalComponentAttendee *ecal_att;
		GDataGDWho *who;
		gchar *email;
		const gchar *rel;

		ecal_att = (ECalComponentAttendee *)l->data;

		/* Extract the attendee's e-mail address from att->value, which should be in the form:
		 * MAILTO:john@foobar.com
		 */
		email = strstr (ecal_att->value, "MAILTO:");
		if (!email)
			continue;
		email += 7; /* length of MAILTO: */

		rel = "http://schemas.google.com/g/2005#event.attendee";
		if (e_cal_component_has_organizer (comp)) {
			ECalComponentOrganizer org;

			e_cal_component_get_organizer (comp, &org);
			if (strcmp (org.value, ecal_att->value) == 0)
				rel = "http://schemas.google.com/g/2005#event.organizer";
		}

		who = gdata_gd_who_new (rel, ecal_att->cn, email);
		gdata_calendar_event_add_person (entry, who);
	}

	/* FIXME For transparency and status */
	item->entry = GDATA_ENTRY (entry);
	return item;
}

/**
 *
 * e_go_item_get_entry:
 * @item a EGoItem
 * Returns the GDataEntry object
 *
 **/

GDataEntry *
e_go_item_get_entry (EGoItem *item)
{
	g_return_val_if_fail (item != NULL, NULL);
	return item->entry;
}


/**
 *
 * e_go_item_set_entry:
 * @item  a EGoItem
 * @entry a GDataEntry
 * Sets the GDataEntry of EGoItem to entry
 *
 **/
void
e_go_item_set_entry (EGoItem *item, GDataEntry *entry)
{
	g_return_if_fail (item != NULL);
	g_return_if_fail (entry != NULL);

	item->entry = entry;
}


/***************************************************************** Utility Functions *********************************************/

static gint
utils_compare_ids (gconstpointer cache_id, gconstpointer modified_cache_id)
{
	return strcmp ((char *)cache_id, (char *)modified_cache_id);
}

static gchar *
utils_form_query (const gchar *query)
{
	if (query!=NULL) {
		query = query + 9;
	}
	return g_strdup(query);
}

static void
utils_update_insertion (ECalBackendGoogle *cbgo, ECalBackendCache *cache, EGoItem *item, GSList *uid_list)
{
	EGoItem *item_t;
	ECalComponent *comp;
	GSList *list = NULL;
	GDataEntry *entry;
	gchar *temp;

	comp = e_cal_component_new ();
	item_t = g_new0 (EGoItem, 1);
	item_t->feed = item->feed;

	for (list = uid_list; list != NULL; list = list->next) {
		entry = gdata_feed_look_up_entry (item->feed, list->data);
		item_t->entry = entry;
		comp = e_go_item_to_cal_component (item_t, cbgo);

		if (comp) {
			e_cal_component_commit_sequence (comp);
			e_cal_backend_cache_put_component (cache, comp);

			temp = e_cal_component_get_as_string (comp);

			e_cal_backend_notify_object_created (E_CAL_BACKEND(cbgo), temp);

			g_free (temp);
			g_object_unref (comp);
		}
	}

	g_free (item_t);
	if (list)
		g_slist_free (list);
}


static void
utils_update_deletion (ECalBackendGoogle *cbgo, ECalBackendCache *cache, GSList *cache_keys)
{
	ECalComponent *comp;
	GSList *list;

	comp = e_cal_component_new ();

	g_return_if_fail (E_IS_CAL_BACKEND_GOOGLE (cbgo));
	g_return_if_fail (cache != NULL && cbgo != NULL);
	g_return_if_fail (cache_keys != NULL);

	for (list = cache_keys; list; list = g_slist_next (list)) {
		ECalComponentId *id = NULL;
		char *comp_str = NULL;
		comp = e_cal_backend_cache_get_component (cache, (const char *)list->data, NULL);
		comp_str = e_cal_component_get_as_string (comp);
		id = e_cal_component_get_id (comp);

		e_cal_backend_notify_object_removed (E_CAL_BACKEND (cbgo), id, comp_str, NULL);
		e_cal_backend_cache_remove_component (cache, (const char *) id->uid, id->rid);

		e_cal_component_free_id (id);
		g_object_unref (comp);
		g_free (comp_str);
	}
}

/**
 * get_timeval: Returns date in a GTimeVal
 * @dt a #ECalComponentDateTime value
 * @timeval a #GTimeVal
 **/
static void
get_timeval (ECalComponentDateTime dt, GTimeVal *timeval)
{
	time_t tt;

	/* GTimeVals are always in UTC */
	tt = icaltime_as_timet_with_zone (*(dt.value), icaltimezone_get_utc_timezone ());

	timeval->tv_sec = (glong) tt;
	timeval->tv_usec = 0;
}

static gboolean
get_deltas_timeout (gpointer cbgo)
{
	GThread *thread;

	if (!cbgo)
		return FALSE;

	e_cal_backend_google_utils_update (cbgo);
	thread = g_thread_create ((GThreadFunc) e_cal_backend_google_utils_update, cbgo, FALSE, NULL);
	if (!thread) {
		 /* FIXME */
	}

	return TRUE;
}

/**
 *
 * gd_timeval_to_ical:
 * Helper Function to convert a gdata format date to ical date
 * @item item from which the time comes. It's used to get to the feed's timezone
 * @timeval date as a #GTimeVal
 * @iit Resulting icaltimetype.
 * @dt Resulting ECalComponentDateTime.
 * @default_zone Default time zone for the backend. If set, then the time will be converted to that timezone.
 *
 * @note Do not free itt or dt values, those come from buildin structures held by libical
 **/
static gboolean
gd_timeval_to_ical (EGoItem *item, GTimeVal *timeval, struct icaltimetype *itt, ECalComponentDateTime *dt, icaltimezone *default_zone)
{
	g_return_val_if_fail (itt != NULL, FALSE);
	g_return_val_if_fail (dt != NULL, FALSE);

	if (!timeval)
		return FALSE;

	*itt = icaltime_from_timet_with_zone (timeval->tv_sec, 0, icaltimezone_get_utc_timezone ());

	if (default_zone)
		*itt = icaltime_convert_to_zone (*itt, default_zone);

	dt->value = itt;
	dt->tzid = icaltimezone_get_tzid ((icaltimezone *) icaltime_get_timezone (*itt));

	return TRUE;
}
