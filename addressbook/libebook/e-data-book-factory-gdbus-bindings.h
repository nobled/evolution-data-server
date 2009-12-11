#include <glib.h>
#include <gdbus/gdbus.h>

#include "e-data-book-gdbus-bindings-common.h"

G_BEGIN_DECLS

/* FIXME: These bindings were created manually; replace with generated bindings
 * when possible */

static gboolean
e_data_book_factory_gdbus_get_book_sync (GDBusProxy *proxy, const char * IN_source, char** OUT_path, GError **error)
{
	GVariant *parameters;
	GVariant *retvals;

	parameters = g_variant_new ("(s)", IN_source);
	retvals = g_dbus_proxy_invoke_method_sync (proxy, "getBook", parameters,
							-1, NULL, error);

	return demarshal_retvals__OBJPATH (retvals, OUT_path);
}

G_END_DECLS
