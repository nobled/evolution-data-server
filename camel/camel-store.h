/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-store.h : Abstract class for an email store */

/* 
 *
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright 1999, 2000 Ximian, Inc. (www.ximian.com)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */


#ifndef CAMEL_STORE_H
#define CAMEL_STORE_H 1


#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

/* for mode_t */
#include <sys/types.h>

#include <camel/camel-object.h>
#include <camel/camel-service.h>

enum {
	CAMEL_STORE_ARG_FIRST  = CAMEL_SERVICE_ARG_FIRST + 100,
};

typedef struct _CamelFolderInfo {
	struct _CamelFolderInfo *parent,
		*sibling,
		*child;
	char *url;
	char *name;
	char *full_name;
	char *path;
	int unread_message_count;
} CamelFolderInfo;

/* Structure of rename event's event_data */
typedef struct _CamelRenameInfo {
	char *old_base;
	struct _CamelFolderInfo *new;
} CamelRenameInfo;

#define CAMEL_STORE_TYPE     (camel_store_get_type ())
#define CAMEL_STORE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_STORE_TYPE, CamelStore))
#define CAMEL_STORE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_STORE_TYPE, CamelStoreClass))
#define CAMEL_IS_STORE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_STORE_TYPE))


/* Flags for store flags */
#define CAMEL_STORE_SUBSCRIPTIONS	(1 << 0)
#define CAMEL_STORE_VTRASH		(1 << 1)
#define CAMEL_STORE_FILTER_INBOX	(1 << 2)

struct _CamelStore
{
	CamelService parent_object;
	struct _CamelStorePrivate *priv;
	
	CamelFolder *vtrash;
	
	/* should have cache_lock when accessing this (priv->cache_lock) */
	GHashTable *folders;

	int flags;

	/* FIXME: This is a temporary measure until IMAP namespaces are properly implemented,
	   after that, all external folder api's will assume a dir separator of '/' */
	/* This is always a copy of IMAP_STORE()->dir_sep, or '/' */
	char dir_sep;
};


/* open mode for folder */
#define CAMEL_STORE_FOLDER_CREATE (1<<0)
#define CAMEL_STORE_FOLDER_BODY_INDEX (1<<1)
#define CAMEL_STORE_FOLDER_PRIVATE (1<<2) /* a private folder, that shouldn't show up in unmatched/folder info's, etc */

#define CAMEL_STORE_FOLDER_INFO_FAST       (1 << 0)
#define CAMEL_STORE_FOLDER_INFO_RECURSIVE  (1 << 1)
#define CAMEL_STORE_FOLDER_INFO_SUBSCRIBED (1 << 2)

/* camel store (and folder) sync flags */
#define CAMEL_STORE_SYNC_EXPUNGE (1<<0) /* expunge deleted messages */
#define CAMEL_STORE_SYNC_OFFLINE (1<<1)	/* prepare for offline */

typedef struct {
	CamelServiceClass parent_class;

	GHashFunc       hash_folder_name;
	GCompareFunc    compare_folder_name;

	CamelFolder *   (*get_folder)               (CamelStore *store,
						     const char *folder_name,
						     guint32 flags,
						     CamelException *ex);
	CamelFolder *   (*get_inbox)                (CamelStore *store,
						     CamelException *ex);
	
	void            (*init_trash)               (CamelStore *store);
	CamelFolder *   (*get_trash)                (CamelStore *store,
						     CamelException *ex);
	
	CamelFolderInfo *(*create_folder)           (CamelStore *store,
						     const char *parent_name,
						     const char *folder_name,
						     CamelException *ex);
	void            (*delete_folder)            (CamelStore *store,
						     const char *folder_name,
						     CamelException *ex);
	void		(*rename_folder)	    (CamelStore *store,
						     const char *old_name,
						     const char *new_name,
						     CamelException *ex);
	
	void            (*sync)                     (CamelStore *store, guint32 flags, CamelException *ex);
	
	CamelFolderInfo *(*get_folder_info)         (CamelStore *store,
						     const char *top,
						     guint32 flags,
						     CamelException *ex);
	void            (*free_folder_info)         (CamelStore *store,
						     CamelFolderInfo *fi);
	
	gboolean        (*folder_subscribed)        (CamelStore *store,
						     const char *folder_name);
	void            (*subscribe_folder)         (CamelStore *store,
						     const char *folder_name,
						     CamelException *ex);
	void            (*unsubscribe_folder)       (CamelStore *store,
						     const char *folder_name,
						     CamelException *ex);
} CamelStoreClass;


/* Standard Camel function */
CamelType camel_store_get_type (void);

/* public methods */
CamelFolder *    camel_store_get_folder         (CamelStore *store,
					         const char *folder_name,
						 guint32 flags,
					         CamelException *ex);
CamelFolder *    camel_store_get_inbox          (CamelStore *store,
						 CamelException *ex);
CamelFolder *    camel_store_get_trash          (CamelStore *store,
						 CamelException *ex);

CamelFolderInfo *camel_store_create_folder      (CamelStore *store,
						 const char *parent_name,
						 const char *folder_name,
						 CamelException *ex);
void             camel_store_delete_folder      (CamelStore *store,
						 const char *folder_name,
						 CamelException *ex);
void             camel_store_rename_folder      (CamelStore *store,
						 const char *old_name,
						 const char *new_name,
						 CamelException *ex);

void             camel_store_sync               (CamelStore *store, guint32 flags, CamelException *ex);

CamelFolderInfo *camel_store_get_folder_info    (CamelStore *store,
						 const char *top,
						 guint32 flags,
						 CamelException *ex);
void             camel_store_free_folder_info   (CamelStore *store,
						 CamelFolderInfo *fi);

void             camel_store_free_folder_info_full (CamelStore *store,
						    CamelFolderInfo *fi);
void             camel_store_free_folder_info_nop  (CamelStore *store,
						    CamelFolderInfo *fi);

void             camel_folder_info_free            (CamelFolderInfo *fi);
void             camel_folder_info_build_path      (CamelFolderInfo *fi,
						    char separator);
CamelFolderInfo *camel_folder_info_build           (GPtrArray *folders,
						    const char *namespace,
						    char separator,
						    gboolean short_names);
CamelFolderInfo *camel_folder_info_clone	   (CamelFolderInfo *fi);

gboolean         camel_store_supports_subscriptions   (CamelStore *store);

gboolean         camel_store_folder_subscribed        (CamelStore *store,
						       const char *folder_name);
void             camel_store_subscribe_folder         (CamelStore *store,
						       const char *folder_name,
						       CamelException *ex);
void             camel_store_unsubscribe_folder       (CamelStore *store,
						       const char *folder_name,
						       CamelException *ex);

gboolean	 camel_store_uri_cmp		      (CamelStore *store, const char *uria, const char *urib);

/* utility needed by some stores */
int camel_mkdir_hier (const char *path, mode_t mode);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_STORE_H */
