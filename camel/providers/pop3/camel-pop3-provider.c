/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-pop3-provider.c: pop3 provider registration code */

/* 
 * Authors :
 *   Dan Winship <danw@helixcode.com>
 *
 * Copyright (C) 2000 Helix Code, Inc. (www.helixcode.com)
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include "config.h"
#include "camel-pop3-store.h"
#include "camel-provider.h"
#include "camel-session.h"
#include "camel-url.h"

static CamelProvider pop3_provider = {
	"pop",
	"POP",

	"For connecting to POP servers. The POP protocol can also be used "
	"to retrieve mail from certain web mail providers and proprietary "
	"email systems.",

	"mail",

	CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_IS_SOURCE,

	{ 0, 0 },

	NULL
};

void
camel_provider_module_init (CamelSession *session)
{
	pop3_provider.object_types[CAMEL_PROVIDER_STORE] =
		camel_pop3_store_get_type();

	pop3_provider.service_cache = g_hash_table_new (camel_url_hash, camel_url_equal);
	
	camel_session_register_provider (session, &pop3_provider);
}
