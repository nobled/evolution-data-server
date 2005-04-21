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

#include <time.h>
#include "e-cal-backend-exchange-tasks.h"
#include "e2k-properties.c"
#include "libecal/e-cal-component.h"
//#include "e-util/e-config-listener.h"
#include "e2k-cal-utils.h"
#include "e2k-context.h"
#include "exchange-account.h"
#include "e-folder-exchange.h"
#include "e2k-operation.h"
#include "e2k-restriction.h"
#include "e2k-utils.h"

#define d(x)

/* Placeholder for each component and its recurrences */
typedef struct {
        ECalComponent *full_object;
        GHashTable *recurrences;
} ECalBackendExchangeTasksObject;

/* Private part of the ECalBackendExchangeTasks structure */
struct _ECalBackendExchangeTasksPrivate {
	/* URI where the task data is stored */
	char *uri;

	/* Top level VTODO component */
	icalcomponent *icalcomp;

	/* All the objects in the calendar, hashed by UID.  The
         * hash key *is* the uid returned by cal_component_get_uid(); it is not
         * copied, so don't free it when you remove an object from the hash
         * table. Each item in the hash table is a ECalBackendExchangeTasksObject.
         */
        GHashTable *comp_uid_hash;
	
	GList *comp;

	int dummy;
};

#define PARENT_TYPE E_TYPE_CAL_BACKEND_EXCHANGE
#define d(x)
static ECalBackendExchange *parent_class = NULL;

static void
get_from (ECalBackendSync *backend, ECalComponent *comp, char **from_name, char **from_addr)
{
	e_cal_backend_exchange_get_from (backend, comp, from_name, from_addr);
#if 0
	ECalComponentOrganizer org;

	e_cal_component_get_organizer (E_CAL_COMPONENT (comp), &org);

	if (org.cn && org.cn[0] && org.value && org.value[0]) { 
                *from_name = org.cn;
                if (!g_ascii_strncasecmp (org.value, "mailto:", 7))
                        *from_addr = org.value + 7;
                else
                        *from_addr = org.value;
        } else {
                *from_name = e_cal_backend_exchange_get_cal_owner (E_CAL_BACKEND_SYNC (backend));
                *from_addr = e_cal_backend_exchange_get_cal_address (E_CAL_BACKEND_SYNC (backend));
        }
#endif
}

static void
set_uid (E2kProperties *props, ECalComponent *comp)
{
        const char *uid;
        
	e_cal_component_get_uid (E_CAL_COMPONENT (comp), &uid);
        e2k_properties_set_string (props, E2K_PR_CALENDAR_UID, g_strdup (uid));
}

static void
set_summary (E2kProperties *props, ECalComponent *comp)
{
        static ECalComponentText summary;
        
	e_cal_component_get_summary (E_CAL_COMPONENT (comp), &summary);
        if (summary.value) {
                e2k_properties_set_string (props, E2K_PR_HTTPMAIL_THREAD_TOPIC,
                                           g_strdup (summary.value));
        } else
                e2k_properties_remove (props, E2K_PR_HTTPMAIL_THREAD_TOPIC);
}

static void
set_priority (E2kProperties *props, ECalComponent *comp)
{
        int *priority, value = 0;
        
	e_cal_component_get_priority (E_CAL_COMPONENT (comp), &priority);
        if (priority) {
                if (*priority == 0)
                        value = 0;
                else if (*priority <= 4)
                        value = 1;
                else if (*priority == 5)
                        value = 0;
                else
                        value = 2;
                e_cal_component_free_priority (priority);
        }
        e2k_properties_set_int (props, E2K_PR_MAPI_PRIORITY, value);
}

static void
set_sensitivity (E2kProperties *props, ECalComponent *comp)
{
        ECalComponentClassification classif;
        int sensitivity;
        
	e_cal_component_get_classification (E_CAL_COMPONENT (comp), &classif);
        switch (classif) {
        case E_CAL_COMPONENT_CLASS_PRIVATE:
                sensitivity = 2;
                break;
        case E_CAL_COMPONENT_CLASS_CONFIDENTIAL:
                sensitivity = 1;
                break;
        default:
                sensitivity = 0;
                break;
        }
        
	e2k_properties_set_int (props, E2K_PR_MAPI_SENSITIVITY, sensitivity);
}
#if 0
SURF : 
icaltimezone *
get_default_timezone (void)
{
        static EConfigListener *cl = NULL;
        gchar *location;
        icaltimezone *local_timezone;
        
	if (!cl)
                cl = e_config_listener_new ();
        
	location = e_config_listener_get_string_with_default (cl, "/apps/evolution/calendar/display/timezone", "UTC", NULL);

        if (location && location[0]) {
                local_timezone = icaltimezone_get_builtin_timezone (location);
        } else {
                local_timezone = icaltimezone_get_utc_timezone ();
        }
        
	g_free (location);
        
	return local_timezone;
}
#endif
char *
calcomponentdatetime_to_string (ECalComponentDateTime *dt,
                                icaltimezone *izone)
{
        time_t tt;
        
	g_return_val_if_fail (dt != NULL, NULL);
        g_return_val_if_fail (dt->value != NULL, NULL);
        
	if (izone != NULL)
                tt = icaltime_as_timet_with_zone (*dt->value, izone);
        else
                tt = icaltime_as_timet (*dt->value);
        
	return e2k_make_timestamp (tt);
}

static char *
convert_to_utc (ECalComponentDateTime *dt)
{
        icaltimezone *from_zone;
        icaltimezone *utc_zone;
        
	from_zone = icaltimezone_get_builtin_timezone_from_tzid (dt->tzid);
        utc_zone = icaltimezone_get_utc_timezone ();
/* SURF :        if (!from_zone)
                from_zone = get_default_timezone (); */
        dt->value->is_date = 0;
        icaltimezone_convert_time (dt->value, from_zone, utc_zone);
        
	return calcomponentdatetime_to_string (dt, utc_zone);
}


static void
set_dtstart (E2kProperties *props, ECalComponent *comp)
{
        ECalComponentDateTime dt;
        char *dtstart_str;
        
	e_cal_component_get_dtstart (E_CAL_COMPONENT (comp), &dt);
        if (!dt.value || icaltime_is_null_time (*dt.value)) {
                e_cal_component_free_datetime (&dt);
                e2k_properties_remove (props, E2K_PR_MAPI_COMMON_START);
                return;
        }
        
	dtstart_str = convert_to_utc (&dt);
        e_cal_component_free_datetime (&dt);
        e2k_properties_set_date (props, E2K_PR_MAPI_COMMON_START, dtstart_str);
}

static void
set_due_date (E2kProperties *props, ECalComponent *comp)
{
        ECalComponentDateTime dt;
        char *due_str;
        
	e_cal_component_get_due (E_CAL_COMPONENT (comp), &dt);
        if (!dt.value || icaltime_is_null_time (*dt.value)) {
                e_cal_component_free_datetime (&dt);
                e2k_properties_remove (props, E2K_PR_MAPI_COMMON_END);
                return;
        }
        
	due_str = convert_to_utc (&dt);
        e_cal_component_free_datetime (&dt);
        e2k_properties_set_date (props, E2K_PR_MAPI_COMMON_END, due_str);
}

char *
icaltime_to_e2k_time (struct icaltimetype *itt)
{
        time_t tt;
        
	g_return_val_if_fail (itt != NULL, NULL);
        
	tt = icaltime_as_timet_with_zone (*itt, icaltimezone_get_utc_timezone ());
        return e2k_make_timestamp (tt);
}

static void
set_date_completed (E2kProperties *props, ECalComponent *comp)
{
        struct icaltimetype *itt;
        char *tstr;
        
	e_cal_component_get_completed (E_CAL_COMPONENT (comp), &itt);
        if (!itt || icaltime_is_null_time (*itt)) {
                e2k_properties_remove (props, E2K_PR_OUTLOOK_TASK_DONE_DT);
                return;
        }
        
	icaltimezone_convert_time (itt, 
				   icaltimezone_get_builtin_timezone ((const char *)itt->zone), 
				   icaltimezone_get_utc_timezone ());
        tstr = icaltime_to_e2k_time (itt);
        e_cal_component_free_icaltimetype (itt);
        
	e2k_properties_set_date (props, E2K_PR_OUTLOOK_TASK_DONE_DT, tstr);
}

static void
set_status (E2kProperties *props, ECalComponent *comp)
{
        icalproperty_status ical_status;
        int status;
        
	e_cal_component_get_status (E_CAL_COMPONENT (comp), &ical_status);
        switch (ical_status) {
        case ICAL_STATUS_NONE :
        case ICAL_STATUS_NEEDSACTION :
                /* Not Started */
                status = 0;
                break;
        case ICAL_STATUS_INPROCESS :
                /* In Progress */
                status = 1;
                break;
        case ICAL_STATUS_COMPLETED :
                /* Completed */
                status = 2;
                break;
        case ICAL_STATUS_CANCELLED :
                /* Deferred */
                status = 4;
                break;
        default :
                status = 0;
        }
        
	e2k_properties_set_int (props, E2K_PR_OUTLOOK_TASK_STATUS, status);
        e2k_properties_set_bool (props, E2K_PR_OUTLOOK_TASK_IS_DONE, status == 2);
}

static void
set_percent (E2kProperties *props, ECalComponent *comp)
{
        int *percent;
        float res;
        
	e_cal_component_get_percent (E_CAL_COMPONENT (comp), &percent);
        if (percent) {
                res = (float) *percent / 100.0;
                e_cal_component_free_percent (percent);
        } else
                res = 0.;
        
	e2k_properties_set_float (props, E2K_PR_OUTLOOK_TASK_PERCENT, res);
}

static void
set_categories (E2kProperties *props, ECalComponent *comp)
{
        GSList *categories;
        GSList *sl;
        GPtrArray *array;
        
	e_cal_component_get_categories_list (E_CAL_COMPONENT (comp), &categories);
        if (!categories) {
                e2k_properties_remove (props, E2K_PR_EXCHANGE_KEYWORDS);
                return;
        }
        
	array = g_ptr_array_new ();
        for (sl = categories; sl != NULL; sl = sl->next) {
                char *cat = (char *) sl->data;
                
		if (cat)
                        g_ptr_array_add (array, g_strdup (cat));
        }
        e_cal_component_free_categories_list (categories);
        
	e2k_properties_set_string_array (props, E2K_PR_EXCHANGE_KEYWORDS, array);
}

static void
set_url (E2kProperties *props, ECalComponent *comp)
{
        const char *url;
        
	e_cal_component_get_url (E_CAL_COMPONENT (comp), &url);
        if (url)
                e2k_properties_set_string (props, E2K_PR_CALENDAR_URL, g_strdup (url));
        else
                e2k_properties_remove (props, E2K_PR_CALENDAR_URL);
}

static void
update_props (ECalComponent *comp, E2kProperties **properties)
{
	E2kProperties *props = *properties;
	
	set_uid (props, E_CAL_COMPONENT (comp));
	set_summary (props, E_CAL_COMPONENT (comp));
	set_priority (props, E_CAL_COMPONENT (comp));
	set_sensitivity (props, E_CAL_COMPONENT (comp));

	set_dtstart (props, E_CAL_COMPONENT (comp));
	set_due_date (props, E_CAL_COMPONENT (comp));
	set_date_completed (props, E_CAL_COMPONENT (comp));

	set_status (props, E_CAL_COMPONENT (comp));
	set_percent (props, E_CAL_COMPONENT (comp));

	set_categories (props, E_CAL_COMPONENT (comp));
	set_url (props, E_CAL_COMPONENT (comp));
}

static const char *
get_priority (ECalComponent *comp)
{
	int *priority;
	const char *result;

	e_cal_component_get_priority (E_CAL_COMPONENT (comp), &priority);

	if (!priority)
		return "normal";
	
	if (*priority == 0)
		result = "normal";
	else if (*priority <= 4)
		result = "high";
	else if (*priority == 5)
		result = "normal";
	else
		result = "low";

	e_cal_component_free_priority (priority);

	return result;
}

static const char *
get_uid (ECalComponent *comp)
{
	const char *uid;

	e_cal_component_get_uid (E_CAL_COMPONENT(comp), &uid);
	return uid;
}

static const char *
get_summary (ECalComponent *comp)
{
	ECalComponentText summary;

	e_cal_component_get_summary(E_CAL_COMPONENT (comp), &summary);

	return summary.value;
}

static int
put_body (ECalComponent *comp, E2kContext *ctx, E2kOperation *op,
         const char *uri, const char *from_name, const char *from_addr,
	 const char *attach_body, const char *boundary,
         char **repl_uid)

{
        GSList *desc_list;
        GString *desc;
        char *desc_crlf;
        char *body, *date;
        int status;

        /* get the description */
        e_cal_component_get_description_list (E_CAL_COMPONENT (comp), &desc_list);
        desc = g_string_new ("");
        if (desc_list != NULL) {
                GSList *sl;

                for (sl = desc_list; sl; sl = sl->next) {
                        ECalComponentText *text = (ECalComponentText *) sl->data;

                        if (text)
                                desc = g_string_append (desc, text->value);
                }
        }

	/* PUT the component on the server */
        desc_crlf = e2k_lf_to_crlf ((const char *) desc->str);
        date = e2k_make_timestamp_rfc822 (time (NULL));
	
	if (attach_body) {
		body = g_strdup_printf ("content-class: urn:content-classes:task\r\n"
                                "Subject: %s\r\n"
                                "Date: %s\r\n"
                                "Message-ID: <%s>\r\n"
                                "MIME-Version: 1.0\r\n"
                                "Content-Type: multipart/mixed;\r\n"
				"\tboundary=\"%s\";\r\n"
				"X-MS_Has-Attach: yes\r\n"
                                "From: \"%s\" <%s>\r\n"
				"\r\n--%s\r\n"
				"content-class: urn:content-classes:task\r\n"
				"Content-Type: text/plain;\r\n"
                                "\tcharset=\"utf-8\"\r\n"
                                "Content-Transfer-Encoding: 8bit\r\n"
                                "Thread-Topic: %s\r\n"
                                "Priority: %s\r\n"
                                "Importance: %s\r\n"
                                "\r\n%s\r\n%s",
                                get_summary (comp),
                                date,
                                get_uid (comp),
				boundary,
				from_name ? from_name : "Evolution",
				from_addr ? from_addr : "",
				boundary,
                                get_summary (comp),
                                get_priority (comp),
                                get_priority (comp),
                                desc_crlf,
				attach_body);

	} else {
		body = g_strdup_printf ("content-class: urn:content-classes:task\r\n"
                                "Subject: %s\r\n"
                                "Date: %s\r\n"
                                "Message-ID: <%s>\r\n"
                                "MIME-Version: 1.0\r\n"
                                "Content-Type: text/plain;\r\n"
                                "\tcharset=\"utf-8\"\r\n"
                                "Content-Transfer-Encoding: 8bit\r\n"
                                "Thread-Topic: %s\r\n"
                                "Priority: %s\r\n"
                                "Importance: %s\r\n"
                                "From: \"%s\" <%s>\r\n"
                                "\r\n%s",
                                get_summary (comp),
                                date,
                                get_uid (comp),
                                get_summary (comp),
                                get_priority (comp),
                                get_priority (comp),
                                from_name ? from_name : "Evolution",
				from_addr ? from_addr : "",
                                desc_crlf);
	}

        status = e2k_context_put (ctx, NULL, uri, "message/rfc822",
				  body, strlen (body), NULL);

        /* free memory */
        g_free (body);
        g_free (desc_crlf);
        g_free (date);
        e_cal_component_free_text_list (desc_list);
        g_string_free (desc, TRUE);

        return status;
}

static const char *task_props[] = {
        E2K_PR_EXCHANGE_MESSAGE_CLASS,
        E2K_PR_DAV_UID,
        E2K_PR_CALENDAR_UID,
        E2K_PR_DAV_LAST_MODIFIED,
        E2K_PR_HTTPMAIL_SUBJECT,
        E2K_PR_HTTPMAIL_TEXT_DESCRIPTION,
        E2K_PR_HTTPMAIL_DATE,
	E2K_PR_HTTPMAIL_HAS_ATTACHMENT,
        E2K_PR_CALENDAR_LAST_MODIFIED,
        E2K_PR_HTTPMAIL_FROM_EMAIL,
        E2K_PR_HTTPMAIL_FROM_NAME,
        E2K_PR_MAILHEADER_IMPORTANCE,
        E2K_PR_MAPI_SENSITIVITY,
        E2K_PR_MAPI_COMMON_START,
        E2K_PR_MAPI_COMMON_END,
        E2K_PR_OUTLOOK_TASK_STATUS,
        E2K_PR_OUTLOOK_TASK_PERCENT,
        E2K_PR_OUTLOOK_TASK_DONE_DT,
        E2K_PR_EXCHANGE_KEYWORDS,
        E2K_PR_CALENDAR_URL
};
static const int n_task_props = sizeof (task_props) / sizeof (task_props[0]);

static guint
get_changed_tasks (ECalBackendExchange *cbex, const char *since)
{
	ECalBackendExchangeComponent *ecalbexcomp;
	E2kRestriction *rn;
	E2kResultIter *iter;
	GPtrArray *hrefs, *array;
	GHashTable *modtimes, *attachments;
	GSList *attachment_list = NULL;
	E2kResult *result;
	E2kContext *ctx;
	const char *modtime, *str, *prop;
	char *uid, *body;
	char *tzid;
	int status, i, priority, percent;
	float f_percent;
	ECalComponent *ecal, *ecomp;
	struct icaltimetype itt;
	const icaltimezone *itzone;
	ECalComponentDateTime ecdatetime;
	icalcomponent *icalcomp;

        g_return_val_if_fail (E_IS_CAL_BACKEND_EXCHANGE (cbex), SOUP_STATUS_CANCELLED);

	rn = e2k_restriction_prop_string (E2K_PR_DAV_CONTENT_CLASS,
					  E2K_RELOP_EQ,
					  "urn:content-classes:task");
	if (since) {
		rn = e2k_restriction_andv (rn,
					   e2k_restriction_prop_date (
						   E2K_PR_DAV_LAST_MODIFIED,
						   E2K_RELOP_GT,
						   since),
					   NULL);
	} else
		e_cal_backend_exchange_cache_sync_start (cbex);

        if (cbex->private_item_restriction) {
                e2k_restriction_ref (cbex->private_item_restriction);
                rn = e2k_restriction_andv (rn, cbex->private_item_restriction, NULL);
        }

        iter = e_folder_exchange_search_start (cbex->folder, NULL,
					       task_props,
					       n_task_props,
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
		if (!uid) {
			uid = e2k_properties_get_prop (result->props,
						       E2K_PR_DAV_UID);
		}
		if (!uid)
			continue;
		
		ecal = e_cal_component_new ();
		icalcomp = icalcomponent_new_vtodo ();
		e_cal_component_set_icalcomponent (ecal, icalcomp);
		e_cal_component_set_uid (ecal, (const char *)uid);

		modtime = e2k_properties_get_prop (result->props,
						   E2K_PR_DAV_LAST_MODIFIED);

		if (!e_cal_backend_exchange_in_cache (cbex, uid, modtime, result->href)) {
			g_ptr_array_add (hrefs, g_strdup (result->href));
			g_hash_table_insert (modtimes, g_strdup (result->href),
					     g_strdup (modtime));
		}

		e_cal_backend_exchange_add_timezone (cbex, icalcomp);		

		itt = icaltime_from_timet (e2k_parse_timestamp (modtime), 0);
		if (!icaltime_is_null_time (itt)) 
			e_cal_component_set_last_modified (ecal, &itt);

		/* Set Priority */
		if ((str = e2k_properties_get_prop (result->props, 
				E2K_PR_MAILHEADER_IMPORTANCE))) {
			if (!strcmp (str, "high"))
				priority = 3;
			else if (!strcmp (str, "low"))
				priority = 7;
			else if (!strcmp (str, "normal"))
				priority = 5;
			else
				priority = 0;
			
			e_cal_component_set_priority (ecal, &priority);
		}

		/* Set Summary */
		if ((str = e2k_properties_get_prop (result->props, 
				E2K_PR_HTTPMAIL_SUBJECT))) {
			ECalComponentText summary;
			summary.value = str;
			summary.altrep = result->href;
			e_cal_component_set_summary (E_CAL_COMPONENT (ecal), &summary);
		}
		
		/* Set DTSTAMP */
		if ((str = e2k_properties_get_prop (result->props, 
				E2K_PR_HTTPMAIL_DATE))) {
			itt = icaltime_from_timet (e2k_parse_timestamp (str), 0);
			if (!icaltime_is_null_time (itt)) {
				e_cal_component_set_dtstamp (
					E_CAL_COMPONENT (ecal), &itt);
				e_cal_component_set_created (
					E_CAL_COMPONENT (ecal), &itt);
			}
		}
	
		/* Set DESCRIPTION */
		if ((str = e2k_properties_get_prop (result->props, 
				E2K_PR_HTTPMAIL_TEXT_DESCRIPTION))) {
			GSList sl;
			ECalComponentText text;
		
			text.value = e2k_crlf_to_lf (str);
			text.altrep = result->href;
			sl.data = &text;
			sl.next = NULL;
                	e_cal_component_set_description_list (E_CAL_COMPONENT (ecal), &sl);
			g_free ((char *)text.value);
		}

		/* Set DUE */
		if ((str = e2k_properties_get_prop (result->props, E2K_PR_MAPI_COMMON_END))) {
			itt = icaltime_from_timet (e2k_parse_timestamp (str), 0);
			if (!icaltime_is_null_time (itt)) {
				itzone = icaltime_get_timezone ((const struct icaltimetype)itt);
				tzid = icaltimezone_get_tzid ((icaltimezone *)itzone);
				ecdatetime.value = &itt;
				ecdatetime.tzid = tzid;
				e_cal_component_set_due (ecal, &ecdatetime);	
			}
		}	
	
		/* Set DTSTART */
		if ((str = e2k_properties_get_prop (result->props, 
				E2K_PR_MAPI_COMMON_START))) {
			itt = icaltime_from_timet (e2k_parse_timestamp (str), 0);
			if (!icaltime_is_null_time (itt)) {
				itzone = icaltime_get_timezone ((const struct icaltimetype) itt);
				tzid = icaltimezone_get_tzid ((icaltimezone *)itzone);
				ecdatetime.value = &itt;
				ecdatetime.tzid = tzid;
				e_cal_component_set_dtstart (ecal, &ecdatetime);
			}
		}

		/* Set CLASSIFICATION */
		if ((str = e2k_properties_get_prop (result->props, 
				E2K_PR_MAPI_SENSITIVITY))){
			if (!strcmp (str, "0"))
				e_cal_component_set_classification (ecal,
					E_CAL_COMPONENT_CLASS_PUBLIC);
			else if (!strcmp (str, "1"))
				e_cal_component_set_classification (ecal,
					E_CAL_COMPONENT_CLASS_CONFIDENTIAL);
			else if (!strcmp (str, "2"))
				e_cal_component_set_classification (ecal,
					E_CAL_COMPONENT_CLASS_PRIVATE);
		}		

		/* Set Percent COMPLETED */
		if ((str = e2k_properties_get_prop (result->props, 
				E2K_PR_OUTLOOK_TASK_PERCENT))) {
			
			f_percent = atof (str);
			percent = (int) (f_percent * 100);
			e_cal_component_set_percent (ecal, &percent);
		}

		/* Set STATUS */
		if ((str = e2k_properties_get_prop (result->props, 
				E2K_PR_OUTLOOK_TASK_STATUS))) {
			if (!strcmp (str, "0")) {
				/* Not Started */
				e_cal_component_set_status (ecal, 
					ICAL_STATUS_NEEDSACTION);
			} else if (!strcmp (str, "1")) {
				/* In Progress */	
				e_cal_component_set_status (ecal,
					ICAL_STATUS_INPROCESS);
			} else if (!strcmp (str, "2")) {
				/* Completed */
				e_cal_component_set_status (ecal,
					ICAL_STATUS_COMPLETED);
			} else if (!strcmp (str, "3")) {
				/* Waiting on someone else */
				e_cal_component_set_status (ecal,
					ICAL_STATUS_INPROCESS);
			} else if (!strcmp (str, "4")) {
				/* Deferred */
				e_cal_component_set_status (ecal,
					ICAL_STATUS_CANCELLED);
			}
		}

		/* Set DATE COMPLETED */
		if ((str = e2k_properties_get_prop (result->props, 
				E2K_PR_OUTLOOK_TASK_DONE_DT))) {
			itt = icaltime_from_timet (e2k_parse_timestamp (str), 0);
			if (!icaltime_is_null_time (itt))
				e_cal_component_set_completed (ecal, &itt);
		}
	
		/* Set LAST MODIFIED */
		if ((str = e2k_properties_get_prop (result->props, 
				E2K_PR_CALENDAR_LAST_MODIFIED))) {
			itt = icaltime_from_timet (e2k_parse_timestamp(str), 0);
			if (!icaltime_is_null_time (itt))
				e_cal_component_set_last_modified (ecal, &itt);
		}
		
		/* Set CATEGORIES */	
		if ((array = e2k_properties_get_prop (result->props, 
				E2K_PR_EXCHANGE_KEYWORDS))) {
			GSList *list = NULL;
			int i;
		
			for (i = 0; i < array->len; i++)
				list = g_slist_prepend (list, array->pdata[i]);
			
			e_cal_component_set_categories_list (ecal, list);
			g_slist_free (list);
		}
		
		/* Set URL */
		if ((str = e2k_properties_get_prop (result->props, 
				E2K_PR_CALENDAR_URL))) {
			e_cal_component_set_url (ecal, str);
		}	

		/* Set Attachments */
		if ((str = e2k_properties_get_prop (result->props,
				E2K_PR_HTTPMAIL_HAS_ATTACHMENT))) {
			g_hash_table_insert (attachments, g_strdup (result->href),
				g_strdup (uid));
		}
		e_cal_component_commit_sequence (ecal);
		icalcomp = e_cal_component_get_icalcomponent (ecal);
		if (icalcomp)
			e_cal_backend_exchange_add_object (cbex, result->href, 
						modtime, icalcomp);
	} /* End while */
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
	
	prop = PR_INTERNET_CONTENT;
	iter = e_folder_exchange_bpropfind_start (cbex->folder, NULL,
						(const char **)hrefs->pdata,
						hrefs->len, &prop, 1);
	for (i = 0; i < hrefs->len; i++)
		g_free (hrefs->pdata[i]);
	g_ptr_array_set_size (hrefs, 0);

	while ((result = e2k_result_iter_next (iter))) {
		GByteArray *ical_data;
		ical_data = e2k_properties_get_prop (result->props, PR_INTERNET_CONTENT);
		if (!ical_data) {
			g_ptr_array_add (hrefs, g_strdup (result->href));
			continue;
		}

		uid = g_hash_table_lookup (attachments, result->href);
		/* Fetch component from cache and update it */
		ecalbexcomp = get_exchange_comp (cbex, uid);
		attachment_list = get_attachment (cbex, uid, ical_data->data, ical_data->len);
		if (attachment_list) {
			ecomp = e_cal_component_new ();
			e_cal_component_set_icalcomponent (ecomp, icalcomponent_new_clone (ecalbexcomp->icomp));
			e_cal_component_set_attachment_list (ecomp, attachment_list);
			icalcomponent_free (ecalbexcomp->icomp);
			ecalbexcomp->icomp = icalcomponent_new_clone (e_cal_component_get_icalcomponent (ecomp));
			g_object_unref (ecomp);
		}
	}
	status = e2k_result_iter_free (iter);

	if (!SOUP_STATUS_IS_SUCCESSFUL (status)) {
		g_ptr_array_free (hrefs, TRUE);
		g_hash_table_destroy (attachments);
		return status;
	}

	if (!hrefs->len) {
		g_ptr_array_free (hrefs, TRUE);
		g_hash_table_destroy (attachments);
		return SOUP_STATUS_OK;
	}

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
		uid = g_hash_table_lookup (attachments, hrefs->pdata[i]);
		/* Fetch component from cache and update it */
		ecalbexcomp = get_exchange_comp (cbex, uid);
		attachment_list = get_attachment (cbex, uid, body, length);
		if (attachment_list) {
			ecomp = e_cal_component_new ();
			e_cal_component_set_icalcomponent (ecomp, icalcomponent_new_clone (ecalbexcomp->icomp));
			e_cal_component_set_attachment_list (ecomp, attachment_list);
			icalcomponent_free (ecalbexcomp->icomp);
			ecalbexcomp->icomp = icalcomponent_new_clone (e_cal_component_get_icalcomponent (ecomp));
			g_object_unref (ecomp);
		}
		g_free (body);
	}

	for (i = 0; i < hrefs->len; i++)
		g_free (hrefs->pdata[i]);
	g_ptr_array_free (hrefs, TRUE);
	g_hash_table_destroy (modtimes);
	g_hash_table_destroy (attachments);
	return status;
}

/* folder subscription notify callback */
static void
notify_changes (E2kContext *ctx, const char *uri,
                     E2kContextChangeType type, gpointer user_data)
{

	ECalBackendExchange *ecalbex = E_CAL_BACKEND_EXCHANGE (user_data);
	
	g_return_if_fail (E_IS_CAL_BACKEND_EXCHANGE (ecalbex));
	g_return_if_fail (uri != NULL);

	get_changed_tasks (ecalbex, NULL);
	
}

static ECalBackendSyncStatus
open_task (ECalBackendSync *backend, EDataCal *cal,
	   gboolean only_if_exits,
	   const char *username, const char *password)
{
	ECalBackendSyncStatus status;

	status = E_CAL_BACKEND_SYNC_CLASS (parent_class)->open_sync (backend,
				     cal, only_if_exits, username, password);
	if (status != GNOME_Evolution_Calendar_Success)
		return status;

	if (!e_cal_backend_exchange_is_online (E_CAL_BACKEND_EXCHANGE (backend))) {
		d(printf ("ECBEC : calendar is offline\n"));
		return GNOME_Evolution_Calendar_Success;
	}

	status = get_changed_tasks (E_CAL_BACKEND_EXCHANGE (backend), NULL);
	if (status != E2K_HTTP_OK)
		return GNOME_Evolution_Calendar_OtherError;
	
	/* Subscribe to the folder to notice changes */
        e_folder_exchange_subscribe (E_CAL_BACKEND_EXCHANGE (backend)->folder,
                                        E2K_CONTEXT_OBJECT_CHANGED, 30,
                                        notify_changes, backend);

	return GNOME_Evolution_Calendar_Success;
}

struct _cb_data {
        ECalBackendSync *be;
        icalcomponent *vcal_comp;
        EDataCal *cal;
};

static ECalBackendSyncStatus
create_task_object (ECalBackendSync *backend, EDataCal *cal,
		    char **calobj, char **return_uid)
{
	ECalBackendExchangeTasks *ecalbextask;
	ECalBackendExchange *ecalbex;
	E2kProperties *props;
	E2kContext *e2kctx;
	ECalComponent *comp;
	icalcomponent *icalcomp, *real_icalcomp;
	icalcomponent_kind kind;
	struct icaltimetype current;
	char *from_name, *from_addr;
	char *boundary = NULL;
	char *attach_body = NULL;
	char *attach_body_crlf = NULL;
	const char *summary;
	char * modtime;
	char *location;
	ECalBackendSyncStatus status;
	const char *temp_comp_uid;

	ecalbextask = E_CAL_BACKEND_EXCHANGE_TASKS (backend);
	ecalbex = E_CAL_BACKEND_EXCHANGE (backend);

	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	if (!e_cal_backend_exchange_is_online (E_CAL_BACKEND_EXCHANGE (backend))) {
		d(printf ("tasks are offline\n"));
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	/* Parse the icalendar text */
	icalcomp = icalparser_parse_string (*calobj);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;

	/* Check kind with the parent */
	kind = e_cal_backend_get_kind (E_CAL_BACKEND (ecalbex));
        if (icalcomponent_isa (icalcomp) != kind) {
		icalcomponent_free (icalcomp);
		return GNOME_Evolution_Calendar_InvalidObject;
        }

	current = icaltime_from_timet (time (NULL), 0);
	icalcomponent_add_property (icalcomp, icalproperty_new_created (current));
	icalcomponent_add_property (icalcomp, icalproperty_new_lastmodified (current));

	modtime = e2k_timestamp_from_icaltime (current);
	
	summary = icalcomponent_get_summary (icalcomp);
	if (!summary)
		summary = "";

	/* Get the uid */
	temp_comp_uid = icalcomponent_get_uid (icalcomp);
	if (!temp_comp_uid) {
		icalcomponent_free (icalcomp);
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	/* check if the object is already present in our cache */
	if (e_cal_backend_exchange_in_cache (E_CAL_BACKEND_EXCHANGE (backend), 
					     temp_comp_uid, modtime, NULL)) {
		icalcomponent_free (icalcomp);
		return GNOME_Evolution_Calendar_ObjectIdAlreadyExists;
	}	

	/* Create the cal component */
        comp = e_cal_component_new ();
        e_cal_component_set_icalcomponent (comp, icalcomp);

	get_from (backend, comp, &from_name, &from_addr);

	/* Check for attachments */
	if (e_cal_component_has_attachments (comp)) {
		d(printf ("This task has attachments\n"));
		attach_body = build_msg (ecalbex, comp, summary, &boundary);
		attach_body_crlf = e_cal_backend_exchange_lf_to_crlf (attach_body);
	}

	props = e2k_properties_new ();

	/* FIXME Check for props and its members */

	e2k_properties_set_string (
		props, E2K_PR_EXCHANGE_MESSAGE_CLASS,
		g_strdup ("IPM.Task"));

	/* Magic number to make the context menu in Outlook work */
	e2k_properties_set_int (props, E2K_PR_MAPI_SIDE_EFFECTS, 272);

	/* I don't remember what happens if you don't set this. */
	e2k_properties_set_int (props, PR_ACTION, 1280);

	/* Various fields we don't support but should initialize
	 * so evo-created tasks look the same as Outlook-created
	 * ones.
	 */
	e2k_properties_set_bool (props, E2K_PR_MAPI_NO_AUTOARCHIVE, FALSE);
	e2k_properties_set_bool (props, E2K_PR_OUTLOOK_TASK_TEAM_TASK, FALSE);
	e2k_properties_set_bool (props, E2K_PR_OUTLOOK_TASK_RECURRING, FALSE);
	e2k_properties_set_int (props, E2K_PR_OUTLOOK_TASK_ACTUAL_WORK, 0);
	e2k_properties_set_int (props, E2K_PR_OUTLOOK_TASK_TOTAL_WORK, 0);
	e2k_properties_set_int (props, E2K_PR_OUTLOOK_TASK_ASSIGNMENT, 0);
	e2k_properties_set_string (props, E2K_PR_OUTLOOK_TASK_OWNER,
				   g_strdup (from_name));

	update_props (comp, &props);
	e_cal_component_commit_sequence (comp);
	*calobj = e_cal_component_get_as_string (comp);
	if (!*calobj){
		g_object_unref (comp);
		return GNOME_Evolution_Calendar_OtherError;
	}

	real_icalcomp = icalparser_parse_string (*calobj);

	e2kctx = exchange_account_get_context (ecalbex->account);
	status = e_folder_exchange_proppatch_new (ecalbex->folder, NULL,
						  summary, NULL, NULL,
						  props, &location, NULL );

	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		status = put_body(comp, e2kctx, NULL, location, from_name, from_addr, 
						attach_body_crlf, boundary, NULL);
		if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
			e_cal_backend_exchange_add_object (ecalbex, location, modtime, real_icalcomp);
		}
		g_free (location);
		g_free (modtime);
	}

	*return_uid = g_strdup (temp_comp_uid); 
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
modify_task_object (ECalBackendSync *backend, EDataCal *cal,
	       const char *calobj, CalObjModType mod,
	       char **old_object, char **new_object)
{
	ECalBackendExchangeTasks *ecalbextask;
	ECalBackendExchangeComponent *ecalbexcomp;
	ECalComponent *cache_comp, *new_comp;
	ECalBackendExchange *ecalbex;
	E2kProperties *props;
	icalcomponent *icalcomp;
	const char* comp_uid, *summary;
	char *from_name, *from_addr;
	char *comp_str;
	char *attach_body = NULL;
	char *attach_body_crlf = NULL;
	char *boundary = NULL;
	struct icaltimetype current;
	ECalBackendSyncStatus status;
	E2kContext *e2kctx;

	ecalbextask = E_CAL_BACKEND_EXCHANGE_TASKS (backend);
	ecalbex = E_CAL_BACKEND_EXCHANGE (backend);

	g_return_val_if_fail (E_IS_CAL_BACKEND_EXCHANGE_TASKS (ecalbextask), 
					GNOME_Evolution_Calendar_NoSuchCal);
	g_return_val_if_fail (calobj != NULL, 
				GNOME_Evolution_Calendar_ObjectNotFound);

	if (!e_cal_backend_exchange_is_online (E_CAL_BACKEND_EXCHANGE (backend))) {
		d(printf ("tasks are offline\n"));
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	/* Parse the icalendar text */
        icalcomp = icalparser_parse_string ((char *) calobj);
        if (!icalcomp)
                return GNOME_Evolution_Calendar_InvalidObject;
        
	/* Check kind with the parent */
        if (icalcomponent_isa (icalcomp) != 
			e_cal_backend_get_kind (E_CAL_BACKEND (backend))) {
                icalcomponent_free (icalcomp);
                return GNOME_Evolution_Calendar_InvalidObject;
        }
        
	/* Get the uid */
        comp_uid = icalcomponent_get_uid (icalcomp);
        
	/* Get the object from our cache */
	ecalbexcomp = get_exchange_comp (E_CAL_BACKEND_EXCHANGE (backend), 
								comp_uid);

	if (!ecalbexcomp) {
		icalcomponent_free (icalcomp);
		return GNOME_Evolution_Calendar_ObjectNotFound;
	}
        
        cache_comp = e_cal_component_new ();
        e_cal_component_set_icalcomponent (cache_comp, ecalbexcomp->icomp);
	*old_object = e_cal_component_get_as_string (cache_comp);
	g_free (cache_comp);
	
	summary = icalcomponent_get_summary (icalcomp);
	if (!summary)
		summary = "";
	/* Create the cal component */
        new_comp = e_cal_component_new ();
        e_cal_component_set_icalcomponent (new_comp, icalcomp);
        	
	/* Set the last modified time on the component */
        current = icaltime_from_timet (time (NULL), 0);
        e_cal_component_set_last_modified (new_comp, &current);

	/* Set Attachments */
	if (e_cal_component_has_attachments (new_comp)) {
		d(printf ("This task has attachments for modifications\n"));
		attach_body = build_msg (ecalbex, new_comp, summary, &boundary);
		attach_body_crlf = e_cal_backend_exchange_lf_to_crlf (attach_body);
	}
	comp_str = e_cal_component_get_as_string (new_comp);
	icalcomp = icalparser_parse_string (comp_str);
	g_free (comp_str);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_OtherError;

	get_from (backend, new_comp, &from_name, &from_addr);
                                                                                
        props = e2k_properties_new ();

	update_props (new_comp, &props);
	e_cal_component_commit_sequence (new_comp);

        e2kctx = exchange_account_get_context (ecalbex->account);
	status = e2k_context_proppatch (e2kctx, NULL, ecalbexcomp->href, props, FALSE, NULL);
	comp_str = e_cal_component_get_as_string (new_comp);
	icalcomp = icalparser_parse_string (comp_str);
	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)){
		status = put_body(new_comp, e2kctx, NULL, ecalbexcomp->href, from_name, from_addr, 
					attach_body_crlf, boundary, NULL);
		if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
			e_cal_backend_exchange_modify_object (ecalbex, icalcomp, mod);
	}
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
receive_task_objects (ECalBackendSync *backend, EDataCal *cal,
                 const char *calobj)
{
	ECalBackendExchangeTasks *ecalbextask;
        ECalComponent *ecalcomp;
        GList *comps, *l;
        struct icaltimetype current;
        icalproperty_method method;
        icalcomponent *subcomp;
        ECalBackendSyncStatus status = GNOME_Evolution_Calendar_Success;
                                                                                
	ecalbextask = E_CAL_BACKEND_EXCHANGE_TASKS (backend);
	
	g_return_val_if_fail (E_IS_CAL_BACKEND_EXCHANGE_TASKS (ecalbextask),
                                        GNOME_Evolution_Calendar_NoSuchCal);
        g_return_val_if_fail (calobj != NULL,
                                GNOME_Evolution_Calendar_ObjectNotFound);
                                                                                
	if (!e_cal_backend_exchange_is_online (E_CAL_BACKEND_EXCHANGE (backend))) {
		d(printf ("tasks are offline\n"));
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	status = e_cal_backend_exchange_extract_components (calobj, &method, &comps);
        if (status != GNOME_Evolution_Calendar_Success)
                return GNOME_Evolution_Calendar_InvalidObject;

	for (l = comps; l; l = l->next) {
                const char *uid, *rid;
                char *calobj;
                                                                                
                subcomp = l->data;
                                                                                
                ecalcomp = e_cal_component_new ();
                e_cal_component_set_icalcomponent (ecalcomp, subcomp);
                                                                                
                current = icaltime_from_timet (time (NULL), 0);
                e_cal_component_set_created (ecalcomp, &current);
                e_cal_component_set_last_modified (ecalcomp, &current);
                                                                                
                /*sanitize?*/

		e_cal_component_get_uid (ecalcomp, &uid);
                rid = e_cal_component_get_recurid_as_string (ecalcomp);
                                                                                
                /*see if the object is there in the cache. if found, modify object, else create object*/
                                                                                
                if (get_exchange_comp (E_CAL_BACKEND_EXCHANGE (ecalbextask), uid)) {
                        char *old_object;
                        status = modify_task_object (backend, cal, calobj, CALOBJ_MOD_THIS, &old_object, NULL);
                        if (status != GNOME_Evolution_Calendar_Success)
                                goto error;
                                                                                
                        e_cal_backend_notify_object_modified (E_CAL_BACKEND (backend), old_object, calobj);
			g_free (old_object);
                } else {
                        char *returned_uid;
			calobj = (char *) icalcomponent_as_ical_string (subcomp);
			status = create_task_object (backend, cal, &calobj, &returned_uid);
                        if (status != GNOME_Evolution_Calendar_Success)
                                goto error;
                                                                                
                        e_cal_backend_notify_object_created (E_CAL_BACKEND (backend), calobj);
                }
        }
                                                                                
        g_list_free (comps);
error:
        return status;
}

static ECalBackendSyncStatus
remove_task_object (ECalBackendSync *backend, EDataCal *cal,
	       const char *uid, const char *rid, CalObjModType mod,
	       char **old_object, char **object)
{
	ECalBackendExchange *ecalbex = E_CAL_BACKEND_EXCHANGE (backend);
	ECalBackendExchangeComponent *ecalbexcomp;
	ECalComponent *comp;
	E2kContext *ctx;
	int status;
	
	g_return_val_if_fail (E_IS_CAL_BACKEND_EXCHANGE (ecalbex), 
					GNOME_Evolution_Calendar_OtherError);

	if (!e_cal_backend_exchange_is_online (E_CAL_BACKEND_EXCHANGE (backend))) {
		d(printf ("tasks are offline\n"));
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	ecalbexcomp = get_exchange_comp (ecalbex, uid);
	if (!ecalbexcomp && !ecalbexcomp->href)
		return GNOME_Evolution_Calendar_ObjectNotFound;		

        comp = e_cal_component_new ();
        e_cal_component_set_icalcomponent (comp, ecalbexcomp->icomp);
	*old_object = e_cal_component_get_as_string (comp);

	ctx = exchange_account_get_context (ecalbex->account);

	status = e2k_context_delete (ctx, NULL, ecalbexcomp->href);
	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		if (e_cal_backend_exchange_remove_object (ecalbex, uid))
			return GNOME_Evolution_Calendar_Success;
	}
	return GNOME_Evolution_Calendar_OtherError;
}
 
static void
init (ECalBackendExchangeTasks *cbext)
{
	cbext->priv = g_new0 (ECalBackendExchangeTasksPrivate, 1);
}

static void
dispose (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	ECalBackendExchangeTasks *cbext =
		E_CAL_BACKEND_EXCHANGE_TASKS (object);

	g_free (cbext->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
class_init (ECalBackendExchangeTasksClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ECalBackendSyncClass *sync_class = E_CAL_BACKEND_SYNC_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	sync_class->open_sync = open_task;
	sync_class->create_object_sync = create_task_object;
	sync_class->modify_object_sync = modify_task_object;
	sync_class->remove_object_sync = remove_task_object;
	sync_class->receive_objects_sync = receive_task_objects;

	object_class->dispose = dispose;
	object_class->finalize = finalize;
}

E2K_MAKE_TYPE (e_cal_backend_exchange_tasks, ECalBackendExchangeTasks, class_init, init, PARENT_TYPE)
