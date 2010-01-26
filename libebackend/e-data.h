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
 * Authors:
 *   Ross Burton <ross@linux.intel.com>
 *   Travis Reitter <travis.reitter@collabora.co.uk>
 */

#ifndef __E_DATA_H__
#define __E_DATA_H__

#include <glib-object.h>
#include <libedataserver/e-source.h>
#include "e-backend.h"

#include "e-data-types.h"

G_BEGIN_DECLS

#define E_TYPE_DATA        (e_data_get_type ())
#define E_DATA(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_DATA, EData))
#define E_DATA_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST ((k), E_TYPE_DATA, EDataClass))
#define E_IS_DATA(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_DATA))
#define E_IS_DATA_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_DATA))
#define E_DATA_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), E_TYPE_DATA, EDataClass))

typedef struct _EDataPrivate EDataPrivate;

struct _EData {
	GObject parent;
};

struct _EDataClass {
	GObjectClass parent;

	/* mandatory virtual functions */
	EBackend*	(* get_backend)			(EData *data);
	ESource*	(* get_source)			(EData *data);

	/* optional virtual functions */
	void		(* notify_auth_required)	(EData *data);
};

GQuark e_data_error_quark (void);
#define E_DATA_ERROR e_data_error_quark ()

GType           e_data_get_type                (void);

EBackend	*e_data_get_backend            (EData *data);
ESource		*e_data_get_source             (EData *data);
void             e_data_notify_auth_required   (EData *data);

G_END_DECLS

#endif /* ! __E_DATA_H__ */
