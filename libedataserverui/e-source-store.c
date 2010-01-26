/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-source-store.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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
 */

/**
 * SECTION: e-source-store
 * @short_description: Store sources and clients in a tree model
 * @include: libedataserverui/e-source-store.h
 *
 * #ESourceStore is a #GtkTreeModel meant for use with widgets that render
 * sources such as #ESourceSelector and #ESourceComboBox.  It can also be
 * subclassed to open and store #EBook and #ECal objects.
 **/

#include "e-source-store.h"

#include <config.h>
#include <glib/gi18n-lib.h>

#define E_SOURCE_STORE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SOURCE_STORE, ESourceStorePrivate))

struct _ESourceStorePrivate {

	ESourceList *source_list;

	/* Source UID -> GtkTreeRowReference */
	GHashTable *index;

	/* Source UID -> GCancellable */
	GHashTable *requests;

	guint refresh_source_id;
};

enum {
	PROP_0,
	PROP_SOURCE_LIST
};

enum {
	SOURCE_ADDED,
	SOURCE_REMOVED,
	SOURCE_SELECTED,
	SOURCE_UNSELECTED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (ESourceStore, e_source_store, GTK_TYPE_TREE_STORE)

typedef struct {
	ESourceStore *source_store;
	GHashTable *remaining_uids;
	GPtrArray *deleted_uids;
	GtkTreeIter iter;
	gint position;
} RefreshData;

static RefreshData *
refresh_data_new (ESourceStore *source_store)
{
	RefreshData *data;

	data = g_slice_new0 (RefreshData);
	data->source_store = g_object_ref (source_store);
	data->remaining_uids = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) gtk_tree_row_reference_free);
	data->deleted_uids = g_ptr_array_new_with_free_func (
		(GDestroyNotify) gtk_tree_row_reference_free);

	return data;
}

static void
refresh_data_free (RefreshData *data)
{
	g_object_unref (data->source_store);
	g_hash_table_destroy (data->remaining_uids);
	g_ptr_array_free (data->deleted_uids, TRUE);
	g_slice_free (RefreshData, data);
}

static gint
source_store_compare_source_names (gconstpointer source_a,
                                   gconstpointer source_b)
{
	const gchar *source_name_a;
	const gchar *source_name_b;

	g_return_val_if_fail (E_IS_SOURCE (source_a), -1);
	g_return_val_if_fail (E_IS_SOURCE (source_b),  1);

	source_name_a = e_source_peek_name (E_SOURCE (source_a));
	source_name_b = e_source_peek_name (E_SOURCE (source_b));

	return g_utf8_collate (source_name_a, source_name_b);
}

static gboolean
source_store_refresh_collect_data (GtkTreeModel *model,
                                   GtkTreePath *path,
                                   GtkTreeIter *iter,
                                   RefreshData *data)
{
	GtkTreeRowReference *reference;
	ESourceStore *source_store;
	ESourceList *source_list;
	GObject *object;
	const gchar *uid;

	source_store = E_SOURCE_STORE (model);
	source_list = e_source_store_get_source_list (source_store);
	reference = gtk_tree_row_reference_new (model, path);

	/* The object may be an ESource or ESourceGroup. */
	gtk_tree_model_get (
		model, iter,
		E_SOURCE_STORE_COLUMN_SOURCE, &object, -1);

	if (E_IS_SOURCE (object)) {
		uid = e_source_peek_uid (E_SOURCE (object));

		if (e_source_list_peek_source_by_uid (source_list, uid))
			g_hash_table_insert (
				data->remaining_uids,
				g_strdup (uid), reference);
		else {
			guint signal_id;

			g_ptr_array_add (data->deleted_uids, reference);

			g_hash_table_remove (
				source_store->priv->index,
				(gpointer) uid);

			signal_id = signals[SOURCE_REMOVED];
			g_signal_emit (source_store, signal_id, 0, object);
		}

	} else if (E_IS_SOURCE_GROUP (object)) {
		uid = e_source_group_peek_uid (E_SOURCE_GROUP (object));

		if (e_source_list_peek_group_by_uid (source_list, uid))
			g_hash_table_insert (
				data->remaining_uids,
				g_strdup (uid), reference);
		else
			g_ptr_array_add (data->deleted_uids, reference);

	} else
		g_warn_if_reached ();

	g_object_unref (object);

	return FALSE;
}

static void
source_store_refresh_add_sources (ESource *source,
                                  RefreshData *data)
{
	GtkTreeRowReference *reference;
	GtkTreeModel *tree_model;
	GtkTreeStore *tree_store;
	GtkTreeIter iter;
	const gchar *uid;

	tree_model = GTK_TREE_MODEL (data->source_store);
	tree_store = GTK_TREE_STORE (data->source_store);

	uid = e_source_peek_uid (source);
	reference = g_hash_table_lookup (data->remaining_uids, uid);

	if (reference == NULL) {
		GtkTreePath *path;
		guint signal_id;

		gtk_tree_store_insert (
			tree_store, &iter, &data->iter, data->position);
		gtk_tree_store_set (
			tree_store, &iter,
			E_SOURCE_STORE_COLUMN_SOURCE, source, -1);

		path = gtk_tree_model_get_path (tree_model, &iter);
		reference = gtk_tree_row_reference_new (tree_model, path);
		gtk_tree_path_free (path);

		g_hash_table_insert (
			data->source_store->priv->index,
			g_strdup (uid), reference);

		signal_id = signals[SOURCE_ADDED];
		g_signal_emit (data->source_store, signal_id, 0, source);
	} else {
		GtkTreePath *path;

		path = gtk_tree_row_reference_get_path (reference);
		gtk_tree_model_get_iter (tree_model, &iter, path);
		gtk_tree_model_row_changed (tree_model, path, &iter);
		gtk_tree_path_free (path);
	}

	data->position++;
}

static void
source_store_refresh_add_groups (ESourceGroup *source_group,
                                 RefreshData *data)
{
	GtkTreeRowReference *reference;
	GtkTreeModel *tree_model;
	GtkTreeStore *tree_store;
	GSList *list;
	const gchar *uid;

	/* Skip empty source groups. */
	list = e_source_group_peek_sources (source_group);
	if (list == NULL)
		return;

	tree_model = GTK_TREE_MODEL (data->source_store);
	tree_store = GTK_TREE_STORE (data->source_store);

	/* Copy the list and sort by name. */
	list = g_slist_copy (list);
	list = g_slist_sort (list, source_store_compare_source_names);

	uid = e_source_group_peek_uid (source_group);
	reference = g_hash_table_lookup (data->remaining_uids, uid);

	if (reference == NULL) {
		gtk_tree_store_append (tree_store, &data->iter, NULL);
		gtk_tree_store_set (
			tree_store, &data->iter,
			E_SOURCE_STORE_COLUMN_SOURCE, source_group, -1);
	} else {
		GtkTreePath *path;

		path = gtk_tree_row_reference_get_path (reference);
		gtk_tree_model_get_iter (tree_model, &data->iter, path);
		gtk_tree_model_row_changed (tree_model, path, &data->iter);
		gtk_tree_path_free (path);
	}

	/* Insert new sources in this source group. */
	data->position = 0;
	g_slist_foreach (list, (GFunc) source_store_refresh_add_sources, data);

	g_slist_free (list);
}

static gboolean
source_store_refresh (ESourceStore *source_store)
{
	ESourceList *source_list;
	GtkTreeModel *tree_model;
	GtkTreeStore *tree_store;
	RefreshData *data;
	GSList *list;
	guint ii;

	tree_model = GTK_TREE_MODEL (source_store);
	tree_store = GTK_TREE_STORE (source_store);

	source_list = e_source_store_get_source_list (source_store);

	data = refresh_data_new (source_store);

	gtk_tree_model_foreach (
		tree_model, (GtkTreeModelForeachFunc)
		source_store_refresh_collect_data, data);

	/* Remove any deleted sources or source groups. */
	for (ii = 0; ii < data->deleted_uids->len; ii++) {
		GtkTreeRowReference *reference;
		GtkTreePath *path;
		GtkTreeIter iter;

		reference = g_ptr_array_index (data->deleted_uids, ii);
		path = gtk_tree_row_reference_get_path (reference);
		gtk_tree_model_get_iter (tree_model, &iter, path);
		gtk_tree_store_remove (tree_store, &iter);
		gtk_tree_path_free (path);
	}

	/* Insert new source groups. */
	list = e_source_list_peek_groups (source_list);
	g_slist_foreach (list, (GFunc) source_store_refresh_add_groups, data);

	refresh_data_free (data);

	source_store->priv->refresh_source_id = 0;

	return FALSE;
}

static void
source_store_get_client_cb (ESourceStore *source_store,
                            GAsyncResult *result,
                            ESource *source)
{
	GHashTable *hash_table;
	GtkTreeIter iter;
	GObject *client;
	const gchar *uid;
	GError *error = NULL;

	uid = e_source_peek_uid (source);
	hash_table = source_store->priv->requests;
	g_hash_table_remove (hash_table, uid);

	client = e_source_store_get_client_finish (
		source_store, result, &error);

	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED)) {
		g_error_free (error);
		goto exit;

	} else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_error_free (error);
		goto exit;

	} else if (error != NULL) {
		e_source_store_unselect_source (source_store, source);
		g_warning ("%s", error->message);
		g_error_free (error);
		goto exit;
	}

	/* The source may have been deleted while we were waiting. */
	if (!e_source_store_get_iter_from_source (source_store, source, &iter))
		goto exit;

	gtk_tree_store_set (
		GTK_TREE_STORE (source_store), &iter,
		E_SOURCE_STORE_COLUMN_CLIENT, client, -1);

exit:
	if (client != NULL)
		g_object_unref (client);

	g_object_unref (source);
}

static void
source_store_set_source_list (ESourceStore *source_store,
                              ESourceList *source_list)
{
	g_return_if_fail (E_IS_SOURCE_LIST (source_list));
	g_return_if_fail (source_store->priv->source_list == NULL);

	source_store->priv->source_list = g_object_ref (source_list);

	source_store_refresh (source_store);

	g_signal_connect_swapped (
		source_list, "changed",
		G_CALLBACK (e_source_store_queue_refresh), source_store);
}

static void
source_store_set_property (GObject *object,
                           guint property_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE_LIST:
			source_store_set_source_list (
				E_SOURCE_STORE (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_store_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE_LIST:
			g_value_set_object (
				value, e_source_store_get_source_list (
				E_SOURCE_STORE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_store_dispose (GObject *object)
{
	ESourceStorePrivate *priv;

	priv = E_SOURCE_STORE_GET_PRIVATE (object);

	if (priv->refresh_source_id > 0) {
		g_source_remove (priv->refresh_source_id);
		priv->refresh_source_id = 0;
	}

	if (priv->source_list != NULL) {
		g_object_unref (priv->source_list);
		priv->source_list = NULL;
	}

	g_hash_table_remove_all (priv->index);
	g_hash_table_remove_all (priv->requests);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_source_store_parent_class)->dispose (object);
}

static void
source_store_finalize (GObject *object)
{
	ESourceStorePrivate *priv;

	priv = E_SOURCE_STORE_GET_PRIVATE (object);

	g_hash_table_destroy (priv->index);
	g_hash_table_destroy (priv->requests);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_source_store_parent_class)->finalize (object);
}

static void
source_store_source_added (ESourceStore *source_store,
                           ESource *source)
{
	/* This is a placeholder in case we decide to do something
	 * here in the future.  Subclasses should still chain up. */
}

static void
source_store_source_removed (ESourceStore *source_store,
                             ESource *source)
{
	/* This is a placeholder in case we decide to do something
	 * here in the future.  Subclasses should still chain up. */
}

static void
source_store_source_selected (ESourceStore *source_store,
                              ESource *source)
{
	GCancellable *cancellable;
	GHashTable *hash_table;
	const gchar *uid;

	uid = e_source_peek_uid (source);
	hash_table = source_store->priv->requests;
	cancellable = g_hash_table_lookup (hash_table, uid);

	if (cancellable != NULL)
		return;

	cancellable = g_cancellable_new ();
	g_hash_table_insert (hash_table, g_strdup (uid), cancellable);

	e_source_store_get_client_async (
		source_store, source, G_PRIORITY_DEFAULT, cancellable,
		(GAsyncReadyCallback) source_store_get_client_cb,
		g_object_ref (source));
}

static void
source_store_source_unselected (ESourceStore *source_store,
                                ESource *source)
{
	GCancellable *cancellable;
	GHashTable *hash_table;
	const gchar *uid;

	uid = e_source_peek_uid (source);
	hash_table = source_store->priv->requests;
	cancellable = g_hash_table_lookup (hash_table, uid);

	if (cancellable != NULL) {
		g_cancellable_cancel (cancellable);
		g_hash_table_remove (hash_table, uid);
	}
}

typedef struct {
	ESource *source;
	GObject *client;
} GetClientData;

static void
source_store_get_client_data_free (GetClientData *data)
{
	if (data->source != NULL)
		g_object_unref (data->source);

	if (data->client != NULL)
		g_object_unref (data->client);

	g_slice_free (GetClientData, data);
}

static void
source_store_get_client_thread (GSimpleAsyncResult *simple,
                                ESourceStore *source_store,
                                GCancellable *cancellable)
{
	GetClientData *data;
	GError *error = NULL;

	data = g_simple_async_result_get_op_res_gpointer (simple);
	g_return_if_fail (data != NULL && data->source != NULL);

	data->client = e_source_store_get_client (
		source_store, data->source, cancellable, &error);

	if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}
}

static GObject *
source_store_get_client (ESourceStore *source_store,
                         ESource *source,
                         GCancellable *cancellable,
                         GError **error)
{
	/* Subclasses should override this. */

	g_set_error (
		error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		_("Operation not supported"));

	return NULL;
}

static void
source_store_get_client_async (ESourceStore *source_store,
                               ESource *source,
                               gint io_priority,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	GObject *object = G_OBJECT (source_store);
	GSimpleAsyncResult *simple;
	GetClientData *data;

	data = g_slice_new0 (GetClientData);
	data->source = g_object_ref (source);

	simple = g_simple_async_result_new (
		object, callback, user_data,
		e_source_store_get_client_async);

	g_simple_async_result_set_op_res_gpointer (
		simple, data, (GDestroyNotify)
		source_store_get_client_data_free);

	g_simple_async_result_run_in_thread (
		simple, (GSimpleAsyncThreadFunc)
		source_store_get_client_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static GObject *
source_store_get_client_finish (ESourceStore *source_store,
                                GAsyncResult *result,
                                GError **error)
{
	GSimpleAsyncResult *simple;
	GetClientData *data;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (source_store),
		e_source_store_get_client_async), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	data = g_simple_async_result_get_op_res_gpointer (simple);
	g_return_val_if_fail (data != NULL, NULL);

	if (data->client != NULL)
		return g_object_ref (data->client);

	return NULL;
}

static void
e_source_store_class_init (ESourceStoreClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (ESourceStorePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_store_set_property;
	object_class->get_property = source_store_get_property;
	object_class->dispose = source_store_dispose;
	object_class->finalize = source_store_finalize;

	class->source_added = source_store_source_added;
	class->source_removed = source_store_source_removed;
	class->source_selected = source_store_source_selected;
	class->source_unselected = source_store_source_unselected;

	class->get_client = source_store_get_client;
	class->get_client_async = source_store_get_client_async;
	class->get_client_finish = source_store_get_client_finish;

	g_object_class_install_property (
		object_class,
		PROP_SOURCE_LIST,
		g_param_spec_object (
			"source-list",
			"Source List",
			NULL,
			E_TYPE_SOURCE_LIST,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));

	signals[SOURCE_ADDED] = g_signal_new (
		"source-added",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ESourceStoreClass, source_added),
		NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE, 1,
		E_TYPE_SOURCE);

	signals[SOURCE_REMOVED] = g_signal_new (
		"source-removed",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ESourceStoreClass, source_removed),
		NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE, 1,
		E_TYPE_SOURCE);

	signals[SOURCE_SELECTED] = g_signal_new (
		"source-selected",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ESourceStoreClass, source_selected),
		NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE, 1,
		E_TYPE_SOURCE);

	signals[SOURCE_UNSELECTED] = g_signal_new (
		"source-unselected",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ESourceStoreClass, source_unselected),
		NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE, 1,
		E_TYPE_SOURCE);
}

static void
e_source_store_init (ESourceStore *source_store)
{
	GType types[E_SOURCE_STORE_NUM_COLUMNS];
	GHashTable *index;
	GHashTable *requests;
	gint column = 0;

	index = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) gtk_tree_row_reference_free);

	requests = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_object_unref);

	source_store->priv = E_SOURCE_STORE_GET_PRIVATE (source_store);

	source_store->priv->index = index;
	source_store->priv->requests = requests;

	types[column++] = G_TYPE_OBJECT;	/* COLUMN_SOURCE */
	types[column++] = G_TYPE_BOOLEAN;	/* COLUMN_SELECTED */
	types[column++] = G_TYPE_OBJECT;	/* COLUMN_CLIENT */

	g_assert (column == E_SOURCE_STORE_NUM_COLUMNS);

	gtk_tree_store_set_column_types (
		GTK_TREE_STORE (source_store),
		G_N_ELEMENTS (types), types);
}

/**
 * e_source_store_new:
 * @source_list: an #ESourceList
 *
 * Creates a new #ESourceStore instance that keeps itself synchronized
 * with @source_list.
 *
 * Returns: a new #ESourceStore instance
 **/
GtkTreeModel *
e_source_store_new (ESourceList *source_list)
{
	g_return_val_if_fail (E_IS_SOURCE_LIST (source_list), NULL);

	return g_object_new (
		E_TYPE_SOURCE_STORE,
		"source-list", source_list, NULL);
}

/**
 * e_source_store_get_source_list:
 * @source_store: an #ESourceStore
 *
 * Returns the #ESourceList with which @source_store is synchronizing
 * itself.
 *
 * Returns: the #ESourceList passed to e_source_store_new()
 **/
ESourceList *
e_source_store_get_source_list (ESourceStore *source_store)
{
	g_return_val_if_fail (E_IS_SOURCE_STORE (source_store), NULL);

	return source_store->priv->source_list;
}

/**
 * e_source_store_queue_refresh:
 * @source_store: an #ESourceStore
 *
 * Queues a request for @source_store to syncrhonize itself with its
 * internal #ESourceList.  This function gets called automatically when
 * the internal #ESourceList emits change notifications.  The actual
 * synchronization happens in an idle callback.
 **/
void
e_source_store_queue_refresh (ESourceStore *source_store)
{
	guint source_id;

	g_return_if_fail (E_IS_SOURCE_STORE (source_store));

	if (source_store->priv->refresh_source_id > 0)
		return;

	source_id = gdk_threads_add_idle (
		(GSourceFunc) source_store_refresh, source_store);

	source_store->priv->refresh_source_id = source_id;
}

/**
 * e_source_store_get_iter_from_source:
 * @source_store: an #ESourceStore
 * @source: an #ESource
 * @iter: an uninitialized #GtkTreeIter
 *
 * Sets @iter to a valid iterator pointing to @source.
 *
 * Returns: %TRUE, if @iter was set
 **/
gboolean
e_source_store_get_iter_from_source (ESourceStore *source_store,
                                     ESource *source,
                                     GtkTreeIter *iter)
{
	GtkTreeRowReference *reference;
	GHashTable *hash_table;
	const gchar *uid;

	g_return_val_if_fail (E_IS_SOURCE_STORE (source_store), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	uid = e_source_peek_uid (source);
	hash_table = source_store->priv->index;
	reference = g_hash_table_lookup (hash_table, uid);

	if (!gtk_tree_row_reference_valid (reference))
		return FALSE;

	if (iter != NULL) {
		GtkTreeModel *model;
		GtkTreePath *path;

		model = gtk_tree_row_reference_get_model (reference);
		path = gtk_tree_row_reference_get_path (reference);
		gtk_tree_model_get_iter (model, iter, path);
		gtk_tree_path_free (path);
	}

	return TRUE;
}

/**
 * e_source_store_select_source:
 * @source_store: an #ESourceStore
 * @source: an #ESource
 *
 * Marks @source as selected and attempts to open a client connection
 * for @source via e_source_store_get_client().
 *
 * <note>
 *   <para>
 *     #ESourceStore itself does not know how to open client connections.
 *     Subclasses must provide this capability.
 *   </para>
 * </note>
 **/
void
e_source_store_select_source (ESourceStore *source_store,
                              ESource *source)
{
	GtkTreeIter iter;
	gboolean selected;

	g_return_if_fail (E_IS_SOURCE_STORE (source_store));
	g_return_if_fail (E_IS_SOURCE (source));

	if (!e_source_store_get_iter_from_source (source_store, source, &iter))
		return;

	/* Avoid emitting unnecessary "row-changed" signals. */

	gtk_tree_model_get (
		GTK_TREE_MODEL (source_store), &iter,
		E_SOURCE_STORE_COLUMN_SELECTED, &selected, -1);

	if (selected)
		return;

	gtk_tree_store_set (
		GTK_TREE_STORE (source_store), &iter,
		E_SOURCE_STORE_COLUMN_SELECTED, TRUE, -1);

	g_signal_emit (source_store, signals[SOURCE_SELECTED], 0, source);
}

/**
 * e_source_store_unselect_source:
 * @source_store: an #ESourceStore
 * @source: an #ESource
 *
 * Marks @source as unselected and closes the corresponding client
 * connection, if present.
 **/
void
e_source_store_unselect_source (ESourceStore *source_store,
                                ESource *source)
{
	GtkTreeIter iter;
	gboolean selected;

	g_return_if_fail (E_IS_SOURCE_STORE (source_store));
	g_return_if_fail (E_IS_SOURCE (source));

	if (!e_source_store_get_iter_from_source (source_store, source, &iter))
		return;

	/* Avoid emitting unnecessary "row-changed" signals. */

	gtk_tree_model_get (
		GTK_TREE_MODEL (source_store), &iter,
		E_SOURCE_STORE_COLUMN_SELECTED, &selected, -1);

	if (!selected)
		return;

	gtk_tree_store_set (
		GTK_TREE_STORE (source_store), &iter,
		E_SOURCE_STORE_COLUMN_SELECTED, FALSE,
		E_SOURCE_STORE_COLUMN_CLIENT, NULL, -1);

	g_signal_emit (source_store, signals[SOURCE_UNSELECTED], 0, source);
}

/**
 * e_source_store_is_selected:
 * @source_store: an #ESourceStore
 * @source: an #ESource
 *
 * Returns %TRUE if @source is selected.
 *
 * Returns: %TRUE, if @source is selected
 **/
gboolean
e_source_store_is_selected (ESourceStore *source_store,
                            ESource *source)
{
	GtkTreeIter iter;
	gboolean selected = FALSE;

	g_return_val_if_fail (E_IS_SOURCE_STORE (source_store), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	if (!e_source_store_get_iter_from_source (source_store, source, &iter))
		return FALSE;

	gtk_tree_model_get (
		GTK_TREE_MODEL (source_store), &iter,
		E_SOURCE_STORE_COLUMN_SELECTED, &selected, -1);

	return selected;
}

/* Helper for e_source_store_get_selection() */
static gboolean
source_store_get_selection_foreach_cb (GtkTreeModel *model,
                                       GtkTreePath *path,
                                       GtkTreeIter *iter,
                                       GSList **list)
{
	ESource *source;
	gboolean selected;

	/* Groups are at depth 1, sources are at depth 2. */
	if (gtk_tree_path_get_depth (path) != 2)
		return FALSE;

	gtk_tree_model_get (
		model, iter,
		E_SOURCE_STORE_COLUMN_SOURCE, &source,
		E_SOURCE_STORE_COLUMN_SELECTED, &selected, -1);

	g_return_val_if_fail (E_IS_SOURCE (source), TRUE);

	if (selected)
		*list = g_slist_prepend (*list, source);
	else
		g_object_unref (source);

	return FALSE;
}

/**
 * e_source_store_get_selected:
 * @source_store: an #ESourceStore
 *
 * Returns a #GSList of currently selected sources.  The returned list
 * should be freed as follows:
 *
 * <informalexample>
 *   <programlisting>
 *     g_slist_foreach (list, (GFunc) g_object_unref, NULL);
 *     g_slist_free (list);
 *   </programlisting>
 * </informalexample>
 *
 * Returns: a #GSList of selected sources
 **/
GSList *
e_source_store_get_selected (ESourceStore *source_store)
{
	GSList *list = NULL;

	/* XXX Only returning GSList here because ESourceSelector does.
	 *     I would prefer GList, but too late to change it. */

	g_return_val_if_fail (E_IS_SOURCE_STORE (source_store), NULL);

	gtk_tree_model_foreach (
		GTK_TREE_MODEL (source_store), (GtkTreeModelForeachFunc)
		source_store_get_selection_foreach_cb, &list);

	return g_slist_reverse (list);
}

/**
 * e_source_store_get_client:
 * @source_store: an #ESourceStore
 * @source: an #ESource
 * @cancellable: optional #GCancellable, or %NULL to ignore
 * @error: return location for a #GError, or %NULL
 *
 * Returns a client connection for @source, creating and opening it first
 * if necessary.  If an error occurs, the function returns %NULL and sets
 * @error.
 *
 * If @cancellable is not %NULL, then the operation can be cancelled by
 * triggering the cancellable object from another thread.  If the operation
 * was cancelled, a #G_IO_ERROR_CANCELLED will be reported.
 *
 * This function may block for a long time, and should not be called from
 * a thread running a #GMainLoop.  See e_source_store_get_client_async()
 * for an asynchronous version of this call.
 *
 * <note>
 *   <para>
 *     #ESourceStore itself does not know how to open client connections
 *     and will simply report a #G_IO_ERROR_NOT_SUPPORTED and return %NULL.
 *     Subclasses must provide this capability.
 *   </para>
 * </note>
 *
 * Returns: a client connection for @source, or %NULL on error
 **/
GObject *
e_source_store_get_client (ESourceStore *source_store,
                           ESource *source,
                           GCancellable *cancellable,
                           GError **error)
{
	ESourceStoreClass *class;

	g_return_val_if_fail (E_IS_SOURCE_STORE (source_store), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	class = E_SOURCE_STORE_GET_CLASS (source_store);
	g_return_val_if_fail (class->get_client != NULL, NULL);

	return class->get_client (source_store, source, cancellable, error);
}

/**
 * e_source_store_get_client_async:
 * @source_store: an #ESourceStore
 * @source: an #ESource
 * @io_priority: the <link linkend="io-priority">I/O priority</link>
 *     of the request
 * @cancellable: optional #GCancellable, or %NULL to ignore
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: the data to pass to the callback function
 *
 * Asynchronously gets a client connection for @source, creating and
 * opening it first if necessary.
 *
 * For more details, see e_source_store_get_client() which is the
 * synchronous version of this call.
 *
 * When the operation is finished, @callback will be called.  You can
 * then call e_source_store_get_client_finish() to get the result of the
 * operation.
 **/
void
e_source_store_get_client_async (ESourceStore *source_store,
                                 ESource *source,
                                 gint io_priority,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
	ESourceStoreClass *class;

	g_return_if_fail (E_IS_SOURCE_STORE (source_store));
	g_return_if_fail (E_IS_SOURCE (source));

	class = E_SOURCE_STORE_GET_CLASS (source_store);
	g_return_if_fail (class->get_client_async != NULL);

	class->get_client_async (
		source_store, source, io_priority,
		cancellable, callback, user_data);
}

/**
 * e_source_store_get_client_finish:
 * @source_store: an #ESourceStore
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes an asynchronous client operation started with
 * e_source_store_get_client_async().
 *
 * Returns: a client connection, or %NULL on error
 **/
GObject *
e_source_store_get_client_finish (ESourceStore *source_store,
                                  GAsyncResult *result,
                                  GError **error)
{
	ESourceStoreClass *class;

	g_return_val_if_fail (E_IS_SOURCE_STORE (source_store), NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

	if (G_IS_SIMPLE_ASYNC_RESULT (result)) {
		GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (result);
		if (g_simple_async_result_propagate_error (simple, error))
			return NULL;
	}

	class = E_SOURCE_STORE_GET_CLASS (source_store);
	g_return_val_if_fail (class->get_client_finish != NULL, NULL);

	return class->get_client_finish (source_store, result, error);
}
