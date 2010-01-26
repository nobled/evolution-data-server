/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-source-store.h
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef E_SOURCE_STORE_H
#define E_SOURCE_STORE_H

#include <gtk/gtk.h>
#include <libedataserver/e-source-list.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_STORE \
	(e_source_store_get_type ())
#define E_SOURCE_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_STORE, ESourceStore))
#define E_SOURCE_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_STORE, ESourceStoreClass))
#define E_IS_SOURCE_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_STORE))
#define E_IS_SOURCE_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_STORE))
#define E_SOURCE_STORE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_STORE, ESourceStoreClass))

G_BEGIN_DECLS

typedef struct _ESourceStore ESourceStore;
typedef struct _ESourceStoreClass ESourceStoreClass;
typedef struct _ESourceStorePrivate ESourceStorePrivate;

enum {
	E_SOURCE_STORE_COLUMN_SOURCE,		/* G_TYPE_OBJECT */
	E_SOURCE_STORE_COLUMN_SELECTED,		/* G_TYPE_BOOLEAN */
	E_SOURCE_STORE_COLUMN_CLIENT,		/* G_TYPE_OBJECT */
	E_SOURCE_STORE_NUM_COLUMNS
};

struct _ESourceStore {
	GtkTreeStore parent;
	ESourceStorePrivate *priv;
};

struct _ESourceStoreClass {
	GtkTreeStoreClass parent_class;

	/* Signals */
	void		(*source_added)		(ESourceStore *source_store,
						 ESource *source);
	void		(*source_removed)	(ESourceStore *source_store,
						 ESource *source);
	void		(*source_selected)	(ESourceStore *source_store,
						 ESource *source);
	void		(*source_unselected)	(ESourceStore *source_store,
						 ESource *source);

	/* Padding for additional signals. */
	void		(*_reserved_signal_0)	(void);
	void		(*_reserved_signal_1)	(void);
	void		(*_reserved_signal_2)	(void);
	void		(*_reserved_signal_3)	(void);

	/* Methods */
	GObject *	(*get_client)		(ESourceStore *source_store,
						 ESource *source,
						 GCancellable *cancellable,
						 GError **error);
	void		(*get_client_async)	(ESourceStore *source_store,
						 ESource *source,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
	GObject *	(*get_client_finish)	(ESourceStore *source_store,
						 GAsyncResult *result,
						 GError **error);

	/* Padding for additional methods. */
	void		(*_reserved_method_0)	(void);
	void		(*_reserved_method_1)	(void);
	void		(*_reserved_method_2)	(void);
	void		(*_reserved_method_3)	(void);
	void		(*_reserved_method_4)	(void);
	void		(*_reserved_method_5)	(void);
};

GType		e_source_store_get_type		(void);
GtkTreeModel *	e_source_store_new		(ESourceList *source_list);
ESourceList *	e_source_store_get_source_list	(ESourceStore *source_store);
void		e_source_store_queue_refresh	(ESourceStore *source_store);
gboolean	e_source_store_get_iter_from_source
						(ESourceStore *source_store,
						 ESource *source,
						 GtkTreeIter *iter);
void		e_source_store_select_source	(ESourceStore *source_store,
						 ESource *source);
void		e_source_store_unselect_source	(ESourceStore *source_store,
						 ESource *source);
gboolean	e_source_store_is_selected	(ESourceStore *source_store,
						 ESource *source);
GSList *	e_source_store_get_selected	(ESourceStore *source_store);
GObject *	e_source_store_get_client	(ESourceStore *source_store,
						 ESource *source,
						 GCancellable *cancellable,
						 GError **error);
void		e_source_store_get_client_async	(ESourceStore *source_store,
						 ESource *source,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
GObject *	e_source_store_get_client_finish(ESourceStore *source_store,
						 GAsyncResult *result,
						 GError **error);

G_END_DECLS

#endif /* E_SOURCE_STORE_H */
