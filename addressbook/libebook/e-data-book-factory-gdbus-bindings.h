#include <glib.h>
#include <gdbus/gdbus.h>

G_BEGIN_DECLS

/* FIXME: These bindings were created manually; replace with generated bindings
 * when possible */

static gboolean
e_data_book_factory_gdbus_get_book_sync (GDBusProxy *proxy, const char * IN_source, char** OUT_path, GError **error)
{
	GVariant *parameters;
	GVariant *retvals;
	gboolean success = TRUE;

	parameters = g_variant_new ("(s)", IN_source);
	retvals = g_dbus_proxy_invoke_method_sync (proxy, "getBook", parameters,
							-1, NULL, error);
	if (retvals) {
		char *const_path = NULL;

		g_variant_get (retvals, "(o)", &const_path);
		if (const_path) {
			*OUT_path = g_strdup (const_path);
		} else {
			success = FALSE;
		}

		g_variant_unref (retvals);
	} else {
		success = FALSE;
	}

	return success;
}

G_END_DECLS
