/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *  camel-private.h: Private info for class implementers.
 *
 * Authors: Michael Zucchi <notzed@helixcode.com>
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

#ifndef CAMEL_PRIVATE_H
#define CAMEL_PRIVATE_H 1

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

/* need a way to configure and save this data, if this header is to
   be installed.  For now, dont install it */

#include "config.h"

#ifdef ENABLE_THREADS
#include <pthread.h>
#endif

struct _CamelFolderPrivate {
#ifdef ENABLE_THREADS
	GMutex *lock;
	GMutex *change_lock;
#endif

	/* must require the 'change_lock' to access this */
	int frozen;
	struct _CamelFolderChangeInfo *changed_frozen; /* queues changed events */
};

#ifdef ENABLE_THREADS
#define CAMEL_FOLDER_LOCK(f, l) (g_mutex_lock(((CamelFolder *)f)->priv->l))
#define CAMEL_FOLDER_UNLOCK(f, l) (g_mutex_unlock(((CamelFolder *)f)->priv->l))
#else
#define CAMEL_FOLDER_LOCK(f, l)
#define CAMEL_FOLDER_UNLOCK(f, l)
#endif

struct _CamelStorePrivate {
#ifdef ENABLE_THREADS
	GMutex *folder_lock;	/* for locking folder operations */
	GMutex *cache_lock;	/* for locking access to the cache */
#endif
};

#ifdef ENABLE_THREADS
#define CAMEL_STORE_LOCK(f, l) (g_mutex_lock(((CamelStore *)f)->priv->l))
#define CAMEL_STORE_UNLOCK(f, l) (g_mutex_unlock(((CamelStore *)f)->priv->l))
#else
#define CAMEL_STORE_LOCK(f, l)
#define CAMEL_STORE_UNLOCK(f, l)
#endif

struct _CamelServicePrivate {
#ifdef ENABLE_THREADS
	GMutex *connect_lock;	/* for locking connection operations */
#endif
};

#ifdef ENABLE_THREADS
#define CAMEL_SERVICE_LOCK(f, l) (g_mutex_lock(((CamelService *)f)->priv->l))
#define CAMEL_SERVICE_UNLOCK(f, l) (g_mutex_unlock(((CamelService *)f)->priv->l))
#else
#define CAMEL_SERVICE_LOCK(f, l)
#define CAMEL_SERVICE_UNLOCK(f, l)
#endif

struct _CamelSessionPrivate {
#ifdef ENABLE_THREADS
	GMutex *lock;		/* for locking everything basically */
#endif
};

#ifdef ENABLE_THREADS
#define CAMEL_SESSION_LOCK(f, l) (g_mutex_lock(((CamelSession *)f)->priv->l))
#define CAMEL_SESSION_UNLOCK(f, l) (g_mutex_unlock(((CamelSession *)f)->priv->l))
#else
#define CAMEL_SESSION_LOCK(f, l)
#define CAMEL_SESSION_UNLOCK(f, l)
#endif


struct _CamelRemoteStorePrivate {
#ifdef ENABLE_THREADS
	GMutex *stream_lock;	/* for locking stream operations */
#endif
};

#ifdef ENABLE_THREADS
#define CAMEL_REMOTE_STORE_LOCK(f, l) (g_mutex_lock(((CamelRemoteStore *)f)->priv->l))
#define CAMEL_REMOTE_STORE_UNLOCK(f, l) (g_mutex_unlock(((CamelRemoteStore *)f)->priv->l))
#else
#define CAMEL_REMOTE_STORE_LOCK(f, l)
#define CAMEL_REMOTE_STORE_UNLOCK(f, l)
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_H */

