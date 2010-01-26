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

#include <unistd.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include "e-data-types.h"
#include "e-data.h"
#include "e-data-view.h"

G_DEFINE_ABSTRACT_TYPE(EData, e_data, G_TYPE_OBJECT);

#define E_DATA_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), E_TYPE_DATA, EDataPrivate))

struct _EDataPrivate {
	EBackend *backend;
	ESource *source;
};

enum {
	PROP_0,
	PROP_BACKEND,
	PROP_SOURCE,
};

enum {
	WRITABLE,
	CONNECTION,
	AUTH_REQUIRED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/* Create the EData error quark */
GQuark
e_data_error_quark (void)
{
	static GQuark quark = 0;
	if (G_UNLIKELY (quark == 0))
		quark = g_quark_from_static_string ("e-data-data-error");
	return quark;
}

static void
set_property (GObject      *object,
	      guint         property_id,
	      const GValue *value,
	      GParamSpec   *pspec)
{
        EData *group;
        EDataPrivate *priv;

        group = E_DATA (object);
        priv = E_DATA_GET_PRIVATE (object);

	switch (property_id) {
		case PROP_BACKEND:
			if (priv->backend) {
				g_warning (G_STRLOC ": backend has already been set");
				return;
			}
			priv->backend = g_value_dup_object (value);
			break;

		case PROP_SOURCE:
			if (priv->source) {
				g_warning (G_STRLOC ": source has already been set");
				return;
			}
			priv->source = g_value_dup_object (value);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
			break;
	}
}

static void
get_property (GObject    *object,
	      guint       property_id,
	      GValue     *value,
	      GParamSpec *pspec)
{
	EDataPrivate *priv;

	priv = E_DATA_GET_PRIVATE (object);

	switch (property_id) {
		case PROP_BACKEND:
			g_value_set_object (value, priv->backend);
			break;
		case PROP_SOURCE:
			g_value_set_object (value, priv->source);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
			break;
	}
}

static void
e_data_dispose (GObject *object)
{
	EDataPrivate *priv;

	priv = E_DATA_GET_PRIVATE (object);

	if (priv->backend) {
		g_object_unref (priv->backend);
		priv->backend = NULL;
	}

	if (priv->source) {
		g_object_unref (priv->source);
		priv->source = NULL;
	}

	G_OBJECT_CLASS (e_data_parent_class)->dispose (object);
}

static void
e_data_class_init (EDataClass *e_data_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (e_data_class);

	object_class->dispose = e_data_dispose;
	object_class->get_property = get_property;
	object_class->set_property = set_property;

	g_object_class_install_property
		(object_class, PROP_BACKEND,
		 g_param_spec_object
			("backend",
			 "Backend",
			 "The backend that created this object",
			 E_TYPE_BACKEND,
			 G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
			 G_PARAM_STATIC_STRINGS));

	g_object_class_install_property
		(object_class, PROP_SOURCE,
		 g_param_spec_object
			("source",
			 "Source",
			 "The ESource for this object",
			 E_TYPE_SOURCE,
			 G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
			 G_PARAM_STATIC_STRINGS));

	signals[WRITABLE] =
		g_signal_new ("writable",
			      G_OBJECT_CLASS_TYPE (e_data_class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	signals[CONNECTION] =
		g_signal_new ("connection",
			      G_OBJECT_CLASS_TYPE (e_data_class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	signals[AUTH_REQUIRED] =
		g_signal_new ("auth-required",
			      G_OBJECT_CLASS_TYPE (e_data_class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

        g_type_class_add_private (e_data_class, sizeof (EDataPrivate));
}

/* Instance init */
static void
e_data_init (EData *ebook)
{
}

EBackend*
e_data_get_backend (EData *data)
{
	g_return_val_if_fail (E_IS_DATA (data), NULL);

        return (E_DATA_GET_PRIVATE (data))->backend;
}

ESource*
e_data_get_source (EData *data)
{
	g_return_val_if_fail (E_IS_DATA (data), NULL);

        return (E_DATA_GET_PRIVATE (data))->source;
}

void
e_data_notify_auth_required (EData *data)
{
	EDataClass *klass;

	g_return_if_fail (E_IS_DATA (data));

	klass = E_DATA_GET_CLASS (data);

	if (klass->notify_auth_required) {
		klass->notify_auth_required (data);
		return;
	}

	g_signal_emit (data, signals[AUTH_REQUIRED], 0);
}
