/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Copyright (C) 2001-2003 Ximian, Inc.
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
#include "config.h"
#endif

#include "e-xml-hash-utils.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libxml/xmlmemory.h>
#include <libxml/entities.h>

GHashTable *
e_xml_to_hash (xmlDoc *doc, EXmlHashType type)
{
	xmlNode *root, *node;
	xmlChar *value;
	GHashTable *hash;
	char *key;

	hash = g_hash_table_new (g_str_hash, g_str_equal);

	root = xmlDocGetRootElement (doc);
	for (node = root->xmlChildrenNode; node; node = node->next) {
		if (node->name == NULL || node->type != XML_ELEMENT_NODE)
			continue;

		if (type == E_XML_HASH_TYPE_OBJECT_UID &&
		    !strcmp (node->name, "object"))
			key = xmlGetProp (node, "uid");
		else
			key = xmlStrdup (node->name);

		if (!key) {
			g_warning ("Key not found!!");
			continue;
		}

		value = xmlNodeListGetString (doc, node->xmlChildrenNode, 1);
		if (!value) {
			xmlFree (key);
			g_warning ("Found a key with no value!!");
			continue;
		}

		g_hash_table_insert (hash, g_strdup (key), g_strdup (value));
		xmlFree (key);
		xmlFree (value);
	}

	return hash;
}


struct save_data {
	EXmlHashType type;
	xmlDoc *doc;
	xmlNode *root;
};

static void
foreach_save_func (gpointer key, gpointer value, gpointer user_data)
{
	struct save_data *sd = user_data;
	xmlNodePtr new_node;
	xmlChar *enc;

	if (sd->type == E_XML_HASH_TYPE_OBJECT_UID) {
		new_node = xmlNewNode (NULL, "object");
		xmlNewProp (new_node, "uid", (const char *) key);
	} else
		new_node = xmlNewNode (NULL, (const char *) key);

	enc = xmlEncodeEntitiesReentrant (sd->doc, value);
	xmlNodeSetContent (new_node, enc);
	xmlFree (enc);

	xmlAddChild (sd->root, new_node);
}

xmlDoc *
e_xml_from_hash (GHashTable *hash, EXmlHashType type, const char *root_name)
{
	xmlDoc *doc;
	struct save_data sd;

	doc = xmlNewDoc ("1.0");
	doc->encoding = g_strdup ("UTF-8");
	sd.type = type;
	sd.doc = doc;
	sd.root = xmlNewDocNode (doc, NULL, root_name, NULL);
	xmlDocSetRootElement (doc, sd.root);

	g_hash_table_foreach (hash, foreach_save_func, &sd);
	return doc;
}

static void
free_values (gpointer key, gpointer value, gpointer data)
{
	g_free (key);
	g_free (value);
}

void
e_xml_destroy_hash (GHashTable *hash)
{
	g_hash_table_foreach (hash, free_values, NULL);
	g_hash_table_destroy (hash);
}



struct EXmlHash {
	char *filename;
	GHashTable *objects;
};

EXmlHash *
e_xmlhash_new (const char *filename)
{
	EXmlHash *hash;
	xmlDoc *doc = NULL;

	g_return_val_if_fail (filename != NULL, NULL);

	hash = g_new0 (EXmlHash, 1);
	hash->filename = g_strdup (filename);

	if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
		doc = xmlParseFile (filename);
		if (!doc) {
			e_xmlhash_destroy (hash);
			
			return NULL;
		}
		hash->objects = e_xml_to_hash (doc, E_XML_HASH_TYPE_OBJECT_UID);
		xmlFreeDoc (doc);
	} else {
		hash->objects = g_hash_table_new (g_str_hash, g_str_equal);
	}
	
	return hash;
}

void
e_xmlhash_add (EXmlHash *hash, const char *key, const char *data)
{
	g_return_if_fail (hash != NULL);
	g_return_if_fail (key != NULL);
	g_return_if_fail (data != NULL);

	e_xmlhash_remove (hash, key);
	g_hash_table_insert (hash->objects, g_strdup (key), g_strdup (data));
}

void
e_xmlhash_remove (EXmlHash *hash, const char *key)
{
	gpointer orig_key;
	gpointer orig_value;

	g_return_if_fail (hash != NULL);
	g_return_if_fail (key != NULL);

	if (g_hash_table_lookup_extended (hash->objects, key, &orig_key, &orig_value)) {
		g_hash_table_remove (hash->objects, key);
		g_free (orig_key);
		g_free (orig_value);
	}
}

EXmlHashStatus
e_xmlhash_compare (EXmlHash *hash, const char *key, const char *compare_data)
{
	char *data;
	int rc;

	g_return_val_if_fail (hash != NULL, E_XMLHASH_STATUS_NOT_FOUND);
	g_return_val_if_fail (key != NULL, E_XMLHASH_STATUS_NOT_FOUND);
	g_return_val_if_fail (compare_data != NULL, E_XMLHASH_STATUS_NOT_FOUND);

	data = g_hash_table_lookup (hash->objects, key);
	if (!data)
		return E_XMLHASH_STATUS_NOT_FOUND;

	rc = strcmp (data, compare_data);
	if (rc == 0)
		return E_XMLHASH_STATUS_SAME;

	return E_XMLHASH_STATUS_DIFFERENT;
}

typedef struct {
	EXmlHashFunc func;
	gpointer user_data;
} foreach_data_t;

static void
foreach_hash_func (gpointer  key, gpointer value, gpointer user_data)
{
	foreach_data_t *data = (foreach_data_t *) user_data;

	data->func ((const char *) key, (const char *) value, data->user_data);
}

void
e_xmlhash_foreach_key (EXmlHash *hash, EXmlHashFunc func, gpointer user_data)
{
	foreach_data_t data;

	g_return_if_fail (hash != NULL);
	g_return_if_fail (func != NULL);

	data.func = func;
	data.user_data = user_data;
	g_hash_table_foreach (hash->objects, foreach_hash_func, &data);
}

void
e_xmlhash_write (EXmlHash *hash)
{
	const char *slash;
	char *filesave;
	xmlDoc *doc;

	g_return_if_fail (hash != NULL);

	doc = e_xml_from_hash (hash->objects, E_XML_HASH_TYPE_OBJECT_UID, "xmlhash");
	
	filesave = g_alloca (strlen (hash->filename) + 5);
	if ((slash = strrchr (hash->filename, '/')))
		sprintf (filesave, "%.*s.#%s", slash - hash->filename + 1, hash->filename, slash + 1);
	else
		sprintf (filesave, ".#%s", hash->filename);
	
	if (xmlSaveFile (filesave, doc) != -1)
		rename (filesave, hash->filename);
	else
		unlink (filesave);
	
	xmlFreeDoc (doc);
}

void
e_xmlhash_destroy (EXmlHash *hash)
{
	g_return_if_fail (hash != NULL);

	g_free (hash->filename);
	if (hash->objects)
		e_xml_destroy_hash (hash->objects);

	g_free (hash);
}
