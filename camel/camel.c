/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *           Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <signal.h>

#ifdef HAVE_NSS
#include <nspr.h>
#include <prthread.h>
#include "nss.h"      /* Don't use <> here or it will include the system nss.h instead */
#include <ssl.h>
#endif /* HAVE_NSS */

#include <glib/gi18n-lib.h>

#include "camel.h"
#include "camel-certdb.h"
#include "camel-debug.h"
#include "camel-provider.h"
#include "camel-private.h"

#ifdef HAVE_NSS
/* To protect NSS initialization and shutdown. This prevents
   concurrent calls to shutdown() and init() by different threads */
PRLock *nss_initlock = NULL;

/* Whether or not Camel has initialized the NSS library. We cannot
   unconditionally call NSS_Shutdown() if NSS was initialized by other
   library before. This boolean ensures that we only perform a cleanup
   if and only if Camel is the one that previously initialized NSS */
volatile gboolean nss_initialized = FALSE;
#endif

static gint initialised = FALSE;

gint camel_application_is_exiting = FALSE;

gint
camel_init (const gchar *configdir, gboolean nss_init)
{
	CamelCertDB *certdb;
	gchar *path;

	if (initialised)
		return 0;

	bindtextdomain (GETTEXT_PACKAGE, EVOLUTION_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	camel_debug_init();

	/* initialise global camel_object_type */
	CAMEL_TYPE_OBJECT;

#ifdef HAVE_NSS
	if (nss_init) {
		gchar *nss_configdir;
		PRUint16 indx;

		if (nss_initlock == NULL) {
			PR_Init(PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 10);
			nss_initlock = PR_NewLock();
		}
		PR_Lock (nss_initlock);

#ifndef G_OS_WIN32
		nss_configdir = g_strdup (configdir);
#else
		nss_configdir = g_win32_locale_filename_from_utf8 (configdir);
#endif

		if (!NSS_IsInitialized()) {
			nss_initialized = 1;

			if (NSS_InitReadWrite (nss_configdir) == SECFailure) {
				/* fall back on using volatile dbs? */
				if (NSS_NoDB_Init (nss_configdir) == SECFailure) {
					g_free (nss_configdir);
					g_warning ("Failed to initialize NSS");
					nss_initialized = 0;
					PR_Unlock(nss_initlock);
					return -1;
				}
			}
		}

		NSS_SetDomesticPolicy ();

		PR_Unlock(nss_initlock);

		/* we must enable all ciphersuites */
		for (indx = 0; indx < SSL_NumImplementedCiphers; indx++) {
			if (!SSL_IS_SSL2_CIPHER(SSL_ImplementedCiphers[indx]))
				SSL_CipherPrefSetDefault (SSL_ImplementedCiphers[indx], PR_TRUE);
		}

		SSL_OptionSetDefault (SSL_ENABLE_SSL2, PR_TRUE);
		SSL_OptionSetDefault (SSL_ENABLE_SSL3, PR_TRUE);
		SSL_OptionSetDefault (SSL_ENABLE_TLS, PR_TRUE);
		SSL_OptionSetDefault (SSL_V2_COMPATIBLE_HELLO, PR_TRUE /* maybe? */);

		g_free (nss_configdir);
	}
#endif /* HAVE_NSS */

	path = g_strdup_printf ("%s/camel-cert.db", configdir);
	certdb = camel_certdb_new ();
	camel_certdb_set_filename (certdb, path);
	g_free (path);

	/* if we fail to load, who cares? it'll just be a volatile certdb */
	camel_certdb_load (certdb);

	/* set this certdb as the default db */
	camel_certdb_set_default (certdb);

	g_object_unref (certdb);

	initialised = TRUE;

	return 0;
}

/**
 * camel_shutdown:
 *
 * Since: 2.24
 **/
void
camel_shutdown (void)
{
	CamelCertDB *certdb;

	if (!initialised)
		return;

	certdb = camel_certdb_get_default ();
	if (certdb) {
		camel_certdb_save (certdb);
		camel_certdb_set_default (NULL);
	}

	/* These next calls must come last. */

#if defined (HAVE_NSS)
	if (nss_initlock != NULL) {
		PR_Lock(nss_initlock);
		if (nss_initialized)
			NSS_Shutdown ();
		PR_Unlock(nss_initlock);
	}
#endif /* HAVE_NSS */

	initialised = FALSE;
}
