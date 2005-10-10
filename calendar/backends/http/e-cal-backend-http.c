/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - iCalendar http backend
 *
 * Copyright (C) 2003 Novell, Inc.
 *
 * Authors: Hans Petter Jansson <hpj@ximian.com>
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 * Based in part on the file backend.
 */

#include <config.h>
#include <string.h>
#include <unistd.h>
#include <gconf/gconf-client.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-moniker-util.h>
#include <glib/gi18n-lib.h>
#include <libedataserver/e-xml-hash-utils.h>
#include <libecal/e-cal-recur.h>
#include <libecal/e-cal-util.h>
#include <libedata-cal/e-cal-backend-cache.h>
#include <libedata-cal/e-cal-backend-util.h>
#include <libedata-cal/e-cal-backend-sexp.h>
#include <libsoup/soup-session-async.h>
#include <libsoup/soup-uri.h>
#include "e-cal-backend-http.h"



/* Private part of the ECalBackendHttp structure */
struct _ECalBackendHttpPrivate {
	/* URI to get remote calendar data from */
	char *uri;

	/* Local/remote mode */
	CalMode mode;

	/* The file cache */
	ECalBackendCache *cache;

	/* The calendar's default timezone, used for resolving DATE and
	   floating DATE-TIME values. */
	icaltimezone *default_zone;

	/* The list of live queries */
	GList *queries;

	/* Soup handles for remote file */
	SoupSession *soup_session;

	/* Reload */
	guint reload_timeout_id;
	guint is_loading : 1;

	/* Flags */
	gboolean opened;
};



#define d(x)

static void e_cal_backend_http_dispose (GObject *object);
static void e_cal_backend_http_finalize (GObject *object);
static gboolean begin_retrieval_cb (ECalBackendHttp *cbhttp);

static ECalBackendSyncClass *parent_class;



/* Dispose handler for the file backend */
static void
e_cal_backend_http_dispose (GObject *object)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (object);
	priv = cbhttp->priv;

	if (G_OBJECT_CLASS (parent_class)->dispose)
		(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

/* Finalize handler for the file backend */
static void
e_cal_backend_http_finalize (GObject *object)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_HTTP (object));

	cbhttp = E_CAL_BACKEND_HTTP (object);
	priv = cbhttp->priv;

	/* Clean up */

	if (priv->cache) {
		g_object_unref (priv->cache);
		priv->cache = NULL;
	}

	if (priv->uri) {
	        g_free (priv->uri);
		priv->uri = NULL;
	}

	if (priv->soup_session) {
		soup_session_abort (priv->soup_session);
		g_object_unref (priv->soup_session);
		priv->soup_session = NULL;
	}

	g_free (priv);
	cbhttp->priv = NULL;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}



/* Calendar backend methods */

/* Is_read_only handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_is_read_only (ECalBackendSync *backend, EDataCal *cal, gboolean *read_only)
{
	*read_only = TRUE;
	
	return GNOME_Evolution_Calendar_Success;
}

/* Get_email_address handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_get_cal_address (ECalBackendSync *backend, EDataCal *cal, char **address)
{
	/* A HTTP backend has no particular email address associated
	 * with it (although that would be a useful feature some day).
	 */
	*address = NULL;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_http_get_ldap_attribute (ECalBackendSync *backend, EDataCal *cal, char **attribute)
{
	*attribute = NULL;
	
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_http_get_alarm_email_address (ECalBackendSync *backend, EDataCal *cal, char **address)
{
 	/* A HTTP backend has no particular email address associated
 	 * with it (although that would be a useful feature some day).
 	 */
	*address = NULL;
	
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_http_get_static_capabilities (ECalBackendSync *backend, EDataCal *cal, char **capabilities)
{
	*capabilities = g_strdup (CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS);

	return GNOME_Evolution_Calendar_Success;
}

static gchar *
webcal_to_http_method (const gchar *webcal_str)
{
	if (strncmp ("webcal://", webcal_str, sizeof ("webcal://") - 1))
		return g_strdup (webcal_str);

	return g_strconcat ("http://", webcal_str + sizeof ("webcal://") - 1, NULL);
}

static gboolean
notify_and_remove_from_cache (gpointer key, gpointer value, gpointer user_data)
{
	const char *uid = key;
	const char *calobj = value;
	ECalBackendHttp *cbhttp = E_CAL_BACKEND_HTTP (user_data);
	ECalComponent *comp = e_cal_component_new_from_string (calobj);
	ECalComponentId *id = e_cal_component_get_id (comp);

	e_cal_backend_notify_object_removed (E_CAL_BACKEND (cbhttp), id, calobj, NULL);

	e_cal_component_free_id (comp);
	g_object_unref (comp);

	return TRUE;
}

static void
retrieval_done (SoupMessage *msg, ECalBackendHttp *cbhttp)
{
	ECalBackendHttpPrivate *priv;
	icalcomponent *icalcomp, *subcomp;
	icalcomponent_kind kind;
	const char *newuri;
	char *str;
	GHashTable *old_cache;
	GList *comps_in_cache;

	priv = cbhttp->priv;

	priv->is_loading = FALSE;
	d(g_message ("Retrieval done.\n"));

	/* Handle redirection ourselves */
	if (SOUP_STATUS_IS_REDIRECTION (msg->status_code)) {
		newuri = soup_message_get_header (msg->response_headers,
						  "Location");

		d(g_message ("Redirected to %s\n", newuri));

		if (newuri) {
			g_free (priv->uri);

			priv->uri = webcal_to_http_method (newuri);
			begin_retrieval_cb (cbhttp);
		} else {
			if (!priv->opened) {
				e_cal_backend_notify_error (E_CAL_BACKEND (cbhttp),
							    _("Redirected to Invalid URI"));
			}
		}

		return;
	}

	/* check status code */
	if (!SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		if (!priv->opened) {
			e_cal_backend_notify_error (E_CAL_BACKEND (cbhttp),
						    soup_status_get_phrase (msg->status_code));
		}
		return;
	}

	/* get the calendar from the response */
	str = g_malloc0 (msg->response.length + 1);
	strncpy (str, msg->response.body, msg->response.length);
	icalcomp = icalparser_parse_string (str);
	g_free (str);

	if (!icalcomp) {
		if (!priv->opened)
			e_cal_backend_notify_error (E_CAL_BACKEND (cbhttp), _("Bad file format."));
		return;
	}

	if (icalcomponent_isa (icalcomp) != ICAL_VCALENDAR_COMPONENT) {
		if (!priv->opened)
			e_cal_backend_notify_error (E_CAL_BACKEND (cbhttp), _("Not a calendar."));
		icalcomponent_free (icalcomp);
		return;
	}

	/* Update cache */
	old_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	comps_in_cache = e_cal_backend_cache_get_components (priv->cache);
	while (comps_in_cache != NULL) {
		const char *uid;
		ECalComponent *comp = comps_in_cache->data;

		e_cal_component_get_uid (comp, &uid);
		g_hash_table_insert (old_cache, g_strdup (uid), e_cal_component_get_as_string (comp));

		comps_in_cache = g_list_remove (comps_in_cache, comps_in_cache->data);
		g_object_unref (comp);
	}

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbhttp));
	subcomp = icalcomponent_get_first_component (icalcomp, ICAL_ANY_COMPONENT);
	e_file_cache_freeze_changes (E_FILE_CACHE (priv->cache));
	while (subcomp) {
		ECalComponent *comp;
		icalcomponent_kind subcomp_kind;
		icalproperty *prop = NULL;

		subcomp_kind = icalcomponent_isa (subcomp);
		prop = icalcomponent_get_first_property (subcomp, ICAL_UID_PROPERTY);
		if (!prop) {
			g_warning (" The component does not have the  mandatory property UID \n");
			subcomp = icalcomponent_get_next_component (icalcomp, kind);
			continue;
		}

		if (subcomp_kind == kind) {
			comp = e_cal_component_new ();
			if (e_cal_component_set_icalcomponent (comp, subcomp)) {
				const char *uid, *orig_key, *orig_value;

				e_cal_backend_cache_put_component (priv->cache, comp);

				e_cal_component_get_uid (comp, &uid);
				if (g_hash_table_lookup_extended (old_cache, uid, (void **)&orig_key, (void **)&orig_value)) {
					e_cal_backend_notify_object_modified (E_CAL_BACKEND (cbhttp),
									      orig_value,
									      icalcomponent_as_ical_string (subcomp));
					g_hash_table_remove (old_cache, uid);
				} else {
					e_cal_backend_notify_object_created (E_CAL_BACKEND (cbhttp),
									     icalcomponent_as_ical_string (subcomp));
				}
			}

			g_object_unref (comp);
		} else if (subcomp_kind == ICAL_VTIMEZONE_COMPONENT) {
			icaltimezone *zone;

			zone = icaltimezone_new ();
			icaltimezone_set_component (zone, subcomp);
			e_cal_backend_cache_put_timezone (priv->cache, (const icaltimezone *) zone);

			icaltimezone_free (zone, 1);
		}

		subcomp = icalcomponent_get_next_component (icalcomp, kind);
	}

	e_file_cache_thaw_changes (E_FILE_CACHE (priv->cache));

	/* notify the removals */
	g_hash_table_foreach_remove (old_cache, (GHRFunc) notify_and_remove_from_cache, cbhttp);
	g_hash_table_destroy (old_cache);

	/* free memory */
	icalcomponent_free (icalcomp);

	d(g_message ("Retrieval really done.\n"));
}

static gboolean reload_cb                  (ECalBackendHttp *cbhttp);
static void     maybe_start_reload_timeout (ECalBackendHttp *cbhttp);

static gboolean
begin_retrieval_cb (ECalBackendHttp *cbhttp)
{
	ECalBackendHttpPrivate *priv;
	SoupMessage *soup_message;

	priv = cbhttp->priv;

	if (priv->mode != CAL_MODE_REMOTE)
		return TRUE;

	maybe_start_reload_timeout (cbhttp);

	d(g_message ("Starting retrieval...\n"));

	if (priv->is_loading)
		return FALSE;

	priv->is_loading = TRUE;

	/* create the Soup session if not already created */
	if (!priv->soup_session) {
		GConfClient *conf_client;

		priv->soup_session = soup_session_async_new ();

		/* set the HTTP proxy, if configuration is set to do so */
		conf_client = gconf_client_get_default ();
		if (gconf_client_get_bool (conf_client, "/system/http_proxy/use_http_proxy", NULL)) {
			char *server, *proxy_uri;
			int port;

			server = gconf_client_get_string (conf_client, "/system/http_proxy/host", NULL);
			port = gconf_client_get_int (conf_client, "/system/http_proxy/port", NULL);

			if (server && server[0]) {
				SoupUri *suri;
				if (gconf_client_get_bool (conf_client, "/system/http_proxy/use_authentication", NULL)) {
					char *user, *password;

					user = gconf_client_get_string (conf_client,
									"/system/http_proxy/authentication_user",
									NULL);
					password = gconf_client_get_string (conf_client,
									    "/system/http_proxy/authentication_password",
									    NULL);

					proxy_uri = g_strdup_printf("http://%s:%s@%s:%d", user, password, server, port);

					g_free (user);
					g_free (password);
				} else
					proxy_uri = g_strdup_printf ("http://%s:%d", server, port);

				suri = soup_uri_new (proxy_uri);
				g_object_set (G_OBJECT (priv->soup_session), SOUP_SESSION_PROXY_URI, suri, NULL);

				soup_uri_free (suri);
				g_free (server);
				g_free (proxy_uri);
			}
		}

		g_object_unref (conf_client);
	}

	if (priv->uri == NULL)
		priv->uri = webcal_to_http_method (e_cal_backend_get_uri (E_CAL_BACKEND (cbhttp)));

	/* create message to be sent to server */
	soup_message = soup_message_new (SOUP_METHOD_GET, priv->uri);
	soup_message_add_header (soup_message->request_headers, "User-Agent",
				 "Evolution/" VERSION);
	soup_message_set_flags (soup_message, SOUP_MESSAGE_NO_REDIRECT);

	soup_session_queue_message (priv->soup_session, soup_message,
				    (SoupMessageCallbackFn) retrieval_done, cbhttp);

	d(g_message ("Retrieval started.\n"));
	return FALSE;
}

static gboolean
reload_cb (ECalBackendHttp *cbhttp)
{
	ECalBackendHttpPrivate *priv;

	priv = cbhttp->priv;

	if (priv->is_loading)
		return TRUE;

	d(g_message ("Reload!\n"));

	priv->reload_timeout_id = 0;
	priv->opened = TRUE;
	begin_retrieval_cb (cbhttp);
	return FALSE;
}

static void
maybe_start_reload_timeout (ECalBackendHttp *cbhttp)
{
	ECalBackendHttpPrivate *priv;
	ESource *source;
	const gchar *refresh_str;

	priv = cbhttp->priv;

	d(g_message ("Setting reload timeout.\n"));

	if (priv->reload_timeout_id)
		return;

	source = e_cal_backend_get_source (E_CAL_BACKEND (cbhttp));
	if (!source) {
		g_warning ("Could not get source for ECalBackendHttp reload.");
		return;
	}

	refresh_str = e_source_get_property (source, "refresh");

	priv->reload_timeout_id = g_timeout_add ((refresh_str ? atoi (refresh_str) : 30) * 60000,
						 (GSourceFunc) reload_cb, cbhttp);
}

/* Open handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_open (ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists,
			 const char *username, const char *password)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	
	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	if (!priv->cache) {
		priv->cache = e_cal_backend_cache_new (e_cal_backend_get_uri (E_CAL_BACKEND (backend)));

		if (!priv->cache) {
			e_cal_backend_notify_error (E_CAL_BACKEND(cbhttp), _("Could not create cache file"));
			return GNOME_Evolution_Calendar_OtherError;	
		}
		if (priv->mode == CAL_MODE_LOCAL)
			return GNOME_Evolution_Calendar_Success;

		g_idle_add ((GSourceFunc) begin_retrieval_cb, cbhttp);
	}

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_http_remove (ECalBackendSync *backend, EDataCal *cal)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	
	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	if (!priv->cache)
		return GNOME_Evolution_Calendar_OtherError;

	e_file_cache_remove (E_FILE_CACHE (priv->cache));
	return GNOME_Evolution_Calendar_Success;
}

/* is_loaded handler for the file backend */
static gboolean
e_cal_backend_http_is_loaded (ECalBackend *backend)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	if (!priv->cache)
		return FALSE;

	return TRUE;
}

/* is_remote handler for the http backend */
static CalMode
e_cal_backend_http_get_mode (ECalBackend *backend)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	return priv->mode;
}

/* Set_mode handler for the http backend */
static void
e_cal_backend_http_set_mode (ECalBackend *backend, CalMode mode)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	GNOME_Evolution_Calendar_CalMode set_mode;
	gboolean loaded;
	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	loaded = e_cal_backend_http_is_loaded (backend);
				
	switch (mode) {
		case CAL_MODE_LOCAL:
			priv->mode = mode;
			set_mode = cal_mode_to_corba (mode);
			if (loaded && priv->reload_timeout_id) {
				g_source_remove (priv->reload_timeout_id);
				priv->reload_timeout_id = 0;
			}
			break;
		case CAL_MODE_REMOTE:
		case CAL_MODE_ANY:	
			priv->mode = mode;
			set_mode = cal_mode_to_corba (mode);
			if (loaded) 
				g_idle_add ((GSourceFunc) begin_retrieval_cb, backend);
			break;
		
			priv->mode = CAL_MODE_REMOTE;
			set_mode = GNOME_Evolution_Calendar_MODE_REMOTE;
			break;
		default:
			set_mode = GNOME_Evolution_Calendar_MODE_ANY;
			break;
	}

	if (loaded) {
		
		if (set_mode == GNOME_Evolution_Calendar_MODE_ANY)
			e_cal_backend_notify_mode (backend,
						   GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED,
						   cal_mode_to_corba (priv->mode));
		else
			e_cal_backend_notify_mode (backend,
						   GNOME_Evolution_Calendar_CalListener_MODE_SET,
						   set_mode);
	}
}

static ECalBackendSyncStatus
e_cal_backend_http_get_default_object (ECalBackendSync *backend, EDataCal *cal, char **object)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	icalcomponent *icalcomp;
	icalcomponent_kind kind;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (backend));
	icalcomp = e_cal_util_new_component (kind);
	*object = g_strdup (icalcomponent_as_ical_string (icalcomp));
	icalcomponent_free (icalcomp);

	return GNOME_Evolution_Calendar_Success;
}

/* Get_object_component handler for the http backend */
static ECalBackendSyncStatus
e_cal_backend_http_get_object (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid, char **object)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	ECalComponent *comp;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	g_return_val_if_fail (uid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	if (!priv->cache)
		return GNOME_Evolution_Calendar_ObjectNotFound;

	comp = e_cal_backend_cache_get_component (priv->cache, uid, rid);
	if (!comp)
		return GNOME_Evolution_Calendar_ObjectNotFound;

	*object = e_cal_component_get_as_string (comp);
	g_free (comp);

	return GNOME_Evolution_Calendar_Success;
}

/* Get_timezone_object handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_get_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzid, char **object)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	const icaltimezone *zone;
	icalcomponent *icalcomp;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	g_return_val_if_fail (tzid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	/* first try to get the timezone from the cache */
	zone = e_cal_backend_cache_get_timezone (priv->cache, tzid);
	if (!zone) {
		zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
		if (!zone)
			return GNOME_Evolution_Calendar_ObjectNotFound;
	}

	icalcomp = icaltimezone_get_component ((icaltimezone *)zone);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;

	*object = g_strdup (icalcomponent_as_ical_string (icalcomp));

	return GNOME_Evolution_Calendar_Success;
}

/* Add_timezone handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_add_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzobj)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	icalcomponent *tz_comp;
	icaltimezone *zone;

	cbhttp = (ECalBackendHttp *) backend;

	g_return_val_if_fail (E_IS_CAL_BACKEND_HTTP (cbhttp), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

	priv = cbhttp->priv;

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp)
		return GNOME_Evolution_Calendar_InvalidObject;

	if (icalcomponent_isa (tz_comp) != ICAL_VTIMEZONE_COMPONENT) {
		icalcomponent_free (tz_comp);
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	zone = icaltimezone_new ();
	icaltimezone_set_component (zone, tz_comp);
	e_cal_backend_cache_put_timezone (priv->cache, zone);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_http_set_default_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzid)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;
	
	/* FIXME */
	return GNOME_Evolution_Calendar_Success;
}

/* Get_objects_in_range handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_get_object_list (ECalBackendSync *backend, EDataCal *cal, const char *sexp, GList **objects)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	GList *components, *l;
	ECalBackendSExp *cbsexp;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	if (!priv->cache)
		return GNOME_Evolution_Calendar_NoSuchCal;

	/* process all components in the cache */
	cbsexp = e_cal_backend_sexp_new (sexp);

	*objects = NULL;
	components = e_cal_backend_cache_get_components (priv->cache);
	for (l = components; l != NULL; l = l->next) {
		if (e_cal_backend_sexp_match_comp (cbsexp, E_CAL_COMPONENT (l->data), E_CAL_BACKEND (backend))) {
			*objects = g_list_append (*objects, e_cal_component_get_as_string (l->data));
		}
	}

	g_list_foreach (components, (GFunc) g_object_unref, NULL);
	g_list_free (components);
	g_object_unref (cbsexp);

	return GNOME_Evolution_Calendar_Success;
}

/* get_query handler for the file backend */
static void
e_cal_backend_http_start_query (ECalBackend *backend, EDataCalView *query)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	GList *components, *l, *objects = NULL;
	ECalBackendSExp *cbsexp;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	d(g_message (G_STRLOC ": Starting query (%s)", e_data_cal_view_get_text (query)));

	if (!priv->cache) {
		e_data_cal_view_notify_done (query, GNOME_Evolution_Calendar_NoSuchCal);
		return;
	}

	/* process all components in the cache */
	cbsexp = e_cal_backend_sexp_new (e_data_cal_view_get_text (query));

	objects = NULL;
	components = e_cal_backend_cache_get_components (priv->cache);
	for (l = components; l != NULL; l = l->next) {
		if (e_cal_backend_sexp_match_comp (cbsexp, E_CAL_COMPONENT (l->data), E_CAL_BACKEND (backend))) {
			objects = g_list_append (objects, e_cal_component_get_as_string (l->data));
		}
	}

	e_data_cal_view_notify_objects_added (query, (const GList *) objects);

	g_list_foreach (components, (GFunc) g_object_unref, NULL);
	g_list_free (components);
	g_list_foreach (objects, (GFunc) g_free, NULL);
	g_list_free (objects);
	g_object_unref (cbsexp);

	e_data_cal_view_notify_done (query, GNOME_Evolution_Calendar_Success);
}


static icaltimezone *
resolve_tzid (const char *tzid, gpointer user_data)
{
        icalcomponent *vcalendar_comp = user_data;

        if (!tzid || !tzid[0])
                return NULL;
        else if (!strcmp (tzid, "UTC"))
                return icaltimezone_get_utc_timezone ();

        return icalcomponent_get_timezone (vcalendar_comp, tzid);
}


static gboolean
free_busy_instance (ECalComponent *comp,
                    time_t        instance_start,
                    time_t        instance_end,
                    gpointer      data)
{
        icalcomponent *vfb = data;
        icalproperty *prop;
        icalparameter *param;
        struct icalperiodtype ipt;
        icaltimezone *utc_zone;

        utc_zone = icaltimezone_get_utc_timezone ();

        ipt.start = icaltime_from_timet_with_zone (instance_start, FALSE, utc_zone);
        ipt.end = icaltime_from_timet_with_zone (instance_end, FALSE, utc_zone);
        ipt.duration = icaldurationtype_null_duration ();

        /* add busy information to the vfb component */
        prop = icalproperty_new (ICAL_FREEBUSY_PROPERTY);
        icalproperty_set_freebusy (prop, ipt);

        param = icalparameter_new_fbtype (ICAL_FBTYPE_BUSY);
        icalproperty_add_parameter (prop, param);

        icalcomponent_add_property (vfb, prop);

        return TRUE;
}

static icalcomponent *
create_user_free_busy (ECalBackendHttp *cbhttp, const char *address, const char *cn,
                       time_t start, time_t end)
{
        GList *list = NULL, *l;
        icalcomponent *vfb;
        icaltimezone *utc_zone;
        ECalBackendSExp *obj_sexp;
        ECalBackendHttpPrivate *priv;
        ECalBackendCache *cache;
        char *query, *iso_start, *iso_end;
	
        priv = cbhttp->priv;
        cache = priv->cache;

        /* create the (unique) VFREEBUSY object that we'll return */
        vfb = icalcomponent_new_vfreebusy ();
        if (address != NULL) {
                icalproperty *prop;
                icalparameter *param;

                prop = icalproperty_new_organizer (address);
                if (prop != NULL && cn != NULL) {
                        param = icalparameter_new_cn (cn);
                        icalproperty_add_parameter (prop, param);
                }
                if (prop != NULL)
                        icalcomponent_add_property (vfb, prop);
        }
        utc_zone = icaltimezone_get_utc_timezone ();
        icalcomponent_set_dtstart (vfb, icaltime_from_timet_with_zone (start, FALSE, utc_zone));
        icalcomponent_set_dtend (vfb, icaltime_from_timet_with_zone (end, FALSE, utc_zone));

        /* add all objects in the given interval */
        iso_start = isodate_from_time_t (start);
        iso_end = isodate_from_time_t (end);
        query = g_strdup_printf ("occur-in-time-range? (make-time \"%s\") (make-time \"%s\")",
                                 iso_start, iso_end);
        obj_sexp = e_cal_backend_sexp_new (query);
        g_free (query);
        g_free (iso_start);
        g_free (iso_end);

        if (!obj_sexp)
                return vfb;
        if (!obj_sexp)
                return vfb;

        list = e_cal_backend_cache_get_components(cache);

        for (l = list; l; l = l->next) {
                ECalComponent *comp = l->data;
                icalcomponent *icalcomp, *vcalendar_comp;
                icalproperty *prop;

                icalcomp = e_cal_component_get_icalcomponent (comp);
                if (!icalcomp)
                        continue;

                /* If the event is TRANSPARENT, skip it. */
                prop = icalcomponent_get_first_property (icalcomp,
                                                         ICAL_TRANSP_PROPERTY);
                if (prop) {
                        icalproperty_transp transp_val = icalproperty_get_transp (prop);
                        if (transp_val == ICAL_TRANSP_TRANSPARENT ||
                            transp_val == ICAL_TRANSP_TRANSPARENTNOCONFLICT)
                                continue;
                }

                if (!e_cal_backend_sexp_match_comp (obj_sexp, l->data, E_CAL_BACKEND (cbhttp)))
                        continue;

                vcalendar_comp = icalcomponent_get_parent (icalcomp);
                if (!vcalendar_comp)
                        vcalendar_comp = icalcomp;
                e_cal_recur_generate_instances (comp, start, end,
                                                free_busy_instance,
                                                vfb,
                                                resolve_tzid,
                                                vcalendar_comp,
                                                e_cal_backend_cache_get_default_timezone (cache));
        }
        g_object_unref (obj_sexp);

        return vfb;
}



/* Get_free_busy handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_get_free_busy (ECalBackendSync *backend, EDataCal *cal, GList *users,
				time_t start, time_t end, GList **freebusy)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	gchar *address, *name;
	icalcomponent *vfb;
	char *calobj;
	

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	g_return_val_if_fail (start != -1 && end != -1, GNOME_Evolution_Calendar_InvalidRange);
	g_return_val_if_fail (start <= end, GNOME_Evolution_Calendar_InvalidRange);

	if (!priv->cache)
		return GNOME_Evolution_Calendar_NoSuchCal;

        if (users == NULL) {
		if (e_cal_backend_mail_account_get_default (&address, &name)) {
                        vfb = create_user_free_busy (cbhttp, address, name, start, end);
                        calobj = icalcomponent_as_ical_string (vfb);
                        *freebusy = g_list_append (*freebusy, g_strdup (calobj));
                        icalcomponent_free (vfb);	
                        g_free (address);
                        g_free (name);
		}
	} else {
                GList *l;
                for (l = users; l != NULL; l = l->next ) {
                        address = l->data;
                        if (e_cal_backend_mail_account_is_valid (address, &name)) {
                                vfb = create_user_free_busy (cbhttp, address, name, start, end);
                                calobj = icalcomponent_as_ical_string (vfb);
                                *freebusy = g_list_append (*freebusy, g_strdup (calobj));
                                icalcomponent_free (vfb);
                                g_free (name);
                        }
                }
	}

	return GNOME_Evolution_Calendar_Success;
}

/* Get_changes handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_get_changes (ECalBackendSync *backend, EDataCal *cal, const char *change_id,
				GList **adds, GList **modifies, GList **deletes)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	g_return_val_if_fail (change_id != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	/* FIXME */
	return GNOME_Evolution_Calendar_Success;
}

/* Discard_alarm handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_discard_alarm (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *auid)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	/* FIXME */
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_http_create_object (ECalBackendSync *backend, EDataCal *cal, char **calobj, char **uid)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	
	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	return GNOME_Evolution_Calendar_PermissionDenied;
}

static ECalBackendSyncStatus
e_cal_backend_http_modify_object (ECalBackendSync *backend, EDataCal *cal, const char *calobj, 
				CalObjModType mod, char **old_object, char **new_object)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	return GNOME_Evolution_Calendar_PermissionDenied;
}

/* Remove_object handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_http_remove_object (ECalBackendSync *backend, EDataCal *cal,
				const char *uid, const char *rid,
				CalObjModType mod, char **old_object,
				char **object)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	g_return_val_if_fail (uid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	*old_object = *object = NULL;

	return GNOME_Evolution_Calendar_PermissionDenied;
}

/* Update_objects handler for the file backend. */
static ECalBackendSyncStatus
e_cal_backend_http_receive_objects (ECalBackendSync *backend, EDataCal *cal, const char *calobj)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_InvalidObject);

	return GNOME_Evolution_Calendar_PermissionDenied;
}

static ECalBackendSyncStatus
e_cal_backend_http_send_objects (ECalBackendSync *backend, EDataCal *cal, const char *calobj, GList **users,
				 char **modified_calobj)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	*users = NULL;
	*modified_calobj = NULL;

	return GNOME_Evolution_Calendar_PermissionDenied;
}

static icaltimezone *
e_cal_backend_http_internal_get_default_timezone (ECalBackend *backend)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	if (!priv->cache)
		return NULL;

	return NULL;
}

static icaltimezone *
e_cal_backend_http_internal_get_timezone (ECalBackend *backend, const char *tzid)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	icaltimezone *zone;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	if (!strcmp (tzid, "UTC"))
	        zone = icaltimezone_get_utc_timezone ();
	else {
		zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
	}

	return zone;
}

/* Object initialization function for the file backend */
static void
e_cal_backend_http_init (ECalBackendHttp *cbhttp, ECalBackendHttpClass *class)
{
	ECalBackendHttpPrivate *priv;

	priv = g_new0 (ECalBackendHttpPrivate, 1);
	cbhttp->priv = priv;

	priv->uri = NULL;
	priv->reload_timeout_id = 0;
	priv->opened = FALSE;

	e_cal_backend_sync_set_lock (E_CAL_BACKEND_SYNC (cbhttp), TRUE);
}

/* Class initialization function for the file backend */
static void
e_cal_backend_http_class_init (ECalBackendHttpClass *class)
{
	GObjectClass *object_class;
	ECalBackendClass *backend_class;
	ECalBackendSyncClass *sync_class;

	object_class = (GObjectClass *) class;
	backend_class = (ECalBackendClass *) class;
	sync_class = (ECalBackendSyncClass *) class;

	parent_class = (ECalBackendSyncClass *) g_type_class_peek_parent (class);

	object_class->dispose = e_cal_backend_http_dispose;
	object_class->finalize = e_cal_backend_http_finalize;

	sync_class->is_read_only_sync = e_cal_backend_http_is_read_only;
	sync_class->get_cal_address_sync = e_cal_backend_http_get_cal_address;
 	sync_class->get_alarm_email_address_sync = e_cal_backend_http_get_alarm_email_address;
 	sync_class->get_ldap_attribute_sync = e_cal_backend_http_get_ldap_attribute;
 	sync_class->get_static_capabilities_sync = e_cal_backend_http_get_static_capabilities;
	sync_class->open_sync = e_cal_backend_http_open;
	sync_class->remove_sync = e_cal_backend_http_remove;
	sync_class->create_object_sync = e_cal_backend_http_create_object;
	sync_class->modify_object_sync = e_cal_backend_http_modify_object;
	sync_class->remove_object_sync = e_cal_backend_http_remove_object;
	sync_class->discard_alarm_sync = e_cal_backend_http_discard_alarm;
	sync_class->receive_objects_sync = e_cal_backend_http_receive_objects;
	sync_class->send_objects_sync = e_cal_backend_http_send_objects;
 	sync_class->get_default_object_sync = e_cal_backend_http_get_default_object;
	sync_class->get_object_sync = e_cal_backend_http_get_object;
	sync_class->get_object_list_sync = e_cal_backend_http_get_object_list;
	sync_class->get_timezone_sync = e_cal_backend_http_get_timezone;
	sync_class->add_timezone_sync = e_cal_backend_http_add_timezone;
	sync_class->set_default_timezone_sync = e_cal_backend_http_set_default_timezone;
	sync_class->get_freebusy_sync = e_cal_backend_http_get_free_busy;
	sync_class->get_changes_sync = e_cal_backend_http_get_changes;

	backend_class->is_loaded = e_cal_backend_http_is_loaded;
	backend_class->start_query = e_cal_backend_http_start_query;
	backend_class->get_mode = e_cal_backend_http_get_mode;
	backend_class->set_mode = e_cal_backend_http_set_mode;

	backend_class->internal_get_default_timezone = e_cal_backend_http_internal_get_default_timezone;
	backend_class->internal_get_timezone = e_cal_backend_http_internal_get_timezone;
}


/**
 * e_cal_backend_http_get_type:
 * @void: 
 * 
 * Registers the #ECalBackendHttp class if necessary, and returns the type ID
 * associated to it.
 * 
 * Return value: The type ID of the #ECalBackendHttp class.
 **/
GType
e_cal_backend_http_get_type (void)
{
	static GType e_cal_backend_http_type = 0;

	if (!e_cal_backend_http_type) {
		static GTypeInfo info = {
                        sizeof (ECalBackendHttpClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_cal_backend_http_class_init,
                        NULL, NULL,
                        sizeof (ECalBackendHttp),
                        0,
                        (GInstanceInitFunc) e_cal_backend_http_init
                };
		e_cal_backend_http_type = g_type_register_static (E_TYPE_CAL_BACKEND_SYNC,
								  "ECalBackendHttp", &info, 0);
	}

	return e_cal_backend_http_type;
}
