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

/* FIXME: use this for demarshalling that has return values */
#if 0
static gboolean
FUCNTION_NAME_demarshal_retvals (GVariant *retvals, char **OUT_path)
{
	gboolean success = TRUE;

	if (retvals) {
		/* FIXME: cut this */
#if 0
		/* FIXME: shouldn't we add const? */
		char *const_path = NULL;

		g_variant_get (retvals, "(o)", &const_path);
		if (const_path) {
			*OUT_path = g_strdup (const_path);
		} else {
			success = FALSE;
		}
#endif

		g_variant_unref (retvals);
	} else {
		success = FALSE;
	}

	return success;
}
#endif

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

G_END_DECLS
