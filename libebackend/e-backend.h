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
 *   Nat Friedman <nat@ximian.com>
 *   Travis Reitter <travis.reitter@collabora.co.uk>
 */

#ifndef __E_BACKEND_H__
#define __E_BACKEND_H__

#include <glib.h>
#include <glib-object.h>
#include <libedataserver/e-list.h>
#include <libedataserver/e-source.h>

#include "e-data-view.h"
#include "e-data-types.h"
#include "e-data.h"

G_BEGIN_DECLS

#define E_TYPE_BACKEND         (e_backend_get_type ())
#define E_BACKEND(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BACKEND, EBackend))
#define E_BACKEND_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BACKEND, EBackendClass))
#define E_IS_BACKEND(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BACKEND))
#define E_IS_BACKEND_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BACKEND))
#define E_BACKEND_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((k), E_TYPE_BACKEND, EBackendClass))

typedef struct _EBackendPrivate EBackendPrivate;

struct _EBackend {
	GObject parent_object;
	EBackendPrivate *priv;
};

struct _EBackendClass {
	GObjectClass parent_class;

/* XXX: eCal equiv is start_query */
	/* FIXME: this could be genericized
	void (*start_view) (EBackend *backend,
			EDataView *view);
			*/
	void		(*stop_view)			(EBackend  *backend,
							 EDataView *view);

	void		(*remove)			(EBackend  *backend,
							 EData     *data,
							 guint32    opid);

        gchar *		(*get_static_capabilities)	(EBackend  *backend);

	void		(*set_mode)			(EBackend  *backend,
							 EDataMode  mode);

	gboolean	(*add_client)			(EBackend  *backend,
							 EData     *data);

	void		(*remove_client)		(EBackend  *backend,
							 EData     *data);

	gboolean	(*is_loaded)			(EBackend  *backend);

	void		(*get_changes)			(EBackend    *backend,
							 EData       *data,
							 guint32      opid,
							 const gchar *change_id);

	/* Notification signals */
	void (* last_client_gone) (EBackend *backend);

	/* Padding for future expansion */
	void (*_pas_reserved1) (void);
	void (*_pas_reserved2) (void);
	void (*_pas_reserved3) (void);
	void (*_pas_reserved4) (void);
};

GType		e_backend_get_type		(void);

ESource*	e_backend_get_source		(EBackend *backend);

void		e_backend_set_source		(EBackend *backend,
						 ESource  *source);

/* XXX: ecal equiv: add_query */
void		e_backend_add_view		(EBackend           *backend,
						 EDataView          *view);
/* XXX: ecal equiv: remove_query */
void		e_backend_remove_view		(EBackend           *backend,
						 EDataView          *view);

EList*		e_backend_get_views		(EBackend           *backend);

void		e_backend_notify_auth_required	(EBackend           *backend);


void		e_backend_remove			(EBackend  *backend,
							 EData     *data,
							 guint32    opid);

gchar*		e_backend_get_static_capabilities	(EBackend  *backend);

void		e_backend_set_mode			(EBackend  *backend,
							 EDataMode  mode);

gboolean	e_backend_add_client			(EBackend  *backend,
							 EData     *data);

void		e_backend_remove_client			(EBackend  *backend,
							 EData     *data);

GList*		e_backend_get_clients			(EBackend  *backend);

GMutex*		e_backend_get_clients_mutex		(EBackend  *backend);

gboolean	e_backend_is_loaded			(EBackend  *backend);

void		e_backend_get_changes			(EBackend    *backend,
							 EData       *data,
							 guint32      opid,
							 const gchar *change_id);

void		e_backend_notify_writable		(EBackend *backend, gboolean is_writable);

void		e_backend_notify_connection_status	(EBackend *backend, gboolean is_online);

void		e_backend_stop_view			(EBackend  *backend,
							 EDataView *view);

/* FIXME: these could be genericized a bit */
#if 0
/* XXX: the ECal equivalent is "start_query" */
void        e_backend_start_data_view            (EBackend           *backend,
						       EDataView          *view);
#endif

G_END_DECLS

#endif /* ! __E_BACKEND_H__ */

