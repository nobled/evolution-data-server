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
open_demarshal_retvals (GVariant *retvals)
{
	gboolean success = TRUE;

	if (retvals)
		g_variant_unref (retvals);
	else
		success = FALSE;

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

	return open_demarshal_retvals (retvals);
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
		if (!open_demarshal_retvals (retvals)) {
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
remove_demarshal_retvals (GVariant *retvals)
{
	gboolean success = TRUE;

	if (retvals)
		g_variant_unref (retvals);
	else
		success = FALSE;

	return success;
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

	return remove_demarshal_retvals (retvals);
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
		if (!remove_demarshal_retvals (retvals)) {
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

G_END_DECLS
