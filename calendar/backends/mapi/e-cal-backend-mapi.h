/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: 
 *    Suman Manjunath <msuman@novell.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of version 2 of the GNU Lesser General Public
 *  License as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public 
 *  License along with this program; if not, write to: 
 *  Free Software Foundation, 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 *
 */



#ifndef E_CAL_BACKEND_MAPI_H
#define E_CAL_BACKEND_MAPI_H

#include <libedata-cal/e-cal-backend.h>
#include <libedata-cal/e-cal-backend-sync.h>
#include <libedata-cal/e-cal-backend-cache.h>
#include <libedata-cal/e-cal-backend-util.h>
#include <libedata-cal/e-cal-backend-factory.h>
#include <libecal/e-cal-component.h>
#include <libecal/e-cal-util.h>
#include <libecal/e-cal-time-util.h>
#include <libedataserver/e-xml-hash-utils.h>
#include <libedataserver/e-url.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <exchange-mapi-connection.h>
#include <exchange-mapi-defs.h>
#include <exchange-mapi-folder.h>
#include <exchange-mapi-utils.h>

#if 0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

G_BEGIN_DECLS

#define E_TYPE_CAL_BACKEND_MAPI            (e_cal_backend_mapi_get_type ())
#define E_CAL_BACKEND_MAPI(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_BACKEND_MAPI,	ECalBackendMAPI))
#define E_CAL_BACKEND_MAPI_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_BACKEND_MAPI,	ECalBackendMAPIClass))
#define E_IS_CAL_BACKEND_MAPI(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_BACKEND_MAPI))
#define E_IS_CAL_BACKEND_MAPI_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_BACKEND_MAPI))

typedef struct _ECalBackendMAPI        ECalBackendMAPI;
typedef struct _ECalBackendMAPIClass   ECalBackendMAPIClass;
typedef struct _ECalBackendMAPIPrivate ECalBackendMAPIPrivate;

struct _ECalBackendMAPI {
	ECalBackendSync backend;

	/* Private data */
	ECalBackendMAPIPrivate *priv;
};

struct _ECalBackendMAPIClass {
	ECalBackendSyncClass parent_class;
};

GType	e_cal_backend_mapi_get_type(void);

const char *	
e_cal_backend_mapi_get_local_attachments_store (ECalBackendMAPI *cbmapi);

const char *	
e_cal_backend_mapi_get_owner_name (ECalBackendMAPI *cbmapi);
const char *	
e_cal_backend_mapi_get_owner_email (ECalBackendMAPI *cbmapi);

const char *	
e_cal_backend_mapi_get_user_name (ECalBackendMAPI *cbmapi);
const char *	
e_cal_backend_mapi_get_user_email (ECalBackendMAPI *cbmapi);

typedef enum {
	NOT_A_MEETING = 0, 
	MEETING_OBJECT = (1 << 0),
	MEETING_OBJECT_SENT = (1 << 1),
	MEETING_REQUEST = (1 << 2), 
	MEETING_RESPONSE = (1 << 3)
} MAPIMeetingOptions;

struct dup_data {
	struct SBinary *globalid;
	struct SBinary *cleanglobalid;
	uint32_t owner_appt_id;
	uint32_t appt_seq;
};

struct cbdata { 
	ECalBackendMAPI *cbmapi;
	ECalComponent *comp;
	MAPIMeetingOptions meeting_type;
	uint32_t msgflags;
	uint32_t new_appt_id;
	struct dup_data dup;
};

G_END_DECLS

#endif
