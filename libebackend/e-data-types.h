/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2006 OpenedHand Ltd
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of version 2.1 of the GNU Lesser General Public License as
 * published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Authors:
 *   Chris Toshok <toshok@ximian.com>
 *   Ross Burton <ross@linux.intel.com>
 *   Travis Reitter <travis.reitter@collabora.co.uk>
 */

#ifndef __E_DATA_TYPES_H__
#define __E_DATA_TYPES_H__

G_BEGIN_DECLS

typedef struct _EDataView        EDataView;
typedef struct _EDataViewClass   EDataViewClass;

typedef struct _EBackend        EBackend;
typedef struct _EBackendClass   EBackendClass;

typedef struct _EBackendSExp        EBackendSExp;
typedef struct _EBackendSExpClass   EBackendSExpClass;

typedef struct _EData        EData;
typedef struct _EDataClass   EDataClass;

/* FIXME: do translation for these to the class-specific types */
typedef enum {
	E_DATA_STATUS_SUCCESS,
	E_DATA_STATUS_REPOSITORY_OFFLINE,
	E_DATA_STATUS_PERMISSION_DENIED,
	E_DATA_STATUS_CONTACT_NOT_FOUND,
	E_DATA_STATUS_CONTACTID_ALREADY_EXISTS,
	E_DATA_STATUS_AUTHENTICATION_FAILED,
	E_DATA_STATUS_AUTHENTICATION_REQUIRED,
	E_DATA_STATUS_UNSUPPORTED_FIELD,
	E_DATA_STATUS_UNSUPPORTED_AUTHENTICATION_METHOD,
	E_DATA_STATUS_TLS_NOT_AVAILABLE,
	E_DATA_STATUS_NO_SUCH_DATA,
	E_DATA_STATUS_REMOVED,
	E_DATA_STATUS_OFFLINE_UNAVAILABLE,
	E_DATA_STATUS_SEARCH_SIZE_LIMIT_EXCEEDED,
	E_DATA_STATUS_SEARCH_TIME_LIMIT_EXCEEDED,
	E_DATA_STATUS_INVALID_QUERY,
	E_DATA_STATUS_QUERY_REFUSED,
	E_DATA_STATUS_COULD_NOT_CANCEL,
	E_DATA_STATUS_OTHER_ERROR,
	E_DATA_STATUS_INVALID_SERVER_VERSION,
	E_DATA_STATUS_NO_SPACE,
} EDataStatus;

typedef enum {
	E_DATA_MODE_LOCAL,
	E_DATA_MODE_REMOTE,
	E_DATA_MODE_ANY,
} EDataMode;

G_END_DECLS

#endif /* __E_DATA_TYPES_H__ */
