/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-nntp-store.h : class for an nntp store */

/* 
 *
 * Copyright (C) 2000 Helix Code, Inc. <toshok@helixcode.com>
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


#ifndef CAMEL_NNTP_STORE_H
#define CAMEL_NNTP_STORE_H 1


#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include "camel-store.h"
#include "camel-nntp-newsrc.h"

#define CAMEL_NNTP_STORE_TYPE     (camel_nntp_store_get_type ())
#define CAMEL_NNTP_STORE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_NNTP_STORE_TYPE, CamelNNTPStore))
#define CAMEL_NNTP_STORE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_NNTP_STORE_TYPE, CamelNNTPStoreClass))
#define IS_CAMEL_NNTP_STORE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_NNTP_STORE_TYPE))


typedef struct {
	CamelStore parent_object;	

#define CAMEL_NNTP_EXT_XOVER 0x01
	guint32 extensions;

	CamelNNTPNewsrc *newsrc;

	CamelStream *istream, *ostream;
} CamelNNTPStore;



typedef struct {
	CamelStoreClass parent_class;

} CamelNNTPStoreClass;


/* public methods */
void camel_nntp_store_open (CamelNNTPStore *store, CamelException *ex);
void camel_nntp_store_close (CamelNNTPStore *store, gboolean expunge,
			     CamelException *ex);

void camel_nntp_store_subscribe_group (CamelStore *store, const gchar *group_name);
void camel_nntp_store_unsubscribe_group (CamelStore *store, const gchar *group_name);
GList *camel_nntp_store_list_subscribed_groups(CamelStore *store);

gchar *camel_nntp_store_get_toplevel_dir (CamelNNTPStore *store);

/* support functions */
enum { CAMEL_NNTP_OK, CAMEL_NNTP_ERR, CAMEL_NNTP_FAIL };
int camel_nntp_command (CamelNNTPStore *store, char **ret, char *fmt, ...);
char *camel_nntp_command_get_additional_data (CamelNNTPStore *store);

/* Standard Camel function */
CamelType camel_nntp_store_get_type (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_NNTP_STORE_H */


