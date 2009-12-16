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
e_data_book_gdbus_open_sync (GDBusProxy      *proxy,
		             const gboolean   IN_only_if_exists,
			     GError         **error)
{
	GVariant *parameters;
	GVariant *retvals;

	parameters = g_variant_new ("(b)", IN_only_if_exists);
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
			error = g_error_new (E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION, "demarshalling results for Book method 'open'");
		}
	}

	(*(reply__VOID)closure->cb) (proxy, error, closure->user_data);
	closure_free (closure);
}

static void
e_data_book_gdbus_open (GDBusProxy     *proxy,
			const gboolean  IN_only_if_exists,
			reply__VOID     callback,
			gpointer        user_data)
{
	GVariant *parameters;
	Closure *closure;

	parameters = g_variant_new ("(b)", IN_only_if_exists);

	closure = g_slice_new (Closure);
	closure->cb = G_CALLBACK (callback);
	closure->user_data = user_data;

	g_dbus_proxy_invoke_method (proxy, "open", parameters, -1, NULL, (GAsyncReadyCallback) open_cb, closure);
}

static gboolean
e_data_book_gdbus_close_sync (GDBusProxy      *proxy,
			      GError         **error)
{
	GVariant *parameters;
	GVariant *retvals;

	parameters = g_variant_new ("()");
	retvals = g_dbus_proxy_invoke_method_sync (proxy, "close", parameters,
							-1, NULL, error);

	return demarshal_retvals__VOID (retvals);
}

static gboolean
e_data_book_gdbus_authenticate_user_sync (GDBusProxy  *proxy,
					  const char  *IN_user,
					  const char  *IN_passwd,
					  const char  *IN_auth_method,
					  GError     **error)
{
	GVariant *parameters;
	GVariant *retvals;

	parameters = g_variant_new ("(sss)", IN_user, IN_passwd, IN_auth_method);
	retvals = g_dbus_proxy_invoke_method_sync (proxy, "authenticateUser", parameters, -1, NULL, error);

	return demarshal_retvals__VOID (retvals);
}

static void
authenticate_user_cb (GDBusProxy   *proxy,
		      GAsyncResult *result,
		      gpointer      user_data)
{
	Closure *closure = user_data;
	GVariant *retvals;
	GError *error = NULL;

	retvals = g_dbus_proxy_invoke_method_finish (proxy, result, &error);
	if (retvals) {
		if (!demarshal_retvals__VOID (retvals)) {
			error = g_error_new (E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION, "demarshalling results for Book method 'authenticateUser'");
		}
	}

	(*(reply__VOID)closure->cb) (proxy, error, closure->user_data);
	closure_free (closure);
}

static void
e_data_book_gdbus_authenticate_user (GDBusProxy  *proxy,
				     const char  *IN_user,
				     const char  *IN_passwd,
				     const char  *IN_auth_method,
				     reply__VOID  callback,
				     gpointer     user_data)
{
	GVariant *parameters;
	Closure *closure;

	parameters = g_variant_new ("(sss)", IN_user, IN_passwd, IN_auth_method);

	closure = g_slice_new (Closure);
	closure->cb = G_CALLBACK (callback);
	closure->user_data = user_data;

	g_dbus_proxy_invoke_method (proxy, "authenticateUser", parameters, -1, NULL, (GAsyncReadyCallback) authenticate_user_cb, closure);
}

static gboolean
e_data_book_gdbus_remove_sync (GDBusProxy  *proxy,
			       GError     **error)
{
	GVariant *parameters;
	GVariant *retvals;

	parameters = g_variant_new ("()");
	retvals = g_dbus_proxy_invoke_method_sync (proxy, "remove", parameters,
							-1, NULL, error);

	return demarshal_retvals__VOID (retvals);
}

static void
remove_cb (GDBusProxy   *proxy,
	   GAsyncResult *result,
	   gpointer      user_data)
{
	Closure *closure = user_data;
	GVariant *retvals;
	GError *error = NULL;

	retvals = g_dbus_proxy_invoke_method_finish (proxy, result, &error);
	if (retvals) {
		if (!demarshal_retvals__VOID (retvals)) {
			error = g_error_new (E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION, "demarshalling results for Book method 'remove'");
		}
	}

	(*(reply__VOID)closure->cb) (proxy, error, closure->user_data);
	closure_free (closure);
}

static void
e_data_book_gdbus_remove (GDBusProxy  *proxy,
			  reply__VOID  callback,
			  gpointer     user_data)
{
	GVariant *parameters;
	Closure *closure;

	parameters = g_variant_new ("()");

	closure = g_slice_new (Closure);
	closure->cb = G_CALLBACK (callback);
	closure->user_data = user_data;

	g_dbus_proxy_invoke_method (proxy, "remove", parameters, -1, NULL, (GAsyncReadyCallback) remove_cb, closure);
}

static gboolean
e_data_book_gdbus_get_contact_sync (GDBusProxy  *proxy,
				    const char  *IN_uid,
				    char       **OUT_vcard,
			            GError     **error)
{
	GVariant *parameters;
	GVariant *retvals;

	parameters = g_variant_new ("(s)", IN_uid);
	retvals = g_dbus_proxy_invoke_method_sync (proxy, "getContact", parameters, -1, NULL, error);

	return demarshal_retvals__STRING (retvals, OUT_vcard);
}

static void
get_contact_cb (GDBusProxy *proxy,
		GAsyncResult *result,
		gpointer user_data)
{
        Closure *closure = user_data;
        GVariant *retvals;
        GError *error = NULL;
        char *OUT_vcard = NULL;

        retvals = g_dbus_proxy_invoke_method_finish (proxy, result, &error);
        if (retvals) {
                if (!demarshal_retvals__STRING (retvals, &OUT_vcard)) {
                        error = g_error_new (E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION, "demarshalling results for Book method 'getContact'");
                }
        }

        (*(reply__STRING) closure->cb) (proxy, OUT_vcard, error, closure->user_data);
        closure_free (closure);
}

static void
e_data_book_gdbus_get_contact (GDBusProxy    *proxy,
			       const char    *IN_uid,
			       reply__STRING  callback,
			       gpointer       user_data)
{
        GVariant *parameters;
        Closure *closure;

        parameters = g_variant_new ("(s)", IN_uid);

        closure = g_slice_new (Closure);
        closure->cb = G_CALLBACK (callback);
        closure->user_data = user_data;

        g_dbus_proxy_invoke_method (proxy, "getContact", parameters, -1, NULL, (GAsyncReadyCallback) get_contact_cb, closure);
}

static gboolean
e_data_book_gdbus_get_static_capabilities_sync (GDBusProxy  *proxy,
						char       **OUT_caps,
						GError     **error)
{
	GVariant *parameters;
	GVariant *retvals;

	parameters = g_variant_new ("()");
	retvals = g_dbus_proxy_invoke_method_sync (proxy, "getStaticCapabilities", parameters, -1, NULL, error);

	return demarshal_retvals__STRING (retvals, OUT_caps);
}

static gboolean
e_data_book_gdbus_get_contact_list_sync (GDBusProxy   *proxy,
					 const char   *IN_query,
					 char       ***OUT_vcards,
					 GError      **error)
{
	GVariant *parameters;
	GVariant *retvals;

	parameters = g_variant_new ("(s)", IN_query);
	retvals = g_dbus_proxy_invoke_method_sync (proxy, "getContactList", parameters, -1, NULL, error);

	return demarshal_retvals__STRINGVECTOR (retvals, OUT_vcards);
}

static void
get_contact_list_cb (GDBusProxy   *proxy,
		     GAsyncResult *result,
		     gpointer      user_data)
{
        Closure *closure = user_data;
        GVariant *retvals;
        GError *error = NULL;
        char **OUT_vcards = NULL;

        retvals = g_dbus_proxy_invoke_method_finish (proxy, result, &error);
	if (retvals) {
		if (!demarshal_retvals__STRINGVECTOR (retvals, &OUT_vcards)) {
			error = g_error_new (E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION, "demarshalling results for Book method 'getContactList'");
		}
	}

	(*(reply__STRINGVECTOR) closure->cb) (proxy, OUT_vcards, error, closure->user_data);
	closure_free (closure);
}

static void
e_data_book_gdbus_get_contact_list (GDBusProxy          *proxy,
				    const char          *IN_query,
				    reply__STRINGVECTOR  callback,
				    gpointer             user_data)
{
        GVariant *parameters;
        Closure *closure;

        parameters = g_variant_new ("(s)", IN_query);

        closure = g_slice_new (Closure);
        closure->cb = G_CALLBACK (callback);
        closure->user_data = user_data;

        g_dbus_proxy_invoke_method (proxy, "getContactList", parameters, -1, NULL, (GAsyncReadyCallback) get_contact_list_cb, closure);
}

static gboolean
e_data_book_gdbus_get_required_fields_sync (GDBusProxy   *proxy,
					    char       ***OUT_fields,
					    GError      **error)
{
	GVariant *parameters;
	GVariant *retvals;

	parameters = g_variant_new ("()");
	retvals = g_dbus_proxy_invoke_method_sync (proxy, "getRequiredFields", parameters, -1, NULL, error);

	return demarshal_retvals__STRINGVECTOR (retvals, OUT_fields);
}

static void
get_required_fields_cb (GDBusProxy   *proxy,
			GAsyncResult *result,
			gpointer      user_data)
{
        Closure *closure = user_data;
        GVariant *retvals;
        GError *error = NULL;
        char **OUT_fields = NULL;

        retvals = g_dbus_proxy_invoke_method_finish (proxy, result, &error);
	if (retvals) {
		if (!demarshal_retvals__STRINGVECTOR (retvals, &OUT_fields)) {
			error = g_error_new (E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION, "demarshalling results for Book method 'getRequiredFields'");
		}
	}

	(*(reply__STRINGVECTOR) closure->cb) (proxy, OUT_fields, error, closure->user_data);
	closure_free (closure);
}

static void
e_data_book_gdbus_get_required_fields (GDBusProxy          *proxy,
				       reply__STRINGVECTOR  callback,
				       gpointer             user_data)
{
        GVariant *parameters;
        Closure *closure;

        parameters = g_variant_new ("()");

        closure = g_slice_new (Closure);
        closure->cb = G_CALLBACK (callback);
        closure->user_data = user_data;

        g_dbus_proxy_invoke_method (proxy, "getRequiredFields", parameters, -1, NULL, (GAsyncReadyCallback) get_required_fields_cb, closure);
}

static gboolean
e_data_book_gdbus_get_supported_auth_methods_sync (GDBusProxy   *proxy,
						   char       ***OUT_methods,
						   GError      **error)
{
	GVariant *parameters;
	GVariant *retvals;

	parameters = g_variant_new ("()");
	retvals = g_dbus_proxy_invoke_method_sync (proxy, "getSupportedAuthMethods", parameters, -1, NULL, error);

	return demarshal_retvals__STRINGVECTOR (retvals, OUT_methods);
}

static void
get_supported_auth_methods_cb (GDBusProxy   *proxy,
			       GAsyncResult *result,
			       gpointer      user_data)
{
        Closure *closure = user_data;
        GVariant *retvals;
        GError *error = NULL;
        char **OUT_methods = NULL;

        retvals = g_dbus_proxy_invoke_method_finish (proxy, result, &error);
	if (retvals) {
		if (!demarshal_retvals__STRINGVECTOR (retvals, &OUT_methods)) {
			error = g_error_new (E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION, "demarshalling results for Book method 'getSupportedAuthMethods'");
		}
	}

	(*(reply__STRINGVECTOR) closure->cb) (proxy, OUT_methods, error, closure->user_data);
	closure_free (closure);
}

static void
e_data_book_gdbus_get_supported_auth_methods (GDBusProxy          *proxy,
					      reply__STRINGVECTOR  callback,
					      gpointer             user_data)
{
        GVariant *parameters;
        Closure *closure;

        parameters = g_variant_new ("()");

        closure = g_slice_new (Closure);
        closure->cb = G_CALLBACK (callback);
        closure->user_data = user_data;

        g_dbus_proxy_invoke_method (proxy, "getSupportedAuthMethods", parameters, -1, NULL, (GAsyncReadyCallback) get_supported_auth_methods_cb, closure);
}

static gboolean
e_data_book_gdbus_get_supported_fields_sync (GDBusProxy   *proxy,
					     char       ***OUT_fields,
					     GError      **error)
{
	GVariant *parameters;
	GVariant *retvals;

	parameters = g_variant_new ("()");
	retvals = g_dbus_proxy_invoke_method_sync (proxy, "getSupportedFields", parameters, -1, NULL, error);

	return demarshal_retvals__STRINGVECTOR (retvals, OUT_fields);
}

static void
get_supported_fields_cb (GDBusProxy   *proxy,
			 GAsyncResult *result,
			 gpointer      user_data)
{
        Closure *closure = user_data;
        GVariant *retvals;
        GError *error = NULL;
        char **OUT_fields = NULL;

        retvals = g_dbus_proxy_invoke_method_finish (proxy, result, &error);
	if (retvals) {
		if (!demarshal_retvals__STRINGVECTOR (retvals, &OUT_fields)) {
			error = g_error_new (E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION, "demarshalling results for Book method 'getSupportedFields'");
		}
	}

	(*(reply__STRINGVECTOR) closure->cb) (proxy, OUT_fields, error, closure->user_data);
	closure_free (closure);
}

static void
e_data_book_gdbus_get_supported_fields (GDBusProxy          *proxy,
					reply__STRINGVECTOR  callback,
					gpointer             user_data)
{
        GVariant *parameters;
        Closure *closure;

        parameters = g_variant_new ("()");

        closure = g_slice_new (Closure);
        closure->cb = G_CALLBACK (callback);
        closure->user_data = user_data;

        g_dbus_proxy_invoke_method (proxy, "getSupportedFields", parameters, -1, NULL, (GAsyncReadyCallback) get_supported_fields_cb, closure);
}

static gboolean
e_data_book_gdbus_add_contact_sync (GDBusProxy  *proxy,
				    const char  *IN_vcard,
				    char       **OUT_uid,
			            GError     **error)
{
	GVariant *parameters;
	GVariant *retvals;

	parameters = g_variant_new ("(s)", IN_vcard);
	retvals = g_dbus_proxy_invoke_method_sync (proxy, "addContact", parameters, -1, NULL, error);

	return demarshal_retvals__STRING (retvals, OUT_uid);
}

static void
add_contact_cb (GDBusProxy   *proxy,
		GAsyncResult *result,
		gpointer      user_data)
{
        Closure *closure = user_data;
        GVariant *retvals;
        GError *error = NULL;
        char *OUT_uid = NULL;

        retvals = g_dbus_proxy_invoke_method_finish (proxy, result, &error);
        if (retvals) {
                if (!demarshal_retvals__STRING (retvals, &OUT_uid)) {
                        error = g_error_new (E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION, "demarshalling results for Book method 'addContact'");
                }
        }

        (*(reply__STRING) closure->cb) (proxy, OUT_uid, error, closure->user_data);
        closure_free (closure);
}

static void
e_data_book_gdbus_add_contact (GDBusProxy    *proxy,
			       const char    *IN_vcard,
			       reply__STRING  callback,
			       gpointer       user_data)
{
        GVariant *parameters;
        Closure *closure;

        parameters = g_variant_new ("(s)", IN_vcard);

        closure = g_slice_new (Closure);
        closure->cb = G_CALLBACK (callback);
        closure->user_data = user_data;

        g_dbus_proxy_invoke_method (proxy, "addContact", parameters, -1, NULL, (GAsyncReadyCallback) add_contact_cb, closure);
}

static gboolean
e_data_book_gdbus_modify_contact_sync (GDBusProxy  *proxy,
				       const char  *IN_vcard,
				       GError     **error)
{
	GVariant *parameters;
	GVariant *retvals;

	parameters = g_variant_new ("(s)", IN_vcard);
	retvals = g_dbus_proxy_invoke_method_sync (proxy, "modifyContact", parameters, -1, NULL, error);

	return demarshal_retvals__VOID (retvals);
}

static void
modify_contact_cb (GDBusProxy   *proxy,
		   GAsyncResult *result,
		   gpointer      user_data)
{
        Closure *closure = user_data;
        GVariant *retvals;
        GError *error = NULL;

        retvals = g_dbus_proxy_invoke_method_finish (proxy, result, &error);
        if (retvals) {
                if (!demarshal_retvals__VOID (retvals)) {
                        error = g_error_new (E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION, "demarshalling results for Book method 'modifyContact'");
                }
        }

        (*(reply__VOID) closure->cb) (proxy, error, closure->user_data);
        closure_free (closure);
}

static void
e_data_book_gdbus_modify_contact (GDBusProxy  *proxy,
				  const char  *IN_vcard,
				  reply__VOID  callback,
				  gpointer     user_data)
{
        GVariant *parameters;
        Closure *closure;

        parameters = g_variant_new ("(s)", IN_vcard);

        closure = g_slice_new (Closure);
        closure->cb = G_CALLBACK (callback);
        closure->user_data = user_data;

        g_dbus_proxy_invoke_method (proxy, "modifyContact", parameters, -1, NULL, (GAsyncReadyCallback) modify_contact_cb, closure);
}

static gboolean
e_data_book_gdbus_remove_contacts_sync (GDBusProxy  *proxy,
					const char **IN_uids,
					GError     **error)
{
	GVariant *parameters;
	GVariant *retvals;

	g_return_val_if_fail (IN_uids && IN_uids[0], FALSE);

	parameters = g_variant_new ("(^as)", IN_uids);

	retvals = g_dbus_proxy_invoke_method_sync (proxy, "removeContacts", parameters, -1, NULL, error);

	return demarshal_retvals__VOID (retvals);
}

static void
remove_contacts_cb (GDBusProxy   *proxy,
		    GAsyncResult *result,
		    gpointer      user_data)
{
        Closure *closure = user_data;
        GVariant *retvals;
        GError *error = NULL;

        retvals = g_dbus_proxy_invoke_method_finish (proxy, result, &error);
        if (retvals) {
                if (!demarshal_retvals__VOID (retvals)) {
                        error = g_error_new (E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION, "demarshalling results for Book method 'removeContacts'");
                }
        }

        (*(reply__VOID) closure->cb) (proxy,  error, closure->user_data);
        closure_free (closure);
}

static void
e_data_book_gdbus_remove_contacts (GDBusProxy   *proxy,
			           const char  **IN_uids,
			           reply__VOID   callback,
			           gpointer      user_data)
{
        GVariant *parameters;
        Closure *closure;

        parameters = g_variant_new ("(^as)", IN_uids);

        closure = g_slice_new (Closure);
        closure->cb = G_CALLBACK (callback);
        closure->user_data = user_data;

        g_dbus_proxy_invoke_method (proxy, "removeContacts", parameters, -1, NULL, (GAsyncReadyCallback) remove_contacts_cb, closure);
}

static gboolean
e_data_book_gdbus_get_book_view_sync (GDBusProxy   *proxy,
				      const char   *IN_query,
				      const guint   IN_max_results,
				      char        **OUT_view,
				      GError      **error)
{
	GVariant *parameters;
	GVariant *retvals;

	parameters = g_variant_new ("(su)", IN_query, IN_max_results);
	retvals = g_dbus_proxy_invoke_method_sync (proxy, "getBookView", parameters, -1, NULL, error);

	return demarshal_retvals__OBJPATH (retvals, OUT_view);
}

static void
get_book_view_cb (GDBusProxy *proxy,
		  GAsyncResult *result,
		  gpointer user_data)
{
        Closure *closure = user_data;
        GVariant *retvals;
        GError *error = NULL;
        char *OUT_view = NULL;

        retvals = g_dbus_proxy_invoke_method_finish (proxy, result, &error);
        if (retvals) {
                if (!demarshal_retvals__OBJPATH (retvals, &OUT_view)) {
                        error = g_error_new (E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION, "demarshalling results for Book method 'getBookView'");
                }
        }

        (*(reply__OBJPATH) closure->cb) (proxy, OUT_view, error, closure->user_data);
        closure_free (closure);
}

static void
e_data_book_gdbus_get_book_view (GDBusProxy    *proxy,
				 const char    *IN_query,
				 const guint    IN_max_results,
				 reply__STRING  callback,
				 gpointer       user_data)
{
        GVariant *parameters;
        Closure *closure;

        parameters = g_variant_new ("(su)", IN_query, IN_max_results);

        closure = g_slice_new (Closure);
        closure->cb = G_CALLBACK (callback);
        closure->user_data = user_data;

        g_dbus_proxy_invoke_method (proxy, "getBookView", parameters, -1, NULL, (GAsyncReadyCallback) get_book_view_cb, closure);
}

static gboolean
e_data_book_gdbus_cancel_operation_sync (GDBusProxy  *proxy,
					 GError     **error)
{
	GVariant *parameters;
	GVariant *retvals;

	parameters = g_variant_new ("()");
	retvals = g_dbus_proxy_invoke_method_sync (proxy, "cancelOperation", parameters, -1, NULL, error);

	return demarshal_retvals__VOID (retvals);
}

static gboolean
e_data_book_gdbus_get_changes_sync (GDBusProxy  *proxy,
				    const char  *IN_change_id,
				    GPtrArray  **OUT_changes,
				    GError     **error)
{
	GVariant *parameters;
	GVariant *retvals;

	parameters = g_variant_new ("(s)", IN_change_id);
	retvals = g_dbus_proxy_invoke_method_sync (proxy, "getChanges", parameters, -1, NULL, error);

	return demarshal_retvals__GPTRARRAY_with_GVALUEARRAY_with_UINT_STRING_endwith_endwith (retvals, OUT_changes);
}

static void
get_changes_cb (GDBusProxy *proxy,
		GAsyncResult *result,
		gpointer user_data)
{
        Closure *closure = user_data;
        GVariant *retvals;
        GError *error = NULL;
        GPtrArray *OUT_changes = NULL;

        retvals = g_dbus_proxy_invoke_method_finish (proxy, result, &error);
        if (retvals) {
                if (!demarshal_retvals__GPTRARRAY_with_GVALUEARRAY_with_UINT_STRING_endwith_endwith (retvals, &OUT_changes)) {
                        error = g_error_new (E_BOOK_ERROR, E_BOOK_ERROR_CORBA_EXCEPTION, "demarshalling results for Book method 'getChanges'");
                }
        }

        (*(reply__GPTRARRAY_with_GVALUEARRAY_with_UINT_STRING_endwith_endwith) closure->cb) (proxy, OUT_changes, error, closure->user_data);
        closure_free (closure);
}

static void
e_data_book_gdbus_get_changes (GDBusProxy    *proxy,
			       const char    *IN_change_id,
			       reply__GPTRARRAY_with_GVALUEARRAY_with_UINT_STRING_endwith_endwith  callback,
			       gpointer       user_data)
{
        GVariant *parameters;
        Closure *closure;

	parameters = g_variant_new ("(s)", IN_change_id);

        closure = g_slice_new (Closure);
        closure->cb = G_CALLBACK (callback);
        closure->user_data = user_data;

        g_dbus_proxy_invoke_method (proxy, "getChanges", parameters, -1, NULL, (GAsyncReadyCallback) get_changes_cb, closure);
}


G_END_DECLS
