/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * Authors:
 *   Chris Toshok <toshok@ximian.com>
 *   Travis Reitter <travis.reitter@collabora.co.uk>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "e-backend-factory.h"

G_DEFINE_ABSTRACT_TYPE (EBackendFactory, e_backend_factory, G_TYPE_OBJECT);

static void
e_backend_factory_init (EBackendFactory *factory)
{
}

static void
e_backend_factory_class_init (EBackendFactoryClass *klass)
{
}

/**
 * e_backend_factory_get_protocol:
 * @factory: an #EBackendFactory
 *
 * Gets the protocol that @factory creates backends for.
 *
 * Return value: A string representing a protocol.
 **/
const gchar *
e_backend_factory_get_protocol (EBackendFactory *factory)
{
	g_return_val_if_fail (E_IS_BACKEND_FACTORY (factory), NULL);

	return E_BACKEND_FACTORY_GET_CLASS (factory)->get_protocol (factory);
}

/**
 * e_backend_factory_new_backend:
 * @factory: an #EBackendFactory
 *
 * Creates a new #EBackend with @factory's protocol.
 *
 * Return value: A new #EBackend.
 **/
EBackend*
e_backend_factory_new_backend (EBackendFactory *factory)
{
	g_return_val_if_fail (E_IS_BACKEND_FACTORY (factory), NULL);

	return E_BACKEND_FACTORY_GET_CLASS (factory)->new_backend (factory);
}
