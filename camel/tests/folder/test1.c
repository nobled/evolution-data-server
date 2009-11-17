/* store testing */

#include "camel-test.h"
#include "camel-test-provider.h"
#include "folders.h"
#include "session.h"

#include <camel/camel-exception.h>
#include <camel/camel-service.h>
#include <camel/camel-store.h>

static const gchar *local_drivers[] = {
	"local"
};

static gchar *local_providers[] = {
	"mbox",
	"mh",
	"maildir"
};

gint main(gint argc, gchar **argv)
{
	CamelSession *session;
	CamelException *ex;
	gint i;
	gchar *path;

	camel_test_init(argc, argv);
	camel_test_provider_init(1, local_drivers);

	ex = camel_exception_new();

	/* clear out any camel-test data */
	system("/bin/rm -rf /tmp/camel-test");

	session = camel_test_session_new ("/tmp/camel-test");

	/* todo: cross-check everything with folder_info checks as well */
	/* todo: subscriptions? */
	/* todo: work out how to do imap/pop/nntp tests */
	for (i = 0; i < G_N_ELEMENTS (local_providers); i++) {
		path = g_strdup_printf("%s:///tmp/camel-test/%s", local_providers[i], local_providers[i]);

		test_folder_basic(session, path, TRUE, FALSE);

		g_free(path);
	}

	g_object_unref (session);
	camel_exception_free(ex);

	return 0;
}
