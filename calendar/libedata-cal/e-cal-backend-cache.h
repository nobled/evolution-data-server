/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - generic backend class
 *
 * Copyright (C) 2003 Novell, Inc.
 *
 * Authors: Rodrigo Moya <rodrigo@ximian.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef E_CAL_BACKEND_CACHE_H
#define E_CAL_BACKEND_CACHE_H

#include <libedataserver/e-file-cache.h>
#include <libecal/e-cal-component.h>

G_BEGIN_DECLS

#define E_TYPE_CAL_BACKEND_CACHE            (e_cal_backend_cache_get_type ())
#define E_CAL_BACKEND_CACHE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_BACKEND_CACHE, ECalBackendCache))
#define E_CAL_BACKEND_CACHE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_BACKEND_CACHE, ECalBackendCacheClass))
#define E_IS_CAL_BACKEND_CACHE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_BACKEND_CACHE))
#define E_IS_CAL_BACKEND_CACHE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_BACKEND_CACHE))

typedef struct _ECalBackendCachePrivate ECalBackendCachePrivate;

typedef struct {
	EFileCache parent;
	ECalBackendCachePrivate *priv;
} ECalBackendCache;

typedef struct {
	EFileCacheClass parent_class;
} ECalBackendCacheClass;

GType               e_cal_backend_cache_get_type (void);

ECalBackendCache   *e_cal_backend_cache_new (const char *uri);
ECalComponent      *e_cal_backend_cache_get_component (ECalBackendCache *cache,
						       const char *uid,
						       const char *rid);
gboolean            e_cal_backend_cache_put_component (ECalBackendCache *cache, ECalComponent *comp);
gboolean            e_cal_backend_cache_remove_component (ECalBackendCache *cache,
							  const char *uid,
							  const char *rid);
GList              *e_cal_backend_cache_get_components (ECalBackendCache *cache);


const icaltimezone *e_cal_backend_cache_get_timezone (ECalBackendCache *cache, const char *tzid);
gboolean            e_cal_backend_cache_put_timezone (ECalBackendCache *cache, const icaltimezone *zone);
gboolean            e_cal_backend_cache_remove_timezone (ECalBackendCache *cache, const char *tzid);

gboolean            e_cal_backend_cache_put_default_timezone (ECalBackendCache *cache, icaltimezone *default_zone);
icaltimezone       *e_cal_backend_cache_get_default_timezone (ECalBackendCache *cache);

GSList             *e_cal_backend_cache_get_keys (ECalBackendCache *cache);

const char         *e_cal_backend_cache_get_marker (ECalBackendCache *cache);
void                e_cal_backend_cache_set_marker (ECalBackendCache *cache);

G_END_DECLS

#endif
