/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-source-group.c
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
#include "e-source-group.h"

static GObjectClass *parent_class = NULL;

/* Private members.  */

struct _ESourceGroupPrivate {
	char *uid;
	char *name;
	char *base_uri;

	GSList *sources;

	gboolean ignore_source_changed;
	gboolean readonly;
};


/* Signals.  */

enum {
	CHANGED,
	SOURCE_REMOVED,
	SOURCE_ADDED,
	LAST_SIGNAL
};
static unsigned int signals[LAST_SIGNAL] = { 0 };


/* Callbacks.  */

static void
source_changed_callback (ESource *source,
			 ESourceGroup *group)
{
	if (! group->priv->ignore_source_changed)
		g_signal_emit (group, signals[CHANGED], 0);
}


/* GObject methods.  */

static void
impl_dispose (GObject *object)
{
	ESourceGroupPrivate *priv = E_SOURCE_GROUP (object)->priv;

	if (priv->sources != NULL) {
		GSList *p;

		for (p = priv->sources; p != NULL; p = p->next) {
			ESource *source = E_SOURCE (p->data);

			g_signal_handlers_disconnect_by_func (source,
							      G_CALLBACK (source_changed_callback),
							      object);
			g_object_unref (source);
		}

		g_slist_free (priv->sources);
		priv->sources = NULL;
	}

	(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
impl_finalize (GObject *object)
{
	ESourceGroupPrivate *priv = E_SOURCE_GROUP (object)->priv;

	g_free (priv->uid);
	g_free (priv->name);
	g_free (priv->base_uri);
	g_free (priv);

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}


/* Initialization.  */

static void
class_init (ESourceGroupClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	object_class->dispose  = impl_dispose;
	object_class->finalize = impl_finalize;

	parent_class = g_type_class_peek_parent (class);

	signals[CHANGED] = 
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ESourceGroupClass, changed),
			      NULL, NULL,
			      e_data_server_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	signals[SOURCE_ADDED] = 
		g_signal_new ("source_added",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ESourceGroupClass, source_added),
			      NULL, NULL,
			      e_data_server_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1,
			      G_TYPE_OBJECT);
	signals[SOURCE_REMOVED] = 
		g_signal_new ("source_removed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ESourceGroupClass, source_removed),
			      NULL, NULL,
			      e_data_server_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1,
			      G_TYPE_OBJECT);
}

static void
init (ESourceGroup *source_group)
{
	ESourceGroupPrivate *priv;

	priv = g_new0 (ESourceGroupPrivate, 1);
	source_group->priv = priv;
}

GType
e_source_group_get_type (void)
{
	static GType e_source_group_type = 0;

	if (!e_source_group_type) {
		static GTypeInfo info = {
                        sizeof (ESourceGroupClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) class_init,
                        NULL, NULL,
                        sizeof (ESourceGroup),
                        0,
                        (GInstanceInitFunc) init
                };
		e_source_group_type = g_type_register_static (G_TYPE_OBJECT, "ESourceGroup", &info, 0);
	}

	return e_source_group_type;
}

/* Public methods.  */

ESourceGroup *
e_source_group_new (const char *name,
		    const char *base_uri)
{
	ESourceGroup *new;

	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (base_uri != NULL, NULL);

	new = g_object_new (e_source_group_get_type (), NULL);
	new->priv->uid = e_uid_new ();

	e_source_group_set_name (new, name);
	e_source_group_set_base_uri (new, base_uri);

	return new;
}

ESourceGroup *
e_source_group_new_from_xml (const char *xml)
{
	xmlDocPtr doc;
	ESourceGroup *group;

	doc = xmlParseDoc ((char *) xml);
	if (doc == NULL)
		return NULL;

	group = e_source_group_new_from_xmldoc (doc);
	xmlFreeDoc (doc);

	return group;
}

ESourceGroup *
e_source_group_new_from_xmldoc (xmlDocPtr doc)
{
	xmlNodePtr root, p;
	xmlChar *uid;
	xmlChar *name;
	xmlChar *base_uri;
	xmlChar *readonly_str;
	ESourceGroup *new = NULL;

	g_return_val_if_fail (doc != NULL, NULL);

	root = doc->children;
	if (strcmp (root->name, "group") != 0)
		return NULL;

	uid = xmlGetProp (root, "uid");
	name = xmlGetProp (root, "name");
	base_uri = xmlGetProp (root, "base_uri");
	readonly_str = xmlGetProp (root, "readonly");

	if (uid == NULL || name == NULL || base_uri == NULL)
		goto done;

	new = g_object_new (e_source_group_get_type (), NULL);
	new->priv->uid = g_strdup (uid);

	e_source_group_set_name (new, name);
	e_source_group_set_base_uri (new, base_uri);
	
	for (p = root->children; p != NULL; p = p->next) {
		ESource *new_source = e_source_new_from_xml_node (p);

		if (new_source == NULL) {
			g_object_unref (new);
			goto done;
		}
		e_source_group_add_source (new, new_source, -1);
	}

	e_source_group_set_readonly (new, readonly_str && !strcmp (readonly_str, "yes"));

 done:
	if (uid != NULL)
		xmlFree (uid);

	if (name != NULL)
		xmlFree (name);
	if (base_uri != NULL)
		xmlFree (base_uri);
	if (readonly_str != NULL)
		xmlFree (readonly_str);
	return new;
}

gboolean
e_source_group_update_from_xml (ESourceGroup *group,
				const char *xml,
				gboolean *changed_return)
{
	xmlDocPtr xmldoc;
	gboolean success;

	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), FALSE);
	g_return_val_if_fail (xml != NULL, FALSE);

	xmldoc = xmlParseDoc ((char *) xml);

	success = e_source_group_update_from_xmldoc (group, xmldoc, changed_return);

	xmlFreeDoc (xmldoc);

	return success;
}

gboolean
e_source_group_update_from_xmldoc (ESourceGroup *group,
				   xmlDocPtr doc,
				   gboolean *changed_return)
{
	GHashTable *new_sources_hash;
	GSList *new_sources_list = NULL;
	xmlNodePtr root, nodep;
	xmlChar *name, *base_uri, *readonly_str;
	gboolean readonly;
	gboolean changed = FALSE;
	GSList *p, *q;

	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), FALSE);
	g_return_val_if_fail (doc != NULL, FALSE);

	*changed_return = FALSE;

	root = doc->children;
	if (strcmp (root->name, "group") != 0)
		return FALSE;

	name = xmlGetProp (root, "name");
	if (name == NULL)
		return FALSE;

	base_uri = xmlGetProp (root, "base_uri");
	if (base_uri == NULL) {
		xmlFree (name);
		return FALSE;
	}

	if (strcmp (group->priv->name, name) != 0) {
		g_free (group->priv->name);
		group->priv->name = g_strdup (name);
		changed = TRUE;
	}
	xmlFree (name);

	if (strcmp (group->priv->base_uri, base_uri) != 0) {
		g_free (group->priv->base_uri);
		group->priv->base_uri = g_strdup (base_uri);
		changed = TRUE;
	}
	xmlFree (base_uri);

	readonly_str = xmlGetProp (root, "readonly");
	readonly = readonly_str && !strcmp (readonly_str, "yes");
	if (readonly != group->priv->readonly) {
		group->priv->readonly = readonly;
		changed = TRUE;
	}
	xmlFree (readonly_str);
	
	new_sources_hash = g_hash_table_new (g_direct_hash, g_direct_equal);

	for (nodep = root->children; nodep != NULL; nodep = nodep->next) {
		ESource *existing_source;
		char *uid = e_source_uid_from_xml_node (nodep);

		if (uid == NULL)
			continue;

		existing_source = e_source_group_peek_source_by_uid (group, uid);
		if (g_hash_table_lookup (new_sources_hash, existing_source) != NULL)
			continue;

		if (existing_source == NULL) {
			ESource *new_source = e_source_new_from_xml_node (nodep);

			if (new_source != NULL) {
				e_source_set_group (new_source, group);
				g_signal_connect (new_source, "changed", G_CALLBACK (source_changed_callback), group);
				new_sources_list = g_slist_prepend (new_sources_list, new_source);

				g_hash_table_insert (new_sources_hash, new_source, new_source);

				g_signal_emit (group, signals[SOURCE_ADDED], 0, new_source);
				changed = TRUE;
			}
		} else {
			gboolean source_changed;

			group->priv->ignore_source_changed ++;

			if (e_source_update_from_xml_node (existing_source, nodep, &source_changed)) {
				new_sources_list = g_slist_prepend (new_sources_list, existing_source);
				g_object_ref (existing_source);
				g_hash_table_insert (new_sources_hash, existing_source, existing_source);

				if (source_changed)
					changed = TRUE;
			}

			group->priv->ignore_source_changed --;
		}

		g_free (uid);
	}

	new_sources_list = g_slist_reverse (new_sources_list);

	/* Emit "group_removed" and disconnect the "changed" signal for all the
	   groups that we haven't found in the new list.  */
	q = new_sources_list;
	for (p = group->priv->sources; p != NULL; p = p->next) {
		ESource *source = E_SOURCE (p->data);

		if (g_hash_table_lookup (new_sources_hash, source) == NULL) {
			changed = TRUE;

			g_signal_emit (group, signals[SOURCE_REMOVED], 0, source);
			g_signal_handlers_disconnect_by_func (source, source_changed_callback, group);
		}

		if (! changed && q != NULL) {
			if (q->data != p->data)
				changed = TRUE;
			q = q->next;
		}
	}

	g_hash_table_destroy (new_sources_hash);

	/* Replace the original group list with the new one.  */
	g_slist_foreach (group->priv->sources, (GFunc) g_object_unref, NULL);
	g_slist_free (group->priv->sources);

	group->priv->sources = new_sources_list;

	/* FIXME if the order changes, the function doesn't notice.  */

	if (changed) {
		g_signal_emit (group, signals[CHANGED], 0);
		*changed_return = TRUE;
	}

	return TRUE;		/* Success. */
}

char *
e_source_group_uid_from_xmldoc (xmlDocPtr doc)
{
	xmlNodePtr root = doc->children;
	xmlChar *name;
	char *retval;

	if (strcmp (root->name, "group") != 0)
		return NULL;

	name = xmlGetProp (root, "uid");
	if (name == NULL)
		return NULL;

	retval = g_strdup (name);
	xmlFree (name);
	return retval;
}

void
e_source_group_set_name (ESourceGroup *group,
			 const char *name)
{
	g_return_if_fail (E_IS_SOURCE_GROUP (group));
	g_return_if_fail (name != NULL);

	if (group->priv->readonly)
		return;
	
	if (group->priv->name == name)
		return;

	g_free (group->priv->name);
	group->priv->name = g_strdup (name);

	g_signal_emit (group, signals[CHANGED], 0);
}

void e_source_group_set_base_uri (ESourceGroup *group,
				  const char *base_uri)
{
	g_return_if_fail (E_IS_SOURCE_GROUP (group));
	g_return_if_fail (base_uri != NULL);
	
	if (group->priv->readonly)
		return;
	
	if (group->priv->base_uri == base_uri)
		return;

	g_free (group->priv->base_uri);
	group->priv->base_uri = g_strdup (base_uri);

	g_signal_emit (group, signals[CHANGED], 0);
}

void e_source_group_set_readonly (ESourceGroup *group,
				  gboolean      readonly)
{
	GSList *i;
	
	g_return_if_fail (E_IS_SOURCE_GROUP (group));
	
	if (group->priv->readonly)
		return;
	
	if (group->priv->readonly == readonly)
		return;

	group->priv->readonly = readonly;
	for (i = group->priv->sources; i != NULL; i = i->next)
		e_source_set_readonly (E_SOURCE (i->data), readonly);	

	g_signal_emit (group, signals[CHANGED], 0);
}

const char *
e_source_group_peek_uid (ESourceGroup *group)
{
	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), NULL);

	return group->priv->uid;
}

const char *
e_source_group_peek_name (ESourceGroup *group)
{
	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), NULL);

	return group->priv->name;
}

const char *
e_source_group_peek_base_uri (ESourceGroup *group)
{
	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), NULL);

	return group->priv->base_uri;
}

gboolean
e_source_group_get_readonly (ESourceGroup *group)
{
	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), FALSE);

	return group->priv->readonly;
}

GSList *
e_source_group_peek_sources (ESourceGroup *group)
{
	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), NULL);

	return group->priv->sources;
}

ESource *
e_source_group_peek_source_by_uid (ESourceGroup *group,
				   const char *uid)
{
	GSList *p;

	for (p = group->priv->sources; p != NULL; p = p->next) {
		if (strcmp (e_source_peek_uid (E_SOURCE (p->data)), uid) == 0)
			return E_SOURCE (p->data);
	}

	return NULL;
}

ESource *
e_source_group_peek_source_by_name (ESourceGroup *group,
				    const char *name)
{
	GSList *p;

	for (p = group->priv->sources; p != NULL; p = p->next) {
		if (strcmp (e_source_peek_name (E_SOURCE (p->data)), name) == 0)
			return E_SOURCE (p->data);
	}

	return NULL;
}

gboolean
e_source_group_add_source (ESourceGroup *group,
			   ESource *source,
			   int position)
{
	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), FALSE);

	if (group->priv->readonly)
		return FALSE;
	
	if (e_source_group_peek_source_by_uid (group, e_source_peek_uid (source)) != NULL)
		return FALSE;

	e_source_set_group (source, group);
	e_source_set_readonly (source, group->priv->readonly);
	g_object_ref (source);

	g_signal_connect (source, "changed", G_CALLBACK (source_changed_callback), group);

	group->priv->sources = g_slist_insert (group->priv->sources, source, position);
	g_signal_emit (group, signals[SOURCE_ADDED], 0, source);
	g_signal_emit (group, signals[CHANGED], 0);

	return TRUE;
}

gboolean
e_source_group_remove_source (ESourceGroup *group,
			      ESource *source)
{
	GSList *p;

	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	if (group->priv->readonly)
		return FALSE;

	for (p = group->priv->sources; p != NULL; p = p->next) {
		if (E_SOURCE (p->data) == source) {
			group->priv->sources = g_slist_remove_link (group->priv->sources, p);
			g_signal_handlers_disconnect_by_func (source,
							      G_CALLBACK (source_changed_callback),
							      group);
			g_signal_emit (group, signals[SOURCE_REMOVED], 0, source);
			g_signal_emit (group, signals[CHANGED], 0);
			return TRUE;
		}
	}

	return FALSE;
}

gboolean
e_source_group_remove_source_by_uid (ESourceGroup *group,
				     const char *uid)
{
	GSList *p;

	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	if (group->priv->readonly)
		return FALSE;
	
	for (p = group->priv->sources; p != NULL; p = p->next) {
		ESource *source = E_SOURCE (p->data);

		if (strcmp (e_source_peek_uid (source), uid) == 0) {
			group->priv->sources = g_slist_remove_link (group->priv->sources, p);
			g_signal_handlers_disconnect_by_func (source,
							      G_CALLBACK (source_changed_callback),
							      group);
			g_signal_emit (group, signals[SOURCE_REMOVED], 0, source);
			g_signal_emit (group, signals[CHANGED], 0);
			return TRUE;
		}
	}

	return FALSE;
}


char *
e_source_group_to_xml (ESourceGroup *group)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	xmlChar *xml_buffer;
	char *returned_buffer;
	int xml_buffer_size;
	GSList *p;

	doc = xmlNewDoc ("1.0");

	root = xmlNewDocNode (doc, NULL, "group", NULL);
	xmlSetProp (root, "uid", e_source_group_peek_uid (group));
	xmlSetProp (root, "name", e_source_group_peek_name (group));
	xmlSetProp (root, "base_uri", e_source_group_peek_base_uri (group));
	xmlSetProp (root, "readonly", group->priv->readonly ? "yes" : "no");
	
	xmlDocSetRootElement (doc, root);

	for (p = group->priv->sources; p != NULL; p = p->next)
		e_source_dump_to_xml_node (E_SOURCE (p->data), root);

	xmlDocDumpMemory (doc, &xml_buffer, &xml_buffer_size);
	xmlFreeDoc (doc);

	returned_buffer = g_malloc (xml_buffer_size + 1);
	memcpy (returned_buffer, xml_buffer, xml_buffer_size);
	returned_buffer [xml_buffer_size] = '\0';
	xmlFree (xml_buffer);

	return returned_buffer;
}
