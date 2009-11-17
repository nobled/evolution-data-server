/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  Camel
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
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
 *  Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib/gi18n-lib.h>

#include "camel-imap4-store.h"

/* FIXME: Add options for cache expiration */
static CamelProviderConfEntry imap4_conf_entries[] = {
	{ CAMEL_PROVIDER_CONF_SECTION_START, "mailcheck", NULL,
	  N_("Checking for new mail") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "check_all", NULL,
	  N_("C_heck for new messages in all folders"), "1" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_SECTION_START, "folders", NULL,
	  N_("Folders") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "use_lsub", NULL,
	  N_("_Show only subscribed folders"), "1" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "override_namespace", NULL,
	  N_("O_verride server-supplied folder namespace"), "0" },
	{ CAMEL_PROVIDER_CONF_ENTRY, "namespace", "override_namespace",
	  N_("Namespace:") },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_SECTION_START, "general", NULL, N_("Options") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter", NULL,
	  N_("_Apply filters to new messages in INBOX on this server"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter_junk", NULL,
	  N_("Check new messages for _Junk contents"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter_junk_inbox", "filter_junk",
	  N_("Only check for Junk messa_ges in the INBOX folder"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "sync_offline", NULL,
	  N_("Automatically synchroni_ze remote mail locally"), "0" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_END }
};

static CamelProvider imap4_provider = {
	"imap4",
	N_("IMAP4rev1"),

	N_("For reading and storing mail on IMAPv4rev1 servers."),

	"mail",

	CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_IS_SOURCE |
	CAMEL_PROVIDER_IS_STORAGE | CAMEL_PROVIDER_SUPPORTS_SSL,

	CAMEL_URL_NEED_USER | CAMEL_URL_NEED_HOST | CAMEL_URL_ALLOW_AUTH | CAMEL_URL_FRAGMENT_IS_PATH,

	imap4_conf_entries,

	/* ... */
};

CamelServiceAuthType camel_imap4_password_authtype = {
	N_("Password"),

	N_("This option will connect to the IMAPv4rev1 server using a "
	   "plaintext password."),

	"",
	TRUE
};

static void
add_hash (guint *hash, gchar *s)
{
	if (s)
		*hash ^= g_str_hash(s);
}

static guint
imap4_url_hash (gconstpointer key)
{
	const CamelURL *u = (CamelURL *)key;
	guint hash = 0;

	add_hash (&hash, u->user);
	add_hash (&hash, u->host);
	hash ^= u->port;

	return hash;
}

static gint
check_equal (gchar *s1, gchar *s2)
{
	if (s1 == NULL) {
		if (s2 == NULL)
			return TRUE;
		else
			return FALSE;
	}

	if (s2 == NULL)
		return FALSE;

	return strcmp (s1, s2) == 0;
}

static gint
imap4_url_equal (gconstpointer a, gconstpointer b)
{
	const CamelURL *u1 = a, *u2 = b;

	return check_equal (u1->protocol, u2->protocol)
		&& check_equal (u1->user, u2->user)
		&& check_equal (u1->host, u2->host)
		&& u1->port == u2->port;
}

void
camel_provider_module_init (void)
{
	imap4_provider.object_types[CAMEL_PROVIDER_STORE] = camel_imap4_store_get_type ();
	imap4_provider.url_hash = imap4_url_hash;
	imap4_provider.url_equal = imap4_url_equal;
	imap4_provider.authtypes = camel_sasl_authtype_list (FALSE);
	imap4_provider.authtypes = g_list_prepend (imap4_provider.authtypes, &camel_imap4_password_authtype);
	imap4_provider.translation_domain = GETTEXT_PACKAGE;

	camel_provider_register (&imap4_provider);
}
