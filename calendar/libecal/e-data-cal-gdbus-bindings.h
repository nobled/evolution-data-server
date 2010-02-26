/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Travis Reitter (travis.reitter@collabora.co.uk)
 */

#include <glib.h>
#include <edbus/edbus.h>

#include <libedata-gdbus-bindings/e-data-gdbus-bindings-common.h>

G_BEGIN_DECLS

/* FIXME: These bindings were created manually; replace with generated bindings
 * when possible */

static gboolean
e_data_cal_gdbus_is_read_only_sync (EDBusProxy  *proxy,
				    GError     **error)
{
        EVariant *parameters;
        EVariant *retvals;

        parameters = e_variant_new ("()");
	retvals = e_dbus_proxy_invoke_method_sync (proxy, "isReadOnly",
			parameters, -1, NULL, error);

        return demarshal_retvals__VOID (retvals);
}

static gboolean
e_data_cal_gdbus_close_sync (EDBusProxy  *proxy,
                             GError     **error)
{
        EVariant *parameters;
        EVariant *retvals;

        parameters = e_variant_new ("()");
        retvals = e_dbus_proxy_invoke_method_sync (proxy, "close", parameters,
                                                        -1, NULL, error);

        return demarshal_retvals__VOID (retvals);
}

static gboolean
e_data_cal_gdbus_open_sync (EDBusProxy      *proxy,
                            const gboolean   IN_only_if_exists,
                            const char      *IN_username,
                            const char      *IN_password,
                            GError         **error)
{
        EVariant *parameters;
        EVariant *retvals;

        parameters = e_variant_new ("(bss)", IN_only_if_exists, IN_username, IN_password);
        retvals = e_dbus_proxy_invoke_method_sync (proxy, "open", parameters,
                                                        -1, NULL, error);

        return demarshal_retvals__VOID (retvals);
}

static void
open_cb (EDBusProxy *proxy,
         GAsyncResult *result,
         gpointer user_data)
{
        Closure *closure = user_data;
        EVariant *retvals;
        GError *error = NULL;

        retvals = e_dbus_proxy_invoke_method_finish (proxy, result, &error);
        if (retvals) {
                if (!demarshal_retvals__VOID (retvals)) {
                        error = g_error_new (E_CALENDAR_ERROR, E_CALENDAR_STATUS_CORBA_EXCEPTION, "demarshalling results for Calendar method 'open'");
                }
        }

        (*(reply__VOID)closure->cb) (proxy, error, closure->user_data);
        closure_free (closure);
}

static gboolean
e_data_cal_gdbus_open (EDBusProxy      *proxy,
                       const gboolean   IN_only_if_exists,
		       const char      *IN_username,
		       const char      *IN_password,
                       reply__VOID      callback,
                       gpointer         user_data)
{
        EVariant *parameters;
        Closure *closure;

        parameters = e_variant_new ("(bss)", IN_only_if_exists, IN_username, IN_password);

        closure = g_slice_new (Closure);
        closure->cb = G_CALLBACK (callback);
        closure->user_data = user_data;

        e_dbus_proxy_invoke_method (proxy, "open", parameters, -1, NULL, (GAsyncReadyCallback) open_cb, closure);

	return TRUE;
}

static gboolean
e_data_cal_gdbus_remove_sync (EDBusProxy  *proxy,
                              GError     **error)
{
        EVariant *parameters;
        EVariant *retvals;

        parameters = e_variant_new ("()");
        retvals = e_dbus_proxy_invoke_method_sync (proxy, "remove", parameters,
                                                        -1, NULL, error);

        return demarshal_retvals__VOID (retvals);
}

static gboolean
e_data_cal_gdbus_get_alarm_email_address_sync (EDBusProxy  *proxy,
					       char       **address,
					       GError     **error)
{
        EVariant *parameters;
        EVariant *retvals;

        parameters = e_variant_new ("()");
	retvals = e_dbus_proxy_invoke_method_sync (proxy, "getAlarmEmailAddress", parameters, -1, NULL, error);

        return demarshal_retvals__STRING (retvals, address);
}

static gboolean
e_data_cal_gdbus_get_cal_address_sync (EDBusProxy  *proxy,
				       char       **address,
				       GError     **error)
{
        EVariant *parameters;
        EVariant *retvals;

        parameters = e_variant_new ("()");
	retvals = e_dbus_proxy_invoke_method_sync (proxy, "getCalAddress", parameters, -1, NULL, error);

        return demarshal_retvals__STRING (retvals, address);
}

static gboolean
e_data_cal_gdbus_get_ldap_attribute_sync (EDBusProxy  *proxy,
					  char       **attr,
					  GError     **error)
{
        EVariant *parameters;
        EVariant *retvals;

        parameters = e_variant_new ("()");
	retvals = e_dbus_proxy_invoke_method_sync (proxy, "getLdapAttribute", parameters, -1, NULL, error);

        return demarshal_retvals__STRING (retvals, attr);
}

static gboolean
e_data_cal_gdbus_get_scheduling_information_sync (EDBusProxy  *proxy,
						  char       **caps,
						  GError     **error)
{
        EVariant *parameters;
        EVariant *retvals;

        parameters = e_variant_new ("()");
	retvals = e_dbus_proxy_invoke_method_sync (proxy, "getSchedulingInformation", parameters, -1, NULL, error);

        return demarshal_retvals__STRING (retvals, caps);
}

static gboolean
e_data_cal_gdbus_set_mode (EDBusProxy  *proxy,
			   guint        mode,
			   GError     **error)
{
        EVariant *parameters;
        EVariant *retvals;

        parameters = e_variant_new ("(u)", mode);
	retvals = e_dbus_proxy_invoke_method_sync (proxy, "setMode", parameters, -1, NULL, error);

        return demarshal_retvals__VOID (retvals);
}

static gboolean
e_data_cal_gdbus_get_object_sync (EDBusProxy  *proxy,
				  const char  *IN_uid,
				  const char  *IN_rid,
				  char       **OUT_object,
				  GError     **error)
{
        EVariant *parameters;
        EVariant *retvals;

        parameters = e_variant_new ("(ss)", IN_uid, IN_rid);
	retvals = e_dbus_proxy_invoke_method_sync (proxy, "getObject", parameters, -1, NULL, error);

        return demarshal_retvals__STRING (retvals, OUT_object);
}

static gboolean
e_data_cal_gdbus_get_object_list_sync (EDBusProxy   *proxy,
				       const char   *IN_sexp,
				       char       ***OUT_objects,
				       GError      **error)
{
        EVariant *parameters;
        EVariant *retvals;

        parameters = e_variant_new ("(s)", IN_sexp);
	retvals = e_dbus_proxy_invoke_method_sync (proxy, "getObjectList", parameters, -1, NULL, error);

        return demarshal_retvals__STRINGVECTOR (retvals, OUT_objects);
}

static gboolean
e_data_cal_gdbus_get_default_object_sync (EDBusProxy  *proxy,
					  char       **object,
					  GError     **error)
{
        EVariant *parameters;
        EVariant *retvals;

        parameters = e_variant_new ("()");
	retvals = e_dbus_proxy_invoke_method_sync (proxy, "getDefaultObject", parameters, -1, NULL, error);

        return demarshal_retvals__STRING (retvals, object);
}

static gboolean
e_data_cal_gdbus_create_object_sync (EDBusProxy  *proxy,
				     const char  *IN_object,
				     char       **OUT_uid,
				     GError     **error)
{
        EVariant *parameters;
        EVariant *retvals;

        parameters = e_variant_new ("(s)", IN_object);
	retvals = e_dbus_proxy_invoke_method_sync (proxy, "createObject", parameters, -1, NULL, error);

        return demarshal_retvals__STRING (retvals, OUT_uid);
}

static gboolean
e_data_cal_gdbus_modify_object_sync (EDBusProxy  *proxy,
				     const char  *IN_object,
				     guint        IN_mod,
				     GError     **error)
{
        EVariant *parameters;
        EVariant *retvals;

        parameters = e_variant_new ("(su)", IN_object, IN_mod);
	retvals = e_dbus_proxy_invoke_method_sync (proxy, "modifyObject", parameters, -1, NULL, error);

        return demarshal_retvals__VOID (retvals);
}

static gboolean
e_data_cal_gdbus_remove_object_sync (EDBusProxy  *proxy,
				     const char  *IN_uid,
				     const char  *IN_rid,
				     guint        IN_mod,
				     GError     **error)
{
        EVariant *parameters;
        EVariant *retvals;

        parameters = e_variant_new ("(ssu)", IN_uid, IN_rid, IN_mod);
	retvals = e_dbus_proxy_invoke_method_sync (proxy, "removeObject", parameters, -1, NULL, error);

        return demarshal_retvals__VOID (retvals);
}

static gboolean
e_data_cal_gdbus_get_timezone_sync (EDBusProxy  *proxy,
				    const char  *IN_tzid,
				    char       **OUT_object,
				    GError     **error)
{
        EVariant *parameters;
        EVariant *retvals;

        parameters = e_variant_new ("(s)", IN_tzid);
	retvals = e_dbus_proxy_invoke_method_sync (proxy, "getTimezone", parameters, -1, NULL, error);

        return demarshal_retvals__STRING (retvals, OUT_object);
}

static gboolean
e_data_cal_gdbus_add_timezone_sync (EDBusProxy  *proxy,
				    const char  *IN_zone,
				    GError     **error)
{
        EVariant *parameters;
        EVariant *retvals;

        parameters = e_variant_new ("(s)", IN_zone);
	retvals = e_dbus_proxy_invoke_method_sync (proxy, "addTimezone", parameters, -1, NULL, error);

        return demarshal_retvals__VOID (retvals);
}

static gboolean
e_data_cal_gdbus_set_default_timezone_sync (EDBusProxy  *proxy,
					    const char  *IN_zone,
					    GError     **error)
{
        EVariant *parameters;
        EVariant *retvals;

        parameters = e_variant_new ("(s)", IN_zone);
	retvals = e_dbus_proxy_invoke_method_sync (proxy, "setDefaultTimezone", parameters, -1, NULL, error);

        return demarshal_retvals__VOID (retvals);
}

static gboolean
e_data_cal_gdbus_get_free_busy_sync (EDBusProxy    *proxy,
				     const char   **IN_users,
				     const guint    IN_start,
				     const guint    IN_end,
				     char        ***OUT_free_busy,
				     GError       **error)
{
	EVariant *parameters;
	EVariant *retvals;

	parameters = e_variant_new ("(^asuu)", IN_users, -1, IN_start, IN_end);

	retvals = e_dbus_proxy_invoke_method_sync (proxy, "getFreeBusy", parameters, -1, NULL, error);

	return demarshal_retvals__STRINGVECTOR (retvals, OUT_free_busy);
}

static gboolean
e_data_cal_gdbus_send_objects_sync (EDBusProxy   *proxy,
				    const char   *IN_object,
				    char       ***OUT_users,
				    char        **OUT_object,
				    GError      **error)
{
	EVariant *parameters;
	EVariant *retvals;

	parameters = e_variant_new ("(s)", IN_object);

	retvals = e_dbus_proxy_invoke_method_sync (proxy, "sendObjects", parameters, -1, NULL, error);

	return demarshal_retvals__STRINGVECTOR_STRING (retvals, OUT_users, OUT_object);
}

static gboolean
e_data_cal_gdbus_receive_objects_sync (EDBusProxy  *proxy,
				       const char  *IN_object,
				       GError     **error)
{
	EVariant *parameters;
	EVariant *retvals;

	parameters = e_variant_new ("(s)", IN_object);

	retvals = e_dbus_proxy_invoke_method_sync (proxy, "receiveObjects", parameters, -1, NULL, error);

	return demarshal_retvals__VOID (retvals);
}

static gboolean
e_data_cal_gdbus_get_query_sync (EDBusProxy  *proxy,
				 const char  *IN_sexp,
				 char       **OUT_path,
				 GError     **error)
{
	EVariant *parameters;
	EVariant *retvals;

	parameters = e_variant_new ("(s)", IN_sexp);

	retvals = e_dbus_proxy_invoke_method_sync (proxy, "getQuery", parameters, -1, NULL, error);

	return demarshal_retvals__OBJPATH (retvals, OUT_path);
}

static gboolean
e_data_cal_gdbus_discard_alarm_sync (EDBusProxy  *proxy,
				     const char  *IN_uid,
				     const char  *IN_auid,
				     GError     **error)
{
	EVariant *parameters;
	EVariant *retvals;

	parameters = e_variant_new ("(ss)", IN_uid, IN_auid);

	retvals = e_dbus_proxy_invoke_method_sync (proxy, "discardAlarm", parameters, -1, NULL, error);

	return demarshal_retvals__VOID (retvals);
}

static gboolean
e_data_cal_gdbus_get_changes_sync (EDBusProxy   *proxy,
				   const char   *IN_change_id,
				   char       ***OUT_additions,
				   char       ***OUT_modifications,
				   char       ***OUT_removals,
				   GError      **error)
{
	EVariant *parameters;
	EVariant *retvals;

	parameters = e_variant_new ("(s)", IN_change_id);

	retvals = e_dbus_proxy_invoke_method_sync (proxy, "getChanges", parameters, -1, NULL, error);

	return demarshal_retvals__STRINGVECTOR_STRINGVECTOR_STRINGVECTOR (retvals, OUT_additions, OUT_modifications, OUT_removals);
}

static gboolean
e_data_cal_gdbus_get_attachment_list_sync (EDBusProxy   *proxy,
					   const char   *IN_uid,
					   const char   *IN_rid,
					   char       ***OUT_attachments,
					   GError      **error)
{
	EVariant *parameters;
	EVariant *retvals;

	parameters = e_variant_new ("(ss)", IN_uid, IN_rid);

	retvals = e_dbus_proxy_invoke_method_sync (proxy, "getAttachmentList", parameters, -1, NULL, error);

	return demarshal_retvals__STRINGVECTOR (retvals, OUT_attachments);
}


G_END_DECLS
