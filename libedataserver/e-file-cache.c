/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-file-cache.c
 *
 * Copyright (C) 2003 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authors: Rodrigo Moya <rodrigo@ximian.com>
 */

#include <config.h>
#include <string.h>
#include <unistd.h>
#include "e-file-cache.h"
#include "e-util.h"
#include "e-xml-hash-utils.h"

struct _EFileCachePrivate {
	char *filename;
	EXmlHash *xml_hash;
	gboolean dirty;
	gboolean frozen;
};

/* Property IDs */
enum {
	PROP_0,
	PROP_FILENAME
};

static GObjectClass *parent_class = NULL;

static void
e_file_cache_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	EFileCache *cache;
	EFileCachePrivate *priv;
	char *dirname;
	int result;

	cache = E_FILE_CACHE (object);
	priv = cache->priv;

	switch (property_id) {
	case PROP_FILENAME :
		/* make sure the directory for the cache exists */
		priv->filename = g_strdup ( g_value_get_string (value));
		dirname = g_path_get_dirname (priv->filename);
		result = e_util_mkdir_hier (dirname, 0700);
		g_free (dirname);
		if (result != 0)
			break;

		if (priv->xml_hash)
			e_xmlhash_destroy (priv->xml_hash);
		priv->xml_hash = e_xmlhash_new (g_value_get_string (value));

		/* if opening the cache file fails, remove it and try again */
		if (!priv->xml_hash) {
			unlink (g_value_get_string (value));
			priv->xml_hash = e_xmlhash_new (g_value_get_string (value));
			if (priv->xml_hash) {
				g_message (G_STRLOC ": could not open not re-create cache file %s",
					   g_value_get_string (value));
			}
		}
		break;
	default :
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
e_file_cache_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
	EFileCache *cache;
	EFileCachePrivate *priv;

	cache = E_FILE_CACHE (object);
	priv = cache->priv;

	switch (property_id) {
	case PROP_FILENAME :
		g_value_set_string (value, priv->filename);
		break;
	default :
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
e_file_cache_finalize (GObject *object)
{
	EFileCache *cache;
	EFileCachePrivate *priv;

	cache = E_FILE_CACHE (object);
	priv = cache->priv;

	if (priv) {
		if (priv->filename) {
			g_free (priv->filename);
			priv->filename = NULL;
		}

		if (priv->xml_hash) {
			e_xmlhash_destroy (priv->xml_hash);
			priv->xml_hash = NULL;
		}

		g_free (priv);
		cache->priv = NULL;
	}

	parent_class->finalize (object);
}

static void
e_file_cache_class_init (EFileCacheClass *klass)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (klass);

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = e_file_cache_finalize;
	object_class->set_property = e_file_cache_set_property;
	object_class->get_property = e_file_cache_get_property;

	g_object_class_install_property (object_class, PROP_FILENAME,
					 g_param_spec_string ("filename", NULL, NULL, "",
							      G_PARAM_READABLE | G_PARAM_WRITABLE
							      | G_PARAM_CONSTRUCT_ONLY));
}

static void
e_file_cache_init (EFileCache *cache)
{
	EFileCachePrivate *priv;

	priv = g_new0 (EFileCachePrivate, 1);
	priv->dirty = FALSE;
	priv->frozen = FALSE;
	cache->priv = priv;
}

/**
 * e_file_cache_get_type:
 * @void:
 *
 * Registers the #EFileCache class if necessary, and returns the type ID
 * associated to it.
 *
 * Return value: The type ID of the #EFileCache class.
 **/
GType
e_file_cache_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static GTypeInfo info = {
                        sizeof (EFileCacheClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_file_cache_class_init,
                        NULL, NULL,
                        sizeof (EFileCache),
                        0,
                        (GInstanceInitFunc) e_file_cache_init,
                };
		type = g_type_register_static (G_TYPE_OBJECT, "EFileCache", &info, 0);
	}

	return type;
}

/**
 * e_file_cache_new
 * @filename: filename where the cache is kept.
 *
 * Creates a new #EFileCache object, which implements a cache of
 * objects, very useful for remote backends.
 *
 * Return value: The newly created object.
 */
EFileCache *
e_file_cache_new (const char *filename)
{
	EFileCache *cache;

	cache = g_object_new (E_TYPE_FILE_CACHE, "filename", filename, NULL);

	return cache;
}

/**
 * e_file_cache_remove:
 * @cache: A #EFileCache object.
 *
 * Remove the cache from disk.
 *
 * Returns: TRUE if successful, FALSE otherwise.
 */
gboolean
e_file_cache_remove (EFileCache *cache)
{
	EFileCachePrivate *priv;

	g_return_val_if_fail (E_IS_FILE_CACHE (cache), FALSE);

	priv = cache->priv;

	if (priv->filename) {
		char *dirname, *full_path;
		const char *fname;
		GDir *dir;
		gboolean success;

		/* remove all files in the directory */
		dirname = g_path_get_dirname (priv->filename);
		dir = g_dir_open (dirname, 0, NULL);
		if (dir) {
			while ((fname = g_dir_read_name (dir))) {
				full_path = g_build_filename (dirname, fname, NULL);
				if (unlink (full_path) != 0) {
					g_free (full_path);
					g_free (dirname);
					g_dir_close (dir);

					return FALSE;
				}

				g_free (full_path);
			}

			g_dir_close (dir);
		}

		/* remove the directory itself */
		success = rmdir (dirname) == 0;

		/* free all memory */
		g_free (dirname);
		g_free (priv->filename);
		priv->filename = NULL;

		e_xmlhash_destroy (priv->xml_hash);
		priv->xml_hash = NULL;

		return success;
	}

	return TRUE;
}

static void
add_key_to_list (const char *key, const char *value, gpointer user_data)
{
	GList **keys = user_data;

	*keys = g_list_append (*keys, (char *) key);
}

/**
 * e_file_cache_clean:
 * @cache: A #EFileCache object.
 *
 * Clean up the cache's contents.
 *
 * Returns: TRUE if successful, FALSE otherwise.
 */
gboolean
e_file_cache_clean (EFileCache *cache)
{
	EFileCachePrivate *priv;
	GList *keys = NULL;

	g_return_val_if_fail (E_IS_FILE_CACHE (cache), FALSE);

	priv = cache->priv;

	e_xmlhash_foreach_key (priv->xml_hash, (EXmlHashFunc) add_key_to_list, &keys);
	while (keys != NULL) {
		e_file_cache_remove_object (cache, (const char *) keys->data);
		keys = g_list_remove (keys, keys->data);
	}

	return TRUE;
}

typedef struct {
	const char *key;
	gboolean found;
	const char *found_value;
} CacheFindData;

static void
find_object_in_hash (gpointer key, gpointer value, gpointer user_data)
{
	CacheFindData *find_data = user_data;

	if (find_data->found)
		return;

	if (!strcmp (find_data->key, (const char *) key)) {
		find_data->found = TRUE;
		find_data->found_value = (const char *) value;
	}
}

/**
 * e_file_cache_get_object:
 */
const char *
e_file_cache_get_object (EFileCache *cache, const char *key)
{
	CacheFindData find_data;
	EFileCachePrivate *priv;

	g_return_val_if_fail (E_IS_FILE_CACHE (cache), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	priv = cache->priv;

	find_data.key = key;
	find_data.found = FALSE;
	find_data.found_value = NULL;

	e_xmlhash_foreach_key (priv->xml_hash, (EXmlHashFunc) find_object_in_hash, &find_data);

	return find_data.found_value;
}

static void
add_object_to_list (const char *key, const char *value, gpointer user_data)
{
	GSList **list = user_data;

	*list = g_slist_prepend (*list, (char *) value);
}

/**
 * e_file_cache_get_objects:
 */
GSList *
e_file_cache_get_objects (EFileCache *cache)
{
	EFileCachePrivate *priv;
	GSList *list = NULL;

	g_return_val_if_fail (E_IS_FILE_CACHE (cache), NULL);

	priv = cache->priv;

	e_xmlhash_foreach_key (priv->xml_hash, (EXmlHashFunc) add_object_to_list, &list);

	return list;
}

/**
 * e_file_cache_get_keys:
 */
GSList *
e_file_cache_get_keys (EFileCache *cache)
{
	EFileCachePrivate *priv;
	GSList *list = NULL;

	g_return_val_if_fail (E_IS_FILE_CACHE (cache), NULL);

	priv = cache->priv;

	e_xmlhash_foreach_key (priv->xml_hash, (EXmlHashFunc) add_key_to_list, &list);

	return list;
}

/**
 * e_file_cache_add_object:
 */
gboolean
e_file_cache_add_object (EFileCache *cache, const char *key, const char *value)
{
	EFileCachePrivate *priv;

	g_return_val_if_fail (E_IS_FILE_CACHE (cache), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	priv = cache->priv;

	if (e_file_cache_get_object (cache, key))
		return FALSE;

	e_xmlhash_add (priv->xml_hash, key, value);
	if (priv->frozen)
		priv->dirty = TRUE;
	else {
		e_xmlhash_write (priv->xml_hash);
		priv->dirty = FALSE;
	}

	return TRUE;
}

/**
 * e_file_cache_replace_object:
 */
gboolean
e_file_cache_replace_object (EFileCache *cache, const char *key, const char *new_value)
{
	EFileCachePrivate *priv;

	g_return_val_if_fail (E_IS_FILE_CACHE (cache), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	priv = cache->priv;

	if (!e_file_cache_get_object (cache, key))
		return FALSE;

	if (!e_file_cache_remove_object (cache, key))
		return FALSE;

	return e_file_cache_add_object (cache, key, new_value);
}

/**
 * e_file_cache_remove_object:
 */
gboolean
e_file_cache_remove_object (EFileCache *cache, const char *key)
{
	EFileCachePrivate *priv;

	g_return_val_if_fail (E_IS_FILE_CACHE (cache), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	priv = cache->priv;

	if (!e_file_cache_get_object (cache, key))
		return FALSE;

	e_xmlhash_remove (priv->xml_hash, key);
	if (priv->frozen)
		priv->dirty = TRUE;
	else {
		e_xmlhash_write (priv->xml_hash);
		priv->dirty = FALSE;
	}

	return TRUE;
}

/**
 * e_file_cache_freeze_changes:
 * @cache: An #EFileCache object.
 *
 * Disables temporarily all writes to disk for the given cache object.
 */
void
e_file_cache_freeze_changes (EFileCache *cache)
{
	EFileCachePrivate *priv;

	g_return_if_fail (E_IS_FILE_CACHE (cache));

	priv = cache->priv;

	priv->frozen = TRUE;
}

/**
 * e_file_cache_thaw_changes:
 * @cache: An #EFileCache object.
 *
 * Enables again writes to disk on every change.
 */
void
e_file_cache_thaw_changes (EFileCache *cache)
{
	EFileCachePrivate *priv;

	g_return_if_fail (E_IS_FILE_CACHE (cache));

	priv = cache->priv;

	priv->frozen = FALSE;
	if (priv->dirty) {
		e_xmlhash_write (priv->xml_hash);
		priv->dirty = FALSE;
	}
}

/**
 * e_file_cache_get_filename:
 * @cache: A %EFileCache object.
 *
 * Gets the name of the file where the cache is being stored.
 *
 * Return value: The name of the cache.
 */
const char *
e_file_cache_get_filename (EFileCache *cache)
{
	g_return_val_if_fail (E_IS_FILE_CACHE (cache), NULL);
	return (const char *) cache->priv->filename;
}

