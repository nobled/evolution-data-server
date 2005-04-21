/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2002-2004 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pthread.h>
#include <string.h>

#include "e-cal-backend-exchange-factory.h"
#include "e-cal-backend-exchange-calendar.h"
#include "e-cal-backend-exchange-tasks.h"

static void
e_cal_backend_exchange_factory_instance_init (ECalBackendExchangeFactory *factory)
{
}

static const char *
_get_protocol (ECalBackendFactory *factory)
{
	return "exchange";
}

static ECalBackend*
_todos_new_backend (ECalBackendFactory *factory, ESource *source)
{
	return g_object_new (E_TYPE_CAL_BACKEND_EXCHANGE_TASKS, 
			     "source", source, 
			     "kind", ICAL_VTODO_COMPONENT, NULL);
}

static icalcomponent_kind 
_todos_get_kind (ECalBackendFactory *factory) 
{ 
	return ICAL_VTODO_COMPONENT; 
}

static ECalBackend*
_events_new_backend (ECalBackendFactory *factory, ESource *source)
{
	return g_object_new (E_TYPE_CAL_BACKEND_EXCHANGE_CALENDAR, 
			     "source", source, 
			     "kind", ICAL_VEVENT_COMPONENT, NULL);
}

static icalcomponent_kind 
_events_get_kind (ECalBackendFactory *factory) 
{ 
	return ICAL_VEVENT_COMPONENT; 
}

static void
todos_backend_exchange_factory_class_init (ECalBackendExchangeFactoryClass *klass)
{
	E_CAL_BACKEND_FACTORY_CLASS (klass)->get_protocol = _get_protocol;
	E_CAL_BACKEND_FACTORY_CLASS (klass)->get_kind     = _todos_get_kind;
	E_CAL_BACKEND_FACTORY_CLASS (klass)->new_backend = _todos_new_backend;
}

static void
events_backend_exchange_factory_class_init (ECalBackendExchangeFactoryClass *klass)
{
	E_CAL_BACKEND_FACTORY_CLASS (klass)->get_protocol = _get_protocol;
	E_CAL_BACKEND_FACTORY_CLASS (klass)->get_kind     = _events_get_kind;
	E_CAL_BACKEND_FACTORY_CLASS (klass)->new_backend = _events_new_backend;
}

GType
events_backend_exchange_factory_get_type (void)
{
	static GType type = 0;

	if (!type) {
		GTypeInfo info = {
			sizeof (ECalBackendExchangeFactoryClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  events_backend_exchange_factory_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (ECalBackend),
			0,    /* n_preallocs */
			(GInstanceInitFunc) e_cal_backend_exchange_factory_instance_init
		};

		type = g_type_register_static (E_TYPE_CAL_BACKEND_FACTORY,
					       "ECalBackendExchangeEventsFactory",
					       &info, 0);
	}
	return type;
}

GType
todos_backend_exchange_factory_get_type (void)
{
	static GType type = 0;

	if (!type) {
		GTypeInfo info = {
			sizeof (ECalBackendExchangeFactoryClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  todos_backend_exchange_factory_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (ECalBackend),
			0,    /* n_preallocs */
			(GInstanceInitFunc) e_cal_backend_exchange_factory_instance_init
		};

		type = g_type_register_static (E_TYPE_CAL_BACKEND_FACTORY,
					       "ECalBackendExchangeTodosFactory",
					       &info, 0);
	}
	return type;
}

static GType exchange_types[2];

void
eds_module_initialize (GTypeModule *module)
{
	exchange_types[0] = E_TYPE_CAL_BACKEND_EXCHANGE_CALENDAR;
	exchange_types[1] = E_TYPE_CAL_BACKEND_EXCHANGE_TASKS;
}

void
eds_module_shutdown (void)
{

}

void
eds_module_list_types (const GType **types, int *num_types)
{
	*types = exchange_types;
	*num_types = 2;
}
