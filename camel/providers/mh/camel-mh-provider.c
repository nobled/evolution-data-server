/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-mbox-provider.c: mbox provider registration code */

/* 
 * Authors :
 *   Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright (C) 2000 HelixCode (www.helixcode.com).
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
#include "camel-mh-store.h"
#include "camel-provider.h"
#include "camel-session.h"
#include "camel-url.h"

static CamelProvider mh_provider = {
	"mh",
	"UNIX mh-format mail files",

	"For reading mail delivered by the local system, and for " "storing mail on local disk.",

	"mail",

	0,

	{0, 0},

	NULL
};

void camel_provider_module_init(CamelSession * session)
{
	mh_provider.object_types[CAMEL_PROVIDER_STORE] = camel_mh_store_get_type();

	mh_provider.service_cache = g_hash_table_new(camel_url_hash, camel_url_equal);

	camel_session_register_provider(session, &mh_provider);
}
