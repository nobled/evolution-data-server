/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2006 OpenedHand Ltd
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of version 2.1 of the GNU Lesser General Public License as
 * published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef __E_DATA_FACTORY_H__
#define __E_DATA_FACTORY_H__

#include <glib-object.h>

#include "e-backend.h"
#include "e-backend-factory.h"

G_BEGIN_DECLS

#define E_TYPE_DATA_FACTORY        (e_data_factory_get_type ())
#define E_DATA_FACTORY(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_DATA_FACTORY, EDataFactory))
#define E_DATA_FACTORY_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_DATA_FACTORY, EDataFactoryClass))
#define E_IS_DATA_FACTORY(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_DATA_FACTORY))
#define E_IS_DATA_FACTORY_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_DATA_FACTORY))
#define E_DATA_FACTORY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), E_TYPE_DATA_FACTORY, EDataFactoryClass))

typedef struct _EDataFactoryPrivate EDataFactoryPrivate;

typedef struct {
	GObject parent;
	EDataFactoryPrivate *priv;
} EDataFactory;

typedef struct {
	GObjectClass parent;

	GType		(*get_backend_type)	(void);

	/* The string format for the D-Bus unique name for the factory; the
	 * format must include a %d (for process ID) and %u (for instance ID),
	 * in that order */
	const gchar*	(*get_dbus_name_format)	(EDataFactory *factory);
	EData*		(*data_new)		(EBackend *backend,
						 ESource  *source);
	void		(*register_backends)	(EDataFactory *factory);
	EBackend*	(*lookup_backend)	(EDataFactory *factory,
						 const char   *uri);
	EBackendFactory* (*lookup_backend_factory) (EDataFactory *factory,
						    const char   *proto);
	void		(*insert_backend)	(EDataFactory *factory,
						 const char   *uri,
						 EBackend     *backend);
	void		(*insert_backend_factory) (EDataFactory    *factory,
						   const char      *proto,
						   EBackendFactory *backend_factory);
} EDataFactoryClass;

typedef enum {
	E_DATA_FACTORY_ERROR_GENERIC
} EDataFactoryError;

GQuark e_data_factory_error_quark (void);
#define E_DATA_FACTORY_ERROR e_data_factory_error_quark ()

GType e_data_factory_get_type (void);

void e_data_factory_set_backend_mode (EDataFactory *factory, gint mode);

/* FIXME: make these private again */
void e_data_factory_publish_data (EDataFactory *factory, const gchar *IN_source, DBusGMethodInvocation *context);
gint e_data_factory_main (gint argc, gchar **argv, EDataFactory *factory, GMainLoop *loop, const char *service_name, const char *object_path);

G_END_DECLS

#endif /* ! __E_DATA_FACTORY_H__ */
