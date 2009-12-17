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
#include <gdbus/gdbus.h>

#include <libedata-gdbus-bindings/e-data-gdbus-bindings-common.h>

G_BEGIN_DECLS

/* FIXME: These bindings were created manually; replace with generated bindings
 * when possible */

static gboolean
e_data_cal_gdbus_is_read_only_sync (GDBusProxy  *proxy,
				    GError     **error)
{
        GVariant *parameters;
        GVariant *retvals;

        parameters = g_variant_new ("()");
	retvals = g_dbus_proxy_invoke_method_sync (proxy, "isReadOnly",
			parameters, -1, NULL, error);

        return demarshal_retvals__VOID (retvals);
}

static gboolean
e_data_cal_gdbus_close_sync (GDBusProxy  *proxy,
                             GError     **error)
{
        GVariant *parameters;
        GVariant *retvals;

        parameters = g_variant_new ("()");
        retvals = g_dbus_proxy_invoke_method_sync (proxy, "close", parameters,
                                                        -1, NULL, error);

        return demarshal_retvals__VOID (retvals);
}

static gboolean
e_data_cal_gdbus_open_sync (GDBusProxy      *proxy,
                            const gboolean   IN_only_if_exists,
                            const char      *IN_username,
                            const char      *IN_password,
                            GError         **error)
{
        GVariant *parameters;
        GVariant *retvals;

        parameters = g_variant_new ("(bss)", IN_only_if_exists, IN_username, IN_password);
        retvals = g_dbus_proxy_invoke_method_sync (proxy, "open", parameters,
                                                        -1, NULL, error);

        return demarshal_retvals__VOID (retvals);
}

static void
open_cb (GDBusProxy *proxy,
         GAsyncResult *result,
         gpointer user_data)
{
        Closure *closure = user_data;
        GVariant *retvals;
        GError *error = NULL;

        retvals = g_dbus_proxy_invoke_method_finish (proxy, result, &error);
        if (retvals) {
                if (!demarshal_retvals__VOID (retvals)) {
                        error = g_error_new (E_CALENDAR_ERROR, E_CALENDAR_STATUS_CORBA_EXCEPTION, "demarshalling results for Calendar method 'open'");
                }
        }

        (*(reply__VOID)closure->cb) (proxy, error, closure->user_data);
        closure_free (closure);
}

static gboolean
e_data_cal_gdbus_open (GDBusProxy      *proxy,
                       const gboolean   IN_only_if_exists,
		       const char      *IN_username,
		       const char      *IN_password,
                       reply__VOID      callback,
                       gpointer         user_data)
{
        GVariant *parameters;
        Closure *closure;

        parameters = g_variant_new ("(bss)", IN_only_if_exists, IN_username, IN_password);

        closure = g_slice_new (Closure);
        closure->cb = G_CALLBACK (callback);
        closure->user_data = user_data;

        g_dbus_proxy_invoke_method (proxy, "open", parameters, -1, NULL, (GAsyncReadyCallback) open_cb, closure);

	return TRUE;
}

static gboolean
e_data_cal_gdbus_remove_sync (GDBusProxy  *proxy,
                              GError     **error)
{
        GVariant *parameters;
        GVariant *retvals;

        parameters = g_variant_new ("()");
        retvals = g_dbus_proxy_invoke_method_sync (proxy, "remove", parameters,
                                                        -1, NULL, error);

        return demarshal_retvals__VOID (retvals);
}

static gboolean
e_data_cal_gdbus_get_alarm_email_address_sync (GDBusProxy  *proxy,
					       char       **address,
					       GError     **error)
{
        GVariant *parameters;
        GVariant *retvals;

        parameters = g_variant_new ("()");
	retvals = g_dbus_proxy_invoke_method_sync (proxy, "getAlarmEmailAddress", parameters, -1, NULL, error);

        return demarshal_retvals__STRING (retvals, address);
}

static gboolean
e_data_cal_gdbus_get_cal_address_sync (GDBusProxy  *proxy,
				       char       **address,
				       GError     **error)
{
        GVariant *parameters;
        GVariant *retvals;

        parameters = g_variant_new ("()");
	retvals = g_dbus_proxy_invoke_method_sync (proxy, "getCalAddress", parameters, -1, NULL, error);

        return demarshal_retvals__STRING (retvals, address);
}

static gboolean
e_data_cal_gdbus_get_ldap_attribute_sync (GDBusProxy  *proxy,
					  char       **attr,
					  GError     **error)
{
        GVariant *parameters;
        GVariant *retvals;

        parameters = g_variant_new ("()");
	retvals = g_dbus_proxy_invoke_method_sync (proxy, "getLdapAttribute", parameters, -1, NULL, error);

        return demarshal_retvals__STRING (retvals, attr);
}

G_END_DECLS
