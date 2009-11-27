#include <glib.h>
#include "camel-imapx-store.h"
#include "camel-imapx-folder.h"
#include <camel/camel.h>
#include <camel/camel-session.h>

gint
main (gint argc, gchar *argv [])
{
	CamelSession *session;
	GError **error;
	gchar *uri = NULL;
	CamelService *service;

	if (argc != 2) {
		printf ("Pass the account url argument \n");
		return -1;
	}

	uri = argv [1];
	g_thread_init (NULL);
	system ("rm -rf /tmp/test-camel-imapx");
	camel_init ("/tmp/test-camel-imapx", 0);
	camel_provider_init ();
	ex = camel_exception_new ();

	session = CAMEL_SESSION (camel_object_new (CAMEL_SESSION_TYPE));
	camel_session_construct (session, "/tmp/test-camel-imapx");

	service = camel_session_get_service (session, uri, CAMEL_PROVIDER_STORE, ex);
	camel_service_connect (service, ex);

	while (1)
	{
	}
	return 0;
}
