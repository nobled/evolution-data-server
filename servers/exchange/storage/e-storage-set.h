/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-storage-set.h
 *
 * Copyright (C) 2000  Ximian, Inc.
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
 * Author: Ettore Perazzoli
 */

#ifndef _E_STORAGE_SET_H_
#define _E_STORAGE_SET_H_

#include <gtk/gtkwidget.h>

#include "e-folder-type-registry.h"
#include "e-storage.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define E_TYPE_STORAGE_SET			(e_storage_set_get_type ())
#define E_STORAGE_SET(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_STORAGE_SET, EStorageSet))
#define E_STORAGE_SET_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_STORAGE_SET, EStorageSetClass))
#define E_IS_STORAGE_SET(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_STORAGE_SET))
#define E_IS_STORAGE_SET_CLASS(klass)		(G_TYPE_CHECK_CLASS_TYPE ((obj), E_TYPE_STORAGE_SET))

typedef struct EStorageSet        EStorageSet;
typedef struct EStorageSetPrivate EStorageSetPrivate;
typedef struct EStorageSetClass   EStorageSetClass;

typedef void (* EStorageSetResultCallback) (EStorageSet *storage_set, EStorageResult result, void *data);

struct EStorageSet {
	GObject parent;

	EStorageSetPrivate *priv;
};

struct EStorageSetClass {
	GObjectClass parent_class;

	/* Signals.  */

	void (* new_storage)     (EStorageSet *storage_set, EStorage *storage);
	void (* removed_storage) (EStorageSet *storage_set, EStorage *storage);
	/* FIXME?  Inconsistency between storage and folders.  */
	void (* new_folder)     (EStorageSet *storage_set, const char *path);
	void (* updated_folder) (EStorageSet *storage_set, const char *path);
	void (* removed_folder) (EStorageSet *storage_set, const char *path);
	void (* moved_folder)   (EStorageSet *storage_set, const char *source_path, const char *destination_path);
	void (* close_folder)   (EStorageSet *storage_set, const char *path);
};

GType        e_storage_set_get_type             (void);
void         e_storage_set_construct            (EStorageSet            *storage_set,
						 EFolderTypeRegistry    *folder_type_registry);
EStorageSet *e_storage_set_new                  (EFolderTypeRegistry    *folder_type_registry);

gboolean     e_storage_set_add_storage          (EStorageSet            *storage_set,
						 EStorage               *storage);
gboolean     e_storage_set_remove_storage       (EStorageSet            *storage_set,
						 EStorage               *storage);
void         e_storage_set_remove_all_storages  (EStorageSet            *storage_set);
GList       *e_storage_set_get_storage_list     (EStorageSet            *storage_set);
EStorage    *e_storage_set_get_storage          (EStorageSet            *storage_set,
						 const char             *storage_name);
EFolder     *e_storage_set_get_folder           (EStorageSet            *storage_set,
						 const char             *path);
GtkWidget   *e_storage_set_create_new_view      (EStorageSet            *storage_set);

void  e_storage_set_async_create_folder  (EStorageSet               *storage_set,
					  const char                *path,
					  const char                *type,
					  EStorageSetResultCallback  callback,
					  void                      *data);
void  e_storage_set_async_remove_folder  (EStorageSet               *storage_set,
					  const char                *path,
					  EStorageSetResultCallback  callback,
					  void                      *data);
void  e_storage_set_async_xfer_folder    (EStorageSet               *storage_set,
					  const char                *source_path,
					  const char                *destination_path,
					  gboolean                   remove_source,
					  EStorageSetResultCallback  callback,
					  void                      *data);

void  e_storage_set_async_remove_shared_folder (EStorageSet               *storage_set,
						const char                *path,
						EStorageSetResultCallback  callback,
						void                      *data);

EFolderTypeRegistry *e_storage_set_get_folder_type_registry (EStorageSet *storage_set);

/* Utility functions.  */

char *e_storage_set_get_path_for_physical_uri  (EStorageSet *storage_set,
						const char  *physical_uri);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _E_STORAGE_SET_H_ */
