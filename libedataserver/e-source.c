/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-source.c
 *
 * Copyright (C) 2003  Ximian, Inc.
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
 * Author: Ettore Perazzoli <ettore@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include "e-data-server-marshal.h"
#include "e-uid.h"
#include "e-source.h"

static GObjectClass *parent_class = NULL;

#define ES_CLASS(obj)  E_SOURCE_CLASS (G_OBJECT_GET_CLASS (obj))


/* String used to put the color in the XML.  */
#define COLOR_FORMAT_STRING "%06x"


/* Private members.  */

struct _ESourcePrivate {
	ESourceGroup *group;

	char *uid;
	char *name;
	char *relative_uri;
	char *absolute_uri;

	gboolean readonly;
	
	gboolean has_color;
	guint32 color;

	GHashTable *properties;
};


/* Signals.  */

enum {
	CHANGED,
	LAST_SIGNAL
};
static unsigned int signals[LAST_SIGNAL] = { 0 };


/* Callbacks.  */

static void
group_weak_notify (ESource *source,
		   GObject **where_the_object_was)
{
	source->priv->group = NULL;

	g_signal_emit (source, signals[CHANGED], 0);
}


/* GObject methods.  */

static void
impl_finalize (GObject *object)
{
	ESourcePrivate *priv = E_SOURCE (object)->priv;

	g_free (priv->uid);
	g_free (priv->name);
	g_free (priv->relative_uri);
	g_free (priv->absolute_uri);

	g_hash_table_destroy (priv->properties);

	g_free (priv);

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

static void
impl_dispose (GObject *object)
{
	ESourcePrivate *priv = E_SOURCE (object)->priv;

	if (priv->group != NULL) {
		g_object_weak_unref (G_OBJECT (priv->group), (GWeakNotify) group_weak_notify, object);
		priv->group = NULL;
	}

	(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}


/* Initialization.  */

static void
class_init (ESourceClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	object_class->dispose  = impl_dispose;
	object_class->finalize = impl_finalize;

	parent_class = g_type_class_peek_parent (class);

	signals[CHANGED] = 
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ESourceClass, changed),
			      NULL, NULL,
			      e_data_server_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

static void
init (ESource *source)
{
	ESourcePrivate *priv;

	priv = g_new0 (ESourcePrivate, 1);
	source->priv = priv;

	priv->properties = g_hash_table_new_full (g_str_hash, g_str_equal,
						  g_free, g_free);
}

GType
e_source_get_type (void)
{
	static GType e_source_type = 0;

	if (!e_source_type) {
		static GTypeInfo info = {
                        sizeof (ESourceClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) class_init,
                        NULL, NULL,
                        sizeof (ESource),
                        0,
                        (GInstanceInitFunc) init
                };
		e_source_type = g_type_register_static (G_TYPE_OBJECT, "ESource", &info, 0);
	}

	return e_source_type;
}

/* Public methods.  */

ESource *
e_source_new  (const char *name,
	       const char *relative_uri)
{
	ESource *source;

	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (relative_uri != NULL, NULL);

	source = g_object_new (e_source_get_type (), NULL);
	source->priv->uid = e_uid_new ();

	e_source_set_name (source, name);
	e_source_set_relative_uri (source, relative_uri);
	return source;
}

ESource *
e_source_new_with_absolute_uri (const char *name,
				const char *absolute_uri)
{
	ESource *source;

	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (absolute_uri != NULL, NULL);

	source = g_object_new (e_source_get_type (), NULL);
	source->priv->uid = e_uid_new ();

	e_source_set_name (source, name);
	e_source_set_absolute_uri (source, absolute_uri);
	return source;
}

ESource *
e_source_new_from_xml_node (xmlNodePtr node)
{
	ESource *source;
	xmlChar *uid;

	uid = xmlGetProp (node, "uid");
	if (uid == NULL)
		return NULL;

	source = g_object_new (e_source_get_type (), NULL);

	source->priv->uid = g_strdup (uid);
	xmlFree (uid);

	if (e_source_update_from_xml_node (source, node, NULL))
		return source;

	g_object_unref (source);
	return NULL;
}

gboolean
e_source_equal (ESource *source_1, ESource *source_2)
{
	gboolean equal = FALSE;

	g_return_val_if_fail (E_IS_SOURCE (source_1), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source_2), FALSE);

	if (source_1->priv->uid && source_2->priv->uid &&
	    !strcmp (source_1->priv->uid, source_2->priv->uid)) {
		equal = TRUE;
	} else {
		gchar *uri_1, *uri_2;

		uri_1 = e_source_get_uri (source_1);
		uri_2 = e_source_get_uri (source_2);

		if (uri_1 && uri_2 && !strcmp (uri_1, uri_2))
			equal = TRUE;

		g_free (uri_1);
		g_free (uri_2);
	}

	return equal;
}

static void
import_properties (ESource *source,
		   xmlNodePtr prop_root)
{
	ESourcePrivate *priv = source->priv;
	xmlNodePtr prop_node;

	for (prop_node = prop_root->children; prop_node; prop_node = prop_node->next) {
		xmlChar *name, *value;

		if (!prop_node->name || strcmp (prop_node->name, "property"))
			continue;

		name = xmlGetProp (prop_node, "name");
		value = xmlGetProp (prop_node, "value");

		if (name && value)
			g_hash_table_insert (priv->properties, g_strdup (name), g_strdup (value));

		if (name)
			xmlFree (name);
		if (value)
			xmlFree (value);
	}
}


/**
 * e_source_update_from_xml_node:
 * @source: An ESource.
 * @node: A pointer to the node to parse.
 * 
 * Update the ESource properties from @node.
 * 
 * Return value: %TRUE if the data in @node was recognized and parsed into
 * acceptable values for @source, %FALSE otherwise.
 **/
gboolean
e_source_update_from_xml_node (ESource *source,
			       xmlNodePtr node,
			       gboolean *changed_return)
{
	xmlChar *name;
	xmlChar *relative_uri;
	xmlChar *absolute_uri;
	xmlChar *color_string;
	gboolean retval;
	gboolean changed = FALSE;

	name = xmlGetProp (node, "name");
	relative_uri = xmlGetProp (node, "relative_uri");
	absolute_uri = xmlGetProp (node, "uri");
	color_string = xmlGetProp (node, "color");

	if (name == NULL || (relative_uri == NULL && absolute_uri == NULL)) {
		retval = FALSE;
		goto done;
	}

	if (source->priv->name == NULL
	    || strcmp (name, source->priv->name) != 0
	    || source->priv->relative_uri == NULL
	    || relative_uri != NULL
	    || strcmp (relative_uri, source->priv->relative_uri) != 0) {
		g_free (source->priv->name);
		source->priv->name = g_strdup (name);

		g_free (source->priv->relative_uri);
		source->priv->relative_uri = g_strdup (relative_uri);

		changed = TRUE;
	}

	if (absolute_uri != NULL) {
		g_free (source->priv->absolute_uri);
		source->priv->absolute_uri = g_strdup (absolute_uri);
		changed = TRUE;
	}

	if (color_string == NULL) {
		if (source->priv->has_color) {
			source->priv->has_color = FALSE;
			changed = TRUE;
		}
	} else {
		guint32 color = 0;

		sscanf (color_string, COLOR_FORMAT_STRING, &color);
		if (! source->priv->has_color || source->priv->color != color) {
			source->priv->has_color = TRUE;
			source->priv->color = color;
			changed = TRUE;
		}
	}

	for (node = node->children; node; node = node->next) {
		if (!node->name)
			continue;

		if (!strcmp (node->name, "properties")) {
			g_hash_table_destroy (source->priv->properties);
			source->priv->properties = g_hash_table_new_full (g_str_hash, g_str_equal,
									  g_free, g_free);
			import_properties (source, node);
			break;
		}
	}

	retval = TRUE;

 done:
	if (changed)
		g_signal_emit (source, signals[CHANGED], 0);

	if (changed_return != NULL)
		*changed_return = changed;

	if (name != NULL)
		xmlFree (name);
	if (relative_uri != NULL)
		xmlFree (relative_uri);
	if (absolute_uri != NULL)
		xmlFree (absolute_uri);
	if (color_string != NULL)
		xmlFree (color_string);

	return retval;
}

/**
 * e_source_name_from_xml_node:
 * @node: A pointer to an XML node.
 * 
 * Assuming that @node is a valid ESource specification, retrieve the name of
 * the source from it.
 * 
 * Return value: Name of the source in the specified @node.  The caller must
 * free the string.
 **/
char *
e_source_uid_from_xml_node (xmlNodePtr node)
{
	xmlChar *uid = xmlGetProp (node, "uid");
	char *retval;

	if (uid == NULL)
		return NULL;

	retval = g_strdup (uid);
	xmlFree (uid);
	return retval;
}

char *
e_source_build_absolute_uri (ESource *source)
{
	const gchar *base_uri_str;
	gchar *uri_str;

	g_return_val_if_fail (source->priv->group != NULL, NULL);
	
	base_uri_str = e_source_group_peek_base_uri (source->priv->group);

	/* If last character in base URI is a dir separator, just concat the strings.
	 * We don't want to compress e.g. the trailing :// in a protocol specification */
	if (*base_uri_str && *(base_uri_str + strlen (base_uri_str) - 1) == G_DIR_SEPARATOR)
		uri_str = g_strconcat (base_uri_str, source->priv->relative_uri, NULL);
	else
		uri_str = g_build_filename (e_source_group_peek_base_uri (source->priv->group),
					    source->priv->relative_uri,
					    NULL);

	return uri_str;
}

void
e_source_set_group (ESource *source,
		    ESourceGroup *group)
{
	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (group == NULL || E_IS_SOURCE_GROUP (group));

	if (source->priv->readonly)
		return;
	
	if (source->priv->group == group)
		return;

	if (source->priv->group != NULL)
		g_object_weak_unref (G_OBJECT (source->priv->group), (GWeakNotify) group_weak_notify, source);

	source->priv->group = group;
	if (group != NULL) {
		g_object_weak_ref (G_OBJECT (group), (GWeakNotify) group_weak_notify, source);
	}

	g_signal_emit (source, signals[CHANGED], 0);
}

void
e_source_set_name (ESource *source,
		   const char *name)
{
	g_return_if_fail (E_IS_SOURCE (source));

	if (source->priv->readonly)
		return;
	
	if (source->priv->name == name)
		return;

	g_free (source->priv->name);
	source->priv->name = g_strdup (name);

	g_signal_emit (source, signals[CHANGED], 0);
}

void
e_source_set_relative_uri (ESource *source,
			   const char *relative_uri)
{
	g_return_if_fail (E_IS_SOURCE (source));

	if (source->priv->readonly)
		return;
	
	if (source->priv->relative_uri == relative_uri)
		return;

	g_free (source->priv->relative_uri);
	source->priv->relative_uri = g_strdup (relative_uri);

	g_signal_emit (source, signals[CHANGED], 0);
}

void
e_source_set_absolute_uri (ESource *source,
			   const char *absolute_uri)
{
	g_return_if_fail (E_IS_SOURCE (source));

	if (!!absolute_uri == !!source->priv->absolute_uri
	    || (absolute_uri && source->priv->absolute_uri && !strcmp (source->priv->absolute_uri, absolute_uri)))
		return;

	g_free (source->priv->absolute_uri);
	source->priv->absolute_uri = g_strdup (absolute_uri);

	g_signal_emit (source, signals[CHANGED], 0);
}

void
e_source_set_readonly (ESource  *source,
		       gboolean  readonly)
{
	g_return_if_fail (E_IS_SOURCE (source));

	if (source->priv->readonly == readonly)
		return;

	source->priv->readonly = readonly;

	g_signal_emit (source, signals[CHANGED], 0);
	
}

void
e_source_set_color (ESource *source,
		    guint32 color)
{
	g_return_if_fail (E_IS_SOURCE (source));

	if (source->priv->readonly)
		return;
	
	if (source->priv->has_color && source->priv->color == color)
		return;

	source->priv->has_color = TRUE;
	source->priv->color = color;

	g_signal_emit (source, signals[CHANGED], 0);
}

void
e_source_unset_color (ESource *source)
{
	g_return_if_fail (E_IS_SOURCE (source));

	if (! source->priv->has_color)
		return;

	source->priv->has_color = FALSE;
	g_signal_emit (source, signals[CHANGED], 0);
}


ESourceGroup *
e_source_peek_group (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return source->priv->group;
}

const char *
e_source_peek_uid (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return source->priv->uid;
}

const char *
e_source_peek_name (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return source->priv->name;
}

const char *
e_source_peek_relative_uri (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return source->priv->relative_uri;
}

const char *
e_source_peek_absolute_uri (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return source->priv->absolute_uri;
}

gboolean
e_source_get_readonly (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	return source->priv->readonly;
}


/**
 * e_source_get_color:
 * @source: An ESource
 * @color_return: Pointer to a variable where the returned color will be
 * stored.
 * 
 * If @source has an associated color, return it in *@color_return.
 * 
 * Return value: %TRUE if the @source has a defined color (and hence
 * *@color_return was set), %FALSE otherwise.
 **/
gboolean
e_source_get_color (ESource *source,
		    guint32 *color_return)
{
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	if (! source->priv->has_color)
		return FALSE;

	if (color_return != NULL)
		*color_return = source->priv->color;

	return TRUE;
}

char *
e_source_get_uri (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	if (source->priv->group == NULL) {
		if (source->priv->absolute_uri != NULL)
			return g_strdup (source->priv->absolute_uri);

		g_warning ("e_source_get_uri () called on source with no absolute URI!");
		return NULL;
	}
	else if (source->priv->absolute_uri != NULL) /* source->priv->group != NULL */
		return g_strdup (source->priv->absolute_uri);
	else
		return e_source_build_absolute_uri (source);
}


static void
property_dump_cb (const gchar *key, const gchar *value, xmlNodePtr root)
{
	xmlNodePtr node;

	node = xmlNewChild (root, NULL, "property", NULL);
	xmlSetProp (node, "name", key);
	xmlSetProp (node, "value", value);
}


static xmlNodePtr
dump_common_to_xml_node (ESource *source,
			 xmlNodePtr parent_node)
{
	ESourcePrivate *priv;
	gboolean has_color;
	guint32 color;
	xmlNodePtr node;
	const char *abs_uri = NULL, *relative_uri = NULL;

	priv = source->priv;

	if (parent_node)
		node = xmlNewChild (parent_node, NULL, "source", NULL);
	else
		node = xmlNewNode (NULL, "source");

	xmlSetProp (node, "uid", e_source_peek_uid (source));
	xmlSetProp (node, "name", e_source_peek_name (source));
	abs_uri = e_source_peek_absolute_uri (source);
	relative_uri = e_source_peek_relative_uri (source);
	if (abs_uri)
		xmlSetProp (node, "uri", abs_uri);
	if (relative_uri)
		xmlSetProp (node, "relative_uri", relative_uri);
	
	has_color = e_source_get_color (source, &color);
	if (has_color) {
		char *color_string = g_strdup_printf (COLOR_FORMAT_STRING, color);
		xmlSetProp (node, "color", color_string);
		g_free (color_string);
	}

	if (g_hash_table_size (priv->properties) != 0) {
		xmlNodePtr properties_node;

		properties_node = xmlNewChild (node, NULL, "properties", NULL);
		g_hash_table_foreach (priv->properties, (GHFunc) property_dump_cb, properties_node);
	}

	return node;
}


void
e_source_dump_to_xml_node (ESource *source,
			   xmlNodePtr parent_node)
{
	g_return_if_fail (E_IS_SOURCE (source));

	dump_common_to_xml_node (source, parent_node);
}


char *
e_source_to_standalone_xml (ESource *source)
{
	xmlDocPtr doc;
	xmlNodePtr node;
	xmlChar *xml_buffer;
	char *returned_buffer;
	int xml_buffer_size;
	gchar *uri;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	doc = xmlNewDoc ("1.0");
	node = dump_common_to_xml_node (source, NULL);

	xmlDocSetRootElement (doc, node);

	uri = e_source_get_uri (source);
	xmlSetProp (node, "uri", uri);
	g_free (uri);

	xmlDocDumpMemory (doc, &xml_buffer, &xml_buffer_size);
	xmlFreeDoc (doc);

	returned_buffer = g_malloc (xml_buffer_size + 1);
	memcpy (returned_buffer, xml_buffer, xml_buffer_size);
	returned_buffer [xml_buffer_size] = '\0';
	xmlFree (xml_buffer);

	return returned_buffer;
}


ESource *
e_source_new_from_standalone_xml (const char *xml)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	ESource *source;

	doc = xmlParseDoc ((char *) xml);
	if (doc == NULL)
		return NULL;

	root = doc->children;
	if (strcmp (root->name, "source") != 0)
		return NULL;

	source = e_source_new_from_xml_node (root);
	xmlFreeDoc (doc);

	return source;
}


const gchar *
e_source_get_property (ESource *source,
		       const gchar *property)
{
	ESourcePrivate *priv;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	priv = source->priv;

	return g_hash_table_lookup (priv->properties, property);
}


void
e_source_set_property (ESource *source,
		       const gchar *property,
		       const gchar *value)
{
	ESourcePrivate *priv;

	g_return_if_fail (E_IS_SOURCE (source));
	priv = source->priv;

	if (value)
		g_hash_table_replace (priv->properties, g_strdup (property), g_strdup (value));
	else
		g_hash_table_remove (priv->properties, property);

	g_signal_emit (source, signals[CHANGED], 0);
}


void
e_source_foreach_property (ESource *source, GHFunc func, gpointer data)
{
	ESourcePrivate *priv;

	g_return_if_fail (E_IS_SOURCE (source));
	priv = source->priv;

	g_hash_table_foreach (priv->properties, func, data);
}


static void
copy_property (const gchar *key, const gchar *value, ESource *new_source)
{
	e_source_set_property (new_source, key, value);
}


ESource *
e_source_copy (ESource *source)
{
	ESource *new_source;
	guint32  color;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	new_source = g_object_new (e_source_get_type (), NULL);
	new_source->priv->uid = g_strdup (e_source_peek_uid (source));

	e_source_set_name (new_source, e_source_peek_name (source));

	if (e_source_get_color (source, &color))
		e_source_set_color (new_source, color);

	new_source->priv->absolute_uri = g_strdup (e_source_peek_absolute_uri (source));

	e_source_foreach_property (source, (GHFunc) copy_property, new_source);

	return new_source;
}

