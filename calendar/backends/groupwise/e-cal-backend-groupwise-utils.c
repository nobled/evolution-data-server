/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Authors : 
 *  JP Rosevear <jpr@ximian.com>
 *  Rodrigo Moya <rodrigo@ximian.com>
 *
 * Copyright 2003, Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 2 of the GNU General Public 
 * License as published by the Free Software Foundation.
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

#include <string.h>
#include <e-gw-connection.h>
#include <e-gw-message.h>
#include "e-cal-backend-groupwise-utils.h"

static EGwItem *
set_properties_from_cal_component (EGwItem *item, ECalComponent *comp)
{
	const char *uid, *location;
	ECalComponentDateTime dt;
	ECalComponentClassification classif;
	ECalComponentTransparency transp;
	ECalComponentText text;
	int *priority;
	GSList *slist, *sl;
	icalproperty *prop;

	/* first set specific properties */
	switch (e_cal_component_get_vtype (comp)) {
	case E_CAL_COMPONENT_EVENT :
		e_gw_item_set_item_type (item, E_GW_ITEM_TYPE_APPOINTMENT);

		/* transparency */
		e_cal_component_get_transparency (comp, &transp);
		if (transp == E_CAL_COMPONENT_TRANSP_OPAQUE)
			e_gw_item_set_accept_level (item, E_GW_ITEM_ACCEPT_LEVEL_BUSY);
		else
			e_gw_item_set_accept_level (item, NULL);

		/* location */
		e_cal_component_get_location (comp, &location);
		e_gw_item_set_place (item, location);

		/* alarms */
		if (e_cal_component_has_alarms (comp)) {
			ECalComponentAlarm *alarm;
			ECalComponentAlarmTrigger trigger;
			int duration;
			GList *l = e_cal_component_get_alarm_uids (comp);

			alarm = e_cal_component_get_alarm (comp, l->data);
			e_cal_component_alarm_get_trigger (alarm, &trigger);
			duration = abs (icaldurationtype_as_int (trigger.u.rel_duration));
			e_gw_item_set_trigger (item, duration);
		}
		
		/* get_attendee_list from cal comp and convert into
		 * egwitemrecipient and set it on recipient_list*/
		if (e_cal_component_has_attendees (comp)) {
			GSList *attendee_list, *recipient_list = NULL, *al;

		 	e_cal_component_get_attendee_list (comp, &attendee_list);	
			for (al = attendee_list; al != NULL; al = al->next) {
				ECalComponentAttendee *attendee = (ECalComponentAttendee *) al->data;
				EGwItemRecipient *recipient = g_new0 (EGwItemRecipient, 1);
				
				/* len (MAILTO:) + 1 = 7 */
				recipient->email = g_strdup (attendee->value + 7);
				if (attendee->cn != NULL)
					recipient->display_name = g_strdup (attendee->cn);
				if (attendee->role == ICAL_ROLE_REQPARTICIPANT) 
					recipient->type = E_GW_ITEM_RECIPIENT_TO;
				else if (attendee->role == ICAL_ROLE_OPTPARTICIPANT)
					recipient->type = E_GW_ITEM_RECIPIENT_CC;
				else recipient->type = E_GW_ITEM_RECIPIENT_NONE;

				recipient_list = g_slist_append (recipient_list, recipient);
			}
			e_gw_item_set_recipient_list (item, recipient_list);
		}

		break;

	case E_CAL_COMPONENT_TODO :
		e_gw_item_set_item_type (item, E_GW_ITEM_TYPE_TASK);

		/* due date */
		e_cal_component_get_due (comp, &dt);
		if (dt.value) {
			e_gw_item_set_due_date (item, icaltime_as_timet (*dt.value));
			e_cal_component_free_datetime (&dt);
		}

		/* priority */
		priority = NULL;
		e_cal_component_get_priority (comp, &priority);
		if (priority && *priority) {
			if (*priority >= 7)
				e_gw_item_set_priority (item, E_GW_ITEM_PRIORITY_LOW);
			else if (*priority >= 5)
				e_gw_item_set_priority (item, E_GW_ITEM_PRIORITY_STANDARD);
			else if (*priority >= 3)
				e_gw_item_set_priority (item, E_GW_ITEM_PRIORITY_HIGH);
			else
				e_gw_item_set_priority (item, NULL);

			e_cal_component_free_priority (priority);
		}

		/* completed */
		e_cal_component_get_completed (comp, &dt.value);
		if (dt.value) {
			e_gw_item_set_completed (item, TRUE);
			e_cal_component_free_datetime (&dt);
		} else
			e_gw_item_set_completed (item, FALSE);

		break;

	default :
		g_object_unref (item);
		return NULL;
	}

	/* set common properties */
	/* GW server ID */
	prop = icalcomponent_get_first_property (e_cal_component_get_icalcomponent (comp),
						 ICAL_X_PROPERTY);
	while (prop) {
		const char *x_name, *x_val;

		x_name = icalproperty_get_x_name (prop);
		x_val = icalproperty_get_x (prop);
		if (!strcmp (x_name, "X-EVOLUTION-GROUPWISE-ID")) {
			e_gw_item_set_id (item, x_val);
			break;
		}

		prop = icalcomponent_get_next_property (e_cal_component_get_icalcomponent (comp),
							ICAL_X_PROPERTY);
	}
	
	/* UID */
	e_cal_component_get_uid (comp, &uid);
	e_gw_item_set_icalid (item, uid);

	/* subject */
	e_cal_component_get_summary (comp, &text);
	e_gw_item_set_subject (item, text.value);

	/* description */
	e_cal_component_get_description_list (comp, &slist);
	if (slist) {
		GString *str = g_string_new ("");

		for (sl = slist; sl != NULL; sl = sl->next) {
			ECalComponentText *pt = sl->data;

			if (pt && pt->value)
				str = g_string_append (str, pt->value);
		}

		e_gw_item_set_message (item, (const char *) str->str);

		g_string_free (str, TRUE);
		e_cal_component_free_text_list (slist);
	}

	/* start date */
	e_cal_component_get_dtstart (comp, &dt);
	if (dt.value) {
		e_gw_item_set_start_date (item, icaltime_as_timet (*dt.value));
	} else if (e_gw_item_get_item_type (item) == E_GW_ITEM_TYPE_APPOINTMENT) {
		/* appointments need the start date property */
		g_object_unref (item);
		return NULL;
	}

	/* end date */
	e_cal_component_get_dtend (comp, &dt);
	if (dt.value)
		e_gw_item_set_end_date (item, icaltime_as_timet (*dt.value));

	/* creation date */
	e_cal_component_get_created (comp, &dt.value);
	if (dt.value) {
		e_gw_item_set_creation_date (item, icaltime_as_timet (*dt.value));
		e_cal_component_free_datetime (&dt);
	} else {
		struct icaltimetype itt;

		e_cal_component_get_dtstamp (comp, &itt);
		e_gw_item_set_creation_date (item, icaltime_as_timet (itt));
	}

	/* classification */
	e_cal_component_get_classification (comp, &classif);
	switch (classif) {
	case E_CAL_COMPONENT_CLASS_PUBLIC :
		e_gw_item_set_classification (item, E_GW_ITEM_CLASSIFICATION_PUBLIC);
		break;
	case E_CAL_COMPONENT_CLASS_PRIVATE :
		e_gw_item_set_classification (item, E_GW_ITEM_CLASSIFICATION_PRIVATE);
		break;
	case E_CAL_COMPONENT_CLASS_CONFIDENTIAL :
		e_gw_item_set_classification (item, E_GW_ITEM_CLASSIFICATION_CONFIDENTIAL);
		break;
	default :
		e_gw_item_set_classification (item, NULL);
	}

	return item;
}

EGwItem *
e_gw_item_new_from_cal_component (const char *container, ECalComponent *comp)
{
	EGwItem *item;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);

	item = e_gw_item_new_empty ();
	e_gw_item_set_container_id (item, container);
	
	return set_properties_from_cal_component (item, comp);
}

ECalComponent *
e_gw_item_to_cal_component (EGwItem *item)
{
	ECalComponent *comp;
	ECalComponentText text;
	ECalComponentDateTime dt;
	const char *description;
	time_t t;
	struct icaltimetype itt;
	int priority;
	int alarm_duration;
	GSList *recipient_list, *rl, *attendee_list = NULL;
	EGwItemType item_type;

	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	comp = e_cal_component_new ();

	item_type = e_gw_item_get_item_type (item);

	if (item_type == E_GW_ITEM_TYPE_APPOINTMENT)
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);
	else if (item_type == E_GW_ITEM_TYPE_TASK)
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
	else {
		g_object_unref (comp);
		return NULL;
	}

	/* set common properties */
	/* GW server ID */
	description = e_gw_item_get_id (item);
	if (description) {
		icalproperty *icalprop;

		icalprop = icalproperty_new_x (description);
		icalproperty_set_x_name (icalprop, "X-EVOLUTION-GROUPWISE-ID");
		icalcomponent_add_property (e_cal_component_get_icalcomponent (comp), icalprop);
	}

	/* UID */
	e_cal_component_set_uid (comp, e_gw_item_get_icalid (item));

	/* summary */
	text.value = e_gw_item_get_subject (item);
	text.altrep = NULL;
	e_cal_component_set_summary (comp, &text);

	/* description */
	description = e_gw_item_get_message (item);
	if (description) {
		GSList l;

		text.value = description;
		text.altrep = NULL;
		l.data = &text;
		l.next = NULL;

		e_cal_component_set_description_list (comp, &l);
	}

	/* creation date */
	t = e_gw_item_get_creation_date (item);
	itt = icaltime_from_timet (t, 0);
	e_cal_component_set_created (comp, &itt);
	e_cal_component_set_dtstamp (comp, &itt);

	/* start date */
	t = e_gw_item_get_start_date (item);
	itt = icaltime_from_timet (t, 0);
	dt.value = &itt;
	dt.tzid = g_strdup ("UTC");
	e_cal_component_set_dtstart (comp, &dt);
	g_free (dt.tzid);

	/* end date */
	t = e_gw_item_get_end_date (item);
	itt = icaltime_from_timet (t, 0);
	dt.value = &itt;
	dt.tzid = g_strdup ("UTC");
	e_cal_component_set_dtend (comp, &dt);
	g_free (dt.tzid);

	/* classification */
	description = e_gw_item_get_classification (item);
	if (description) {
		if (strcmp (description, E_GW_ITEM_CLASSIFICATION_PUBLIC) == 0)
			e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_PUBLIC);
		else if (strcmp (description, E_GW_ITEM_CLASSIFICATION_PRIVATE) == 0)
			e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_PRIVATE);
		else if (strcmp (description, E_GW_ITEM_CLASSIFICATION_CONFIDENTIAL) == 0)
			e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_CONFIDENTIAL);
		else
			e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_NONE);
	} else
		e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_NONE);

	/* set specific properties */
	switch (item_type) {
	case E_GW_ITEM_TYPE_APPOINTMENT :
		/* transparency */
		description = e_gw_item_get_accept_level (item);
		if (description &&
		    (!strcmp (description, E_GW_ITEM_ACCEPT_LEVEL_BUSY) ||
		     !strcmp (description, E_GW_ITEM_ACCEPT_LEVEL_OUT_OF_OFFICE)))
			e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_OPAQUE);
		else
			e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_TRANSPARENT);

		/* location */
		e_cal_component_set_location (comp, e_gw_item_get_place (item));

		/* alarms*/
		/* we negate the value as GW supports only "before" the start of event alarms */
		alarm_duration = 0 - e_gw_item_get_trigger (item);
		if (alarm_duration != 0) {
			ECalComponentAlarm *alarm;
			ECalComponentAlarmTrigger trigger;
			
			alarm = e_cal_component_alarm_new ();
			e_cal_component_alarm_set_action (alarm, E_CAL_COMPONENT_ALARM_DISPLAY);
			trigger.type = E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START;
			trigger.u.rel_duration = (struct icaldurationtype) icaldurationtype_from_int (alarm_duration);
			e_cal_component_alarm_set_trigger (alarm, trigger);
			e_cal_component_add_alarm (comp, alarm);
		}

		recipient_list = e_gw_item_get_recipient_list (item);
		if (recipient_list != NULL) {
			for (rl = recipient_list; rl != NULL; rl = rl->next) {
				EGwItemRecipient *recipient = (EGwItemRecipient *) rl->data;
				ECalComponentAttendee *attendee = g_new0 (ECalComponentAttendee, 1);

				attendee->cn = g_strdup (recipient->display_name);
				attendee->value = g_strconcat("MAILTO:", recipient->email, NULL);
				if (recipient->type == E_GW_ITEM_RECIPIENT_TO)
					attendee->role = ICAL_ROLE_REQPARTICIPANT;
				else if (recipient->type == E_GW_ITEM_RECIPIENT_CC)
					attendee->role = ICAL_ROLE_OPTPARTICIPANT;
				else 
					attendee->role = ICAL_ROLE_NONE;
				/* FIXME  needs a server fix on the interface 
				 * for getting cutype and the status */
				attendee->cutype = ICAL_CUTYPE_INDIVIDUAL;
				attendee->status = ICAL_PARTSTAT_NEEDSACTION; 
				attendee_list = g_slist_append (attendee_list, attendee);				
			}

			e_cal_component_set_attendee_list (comp, attendee_list);
		}

		break;
	case E_GW_ITEM_TYPE_TASK :
		/* due date */
		t = e_gw_item_get_due_date (item);
		itt = icaltime_from_timet (t, 0);
		dt.value = &itt;
		dt.tzid = g_strdup ("UTC");
		e_cal_component_set_due (comp, &dt);
		break;

		/* priority */
		description = e_gw_item_get_priority (item);
		if (description) {
			if (!strcmp (description, E_GW_ITEM_PRIORITY_STANDARD))
				priority = 5;
			else if (!strcmp (description, E_GW_ITEM_PRIORITY_HIGH))
				priority = 3;
			else
				priority = 7;
		} else
			priority = 7;

		e_cal_component_set_priority (comp, &priority);

		/* FIXME: EGwItem's completed is a boolean */
		break;
	default :
		return NULL;
	}

	return comp;
}

EGwConnectionStatus
e_gw_connection_send_appointment (EGwConnection *cnc, const char *container, ECalComponent *comp, char **id)
{
	EGwItem *item;
	EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	item = e_gw_item_new_from_cal_component (container, comp);
	e_gw_item_set_container_id (item, container);
	status = e_gw_connection_send_item (cnc, item, id);
	g_object_unref (item);

	return status;
}

static EGwConnectionStatus
start_freebusy_session (EGwConnection *cnc, GList *users, 
               time_t start, time_t end, const char **session)
{
        SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
        SoupSoapParameter *param;
        GList *l;
        icaltimetype icaltime;
	icaltimezone *utc;
        const char *start_date, *end_date;

	if (users == NULL)
                return E_GW_CONNECTION_STATUS_INVALID_OBJECT;

        /* build the SOAP message */
	msg = e_gw_message_new_with_header (e_gw_connection_get_uri (cnc),
					    e_gw_connection_get_session_id (cnc),
					    "startFreeBusySessionRequest");
        /* FIXME users is just a buch of user names - associate it with uid,
         * email id apart from the name*/
        
        soup_soap_message_start_element (msg, "users", NULL, NULL); 
        for ( l = users; l != NULL; l = g_list_next (l)) {
		soup_soap_message_start_element (msg, "user", NULL, NULL); 
                e_gw_message_write_string_parameter (msg, "email", NULL, l->data);
		soup_soap_message_end_element (msg);
        }

        soup_soap_message_end_element (msg);

        /*FIXME check if this needs to be formatted into GW form with separators*/
        /*FIXME  the following code converts time_t to String representation
         * through icaltime. Find if a direct conversion exists.  */ 
        /* Timezone in server is assumed to be UTC */

	utc = icaltimezone_get_utc_timezone ();
	icaltime = icaltime_from_timet_with_zone (start, FALSE, utc);
	start_date = icaltime_as_ical_string (icaltime);
	
	icaltime = icaltime_from_timet_with_zone (end, FALSE, utc);
	end_date = icaltime_as_ical_string (icaltime);
        	
        e_gw_message_write_string_parameter (msg, "startDate", NULL, start_date);
        e_gw_message_write_string_parameter (msg, "endDate", NULL, end_date);
        
	e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
	}

	status = e_gw_connection_parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK)
        {
                g_object_unref (msg);
                g_object_unref (response);
                return status;
        }
        
       	/* if status is OK - parse result, return the list */
        param = soup_soap_response_get_first_parameter_by_name (response, "freeBusySessionId");
        if (!param) {
                g_object_unref (response);
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }
	
	*session = soup_soap_parameter_get_string_value (param); 
        /* free memory */
	g_object_unref (response);
	g_object_unref (msg);

	return status;
}

static EGwConnectionStatus 
close_freebusy_session (EGwConnection *cnc, const char *session)
{
        SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status;

        /* build the SOAP message */
	msg = e_gw_message_new_with_header (e_gw_connection_get_uri (cnc),
					    e_gw_connection_get_session_id (cnc),
					    "closeFreeBusySessionRequest");
       	e_gw_message_write_string_parameter (msg, "freeBusySessionId", NULL, session);
        e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
	}

	status = e_gw_connection_parse_response_status (response);

        g_object_unref (msg);
        g_object_unref (response);
        return status;
}

EGwConnectionStatus
e_gw_connection_get_freebusy_info (EGwConnection *cnc, GList *users, time_t start, time_t end, GList **freebusy)
{
        SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
        SoupSoapParameter *param, *subparam;
        const char *session;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);

        /* Perform startFreeBusySession */
        status = start_freebusy_session (cnc, users, start, end, &session); 
        /*FIXME log error messages  */
        if (status != E_GW_CONNECTION_STATUS_OK)
                return status;

        /* getFreeBusy */
        /* build the SOAP message */
	msg = e_gw_message_new_with_header (e_gw_connection_get_uri (cnc),
					    e_gw_connection_get_session_id (cnc),
					    "getFreeBusyRequest");
       	e_gw_message_write_string_parameter (msg, "freeBusySessionId", NULL, session);
        e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
	}

	status = e_gw_connection_parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
                g_object_unref (msg);
                g_object_unref (response);
                return status;
        }

        /* FIXME  the FreeBusyStats are not used currently.  */
        param = soup_soap_response_get_first_parameter_by_name (response, "freeBusyInfo");
        if (!param) {
                g_object_unref (response);
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }

        for (subparam = soup_soap_parameter_get_first_child_by_name (param, "user");
	     subparam != NULL;
	     subparam = soup_soap_parameter_get_next_child_by_name (subparam, "user")) {
		SoupSoapParameter *param_blocks, *subparam_block, *tmp;
		const char *uuid = NULL, *email = NULL, *name = NULL;
		ECalComponent *comp;
		ECalComponentAttendee attendee;
		GSList *attendee_list = NULL;
		icalcomponent *icalcomp = NULL;
		
		tmp = soup_soap_parameter_get_first_child_by_name (subparam, "email");
		if (tmp)
			email = soup_soap_parameter_get_string_value (tmp);
		tmp = soup_soap_parameter_get_first_child_by_name (subparam, "uuid");
		if (tmp)
			uuid = soup_soap_parameter_get_string_value (tmp);
		tmp = soup_soap_parameter_get_first_child_by_name (subparam, "displayName");
		if (tmp)
			name = soup_soap_parameter_get_string_value (tmp);

		comp = e_cal_component_new ();
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_FREEBUSY); 
		e_cal_component_commit_sequence (comp);
		icalcomp = e_cal_component_get_icalcomponent (comp);
		
		memset (&attendee, 0, sizeof (ECalComponentAttendee));
		if (name)
			attendee.cn = name;
		if (email)
			attendee.value = email;
		
		attendee.cutype = ICAL_CUTYPE_INDIVIDUAL;
		attendee.role = ICAL_ROLE_REQPARTICIPANT;
		attendee.status = ICAL_PARTSTAT_NEEDSACTION;

		/* XXX the uuid is not currently used. hence it is
		 * discarded */

		attendee_list = g_slist_append (attendee_list, &attendee);	

		e_cal_component_set_attendee_list (comp, attendee_list);

		
		param_blocks = soup_soap_parameter_get_first_child_by_name (subparam, "blocks");
		if (!param_blocks) {
			g_object_unref (response);
			g_object_unref (msg);
			return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
		}
        
		for (subparam_block = soup_soap_parameter_get_first_child_by_name (param_blocks, "block");
		     subparam_block != NULL;
		     subparam_block = soup_soap_parameter_get_next_child_by_name (subparam_block, "block")) {

			/* process each block and create ECal free/busy components.*/ 
			SoupSoapParameter *tmp;
			struct icalperiodtype ipt;
			icalproperty *icalprop;
			icaltimetype itt;
			time_t t;
			const char *start, *end, *accept_level;
			
			memset (&ipt, 0, sizeof (struct icalperiodtype));
			tmp = soup_soap_parameter_get_first_child_by_name (subparam_block, "startDate");
			if (tmp) {
				start = soup_soap_parameter_get_string_value (tmp);
				t = e_gw_connection_get_date_from_string (start);
				itt = icaltime_from_timet (t, 0);
				ipt.start = itt;
			}        

			tmp = soup_soap_parameter_get_first_child_by_name (subparam_block, "endDate");
			if (tmp) {
				end = soup_soap_parameter_get_string_value (tmp);
				t = e_gw_connection_get_date_from_string (end);
				itt = icaltime_from_timet (t, 0);
				ipt.end = itt;
			}
			icalprop = icalproperty_new_freebusy (ipt);

			tmp = soup_soap_parameter_get_first_child_by_name (subparam_block, "acceptLevel");
			if (tmp) {
				accept_level = soup_soap_parameter_get_string_value (tmp);
				if (!strcmp (accept_level, "Busy"))
					icalproperty_set_parameter_from_string (icalprop, "FBTYPE", "BUSY");
				else if (!strcmp (accept_level, "Tentative"))
					icalproperty_set_parameter_from_string (icalprop, "FBTYPE", "BUSYTENTATIVE");
				else if (!strcmp (accept_level, "OutOfOffice"))
					icalproperty_set_parameter_from_string (icalprop, "FBTYPE", "BUSYUNAVAILABLE");
				else if (!strcmp (accept_level, "Free"))
					icalproperty_set_parameter_from_string (icalprop, "FBTYPE", "FREE");
							}
			icalcomponent_add_property(icalcomp, icalprop);

		}

		e_cal_component_commit_sequence (comp);
		*freebusy = g_list_append (*freebusy, g_strdup (e_cal_component_get_as_string (comp)));
		g_object_unref (comp);
	}

        g_object_unref (msg);
        g_object_unref (response);

        /* closeFreeBusySession*/
        return close_freebusy_session (cnc, session);
}
