/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 *  e-data-server-module.c - Interface to e-d-s extensions
 * 
 *  Copyright (C) 2004 Novell, Inc.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Authors: Chris Toshok <toshok@ximian.com>
 *           Dave Camp <dave@ximian.com>
 * 
 */

#include <config.h>
#include "e-data-server-module.h"

#include <gmodule.h>
#include <libgnome/gnome-macros.h>

#define E_DATA_SERVER_TYPE_MODULE    	        (e_data_server_module_get_type ())
#define E_DATA_SERVER_MODULE(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), E_DATA_SERVER_TYPE_MODULE, EDataServerModule))
#define E_DATA_SERVER_MODULE_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), E_DATA_SERVER_TYPE_MODULE, EDataServerModule))
#define E_DATA_SERVER_IS_MODULE(obj)		(G_TYPE_INSTANCE_CHECK_TYPE ((obj), E_DATA_SERVER_TYPE_MODULE))
#define E_DATA_SERVER_IS_MODULE_CLASS(klass)	(G_TYPE_CLASS_CHECK_CLASS_TYPE ((klass), E_DATA_SERVER_TYPE_MODULE))

typedef struct _EDataServerModule        EDataServerModule;
typedef struct _EDataServerModuleClass   EDataServerModuleClass;

struct _EDataServerModule {
	GTypeModule parent;

	GModule *library;

	char *path;

	void (*initialize) (GTypeModule  *module);
	void (*shutdown)   (void);

	void (*list_types) (const GType **types,
			    int          *num_types);

};

struct _EDataServerModuleClass {
	GTypeModuleClass parent;	
};

static GType e_data_server_module_get_type (void);

static GList *module_objects = NULL;

GNOME_CLASS_BOILERPLATE (EDataServerModule,
			 e_data_server_module,
			 GTypeModule,
			 G_TYPE_TYPE_MODULE);

static gboolean
e_data_server_module_load (GTypeModule *gmodule)
{
	EDataServerModule *module;
	
	module = E_DATA_SERVER_MODULE (gmodule);
	
	module->library = g_module_open (module->path, 0);

	if (!module->library) {
		g_warning (g_module_error ());
		return FALSE;
	}

	if (!g_module_symbol (module->library,
			      "eds_module_initialize",
			      (gpointer *)&module->initialize) ||
	    !g_module_symbol (module->library,
			      "eds_module_shutdown",
			      (gpointer *)&module->shutdown) ||
	    !g_module_symbol (module->library,
			      "eds_module_list_types",
			      (gpointer *)&module->list_types)) {

		g_warning (g_module_error ());
		g_module_close (module->library);
		
		return FALSE;
	}

	module->initialize (gmodule);
	
	return TRUE;
}

static void
e_data_server_module_unload (GTypeModule *gmodule)
{
	EDataServerModule *module;
	
	module = E_DATA_SERVER_MODULE (gmodule);
	
	module->shutdown ();
	
	g_module_close (module->library);
	
	module->initialize = NULL;
	module->shutdown = NULL;
	module->list_types = NULL;
}

static void
e_data_server_module_finalize (GObject *object)
{
	EDataServerModule *module;
	
	module = E_DATA_SERVER_MODULE (object);

	g_free (module->path);

        if (G_OBJECT_CLASS (parent_class)->finalize)
		(*G_OBJECT_CLASS (parent_class)->finalize) (object);
}

static void
e_data_server_module_instance_init (EDataServerModule *module)
{
}

static void
e_data_server_module_class_init (EDataServerModuleClass *class)
{
	G_OBJECT_CLASS (class)->finalize = e_data_server_module_finalize;
	G_TYPE_MODULE_CLASS (class)->load = e_data_server_module_load;
	G_TYPE_MODULE_CLASS (class)->unload = e_data_server_module_unload;
}

static void
module_object_weak_notify (gpointer user_data, GObject *object)
{
	module_objects = g_list_remove (module_objects, object);
}

static void
add_module_objects (EDataServerModule *module)
{
	const GType *types;
	int num_types;
	int i;
	
	module->list_types (&types, &num_types);
	
	for (i = 0; i < num_types; i++) {
		e_data_server_module_add_type (types[i]);
	}
}

static EDataServerModule *
e_data_server_module_load_file (const char *filename)
{
	EDataServerModule *module;
	
	module = g_object_new (E_DATA_SERVER_TYPE_MODULE, NULL);
	module->path = g_strdup (filename);
	
	if (g_type_module_use (G_TYPE_MODULE (module))) {
		add_module_objects (module);
		g_type_module_unuse (G_TYPE_MODULE (module));
		return module;
	} else {
		g_object_unref (module);
		return NULL;
	}
}

static void
load_module_dir (const char *dirname)
{
	GDir *dir;
	
	dir = g_dir_open (dirname, 0, NULL);
	
	if (dir) {
		const char *name;
		
		while ((name = g_dir_read_name (dir))) {
			if (g_str_has_suffix (name, "." G_MODULE_SUFFIX)) {
				char *filename;

				filename = g_build_filename (dirname, 
							     name, 
							     NULL);
				e_data_server_module_load_file (filename);
			}
		}

		g_dir_close (dir);
	}
}

void
e_data_server_module_init (void)
{
	static gboolean initialized = FALSE;

	if (!initialized) {
		initialized = TRUE;
		
		load_module_dir (E_DATA_SERVER_EXTENSIONDIR);
	}
}

GList *
e_data_server_get_extensions_for_type (GType type)
{
	GList *l;
	GList *ret = NULL;
	
	for (l = module_objects; l != NULL; l = l->next) {
		if (G_TYPE_CHECK_INSTANCE_TYPE (G_OBJECT (l->data),
						type)) {
			g_object_ref (l->data);
			ret = g_list_prepend (ret, l->data);
		}
	}

	return ret;	
}

void
e_data_server_extension_list_free (GList *extensions)
{
	GList *l;
	
	for (l = extensions; l != NULL; l = l->next) {
		g_object_unref (l->data);
	}
	g_list_free (extensions);
}

void   
e_data_server_module_add_type (GType type)
{
	GObject *object;

	g_message ("adding type `%s'", g_type_name (type));

	object = g_object_new (type, NULL);
	g_object_weak_ref (object, 
			   (GWeakNotify)module_object_weak_notify,
			   NULL);

	module_objects = g_list_prepend (module_objects, object);
}
