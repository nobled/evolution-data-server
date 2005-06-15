/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
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

#ifndef _CAMEL_FOLDER_SUMMARY_H
#define _CAMEL_FOLDER_SUMMARY_H

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#include <time.h>
#include <camel/camel-object.h>
#include <libedataserver/e-msgport.h>

struct _CamelFolder;
struct _CamelMimeParser;
struct _CamelMimePart;
struct _camel_header_raw;

#define CAMEL_FOLDER_SUMMARY_TYPE         camel_folder_summary_get_type ()
#define CAMEL_FOLDER_SUMMARY(obj)         CAMEL_CHECK_CAST (obj, camel_folder_summary_get_type (), CamelFolderSummary)
#define CAMEL_FOLDER_SUMMARY_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_folder_summary_get_type (), CamelFolderSummaryClass)
#define CAMEL_IS_FOLDER_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_folder_summary_get_type ())

/*typedef struct _CamelFolderSummary      CamelFolderSummary;*/
typedef struct _CamelFolderSummaryClass CamelFolderSummaryClass;
typedef struct _CamelFolderView CamelFolderView;

typedef struct _CamelMessageInfo CamelMessageInfo;
typedef struct _CamelMessageIterator CamelMessageIterator;

/* system flag bits */
typedef enum _CamelMessageFlags {
	CAMEL_MESSAGE_ANSWERED = 1<<0,
	CAMEL_MESSAGE_DELETED = 1<<1,
	CAMEL_MESSAGE_DRAFT = 1<<2,
	CAMEL_MESSAGE_FLAGGED = 1<<3,
	CAMEL_MESSAGE_SEEN = 1<<4,
	
	/* these aren't really system flag bits, but are convenience flags */
	CAMEL_MESSAGE_ATTACHMENTS = 1<<5,
	CAMEL_MESSAGE_ANSWERED_ALL = 1<<6,
	CAMEL_MESSAGE_JUNK = 1<<7,
	CAMEL_MESSAGE_SECURE = 1<<8,
	
	/* following flags are for the folder, and are not really permanent flags */
	CAMEL_MESSAGE_FOLDER_FLAGGED = 1<<16, /* for use by the folder implementation */

	/* flags after 1<<16 are used by camel providers,
           if adding non permanent flags, add them to the end  */

	CAMEL_MESSAGE_JUNK_LEARN = 1<<30, /* used when setting CAMEL_MESSAGE_JUNK flag
					     to say that we request junk plugin
					     to learn that message as junk/non junk */
	CAMEL_MESSAGE_USER = 1<<31 /* supports user flags */
} CamelMessageFlags;

/* Changes to system flags will NOT trigger a folder changed event */
#define CAMEL_MESSAGE_SYSTEM_MASK (0xffff << 16)

typedef struct _CamelFlag {
	struct _CamelFlag *next;
	char name[1];		/* name allocated as part of the structure */
} CamelFlag;

typedef struct _CamelTag {
	struct _CamelTag *next;
	char *value;
	char name[1];		/* name allocated as part of the structure */
} CamelTag;

/* a summary messageid is a 64 bit identifier (partial md5 hash) */
typedef struct _CamelSummaryMessageID {
	union {
		guint64 id;
		unsigned char hash[8];
		struct {
			guint32 hi;
			guint32 lo;
		} part;
	} id;
} CamelSummaryMessageID;

/* summary references is a fixed size array of references */
typedef struct _CamelSummaryReferences {
	int size;
	CamelSummaryMessageID references[1];
} CamelSummaryReferences;

/* accessor id's */
enum {
	CAMEL_MESSAGE_INFO_SUBJECT,
	CAMEL_MESSAGE_INFO_FROM,
	CAMEL_MESSAGE_INFO_TO,
	CAMEL_MESSAGE_INFO_CC,
	CAMEL_MESSAGE_INFO_MLIST,

	CAMEL_MESSAGE_INFO_FLAGS,
	CAMEL_MESSAGE_INFO_SIZE,

	CAMEL_MESSAGE_INFO_DATE_SENT,
	CAMEL_MESSAGE_INFO_DATE_RECEIVED,

	CAMEL_MESSAGE_INFO_MESSAGE_ID,
	CAMEL_MESSAGE_INFO_REFERENCES,
	CAMEL_MESSAGE_INFO_USER_FLAGS,
	CAMEL_MESSAGE_INFO_USER_TAGS,

	CAMEL_MESSAGE_INFO_LAST
};

/* CamelMessageInfo
 * @summary: parent summary object
 * @refcount: refcounting
 * @uid: uid (to be moved to subclasses?)
 *
 * The CamelMessageInfo is an abstract boxed object, managed by a given
 * CamelFolderSummary concrete class.  It stores information about
 * each message in a folder.
 */
struct _CamelMessageInfo {
	CamelFolderSummary *summary;

	guint32 refcount;	/* ??? */
	char *uid;

	/* subclasses add additional fields as required */
};

/* Used by the virtual methods if they are called, use
   to save some work in a subclass if you store intermediate data in memory */
typedef struct _CamelMessageInfoBase CamelMessageInfoBase;
struct _CamelMessageInfoBase {
	CamelFolderSummary *summary;

	guint32 refcount;	/* ??? */
	char *uid;

	const char *subject;
	const char *from;
	const char *to;
	const char *cc;
	const char *mlist;

	guint32 flags;
	guint32 size;

	time_t date_sent;
	time_t date_received;

	CamelSummaryMessageID message_id;
	CamelSummaryReferences *references;/* from parent to root */

	struct _CamelFlag *user_flags;
	struct _CamelTag *user_tags;

	/* subclasses add additional fields as required */
};

/* probably do this as well, removing CamelFolderChangeInfo and interfaces 
typedef struct _CamelChangeInfo CamelChangeInfo;
struct _CamelChangeInfo {
	GPtrArray *added;
	GPtrArray *removed;
	GPtrArray *changed;
	GPtrArray *recent;
};
*/

/* Message-iterators are non-reffed pseudo-objects a little like a type-less interface 
   TODO: Rename this to CamelIterator, it has uses beyond message lists */
typedef struct _CamelMessageIteratorVTable CamelMessageIteratorVTable;

struct _CamelMessageIterator {
	CamelMessageIteratorVTable *klass;

	/* subclass adds new fields afterwards */
};

struct _CamelMessageIteratorVTable {
	/* free fields, dont free base object */
	void (*free)(void *it);
	/* go to the next messageinfo */
	const struct _CamelMessageInfo *(*next)(void *it, CamelException *ex);
	/* go back to the start */
	void (*reset)(void *it);
};

/* view stuff, the new vfolders */
struct _CamelFolderView {
	struct _CamelFolderView *next;
	struct _CamelFolderView *prev;

	/* ref them for threading purposes */
	guint32 refcount:30;
	/* set when we're deleted, so we can abort/do the right thing if we have open cursors */
	guint32 deleted:1;
	guint32 is_static:1;

	CamelFolderSummary *summary;

	char *vid;		/* unique id for all time for this view */
	char *expr;
	/* This is not a real iterator, but is used in one-shot search mode */
	struct _CamelFolderSearchIterator *iter;

	struct _CamelFolderChangeInfo *changes;

	/* handy totals */
	guint32 total_count;
	guint32 visible_count;
	guint32 unread_count;
	guint32 deleted_count;
	guint32 junk_count;

	/* subclasses may add additional fields ... */
};

struct _CamelFolderSummary {
	CamelObject parent;

	struct _CamelFolderSummaryPrivate *priv;

	struct _CamelFolder *folder; /* parent folder, for events, not reffed */

	/* needs locking - TBD */
	struct _CamelFolderSearch *search;

	/* the root view contains all messages, and always exists */
	/* It must be created by the implementation ! */
	struct _CamelFolderView *root_view;
	EDList views;
};

struct _CamelFolderSummaryClass {
	CamelObjectClass parent_class;

	/* sizes of allocated objects */
	int view_sizeof;

	/* comparison functions used to sort data items in summary order, compare uid's or compare messageinfos */
	GCompareDataFunc uid_cmp;
	GCompareDataFunc info_cmp;

	/* the underlying folder is being renamed */
	int (*rename)(CamelFolderSummary *, const char *newname);

	/* summary management, abstract virtual */
	int (*add)(CamelFolderSummary *, CamelMessageInfo *);
	/*  base implements naive implementation */
	int (*add_array)(CamelFolderSummary *, GPtrArray *);

	int (*remove)(CamelFolderSummary *, CamelMessageInfo *);
	/*  base implements naive implementation */
	int (*remove_array)(CamelFolderSummary *, GPtrArray *);

	void (*clear)(CamelFolderSummary *);

	/* summary lookup & query, abstract virtual */
	CamelMessageInfo * (*get)(CamelFolderSummary *, const char *uid);
	/*  base implements naive implementation */
	GPtrArray *(*get_array)(CamelFolderSummary *, const GPtrArray *uids);

	/* the master iterator/search/view interface */
	struct _CamelMessageIterator *(*search)(CamelFolderSummary *, const char *vid, const char *expr, CamelMessageIterator *, CamelException *ex);

	/* view management */
	void (*view_free)(CamelFolderSummary *, CamelFolderView *);
	CamelFolderView *(*view_create)(CamelFolderSummary *, const char *vid, const char *expr, CamelException *ex);
	void (*view_delete)(CamelFolderSummary *, CamelFolderView *);

	/* messageinfo alloc/copy/free, base class works on MessageInfoBase's */
	CamelMessageInfo * (*message_info_alloc)(CamelFolderSummary *);
	CamelMessageInfo * (*message_info_clone)(const CamelMessageInfo *);

	void (*message_info_free)(CamelMessageInfo *);
	/*  base implements naive implementation */
	void (*message_info_free_array)(CamelFolderSummary *, GPtrArray *);

	/* construction functions, base works on MessageInfoBase's */
	CamelMessageInfo * (*message_info_new_from_header)(CamelFolderSummary *, struct _camel_header_raw *);
	CamelMessageInfo * (*message_info_new_from_parser)(CamelFolderSummary *, struct _CamelMimeParser *);
	CamelMessageInfo * (*message_info_new_from_message)(CamelFolderSummary *, struct _CamelMimeMessage *, const CamelMessageInfo *info);

	/* virtual accessors on messageinfo's, base works on MessageInfoBase's */
	const void *(*info_ptr)(const CamelMessageInfo *mi, int id);
	guint32     (*info_uint32)(const CamelMessageInfo *mi, int id);
	time_t      (*info_time)(const CamelMessageInfo *mi, int id);

	gboolean    (*info_user_flag)(const CamelMessageInfo *mi, const char *id);
	const char *(*info_user_tag)(const CamelMessageInfo *mi, const char *id);

	/* set accessors for the modifyable bits, base works on MessageInfoBase's */
	gboolean (*info_set_user_flag)(CamelMessageInfo *mi, const char *id, gboolean state);
	gboolean (*info_set_user_tag)(CamelMessageInfo *mi, const char *id, const char *val);
	gboolean (*info_set_flags)(CamelMessageInfo *mi, guint32 mask, guint32 set);
};

CamelType			 camel_folder_summary_get_type	(void);
CamelFolderSummary      *camel_folder_summary_new	(struct _CamelFolder *folder);

int camel_folder_summary_rename(CamelFolderSummary *, const char *newname);

/* summary management */
int camel_folder_summary_add(CamelFolderSummary *summary, CamelMessageInfo *info);
int camel_folder_summary_add_array(CamelFolderSummary *summary, GPtrArray *infos);
int camel_folder_summary_remove(CamelFolderSummary *summary, CamelMessageInfo *info);
int camel_folder_summary_remove_array(CamelFolderSummary *summary, GPtrArray *infos);

/* retrieve items */
CamelMessageInfo *camel_folder_summary_get(CamelFolderSummary *, const char *uid);
GPtrArray *camel_folder_summary_get_array(CamelFolderSummary *, const GPtrArray *uids);

void camel_folder_summary_free_array(CamelFolderSummary *summary, GPtrArray *array);

/* search/iterator interface */
CamelMessageIterator *camel_folder_summary_search(CamelFolderSummary *summary, const char *viewid, const char *expr, CamelMessageIterator *subset, CamelException *ex);

CamelFolderView *camel_folder_summary_view_lookup(CamelFolderSummary *s, const char *vid);
void camel_folder_summary_view_unref(CamelFolderView *v);
const CamelFolderView *camel_folder_summary_view_create(CamelFolderSummary *s, const char *vid, const char *expr, CamelException *ex);
void camel_folder_summary_view_delete(CamelFolderSummary *s, const char *vid);

/* remove all items */
void camel_folder_summary_clear(CamelFolderSummary *summary);

/* Summary may be null, in which case internal structure is used to track it */
/* Use anonymous pointers to avoid tons of cast crap */
void *camel_message_info_new(CamelFolderSummary *summary);
void *camel_message_info_new_from_header(CamelFolderSummary *summary, struct _camel_header_raw *header);
void *camel_message_info_new_from_parser(CamelFolderSummary *summary, struct _CamelMimeParser *parser);
void *camel_message_info_new_from_message(CamelFolderSummary *summary, struct _CamelMimeMessage *message, const CamelMessageInfo *base);

void *camel_message_info_clone(const void *info);

void camel_message_info_ref(void *info);
void camel_message_info_free(void *info);

/* accessors */
const void *camel_message_info_ptr(const CamelMessageInfo *mi, int id);
guint32 camel_message_info_uint32(const CamelMessageInfo *mi, int id);
time_t camel_message_info_time(const CamelMessageInfo *mi, int id);

const CamelFolder *camel_message_info_folder(const void *mi);

#define camel_message_info_uid(mi) ((const char *)((const CamelMessageInfo *)mi)->uid)

#define camel_message_info_subject(mi) ((const char *)camel_message_info_ptr((const CamelMessageInfo *)mi, CAMEL_MESSAGE_INFO_SUBJECT))
#define camel_message_info_from(mi) ((const char *)camel_message_info_ptr((const CamelMessageInfo *)mi, CAMEL_MESSAGE_INFO_FROM))
#define camel_message_info_to(mi) ((const char *)camel_message_info_ptr((const CamelMessageInfo *)mi, CAMEL_MESSAGE_INFO_TO))
#define camel_message_info_cc(mi) ((const char *)camel_message_info_ptr((const CamelMessageInfo *)mi, CAMEL_MESSAGE_INFO_CC))
#define camel_message_info_mlist(mi) ((const char *)camel_message_info_ptr((const CamelMessageInfo *)mi, CAMEL_MESSAGE_INFO_MLIST))

#define camel_message_info_flags(mi) camel_message_info_uint32((const CamelMessageInfo *)mi, CAMEL_MESSAGE_INFO_FLAGS)
#define camel_message_info_size(mi) camel_message_info_uint32((const CamelMessageInfo *)mi, CAMEL_MESSAGE_INFO_SIZE)

#define camel_message_info_date_sent(mi) camel_message_info_time((const CamelMessageInfo *)mi, CAMEL_MESSAGE_INFO_DATE_SENT)
#define camel_message_info_date_received(mi) camel_message_info_time((const CamelMessageInfo *)mi, CAMEL_MESSAGE_INFO_DATE_RECEIVED)

#define camel_message_info_message_id(mi) ((const CamelSummaryMessageID *)camel_message_info_ptr((const CamelMessageInfo *)mi, CAMEL_MESSAGE_INFO_MESSAGE_ID))
#define camel_message_info_references(mi) ((const CamelSummaryReferences *)camel_message_info_ptr((const CamelMessageInfo *)mi, CAMEL_MESSAGE_INFO_REFERENCES))
#define camel_message_info_user_flags(mi) ((const CamelFlag *)camel_message_info_ptr((const CamelMessageInfo *)mi, CAMEL_MESSAGE_INFO_USER_FLAGS))
#define camel_message_info_user_tags(mi) ((const CamelTag *)camel_message_info_ptr((const CamelMessageInfo *)mi, CAMEL_MESSAGE_INFO_USER_TAGS))

gboolean camel_message_info_user_flag(const CamelMessageInfo *mi, const char *id);
const char *camel_message_info_user_tag(const CamelMessageInfo *mi, const char *id);

gboolean camel_message_info_set_flags(CamelMessageInfo *mi, guint32 mask, guint32 set);
gboolean camel_message_info_set_user_flag(CamelMessageInfo *mi, const char *id, gboolean state);
gboolean camel_message_info_set_user_tag(CamelMessageInfo *mi, const char *id, const char *val);

void camel_message_info_dump (CamelMessageInfo *mi);

char *camel_message_info_format_address(struct _camel_header_raw *h, const char *name, const char *charset);
char *camel_message_info_format_string(struct _camel_header_raw *h, const char *name, const char *charset);

/* message flag operations */
gboolean camel_flag_get(CamelFlag **list, const char *name);
gboolean camel_flag_set(CamelFlag **list, const char *name, gboolean state);
gboolean camel_flag_list_copy(CamelFlag **to, CamelFlag **from);
int camel_flag_list_size(CamelFlag **list);
void camel_flag_list_free(CamelFlag **list);

guint32 camel_system_flag (const char *name);
gboolean camel_system_flag_get (guint32 flags, const char *name);

/* message tag operations */
const char *camel_tag_get(CamelTag **list, const char *name);
gboolean camel_tag_set(CamelTag **list, const char *name, const char *value);
gboolean camel_tag_list_copy(CamelTag **to, CamelTag **from);
int camel_tag_list_size(CamelTag **list);
void camel_tag_list_free(CamelTag **list);

/* Iterator may not be NULL, it tracks its own parent summary */
void *camel_message_iterator_new(CamelMessageIteratorVTable *klass, size_t size);
void camel_message_iterator_free(void *it);
const CamelMessageInfo *camel_message_iterator_next(void *it, CamelException *ex);
void camel_message_iterator_reset(void *it);

/* helpers */
void *camel_message_iterator_infos_new(GPtrArray *mis, int freeit);
void *camel_message_iterator_uids_new(CamelFolder *source, GPtrArray *uids, int freeit);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ! _CAMEL_FOLDER_SUMMARY_H */
