#include <glib.h>
#include <gdbus/gdbus.h>

G_BEGIN_DECLS

/* FIXME: These bindings were created manually; replace with generated bindings
 * when possible */

typedef struct {
	GCallback cb;
	gpointer user_data;
} Closure;

static void
closure_free (Closure *closure)
{
	g_slice_free (Closure, closure);
}

static gboolean
demarshal_retvals__VOID (GVariant *retvals)
{
	gboolean success = TRUE;

	if (retvals)
		g_variant_unref (retvals);
	else
		success = FALSE;

	return success;
}

static gboolean
demarshal_retvals__STRING (GVariant *retvals, char **OUT_string1)
{
        gboolean success = TRUE;

        if (retvals) {
                const char *string1 = NULL;

                g_variant_get (retvals, "(s)", &string1);
                if (string1) {
                        *OUT_string1 = g_strdup (string1);
                } else {
                        success = FALSE;
                }

                g_variant_unref (retvals);
        } else {
                success = FALSE;
        }

        return success;
}

static gboolean
demarshal_retvals__STRINGVECTOR (GVariant *retvals, char ***OUT_strv1)
{
        gboolean success = TRUE;

        if (retvals) {
		GVariant *strv1_variant;
                char **strv1 = NULL;
		gint strv1_length;

		/* retvals contains a (as) with length 1; de-shell the
		 * array of strings from the tuple */
		strv1_variant = g_variant_get_child_value (retvals, 0);
                strv1 = g_variant_dup_strv (strv1_variant, &strv1_length);

                if (strv1) {
                        *OUT_strv1 = strv1;
                } else {
                        success = FALSE;
                }

                g_variant_unref (retvals);
        } else {
                success = FALSE;
        }

        return success;
}

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

typedef void (*e_data_book_gdbus_open_reply) (GDBusProxy *proxy,
		                              GError     *error,
					      gpointer    user_data);

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

	(*(e_data_book_gdbus_open_reply)closure->cb) (proxy, error, closure->user_data);
	closure_free (closure);
}

static void
e_data_book_gdbus_open (GDBusProxy                   *proxy,
			const gboolean                IN_only_if_exists,
			e_data_book_gdbus_open_reply  callback,
			gpointer                      user_data)
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

typedef void (*e_data_book_gdbus_remove_reply) (GDBusProxy *proxy,
						GError     *error,
						gpointer    user_data);

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

	(*(e_data_book_gdbus_remove_reply)closure->cb) (proxy, error, closure->user_data);
	closure_free (closure);
}

static void
e_data_book_gdbus_remove (GDBusProxy                     *proxy,
			  e_data_book_gdbus_remove_reply  callback,
			  gpointer                        user_data)
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

typedef void (*e_data_book_gdbus_get_contact_reply) (GDBusProxy *proxy,
						     char *OUT_vcard,
						     GError *error,
						     gpointer user_data);

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

        (*(e_data_book_gdbus_get_contact_reply) closure->cb) (proxy, OUT_vcard, error, closure->user_data);
        closure_free (closure);
}

static void
e_data_book_gdbus_get_contact (GDBusProxy                          *proxy,
			       const char                          *IN_uid,
			       e_data_book_gdbus_get_contact_reply  callback,
			       gpointer                             user_data)
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

typedef void (*e_data_book_gdbus_get_contact_list_reply) (GDBusProxy  *proxy,
							  char       **OUT_vcards,
							  GError      *error,
							  gpointer     user_data);

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

	(*(e_data_book_gdbus_get_contact_list_reply) closure->cb) (proxy, OUT_vcards, error, closure->user_data);
	closure_free (closure);
}

static void
e_data_book_gdbus_get_contact_list (GDBusProxy                               *proxy,
				    const char                               *IN_query,
				    e_data_book_gdbus_get_contact_list_reply  callback,
				    gpointer                                  user_data)
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

typedef void (*e_data_book_gdbus_get_required_fields_reply) (GDBusProxy  *proxy,
							     char       **OUT_fields,
							     GError      *error,
							     gpointer     user_data);

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

	(*(e_data_book_gdbus_get_required_fields_reply) closure->cb) (proxy, OUT_fields, error, closure->user_data);
	closure_free (closure);
}

static void
e_data_book_gdbus_get_required_fields (GDBusProxy                                  *proxy,
				       e_data_book_gdbus_get_required_fields_reply  callback,
				       gpointer                                     user_data)
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

typedef void (*e_data_book_gdbus_add_contact_reply) (GDBusProxy *proxy,
						     char       *OUT_uid,
						     GError     *error,
						     gpointer    user_data);

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

        (*(e_data_book_gdbus_add_contact_reply) closure->cb) (proxy, OUT_uid, error, closure->user_data);
        closure_free (closure);
}

static void
e_data_book_gdbus_add_contact (GDBusProxy                          *proxy,
			       const char                          *IN_vcard,
			       e_data_book_gdbus_add_contact_reply  callback,
			       gpointer                             user_data)
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

typedef void (*e_data_book_gdbus_modify_contact_reply) (GDBusProxy *proxy,
							GError     *error,
							gpointer    user_data);

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

        (*(e_data_book_gdbus_modify_contact_reply) closure->cb) (proxy, error, closure->user_data);
        closure_free (closure);
}

static void
e_data_book_gdbus_modify_contact (GDBusProxy				 *proxy,
				  const char				 *IN_vcard,
				  e_data_book_gdbus_modify_contact_reply  callback,
				  gpointer                                user_data)
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

typedef void (*e_data_book_gdbus_remove_contacts_reply) (GDBusProxy *proxy,
						         GError     *error,
						         gpointer    user_data);

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

        (*(e_data_book_gdbus_remove_contacts_reply) closure->cb) (proxy,  error, closure->user_data);
        closure_free (closure);
}

static void
e_data_book_gdbus_remove_contacts (GDBusProxy                               *proxy,
			           const char                              **IN_uids,
			           e_data_book_gdbus_remove_contacts_reply   callback,
			           gpointer                                  user_data)
{
        GVariant *parameters;
        Closure *closure;

        parameters = g_variant_new ("(^as)", IN_uids);

        closure = g_slice_new (Closure);
        closure->cb = G_CALLBACK (callback);
        closure->user_data = user_data;

        g_dbus_proxy_invoke_method (proxy, "removeContacts", parameters, -1, NULL, (GAsyncReadyCallback) remove_contacts_cb, closure);
}

G_END_DECLS
