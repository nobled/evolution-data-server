/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-mbox-folder.c : Abstract class for an email folder */

/* 
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com> 
 *          Michael Zucchi <notzed@helixcode.com>
 *
 * Copyright (C) 1999, 2000 Helix Code Inc.
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


#include <config.h> 

#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#include "camel-mbox-folder.h"
#include "camel-mbox-store.h"
#include "string-utils.h"
#include "camel-log.h"
#include "camel-stream-fs.h"
#include "camel-mbox-summary.h"
#include "camel-mbox-parser.h"
#include "camel-mbox-utils.h"
#include "gmime-utils.h"
#include "camel-mbox-search.h"
#include "camel-data-wrapper.h"
#include "camel-mime-message.h"

#include "camel-exception.h"

static CamelFolderClass *parent_class=NULL;

/* Returns the class for a CamelMboxFolder */
#define CMBOXF_CLASS(so) CAMEL_MBOX_FOLDER_CLASS (GTK_OBJECT(so)->klass)
#define CF_CLASS(so) CAMEL_FOLDER_CLASS (GTK_OBJECT(so)->klass)
#define CMBOXS_CLASS(so) CAMEL_STORE_CLASS (GTK_OBJECT(so)->klass)


static void _init (CamelFolder *folder, CamelStore *parent_store,
		   CamelFolder *parent_folder, const gchar *name,
		   gchar separator, CamelException *ex);

static void _open (CamelFolder *folder, CamelFolderOpenMode mode, CamelException *ex);
static void _close (CamelFolder *folder, gboolean expunge, CamelException *ex);
static gboolean _exists (CamelFolder *folder, CamelException *ex);
static gboolean _create(CamelFolder *folder, CamelException *ex);
static gboolean _delete (CamelFolder *folder, gboolean recurse, CamelException *ex);
static gboolean _delete_messages (CamelFolder *folder, CamelException *ex);
static GList *_list_subfolders (CamelFolder *folder, CamelException *ex);
/* static CamelMimeMessage *_get_message_by_number (CamelFolder *folder, gint number, CamelException *ex);*/
static gint _get_message_count (CamelFolder *folder, CamelException *ex);
static void _append_message (CamelFolder *folder, CamelMimeMessage *message, CamelException *ex);
static GList *_get_uid_list  (CamelFolder *folder, CamelException *ex);
static CamelMimeMessage *_get_message_by_uid (CamelFolder *folder, const gchar *uid, CamelException *ex);
#if 0
static void _expunge (CamelFolder *folder, CamelException *ex);
static void _copy_message_to (CamelFolder *folder, CamelMimeMessage *message, CamelFolder *dest_folder, CamelException *ex);
static const gchar *_get_message_uid (CamelFolder *folder, CamelMimeMessage *message, CamelException *ex);
#endif

GPtrArray *summary_get_message_info (CamelFolder *folder, int first, int count);

static void _finalize (GtkObject *object);

static void
camel_mbox_folder_class_init (CamelMboxFolderClass *camel_mbox_folder_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS (camel_mbox_folder_class);
	GtkObjectClass *gtk_object_class = GTK_OBJECT_CLASS (camel_folder_class);

	parent_class = gtk_type_class (camel_folder_get_type ());
		
	/* virtual method definition */

	/* virtual method overload */
	camel_folder_class->init = _init;
	camel_folder_class->open = _open;
	camel_folder_class->close = _close;
	camel_folder_class->exists = _exists;
	camel_folder_class->create = _create;
	camel_folder_class->delete = _delete;
	camel_folder_class->delete_messages = _delete_messages;
	camel_folder_class->list_subfolders = _list_subfolders;
	/* camel_folder_class->get_message_by_number = _get_message_by_number; */
	camel_folder_class->get_message_count = _get_message_count;
	camel_folder_class->append_message = _append_message;
	camel_folder_class->get_uid_list = _get_uid_list;
#if 0
	camel_folder_class->expunge = _expunge;
	camel_folder_class->copy_message_to = _copy_message_to;
	camel_folder_class->get_message_uid = _get_message_uid;
#endif
	camel_folder_class->get_message_by_uid = _get_message_by_uid;

	camel_folder_class->search_by_expression = camel_mbox_folder_search_by_expression;
	camel_folder_class->search_complete = camel_mbox_folder_search_complete;
	camel_folder_class->search_cancel = camel_mbox_folder_search_cancel;

	camel_folder_class->get_message_info = summary_get_message_info;

	gtk_object_class->finalize = _finalize;
	
}

static void           
_finalize (GtkObject *object)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER (object);

	CAMEL_LOG_FULL_DEBUG ("Entering CamelFolder::finalize\n");

	
	g_free (mbox_folder->folder_file_path);
	g_free (mbox_folder->folder_dir_path);

	GTK_OBJECT_CLASS (parent_class)->finalize (object);
	CAMEL_LOG_FULL_DEBUG ("Leaving CamelFolder::finalize\n");
}

GtkType
camel_mbox_folder_get_type (void)
{
	static GtkType camel_mbox_folder_type = 0;
	
	if (!camel_mbox_folder_type)	{
		GtkTypeInfo camel_mbox_folder_info =	
		{
			"CamelMboxFolder",
			sizeof (CamelMboxFolder),
			sizeof (CamelMboxFolderClass),
			(GtkClassInitFunc) camel_mbox_folder_class_init,
			(GtkObjectInitFunc) NULL,
				/* reserved_1 */ NULL,
				/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};
		
		camel_mbox_folder_type = gtk_type_unique (CAMEL_FOLDER_TYPE, &camel_mbox_folder_info);
	}
	
	return camel_mbox_folder_type;
}

static void 
_init (CamelFolder *folder, CamelStore *parent_store,
       CamelFolder *parent_folder, const gchar *name, gchar separator,
       CamelException *ex)
{
	CamelMboxFolder *mbox_folder = (CamelMboxFolder *)folder;
	const gchar *root_dir_path;

	CAMEL_LOG_FULL_DEBUG ("Entering CamelMboxFolder::init_with_store\n");

	/* call parent method */
	parent_class->init (folder, parent_store, parent_folder,
			    name, separator, ex);
	if (camel_exception_get_id (ex))
		return;

	/* we assume that the parent init
	   method checks for the existance of @folder */
	folder->can_hold_messages = TRUE;
	folder->can_hold_folders = TRUE;
	folder->has_summary_capability = TRUE;
	folder->has_uid_capability = TRUE;
	folder->has_search_capability = TRUE;
 	mbox_folder->summary = NULL;

	/* now set the name info */
	g_free (mbox_folder->folder_file_path);
	g_free (mbox_folder->folder_dir_path);
	g_free (mbox_folder->index_file_path);

	root_dir_path = camel_mbox_store_get_toplevel_dir (CAMEL_MBOX_STORE(folder->parent_store));

	CAMEL_LOG_FULL_DEBUG ("CamelMboxFolder::set_name full_name is %s\n", folder->full_name);
	CAMEL_LOG_FULL_DEBUG ("CamelMboxFolder::set_name root_dir_path is %s\n", root_dir_path);

	mbox_folder->folder_file_path = g_strdup_printf ("%s/%s", root_dir_path, folder->full_name);
	mbox_folder->summary_file_path = g_strdup_printf ("%s/%s-ev-summary", root_dir_path, folder->full_name);
	mbox_folder->folder_dir_path = g_strdup_printf ("%s/%s.sdb", root_dir_path, folder->full_name);
	mbox_folder->index_file_path = g_strdup_printf ("%s/%s.ibex", root_dir_path, folder->full_name);

	CAMEL_LOG_FULL_DEBUG ("CamelMboxFolder::set_name mbox_folder->folder_file_path is %s\n", 
			      mbox_folder->folder_file_path);
	CAMEL_LOG_FULL_DEBUG ("CamelMboxFolder::set_name mbox_folder->folder_dir_path is %s\n", 
			      mbox_folder->folder_dir_path);
	CAMEL_LOG_FULL_DEBUG ("Leaving CamelMboxFolder::set_name\n");

	CAMEL_LOG_FULL_DEBUG ("Leaving CamelMboxFolder::init_with_store\n");
}

static void
_open (CamelFolder *folder, CamelFolderOpenMode mode, CamelException *ex)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER (folder);

	/* call parent class */
	parent_class->open (folder, mode, ex);
	if (camel_exception_get_id(ex))
		return;

	mbox_folder->index = ibex_open(mbox_folder->index_file_path, O_CREAT|O_RDWR, 0600);
	if (mbox_folder->index == NULL) {
		g_warning("Could not open/create index file: %s: indexing will not function",
			  strerror(errno));
	}

	mbox_folder->summary = camel_mbox_summary_new(mbox_folder->summary_file_path, mbox_folder->folder_file_path, mbox_folder->index);
	if (mbox_folder->summary == NULL) {
		camel_exception_set (ex, 
				     CAMEL_EXCEPTION_FOLDER_INVALID, /* FIXME: right error code */
				     "Could not create summary");
		return;
	}
	camel_mbox_summary_load(mbox_folder->summary);
}

static void
_close (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER (folder);

	/* call parent implementation */
	parent_class->close (folder, expunge, ex);

	/* save index */
	if (mbox_folder->index) {
		ibex_close(mbox_folder->index);
		mbox_folder->index = NULL;
	}

	camel_mbox_summary_save (mbox_folder->summary);
	camel_mbox_summary_unref (mbox_folder->summary);
	mbox_folder->summary = NULL;
}

/* FIXME: clean up this snot */
static gboolean
_exists (CamelFolder *folder, CamelException *ex)
{
	CamelMboxFolder *mbox_folder;
	struct stat stat_buf;
	gint stat_error;
	gboolean exists;

	g_assert(folder != NULL);

	CAMEL_LOG_FULL_DEBUG ("Entering CamelMboxFolder::exists\n");

	mbox_folder = CAMEL_MBOX_FOLDER (folder);

	/* check if the mbox file path is determined */
	if (!mbox_folder->folder_file_path) {
		camel_exception_set (ex, 
				     CAMEL_EXCEPTION_FOLDER_INVALID,
				     "undetermined folder file path. Maybe use set_name ?");
		return FALSE;
	}

	/* check if the mbox dir path is determined */
	if (!mbox_folder->folder_dir_path) {
		camel_exception_set (ex, 
				     CAMEL_EXCEPTION_FOLDER_INVALID,
				     "undetermined folder directory path. Maybe use set_name ?");
		return FALSE;
	}


	/* we should not check for that here */
#if 0
	/* check if the mbox directory exists */
	access_result = access (mbox_folder->folder_dir_path, F_OK);
	if (access_result < 0) {
		CAMEL_LOG_FULL_DEBUG ("CamelMboxFolder::exists errot when executing access on %s\n", 
				      mbox_folder->folder_dir_path);
		CAMEL_LOG_FULL_DEBUG ("  Full error text is : %s\n", strerror(errno));
		camel_exception_set (ex, 
				     CAMEL_EXCEPTION_SYSTEM,
				     strerror(errno));
		return FALSE;
	}
	stat_error = stat (mbox_folder->folder_dir_path, &stat_buf);
	if (stat_error == -1)  {
		CAMEL_LOG_FULL_DEBUG ("CamelMboxFolder::exists when executing stat on %s, stat_error = %d\n", 
				      mbox_folder->folder_dir_path, stat_error);
		CAMEL_LOG_FULL_DEBUG ("  Full error text is : %s\n", strerror(errno));
		camel_exception_set (ex, 
				     CAMEL_EXCEPTION_SYSTEM,
				     strerror(errno));
		return FALSE;
	}
	exists = S_ISDIR (stat_buf.st_mode);
	if (!exists) return FALSE;
#endif 


	/* check if the mbox file exists */
	stat_error = stat (mbox_folder->folder_file_path, &stat_buf);
	if (stat_error == -1)
		return FALSE;
	
	exists = S_ISREG (stat_buf.st_mode);
	/* we should  check the rights here  */
	
	CAMEL_LOG_FULL_DEBUG ("Leaving CamelMboxFolder::exists\n");
	return exists;
}

/* FIXME: clean up this snot */
static gboolean
_create (CamelFolder *folder, CamelException *ex)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER (folder);
	const gchar *folder_file_path, *folder_dir_path;
	mode_t dir_mode = S_IRWXU;
	gint mkdir_error;
	gboolean folder_already_exists;
	int creat_fd;

	g_assert(folder != NULL);

	/* call default implementation */
	parent_class->create (folder, ex);

	/* get the paths of what we need to create */
	folder_file_path = mbox_folder->folder_file_path;
	folder_dir_path = mbox_folder->folder_dir_path;
	
	if (!(folder_file_path || folder_dir_path)) {
		camel_exception_set (ex, 
				     CAMEL_EXCEPTION_FOLDER_INVALID,
				     "invalid folder path. Use set_name ?");
		return FALSE;
	}

	
	/* if the folder already exists, simply return */
	folder_already_exists = camel_folder_exists (folder,ex);
	if (camel_exception_get_id (ex))
		return FALSE;

	if (folder_already_exists)
		return TRUE;


	/* create the directory for the subfolders */
	mkdir_error = mkdir (folder_dir_path, dir_mode);
	if (mkdir_error == -1)
		goto io_error;
	

	/* create the mbox file */ 
	/* it must be rw for the user and none for the others */
	creat_fd = open (folder_file_path, 
			 O_WRONLY | O_CREAT | O_APPEND,
			 0600);
	if (creat_fd == -1)
		goto io_error;

	close (creat_fd);

	return TRUE;

	/* exception handling for io errors */
	io_error :

		CAMEL_LOG_WARNING ("CamelMboxFolder::create, error when creating %s and %s\n", 
				   folder_dir_path, folder_file_path);
		CAMEL_LOG_FULL_DEBUG ( "  Full error text is : %s\n", strerror(errno));

		if (errno == EACCES) {
			camel_exception_set (ex, 
					     CAMEL_EXCEPTION_FOLDER_INSUFFICIENT_PERMISSION,
					     "You don't have the permission to create the mbox file.");
			return FALSE;
		} else {
			camel_exception_set (ex, 
					     CAMEL_EXCEPTION_SYSTEM,
					     "Unable to create the mbox file.");
			return FALSE;
		}
}


/* FIXME: cleanup */
static gboolean
_delete (CamelFolder *folder, gboolean recurse, CamelException *ex)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER (folder);
	const gchar *folder_file_path, *folder_dir_path;
	gint rmdir_error = 0;
	gint unlink_error = 0;
	gboolean folder_already_exists;

	g_assert(folder != NULL);

	/* check if the folder object exists */

	/* in the case where the folder does not exist, 
	   return immediatly */
	folder_already_exists = camel_folder_exists (folder, ex);
	if (camel_exception_get_id (ex))
		return FALSE;

	if (!folder_already_exists)
		return TRUE;


	/* call default implementation.
	   It should delete the messages in the folder
	   and recurse the operation to subfolders */
	parent_class->delete (folder, recurse, ex);
	

	/* get the paths of what we need to be deleted */
	folder_file_path = mbox_folder->folder_file_path;
	folder_dir_path = mbox_folder->folder_file_path;
	
	if (!(folder_file_path || folder_dir_path)) {
		camel_exception_set (ex, 
				     CAMEL_EXCEPTION_FOLDER_INVALID,
				     "invalid folder path. Use set_name ?");
		return FALSE;
	}

	
	/* physically delete the directory */
	CAMEL_LOG_FULL_DEBUG ("CamelMboxFolder::delete removing directory %s\n", folder_dir_path);
	rmdir_error = rmdir (folder_dir_path);
	if (rmdir_error == -1) 
		switch errno { 
		case EACCES :
			camel_exception_set (ex, 
					     CAMEL_EXCEPTION_FOLDER_INSUFFICIENT_PERMISSION,
					     "Not enough permission to delete the mbox folder");
			return FALSE;			
			break;
			
		case ENOTEMPTY :
				camel_exception_set (ex, 
					     CAMEL_EXCEPTION_FOLDER_NON_EMPTY,
						     "mbox folder not empty. Cannot delete it. Maybe use recurse flag ?");
				return FALSE;		
				break;
		default :
			camel_exception_set (ex, 
					     CAMEL_EXCEPTION_SYSTEM,
					     "Unable to delete the mbox folder.");
			return FALSE;
	}
	
	/* physically delete the file */
	unlink_error = unlink (folder_dir_path);
	if (unlink_error == -1) 
		switch errno { 
		case EACCES :
		case EPERM :
		case EROFS :
			camel_exception_set (ex, 
					     CAMEL_EXCEPTION_FOLDER_INSUFFICIENT_PERMISSION,
					     "Not enough permission to delete the mbox file");
			return FALSE;			
			break;
			
		case EFAULT :
		case ENOENT :
		case ENOTDIR :
		case EISDIR :
			camel_exception_set (ex, 
					     CAMEL_EXCEPTION_FOLDER_INVALID_PATH,
					     "Invalid mbox file");
			return FALSE;			
			break;

		default :
			camel_exception_set (ex, 
					     CAMEL_EXCEPTION_SYSTEM,
					     "Unable to delete the mbox folder.");
			return FALSE;
	}


	return TRUE;
}


/* TODO: remove this */
/* I mean, duh, even if it was to be kept, why not just trunc the file to 0 bytes? */
gboolean
_delete_messages (CamelFolder *folder, CamelException *ex)
{
	
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER (folder);
	const gchar *folder_file_path;
	gboolean folder_already_exists;
	int creat_fd;

	g_assert(folder!=NULL);
	
	/* in the case where the folder does not exist, 
	   return immediatly */
	folder_already_exists = camel_folder_exists (folder, ex);
	if (camel_exception_get_id (ex)) return FALSE;

	if (!folder_already_exists) return TRUE;



	/* get the paths of the mbox file we need to delete */
	folder_file_path = mbox_folder->folder_file_path;
	
	if (!folder_file_path) {
		camel_exception_set (ex, 
				     CAMEL_EXCEPTION_FOLDER_INVALID,
				     "invalid folder path. Use set_name ?");
		return FALSE;
	}

		
	/* create the mbox file */ 
	/* it must be rw for the user and none for the others */
	creat_fd = open (folder_file_path, 
			 O_WRONLY | O_TRUNC,
			 0600); 
	if (creat_fd == -1)
		goto io_error;
	close (creat_fd);
	
	return TRUE;

	/* exception handling for io errors */
	io_error :

		CAMEL_LOG_WARNING ("CamelMboxFolder::create, error when deleting files  %s\n", 
				   folder_file_path);
		CAMEL_LOG_FULL_DEBUG ( "  Full error text is : %s\n", strerror(errno));

		if (errno == EACCES) {
			camel_exception_set (ex, 
					     CAMEL_EXCEPTION_FOLDER_INSUFFICIENT_PERMISSION,
					     "You don't have the permission to write in the mbox file.");
			return FALSE;
		} else {
			camel_exception_set (ex, 
					     CAMEL_EXCEPTION_SYSTEM,
					     "Unable to write in the mbox file.");
			return FALSE;
		}
	

}

/* FIXME: cleanup */
static GList *
_list_subfolders (CamelFolder *folder, CamelException *ex)
{
	GList *subfolder_name_list = NULL;

	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER (folder);
	const gchar *folder_dir_path;
	gboolean folder_exists;

	struct stat stat_buf;
	gint stat_error = 0;
	gchar *entry_name;
	gchar *full_entry_name;
	gchar *real_folder_name;
	struct dirent *dir_entry;
	DIR *dir_handle;
	gboolean folder_suffix_found;
	

	/* check if the folder object exists */
	if (!folder) {
		camel_exception_set (ex, 
				     CAMEL_EXCEPTION_FOLDER_NULL,
				     "folder object is NULL");
		return FALSE;
	}


	/* in the case the folder does not exist, 
	   raise an exception */
	folder_exists = camel_folder_exists (folder, ex);
	if (camel_exception_get_id (ex)) return FALSE;

	if (!folder_exists) {
		camel_exception_set (ex, 
				     CAMEL_EXCEPTION_FOLDER_INVALID,
				     "Inexistant folder.");
		return FALSE;
	}


	/* get the mbox subfolders directories */
	folder_dir_path = mbox_folder->folder_file_path;
	if (!folder_dir_path) {
		camel_exception_set (ex, 
				     CAMEL_EXCEPTION_FOLDER_INVALID,
				     "Invalid folder path. Use set_name ?");
		return FALSE;
	}

		
	dir_handle = opendir (folder_dir_path);
	
	/* read the first entry in the directory */
	dir_entry = readdir (dir_handle);
	while ((stat_error != -1) && (dir_entry != NULL)) {

		/* get the name of the next entry in the dir */
		entry_name = dir_entry->d_name;
		full_entry_name = g_strdup_printf ("%s/%s", folder_dir_path, entry_name);
		stat_error = stat (full_entry_name, &stat_buf);
		g_free (full_entry_name);

		/* is it a directory ? */
		if ((stat_error != -1) && S_ISDIR (stat_buf.st_mode)) {
			/* yes, add it to the list */
			if (entry_name[0] != '.') {
				CAMEL_LOG_FULL_DEBUG ("CamelMboxFolder::list_subfolders adding "
						      "%s\n", entry_name);

				/* if the folder is a netscape folder, remove the  
				   ".sdb" from the name */
				real_folder_name = string_prefix (entry_name, ".sdb", &folder_suffix_found);
				/* stick here the tests for other folder suffixes if any */
				
				if (!folder_suffix_found) real_folder_name = g_strdup (entry_name);
				
				/* add the folder name to the list */
				subfolder_name_list = g_list_append (subfolder_name_list, 
								     real_folder_name);
			}
		}
		/* read next entry */
		dir_entry = readdir (dir_handle);
	}

	closedir (dir_handle);

	return subfolder_name_list;

	

	/* io exception handling */
		switch errno { 
		case EACCES :
			
			camel_exception_setv (ex, 
					      CAMEL_EXCEPTION_FOLDER_INSUFFICIENT_PERMISSION,
					      "Unable to list the directory. Full Error text is : %s ", 
					      strerror (errno));
			break;
			
		case ENOENT :
		case ENOTDIR :
			camel_exception_setv (ex, 
					      CAMEL_EXCEPTION_FOLDER_INVALID_PATH,
					      "Invalid mbox folder path. Full Error text is : %s ", 
					      strerror (errno));
			break;
			
		default :
			camel_exception_set (ex, 
					     CAMEL_EXCEPTION_SYSTEM,
					     "Unable to delete the mbox folder.");
			
		}
	
	g_list_free (subfolder_name_list);
	return NULL;
}

static gint
_get_message_count (CamelFolder *folder, CamelException *ex)
{
	CamelMboxFolder *mbox_folder = (CamelMboxFolder *)folder;

	g_assert (folder);
	g_assert (mbox_folder->summary);
	
	return camel_mbox_summary_message_count(mbox_folder->summary);
}


/* FIXME: this may need some tweaking for performance? */
static void
_append_message (CamelFolder *folder, CamelMimeMessage *message, CamelException *ex)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER (folder);
	CamelStream *output_stream;
	struct stat st;
	off_t seek;

	CAMEL_LOG_FULL_DEBUG ("Entering CamelMboxFolder::append_message\n");

	if (stat(mbox_folder->folder_file_path, &st) != 0) {
		camel_exception_setv (ex, 
				      CAMEL_EXCEPTION_FOLDER_INSUFFICIENT_PERMISSION, /* FIXME: what code? */
				      "Cannot append to mbox file: %s", strerror (errno));
		return;
	}

	output_stream = camel_stream_fs_new_with_name (mbox_folder->folder_file_path, CAMEL_STREAM_FS_WRITE);
	if (output_stream == NULL) {
		camel_exception_setv (ex, 
				      CAMEL_EXCEPTION_FOLDER_INSUFFICIENT_PERMISSION, /* FIXME: what code? */
				      "Cannot append to mbox file: %s", strerror (errno));
		return;
	}

	seek = camel_seekable_stream_seek((CamelSeekableStream *)output_stream, st.st_size, SEEK_SET);
	if (seek != st.st_size) {
		camel_exception_setv (ex, 
				      CAMEL_EXCEPTION_FOLDER_INSUFFICIENT_PERMISSION, /* FIXME: what code? */
				      "Cannot seek to position in mbox file: %s", strerror (errno));
		gtk_object_unref ((GtkObject *)output_stream);
		return;
	}

	camel_stream_write_string (output_stream, "From - \n");
	/* FIXME: does this return an error?   IT HAS TO FOR THIS TO BE RELIABLE */
	camel_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (message), output_stream);
	camel_stream_close (output_stream);

	/* TODO: update the summary so it knows a new message is there to summarise/index */
	/* This is only a performance improvement, the summary is *only* a cache */

	gtk_object_unref (GTK_OBJECT (output_stream));

	CAMEL_LOG_FULL_DEBUG ("Leaving CamelMboxFolder::append_message\n");
}




static GList *
_get_uid_list (CamelFolder *folder, CamelException *ex) 
{
	GList *uid_list = NULL;
	CamelMboxFolder *mbox_folder = (CamelMboxFolder *)folder;
	int i, count;

	CAMEL_LOG_FULL_DEBUG ("Entering CamelMboxFolder::get_uid_list\n");

	/* FIXME: how are these allocated strings ever free'd? */
	count = camel_mbox_summary_message_count(mbox_folder->summary);
	for (i=0;i<count;i++) {
		CamelMboxMessageInfo *info = camel_mbox_summary_index(mbox_folder->summary, i);
		uid_list = g_list_prepend(uid_list, g_strdup(info->info.uid));
	}
	
	CAMEL_LOG_FULL_DEBUG ("Leaving CamelMboxFolder::get_uid_list\n");
	
	return uid_list;
}

static CamelMimeMessage *
_get_message_by_uid (CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER (folder);
	CamelStream *message_stream;
	CamelMimeMessage *message = NULL;
	CamelStore *parent_store;
	CamelMboxMessageInfo *info;

	CAMEL_LOG_FULL_DEBUG ("Entering CamelMboxFolder::get_message_by_uid\n");

	/* get the parent store */
	parent_store = camel_folder_get_parent_store (folder, ex);
	if (camel_exception_get_id (ex)) {
		return NULL;
	}

	/* get the message summary info */
	info = camel_mbox_summary_uid(mbox_folder->summary, uid);

	if (info == NULL) {
		camel_exception_setv (ex, 
				     CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				     "uid %s not found in the folder",
				      uid);
		return NULL;
	}

	/* if this has no content, its an error in the library */
	g_assert(info->info.content);

	/* FIXME: more checks below */
        /* create a stream bound to the message position/size */
	message_stream = camel_stream_fs_new_with_name_and_bounds (mbox_folder->folder_file_path, 
								   CAMEL_STREAM_FS_READ,
								   ((CamelMboxMessageContentInfo *)info->info.content)->pos,
								   ((CamelMboxMessageContentInfo *)info->info.content)->endpos);
	message = camel_mime_message_new_with_session (camel_service_get_session (CAMEL_SERVICE (parent_store)));
	camel_data_wrapper_set_input_stream (CAMEL_DATA_WRAPPER (message), message_stream);
	
	CAMEL_LOG_FULL_DEBUG ("Leaving CamelMboxFolder::get_uid_list\n");	
	return message;
}

/* get message info for a range of messages */
GPtrArray *summary_get_message_info (CamelFolder *folder, int first, int count)
{
	GPtrArray *array = g_ptr_array_new();
	int i, maxcount;
	CamelMboxFolder *mbox_folder = (CamelMboxFolder *)folder;

        maxcount = camel_mbox_summary_message_count(mbox_folder->summary);
	maxcount = MAX(count, maxcount);
	for (i=first;i<maxcount;i++)
		g_ptr_array_add(array, g_ptr_array_index(mbox_folder->summary->messages, i));

	return array;
}
