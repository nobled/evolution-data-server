/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
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

#ifndef _E_BACKEND_FACTORY_H_
#define _E_BACKEND_FACTORY_H_

#include <glib-object.h>
#include "e-backend.h"

G_BEGIN_DECLS

#define E_TYPE_BACKEND_FACTORY        (e_backend_factory_get_type ())
#define E_BACKEND_FACTORY(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BACKEND_FACTORY, EBackendFactory))
#define E_BACKEND_FACTORY_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BACKEND_FACTORY, EBackendFactoryClass))
#define E_IS_BACKEND_FACTORY(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BACKEND_FACTORY))
#define E_IS_BACKEND_FACTORY_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BACKEND_FACTORY))
#define E_BACKEND_FACTORY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), E_TYPE_BACKEND_FACTORY, EBackendFactoryClass))

typedef struct _EBackendFactoryPrivate EBackendFactoryPrivate;

typedef struct {
	GObject            parent_object;
} EBackendFactory;

typedef struct {
	GObjectClass parent_class;

	const gchar*	(*get_protocol)	(EBackendFactory *factory);
	EBackend*	(*new_backend)	(EBackendFactory *factory);
} EBackendFactoryClass;

GType		e_backend_factory_get_type	(void);

const gchar*	e_backend_factory_get_protocol	(EBackendFactory *factory);
EBackend*	e_backend_factory_new_backend	(EBackendFactory *factory);

G_END_DECLS

#endif /* _E_BACKEND_FACTORY_H_ */
