/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Travis Reitter (travis.reitter@collabora.co.uk)
 */

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