#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "session.h"

GType
camel_test_session_get_type (void)
{
	static GType type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID))
		type = g_type_register_static_simple (
			CAMEL_TYPE_SESSION,
			"CamelTestSession",
			sizeof (CamelTestSessionClass),
			(GClassInitFunc) NULL,
			sizeof (CamelTestSession),
			(GInstanceInitFunc) NULL,
			0);

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
