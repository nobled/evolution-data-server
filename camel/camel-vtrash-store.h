/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2002 Ximian Inc.
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


#ifndef _CAMEL_VTRASH_STORE_H
#define _CAMEL_VTRASH_STORE_H

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#include <glib.h>
#include <camel/camel-store.h>

#define CAMEL_VTRASH_STORE(obj)         CAMEL_CHECK_CAST (obj, camel_vtrash_store_get_type (), CamelVTrashStore)
#define CAMEL_VTRASH_STORE_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_vtrash_store_get_type (), CamelVTrashStoreClass)
#define CAMEL_IS_VTRASH_STORE(obj)      CAMEL_CHECK_TYPE (obj, camel_vtrash_store_get_type ())

typedef struct _CamelVTrashStore      CamelVTrashStore;
typedef struct _CamelVTrashStoreClass CamelVTrashStoreClass;

struct _CamelVTrashStore {
	CamelStore parent;

	struct _CamelVTrashStorePrivate *priv;
};

struct _CamelVTrashStoreClass {
	CamelStoreClass parent_class;
};

CamelType		camel_vtrash_store_get_type	(void);
CamelVTrashStore      *camel_vtrash_store_new	(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ! _CAMEL_VTRASH_STORE_H */
