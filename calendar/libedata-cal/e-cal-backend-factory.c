/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright 2004, Novell, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pthread.h>
#include <string.h>

#include "e-cal-backend-factory.h"

static void
e_cal_backend_factory_instance_init (ECalBackendFactory *factory)
{
}

static void
e_cal_backend_factory_class_init (ECalBackendFactoryClass *klass)
{
}

GType
e_cal_backend_factory_get_type (void)
{
	static GType type = 0;

	if (! type) {
		GTypeInfo info = {
			sizeof (ECalBackendFactoryClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  e_cal_backend_factory_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (ECalBackend),
			0,    /* n_preallocs */
			(GInstanceInitFunc) e_cal_backend_factory_instance_init
		};

		type = g_type_register_static (G_TYPE_OBJECT, "ECalBackendFactory", &info, 0);
	}

	return type;
}

icalcomponent_kind
e_cal_backend_factory_get_kind (ECalBackendFactory *factory)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_FACTORY (factory), 0/*XXX*/);

	return E_CAL_BACKEND_FACTORY_GET_CLASS (factory)->get_kind (factory);
}

const char*
e_cal_backend_factory_get_protocol (ECalBackendFactory *factory)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_FACTORY (factory), NULL);

	return E_CAL_BACKEND_FACTORY_GET_CLASS (factory)->get_protocol (factory);
}

ECalBackend*
e_cal_backend_factory_new_backend (ECalBackendFactory *factory, ESource *source)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_FACTORY (factory), NULL);

	return E_CAL_BACKEND_FACTORY_GET_CLASS (factory)->new_backend (factory, source);
}
