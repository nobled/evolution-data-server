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

#ifndef _CAMEL_FOLDER_SEARCH_H
#define _CAMEL_FOLDER_SEARCH_H

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#include <libedataserver/e-sexp.h>
#include <camel/camel-folder.h>
#include <camel/camel-object.h>
#include <camel/camel-index.h>

#define CAMEL_FOLDER_SEARCH_TYPE         (camel_folder_search_get_type ())
#define CAMEL_FOLDER_SEARCH(obj)         CAMEL_CHECK_CAST (obj, camel_folder_search_get_type (), CamelFolderSearch)
#define CAMEL_FOLDER_SEARCH_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_folder_search_get_type (), CamelFolderSearchClass)
#define CAMEL_IS_FOLDER_SEARCH(obj)      CAMEL_CHECK_TYPE (obj, camel_folder_search_get_type ())

typedef struct _CamelFolderSearch CamelFolderSearch;
typedef struct _CamelFolderSearchClass CamelFolderSearchClass;
typedef struct _CamelFolderSearchIterator CamelFolderSearchIterator;

struct _CamelFolderSearchIterator {
	CamelMessageIterator iter;

	CamelFolderSearch *search;
	CamelMessageIterator *source;
	CamelException *ex;

	char *expr;		/* only needed for error messages */
	ESExpTerm *term;

	const struct _CamelMessageInfo *current;
	struct _CamelMimeMessage *current_message;
};

struct _CamelFolderSearch {
	CamelObject parent;

	struct _CamelFolderSearchPrivate *priv;

	ESExp *sexp;		/* s-exp parser */

	CamelIndex *body_index;

#if 0
	char *last_search;	/* last searched expression */

	/* these are only valid during the search, and are reset afterwards */
	CamelFolder *folder;	/* folder for current search */
	GPtrArray *summary;	/* summary array for current search */
	GPtrArray *summary_set;	/* subset of summary to actually include in search */
	GHashTable *summary_hash; /* hashtable of summary items */
	CamelMessageInfo *current; /* current message info, when searching one by one */
	CamelMimeMessage *current_message; /* cache of current message, if required */
#endif
};

struct _CamelFolderSearchClass {
	CamelObjectClass parent_class;

	/* general bool/comparison options, usually these wont need to be set, unless it is compiling into another language */
	ESExpResult * (*and)(struct _ESExp *f, int argc, struct _ESExpTerm **argv, CamelFolderSearchIterator *s);
	ESExpResult * (*or)(struct _ESExp *f, int argc, struct _ESExpTerm **argv, CamelFolderSearchIterator *s);
	ESExpResult * (*not)(struct _ESExp *f, int argc, struct _ESExpResult **argv, CamelFolderSearchIterator *s);
	ESExpResult * (*lt)(struct _ESExp *f, int argc, struct _ESExpTerm **argv, CamelFolderSearchIterator *s);
	ESExpResult * (*gt)(struct _ESExp *f, int argc, struct _ESExpTerm **argv, CamelFolderSearchIterator *s);
	ESExpResult * (*eq)(struct _ESExp *f, int argc, struct _ESExpTerm **argv, CamelFolderSearchIterator *s);

	/* search options */
	/* (match-all [boolean expression]) Apply match to all messages */
	ESExpResult * (*match_all)(struct _ESExp *f, int argc, struct _ESExpTerm **argv, CamelFolderSearchIterator *s);

	/* (match-threads "type" [array expression]) add all related threads */
	ESExpResult * (*match_threads)(struct _ESExp *f, int argc, struct _ESExpTerm **argv, CamelFolderSearchIterator *s);

	/* (body-contains "string1" "string2" ...) Returns a list of matches, or true if in single-message mode */
	ESExpResult * (*body_contains)(struct _ESExp *f, int argc, struct _ESExpResult **argv, CamelFolderSearchIterator *s);

	/* (header-contains "headername" "string1" ...) List of matches, or true if in single-message mode */
	ESExpResult * (*header_contains)(struct _ESExp *f, int argc, struct _ESExpResult **argv, CamelFolderSearchIterator *s);
	
	/* (header-matches "headername" "string") */
	ESExpResult * (*header_matches)(struct _ESExp *f, int argc, struct _ESExpResult **argv, CamelFolderSearchIterator *s);
	
	/* (header-starts-with "headername" "string") */
	ESExpResult * (*header_starts_with)(struct _ESExp *f, int argc, struct _ESExpResult **argv, CamelFolderSearchIterator *s);
	
	/* (header-ends-with "headername" "string") */
	ESExpResult * (*header_ends_with)(struct _ESExp *f, int argc, struct _ESExpResult **argv, CamelFolderSearchIterator *s);
	
	/* (header-exists "headername") */
	ESExpResult * (*header_exists)(struct _ESExp *f, int argc, struct _ESExpResult **argv, CamelFolderSearchIterator *s);
	
	/* (user-flag "flagname" "flagname" ...) If one of user-flag set */
	ESExpResult * (*user_flag)(struct _ESExp *f, int argc, struct _ESExpResult **argv, CamelFolderSearchIterator *s);

	/* (user-tag "flagname") Returns the value of a user tag.  Can only be used in match-all */
	ESExpResult * (*user_tag)(struct _ESExp *f, int argc, struct _ESExpResult **argv, CamelFolderSearchIterator *s);
	
	/* (system-flag "flagname") Returns the value of a system flag.  Can only be used in match-all */
	ESExpResult * (*system_flag)(struct _ESExp *f, int argc, struct _ESExpResult **argv, CamelFolderSearchIterator *s);
	
	/* (get-sent-date) Retrieve the date that the message was sent on as a time_t */
	ESExpResult * (*get_sent_date)(struct _ESExp *f, int argc, struct _ESExpResult **argv, CamelFolderSearchIterator *s);

	/* (get-received-date) Retrieve the date that the message was received on as a time_t */
	ESExpResult * (*get_received_date)(struct _ESExp *f, int argc, struct _ESExpResult **argv, CamelFolderSearchIterator *s);

	/* (get-current-date) Retrieve 'now' as a time_t */
	ESExpResult * (*get_current_date)(struct _ESExp *f, int argc, struct _ESExpResult **argv, CamelFolderSearchIterator *s);

	/* (get-size) Retrieve message size as an int (in kilobytes) */
	ESExpResult * (*get_size)(struct _ESExp *f, int argc, struct _ESExpResult **argv, CamelFolderSearchIterator *s);

	/* (uid "uid" ...) True if the uid is in the list */
	ESExpResult * (*uid)(struct _ESExp *f, int argc, struct _ESExpResult **argv, CamelFolderSearchIterator *s);
};

CamelType		camel_folder_search_get_type	(void);
CamelFolderSearch      *camel_folder_search_new	(void);
void camel_folder_search_construct (CamelFolderSearch *search);

CamelFolderSearchIterator *camel_folder_search_search(CamelFolderSearch *, const char *expr, CamelMessageIterator *iter, CamelException *ex);

void camel_folder_search_set_body_index(CamelFolderSearch *search, CamelIndex *index);

int camel_folder_search_is_static(CamelFolderSearch *search, const char *expr, CamelException *ex);

#if 0
/* This stuff currently gets cleared when you run a search ... what on earth was i thinking ... */
void camel_folder_search_set_folder(CamelFolderSearch *search, CamelFolder *folder);
void camel_folder_search_set_summary(CamelFolderSearch *search, GPtrArray *summary);
/* this interface is deprecated */
GPtrArray *camel_folder_search_execute_expression(CamelFolderSearch *search, const char *expr, CamelException *ex);

GPtrArray *camel_folder_search_search(CamelFolderSearch *search, const char *expr, GPtrArray *uids, CamelException *ex);
void camel_folder_search_free_result(CamelFolderSearch *search, GPtrArray *);
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ! _CAMEL_FOLDER_SEARCH_H */
