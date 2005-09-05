/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * camel-folder.h: Abstract class for an email folder
 *
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 *	    Michael Zucchi <notzed@ximian.com>
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

#ifndef CAMEL_FOLDER_H
#define CAMEL_FOLDER_H 1


#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#include <glib.h>
#include <camel/camel-object.h>
#include <camel/camel-folder-summary.h>

#define CAMEL_FOLDER_TYPE     (camel_folder_get_type ())
#define CAMEL_FOLDER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_FOLDER_TYPE, CamelFolder))
#define CAMEL_FOLDER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_FOLDER_TYPE, CamelFolderClass))
#define CAMEL_IS_FOLDER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_FOLDER_TYPE))

enum {
	CAMEL_FOLDER_ARG_FIRST = CAMEL_ARG_FIRST + 0x1000,
	CAMEL_FOLDER_ARG_NAME = CAMEL_FOLDER_ARG_FIRST,
	CAMEL_FOLDER_ARG_FULL_NAME,
	CAMEL_FOLDER_ARG_STORE,
	CAMEL_FOLDER_ARG_PERMANENTFLAGS,
	CAMEL_FOLDER_ARG_TOTAL,
	CAMEL_FOLDER_ARG_UNREAD, /* unread messages */
	CAMEL_FOLDER_ARG_DELETED, /* deleted messages */
	CAMEL_FOLDER_ARG_JUNKED, /* junked messages */
	CAMEL_FOLDER_ARG_VISIBLE, /* visible !(deleted or junked) */
	CAMEL_FOLDER_ARG_UID_ARRAY,
	CAMEL_FOLDER_ARG_INFO_ARRAY,
	CAMEL_FOLDER_ARG_PROPERTIES,
	CAMEL_FOLDER_ARG_URI,	/* uri representing this folder */
	CAMEL_FOLDER_ARG_LAST = CAMEL_ARG_FIRST + 0x2000,
};

enum {
	CAMEL_FOLDER_NAME = CAMEL_FOLDER_ARG_NAME | CAMEL_ARG_STR,
	CAMEL_FOLDER_FULL_NAME = CAMEL_FOLDER_ARG_FULL_NAME | CAMEL_ARG_STR,
	CAMEL_FOLDER_STORE = CAMEL_FOLDER_ARG_STORE | CAMEL_ARG_OBJ,
	CAMEL_FOLDER_PERMANENTFLAGS = CAMEL_FOLDER_ARG_PERMANENTFLAGS | CAMEL_ARG_INT,
	CAMEL_FOLDER_TOTAL = CAMEL_FOLDER_ARG_TOTAL | CAMEL_ARG_INT,
	CAMEL_FOLDER_UNREAD = CAMEL_FOLDER_ARG_UNREAD | CAMEL_ARG_INT,
	CAMEL_FOLDER_DELETED = CAMEL_FOLDER_ARG_DELETED | CAMEL_ARG_INT,
	CAMEL_FOLDER_JUNKED = CAMEL_FOLDER_ARG_JUNKED | CAMEL_ARG_INT,
	CAMEL_FOLDER_VISIBLE = CAMEL_FOLDER_ARG_VISIBLE | CAMEL_ARG_INT,

	CAMEL_FOLDER_UID_ARRAY = CAMEL_FOLDER_ARG_UID_ARRAY | CAMEL_ARG_PTR,
	CAMEL_FOLDER_INFO_ARRAY = CAMEL_FOLDER_ARG_INFO_ARRAY | CAMEL_ARG_PTR,

	CAMEL_FOLDER_URI = CAMEL_FOLDER_ARG_URI | CAMEL_ARG_STR,

	/* GSList of settable folder properties */
	CAMEL_FOLDER_PROPERTIES = CAMEL_FOLDER_ARG_PROPERTIES | CAMEL_ARG_PTR,
};

struct _CamelFolder {
	CamelObject parent_object;

	struct _CamelFolderPrivate *priv;

	/* get these via the :get() method, they might not be set otherwise */
	char *name;
	char *full_name;
	char *description;

	CamelStore *parent_store;
	CamelFolderSummary *summary;

	guint32 folder_flags;
	guint32 permanent_flags;
};

// To be deleted
#define CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY (1<<0)
#define CAMEL_FOLDER_HAS_SEARCH_CAPABILITY  (1<<1)
// ^^
#define CAMEL_FOLDER_FILTER_RECENT          (1<<2)
#define CAMEL_FOLDER_HAS_BEEN_DELETED       (1<<3)
#define CAMEL_FOLDER_IS_TRASH               (1<<4)
#define CAMEL_FOLDER_IS_JUNK                (1<<5)
#define CAMEL_FOLDER_FILTER_JUNK  	    (1<<6)

/* Folder can hold folders */
#define CAMEL_FOLDER_HOLDS_FOLDERS	    (1<<7)
/* Folder can hold messages */
#define CAMEL_FOLDER_HOLDS_MESSAGES	    (1<<8)

typedef struct {
	CamelObjectClass parent_class;

	/* Virtual methods */	
	void   (*refresh_info) (CamelFolder *folder, CamelException *ex);

	void   (*sync) (CamelFolder *folder, gboolean expunge, CamelException *ex);

	void (*append_message)  (CamelFolder *folder, 
				 CamelMimeMessage *message,
				 const CamelMessageInfo *info,
				 char **appended_uid,
				 CamelException *ex);
	
	CamelMimeMessage * (*get_message)(CamelFolder *folder, const char *uid, CamelException *ex);
	CamelIterator *(*search)(CamelFolder *, const char *, const char *, CamelIterator *, CamelException *);
	CamelMessageInfo * (*get_message_info) (CamelFolder *, const char *uid);

	void (*transfer_messages_to) (CamelFolder *source,
				      GPtrArray *uids,
				      CamelFolder *destination,
				      GPtrArray **transferred_uids,
				      gboolean delete_originals,
				      CamelException *ex);
	
	void (*delete)           (CamelFolder *folder);
	void (*rename)           (CamelFolder *folder, const char *newname);
	
	void     (*freeze)    (CamelFolder *folder);
	void     (*thaw)      (CamelFolder *folder);
	gboolean (*is_frozen) (CamelFolder *folder);

	/* new folder interface */
	CamelIterator *(*get_folders)(CamelFolder *folder, const char *pattern, CamelException *ex);

} CamelFolderClass;

/* Standard Camel function */
CamelType camel_folder_get_type (void);


/* public methods */
void               camel_folder_construct              (CamelFolder *folder,
							CamelStore *parent_store,
							const char *full_name,
							const char *name);

void               camel_folder_refresh_info           (CamelFolder *folder, 
							CamelException *ex);
void               camel_folder_sync                   (CamelFolder *folder, 
							gboolean expunge, 
							CamelException *ex);

/* message manipulation */
void               camel_folder_append_message         (CamelFolder *folder, 
							CamelMimeMessage *message,
							const CamelMessageInfo *info,
							char **appended_uid,
							CamelException *ex);

/* uid based access operations */
CamelMimeMessage * camel_folder_get_message           (CamelFolder *folder, 
						       const char *uid, 
						       CamelException *ex);

/* search/iterator api */
CamelIterator *camel_folder_search(CamelFolder *folder, const char *, const char *expr, CamelIterator *, CamelException *ex);

/* summary info */
CamelMessageInfo *camel_folder_get_message_info		(CamelFolder *folder, const char *uid);

void               camel_folder_transfer_messages_to   (CamelFolder *source,
							GPtrArray *uids,
							CamelFolder *dest,
							GPtrArray **transferred_uids,
							gboolean delete_originals,
							CamelException *ex);

void               camel_folder_delete                 (CamelFolder *folder);
void               camel_folder_rename                 (CamelFolder *folder, const char *new);

/* stop/restart getting events */
void               camel_folder_freeze                (CamelFolder *folder);
void               camel_folder_thaw                  (CamelFolder *folder);
gboolean           camel_folder_is_frozen             (CamelFolder *folder);

#if 0
//Potential ideas ...

void camel_folder_subscribe(CamelFolder *folder, int state, CamelException *ex);
void camel_folder_renameX(CamelFolder *, const char *new, CamelException *ex);
void camel_folder_deleteX(CamelFolder *, CamelException *ex);
#endif

CamelIterator *camel_folder_get_folders(CamelFolder *folder, const char *pattern, CamelException *ex);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_FOLDER_H */
