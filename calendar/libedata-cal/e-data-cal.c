/* Evolution calendar client interface object
 *
 * Copyright (C) 2000 Ximian, Inc.
 * Copyright (C) 2000 Ximian, Inc.
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          Rodrigo Moya <rodrigo@ximian.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libical/ical.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-exception.h>
#include <libedata-cal/e-cal-backend.h>
#include "e-data-cal.h"

#define PARENT_TYPE         BONOBO_TYPE_OBJECT

static BonoboObjectClass *parent_class;

/* Private part of the Cal structure */
struct _EDataCalPrivate {
	/* Our backend */
	ECalBackend *backend;

	/* Listener on the client we notify */
	GNOME_Evolution_Calendar_CalListener listener;

	/* Cache of live queries */
	GHashTable *live_queries;
};

/* Cal::get_uri method */
static CORBA_char *
impl_Cal_get_uri (PortableServer_Servant servant,
		  CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;
	const char *str_uri;
	CORBA_char *str_uri_copy;

	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	str_uri = e_cal_backend_get_uri (priv->backend);
	str_uri_copy = CORBA_string_dup (str_uri);

	return str_uri_copy;
}

static void
impl_Cal_open (PortableServer_Servant servant,
	       CORBA_boolean only_if_exists,
	       const CORBA_char *username,
	       const CORBA_char *password,
	       CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;
	
	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	e_cal_backend_open (priv->backend, cal, only_if_exists, username, password);
}

static void
impl_Cal_remove (PortableServer_Servant servant,
		 CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;
	
	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	e_cal_backend_remove (priv->backend, cal);
}

/* Cal::isReadOnly method */
static void
impl_Cal_isReadOnly (PortableServer_Servant servant,
		     CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;

	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	e_cal_backend_is_read_only (priv->backend, cal);
}
		       
/* Cal::getEmailAddress method */
static void
impl_Cal_getCalAddress (PortableServer_Servant servant,
			CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;

	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	e_cal_backend_get_cal_address (priv->backend, cal);
}
		       
/* Cal::get_alarm_email_address method */
static void
impl_Cal_getAlarmEmailAddress (PortableServer_Servant servant,
			       CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;

	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	e_cal_backend_get_alarm_email_address (priv->backend, cal);
}
		       
/* Cal::get_ldap_attribute method */
static void
impl_Cal_getLdapAttribute (PortableServer_Servant servant,
			   CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;

	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	e_cal_backend_get_ldap_attribute (priv->backend, cal);
}

/* Cal::getSchedulingInformation method */
static void
impl_Cal_getStaticCapabilities (PortableServer_Servant servant,
				CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;

	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	e_cal_backend_get_static_capabilities (priv->backend, cal);
}

/* Cal::setMode method */
static void
impl_Cal_setMode (PortableServer_Servant servant,
		  GNOME_Evolution_Calendar_CalMode mode,
		  CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;
	
	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	e_cal_backend_set_mode (priv->backend, mode);
}

static void
impl_Cal_getDefaultObject (PortableServer_Servant servant,
			   CORBA_Environment *ev)
{
 	EDataCal *cal;
 	EDataCalPrivate *priv;
 
 	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
 	priv = cal->priv;
 
 	e_cal_backend_get_default_object (priv->backend, cal);
}

/* Cal::getObject method */
static void
impl_Cal_getObject (PortableServer_Servant servant,
		    const CORBA_char *uid,
		    const CORBA_char *rid,
		    CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;

	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	e_cal_backend_get_object (priv->backend, cal, uid, rid);
}

/* Cal::getObjectList method */
static void
impl_Cal_getObjectList (PortableServer_Servant servant,
			const CORBA_char *sexp,
			CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;
	EDataCalView *query;
	
	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	query = g_hash_table_lookup (priv->live_queries, sexp);
	if (query) {
		GList *matched_objects;

		matched_objects = e_data_cal_view_get_matched_objects (query);
		e_data_cal_notify_object_list (
			cal,
			e_data_cal_view_is_done (query) ? e_data_cal_view_get_done_status (query) : GNOME_Evolution_Calendar_Success,
			matched_objects);

		g_list_free (matched_objects);
	} else
		e_cal_backend_get_object_list (priv->backend, cal, sexp);
}

/* Cal::getChanges method */
static void
impl_Cal_getChanges (PortableServer_Servant servant,
		     const CORBA_char *change_id,
		     CORBA_Environment *ev)
{
       EDataCal *cal;
       EDataCalPrivate *priv;

       cal = E_DATA_CAL (bonobo_object_from_servant (servant));
       priv = cal->priv;

       e_cal_backend_get_changes (priv->backend, cal, change_id);
}

/* Cal::getFreeBusy method */
static void
impl_Cal_getFreeBusy (PortableServer_Servant servant,
		      const GNOME_Evolution_Calendar_UserList *user_list,
		      const GNOME_Evolution_Calendar_Time_t start,
		      const GNOME_Evolution_Calendar_Time_t end,
		      CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;
	GList *users = NULL;

	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	/* convert the CORBA user list to a GList */
	if (user_list) {
		int i;

		for (i = 0; i < user_list->_length; i++)
			users = g_list_append (users, user_list->_buffer[i]);
	}

	/* call the backend's get_free_busy method */
	e_cal_backend_get_free_busy (priv->backend, cal, users, start, end);
}

/* Cal::discardAlarm method */
static void
impl_Cal_discardAlarm (PortableServer_Servant servant,
		       const CORBA_char *uid,
		       const CORBA_char *auid,
		       CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;

	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	e_cal_backend_discard_alarm (priv->backend, cal, uid, auid);
}

static void
impl_Cal_createObject (PortableServer_Servant servant,
		       const CORBA_char *calobj,
		       CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;

	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	e_cal_backend_create_object (priv->backend, cal, calobj);
}

static void
impl_Cal_modifyObject (PortableServer_Servant servant,
		       const CORBA_char *calobj,
		       const GNOME_Evolution_Calendar_CalObjModType mod,
		       CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;

	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	e_cal_backend_modify_object (priv->backend, cal, calobj, mod);
}

/* Cal::removeObject method */
static void
impl_Cal_removeObject (PortableServer_Servant servant,
		       const CORBA_char *uid,
		       const CORBA_char *rid,
		       const GNOME_Evolution_Calendar_CalObjModType mod,
		       CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;

	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	e_cal_backend_remove_object (priv->backend, cal, uid, rid, mod);
}

static void
impl_Cal_receiveObjects (PortableServer_Servant servant,
			 const CORBA_char *calobj,
			 CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;

	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	e_cal_backend_receive_objects (priv->backend, cal, calobj);
}

static void
impl_Cal_sendObjects (PortableServer_Servant servant,
		      const CORBA_char *calobj,
		      CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;

	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	e_cal_backend_send_objects (priv->backend, cal, calobj);
}

/* Cal::getQuery implementation */
static void
impl_Cal_getQuery (PortableServer_Servant servant,
		   const CORBA_char *sexp,
		   GNOME_Evolution_Calendar_CalViewListener ql,
		   CORBA_Environment *ev)
{

	EDataCal *cal;
	EDataCalPrivate *priv;
	EDataCalView *query;
	ECalBackendSExp *obj_sexp;
	
	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	/* first see if we already have the query in the cache */
	query = g_hash_table_lookup (priv->live_queries, sexp);
	if (query) {
		e_data_cal_view_add_listener (query, ql);
		e_data_cal_notify_query (cal, GNOME_Evolution_Calendar_Success, query);
		return;
	}

	/* we handle this entirely here, since it doesn't require any
	   backend involvement now that we have e_cal_view_start to
	   actually kick off the search. */

	obj_sexp = e_cal_backend_sexp_new (sexp);
	if (!obj_sexp) {
		e_data_cal_notify_query (cal, GNOME_Evolution_Calendar_InvalidQuery, NULL);

		return;
	}

	query = e_data_cal_view_new (priv->backend, ql, obj_sexp);
	if (!query) {
		g_object_unref (obj_sexp);
		e_data_cal_notify_query (cal, GNOME_Evolution_Calendar_OtherError, NULL);

		return;
	}

	g_hash_table_insert (priv->live_queries, g_strdup (sexp), query);
	e_cal_backend_add_query (priv->backend, query);

	e_data_cal_notify_query (cal, GNOME_Evolution_Calendar_Success, query);
}


/* Cal::getTimezone method */
static void
impl_Cal_getTimezone (PortableServer_Servant servant,
		      const CORBA_char *tzid,
		      CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;

	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	e_cal_backend_get_timezone (priv->backend, cal, tzid);
}

/* Cal::addTimezone method */
static void
impl_Cal_addTimezone (PortableServer_Servant servant,
		      const CORBA_char *tz,
		      CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;

	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	e_cal_backend_add_timezone (priv->backend, cal, tz);
}

/* Cal::setDefaultTimezone method */
static void
impl_Cal_setDefaultTimezone (PortableServer_Servant servant,
			     const CORBA_char *tzid,
			     CORBA_Environment *ev)
{
	EDataCal *cal;
	EDataCalPrivate *priv;

	cal = E_DATA_CAL (bonobo_object_from_servant (servant));
	priv = cal->priv;

	e_cal_backend_set_default_timezone (priv->backend, cal, tzid);
}

/**
 * cal_construct:
 * @cal: A calendar client interface.
 * @corba_cal: CORBA object for the calendar.
 * @backend: Calendar backend that this @cal presents an interface to.
 * @listener: Calendar listener for notification.
 *
 * Constructs a calendar client interface object by binding the corresponding
 * CORBA object to it.  The calendar interface is bound to the specified
 * @backend, and will notify the @listener about changes to the calendar.
 *
 * Return value: The same object as the @cal argument.
 **/
EDataCal *
e_data_cal_construct (EDataCal *cal,
		      ECalBackend *backend,
		      GNOME_Evolution_Calendar_CalListener listener)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_val_if_fail (cal != NULL, NULL);
	g_return_val_if_fail (E_IS_DATA_CAL (cal), NULL);
	g_return_val_if_fail (backend != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), NULL);

	priv = cal->priv;

	CORBA_exception_init (&ev);
	priv->listener = CORBA_Object_duplicate (listener, &ev);
	if (BONOBO_EX (&ev)) {
		g_message ("cal_construct: could not duplicate the listener");
		priv->listener = CORBA_OBJECT_NIL;
		CORBA_exception_free (&ev);
		return NULL;
	}

	CORBA_exception_free (&ev);

	priv->backend = backend;
	
	return cal;
}

/**
 * cal_new:
 * @backend: A calendar backend.
 * @listener: A calendar listener.
 *
 * Creates a new calendar client interface object and binds it to the
 * specified @backend and @listener objects.
 *
 * Return value: A newly-created #EDataCal calendar client interface
 * object, or %NULL if its corresponding CORBA object could not be
 * created.
 **/
EDataCal *
e_data_cal_new (ECalBackend *backend, GNOME_Evolution_Calendar_CalListener listener)
{
	EDataCal *cal, *retval;

	g_return_val_if_fail (backend != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), NULL);

	cal = E_DATA_CAL (g_object_new (E_TYPE_DATA_CAL, 
				 "poa", bonobo_poa_get_threaded (ORBIT_THREAD_HINT_PER_REQUEST, NULL),
				 NULL));

	retval = e_data_cal_construct (cal, backend, listener);
	if (!retval) {
		g_message (G_STRLOC ": could not construct the calendar client interface");
		bonobo_object_unref (BONOBO_OBJECT (cal));
		return NULL;
	}

	return retval;
}

ECalBackend *
e_data_cal_get_backend (EDataCal *cal)
{
	g_return_val_if_fail (cal != NULL, NULL);
	g_return_val_if_fail (E_IS_DATA_CAL (cal), NULL);

	return cal->priv->backend;
}

GNOME_Evolution_Calendar_CalListener
e_data_cal_get_listener (EDataCal *cal)
{
	g_return_val_if_fail (cal != NULL, NULL);
	g_return_val_if_fail (E_IS_DATA_CAL (cal), NULL);

	return cal->priv->listener;
}

/* Destroy handler for the calendar */
static void
e_data_cal_finalize (GObject *object)
{
	EDataCal *cal;
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_DATA_CAL (object));

	cal = E_DATA_CAL (object);
	priv = cal->priv;

	priv->backend = NULL;
	
	CORBA_exception_init (&ev);
	bonobo_object_release_unref (priv->listener, &ev);
	if (BONOBO_EX (&ev))
		g_message (G_STRLOC ": could not release the listener");

	priv->listener = NULL;
	CORBA_exception_free (&ev);

	g_hash_table_destroy (priv->live_queries);
	priv->live_queries = NULL;

	g_free (priv);

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}



/* Class initialization function for the calendar */
static void
e_data_cal_class_init (EDataCalClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;
	POA_GNOME_Evolution_Calendar_Cal__epv *epv = &klass->epv;

	parent_class = g_type_class_peek_parent (klass);

	/* Class method overrides */
	object_class->finalize = e_data_cal_finalize;

	/* Epv methods */
	epv->_get_uri = impl_Cal_get_uri;
	epv->open = impl_Cal_open;
	epv->remove = impl_Cal_remove;
	epv->isReadOnly = impl_Cal_isReadOnly;
	epv->getCalAddress = impl_Cal_getCalAddress;
 	epv->getAlarmEmailAddress = impl_Cal_getAlarmEmailAddress;
 	epv->getLdapAttribute = impl_Cal_getLdapAttribute;
 	epv->getStaticCapabilities = impl_Cal_getStaticCapabilities;
	epv->setMode = impl_Cal_setMode;
	epv->getDefaultObject = impl_Cal_getDefaultObject;
	epv->getObject = impl_Cal_getObject;
	epv->getTimezone = impl_Cal_getTimezone;
	epv->addTimezone = impl_Cal_addTimezone;
	epv->setDefaultTimezone = impl_Cal_setDefaultTimezone;
	epv->getObjectList = impl_Cal_getObjectList;
	epv->getChanges = impl_Cal_getChanges;
	epv->getFreeBusy = impl_Cal_getFreeBusy;
	epv->discardAlarm = impl_Cal_discardAlarm;
	epv->createObject = impl_Cal_createObject;
	epv->modifyObject = impl_Cal_modifyObject;
	epv->removeObject = impl_Cal_removeObject;
	epv->receiveObjects = impl_Cal_receiveObjects;
	epv->sendObjects = impl_Cal_sendObjects;
	epv->getQuery = impl_Cal_getQuery;
}


/* Object initialization function for the calendar */
static void
e_data_cal_init (EDataCal *cal, EDataCalClass *klass)
{
	EDataCalPrivate *priv;

	priv = g_new0 (EDataCalPrivate, 1);
	cal->priv = priv;

	priv->listener = CORBA_OBJECT_NIL;
	priv->live_queries = g_hash_table_new_full (g_str_hash, g_str_equal,
						    (GDestroyNotify) g_free,
						    (GDestroyNotify) g_object_unref);
}

BONOBO_TYPE_FUNC_FULL (EDataCal, GNOME_Evolution_Calendar_Cal, PARENT_TYPE, e_data_cal);

void 
e_data_cal_notify_read_only (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status, gboolean read_only)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyReadOnly (priv->listener, status, read_only, &ev);

	if (BONOBO_EX (&ev))
		g_message (G_STRLOC ": could not notify the listener of read only");

	CORBA_exception_free (&ev);	
}

void 
e_data_cal_notify_cal_address (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status, const char *address)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyCalAddress (priv->listener, status, address ? address : "", &ev);

	if (BONOBO_EX (&ev))
		g_message (G_STRLOC ": could not notify the listener of cal address");

	CORBA_exception_free (&ev);	
}

void
e_data_cal_notify_alarm_email_address (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status, const char *address)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyAlarmEmailAddress (priv->listener, status, address ? address : "", &ev);

	if (BONOBO_EX (&ev))
		g_message (G_STRLOC ": could not notify the listener of alarm address");

	CORBA_exception_free (&ev);
}

void
e_data_cal_notify_ldap_attribute (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status, const char *attribute)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyLDAPAttribute (priv->listener, status, attribute ? attribute : "", &ev);

	if (BONOBO_EX (&ev))
		g_message (G_STRLOC ": could not notify the listener of ldap attribute");

	CORBA_exception_free (&ev);
}

void
e_data_cal_notify_static_capabilities (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status, const char *capabilities)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyStaticCapabilities (priv->listener, status,
								    capabilities ? capabilities : "", &ev);

	if (BONOBO_EX (&ev))
		g_message (G_STRLOC ": could not notify the listener of static capabilities");

	CORBA_exception_free (&ev);
}

void 
e_data_cal_notify_open (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyCalOpened (priv->listener, status, &ev);

	if (BONOBO_EX (&ev))
		g_message (G_STRLOC ": could not notify the listener of open");

	CORBA_exception_free (&ev);
}

void
e_data_cal_notify_remove (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyCalRemoved (priv->listener, status, &ev);

	if (BONOBO_EX (&ev))
		g_message (G_STRLOC ": could not notify the listener of remove");

	CORBA_exception_free (&ev);
}

void
e_data_cal_notify_object_created (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status,
				  const char *uid, const char *object)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	if (status == GNOME_Evolution_Calendar_Success)
		e_cal_backend_notify_object_created (priv->backend, object);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyObjectCreated (priv->listener, status, uid ? uid : "", &ev);

	if (BONOBO_EX (&ev))
		g_message (G_STRLOC ": could not notify the listener of object creation");

	CORBA_exception_free (&ev);
}

void
e_data_cal_notify_object_modified (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status, 
				   const char *old_object, const char *object)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	if (status == GNOME_Evolution_Calendar_Success)
		e_cal_backend_notify_object_modified (priv->backend, old_object, object);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyObjectModified (priv->listener, status, &ev);

	if (BONOBO_EX (&ev))
		g_message (G_STRLOC ": could not notify the listener of object creation");

	CORBA_exception_free (&ev);
}

void
e_data_cal_notify_object_removed (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status, 
				  const char *uid, const char *old_object, const char *object)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	if (status == GNOME_Evolution_Calendar_Success)
		e_cal_backend_notify_object_removed (priv->backend, uid, old_object, object);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyObjectRemoved (priv->listener, status, &ev);

	if (BONOBO_EX (&ev))
		g_message (G_STRLOC ": could not notify the listener of object removal");

	CORBA_exception_free (&ev);
}

void
e_data_cal_notify_objects_received (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyObjectsReceived (priv->listener, status, &ev);

	if (BONOBO_EX (&ev))
		g_message (G_STRLOC ": could not notify the listener of objects received");

	CORBA_exception_free (&ev);
}

void
e_data_cal_notify_alarm_discarded (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyAlarmDiscarded (priv->listener, status, &ev);

	if (BONOBO_EX (&ev))
		g_message (G_STRLOC ": could not notify the listener of alarm discarded");

	CORBA_exception_free (&ev);	
}

void
e_data_cal_notify_objects_sent (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status, GList *users, const char *calobj)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;
	GNOME_Evolution_Calendar_UserList *corba_users;

	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	corba_users = GNOME_Evolution_Calendar_UserList__alloc ();
	corba_users->_length = g_list_length (users);
	if (users) {
		GList *l;
		int n;

		corba_users->_buffer = CORBA_sequence_GNOME_Evolution_Calendar_User_allocbuf (corba_users->_length);
		for (l = users, n = 0; l != NULL; l = l->next, n++)
			corba_users->_buffer[n] = CORBA_string_dup (l->data);
	}

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyObjectsSent (priv->listener, status, corba_users,
								calobj ? calobj : "", &ev);

	if (BONOBO_EX (&ev))
		g_message (G_STRLOC ": could not notify the listener of objects sent");

	CORBA_exception_free (&ev);
	CORBA_free (corba_users);
}

void
e_data_cal_notify_default_object (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status, const char *object)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;
	
	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_CalListener_notifyDefaultObjectRequested (priv->listener, status, 
									   object ? object : "", &ev);

	if (BONOBO_EX (&ev))
		g_message (G_STRLOC ": could not notify the listener of default object");

	CORBA_exception_free (&ev);
}

void
e_data_cal_notify_object (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status, const char *object)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;
	
	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_CalListener_notifyObjectRequested (priv->listener, status,
								    object ? object : "", &ev);

	if (BONOBO_EX (&ev))
		g_message (G_STRLOC ": could not notify the listener of object");

	CORBA_exception_free (&ev);
}

void
e_data_cal_notify_object_list (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status, GList *objects)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;
	GNOME_Evolution_Calendar_stringlist seq;
	GList *l;
	int i;
	
	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);

	seq._maximum = g_list_length (objects);
	seq._length = 0;
	seq._buffer = GNOME_Evolution_Calendar_stringlist_allocbuf (seq._maximum);

	for (l = objects, i = 0; l; l = l->next, i++) {
		seq._buffer[i] = CORBA_string_dup (l->data);
		seq._length++;
	}

	GNOME_Evolution_Calendar_CalListener_notifyObjectListRequested (priv->listener, status, &seq, &ev);

	if (BONOBO_EX (&ev))
		g_message (G_STRLOC ": could not notify the listener of object list");

	CORBA_exception_free (&ev);

	CORBA_free(seq._buffer);
}

void
e_data_cal_notify_query (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status, EDataCalView *query)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyQuery (priv->listener, status, BONOBO_OBJREF (query), &ev);

	if (BONOBO_EX (&ev))
		g_message (G_STRLOC ": could not notify the listener of query");

	CORBA_exception_free (&ev);	
}

void
e_data_cal_notify_timezone_requested (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status, const char *object)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyTimezoneRequested (priv->listener, status, object ? object : "", &ev);

	if (BONOBO_EX (&ev))
		g_warning (G_STRLOC ": could not notify the listener of timezone requested");

	CORBA_exception_free (&ev);
}

void
e_data_cal_notify_timezone_added (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status, const char *tzid)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyTimezoneAdded (priv->listener, status, tzid ? tzid : "", &ev);

	if (BONOBO_EX (&ev))
		g_warning (G_STRLOC ": could not notify the listener of timezone added");

	CORBA_exception_free (&ev);
}

void
e_data_cal_notify_default_timezone_set (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyDefaultTimezoneSet (priv->listener, status, &ev);

	if (BONOBO_EX (&ev))
		g_warning (G_STRLOC ": could not notify the listener of default timezone set");

	CORBA_exception_free (&ev);
}

void
e_data_cal_notify_changes (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status, 
			   GList *adds, GList *modifies, GList *deletes)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;
	GNOME_Evolution_Calendar_CalObjChangeSeq seq;
	GList *l;	
	int n, i;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	n = g_list_length (adds) + g_list_length (modifies) + g_list_length (deletes);
	seq._maximum = n;
	seq._length = n;
	seq._buffer = CORBA_sequence_GNOME_Evolution_Calendar_CalObjChange_allocbuf (n);

	i = 0;
	for (l = adds; l; i++, l = l->next) {
		GNOME_Evolution_Calendar_CalObjChange *change = &seq._buffer[i];
		
		change->calobj = CORBA_string_dup (l->data);
		change->type = GNOME_Evolution_Calendar_ADDED;
	}

	for (l = modifies; l; i++, l = l->next) {
		GNOME_Evolution_Calendar_CalObjChange *change = &seq._buffer[i];

		change->calobj = CORBA_string_dup (l->data);
		change->type = GNOME_Evolution_Calendar_MODIFIED;
	}

	for (l = deletes; l; i++, l = l->next) {
		GNOME_Evolution_Calendar_CalObjChange *change = &seq._buffer[i];

		change->calobj = CORBA_string_dup (l->data);
		change->type = GNOME_Evolution_Calendar_DELETED;
	}
	
	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyChanges (priv->listener, status, &seq, &ev);

	CORBA_free (seq._buffer);

	if (BONOBO_EX (&ev))
		g_warning (G_STRLOC ": could not notify the listener of default timezone set");

	CORBA_exception_free (&ev);
}

void
e_data_cal_notify_free_busy (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status, GList *freebusy)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;
	GNOME_Evolution_Calendar_CalObjSeq seq;
	GList *l;
	int n, i;
	
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	n = g_list_length (freebusy);
	seq._maximum = n;
	seq._length = n;
	seq._buffer = CORBA_sequence_GNOME_Evolution_Calendar_CalObj_allocbuf (n);

	for (i = 0, l = freebusy; l; i++, l = l->next)
		seq._buffer[i] = CORBA_string_dup (l->data);
	
	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyFreeBusy (priv->listener, status, &seq, &ev);

	CORBA_free (seq._buffer);

	if (BONOBO_EX (&ev))
		g_warning (G_STRLOC ": could not notify the listener of freebusy");

	CORBA_exception_free (&ev);
}

/**
 * e_data_cal_notify_mode:
 * @cal: A calendar client interface.
 * @status: Status of the mode set.
 * @mode: The current mode.
 * 
 * Notifys the listener of the results of a setMode call.
 **/
void
e_data_cal_notify_mode (EDataCal *cal,
			GNOME_Evolution_Calendar_CalListener_SetModeStatus status,
			GNOME_Evolution_Calendar_CalMode mode)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyCalSetMode (priv->listener, status, mode, &ev);

	if (BONOBO_EX (&ev))
		g_message ("e_data_cal_notify_mode(): could not notify the listener "
			   "about a mode change");

	CORBA_exception_free (&ev);	
}

/**
 * e_data_cal_notify_error
 * @cal: A calendar client interface.
 * @message: Error message.
 *
 * Notify a calendar client of an error occurred in the backend.
 */
void
e_data_cal_notify_error (EDataCal *cal, const char *message)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));
	g_return_if_fail (message != NULL);

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyErrorOccurred (priv->listener, (char *) message, &ev);

	if (BONOBO_EX (&ev))
		g_message ("e_data_cal_notify_remove(): could not notify the listener "
			   "about a removed object");

	CORBA_exception_free (&ev);
}

/**
 * e_data_cal_notify_categories_changed:
 * @cal: A calendar client interface.
 * @categories: List of categories.
 * 
 * Notifies a listener attached to a calendar client interface object about the
 * current set of categories in a calendar backend.
 **/
void
e_data_cal_notify_categories_changed (EDataCal *cal, GNOME_Evolution_Calendar_StringSeq *categories)
{
	EDataCalPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));
	g_return_if_fail (categories != NULL);

	priv = cal->priv;
	g_return_if_fail (priv->listener != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_CalListener_notifyCategoriesChanged (priv->listener, categories, &ev);

	if (BONOBO_EX (&ev))
		g_message ("e_data_cal_notify_categories_changed(): Could not notify the listener "
			   "about the current set of categories");

	CORBA_exception_free (&ev);
}
