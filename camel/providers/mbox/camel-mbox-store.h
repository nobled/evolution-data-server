/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-mbox-store.h : class for an mbox store */

/* 
 *
 * Copyright (C) 2000 Helix Code, Inc. <bertrand@helixcode.com>
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


#ifndef CAMEL_MBOX_STORE_H
#define CAMEL_MBOX_STORE_H 1


#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include "camel-store.h"

#define CAMEL_MBOX_STORE_TYPE     (camel_mbox_store_get_type ())
#define CAMEL_MBOX_STORE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_MBOX_STORE_TYPE, CamelMboxStore))
#define CAMEL_MBOX_STORE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_MBOX_STORE_TYPE, CamelMboxStoreClass))
#define IS_CAMEL_MBOX_STORE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_MBOX_STORE_TYPE))


typedef struct {
	CamelStore parent_object;	
	
} CamelMboxStore;



typedef struct {
	CamelStoreClass parent_class;

} CamelMboxStoreClass;


/* public methods */

/* Standard Camel function */
CamelType camel_mbox_store_get_type (void);

const gchar *camel_mbox_store_get_toplevel_dir (CamelMboxStore *store);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_MBOX_STORE_H */


