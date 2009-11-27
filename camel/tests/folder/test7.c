/* folder testing */

#include "camel-test.h"
#include "camel-test-provider.h"
#include "folders.h"
#include "messages.h"
#include "session.h"

#include <camel/camel-service.h>
#include <camel/camel-store.h>

#include <camel/camel-folder.h>
#include <camel/camel-folder-summary.h>
#include <camel/camel-mime-message.h>

static const gchar *nntp_drivers[] = { "nntp" };
static gchar *remote_providers[] = {
	"NNTP_TEST_URL",
};

gint main(gint argc, gchar **argv)
{
	CamelSession *session;
	CamelException *ex;
	gint i;
	gchar *path;

	camel_test_init(argc, argv);
	camel_test_provider_init(1, nntp_drivers);

	/* clear out any camel-test data */
	system("/bin/rm -rf /tmp/camel-test");

	ex = camel_exception_new();

	session = camel_test_session_new ("/tmp/camel-test");

	for (i = 0; i < G_N_ELEMENTS (remote_providers); i++) {
		path = getenv(remote_providers[i]);

		if (path == NULL) {
			printf("Aborted (ignored).\n");
			printf("Set '%s', to re-run test.\n", remote_providers[i]);
			/* tells make check to ignore us in the total count */
			_exit(77);
		}
		camel_test_nonfatal("Dont know how many tests apply to NNTP");
		test_folder_message_ops(session, path, FALSE, "testbox");
		camel_test_fatal();
	}

	check_unref(session, 1);
	camel_exception_free(ex);

	return 0;
}
