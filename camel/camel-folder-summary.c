/*
 *  Copyright (C) 2000 Helix Code Inc.
 *
 *  Authors: Michael Zucchi <notzed@helixcode.com>
 *
 *  This program is free software; you can redistribute it and/or 
 *  modify it under the terms of the GNU General Public License as 
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 *  USA
 */

#include <unistd.h>
#include <netinet/in.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "camel-folder-summary.h"

#include <camel/camel-mime-message.h>
#include <camel/camel-multipart.h>

#include <camel/camel-mime-filter.h>
#include <camel/camel-mime-filter-index.h>
#include <camel/camel-mime-filter-charset.h>
#include <camel/camel-mime-filter-save.h>
#include <camel/camel-mime-filter-basic.h>
#include <camel/camel-mime-message.h>
#include <camel/camel-stream-mem.h>

#include "hash-table-utils.h"
#include "e-util/md5-utils.h"
#include "e-util/e-memory.h"

#include "camel-private.h"

#ifdef ENABLE_THREADS
#include <pthread.h>

static pthread_mutex_t info_lock = PTHREAD_MUTEX_INITIALIZER;

/* this lock is ONLY for the standalone messageinfo stuff */
#define GLOBAL_INFO_LOCK(i) pthread_mutex_lock(&info_lock)
#define GLOBAL_INFO_UNLOCK(i) pthread_mutex_unlock(&info_lock)
#else
#define GLOBAL_INFO_LOCK(i) 
#define GLOBAL_INFO_UNLOCK(i) 
#endif

/* this should probably be conditional on it existing */
#define USE_BSEARCH

#define d(x)
#define io(x)			/* io debug */

#if 0
extern int strdup_count, malloc_count, free_count;
#endif

#define CAMEL_FOLDER_SUMMARY_VERSION (11)

#define _PRIVATE(o) (((CamelFolderSummary *)(o))->priv)

/* trivial lists, just because ... */
struct _node {
	struct _node *next;
};

static struct _node *my_list_append(struct _node **list, struct _node *n);
static int my_list_size(struct _node **list);

static int summary_header_load(CamelFolderSummary *, FILE *);
static int summary_header_save(CamelFolderSummary *, FILE *);

static CamelMessageInfo * message_info_new(CamelFolderSummary *, struct _header_raw *);
static CamelMessageInfo * message_info_new_from_parser(CamelFolderSummary *, CamelMimeParser *);
static CamelMessageInfo * message_info_new_from_message(CamelFolderSummary *s, CamelMimeMessage *msg);
static CamelMessageInfo * message_info_load(CamelFolderSummary *, FILE *);
static int		  message_info_save(CamelFolderSummary *, FILE *, CamelMessageInfo *);
static void		  message_info_free(CamelFolderSummary *, CamelMessageInfo *);

static CamelMessageContentInfo * content_info_new(CamelFolderSummary *, struct _header_raw *);
static CamelMessageContentInfo * content_info_new_from_parser(CamelFolderSummary *, CamelMimeParser *);
static CamelMessageContentInfo * content_info_new_from_message(CamelFolderSummary *s, CamelMimePart *mp);
static CamelMessageContentInfo * content_info_load(CamelFolderSummary *, FILE *);
static int		         content_info_save(CamelFolderSummary *, FILE *, CamelMessageContentInfo *);
static void		         content_info_free(CamelFolderSummary *, CamelMessageContentInfo *);

static char *next_uid_string(CamelFolderSummary *s);

static CamelMessageContentInfo * summary_build_content_info(CamelFolderSummary *s, CamelMessageInfo *msginfo, CamelMimeParser *mp);
static CamelMessageContentInfo * summary_build_content_info_message(CamelFolderSummary *s, CamelMessageInfo *msginfo, CamelMimePart *object);

static void camel_folder_summary_class_init (CamelFolderSummaryClass *klass);
static void camel_folder_summary_init       (CamelFolderSummary *obj);
static void camel_folder_summary_finalize   (CamelObject *obj);

static CamelObjectClass *camel_folder_summary_parent;

static void
camel_folder_summary_class_init (CamelFolderSummaryClass *klass)
{
	camel_folder_summary_parent = camel_type_get_global_classfuncs (camel_object_get_type ());

	klass->summary_header_load = summary_header_load;
	klass->summary_header_save = summary_header_save;

	klass->message_info_new  = message_info_new;
	klass->message_info_new_from_parser = message_info_new_from_parser;
	klass->message_info_new_from_message = message_info_new_from_message;
	klass->message_info_load = message_info_load;
	klass->message_info_save = message_info_save;
	klass->message_info_free = message_info_free;

	klass->content_info_new  = content_info_new;
	klass->content_info_new_from_parser = content_info_new_from_parser;
	klass->content_info_new_from_message = content_info_new_from_message;
	klass->content_info_load = content_info_load;
	klass->content_info_save = content_info_save;
	klass->content_info_free = content_info_free;

	klass->next_uid_string = next_uid_string;
}

static void
camel_folder_summary_init (CamelFolderSummary *s)
{
	struct _CamelFolderSummaryPrivate *p;

	p = _PRIVATE(s) = g_malloc0(sizeof(*p));

	p->filter_charset = g_hash_table_new(g_strcase_hash, g_strcase_equal);

	s->message_info_size = sizeof(CamelMessageInfo);
	s->content_info_size = sizeof(CamelMessageContentInfo);

	s->message_info_chunks = NULL;
	s->content_info_chunks = NULL;

#ifdef DOESTRV
	s->message_info_strings = CAMEL_MESSAGE_INFO_LAST;
#endif

	s->version = CAMEL_FOLDER_SUMMARY_VERSION;
	s->flags = 0;
	s->time = 0;
	s->nextuid = 1;

	s->messages = g_ptr_array_new();
	s->messages_uid = g_hash_table_new(g_str_hash, g_str_equal);

#ifdef ENABLE_THREADS
	p->summary_lock = g_mutex_new();
	p->io_lock = g_mutex_new();
	p->filter_lock = g_mutex_new();
	p->alloc_lock = g_mutex_new();
	p->ref_lock = g_mutex_new();
#endif
}

static void free_o_name(void *key, void *value, void *data)
{
	camel_object_unref((CamelObject *)value);
	g_free(key);
}

static void
camel_folder_summary_finalize (CamelObject *obj)
{
	struct _CamelFolderSummaryPrivate *p;
	CamelFolderSummary *s = (CamelFolderSummary *)obj;

	p = _PRIVATE(obj);

	camel_folder_summary_clear(s);
	g_ptr_array_free(s->messages, TRUE);
	g_hash_table_destroy(s->messages_uid);

	g_hash_table_foreach(p->filter_charset, free_o_name, 0);
	g_hash_table_destroy(p->filter_charset);

	g_free(s->summary_path);

	if (s->message_info_chunks)
		e_memchunk_destroy(s->message_info_chunks);
	if (s->content_info_chunks)
		e_memchunk_destroy(s->content_info_chunks);

	if (p->filter_index)
		camel_object_unref ((CamelObject *)p->filter_index);
	if (p->filter_64)
		camel_object_unref ((CamelObject *)p->filter_64);
	if (p->filter_qp)
		camel_object_unref ((CamelObject *)p->filter_qp);
	if (p->filter_save)
		camel_object_unref ((CamelObject *)p->filter_save);

#ifdef ENABLE_THREADS
	g_mutex_free(p->summary_lock);
	g_mutex_free(p->io_lock);
	g_mutex_free(p->filter_lock);
	g_mutex_free(p->alloc_lock);
	g_mutex_free(p->ref_lock);
#endif

	g_free(p);
}

CamelType
camel_folder_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_object_get_type (), "CamelFolderSummary",
					    sizeof (CamelFolderSummary),
					    sizeof (CamelFolderSummaryClass),
					    (CamelObjectClassInitFunc) camel_folder_summary_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_folder_summary_init,
					    (CamelObjectFinalizeFunc) camel_folder_summary_finalize);
	}
	
	return type;
}

/**
 * camel_folder_summary_new:
 *
 * Create a new CamelFolderSummary object.
 * 
 * Return value: A new CamelFolderSummary widget.
 **/
CamelFolderSummary *
camel_folder_summary_new (void)
{
	CamelFolderSummary *new = CAMEL_FOLDER_SUMMARY ( camel_object_new (camel_folder_summary_get_type ()));	return new;
}


/**
 * camel_folder_summary_set_filename:
 * @s: 
 * @name: 
 * 
 * Set the filename where the summary will be loaded to/saved from.
 **/
void camel_folder_summary_set_filename(CamelFolderSummary *s, const char *name)
{
	CAMEL_SUMMARY_LOCK(s, summary_lock);

	g_free(s->summary_path);
	s->summary_path = g_strdup(name);

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);
}

/**
 * camel_folder_summary_set_index:
 * @s: 
 * @index: 
 * 
 * Set the index used to index body content.  If the index is NULL, or
 * not set (the default), no indexing of body content will take place.
 *
 * Unlike earlier behaviour, build_content need not be set to perform indexing.
 **/
void camel_folder_summary_set_index(CamelFolderSummary *s, ibex *index)
{
	struct _CamelFolderSummaryPrivate *p = _PRIVATE(s);

	p->index = index;
}

/**
 * camel_folder_summary_set_build_content:
 * @s: 
 * @state: 
 * 
 * Set a flag to tell the summary to build the content info summary
 * (CamelMessageInfo.content).  The default is not to build content info
 * summaries.
 **/
void camel_folder_summary_set_build_content(CamelFolderSummary *s, gboolean state)
{
	s->build_content = state;
}

/**
 * camel_folder_summary_count:
 * @s: 
 * 
 * Get the number of summary items stored in this summary.
 * 
 * Return value: The number of items int he summary.
 **/
int
camel_folder_summary_count(CamelFolderSummary *s)
{
	return s->messages->len;
}

/**
 * camel_folder_summary_index:
 * @s: 
 * @i: 
 * 
 * Retrieve a summary item by index number.
 *
 * A referenced to the summary item is returned, which may be
 * ref'd or free'd as appropriate.
 * 
 * Return value: The summary item, or NULL if the index @i is out
 * of range.
 * It must be freed using camel_folder_summary_info_free().
 **/
CamelMessageInfo *
camel_folder_summary_index(CamelFolderSummary *s, int i)
{
	CamelMessageInfo *info = NULL;

	CAMEL_SUMMARY_LOCK(s, ref_lock);
	CAMEL_SUMMARY_LOCK(s, summary_lock);

	if (i<s->messages->len)
		info = g_ptr_array_index(s->messages, i);

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

	if (info)
		info->refcount++;

	CAMEL_SUMMARY_UNLOCK(s, ref_lock);

	return info;
}

/**
 * camel_folder_summary_uid:
 * @s: 
 * @uid: 
 * 
 * Retrieve a summary item by uid.
 *
 * A referenced to the summary item is returned, which may be
 * ref'd or free'd as appropriate.
 * 
 * Return value: The summary item, or NULL if the uid @uid
 * is not available.
 * It must be freed using camel_folder_summary_info_free().
 **/
CamelMessageInfo *
camel_folder_summary_uid(CamelFolderSummary *s, const char *uid)
{
	CamelMessageInfo *info;

	CAMEL_SUMMARY_LOCK(s, ref_lock);
	CAMEL_SUMMARY_LOCK(s, summary_lock);

	info = g_hash_table_lookup(s->messages_uid, uid);

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

	if (info)
		info->refcount++;

	CAMEL_SUMMARY_UNLOCK(s, ref_lock);

	return info;
}

/**
 * camel_folder_summary_next_uid:
 * @s: 
 * 
 * Generate a new unique uid value as an integer.  This
 * may be used to create a unique sequence of numbers.
 * 
 * Return value: The next unique uid value.
 **/
guint32 camel_folder_summary_next_uid(CamelFolderSummary *s)
{
	guint32 uid;


	CAMEL_SUMMARY_LOCK(s, summary_lock);

	uid = s->nextuid++;

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

	/* FIXME: sync this to disk */
/*	summary_header_save(s);*/
	return uid;
}

/**
 * camel_folder_summary_set_uid:
 * @s: 
 * @uid: The next minimum uid to assign.  To avoid clashing
 * uid's, set this to the uid of a given messages + 1.
 * 
 * Set the next minimum uid available.  This can be used to
 * ensure new uid's do not clash with existing uid's.
 **/
void camel_folder_summary_set_uid(CamelFolderSummary *s, guint32 uid)
{
	/* TODO: sync to disk? */
	CAMEL_SUMMARY_LOCK(s, summary_lock);

	s->nextuid = MAX(s->nextuid, uid);

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);
}

/**
 * camel_folder_summary_next_uid_string:
 * @s: 
 * 
 * Retrieve the next uid, but as a formatted string.
 * 
 * Return value: The next uid as an unsigned integer string.
 * This string must be freed by the caller.
 **/
char *
camel_folder_summary_next_uid_string(CamelFolderSummary *s)
{
	return ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->next_uid_string(s);
}

/* loads the content descriptions, recursively */
static CamelMessageContentInfo *
perform_content_info_load(CamelFolderSummary *s, FILE *in)
{
	int i;
	guint32 count;
	CamelMessageContentInfo *ci, *part;

	ci = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->content_info_load(s, in);
	camel_folder_summary_decode_uint32(in, &count);
	for (i=0;i<count;i++) {
		part = perform_content_info_load(s, in);
		if (part) {
			my_list_append((struct _node **)&ci->childs, (struct _node *)part);
			part->parent = ci;
		} else {
			g_warning("Summary file format messed up?");
		}
	}
	return ci;
}

int
camel_folder_summary_load(CamelFolderSummary *s)
{
	FILE *in;
	int i;
	CamelMessageInfo *mi;

	g_assert(s->summary_path);

	in = fopen(s->summary_path, "r");
	if ( in == NULL ) {
		return -1;
	}

	CAMEL_SUMMARY_LOCK(s, io_lock);
	if ( ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->summary_header_load(s, in) == -1) {
		fclose(in);
		CAMEL_SUMMARY_UNLOCK(s, io_lock);
		return -1;
	}

	/* now read in each message ... */
	/* FIXME: check returns */
	for (i=0;i<s->saved_count;i++) {
		mi = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_load(s, in);

		if (s->build_content) {
			mi->content = perform_content_info_load(s, in);
		}

		camel_folder_summary_add(s, mi);
	}

	CAMEL_SUMMARY_UNLOCK(s, io_lock);
	
	if (fclose(in) == -1)
		return -1;

	s->flags &= ~CAMEL_SUMMARY_DIRTY;

	return 0;
}

/* saves the content descriptions, recursively */
static int
perform_content_info_save(CamelFolderSummary *s, FILE *out, CamelMessageContentInfo *ci)
{
	CamelMessageContentInfo *part;

	((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->content_info_save(s, out, ci);
	camel_folder_summary_encode_uint32(out, my_list_size((struct _node **)&ci->childs));
	part = ci->childs;
	while (part) {
		perform_content_info_save(s, out, part);
		part = part->next;
	}
	return 0;
}

/**
 * camel_folder_summary_save:
 * @s: 
 * 
 * Writes the summary to disk.  The summary is only written if changes
 * have occured.
 * 
 * Return value: Returns -1 on error.
 **/
int
camel_folder_summary_save(CamelFolderSummary *s)
{
	FILE *out;
	int fd;
	int i;
	guint32 count;
	CamelMessageInfo *mi;

	g_assert(s->summary_path);

	if ((s->flags & CAMEL_SUMMARY_DIRTY) == 0)
		return 0;

	fd = open(s->summary_path, O_RDWR|O_CREAT, 0600);
	if (fd == -1)
		return -1;
	out = fdopen(fd, "w");
	if ( out == NULL ) {
		close(fd);
		return -1;
	}

	io(printf("saving header\n"));

	CAMEL_SUMMARY_LOCK(s, io_lock);

	if ( ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->summary_header_save(s, out) == -1) {
		fclose(out);
		CAMEL_SUMMARY_UNLOCK(s, io_lock);
		return -1;
	}

	/* now write out each message ... */
	/* FIXME: check returns */

	count = s->messages->len;
	for (i=0;i<count;i++) {
		mi = s->messages->pdata[i];
		((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_save(s, out, mi);

		if (s->build_content) {
			perform_content_info_save(s, out, mi->content);
		}
	}

	CAMEL_SUMMARY_UNLOCK(s, io_lock);

	if (fclose(out) == -1)
		return -1;

	s->flags &= ~CAMEL_SUMMARY_DIRTY;
	return 0;
}

static void
summary_assign_uid(CamelFolderSummary *s, CamelMessageInfo *info)
{
	const char *uid;

	uid = camel_message_info_uid(info);
	if (uid == NULL || uid[0] == 0) {
		camel_message_info_set_uid(info, camel_folder_summary_next_uid_string(s));
		uid = camel_message_info_uid(info);
	}

	CAMEL_SUMMARY_LOCK(s, summary_lock);

	while (g_hash_table_lookup(s->messages_uid, uid)) {
		g_warning("Trying to insert message with clashing uid (%s).  new uid re-assigned", camel_message_info_uid(info));
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);
		camel_message_info_set_uid(info, camel_folder_summary_next_uid_string(s));
		uid = camel_message_info_uid(info);
		info->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
		CAMEL_SUMMARY_LOCK(s, summary_lock);
	}

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);
}

/**
 * camel_folder_summary_add:
 * @s: 
 * @info: 
 * 
 * Adds a new @info record to the summary.  If @info->uid is NULL, then a new
 * uid is automatically re-assigned by calling :next_uid_string().
 *
 * The @info record should have been generated by calling one of the
 * info_new_*() functions, as it will be free'd based on the summary
 * class.  And MUST NOT be allocated directly using malloc.
 **/
void camel_folder_summary_add(CamelFolderSummary *s, CamelMessageInfo *info)
{
	if (info == NULL)
		return;

	summary_assign_uid(s, info);

	CAMEL_SUMMARY_LOCK(s, summary_lock);

#ifdef DOESTRV
	/* this is vitally important, and also if this is ever modified, then
	   the hash table needs to be resynced */
	info->strings = e_strv_pack(info->strings);
#endif

	g_ptr_array_add(s->messages, info);
	g_hash_table_insert(s->messages_uid, (char *)camel_message_info_uid(info), info);
	s->flags |= CAMEL_SUMMARY_DIRTY;

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);
}

/**
 * camel_folder_summary_add_from_header:
 * @s: 
 * @h: 
 * 
 * Build a new info record based on a set of headers, and add it to the
 * summary.
 *
 * Note that this function should not be used if build_content_info has
 * been specified for this summary.
 * 
 * Return value: The newly added record.
 **/
CamelMessageInfo *camel_folder_summary_add_from_header(CamelFolderSummary *s, struct _header_raw *h)
{
	CamelMessageInfo *info = camel_folder_summary_info_new_from_header(s, h);

	camel_folder_summary_add(s, info);

	return info;
}

/**
 * camel_folder_summary_add_from_parser:
 * @s: 
 * @mp: 
 * 
 * Build a new info record based on the current position of a CamelMimeParser.
 *
 * The parser should be positioned before the start of the message to summarise.
 * This function may be used if build_contnet_info or an index has been
 * specified for the summary.
 * 
 * Return value: The newly added record.
 **/
CamelMessageInfo *camel_folder_summary_add_from_parser(CamelFolderSummary *s, CamelMimeParser *mp)
{
	CamelMessageInfo *info = camel_folder_summary_info_new_from_parser(s, mp);

	camel_folder_summary_add(s, info);

	return info;
}

/**
 * camel_folder_summary_add_from_message:
 * @s: 
 * @msg: 
 * 
 * Add a summary item from an existing message.
 * 
 * Return value: 
 **/
CamelMessageInfo *camel_folder_summary_add_from_message(CamelFolderSummary *s, CamelMimeMessage *msg)
{
	CamelMessageInfo *info = camel_folder_summary_info_new_from_message(s, msg);

	camel_folder_summary_add(s, info);

	return info;
}

/**
 * camel_folder_summary_info_new_from_header:
 * @s: 
 * @h: 
 * 
 * Create a new info record from a header.
 * 
 * Return value: Guess?  This info record MUST be freed using
 * camel_folder_summary_info_free(), camel_message_info_free() will not work.
 **/
CamelMessageInfo *camel_folder_summary_info_new_from_header(CamelFolderSummary *s, struct _header_raw *h)
{
	return ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s))) -> message_info_new(s, h);
}

/**
 * camel_folder_summary_info_new_from_parser:
 * @s: 
 * @mp: 
 * 
 * Create a new info record from a parser.  If the parser cannot
 * determine a uid, then a new one is automatically assigned.
 *
 * If indexing is enabled, then the content will be indexed based
 * on this new uid.  In this case, the message info MUST be
 * added using :add().
 *
 * Once complete, the parser will be positioned at the end of
 * the message.
 *
 * Return value: Guess?  This info record MUST be freed using
 * camel_folder_summary_info_free(), camel_message_info_free() will not work.
 **/
CamelMessageInfo *camel_folder_summary_info_new_from_parser(CamelFolderSummary *s, CamelMimeParser *mp)
{
	CamelMessageInfo *info = NULL;
	char *buffer;
	int len;
	struct _CamelFolderSummaryPrivate *p = _PRIVATE(s);
	off_t start;

	/* should this check the parser is in the right state, or assume it is?? */

	start = camel_mime_parser_tell(mp);
	if (camel_mime_parser_step(mp, &buffer, &len) != HSCAN_EOF) {
		info = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_new_from_parser(s, mp);

		camel_mime_parser_unstep(mp);

		/* assign a unique uid, this is slightly 'wrong' as we do not really
		 * know if we are going to store this in the summary, but no matter */
		summary_assign_uid(s, info);

		CAMEL_SUMMARY_LOCK(s, filter_lock);

		if (p->index) {
			if (p->filter_index == NULL)
				p->filter_index = camel_mime_filter_index_new_ibex(p->index);
			camel_mime_filter_index_set_name(p->filter_index, (char *)camel_message_info_uid(info));
			ibex_unindex(p->index, (char *)camel_message_info_uid(info));
		}

		/* always scan the content info, even if we dont save it */
		info->content = summary_build_content_info(s, info, mp);

		CAMEL_SUMMARY_UNLOCK(s, filter_lock);

		info->size = camel_mime_parser_tell(mp) - start;
	}
	return info;
}

/**
 * camel_folder_summary_info_new_from_message:
 * @: 
 * @: 
 * 
 * Create a summary item from a message.
 * 
 * Return value: 
 **/
CamelMessageInfo *camel_folder_summary_info_new_from_message(CamelFolderSummary *s, CamelMimeMessage *msg)
{
	CamelMessageInfo *info;
	struct _CamelFolderSummaryPrivate *p = _PRIVATE(s);

	info = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_new_from_message(s, msg);

	/* assign a unique uid, this is slightly 'wrong' as we do not really
	 * know if we are going to store this in the summary, but no matter */
	summary_assign_uid(s, info);

	if (p->index)
		ibex_unindex(p->index, (char *)camel_message_info_uid(info));

	info->content = summary_build_content_info_message(s, info, (CamelMimePart *)msg);
	/* FIXME: calculate the size as part of build_content_info_message */
	/* info->size = ... */

	return info;
}

static void
perform_content_info_free(CamelFolderSummary *s, CamelMessageContentInfo *ci)
{
	CamelMessageContentInfo *pw, *pn;

	pw = ci->childs;
	((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->content_info_free(s, ci);
	while (pw) {
		pn = pw->next;
		perform_content_info_free(s, pw);
		pw = pn;
	}
}

/**
 * camel_folder_summary_info_free:
 * @s: 
 * @mi: 
 * 
 * Unref and potentially free the message info @mi, and all associated memory.
 **/
void camel_folder_summary_info_free(CamelFolderSummary *s, CamelMessageInfo *mi)
{
	CamelMessageContentInfo *ci;

	g_assert(mi);
	g_assert(s);

	CAMEL_SUMMARY_LOCK(s, ref_lock);

	g_assert(mi->refcount >= 1);

	mi->refcount--;
	if (mi->refcount > 0) {
		CAMEL_SUMMARY_UNLOCK(s, ref_lock);
		return;
	}

	CAMEL_SUMMARY_UNLOCK(s, ref_lock);

	ci = mi->content;

	((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_free(s, mi);		
	if (s->build_content && ci) {
		perform_content_info_free(s, ci);
	}
}

/**
 * camel_folder_summary_info_ref:
 * @s: 
 * @mi: 
 * 
 * Add an extra reference to @mi.
 **/
void camel_folder_summary_info_ref(CamelFolderSummary *s, CamelMessageInfo *mi)
{
	g_assert(mi);
	g_assert(s);

	CAMEL_SUMMARY_LOCK(s, ref_lock);
	g_assert(mi->refcount >= 1);
	mi->refcount++;
	CAMEL_SUMMARY_UNLOCK(s, ref_lock);
}

/**
 * camel_folder_summary_touch:
 * @s: 
 * 
 * Mark the summary as changed, so that a save will save it.
 **/
void
camel_folder_summary_touch(CamelFolderSummary *s)
{
	CAMEL_SUMMARY_LOCK(s, summary_lock);
	s->flags |= CAMEL_SUMMARY_DIRTY;
	CAMEL_SUMMARY_UNLOCK(s, summary_lock);
}

/**
 * camel_folder_summary_clear:
 * @s: 
 * 
 * Empty the summary contents.
 **/
void
camel_folder_summary_clear(CamelFolderSummary *s)
{
	int i;

	CAMEL_SUMMARY_LOCK(s, summary_lock);
	if (camel_folder_summary_count(s) == 0) {
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);
		return;
	}

	for (i=0;i<s->messages->len;i++)
		camel_folder_summary_info_free(s, s->messages->pdata[i]);

	g_ptr_array_set_size(s->messages, 0);
	g_hash_table_destroy(s->messages_uid);
	s->messages_uid = g_hash_table_new(g_str_hash, g_str_equal);
	s->flags |= CAMEL_SUMMARY_DIRTY;
	CAMEL_SUMMARY_UNLOCK(s, summary_lock);
}

/**
 * camel_folder_summary_remove:
 * @s: 
 * @info: 
 * 
 * Remove a specific @info record from the summary.
 **/
void camel_folder_summary_remove(CamelFolderSummary *s, CamelMessageInfo *info)
{
	CAMEL_SUMMARY_LOCK(s, summary_lock);
	g_hash_table_remove(s->messages_uid, camel_message_info_uid(info));
	g_ptr_array_remove(s->messages, info);
	s->flags |= CAMEL_SUMMARY_DIRTY;
	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

	camel_folder_summary_info_free(s, info);
}

/**
 * camel_folder_summary_remove_uid:
 * @s: 
 * @uid: 
 * 
 * Remove a specific info record from the summary, by @uid.
 **/
void camel_folder_summary_remove_uid(CamelFolderSummary *s, const char *uid)
{
        CamelMessageInfo *oldinfo;
        char *olduid;

	CAMEL_SUMMARY_LOCK(s, ref_lock);
	CAMEL_SUMMARY_LOCK(s, summary_lock);
        if (g_hash_table_lookup_extended(s->messages_uid, uid, (void *)&olduid, (void *)&oldinfo)) {
		/* make sure it doesn't vanish while we're removing it */
		oldinfo->refcount++;
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);
		CAMEL_SUMMARY_UNLOCK(s, ref_lock);
		camel_folder_summary_remove(s, oldinfo);
		camel_folder_summary_info_free(s, oldinfo);
        } else {
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);
		CAMEL_SUMMARY_UNLOCK(s, ref_lock);
	}
}

/**
 * camel_folder_summary_remove_index:
 * @s: 
 * @index: 
 * 
 * Remove a specific info record from the summary, by index.
 **/
void camel_folder_summary_remove_index(CamelFolderSummary *s, int index)
{
	CAMEL_SUMMARY_LOCK(s, ref_lock);
	CAMEL_SUMMARY_LOCK(s, summary_lock);
	if (index < s->messages->len) {
		CamelMessageInfo *info = s->messages->pdata[index];
		/* make sure it doesn't vanish while we're not looking */
		info->refcount++;
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);
		CAMEL_SUMMARY_UNLOCK(s, ref_lock);
		camel_folder_summary_remove(s, info);
		camel_folder_summary_info_free(s, info);
	} else {
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);
		CAMEL_SUMMARY_UNLOCK(s, ref_lock);
	}
}

/**
 * camel_folder_summary_encode_uint32:
 * @out: 
 * @value: 
 * 
 * Utility function to save an uint32 to a file.
 * 
 * Return value: -1 on error.
 **/
int
camel_folder_summary_encode_uint32(FILE *out, guint32 value)
{
	int i;

	io(printf("Encoding int %u\n", value));

	for (i=28;i>0;i-=7) {
		if (value >= (1<<i)) {
			unsigned int c = (value>>i) & 0x7f;
			if (fputc(c, out) == -1)
				return -1;
		}
	}
	return fputc(value | 0x80, out);
}

/**
 * camel_folder_summary_decode_uint32:
 * @in: 
 * @dest: 
 * 
 * Retrieve an encoded uint32 from a file.
 * 
 * Return value: -1 on error.  @*dest will contain the
 * decoded value.
 **/
int
camel_folder_summary_decode_uint32(FILE *in, guint32 *dest)
{
        guint32 value=0, v;

        /* until we get the last byte, keep decoding 7 bits at a time */
        while ( ((v = fgetc(in)) & 0x80) == 0 && v!=EOF) {
                value |= v;
                value <<= 7;
        }
	if (v == EOF) {
		*dest = value>>7;
		return -1;
	}
	*dest = value | (v&0x7f);

	io(printf("Decoding int %u\n", *dest));

        return 0;
}

/**
 * camel_folder_summary_encode_fixed_int32:
 * @out: 
 * @value: 
 * 
 * Encode a gint32, performing no compression, but converting
 * to network order.
 * 
 * Return value: -1 on error.
 **/
int
camel_folder_summary_encode_fixed_int32(FILE *out, gint32 value)
{
	guint32 save;

	save = htonl(value);
	if (fwrite(&save, sizeof(save), 1, out) != 1)
		return -1;
	return 0;
}

/**
 * camel_folder_summary_decode_fixed_int32:
 * @in: 
 * @dest: 
 * 
 * Retrieve a gint32.
 * 
 * Return value: -1 on error.
 **/
int
camel_folder_summary_decode_fixed_int32(FILE *in, gint32 *dest)
{
	guint32 save;

	if (fread(&save, sizeof(save), 1, in) == 1) {
		*dest = ntohl(save);
		return 0;
	} else {
		return -1;
	}
}

/**
 * camel_folder_summary_encode_time_t:
 * @out: 
 * @value: 
 * 
 * Encode a time_t value to the file.
 * 
 * Return value: -1 on error.
 **/
int
camel_folder_summary_encode_time_t(FILE *out, time_t value)
{
	int i;

	for (i=sizeof(time_t)-1;i>=0;i--) {
		if (fputc((value >> (i*8)) & 0xff, out) == -1)
			return -1;
	}
	return 0;
}

/**
 * camel_folder_summary_decode_time_t:
 * @in: 
 * @dest: 
 * 
 * Decode a time_t value.
 * 
 * Return value: -1 on error.
 **/
int
camel_folder_summary_decode_time_t(FILE *in, time_t *dest)
{
	time_t save = 0;
	unsigned int v;
	int i = sizeof(time_t) - 1;

        while ( i>=0 && (v = fgetc(in)) != EOF) {
		save |= v << (i*8);
		i--;
	}
	*dest = save;
	if (v == EOF)
		return -1;
	return 0;
}

/**
 * camel_folder_summary_encode_off_t:
 * @out: 
 * @value: 
 * 
 * Encode an off_t type.
 * 
 * Return value: 
 **/
int
camel_folder_summary_encode_off_t(FILE *out, off_t value)
{
	int i;

	for (i=sizeof(off_t)-1;i>=0;i--) {
		if (fputc((value >> (i*8)) & 0xff, out) == -1)
			return -1;
	}
	return 0;
}

/**
 * camel_folder_summary_decode_off_t:
 * @in: 
 * @dest: 
 * 
 * Decode an off_t type.
 * 
 * Return value: 
 **/
int
camel_folder_summary_decode_off_t(FILE *in, off_t *dest)
{
	off_t save = 0;
	unsigned int v;
	int i = sizeof(off_t) - 1;

        while ( i>=0 && (v = fgetc(in)) != EOF) {
		save |= v << (i*8);
		i--;
	}
	*dest = save;
	if (v == EOF)
		return -1;
	return 0;
}

/* should be sorted, for binary search */
/* This is a tokenisation mechanism for strings written to the
   summary - to save space.
   This list can have at most 31 words. */
static char * tokens[] = {
	"7bit",
	"8bit",
	"alternative",
	"application",
	"base64",
	"boundary",
	"charset",
	"filename",
	"html",
	"image",
	"iso-8859-1",
	"iso-8859-8",
	"message",
	"mixed",
	"multipart",
	"name",
	"octet-stream",
	"parallel",
	"plain",
	"postscript",
	"quoted-printable",
	"related",
	"rfc822",
	"text",
	"us-ascii",		/* 25 words */
};

#define tokens_len (sizeof(tokens)/sizeof(tokens[0]))

/* baiscally ...
    0 = null
    1-tokens_len == tokens[id-1]
    >=32 string, length = n-32
*/

#ifdef USE_BSEARCH
static int
token_search_cmp(char *key, char **index)
{
	d(printf("comparing '%s' to '%s'\n", key, *index));
	return strcmp(key, *index);
}
#endif

/**
 * camel_folder_summary_encode_token:
 * @out: 
 * @str: 
 * 
 * Encode a string value, but use tokenisation and compression
 * to reduce the size taken for common mailer words.  This
 * can still be used to encode normal strings as well.
 * 
 * Return value: -1 on error.
 **/
int
camel_folder_summary_encode_token(FILE *out, const char *str)
{
	io(printf("Encoding token: '%s'\n", str));

	if (str == NULL) {
		return camel_folder_summary_encode_uint32(out, 0);
	} else {
		int len = strlen(str);
		int i, token=-1;

		if (len <= 16) {
			char lower[32];
			char **match;

			for (i=0;i<len;i++)
				lower[i] = tolower(str[i]);
			lower[i] = 0;
#ifdef USE_BSEARCH
			match = bsearch(lower, tokens, tokens_len, sizeof(char *), (int (*)(const void *, const void *))token_search_cmp);
			if (match)
				token = match-tokens;
#else
			for (i=0;i<tokens_len;i++) {
				if (!strcmp(tokens[i], lower)) {
					token = i;
					break;
				}
			}
#endif
		}
		if (token != -1) {
			return camel_folder_summary_encode_uint32(out, token+1);
		} else {
			if (camel_folder_summary_encode_uint32(out, len+32) == -1)
				return -1;
			if (fwrite(str, len, 1, out) != 1)
				return -1;
		}
	}
	return 0;
}

/**
 * camel_folder_summary_decode_token:
 * @in: 
 * @str: 
 * 
 * Decode a token value.
 * 
 * Return value: -1 on error.
 **/
int
camel_folder_summary_decode_token(FILE *in, char **str)
{
	char *ret;
	guint32 len;

	io(printf("Decode token ...\n"));
	
	if (camel_folder_summary_decode_uint32(in, &len) == -1) {
		g_warning("Could not decode token from file");
		*str = NULL;
		return -1;
	}

	if (len<32) {
		if (len <= 0) {
			ret = NULL;
		} else if (len<= tokens_len) {
			ret = g_strdup(tokens[len-1]);
		} else {
			g_warning("Invalid token encountered: %d", len);
			*str = NULL;
			return -1;
		}
	} else if (len > 10240) {
		g_warning("Got broken string header length: %d bytes", len);
		*str = NULL;
		return -1;
	} else {
		len -= 32;
		ret = g_malloc(len+1);
		if (fread(ret, len, 1, in) != 1) {
			g_free(ret);
			*str = NULL;
			return -1;
		}
		ret[len]=0;
	}

	io(printf("Token = '%s'\n", ret));

	*str = ret;
	return 0;
}

/**
 * camel_folder_summary_encode_string:
 * @out: 
 * @str: 
 * 
 * Encode a normal string and save it in the output file.
 * 
 * Return value: -1 on error.
 **/
int
camel_folder_summary_encode_string(FILE *out, const char *str)
{
	register int len;

	io(printf("Encoding string: '%s'\n", str));

	if (str == NULL)
		return camel_folder_summary_encode_uint32(out, 0);

	len = strlen(str);
	if (camel_folder_summary_encode_uint32(out, len+1) == -1)
		return -1;
	if (fwrite(str, len, 1, out) == 1)
		return 0;
	return -1;
}


/**
 * camel_folder_summary_decode_string:
 * @in: 
 * @str: 
 * 
 * Decode a normal string from the input file.
 * 
 * Return value: -1 on error.
 **/
int
camel_folder_summary_decode_string(FILE *in, char **str)
{
	gint32 len;
	register char *ret;

	io(printf("Decode string ...\n", str));

	if (camel_folder_summary_decode_uint32(in, &len) == -1) {
		*str = NULL;
		return -1;
	}

	len--;
	if (len < 0) {
		*str = NULL;
		io(printf("String = '%s'\n", *str));
		return -1;
	}

	ret = g_malloc(len+1);
	if (fread(ret, len, 1, in) != 1) {
		g_free(ret);
		*str = NULL;
		return -1;
	}

	io(printf("String = '%s'\n", ret));

	ret[len] = 0;
	*str = ret;
	return 0;
}

static struct _node *
my_list_append(struct _node **list, struct _node *n)
{
	struct _node *ln = (struct _node *)list;
	while (ln->next)
		ln = ln->next;
	n->next = 0;
	ln->next = n;
	return n;
}

static int
my_list_size(struct _node **list)
{
	int len = 0;
	struct _node *ln = (struct _node *)list;
	while (ln->next) {
		ln = ln->next;
		len++;
	}
	return len;
}

static int
summary_header_load(CamelFolderSummary *s, FILE *in)
{
	gint32 version, flags, nextuid, count;
	time_t time;

	fseek(in, 0, SEEK_SET);

	io(printf("Loading header\n"));

	if (camel_folder_summary_decode_fixed_int32(in, &version) == -1
	    || camel_folder_summary_decode_fixed_int32(in, &flags) == -1
	    || camel_folder_summary_decode_fixed_int32(in, &nextuid) == -1
	    || camel_folder_summary_decode_time_t(in, &time) == -1
	    || camel_folder_summary_decode_fixed_int32(in, &count) == -1) {
		return -1;
	}

	s->nextuid = nextuid;
	s->flags = flags;
	s->time = time;
	s->saved_count = count;
	if (s->version != version) {
		g_warning("Summary header version mismatch");
		return -1;
	}
	return 0;
}

static int
summary_header_save(CamelFolderSummary *s, FILE *out)
{
	fseek(out, 0, SEEK_SET);

	io(printf("Savining header\n"));

	camel_folder_summary_encode_fixed_int32(out, s->version);
	camel_folder_summary_encode_fixed_int32(out, s->flags);
	camel_folder_summary_encode_fixed_int32(out, s->nextuid);
	camel_folder_summary_encode_time_t(out, s->time);
	return camel_folder_summary_encode_fixed_int32(out, camel_folder_summary_count(s));
}

/* are these even useful for anything??? */
static CamelMessageInfo * message_info_new_from_parser(CamelFolderSummary *s, CamelMimeParser *mp)
{
	CamelMessageInfo *mi = NULL;
	int state;

	state = camel_mime_parser_state(mp);
	switch (state) {
	case HSCAN_HEADER:
	case HSCAN_MESSAGE:
	case HSCAN_MULTIPART:
		mi = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_new(s, camel_mime_parser_headers_raw(mp));
		break;
	default:
		g_error("Invalid parser state");
	}

	return mi;
}

static CamelMessageContentInfo * content_info_new_from_parser(CamelFolderSummary *s, CamelMimeParser *mp)
{
	CamelMessageContentInfo *ci = NULL;

	switch (camel_mime_parser_state(mp)) {
	case HSCAN_HEADER:
	case HSCAN_MESSAGE:
	case HSCAN_MULTIPART:
		ci = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->content_info_new(s, camel_mime_parser_headers_raw(mp));
		if (ci) {
			ci->type = camel_mime_parser_content_type(mp);
			header_content_type_ref(ci->type);
		}
		break;
	default:
		g_error("Invalid parser state");
	}

	return ci;
}

static CamelMessageInfo * message_info_new_from_message(CamelFolderSummary *s, CamelMimeMessage *msg)
{
	CamelMessageInfo *mi;

	mi = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_new(s, ((CamelMimePart *)msg)->headers);

	return mi;
}

static CamelMessageContentInfo * content_info_new_from_message(CamelFolderSummary *s, CamelMimePart *mp)
{
	CamelMessageContentInfo *ci;

	ci = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->content_info_new(s, mp->headers);

	return ci;
}

#ifndef NO_WARNINGS
#warning "These should be made private again, easy to fix (used in filter-driver)"
#endif
char *
camel_folder_summary_format_address(struct _header_raw *h, const char *name)
{
	struct _header_address *addr;
	const char *text;
	char *ret;

	text = header_raw_find (&h, name, NULL);
	addr = header_address_decode (text);
	if (addr) {
		ret = header_address_list_format (addr);
		header_address_list_clear (&addr);
	} else {
		ret = g_strdup (text);
	}
	
	return ret;
}

char *
camel_folder_summary_format_string(struct _header_raw *h, const char *name)
{
	const char *text;

	text = header_raw_find(&h, name, NULL);
	if (text) {
		while (isspace(*text))
			text++;
		return header_decode_string(text);
	} else {
		return NULL;
	}
}

/**
 * camel_folder_summary_info_new:
 * @s: 
 * 
 * Allocate a new camel message info, suitable for adding
 * to this summary.
 * 
 * Return value: 
 **/
CamelMessageInfo *
camel_folder_summary_info_new(CamelFolderSummary *s)
{
	CamelMessageInfo *mi;

	CAMEL_SUMMARY_LOCK(s, alloc_lock);
	if (s->message_info_chunks == NULL)
		s->message_info_chunks = e_memchunk_new(32, s->message_info_size);
	mi = e_memchunk_alloc(s->message_info_chunks);
	CAMEL_SUMMARY_UNLOCK(s, alloc_lock);

	memset(mi, 0, s->message_info_size);
#ifdef DOESTRV
	mi->strings = e_strv_new(s->message_info_strings);
#endif
	mi->refcount = 1;
	return mi;
}

static CamelMessageContentInfo *
content_info_alloc(CamelFolderSummary *s)
{
	CamelMessageContentInfo *ci;

	CAMEL_SUMMARY_LOCK(s, alloc_lock);
	if (s->content_info_chunks == NULL)
		s->content_info_chunks = e_memchunk_new(32, s->content_info_size);
	ci = e_memchunk_alloc(s->content_info_chunks);
	CAMEL_SUMMARY_UNLOCK(s, alloc_lock);

	memset(ci, 0, s->content_info_size);
	return ci;
}

static CamelMessageInfo *
message_info_new(CamelFolderSummary *s, struct _header_raw *h)
{
	CamelMessageInfo *mi;
	const char *received;
	guchar digest[16];
	struct _header_references *refs, *scan;
	char *msgid;
	int count;

	mi = camel_folder_summary_info_new(s);

#ifdef DOESTRV
	msgid = camel_folder_summary_format_string(h, "subject");
	e_strv_set_ref_free(mi->strings, CAMEL_MESSAGE_INFO_SUBJECT, msgid);
	msgid = camel_folder_summary_format_address(h, "from");
	e_strv_set_ref_free(mi->strings, CAMEL_MESSAGE_INFO_FROM, msgid);
	msgid = camel_folder_summary_format_address(h, "to");
	e_strv_set_ref_free(mi->strings, CAMEL_MESSAGE_INFO_TO, msgid);
	msgid = camel_folder_summary_format_address(h, "cc");
	e_strv_set_ref_free(mi->strings, CAMEL_MESSAGE_INFO_CC, msgid);
#else
	mi->subject = camel_folder_summary_format_string(h, "subject");
	mi->from = camel_folder_summary_format_address(h, "from");
	mi->to = camel_folder_summary_format_address(h, "to");
	mi->cc = camel_folder_summary_format_address(h, "cc");
#endif
	mi->user_flags = NULL;
	mi->user_tags = NULL;
	mi->date_sent = header_decode_date(header_raw_find(&h, "date", NULL), NULL);
	received = header_raw_find(&h, "received", NULL);
	if (received)
		received = strrchr(received, ';');
	if (received)
		mi->date_received = header_decode_date(received + 1, NULL);
	else
		mi->date_received = 0;

	msgid = header_msgid_decode(header_raw_find(&h, "message-id", NULL));
	if (msgid) {
		md5_get_digest(msgid, strlen(msgid), digest);
		memcpy(mi->message_id.id.hash, digest, sizeof(mi->message_id.id.hash));
		g_free(msgid);
	}
	/* if we have a references, use that, otherwise, see if we have an in-reply-to
	   header, with parsable content, otherwise *shrug* */
	if ((refs = header_references_decode(header_raw_find(&h, "references", NULL))) != NULL
	    || (refs = header_references_decode(header_raw_find(&h, "in-reply-to", NULL))) != NULL) {
		count = header_references_list_size(&refs);
		mi->references = g_malloc(sizeof(*mi->references) + ((count-1) * sizeof(mi->references->references[0])));
		count = 0;
		scan = refs;
		while (scan) {
			/* FIXME: the id might be NULL because of a small bug in camel-mime-utils */
			if (scan->id) {
				md5_get_digest(scan->id, strlen(scan->id), digest);
				memcpy(mi->references->references[count].id.hash, digest, sizeof(mi->message_id.id.hash));
				count++;
			}
			scan = scan->next;
		}
		mi->references->size = count;
		header_references_list_clear(&refs);
	}

	return mi;
}


static CamelMessageInfo *
message_info_load(CamelFolderSummary *s, FILE *in)
{
	CamelMessageInfo *mi;
	guint count;
	int i;
#ifdef DOESTRV
	char *tmp;
#endif

	mi = camel_folder_summary_info_new(s);

	io(printf("Loading message info\n"));
#ifdef DOESTRV
	camel_folder_summary_decode_string(in, &tmp);
	e_strv_set_ref_free(mi->strings, CAMEL_MESSAGE_INFO_UID, tmp);
	camel_folder_summary_decode_uint32(in, &mi->flags);
	camel_folder_summary_decode_uint32(in, &mi->size);
	camel_folder_summary_decode_time_t(in, &mi->date_sent);
	camel_folder_summary_decode_time_t(in, &mi->date_received);
	camel_folder_summary_decode_string(in, &tmp);
	e_strv_set_ref_free(mi->strings, CAMEL_MESSAGE_INFO_SUBJECT, tmp);
	camel_folder_summary_decode_string(in, &tmp);
	e_strv_set_ref_free(mi->strings, CAMEL_MESSAGE_INFO_FROM, tmp);
	camel_folder_summary_decode_string(in, &tmp);
	e_strv_set_ref_free(mi->strings, CAMEL_MESSAGE_INFO_TO, tmp);
	camel_folder_summary_decode_string(in, &tmp);
	e_strv_set_ref_free(mi->strings, CAMEL_MESSAGE_INFO_CC, tmp);
#else
	camel_folder_summary_decode_string(in, &mi->uid);
	camel_folder_summary_decode_uint32(in, &mi->flags);
	camel_folder_summary_decode_uint32(in, &mi->size);
	camel_folder_summary_decode_time_t(in, &mi->date_sent);
	camel_folder_summary_decode_time_t(in, &mi->date_received);
	camel_folder_summary_decode_string(in, &mi->subject);
	camel_folder_summary_decode_string(in, &mi->from);
	camel_folder_summary_decode_string(in, &mi->to);
	camel_folder_summary_decode_string(in, &mi->cc);
#endif
	mi->content = NULL;

	camel_folder_summary_decode_fixed_int32(in, &mi->message_id.id.part.hi);
	camel_folder_summary_decode_fixed_int32(in, &mi->message_id.id.part.lo);

	camel_folder_summary_decode_uint32(in, &count);
	if (count > 0) {
		mi->references = g_malloc(sizeof(*mi->references) + ((count-1) * sizeof(mi->references->references[0])));
		mi->references->size = count;
		for (i=0;i<count;i++) {
			camel_folder_summary_decode_fixed_int32(in, &mi->references->references[i].id.part.hi);
			camel_folder_summary_decode_fixed_int32(in, &mi->references->references[i].id.part.lo);
		}
	}

	camel_folder_summary_decode_uint32(in, &count);
	for (i=0;i<count;i++) {
		char *name;
		camel_folder_summary_decode_string(in, &name);
		camel_flag_set(&mi->user_flags, name, TRUE);
		g_free(name);
	}

	camel_folder_summary_decode_uint32(in, &count);
	for (i=0;i<count;i++) {
		char *name, *value;
		camel_folder_summary_decode_string(in, &name);
		camel_folder_summary_decode_string(in, &value);
		camel_tag_set(&mi->user_tags, name, value);
		g_free(name);
		g_free(value);
	}

	return mi;
}

static int
message_info_save(CamelFolderSummary *s, FILE *out, CamelMessageInfo *mi)
{
	guint32 count;
	CamelFlag *flag;
	CamelTag *tag;
	int i;

	io(printf("Saving message info\n"));

	camel_folder_summary_encode_string(out, camel_message_info_uid(mi));
	camel_folder_summary_encode_uint32(out, mi->flags);
	camel_folder_summary_encode_uint32(out, mi->size);
	camel_folder_summary_encode_time_t(out, mi->date_sent);
	camel_folder_summary_encode_time_t(out, mi->date_received);
	camel_folder_summary_encode_string(out, camel_message_info_subject(mi));
	camel_folder_summary_encode_string(out, camel_message_info_from(mi));
	camel_folder_summary_encode_string(out, camel_message_info_to(mi));
	camel_folder_summary_encode_string(out, camel_message_info_cc(mi));

	camel_folder_summary_encode_fixed_int32(out, mi->message_id.id.part.hi);
	camel_folder_summary_encode_fixed_int32(out, mi->message_id.id.part.lo);

	if (mi->references) {
		camel_folder_summary_encode_uint32(out, mi->references->size);
		for (i=0;i<mi->references->size;i++) {
			camel_folder_summary_encode_fixed_int32(out, mi->references->references[i].id.part.hi);
			camel_folder_summary_encode_fixed_int32(out, mi->references->references[i].id.part.lo);
		}
	} else {
		camel_folder_summary_encode_uint32(out, 0);
	}

	count = camel_flag_list_size(&mi->user_flags);
	camel_folder_summary_encode_uint32(out, count);
	flag = mi->user_flags;
	while (flag) {
		camel_folder_summary_encode_string(out, flag->name);
		flag = flag->next;
	}

	count = camel_tag_list_size(&mi->user_tags);
	camel_folder_summary_encode_uint32(out, count);
	tag = mi->user_tags;
	while (tag) {
		camel_folder_summary_encode_string(out, tag->name);
		camel_folder_summary_encode_string(out, tag->value);
		tag = tag->next;
	}

	return ferror(out);
}

static void
message_info_free(CamelFolderSummary *s, CamelMessageInfo *mi)
{
#ifdef DOESTRV
	e_strv_destroy(mi->strings);
#else
	g_free(mi->uid);
	g_free(mi->subject);
	g_free(mi->from);
	g_free(mi->to);
	g_free(mi->cc);
#endif
	g_free(mi->references);
	camel_flag_list_free(&mi->user_flags);
	camel_tag_list_free(&mi->user_tags);
	e_memchunk_free(s->message_info_chunks, mi);
}

static CamelMessageContentInfo *
content_info_new(CamelFolderSummary *s, struct _header_raw *h)
{
	CamelMessageContentInfo *ci;

	ci = content_info_alloc(s);

	ci->id = header_msgid_decode(header_raw_find(&h, "content-id", NULL));
	ci->description = header_decode_string(header_raw_find(&h, "content-description", NULL));
	ci->encoding = header_content_encoding_decode(header_raw_find(&h, "content-transfer-encoding", NULL));

	return ci;
}

static CamelMessageContentInfo *
content_info_load(CamelFolderSummary *s, FILE *in)
{
	CamelMessageContentInfo *ci;
	char *type, *subtype;
	guint32 count, i;
	struct _header_content_type *ct;

	io(printf("Loading content info\n"));

	ci = content_info_alloc(s);
	
	camel_folder_summary_decode_token(in, &type);
	camel_folder_summary_decode_token(in, &subtype);
	ct = header_content_type_new(type, subtype);
	g_free(type);		/* can this be removed? */
	g_free(subtype);
	camel_folder_summary_decode_uint32(in, &count);
	for (i=0;i<count;i++) {
		char *name, *value;
		camel_folder_summary_decode_token(in, &name);
		camel_folder_summary_decode_token(in, &value);
		header_content_type_set_param(ct, name, value);
		/* TODO: do this so we dont have to double alloc/free */
		g_free(name);
		g_free(value);
	}
	ci->type = ct;

	camel_folder_summary_decode_token(in, &ci->id);
	camel_folder_summary_decode_token(in, &ci->description);
	camel_folder_summary_decode_token(in, &ci->encoding);

	ci->childs = NULL;
	return ci;
}

static int
content_info_save(CamelFolderSummary *s, FILE *out, CamelMessageContentInfo *ci)
{
	struct _header_content_type *ct;
	struct _header_param *hp;

	io(printf("Saving content info\n"));

	ct = ci->type;
	if (ct) {
		camel_folder_summary_encode_token(out, ct->type);
		camel_folder_summary_encode_token(out, ct->subtype);
		camel_folder_summary_encode_uint32(out, my_list_size((struct _node **)&ct->params));
		hp = ct->params;
		while (hp) {
			camel_folder_summary_encode_token(out, hp->name);
			camel_folder_summary_encode_token(out, hp->value);
			hp = hp->next;
		}
	} else {
		camel_folder_summary_encode_token(out, NULL);
		camel_folder_summary_encode_token(out, NULL);
		camel_folder_summary_encode_uint32(out, 0);
	}
	camel_folder_summary_encode_token(out, ci->id);
	camel_folder_summary_encode_token(out, ci->description);
	return camel_folder_summary_encode_token(out, ci->encoding);
}

static void
content_info_free(CamelFolderSummary *s, CamelMessageContentInfo *ci)
{
	header_content_type_unref(ci->type);
	g_free(ci->id);
	g_free(ci->description);
	g_free(ci->encoding);
	e_memchunk_free(s->content_info_chunks, ci);
}

static char *
next_uid_string(CamelFolderSummary *s)
{
	return g_strdup_printf("%u", camel_folder_summary_next_uid(s));
}

/*
  OK
  Now this is where all the "smarts" happen, where the content info is built,
  and any indexing and what not is performed
*/

/* must have filter_lock before calling this function */
static CamelMessageContentInfo *
summary_build_content_info(CamelFolderSummary *s, CamelMessageInfo *msginfo, CamelMimeParser *mp)
{
	int state, len;
	char *buffer;
	CamelMessageContentInfo *info = NULL;
	struct _header_content_type *ct;
	int body;
	int enc_id = -1, chr_id = -1, idx_id = -1;
	struct _CamelFolderSummaryPrivate *p = _PRIVATE(s);
	CamelMimeFilterCharset *mfc;
	CamelMessageContentInfo *part;

	d(printf("building content info\n"));

	/* start of this part */
	state = camel_mime_parser_step(mp, &buffer, &len);
	body = camel_mime_parser_tell(mp);

	if (s->build_content)
		info = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->content_info_new_from_parser(s, mp);

	switch(state) {
	case HSCAN_HEADER:
		/* check content type for indexing, then read body */
		ct = camel_mime_parser_content_type(mp);
		/* update attachments flag as we go */
		if (!header_content_type_is(ct, "text", "*"))
			msginfo->flags |= CAMEL_MESSAGE_ATTACHMENTS;

		if (p->index && header_content_type_is(ct, "text", "*")) {
			char *encoding;
			const char *charset;

			d(printf("generating index:\n"));
			
			encoding = header_content_encoding_decode(camel_mime_parser_header(mp, "content-transfer-encoding", NULL));
			if (encoding) {
				if (!strcasecmp(encoding, "base64")) {
					d(printf(" decoding base64\n"));
					if (p->filter_64 == NULL)
						p->filter_64 = camel_mime_filter_basic_new_type(CAMEL_MIME_FILTER_BASIC_BASE64_DEC);
					enc_id = camel_mime_parser_filter_add(mp, (CamelMimeFilter *)p->filter_64);
				} else if (!strcasecmp(encoding, "quoted-printable")) {
					d(printf(" decoding quoted-printable\n"));
					if (p->filter_qp == NULL)
						p->filter_qp = camel_mime_filter_basic_new_type(CAMEL_MIME_FILTER_BASIC_QP_DEC);
					enc_id = camel_mime_parser_filter_add(mp, (CamelMimeFilter *)p->filter_qp);
				} else {
					d(printf(" ignoring encoding %s\n", encoding));
				}
				g_free(encoding);
			}
				
			charset = header_content_type_param(ct, "charset");
			if (charset!=NULL
			    && !(strcasecmp(charset, "us-ascii")==0
				 || strcasecmp(charset, "utf-8")==0)) {
				d(printf(" Adding conversion filter from %s to UTF-8\n", charset));
				mfc = g_hash_table_lookup(p->filter_charset, charset);
				if (mfc == NULL) {
					mfc = camel_mime_filter_charset_new_convert(charset, "UTF-8");
					if (mfc)
						g_hash_table_insert(p->filter_charset, g_strdup(charset), mfc);
				}
				if (mfc) {
					chr_id = camel_mime_parser_filter_add(mp, (CamelMimeFilter *)mfc);
				} else {
					g_warning("Cannot convert '%s' to 'UTF-8', message index may be corrupt", charset);
				}
			}

			/* and this filter actually does the indexing */
			idx_id = camel_mime_parser_filter_add(mp, (CamelMimeFilter *)p->filter_index);
		}
		/* and scan/index everything */
		while (camel_mime_parser_step(mp, &buffer, &len) != HSCAN_BODY_END)
			;
		/* and remove the filters */
		camel_mime_parser_filter_remove(mp, enc_id);
		camel_mime_parser_filter_remove(mp, chr_id);
		camel_mime_parser_filter_remove(mp, idx_id);
		break;
	case HSCAN_MULTIPART:
		d(printf("Summarising multipart\n"));
		/* update attachments flag as we go */
		ct = camel_mime_parser_content_type(mp);
		if (header_content_type_is(ct, "multipart", "mixed"))
			msginfo->flags |= CAMEL_MESSAGE_ATTACHMENTS;

		while (camel_mime_parser_step(mp, &buffer, &len) != HSCAN_MULTIPART_END) {
			camel_mime_parser_unstep(mp);
			part = summary_build_content_info(s, msginfo, mp);
			if (part) {
				part->parent = info;
				my_list_append((struct _node **)&info->childs, (struct _node *)part);
			}
		}
		break;
	case HSCAN_MESSAGE:
		d(printf("Summarising message\n"));
		/* update attachments flag as we go */
		msginfo->flags |= CAMEL_MESSAGE_ATTACHMENTS;

		part = summary_build_content_info(s, msginfo, mp);
		if (part) {
			part->parent = info;
			my_list_append((struct _node **)&info->childs, (struct _node *)part);
		}
		state = camel_mime_parser_step(mp, &buffer, &len);
		if (state != HSCAN_MESSAGE_END) {
			g_error("Bad parser state: Expecing MESSAGE_END or MESSAGE_EOF, got: %d", state);
			camel_mime_parser_unstep(mp);
		}
		break;
	}

	d(printf("finished building content info\n"));

	return info;
}

/* build the content-info, from a message */
/* this needs no lock, as we copy all data, and ibex is threadsafe */
static CamelMessageContentInfo *
summary_build_content_info_message(CamelFolderSummary *s, CamelMessageInfo *msginfo, CamelMimePart *object)
{
	CamelDataWrapper *containee;
	int parts, i;
	struct _CamelFolderSummaryPrivate *p = _PRIVATE(s);
	CamelMessageContentInfo *info = NULL, *child;

	if (s->build_content)
		info = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->content_info_new_from_message(s, object);
	
	containee = camel_medium_get_content_object(CAMEL_MEDIUM(object));

	if (containee == NULL)
		return info;

	/* TODO: I find it odd that get_part and get_content_object do not
	   add a reference, probably need fixing for multithreading */

	/* check for attachments */
	if (gmime_content_field_is_type(CAMEL_DATA_WRAPPER(containee)->mime_type, "multipart", "*")) {
		if (gmime_content_field_is_type(CAMEL_DATA_WRAPPER(containee)->mime_type, "multipart", "mixed"))
			msginfo->flags |= CAMEL_MESSAGE_ATTACHMENTS;
	} else if (!gmime_content_field_is_type(CAMEL_DATA_WRAPPER(containee)->mime_type, "text", "*"))
		msginfo->flags |= CAMEL_MESSAGE_ATTACHMENTS;

	/* using the object types is more accurate than using the mime/types */
	if (CAMEL_IS_MULTIPART(containee)) {
		parts = camel_multipart_get_number(CAMEL_MULTIPART(containee));
		for (i=0;i<parts;i++) {
			CamelMimePart *part = camel_multipart_get_part(CAMEL_MULTIPART(containee), i);
			g_assert(part);
			child = summary_build_content_info_message(s, msginfo, part);
			if (child) {
				child->parent = info;
				my_list_append((struct _node **)&info->childs, (struct _node *)child);
			}
		}
	} else if (CAMEL_IS_MIME_MESSAGE(containee)) {
		/* for messages we only look at its contents */
		child = summary_build_content_info_message(s, msginfo, (CamelMimePart *)containee);
		if (child) {
			child->parent = info;
			my_list_append((struct _node **)&info->childs, (struct _node *)child);
		}
	} else if (p->index
		   && gmime_content_field_is_type(CAMEL_DATA_WRAPPER(containee)->mime_type, "text", "*")) {
		/* index all text parts if we're indexing */
		CamelStreamMem *mem = (CamelStreamMem *)camel_stream_mem_new();

		camel_data_wrapper_write_to_stream(containee, (CamelStream *)mem);
		ibex_index_buffer(p->index, (char *)camel_message_info_uid(msginfo), mem->buffer->data, mem->buffer->len, NULL);
		camel_object_unref((CamelObject *)mem);
	}

	return info;
}

/**
 * camel_flag_get:
 * @list: 
 * @name: 
 * 
 * Find the state of the flag @name in @list.
 * 
 * Return value: The state of the flag (TRUE or FALSE).
 **/
gboolean
camel_flag_get(CamelFlag **list, const char *name)
{
	CamelFlag *flag;
	flag = *list;
	while (flag) {
		if (!strcmp(flag->name, name))
			return TRUE;
		flag = flag->next;
	}
	return FALSE;
}

/**
 * camel_flag_set:
 * @list: 
 * @name: 
 * @value: 
 * 
 * Set the state of a flag @name in the list @list to @value.
 *
 * Return value: Whether or not it changed.
 **/
gboolean
camel_flag_set(CamelFlag **list, const char *name, gboolean value)
{
	CamelFlag *flag, *tmp;

	/* this 'trick' works because flag->next is the first element */
	flag = (CamelFlag *)list;
	while (flag->next) {
		tmp = flag->next;
		if (!strcmp(flag->next->name, name)) {
			if (!value) {
				flag->next = tmp->next;
				g_free(tmp);
			}
			return !value;
		}
		flag = tmp;
	}

	if (value) {
		tmp = g_malloc(sizeof(*tmp) + strlen(name));
		strcpy(tmp->name, name);
		tmp->next = 0;
		flag->next = tmp;
	}
	return value;
}

/**
 * camel_flag_list_size:
 * @list: 
 * 
 * Get the length of the flag list.
 * 
 * Return value: The number of TRUE flags in the list.
 **/
int
camel_flag_list_size(CamelFlag **list)
{
	int count=0;
	CamelFlag *flag;

	flag = *list;
	while (flag) {
		count++;
		flag = flag->next;
	}
	return count;
}

/**
 * camel_flag_list_free:
 * @list: 
 * 
 * Free the memory associated with the flag list @list.
 **/
void
camel_flag_list_free(CamelFlag **list)
{
	CamelFlag *flag, *tmp;
	flag = *list;
	while (flag) {
		tmp = flag->next;
		g_free(flag);
		flag = tmp;
	}
	*list = NULL;
}

const char
*camel_tag_get(CamelTag **list, const char *name)
{
	CamelTag *tag;

	tag = *list;
	while (tag) {
		if (!strcmp(tag->name, name))
			return (const char *)tag->value;
		tag = tag->next;
	}
	return NULL;
}

/**
 * camel_tag_set:
 * @list: 
 * @name: 
 * @value: 
 * 
 * Set the tag @name in the tag list @list to @value.
 *
 * Return value: whether or not it changed
 **/
gboolean
camel_tag_set(CamelTag **list, const char *name, const char *value)
{
	CamelTag *tag, *tmp;

	/* this 'trick' works because tag->next is the first element */
	tag = (CamelTag *)list;
	while (tag->next) {
		tmp = tag->next;
		if (!strcmp(tmp->name, name)) {
			if (value == NULL) { /* clear it? */
				tag->next = tmp->next;
				g_free(tmp->value);
				g_free(tmp);
				return TRUE;
			} else if (strcmp(tmp->value, value)) { /* has it changed? */
				g_free(tmp->value);
				tmp->value = g_strdup(value);
				return TRUE;
			}
			return FALSE;
		}
		tag = tmp;
	}

	if (value) {
		tmp = g_malloc(sizeof(*tmp)+strlen(name));
		strcpy(tmp->name, name);
		tmp->value = g_strdup(value);
		tmp->next = 0;
		tag->next = tmp;
		return TRUE;
	}
	return FALSE;
}

/**
 * camel_tag_list_size:
 * @list: 
 * 
 * Get the number of tags present in the tag list @list.
 * 
 * Return value: The number of tags.
 **/
int		camel_tag_list_size(CamelTag **list)
{
	int count=0;
	CamelTag *tag;

	tag = *list;
	while (tag) {
		count++;
		tag = tag->next;
	}
	return count;
}

/**
 * camel_tag_list_free:
 * @list: 
 * 
 * Free the tag list @list.
 **/
void		camel_tag_list_free(CamelTag **list)
{
	CamelTag *tag, *tmp;
	tag = *list;
	while (tag) {
		tmp = tag->next;
		g_free(tag->value);
		g_free(tag);
		tag = tmp;
	}
	*list = NULL;
}

struct flag_names_t {
	char *name;
	guint32 value;
} flag_names[] = {
	{ "answered", CAMEL_MESSAGE_ANSWERED },
	{ "deleted", CAMEL_MESSAGE_DELETED },
	{ "draft", CAMEL_MESSAGE_DELETED },
	{ "flagged", CAMEL_MESSAGE_FLAGGED },
	{ "seen", CAMEL_MESSAGE_SEEN },
	{ "attachments", CAMEL_MESSAGE_ATTACHMENTS },
	{ NULL, 0 }
};

/**
 * camel_system_flag:
 * @name: 
 * 
 * Returns the integer value of the flag string.
 **/
guint32
camel_system_flag (const char *name)
{
	struct flag_names_t *flag;
	
	g_return_val_if_fail (name != NULL, 0);
	
	for (flag = flag_names; *flag->name; flag++)
		if (!g_strcasecmp (name, flag->name))
			return flag->value;
	
	return 0;
}

/**
 * camel_system_flag_get:
 * @flags: 
 * @name: 
 * 
 * Find the state of the flag @name in @flags.
 * 
 * Return value: The state of the flag (TRUE or FALSE).
 **/
gboolean
camel_system_flag_get (guint32 flags, const char *name)
{
	g_return_val_if_fail (name != NULL, FALSE);
	
	return flags & camel_system_flag (name);
}


/**
 * camel_message_info_new:
 *
 * Returns a new CamelMessageInfo structure.
 **/
CamelMessageInfo *
camel_message_info_new (void)
{
	CamelMessageInfo *info;
	
	info = g_malloc0(sizeof(*info));
#ifdef DOESTRV
	info->strings = e_strv_new (CAMEL_MESSAGE_INFO_LAST);
#endif
	info->refcount = 1;

	return info;
}

/**
 * camel_message_info_ref:
 * @info: 
 * 
 * Reference an info.
 *
 * NOTE: This interface is not MT-SAFE, like the others.
 **/
void camel_message_info_ref(CamelMessageInfo *info)
{
	GLOBAL_INFO_LOCK(info);
	info->refcount++;
	GLOBAL_INFO_UNLOCK(info);
}

/**
 * camel_message_info_new_from_header:
 * @header: raw header
 *
 * Returns a new CamelMessageInfo structure populated by the header.
 **/
CamelMessageInfo *
camel_message_info_new_from_header (struct _header_raw *header)
{
	CamelMessageInfo *info;
	char *subject, *from, *to, *cc;
	
	subject = camel_folder_summary_format_string (header, "subject");
	from = camel_folder_summary_format_address (header, "from");
	to = camel_folder_summary_format_address (header, "to");
	cc = camel_folder_summary_format_address (header, "cc");
	
	info = camel_message_info_new();

	camel_message_info_set_subject (info, subject);
	camel_message_info_set_from (info, from);
	camel_message_info_set_to (info, to);
	camel_message_info_set_cc (info, cc);

	return info;
}

/**
 * camel_message_info_dup_to:
 * @from: source message info
 * @to: destination message info
 *
 * Duplicates the contents of one CamelMessageInfo structure into another.
 * (The destination is assumed to be empty: its contents are not freed.)
 * The slightly odd interface is to allow this to be used to initialize
 * "subclasses" of CamelMessageInfo.
 **/
void
camel_message_info_dup_to(const CamelMessageInfo *from, CamelMessageInfo *to)
{
	CamelFlag *flag;
	CamelTag *tag;

	/* Copy numbers */
	to->flags = from->flags;
	to->size = from->size;
	to->date_sent = from->date_sent;
	to->date_received = from->date_received;
	to->refcount = 1;

	/* Copy strings */
#ifdef DOESTRV
	to->strings = e_strv_new(CAMEL_MESSAGE_INFO_LAST);
	e_strv_set(to->strings, CAMEL_MESSAGE_INFO_SUBJECT, camel_message_info_subject(from));
	e_strv_set(to->strings, CAMEL_MESSAGE_INFO_FROM, camel_message_info_from(from));
	e_strv_set(to->strings, CAMEL_MESSAGE_INFO_TO, camel_message_info_to(from));
	e_strv_set(to->strings, CAMEL_MESSAGE_INFO_CC, camel_message_info_cc(from));
	e_strv_set(to->strings, CAMEL_MESSAGE_INFO_UID, camel_message_info_uid(from));
#else
	to->subject = g_strdup(from->subject);
	to->from = g_strdup(from->from);
	to->to = g_strdup(from->to);
	to->cc = g_strdup(from->cc);
	to->uid = g_strdup(from->uid);
#endif
	memcpy(&to->message_id, &from->message_id, sizeof(from->message_id));

	/* Copy structures */
	if (from->references) {
		int len = sizeof(*from->references) + ((from->references->size-1) * sizeof(from->references->references[0]));

		to->references = g_malloc(len);
		memcpy(to->references, from->references, len);
	} else {
		to->references = NULL;
	}

	flag = from->user_flags;
	while (flag) {
		camel_flag_set(&to->user_flags, flag->name, TRUE);
		flag = flag->next;
	}

	tag = from->user_tags;
	while (tag) {
		camel_tag_set(&to->user_tags, tag->name, tag->value);
		tag = tag->next;
	}

	/* No, this is impossible without knowing the class of summary we came from */
	/* FIXME some day */
	to->content = NULL;
}

/**
 * camel_message_info_free:
 * @mi: the message info
 *
 * Unref's and potentially frees a CamelMessageInfo and its contents.
 *
 * Can only be used to free CamelMessageInfo's created with
 * camel_message_info_dup_to.
 *
 * NOTE: This interface is not MT-SAFE, like the others.
 *
 **/
void
camel_message_info_free(CamelMessageInfo *mi)
{
	g_return_if_fail(mi != NULL);

	GLOBAL_INFO_LOCK(info);
	mi->refcount--;
	if (mi->refcount > 0) {
		GLOBAL_INFO_UNLOCK(info);
		return;
	}
	GLOBAL_INFO_UNLOCK(info);

#ifdef DOESTRV
	e_strv_destroy(mi->strings);
#else
	g_free(mi->uid);
	g_free(mi->subject);
	g_free(mi->from);
	g_free(mi->to);
	g_free(mi->cc);
#endif
	g_free(mi->references);
	camel_flag_list_free(&mi->user_flags);
	camel_tag_list_free(&mi->user_tags);
	/* FIXME: content info? */
	g_free(mi);
}

#ifdef DOESTRV
const char *camel_message_info_string(const CamelMessageInfo *mi, int type)
{
	if (mi->strings == NULL)
		return "";
	return e_strv_get(mi->strings, type);
}

void camel_message_info_set_string(CamelMessageInfo *mi, int type, char *str)
{
	g_assert(mi->strings != NULL);
		
	mi->strings = e_strv_set_ref_free(mi->strings, type, str);
}
#endif

#if 0
static void
content_info_dump(CamelMessageContentInfo *ci, int depth)
{
	char *p;

	p = alloca(depth*4+1);
	memset(p, ' ', depth*4);
	p[depth*4] = 0;

	if (ci == NULL) {
		printf("%s<empty>\n", p);
		return;
	}

	printf("%sconent-type: %s/%s\n", p, ci->type->type, ci->type->subtype);
	printf("%sontent-transfer-encoding: %s\n", p, ci->encoding);
	printf("%scontent-description: %s\n", p, ci->description);
	printf("%sbytes: %d %d %d\n", p, (int)ci->pos, (int)ci->bodypos, (int)ci->endpos);
	ci = ci->childs;
	while (ci) {
		content_info_dump(ci, depth+1);
		ci = ci->next;
	}
}

static void
message_info_dump(CamelMessageInfo *mi)
{
	if (mi == NULL) {
		printf("No message?\n");
		return;
	}

	printf("Subject: %s\n", camel_message_info_subject(mi));
	printf("To: %s\n", camel_message_info_to(to));
	printf("Cc: %s\n", camel_message_info_cc(cc));
	printf("From: %s\n", camel_message_info_from(from));
	printf("UID: %s\n", camel_message_info_uid(uid));
	printf("Flags: %04x\n", mi->flags & 0xffff);
	content_info_dump(mi->content, 0);
}

int main(int argc, char **argv)
{
	CamelMimeParser *mp;
	int fd;
	CamelFolderSummary *s;
	char *buffer;
	int len;
	int i;
	ibex *index;

	/*g_tk_init(&argc, &argv);*/

#if 0
	{
		int i;
		char *s;
		char buf[1024];

		for (i=0;i<434712;i++) {
			memcpy(buf, "                                                         ", 50);
			buf[50] = 0;
#if 0
			s = g_strdup(buf);
			g_free(s);
#endif
		}
		return 0;
	}
#endif

	if (argc < 2 ) {
		printf("usage: %s mbox\n", argv[0]);
		return 1;
	}

	fd = open(argv[1], O_RDONLY);

	index = ibex_open("index.ibex", O_CREAT|O_RDWR, 0600);

	mp = camel_mime_parser_new();
	camel_mime_parser_scan_from(mp, TRUE);
/*	camel_mime_parser_set_header_regex(mp, "^(content-[^:]*|subject|from|to|date):");*/
	camel_mime_parser_init_with_fd(mp, fd);

	s = camel_folder_summary_new();
	camel_folder_summary_set_build_content(s, TRUE);
/*	camel_folder_summary_set_index(s, index);*/

	while (camel_mime_parser_step(mp, &buffer, &len) == HSCAN_FROM) {
		/*printf("Parsing message ...\n");*/
		camel_folder_summary_add_from_parser(s, mp);
		if (camel_mime_parser_step(mp, &buffer, &len) != HSCAN_FROM_END) {
			g_warning("Uknown state encountered, excpecting %d, got %d\n", HSCAN_FROM_END, camel_mime_parser_state(mp));
			break;
		}
	}

	printf("Printing summary\n");
	for (i=0;i<camel_folder_summary_count(s);i++) {
		CamelMessageInfo *info = camel_folder_summary_index(s, i);
		message_info_dump(info);
		camel_folder_summary_info_free(info);
	}

	printf("Saivng summary\n");
	camel_folder_summary_set_filename(s, "index.summary");
	camel_folder_summary_save(s);

	{
		CamelFolderSummary *n;

		printf("\nLoading summary\n");
		n = camel_folder_summary_new();
		camel_folder_summary_set_build_content(n, TRUE);
		camel_folder_summary_set_filename(n, "index.summary");
		camel_folder_summary_load(n);

		printf("Printing summary\n");
		for (i=0;i<camel_folder_summary_count(n);i++) {
			CamelMessageInfo *info = camel_folder_summary_index(s, i);
			message_info_dump(info);
			camel_folder_summary_info_free(info);
		}
		camel_object_unref(n);		
	}


	camel_object_unref(mp);
	camel_object_unref(s);

	printf("summarised %d messages\n", camel_folder_summary_count(s));
#if 0
	printf("g_strdup count = %d\n", strdup_count);
	printf("g_malloc count = %d\n", malloc_count);
	printf("g_free count = %d\n", free_count);
#endif
	return 0;
}

#endif
