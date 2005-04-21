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

#include <string.h>
#include <unistd.h>
#include <libedataserver/e-time-utils.h>

#include <libgnomevfs/gnome-vfs-mime-utils.h>

#include <camel/camel-mime-message.h>
#include <camel/camel-multipart.h>
#include <camel/camel-stream-mem.h>
#include <camel/camel-file-utils.h>

#include "e-cal-backend-exchange.h"
#include "e2k-cal-utils.h"
#include "e2k-uri.h"

#include "e2k-propnames.h"
#include "e2k-restriction.h"
#include "mapi.h"

#include "e-folder-exchange.h"
#include "exchange-component.h"
#include "exchange-hierarchy.h"

struct ECalBackendExchangePrivate {
	gboolean read_only;

	/* Objects */
	GHashTable *objects, *cache_unseen;
	char *object_cache_file;
	char *lastmod;
	char *local_attachment_store;
	guint save_timeout_id;

	/* Timezones */
	GHashTable *timezones;
	icaltimezone *default_timezone;
	CalMode mode;
};

#define PARENT_TYPE E_TYPE_CAL_BACKEND_SYNC
static GObjectClass *parent_class = NULL;

#define d(x)

static ECalBackendSyncStatus
is_read_only (ECalBackendSync *backend, EDataCal *cal, gboolean *read_only)
{
	ECalBackendExchange *cbex = E_CAL_BACKEND_EXCHANGE (backend);

	d(printf("ecbe_is_read_only(%p, %p) -> %d\n", backend, cal, cbex->priv->read_only));

	*read_only = cbex->priv->read_only;
	return GNOME_Evolution_Calendar_Success;		
}

static ECalBackendSyncStatus
get_cal_address (ECalBackendSync *backend, EDataCal *cal, char **address)
{
	ECalBackendExchange *cbex = E_CAL_BACKEND_EXCHANGE (backend);
	ExchangeHierarchy *hier;

	hier = e_folder_exchange_get_hierarchy (cbex->folder);
	d(printf("ecbe_get_cal_address(%p, %p) -> %s\n", backend, cal, hier->owner_email));
	*address = g_strdup (hier->owner_email);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
get_alarm_email_address (ECalBackendSync *backend, EDataCal *cal, char **address)
{
	d(printf("ecbe_get_alarm_email_address(%p, %p)\n", backend, cal));

	/* We don't support email alarms.
	 * This should not have been called.
	 */
	*address = NULL;
	return GNOME_Evolution_Calendar_OtherError;
}

static ECalBackendSyncStatus
get_ldap_attribute (ECalBackendSync *backend, EDataCal *cal, char **attribute)
{
	d(printf("ecbe_get_ldap_attribute(%p, %p)\n", backend, cal));

	/* This is just a hack for SunONE */
	*attribute = NULL;
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
get_static_capabilities (ECalBackendSync *backend, EDataCal *cal, char **capabilities)
{
	d(printf("ecbe_get_static_capabilities(%p, %p)\n", backend, cal));

	*capabilities = g_strdup (
		CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS ","
		CAL_STATIC_CAPABILITY_NO_TASK_ASSIGNMENT ","
		CAL_STATIC_CAPABILITY_NO_THISANDFUTURE ","
		CAL_STATIC_CAPABILITY_NO_THISANDPRIOR ","
		CAL_STATIC_CAPABILITY_NO_GEN_OPTIONS ","
		CAL_STATIC_CAPABILITY_REQ_SEND_OPTIONS ","
		CAL_STATIC_CAPABILITY_REMOVE_ALARMS);

	return GNOME_Evolution_Calendar_Success;
}

static void
load_cache (ECalBackendExchange *cbex, E2kUri *e2kuri)
{
	icalcomponent *vcalcomp, *comp, *tmp_comp;
	struct icaltimetype comp_last_mod, folder_last_mod;
	icalcomponent_kind kind;
	icalproperty *prop;
	char *lastmod, *mangled_uri, *storage_dir, *end;
	const char *uristr;
	int i;
	struct stat buf;

	cbex->priv->object_cache_file =
		e_folder_exchange_get_storage_file (cbex->folder, "cache.ics");

	/* Fixme : Try avoiding to do this everytime we come here */
	uristr = e_cal_backend_get_uri (E_CAL_BACKEND (cbex));
	mangled_uri = g_strdup (uristr);
	for (i = 0; i < strlen (mangled_uri); i++) {
		switch (mangled_uri[i]) {
		case ':' :
		case '/' :
			mangled_uri[i] = '_';
		}
	}
	cbex->priv->local_attachment_store = g_strdup_printf ("%s/.evolution/exchange/%s", g_get_home_dir (), mangled_uri);
	end = strrchr (cbex->priv->object_cache_file, '/');
	storage_dir = g_strndup (cbex->priv->object_cache_file, end - cbex->priv->object_cache_file);
	if (lstat(cbex->priv->local_attachment_store , &buf) < 0) {
		symlink (storage_dir, cbex->priv->local_attachment_store);
	}
	g_free (storage_dir);
	g_free (mangled_uri);

	vcalcomp = e_cal_util_parse_ics_file (cbex->priv->object_cache_file);
	if (!vcalcomp)
		return;

	if (icalcomponent_isa (vcalcomp) != ICAL_VCALENDAR_COMPONENT) {
		icalcomponent_free (vcalcomp);
		return;
	}

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbex));

	folder_last_mod = icaltime_null_time ();
	for (comp = icalcomponent_get_first_component (vcalcomp, kind);
	     comp;
	     comp = icalcomponent_get_next_component (vcalcomp, kind)) {
		prop = icalcomponent_get_first_property (comp, ICAL_LASTMODIFIED_PROPERTY);
		if (prop) {
			comp_last_mod = icalproperty_get_lastmodified (prop);
			if (icaltime_compare (comp_last_mod, folder_last_mod) > 0)
				folder_last_mod = comp_last_mod;
		}

		lastmod = e2k_timestamp_from_icaltime (comp_last_mod);
		e_cal_backend_exchange_add_object (cbex, NULL, lastmod, comp);
		g_free (lastmod);
	}
	cbex->priv->lastmod = e2k_timestamp_from_icaltime (folder_last_mod);

	for (comp = icalcomponent_get_first_component (vcalcomp, ICAL_VTIMEZONE_COMPONENT);
	     comp;
	     comp = icalcomponent_get_next_component (vcalcomp, ICAL_VTIMEZONE_COMPONENT)) {
		tmp_comp = icalcomponent_new_clone (comp);
		if (tmp_comp) {
			e_cal_backend_exchange_add_timezone (cbex, tmp_comp);
			icalcomponent_free (tmp_comp);
		}
	}

	icalcomponent_free (vcalcomp);
}

static void
save_timezone (gpointer key, gpointer tz, gpointer vcalcomp)
{
	icalcomponent *tzcomp;

	tzcomp = icalcomponent_new_clone (icaltimezone_get_component (tz));
	icalcomponent_add_component (vcalcomp, tzcomp);
}

static void
save_object (gpointer key, gpointer value, gpointer vcalcomp)
{
	ECalBackendExchangeComponent *ecomp = value;
	icalcomponent *icalcomp;
	GList *l;

	icalcomp = icalcomponent_new_clone (ecomp->icomp);
	icalcomponent_add_component (vcalcomp, icalcomp);

	for (l = ecomp->instances; l; l = l->next) {
		icalcomp = icalcomponent_new_clone (l->data);
		icalcomponent_add_component (vcalcomp, icalcomp);
	}
}

static gboolean
timeout_save_cache (gpointer user_data)
{
	ECalBackendExchange *cbex = user_data;
	icalcomponent *vcalcomp;
	char *data, *tmpfile;
	size_t len, nwrote;
	FILE *f;

	d(printf("timeout_save_cache\n"));
	cbex->priv->save_timeout_id = 0;

	vcalcomp = e_cal_util_new_top_level ();
	g_hash_table_foreach (cbex->priv->timezones, save_timezone, vcalcomp);
	g_hash_table_foreach (cbex->priv->objects, save_object, vcalcomp);
	data = icalcomponent_as_ical_string (vcalcomp);
	icalcomponent_free (vcalcomp);

	tmpfile = g_strdup_printf ("%s~", cbex->priv->object_cache_file);
	f = fopen (tmpfile, "w");
	if (!f)
		goto error;

	len = strlen (data);
	nwrote = fwrite (data, 1, len, f);
	if (fclose (f) != 0 || nwrote != len)
		goto error;

	if (rename (tmpfile, cbex->priv->object_cache_file) != 0)
		unlink (tmpfile);
error:
	g_free (tmpfile);
	return FALSE;
}

static void
save_cache (ECalBackendExchange *cbex)
{
	/* This is just a cache, so if we crash with unsaved changes,
	 * it's not a big deal. So we use a reasonably large timeout.
	 */
	if (cbex->priv->save_timeout_id)
		g_source_remove (cbex->priv->save_timeout_id);
	cbex->priv->save_timeout_id = g_timeout_add (6 * 1000,
						     timeout_save_cache,
						     cbex);
}

static ECalBackendSyncStatus
open_calendar (ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists,
	       const char *username, const char *password)
{
	ECalBackendExchange *cbex = E_CAL_BACKEND_EXCHANGE (backend);
	const char *uristr;
	ExchangeHierarchy *hier;
	const char *prop = PR_ACCESS;
	E2kHTTPStatus status;
	E2kResult *results;
	E2kUri *euri = NULL;
	int nresults;
	guint access = 0;

	d(printf("ecbe_open_calendar(%p, %p, %sonly if exists, user=%s, pass=%s)\n", backend, cal, only_if_exists?"":"not ", username?username:"(null)", password?password:"(null)"));

	uristr = e_cal_backend_get_uri (E_CAL_BACKEND (backend));

	if (cbex->priv->mode == CAL_MODE_LOCAL) {
		ESource *source;
		const char *display_contents = NULL;

		d(printf ("ECBE : cal is offline .. load cache\n"));

		cbex->priv->read_only = TRUE;
		source = e_cal_backend_get_source (E_CAL_BACKEND (cbex));
		display_contents = e_source_get_property (source, "offline_sync");
		
		if (!display_contents || !g_str_equal (display_contents, "1")) {
			e_cal_backend_notify_error (E_CAL_BACKEND (cbex), _("Folder not marked for offline support"));
			return GNOME_Evolution_Calendar_RepositoryOffline;
		}

		euri = e2k_uri_new (uristr);
		load_cache (cbex, euri);
		e2k_uri_free (euri);
		return GNOME_Evolution_Calendar_Success;
	}
		
	/* Make sure we have an open connection */
	// SURF : cbex->account = exchange_component_get_account_for_uri (global_exchange_component, uristr);
	if (!cbex->account)
		return GNOME_Evolution_Calendar_PermissionDenied;
	if (!exchange_account_get_context (cbex->account))
		return GNOME_Evolution_Calendar_RepositoryOffline;

	cbex->folder = exchange_account_get_folder (cbex->account, uristr);
	if (!cbex->folder) {
		/* FIXME: theoretically we should create it if
		 * only_if_exists is FALSE.
		 */
		return GNOME_Evolution_Calendar_NoSuchCal;
	}
	g_object_ref (cbex->folder);

	hier = e_folder_exchange_get_hierarchy (cbex->folder);
	if (hier->hide_private_items) {
		cbex->private_item_restriction =
			e2k_restriction_prop_int (
				E2K_PR_MAPI_SENSITIVITY, E2K_RELOP_NE, 2);
	} else
		cbex->private_item_restriction = NULL;

	status = e_folder_exchange_propfind (cbex->folder, NULL,
					     &prop, 1,
					     &results, &nresults);
	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status) && nresults >= 1) {
		prop = e2k_properties_get_prop (results[0].props, PR_ACCESS);
		if (prop)
			access = atoi (prop);
	}

	if (!(access & MAPI_ACCESS_READ))
		return GNOME_Evolution_Calendar_PermissionDenied;

	cbex->priv->read_only = ((access & MAPI_ACCESS_CREATE_CONTENTS) == 0);

	load_cache (cbex, euri);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
remove_calendar (ECalBackendSync *backend, EDataCal *cal)
{
	d(printf("ecbe_remove_calendar(%p, %p)\n", backend, cal));

	//display_error_dialog("Can not perform this operation, Use Exchange component for folder operations\n");
	return GNOME_Evolution_Calendar_PermissionDenied; /* Error code is not handled in Evolution */

	/* FIXME: Deleting calendar/tasks from respective views */
#if 0
	status = e_folder_exchange_delete (be->folder, NULL);
	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
		return GNOME_Evolution_Calendar_Success;
	else if (status == E2K_HTTP_UNAUTHORIZED)
		return GNOME_Evolution_Calendar_PermissionDenied;
	else
		return GNOME_Evolution_Calendar_OtherError;
#endif
}

static void
add_to_unseen (gpointer key, gpointer value, gpointer data)
{
	ECalBackendExchange *cbex = data;

	g_hash_table_insert (cbex->priv->cache_unseen, key, value);
}

void
e_cal_backend_exchange_cache_sync_start (ECalBackendExchange *cbex)
{
	g_return_if_fail (cbex->priv->cache_unseen == NULL);

	cbex->priv->cache_unseen = g_hash_table_new (NULL, NULL);
	g_hash_table_foreach (cbex->priv->objects, add_to_unseen, cbex);
}

gboolean
e_cal_backend_exchange_in_cache (ECalBackendExchange *cbex,
				 const char          *uid,
				 const char          *lastmod,
				 const char	     *href)
{
	ECalBackendExchangeComponent *ecomp;

	g_return_val_if_fail (cbex->priv->cache_unseen != NULL, FALSE);

	ecomp = g_hash_table_lookup (cbex->priv->objects, uid);
	if (!ecomp)
		return FALSE;
	g_hash_table_remove (cbex->priv->cache_unseen, ecomp->uid);

	if (strcmp (ecomp->lastmod, lastmod) < 0) {
		g_hash_table_remove (cbex->priv->objects, uid);
		return FALSE;
	}

	/* Update the cache with the new href */
	if (href) {
		if (ecomp->href)
			g_free (ecomp->href);
		ecomp->href = g_strdup (href);
	}

	return TRUE;
}

static void
uncache (gpointer key, gpointer value, gpointer data)
{
	ECalBackendExchange *cbex = data;

	g_hash_table_remove (cbex->priv->objects, key);
}

void
e_cal_backend_exchange_cache_sync_end (ECalBackendExchange *cbex)
{
	g_return_if_fail (cbex->priv->cache_unseen != NULL);

	g_hash_table_foreach (cbex->priv->cache_unseen, uncache, cbex);
	g_hash_table_destroy (cbex->priv->cache_unseen);
	cbex->priv->cache_unseen = NULL;

	save_cache (cbex);
}

/**
 * e_cal_backend_exchange_add_object:
 * @cbex: an #ECalBackendExchange
 * @href: the object's href
 * @lastmod: the object's last modtime, as an Exchange timestamp
 * @comp: the object
 *
 * Adds @comp to @cbex's cache
 *
 * Return value: %TRUE on success, %FALSE if @comp was already in
 * @cbex.
 **/
gboolean
e_cal_backend_exchange_add_object (ECalBackendExchange *cbex,
				   const char *href, const char *lastmod,
				   icalcomponent *comp)
{
	ECalBackendExchangeComponent *ecomp;
	gboolean is_instance;
	const char *uid;
	struct icaltimetype rid;

	d(printf("ecbe_add_object(%p, %s, %s)\n", cbex, href, lastmod));

	uid = icalcomponent_get_uid (comp);
	ecomp = g_hash_table_lookup (cbex->priv->objects, uid);

	is_instance = (icalcomponent_get_first_property (comp, ICAL_RECURRENCEID_PROPERTY) != NULL);

	if (ecomp && ecomp->icomp && !is_instance)
		return FALSE;

	if (!ecomp) {
		ecomp = g_new0 (ECalBackendExchangeComponent, 1);
		ecomp->uid = g_strdup (uid);
		g_hash_table_insert (cbex->priv->objects, ecomp->uid, ecomp);
	}

	if (href) {
		g_free (ecomp->href);
		ecomp->href = g_strdup (href);
	}
	if (lastmod && (!ecomp->lastmod || strcmp (ecomp->lastmod, lastmod) > 0)) {
		g_free (ecomp->lastmod);
		ecomp->lastmod = g_strdup (lastmod);
	}

	if (is_instance) {
		GList *l;
		struct icaltimetype inst_rid;
		gboolean inst_found = FALSE;

		rid = icalcomponent_get_recurrenceid (comp);
		for (l = ecomp->instances; l; l = l->next) {
			inst_rid = icalcomponent_get_recurrenceid (l->data);
			if (icaltime_compare (inst_rid, rid) == 0) {
				inst_found = TRUE;
				break;
			}
		}
		if (!inst_found) {

			ecomp->instances = g_list_prepend (ecomp->instances,
						   icalcomponent_new_clone (comp));
		}
	} else
		ecomp->icomp = icalcomponent_new_clone (comp);

	save_cache (cbex);
	return TRUE;
}

static void
discard_detached_instance (ECalBackendExchangeComponent *ecomp,
			   struct icaltimetype rid)
{
	GList *inst;
	struct icaltimetype inst_rid;

	for (inst = ecomp->instances; inst; inst = inst->next) {
		inst_rid = icalcomponent_get_recurrenceid (inst->data);
		if (icaltime_compare (inst_rid, rid) == 0) {
			ecomp->instances = g_list_remove (ecomp->instances, inst->data);
			icalcomponent_free (inst->data);
			return;
		}
	}
}

/**
 * e_cal_backend_exchange_modify_object:
 * @cbex: an #ECalBackendExchange
 * @comp: the object
 * @mod: what parts of @comp to modify
 *
 * Modifies the component identified by @comp's UID, in the manner
 * specified by @comp and @mod.
 *
 * Return value: %TRUE on success, %FALSE if @comp was not found.
 **/
gboolean
e_cal_backend_exchange_modify_object (ECalBackendExchange *cbex,
				      icalcomponent *comp,
				      CalObjModType mod)
{
	ECalBackendExchangeComponent *ecomp;
	const char *uid;
	struct icaltimetype rid;

	d(printf("ecbe_modify_object(%p)\n", cbex));

	g_return_val_if_fail (mod == CALOBJ_MOD_THIS || mod == CALOBJ_MOD_ALL,
			      FALSE);

	uid = icalcomponent_get_uid (comp);
	rid = icalcomponent_get_recurrenceid (comp);

	ecomp = g_hash_table_lookup (cbex->priv->objects, uid);
	if (!ecomp)
		return FALSE;

	if (mod == CALOBJ_MOD_ALL || icaltime_is_null_time (rid)) {
		icalcomponent_free (ecomp->icomp);
		ecomp->icomp = icalcomponent_new_clone (comp);
	} else {
		discard_detached_instance (ecomp, rid);
		ecomp->instances = g_list_prepend (ecomp->instances,
						   icalcomponent_new_clone (comp));
	}

	save_cache (cbex);
	return TRUE;
}

/**
 * e_cal_backend_exchange_remove_object:
 * @cbex: an #ECalBackendExchange
 * @uid: the UID of the object to remove
 *
 * Removes all instances of the component with UID @uid.
 *
 * Return value: %TRUE on success, %FALSE if @comp was not found.
 **/
gboolean
e_cal_backend_exchange_remove_object (ECalBackendExchange *cbex, const char *uid)
{
	d(printf("ecbe_remove_object(%p, %s)\n", cbex, uid));

	if (!g_hash_table_lookup (cbex->priv->objects, uid))
		return FALSE;

	g_hash_table_remove (cbex->priv->objects, uid);

	save_cache (cbex);
	return TRUE;
}

static ECalBackendSyncStatus
discard_alarm (ECalBackendSync *backend, EDataCal *cal,
	       const char *uid, const char *auid)
{
	/* To be called from the Calendar derived class */
	return GNOME_Evolution_Calendar_OtherError;
}

/*To be overriden by Calendar and Task classes*/
static ECalBackendSyncStatus
create_object (ECalBackendSync *backend, EDataCal *cal, 
	       char **calobj, char **uid)
{
	return GNOME_Evolution_Calendar_OtherError;
}

/*To be overriden by Calendar and Task classes*/
static ECalBackendSyncStatus
modify_object (ECalBackendSync *backend, EDataCal *cal, 
			const char * calobj, CalObjModType mod, char **old_object, char **new_object)
{
	return GNOME_Evolution_Calendar_OtherError;
}

ECalBackendExchangeComponent *
get_exchange_comp (ECalBackendExchange *cbex, const char *uid)
{
	ECalBackendExchangeComponent *ecomp;
	
	ecomp = g_hash_table_lookup (cbex->priv->objects, uid);
	if (ecomp)
		return ecomp;
	
	return NULL;			
}

static ECalBackendSyncStatus
get_object (ECalBackendSync *backend, EDataCal *cal,
	    const char *uid, const char *rid, char **object)
{
	ECalBackendExchange *cbex = E_CAL_BACKEND_EXCHANGE (backend);
	ECalBackendExchangeComponent *ecomp;
	ECalComponent *comp;
	
	d(printf("ecbe_get_object(%p, %p, uid=%s, rid=%s)\n", backend, cal, uid, rid));
	
	g_return_val_if_fail (uid != NULL, GNOME_Evolution_Calendar_InvalidObject);
	/*any other asserts?*/
	
	ecomp = g_hash_table_lookup (cbex->priv->objects, uid);
	if ((!ecomp) || (!ecomp->icomp))
		return GNOME_Evolution_Calendar_ObjectNotFound;
	
	/*anything on recur id here????*/
	
	comp = e_cal_component_new ();
	if (e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (ecomp->icomp))) {
		*object = e_cal_component_get_as_string (comp);
	} else {
		*object = NULL;
		g_object_unref (comp);
		return GNOME_Evolution_Calendar_ObjectNotFound;
	}
	
	g_object_unref (comp);
	return GNOME_Evolution_Calendar_Success;
}

ECalBackendSyncStatus
e_cal_backend_exchange_extract_components (const char *calobj,
					   icalproperty_method *method, 
					   GList **comp_list)
{
	icalcomponent *icalcomp, *comp = NULL;
	icalcomponent *subcomp;
	icalcomponent_kind kind;
	GList *comps;
	ECalBackendSyncStatus status = GNOME_Evolution_Calendar_Success;
	
	icalcomp = icalparser_parse_string (calobj);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;
	
	kind = icalcomponent_isa (icalcomp);
	if (kind != ICAL_VCALENDAR_COMPONENT) {
		comp = icalcomp;
		icalcomp = e_cal_util_new_top_level ();
		icalcomponent_add_component (icalcomp, comp);
	}
	
	*method = icalcomponent_get_method (icalcomp);

#if 0
	/* Might have to include this later */
	/*time zone?*/
	subcomp = icalcomponent_get_first_component (icalcomp, ICAL_VTIMEZONE_COMPONENT);
	while (subcomp) {
		e_cal_backend_exchange_add_timezone (cbex, icalcomponent_new_clone (subcomp));
		subcomp = icalcomponent_get_next_component (icalcomp, ICAL_VTIMEZONE_COMPONENT);
	}
#endif

	comps = NULL;
	subcomp = icalcomponent_get_first_component (icalcomp, ICAL_ANY_COMPONENT);
	while (subcomp)	{
		icalcomponent_kind child_kind = icalcomponent_isa (subcomp);
		switch (child_kind)	{
			case ICAL_VEVENT_COMPONENT:
			case ICAL_VTODO_COMPONENT:
	
				/*
					check time zone .....
					icalcomponent_foreach_tzid
				*/
			
				if (!icalcomponent_get_uid (subcomp)) {
					status = GNOME_Evolution_Calendar_InvalidObject;
					goto error;
				}
				comps = g_list_prepend (comps, subcomp);
				break;
				/*journal?*/
			default:
				break;
		}
		subcomp = icalcomponent_get_next_component (icalcomp, ICAL_ANY_COMPONENT);
	}
	
	*comp_list = comps;
error:	
	return status;
}

static ECalBackendSyncStatus
send_objects (ECalBackendSync *backend, EDataCal *cal,
	      const char *calobj,
	      GList **users, char **modified_calobj)
{
	d(printf("ecbe_send_objects(%p, %p, %s)\n", backend, cal, calobj));

	/* FIXME */
	return GNOME_Evolution_Calendar_OtherError;
}

static ECalBackendSyncStatus
get_default_object (ECalBackendSync *backend, EDataCal *cal, char **object)
{
	icalcomponent *comp;
	char *ical_obj;

	d(printf("ecbe_get_default_object(%p, %p)\n", backend, cal));

	comp = e_cal_util_new_component (e_cal_backend_get_kind (E_CAL_BACKEND (backend)));
	ical_obj = icalcomponent_as_ical_string (comp);
	*object = g_strdup (ical_obj);

	icalcomponent_free (comp);

	return GNOME_Evolution_Calendar_Success;
}

typedef struct {
	GList *obj_list;
	gboolean search_needed;
	const char *query;
	ECalBackendSExp *obj_sexp;
	ECalBackend *backend;
	icaltimezone *default_zone;
} MatchObjectData;

static void
match_object_sexp (gpointer key, gpointer value, gpointer data)
{
	ECalBackendExchangeComponent *ecomp = value;
	MatchObjectData *match_data = data;
	ECalComponent *comp;
	
	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (ecomp->icomp));

	if ((!match_data->search_needed) ||
	    (e_cal_backend_sexp_match_comp (match_data->obj_sexp, comp, match_data->backend))) {
		match_data->obj_list = g_list_append (match_data->obj_list,
						      e_cal_component_get_as_string (comp));

		#if 0
		/* match also recurrences */
		g_hash_table_foreach (obj_data->recurrences,
				      (GHFunc) match_recurrence_sexp,
				      match_data);
		#endif
	}
	g_object_unref (comp);
}

static ECalBackendSyncStatus
get_object_list (ECalBackendSync *backend, EDataCal *cal,
		 const char *sexp, GList **objects)
{

	ECalBackendExchange *cbex;
	MatchObjectData match_data;

	cbex = E_CAL_BACKEND_EXCHANGE (backend);
	
	match_data.search_needed = TRUE;
	match_data.query = sexp;
	match_data.obj_list = NULL;
	match_data.backend = E_CAL_BACKEND (backend);
	match_data.default_zone = cbex->priv->default_timezone;

	if (!strcmp (sexp, "#t"))
		match_data.search_needed = FALSE;

	match_data.obj_sexp = e_cal_backend_sexp_new (sexp);
	if (!match_data.obj_sexp)
		return GNOME_Evolution_Calendar_InvalidQuery;

	g_hash_table_foreach (cbex->priv->objects, (GHFunc) match_object_sexp, &match_data);

	*objects = match_data.obj_list;

	return GNOME_Evolution_Calendar_Success;
}

ECalBackendSyncStatus
get_timezone (ECalBackendSync *backend, EDataCal *cal,
	      const char *tzid, char **object)
{
	ECalBackendExchange *cbex = E_CAL_BACKEND_EXCHANGE (backend);
	icaltimezone *zone;
	icalcomponent *vtzcomp;
	char *ical_obj;

	d(printf("ecbe_get_timezone(%p, %p, %s)\n", backend, cal, tzid));

	zone = g_hash_table_lookup (cbex->priv->timezones, tzid);
	if (!zone)
		return GNOME_Evolution_Calendar_ObjectNotFound;

	vtzcomp = icaltimezone_get_component (zone);
	if (!vtzcomp)
		return GNOME_Evolution_Calendar_OtherError;

	ical_obj = icalcomponent_as_ical_string (vtzcomp);
	*object = g_strdup (ical_obj);

	return  GNOME_Evolution_Calendar_Success;
}

icaltimezone *
e_cal_backend_exchange_get_default_time_zone (ECalBackendSync *backend)
{
	ECalBackendExchange *cbex = E_CAL_BACKEND_EXCHANGE (backend);
	ECalBackendExchangePrivate *priv;
	
	priv = cbex->priv;
	
	return priv->default_timezone;
}

ECalBackendSyncStatus
e_cal_backend_exchange_add_timezone (ECalBackendExchange *cbex,
				     icalcomponent *vtzcomp)
{
	icalproperty *prop;
	icaltimezone *zone;
	const char *tzid;

	d(printf("ecbe_add_timezone(%p)\n", cbex));

	prop = icalcomponent_get_first_property (vtzcomp, ICAL_TZID_PROPERTY);
	if (!prop) 
		return GNOME_Evolution_Calendar_InvalidObject;
	tzid = icalproperty_get_tzid (prop);
	if (g_hash_table_lookup (cbex->priv->timezones, tzid))
		return GNOME_Evolution_Calendar_ObjectIdAlreadyExists;

	zone = icaltimezone_new ();
	if (!icaltimezone_set_component (zone, icalcomponent_new_clone (vtzcomp))) {
		icaltimezone_free (zone, TRUE);
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	g_hash_table_insert (cbex->priv->timezones, g_strdup (tzid), zone);
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
add_timezone (ECalBackendSync *backend, EDataCal *cal,
	      const char *tzobj)
{
	ECalBackendExchange *cbex = E_CAL_BACKEND_EXCHANGE (backend);
	GNOME_Evolution_Calendar_CallStatus status;
	icalcomponent *vtzcomp;

	vtzcomp = icalcomponent_new_from_string ((char *)tzobj);
	if (!vtzcomp)
		return GNOME_Evolution_Calendar_InvalidObject;

	status = e_cal_backend_exchange_add_timezone (cbex, vtzcomp);
	switch (status) {
	case GNOME_Evolution_Calendar_ObjectIdAlreadyExists:
		/* fall through */

	case GNOME_Evolution_Calendar_Success:
		icalcomponent_free (vtzcomp);
		return GNOME_Evolution_Calendar_Success;

	default:
		icalcomponent_free (vtzcomp);
		return status;
	}
	icalcomponent_free (vtzcomp);
}

static ECalBackendSyncStatus
set_default_timezone (ECalBackendSync *backend, EDataCal *cal,
		      const char *tzid)
{
	ECalBackendExchange *cbex = E_CAL_BACKEND_EXCHANGE (backend);
	icaltimezone *zone;

	d(printf("ecbe_set_default_timezone(%p, %p, %s)\n", backend, cal, tzid));

	zone = g_hash_table_lookup (cbex->priv->timezones, tzid);
	if (zone) {
		cbex->priv->default_timezone = zone;
		return GNOME_Evolution_Calendar_Success;
	} else
		return GNOME_Evolution_Calendar_ObjectNotFound;
}

struct search_data {
	ECalBackendSExp *sexp;
	GList *matches;
	ECalBackend *backend;
};

static void
match_object (gpointer key, gpointer value, gpointer data)
{
	ECalBackendExchangeComponent *ecomp = value;
	struct search_data *sd = data;
	ECalComponent *cal_comp;
	icalcomponent *tmp;
	char * ecal_str;
	GList *inst;

	/* FIXME: we shouldn't have to convert to ECalComponent here */
	cal_comp = e_cal_component_new ();

	/* Find a way to test if the icalcomp added to cal_comp is not null */
	tmp = icalcomponent_new_clone (ecomp->icomp);
	if (tmp) {
		e_cal_component_set_icalcomponent (cal_comp, tmp);
		if (e_cal_backend_sexp_match_comp (sd->sexp, cal_comp, sd->backend)) {
			ecal_str = e_cal_component_get_as_string (cal_comp);
			if (ecal_str)
				sd->matches = g_list_prepend (sd->matches, ecal_str);
		}
	}

	for (inst = ecomp->instances; inst; inst = inst->next) {
		e_cal_component_set_icalcomponent (cal_comp, icalcomponent_new_clone (inst->data));
		if (e_cal_backend_sexp_match_comp (sd->sexp, cal_comp, sd->backend)) {
			ecal_str = e_cal_component_get_as_string (cal_comp);
			if (ecal_str)
				sd->matches = g_list_prepend (sd->matches, ecal_str);
		}
	}

	g_object_unref (cal_comp);
}

static void
start_query (ECalBackend *backend, EDataCalView *view)
{
	ECalBackendExchange *cbex = E_CAL_BACKEND_EXCHANGE (backend);
	struct search_data sd;
	GList *m;

	d(printf("ecbe_start_query(%p, %p)\n", backend, view));

	sd.sexp = e_data_cal_view_get_object_sexp (view);
	if (!sd.sexp) {
		e_data_cal_view_notify_done (view, GNOME_Evolution_Calendar_InvalidQuery);
		return;
	}
	sd.matches = NULL;
	sd.backend = backend;

	g_hash_table_foreach (cbex->priv->objects, match_object, &sd);

	if (sd.matches) {
		e_data_cal_view_notify_objects_added (view, sd.matches);

		for (m = sd.matches; m; m = m->next)
			g_free (m->data);
		g_list_free (sd.matches);
	}

	e_data_cal_view_notify_done (view, GNOME_Evolution_Calendar_Success);
}

gboolean 
e_cal_backend_exchange_is_online (ECalBackendExchange *cbex)
{
	if (cbex->priv->mode == CAL_MODE_LOCAL)
		return FALSE;
	else
		return TRUE;
}

static CalMode
get_mode (ECalBackend *backend)
{
	ECalBackendExchange *cbex;
	ECalBackendExchangePrivate *priv;
	
	cbex = E_CAL_BACKEND_EXCHANGE (backend);
	priv = cbex->priv;

	d(printf("ecbe_get_mode(%p)\n", backend));

	return priv->mode;
}

static void
set_mode (ECalBackend *backend, CalMode mode)
{
	ECalBackendExchange *cbex;
	ECalBackendExchangePrivate *priv;
	ExchangeAccount *account;
	const char *uristr;
	
	cbex = E_CAL_BACKEND_EXCHANGE (backend);
	priv = cbex->priv;

	d(printf("ecbe_set_mode(%p) : mode : %d\n", backend, mode));

	if (priv->mode == mode) {
		e_cal_backend_notify_mode (
			backend, GNOME_Evolution_Calendar_CalListener_MODE_SET,
			cal_mode_to_corba (mode));
	}

	switch (mode) {
	
	case CAL_MODE_REMOTE:
			/* Change status to be online now */
			/* Should we check for access rights before setting this ? */
			d(printf ("set mode to online\n"));
			uristr = e_cal_backend_get_uri (E_CAL_BACKEND (backend));
			// SURF : account = exchange_component_get_account_for_uri (global_exchange_component, uristr);
			if (!account)
				return;
			cbex->folder = exchange_account_get_folder (account, uristr);
			/* FIXME : Test if available for read already */
			priv->read_only = FALSE;
			exchange_account_set_online (account);
			priv->mode = CAL_MODE_REMOTE;
			e_cal_backend_notify_mode (backend, 
				GNOME_Evolution_Calendar_CalListener_MODE_SET,
				GNOME_Evolution_Calendar_MODE_REMOTE);
			/* FIXME : Check if online and check if authentication
				is needed */
			break;

	case CAL_MODE_LOCAL:
			d(printf ("set mode to offline\n"));
			uristr = e_cal_backend_get_uri (E_CAL_BACKEND (backend));
			// SURF : account = exchange_component_get_account_for_uri (global_exchange_component, uristr);
			if (!account)
				return;
			cbex->folder = exchange_account_get_folder (account, uristr);
			priv->mode = CAL_MODE_LOCAL;
			priv->read_only = TRUE;
			exchange_account_set_offline (account);
			e_cal_backend_notify_mode (backend, 
				GNOME_Evolution_Calendar_CalListener_MODE_SET,
				GNOME_Evolution_Calendar_MODE_LOCAL);
			break;

	default :
		e_cal_backend_notify_mode (
			backend, GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED,
			cal_mode_to_corba (mode));
	}
}

static ECalBackendSyncStatus
get_freebusy (ECalBackendSync *backend, EDataCal *cal,
	      GList *users, time_t start, time_t end,
	      GList **freebusy)
{
	d(printf("ecbe_get_free_busy(%p, %p)\n", backend, cal));

	/* FIXME */
	return GNOME_Evolution_Calendar_OtherError;
}

/**
 * e_cal_backend_exchange_lf_to_crlf:
 * @in: input text in UNIX ("\n") format
 *
 * Return value: text with all LFs converted to CRLF. The caller must
 * free the text.
 **/
char *
e_cal_backend_exchange_lf_to_crlf (const char *in)
{
	int len;
	const char *s;
	char *out, *d;

	g_return_val_if_fail (in != NULL, NULL);

	len = strlen (in);
	for (s = strchr (in, '\n'); s; s = strchr (s + 1, '\n'))
		len++;

	out = g_malloc (len + 1);
	for (s = in, d = out; *s; s++) {
		if (*s == '\n')
			*d++ = '\r';
		*d++ = *s;
	}
	*d = '\0';

	return out;
}

static void
get_cal_owner (ECalBackendSync *backend, char **name)
{
	ECalBackendExchange *cbex = E_CAL_BACKEND_EXCHANGE (backend);
	ExchangeHierarchy *hier;

	g_return_if_fail (E_IS_CAL_BACKEND_EXCHANGE (cbex));

	hier = e_folder_exchange_get_hierarchy (cbex->folder);
	*name = g_strdup (hier->owner_name);
}

void
e_cal_backend_exchange_get_from (ECalBackendSync *backend, ECalComponent *comp, 
			char **name, char **email)
{
	ECalComponentOrganizer org;
	ECalBackendSyncStatus status;

	g_return_if_fail (E_IS_CAL_BACKEND_EXCHANGE (backend));

	e_cal_component_get_organizer (comp, &org);
	if (org.cn) {
		*name = g_strdup (org.cn);
		*email = g_strdup (org.value);
	} else {
		get_cal_owner (backend, name);
		status = get_cal_address (backend, NULL, email);
	}
}


/* Do not internationalize */
static const char *e2k_rfc822_months [] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/**
 * e_cal_backend_exchange_make_timestamp_rfc822:
 * @when: the %time_t to convert to an RFC822 timestamp
 *
 * Creates an RFC822 Date header value corresponding to @when, in the
 * locale timezone.
 *
 * Return value: the timestamp, which the caller must free.
 **/
char *
e_cal_backend_exchange_make_timestamp_rfc822 (time_t when)
{
	struct tm tm;
	int offset;

	e_localtime_with_offset (when, &tm, &offset);
	offset = (offset / 3600) * 100 + (offset / 60) % 60;

	return g_strdup_printf ("%02d %s %04d %02d:%02d:%02d %+05d",
				tm.tm_mday, e2k_rfc822_months[tm.tm_mon],
				tm.tm_year + 1900,
				tm.tm_hour, tm.tm_min, tm.tm_sec,
				offset);
}

char *
e_cal_backend_exchange_get_from_string (ECalBackendSync *backend, ECalComponent *comp)
{
	char *name, *addr;

	e_cal_backend_exchange_get_from (backend, comp, &name, &addr);
	
	return g_strdup_printf ("\"%s\" <%s>", name, addr);
}


struct ChangeData {
	EXmlHash *ehash;
	GList *adds;
	GList *modifies;
};

static void
check_change_type (gpointer key, gpointer value, gpointer data)
{
	ECalBackendExchangeComponent *ecomp = value;
	struct ChangeData *change_data = data;
	char *calobj;
	ECalComponent *comp;
	char *uid = key;
	
	if (ecomp != NULL) {

		comp = e_cal_component_new ();
		e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (ecomp->icomp));

		calobj = e_cal_component_get_as_string (comp);
		switch (e_xmlhash_compare (change_data->ehash, uid, calobj)){
		case E_XMLHASH_STATUS_SAME:
			break;
		case E_XMLHASH_STATUS_NOT_FOUND:
			change_data->adds = g_list_prepend (change_data->adds, g_strdup (calobj));
			e_xmlhash_add (change_data->ehash, uid, calobj);
			break;
		case E_XMLHASH_STATUS_DIFFERENT:
			change_data->modifies = g_list_prepend (change_data->modifies, g_strdup (calobj));
			e_xmlhash_add (change_data->ehash, uid, calobj);
		}
		
		g_free (calobj);
		g_object_unref (comp);
	}
		
}

struct cbe_data {
	ECalBackendExchange *cbex;
	icalcomponent_kind kind;
	GList *deletes;
	EXmlHash *ehash;
};

static void
e_cal_backend_exchange_compute_changes_foreach_key (const char *key, gpointer data)
{
	struct cbe_data *cbedata = data;
	ECalBackendExchangeComponent *ecomp;
	ecomp = g_hash_table_lookup (cbedata->cbex->priv->objects, key);

	if (!ecomp) {
		ECalComponent *comp;
		comp = e_cal_component_new ();
		if (cbedata->kind == ICAL_VTODO_COMPONENT)
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
		else
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);
		e_cal_component_set_uid (comp, key);
		cbedata->deletes = g_list_prepend (cbedata->deletes, e_cal_component_get_as_string (comp));
		
		e_xmlhash_remove (cbedata->ehash, key);
		g_object_unref (comp);	
	}
}

/* Attachments */
static char *
save_attach_file (const char *dest_file, char *file_contents, int len)
{
	char *dest_url = NULL;
	int fd;

	d(printf ("dest_file is :%s\n", dest_file));

	/* Write it to our local exchange store in .evolution */
	fd = open (dest_file, O_RDWR | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		d(printf ("open of destination file for attachments failed\n"));
		goto end;
	}
	
	if (camel_write (fd, file_contents, len) < 0) {
		d(printf ("camel write to attach file failed\n"));
		goto end;
	}
	/* FIXME : Add a ATTACH:CID:someidentifier here */
	dest_url = g_strdup_printf ("file://%s", dest_file);

end :
	close (fd);
	return dest_url;
}

GSList * 
get_attachment (ECalBackendExchange *cbex, const char *uid, 
			const char *body, int len)
{
	CamelStream *stream;
	CamelMimeMessage *msg;
	CamelDataWrapper *msg_content, *content = NULL;
	CamelMultipart *multipart;
	CamelMimePart *part;
	const char *filename = NULL;
	char *attach_file_url, *attach_file;
	int i;
	GSList *list = NULL;
	unsigned char *attach_data;

	stream = camel_stream_mem_new_with_buffer (body, len);
	msg = camel_mime_message_new ();
	camel_data_wrapper_construct_from_stream (CAMEL_DATA_WRAPPER (msg), stream);
	camel_object_unref (stream);

	msg_content = camel_medium_get_content_object (CAMEL_MEDIUM (msg));
	if (msg_content && CAMEL_IS_MULTIPART (msg_content)) {
		multipart = (CamelMultipart *)msg_content;
	
		for (i = 0; i < (int)camel_multipart_get_number (multipart); i++) {
			part = camel_multipart_get_part (multipart, i);
			filename = camel_mime_part_get_filename (part);
			if (filename) {
				CamelStreamMem *stream_mem;

				content = camel_medium_get_content_object (CAMEL_MEDIUM (part));
				
				stream = camel_stream_mem_new ();
				stream_mem = (CamelStreamMem *)stream;
			
				camel_data_wrapper_decode_to_stream (content, stream);
				attach_data = g_memdup (stream_mem->buffer->data, stream_mem->buffer->len);
				attach_file = g_strdup_printf ("%s/%s-%s", cbex->priv->local_attachment_store, uid, filename);
				// Attach
				attach_file_url = save_attach_file (attach_file, attach_data, stream_mem->buffer->len);
				g_free (attach_file);
				d(printf ("attach file name : %s\n", attach_file_url));
				list = g_slist_append (list, g_strdup (attach_file_url));

				camel_object_unref (stream);
			}
		} /* Loop through each multipart */
	}

	camel_object_unref (msg);
	return list;
}

static char *
get_attach_file_contents (const char *filename)
{
	int fd, len = 0;
	struct stat sb;
	char *file_contents = NULL;

	fd = open (filename, O_RDONLY);
	if (fd < 0) {
		d(printf ("Could not open the attachment file : %s\n", filename));
		goto end;
	}
	if (fstat (fd, &sb) < 0) {
		d(printf ("fstat of attachment file failed\n"));
		goto end;
	}
	len = sb.st_size;

	if (len > 0) {
		file_contents = g_malloc0 (len + 1);

		if (camel_read (fd, file_contents, len) < 0) {
			d(printf ("reading from the attachment file failed\n"));
			g_free (file_contents);
			file_contents = NULL;
			goto end;
		}
		file_contents [len] = '\0';
	}

end :
	close (fd);
	return file_contents;
}

char *
build_msg ( ECalBackendExchange *cbex, ECalComponent *comp, const char *subject, char **boundary)
{
	CamelMimeMessage *msg;
	CamelStreamMem *content;
	CamelMimePart *mime_part;
	CamelDataWrapper *dw, *wrapper;
	CamelMultipart *multipart;
	CamelInternetAddress *from;
	CamelStream *stream;
	CamelContentType *type;
	const char *uid;
	char *buffer = NULL, *cid;
	char *from_name, *from_email;
	GSList *attach_list = NULL, *l, *new_attach_list = NULL;
	char *fname, *file_contents = NULL, *filename, *dest_url, *mime_filename, *attach_file;
	int len = 0;

	e_cal_backend_exchange_get_from (E_CAL_BACKEND_SYNC (cbex), comp, &from_name, &from_email);

	msg = camel_mime_message_new ();

	multipart = camel_multipart_new ();

	/* Headers */
	camel_mime_message_set_subject (msg, subject);

	from = camel_internet_address_new ();
	camel_internet_address_add (from, from_name, from_email);
	camel_mime_message_set_from (msg, from);
	camel_object_unref (from);

	e_cal_component_get_uid (comp, &uid);
	e_cal_component_get_attachment_list (comp, &attach_list);
	for (l = attach_list; l ; l = l->next){
		if (!strncmp ((char *)l->data, "file://", 7)) {
			fname = (char *)(l->data) + strlen ("file://");
			filename = g_strrstr (fname, "/") + 1;
			mime_filename = filename + strlen(uid) + 1;
			attach_file = g_strdup (fname);
		} else {
			fname = (char *)(l->data);
			filename = g_strrstr (fname, "/") + 1;
			mime_filename = filename;
			attach_file = g_strdup_printf ("%s/%s-%s", cbex->priv->local_attachment_store, uid, filename);
		}
	
		file_contents = get_attach_file_contents (fname);
		if (!file_contents)
			continue;

		len = strlen (file_contents);
		dest_url = save_attach_file (attach_file, file_contents, len);
		g_free (attach_file);
		if (!dest_url)
			continue;
		new_attach_list = g_slist_append (new_attach_list, dest_url);

		/* Content */
		stream = camel_stream_mem_new_with_buffer (file_contents, len);
		wrapper = camel_data_wrapper_new ();
		camel_data_wrapper_construct_from_stream (wrapper, stream);
		camel_object_unref (stream);

		char *mime_type = gnome_vfs_get_mime_type (dest_url + strlen ("file://"));
		type = camel_content_type_decode (mime_type);
		camel_data_wrapper_set_mime_type_field (wrapper, type);
		camel_content_type_unref (type);

		mime_part = camel_mime_part_new ();

		camel_medium_set_content_object (CAMEL_MEDIUM (mime_part), wrapper);
		camel_mime_part_set_filename (mime_part, mime_filename);
		camel_mime_part_set_encoding (mime_part, CAMEL_TRANSFER_ENCODING_BASE64);
		cid = camel_header_msgid_generate ();
		camel_mime_part_set_content_id (mime_part, cid);	
		camel_mime_part_set_description (mime_part, mime_filename);
		camel_mime_part_set_disposition (mime_part, "attachment");
		camel_multipart_set_boundary (multipart, NULL);
		*boundary = g_strdup (camel_multipart_get_boundary (multipart));
		camel_multipart_add_part (multipart, mime_part);
		camel_object_unref (mime_part);
		g_free (cid);

	}
	if (!new_attach_list) {
		camel_object_unref (multipart);
		camel_object_unref (msg);
		return NULL;
	}
	e_cal_component_set_attachment_list (comp, new_attach_list);

	camel_medium_set_content_object (CAMEL_MEDIUM (msg), CAMEL_DATA_WRAPPER (multipart));
	camel_object_unref (multipart);

	content = (CamelStreamMem *)camel_stream_mem_new();
	dw = camel_medium_get_content_object (CAMEL_MEDIUM (msg));
	camel_data_wrapper_decode_to_stream(dw, (CamelStream *)content);
	buffer = g_memdup (content->buffer->data, content->buffer->len);
	buffer[content->buffer->len] = '\0';
	d(printf ("|| Buffer: \n%s\n", buffer));
	camel_object_unref (content);
	camel_object_unref (msg);

	return buffer;
}

static ECalBackendSyncStatus
get_changes (ECalBackendSync *backend, EDataCal *cal,
	     const char *change_id,
	     GList **adds, GList **modifies, GList **deletes)
{
	ECalBackendExchange *cbex = E_CAL_BACKEND_EXCHANGE (backend);
	char *path, *filename;
	EXmlHash *ehash;
	struct ChangeData data;
	struct cbe_data cbedata;
	
	d(printf("ecbe_get_changes(%p, %p, %s)\n", backend, cal, change_id));
	
	g_return_val_if_fail (E_IS_CAL_BACKEND_EXCHANGE (cbex), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (change_id != NULL, GNOME_Evolution_Calendar_ObjectNotFound);
	
	/* open the changes file */
	filename = g_strdup_printf ("%s.changes", change_id);
	path = e_folder_exchange_get_storage_file (cbex->folder, filename);
	ehash = e_xmlhash_new (path);
	g_free (path);
	g_free (filename);	
	
	/*calculate add/mod*/
	data.ehash = ehash;
	data.adds = NULL;
	data.modifies = NULL;
	g_hash_table_foreach (cbex->priv->objects, check_change_type, &data);
	
	*adds = data.adds;
	*modifies = data.modifies;
	ehash = data.ehash;
	
	/*deletes*/
	cbedata.cbex = cbex;
	cbedata.kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbex));
	cbedata.deletes = NULL;
	cbedata.ehash = ehash;
	e_xmlhash_foreach_key (ehash, (EXmlHashFunc)e_cal_backend_exchange_compute_changes_foreach_key, &cbedata);
	
	*deletes = cbedata.deletes;
	
	e_xmlhash_write (ehash);
	e_xmlhash_destroy (ehash);
	
	return GNOME_Evolution_Calendar_Success;
}

static icaltimezone *
internal_get_default_timezone (ECalBackend *backend)
{
	ECalBackendExchange *cbex = E_CAL_BACKEND_EXCHANGE (backend);

	/* FIXME : This should never happen. Sometimes gets triggered while moving 
	between online and offline. */
	if (!cbex->account)
		return NULL;

	if (!cbex->priv->default_timezone &&
	    cbex->account->default_timezone) {
		cbex->priv->default_timezone =
			g_hash_table_lookup (cbex->priv->timezones,
					     cbex->account->default_timezone);
	}

	return cbex->priv->default_timezone;
}

static icaltimezone *
internal_get_timezone (ECalBackend *backend, const char *tzid)
{
	ECalBackendExchange *cbex = E_CAL_BACKEND_EXCHANGE (backend);

	return g_hash_table_lookup (cbex->priv->timezones, tzid);
}


static void
free_exchange_comp (gpointer value)
{
	ECalBackendExchangeComponent *ecomp = value;
	GList *inst;

	g_free (ecomp->uid);
	g_free (ecomp->href);
	g_free (ecomp->lastmod);

	icalcomponent_free (ecomp->icomp);

	for (inst = ecomp->instances; inst; inst = inst->next)
		icalcomponent_free (inst->data);
	g_list_free (ecomp->instances);

	g_free (ecomp);
}

static void
init (ECalBackendExchange *cbex)
{
	cbex->priv = g_new0 (ECalBackendExchangePrivate, 1);

	cbex->priv->objects = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		NULL, free_exchange_comp);

	cbex->priv->timezones = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		g_free, (GDestroyNotify)icaltimezone_free);
}

static void
dispose (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	ECalBackendExchange *cbex = E_CAL_BACKEND_EXCHANGE (object);

	if (cbex->priv->save_timeout_id) {
		g_source_remove (cbex->priv->save_timeout_id);
		timeout_save_cache (cbex);
	}

	g_hash_table_destroy (cbex->priv->objects);
	if (cbex->priv->cache_unseen)
		g_hash_table_destroy (cbex->priv->cache_unseen);
	g_free (cbex->priv->object_cache_file);
	g_free (cbex->priv->lastmod);

	g_hash_table_destroy (cbex->priv->timezones);

	g_free (cbex->priv);

	if (G_OBJECT_CLASS (parent_class)->finalize)
		G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
class_init (ECalBackendExchangeClass *klass)
{
	GObjectClass *object_class;
	ECalBackendClass *backend_class = E_CAL_BACKEND_CLASS (klass);
	ECalBackendSyncClass *sync_class = E_CAL_BACKEND_SYNC_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class = (GObjectClass *) klass;

	sync_class->is_read_only_sync = is_read_only;
	sync_class->get_cal_address_sync = get_cal_address;
	sync_class->get_alarm_email_address_sync = get_alarm_email_address;
	sync_class->get_ldap_attribute_sync = get_ldap_attribute;
	sync_class->get_static_capabilities_sync = get_static_capabilities;
	sync_class->open_sync = open_calendar;
	sync_class->remove_sync = remove_calendar;
	sync_class->discard_alarm_sync = discard_alarm;
	sync_class->send_objects_sync = send_objects;
	sync_class->get_default_object_sync = get_default_object;
	sync_class->get_object_sync = get_object;
	sync_class->get_object_list_sync = get_object_list;
	sync_class->get_timezone_sync = get_timezone;
	sync_class->add_timezone_sync = add_timezone;
	sync_class->set_default_timezone_sync = set_default_timezone;
 	sync_class->get_freebusy_sync = get_freebusy;
 	sync_class->get_changes_sync = get_changes;
	sync_class->create_object_sync = create_object;
	sync_class->modify_object_sync = modify_object;

 	backend_class->start_query = start_query;
 	backend_class->get_mode = get_mode;
 	backend_class->set_mode = set_mode;
	backend_class->internal_get_default_timezone = internal_get_default_timezone;
	backend_class->internal_get_timezone = internal_get_timezone;

	object_class->dispose = dispose;
	object_class->finalize = finalize;
}

E2K_MAKE_TYPE (e_cal_backend_exchange, ECalBackendExchange, class_init, init, PARENT_TYPE)
