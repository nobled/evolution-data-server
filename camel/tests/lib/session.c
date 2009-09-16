#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "session.h"

static void
class_init (CamelTestSessionClass *camel_test_session_class)
{
	CamelSessionClass *camel_session_class =
		CAMEL_SESSION_CLASS (camel_test_session_class);
}

GType
camel_test_session_get_type (void)
{
	static GType type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID))
		type = camel_type_register (
			CAMEL_TYPE_SESSION,
			"CamelTestSession",
			sizeof (CamelTestSession),
			sizeof (CamelTestSessionClass),
			(GClassInitFunc) class_init,
			NULL,
			NULL,
			NULL);

	return type;
}

CamelSession *
camel_test_session_new (const gchar *path)
{
	CamelSession *session;

	session = g_object_new (CAMEL_TYPE_TEST_SESSION, NULL);
	camel_session_construct (session, path);

	return session;
}
