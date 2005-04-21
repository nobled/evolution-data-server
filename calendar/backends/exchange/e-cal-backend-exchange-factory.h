/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* e-cal-backend-exchange-factory.h
 *
 * Copyright (C) 2004  Novell, Inc.
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
 *
 */

#ifndef _E_CAL_BACKEND_EXCHANGE_FACTORY_H_
#define _E_CAL_BACKEND_EXCHANGE_FACTORY_H_

#include <glib-object.h>
#include <libedata-cal/e-cal-backend-factory.h>

G_BEGIN_DECLS

#define E_TYPE_CAL_BACKEND_EXCHANGE_FACTORY        (e_cal_backend_exchange_factory_get_type ())
#define E_CAL_BACKEND_EXCHANGE_FACTORY(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_CAL_BACKEND_EXCHANGE_FACTORY, ECalBackendExchangeFactory))
#define E_CAL_BACKEND_EXCHANGE_FACTORY_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_CAL_BACKEND_EXCHANGE_FACTORY, ECalBackendExchangeFactoryClass))
#define E_IS_CAL_BACKEND_EXCHANGE_FACTORY(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_CAL_BACKEND_EXCHANGE_FACTORY))
#define E_IS_CAL_BACKEND_EXCHANGE_FACTORY_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_CAL_BACKEND_EXCHANGE_FACTORY))
#define E_CAL_BACKEND_EXCHANGE_FACTORY_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_CAL_BACKEND_EXCHANGE_FACTORY, ECalBackendExchangeFactoryClass))

typedef struct {
	ECalBackendFactory            parent_object;
} ECalBackendExchangeFactory;

typedef struct {
	ECalBackendFactoryClass parent_class;
} ECalBackendExchangeFactoryClass;

GType	events_backend_exchange_factory_get_type (void);
GType	todos_backend_exchange_factory_get_type (void);

G_END_DECLS

#endif /* _E_CAL_BACKEND_EXCHANGE_FACTORY_H_ */
