/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-folder.h: Abstract class for an email folder */

/* 
 * Author: 
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright 1999, 2000 Helix Code, Inc. (http://www.helixcode.com)
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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
#endif /* __cplusplus }*/

#include <camel/camel-object.h>
#include <camel/camel-folder-summary.h>

#define CAMEL_FOLDER_TYPE     (camel_folder_get_type ())
#define CAMEL_FOLDER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_FOLDER_TYPE, CamelFolder))
#define CAMEL_FOLDER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_FOLDER_TYPE, CamelFolderClass))
#define CAMEL_IS_FOLDER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_FOLDER_TYPE))

struct _CamelFolder
{
	CamelObject parent_object;

	int frozen;
	gboolean folder_changed;
	GList *messages_changed;

	gchar *name;
	gchar *full_name;
	gchar *separator;
	CamelStore *parent_store;
	CamelFolder *parent_folder;
	guint32 permanent_flags;

	gboolean path_begins_with_sep;
	
	gboolean can_hold_folders:1;
	gboolean can_hold_messages:1;
	gboolean has_summary_capability:1;
	gboolean has_search_capability:1;
};

typedef struct {
	CamelObjectClass parent_class;

	/* signals */
	/* Not anymore! bwahahahah!
	 * void		(*folder_changed)	(CamelFolder *, int type);
	 * void            (*message_changed)      (CamelFolder *,
	 *					 const char *uid);
	 */

	/* Virtual methods */	
	void   (*init) (CamelFolder *folder, CamelStore *parent_store,
			CamelFolder *parent_folder, const gchar *name,
			gchar *separator, gboolean path_begins_with_sep,
			CamelException *ex);

	void   (*sync) (CamelFolder *folder, gboolean expunge, 
			CamelException *ex);

	const gchar *  (*get_name)  (CamelFolder *folder);

	const gchar *  (*get_full_name)  (CamelFolder *folder);

	gboolean   (*can_hold_folders)   (CamelFolder *folder);

	gboolean   (*can_hold_messages)  (CamelFolder *folder);

	CamelFolder *  (*get_subfolder)  (CamelFolder *folder, 
					  const gchar *folder_name, 
					  gboolean create,
					  CamelException *ex);

	CamelFolder *  (*get_parent_folder)   (CamelFolder *folder);

	CamelStore *  (*get_parent_store) (CamelFolder *folder);

	void (*expunge)  (CamelFolder *folder, 
			  CamelException *ex);

	gint   (*get_message_count)   (CamelFolder *folder);

	gint   (*get_unread_message_count) (CamelFolder *folder);

	void (*append_message)  (CamelFolder *folder, 
				 CamelMimeMessage *message,
				 const CamelMessageInfo *info,
				 CamelException *ex);
	
	guint32 (*get_permanent_flags) (CamelFolder *folder);
	guint32 (*get_message_flags)   (CamelFolder *folder,
					const char *uid);
	void    (*set_message_flags)   (CamelFolder *folder,
					const char *uid,
					guint32 flags, guint32 set);

	gboolean (*get_message_user_flag) (CamelFolder *folder,
					   const char *uid,
					   const char *name);
	void     (*set_message_user_flag) (CamelFolder *folder,
					   const char *uid,
					   const char *name,
					   gboolean value);

	CamelMimeMessage * (*get_message)  (CamelFolder *folder, 
					    const gchar *uid, 
					    CamelException *ex);

	void (*delete_message)  (CamelFolder *folder, 
				 const gchar *uid, 
				 CamelException *ex);

	GPtrArray * (*get_uids)       (CamelFolder *folder);
	void (*free_uids)             (CamelFolder *folder,
				       GPtrArray *array);

	GPtrArray * (*get_summary)    (CamelFolder *folder);
	void (*free_summary)          (CamelFolder *folder,
				       GPtrArray *summary);

	GPtrArray * (*get_subfolder_names) (CamelFolder *folder);
	void (*free_subfolder_names)       (CamelFolder *folder,
					    GPtrArray *subfolders);

	gboolean (*has_search_capability) (CamelFolder *folder);

	GPtrArray * (*search_by_expression) (CamelFolder *folder,
					     const char *expression,
					     CamelException *ex);

	void (*search_free) (CamelFolder *folder, GPtrArray *result);

	const CamelMessageInfo * (*get_message_info) (CamelFolder *, const char *uid);

	void (*copy_message_to) (CamelFolder *source,
				 const char *uid,
				 CamelFolder *destination,
				 CamelException *ex);
	
	void (*move_message_to) (CamelFolder *source,
				 const char *uid,
				 CamelFolder *destination,
				 CamelException *ex);

	void (*freeze) (CamelFolder *folder);
	void (*thaw)   (CamelFolder *folder);
} CamelFolderClass;



/* Standard Camel function */
CamelType camel_folder_get_type (void);


/* public methods */



CamelFolder *      camel_folder_get_subfolder          (CamelFolder *folder, 
							const gchar *folder_name, 
							gboolean create,
							CamelException *ex);

void               camel_folder_sync                   (CamelFolder *folder, 
							gboolean expunge, 
							CamelException *ex);

CamelFolder *      camel_folder_get_parent_folder      (CamelFolder *folder);
CamelStore *       camel_folder_get_parent_store       (CamelFolder *folder);


/* delete operations */
void		   camel_folder_expunge                (CamelFolder *folder, 
							CamelException *ex);


/* folder name operations */
const gchar *      camel_folder_get_name               (CamelFolder *folder);
const gchar *      camel_folder_get_full_name          (CamelFolder *folder);


/* various properties accessors */
guint32		   camel_folder_get_permanent_flags    (CamelFolder *folder);

guint32		   camel_folder_get_message_flags      (CamelFolder *folder,
							const char *uid);

void		   camel_folder_set_message_flags      (CamelFolder *folder,
							const char *uid,
							guint32 flags,
							guint32 set);

gboolean	   camel_folder_get_message_user_flag  (CamelFolder *folder,
							const char *uid,
							const char *name);

void		   camel_folder_set_message_user_flag  (CamelFolder *folder,
							const char *uid,
							const char *name,
							gboolean value);



/* message manipulation */
void               camel_folder_append_message         (CamelFolder *folder, 
							CamelMimeMessage *message,
							const CamelMessageInfo *info,
							CamelException *ex);


/* summary related operations */
gboolean           camel_folder_has_summary_capability (CamelFolder *folder);


gint               camel_folder_get_message_count     (CamelFolder *folder);

gint               camel_folder_get_unread_message_count (CamelFolder *folder);

GPtrArray *        camel_folder_get_summary           (CamelFolder *folder);
void               camel_folder_free_summary          (CamelFolder *folder,
						       GPtrArray *array);

GPtrArray *        camel_folder_get_subfolder_names   (CamelFolder *folder);
void               camel_folder_free_subfolder_names  (CamelFolder *folder,
						       GPtrArray *array);


/* uid based access operations */
CamelMimeMessage * camel_folder_get_message           (CamelFolder *folder, 
						       const gchar *uid, 
						       CamelException *ex);
#define camel_folder_delete_message(folder, uid) \
	camel_folder_set_message_flags (folder, uid, CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_DELETED)

GPtrArray *        camel_folder_get_uids              (CamelFolder *folder);
void               camel_folder_free_uids             (CamelFolder *folder,
						       GPtrArray *array);

/* search api */
gboolean           camel_folder_has_search_capability (CamelFolder *folder);
GPtrArray *	   camel_folder_search_by_expression  (CamelFolder *folder,
						       const char *expression,
						       CamelException *ex);
void		   camel_folder_search_free	      (CamelFolder *folder, GPtrArray *);

/* summary info */
const CamelMessageInfo *camel_folder_get_message_info (CamelFolder *summary,
						       const char *uid);

void               camel_folder_copy_message_to       (CamelFolder *source,
						       const char *uid,
						       CamelFolder *dest,
						       CamelException *ex);

void               camel_folder_move_message_to       (CamelFolder *source,
						       const char *uid,
						       CamelFolder *dest,
						       CamelException *ex);

void               camel_folder_freeze                (CamelFolder *folder);
void               camel_folder_thaw                  (CamelFolder *folder);


/* For use by subclasses (for free_{uids,summary,subfolder_names}) */
void camel_folder_free_nop     (CamelFolder *folder, GPtrArray *array);
void camel_folder_free_shallow (CamelFolder *folder, GPtrArray *array);
void camel_folder_free_deep    (CamelFolder *folder, GPtrArray *array);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_FOLDER_H */

