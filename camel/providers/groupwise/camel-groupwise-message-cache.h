/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-groupwise-message-cache.h: Class for message cache */

/*
 * Author:
 *   parthasarathi susarla <sparthasarathi@novell.com
 *
 *	Based on the IMAP message cache implementation by Dan Winship <danw@ximian.com>
 *
 * Copyright (C) 2001 Novell, Inc. (www.novell.com)
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


#ifndef CAMEL_GW_MESSAGE_CACHE_H
#define CAMEL_GW_MESSAGE_CACHE_H 1


#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include "camel-folder.h"
//#include "camel-groupwise-folder.h"
#include <camel/camel-folder-search.h>
#include <camel/camel-folder-summary.h>
#include <camel/camel-store.h>

#define CAMEL_GW_MESSAGE_CACHE_TYPE     (camel_groupwise_message_cache_get_type ())
#define CAMEL_GW_MESSAGE_CACHE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_GW_MESSAGE_CACHE_TYPE, CamelGroupwiseFolder))
#define CAMEL_GW_MESSAGE_CACHE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_GW_MESSAGE_CACHE_TYPE, CamelGroupwiseFolderClass))
#define CAMEL_IS_GW_MESSAGE_CACHE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_GW_MESSAGE_CACHE_TYPE))

typedef struct _CamelGroupwiseMessageCache CamelGroupwiseMessageCache ; 

struct _CamelGroupwiseMessageCache {
	CamelObject parent_object;

	char *path;
	GHashTable *parts, *cached;
	guint32 max_uid;
} ;


typedef struct {
	CamelFolderClass parent_class;

	/* Virtual methods */

} CamelGroupwiseMessageCacheClass;


/* public methods */
CamelGroupwiseMessageCache *camel_groupwise_message_cache_new (const char *path,
						CamelFolderSummary *summ,
						CamelException *ex);

void camel_groupwise_message_cache_set_path (CamelGroupwiseMessageCache *cache,
							const char *path);

guint32     camel_groupwise_message_cache_max_uid (CamelGroupwiseMessageCache *cache);

CamelStream *camel_groupwise_message_cache_insert (CamelGroupwiseMessageCache *cache,
						const char *uid,
						const char *part_spec,
						const char *data,
						int len,
						CamelException *ex);
void camel_groupwise_message_cache_insert_stream  (CamelGroupwiseMessageCache *cache,
						const char *uid,
						const char *part_spec,
						CamelStream *data_stream,
						CamelException *ex);
void camel_groupwise_message_cache_insert_wrapper (CamelGroupwiseMessageCache *cache,
						const char *uid,
						const char *part_spec,
						CamelDataWrapper *wrapper,
						CamelException *ex);

CamelStream *camel_groupwise_message_cache_get (CamelGroupwiseMessageCache *cache,
						const char *uid,
						const char *part_spec,
						CamelException *ex);

void  camel_groupwise_message_cache_remove (CamelGroupwiseMessageCache *cache,
		const char *uid);

void  camel_groupwise_message_cache_clear  (CamelGroupwiseMessageCache *cache);

void camel_groupwise_message_cache_copy   (CamelGroupwiseMessageCache *source,
						const char *source_uid,
						CamelGroupwiseMessageCache *dest,
						const char *dest_uid,
						CamelException *ex);

/* Standard Camel function */
CamelType camel_groupwise_message_cache_get_type (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_GW_MESSAGE_CACHE_H */

