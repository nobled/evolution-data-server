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
 *
 * Author: Nat Friedman <nat@ximian.com>
 * Author: Ross Burton <ross@linux.intel.com>
 */

#ifndef __E_DATA_VIEW_H__
#define __E_DATA_VIEW_H__

#include <glib.h>
#include <glib-object.h>

#include "e-data-types.h"
#include "e-backend.h"
#include "e-backend-sexp.h"

G_BEGIN_DECLS

#define E_TYPE_DATA_VIEW        (e_data_view_get_type ())
#define E_DATA_VIEW(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_DATA_VIEW, EDataView))
#define E_DATA_VIEW_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_DATA_VIEW, EDataViewClass))
#define E_IS_DATA_VIEW(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_DATA_VIEW))
#define E_IS_DATA_VIEW_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_DATA_VIEW))
#define E_DATA_VIEW_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((k), E_TYPE_DATA_VIEW, EDataViewClass))

typedef struct _EDataViewPrivate EDataViewPrivate;

struct _EDataView {
	GObject parent;
	EDataViewPrivate *priv;
};

struct _EDataViewClass {
	GObjectClass parent;

        void	(*stop_if_running)	(EDataView *view);
};

GType		e_data_view_get_type		(void);

EBackendSExp*	e_data_view_get_sexp		(EDataView *view);
EBackend*	e_data_view_get_backend		(EDataView *view);
const gchar*	e_data_view_get_dbus_path	(EDataView *view);


G_END_DECLS

#endif /* ! __E_DATA_VIEW_H__ */
