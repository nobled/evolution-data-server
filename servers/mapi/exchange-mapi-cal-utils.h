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


#ifndef EXCHANGE_MAPI_CAL_UTILS_H
#define EXCHANGE_MAPI_CAL_UTILS_H

#include <libecal/e-cal-component.h>

#include "exchange-mapi-connection.h"
#include "exchange-mapi-defs.h"
#include "exchange-mapi-utils.h"

#include "exchange-mapi-cal-tz-utils.h"
#include "exchange-mapi-cal-recur-utils.h"

G_BEGIN_DECLS

typedef enum {
	NOT_A_MEETING = 0, 
	MEETING_OBJECT = (1 << 0),
	MEETING_OBJECT_SENT = (1 << 1),
	MEETING_REQUEST = (1 << 2), 
	MEETING_RESPONSE = (1 << 3)
} MAPIMeetingOptions;

struct cbdata { 
	ECalComponent *comp;
	struct SPropValue *props;
	gboolean is_modify;

	uint32_t msgflags;

	/* These are appt specific data */ 
	MAPIMeetingOptions meeting_type;
	uint32_t appt_id;
	uint32_t appt_seq;
	struct SBinary *globalid;
	struct SBinary *cleanglobalid;

	const char *username;
	const char *userid;
	const char *ownername;
	const char *ownerid;
};

void
exchange_mapi_cal_util_fetch_recipients (ECalComponent *comp, GSList **recip_list);
void
exchange_mapi_cal_util_fetch_attachments (ECalComponent *comp, GSList **attach_list, const char *local_store_uri);

ECalComponent *
exchange_mapi_cal_util_mapi_props_to_comp (icalcomponent_kind kind, const gchar *mid, struct mapi_SPropValue_array *properties, 
					   GSList *streams, GSList *recipients, GSList *attachments, 
					   const char *local_store_uri, const icaltimezone *default_zone);
gboolean
exchange_mapi_cal_util_build_name_id (struct mapi_nameid *nameid, gpointer data);

int
exchange_mapi_cal_util_build_props (struct SPropValue **value, struct SPropTagArray *proptag_array, gpointer data);

void
exchange_mapi_cal_util_generate_globalobjectid (gboolean is_clean, const char *uid, struct SBinary *sb);

/* we don't have to specify the PR_BODY_* tags since it is fetched by default */
static const uint32_t cal_GetPropsList[] = {
	PR_FID, 
	PR_MID, 
	PR_SUBJECT, 
	PR_SUBJECT_UNICODE, 
	PR_SUBJECT_ERROR, 
	PR_NORMALIZED_SUBJECT, 
	PR_NORMALIZED_SUBJECT_UNICODE, 
	PR_NORMALIZED_SUBJECT_ERROR, 
	PR_CREATION_TIME, 
	PR_LAST_MODIFICATION_TIME, 
	PR_PRIORITY, 
	PR_SENSITIVITY, 
	PR_START_DATE, 
	PR_END_DATE, 
	PR_RESPONSE_REQUESTED, 

	PR_SENT_REPRESENTING_NAME, 
	PR_SENT_REPRESENTING_NAME_UNICODE, 
	PR_SENT_REPRESENTING_ADDRTYPE, 
	PR_SENT_REPRESENTING_ADDRTYPE_UNICODE, 
	PR_SENT_REPRESENTING_EMAIL_ADDRESS, 
	PR_SENT_REPRESENTING_EMAIL_ADDRESS_UNICODE, 

	PR_SENDER_NAME, 
	PR_SENDER_NAME_UNICODE, 
	PR_SENDER_ADDRTYPE, 
	PR_SENDER_ADDRTYPE_UNICODE, 
	PR_SENDER_EMAIL_ADDRESS, 
	PR_SENDER_EMAIL_ADDRESS_UNICODE, 

	PR_RCVD_REPRESENTING_NAME, 
	PR_RCVD_REPRESENTING_NAME_UNICODE, 
	PR_RCVD_REPRESENTING_ADDRTYPE, 
	PR_RCVD_REPRESENTING_ADDRTYPE_UNICODE, 
	PR_RCVD_REPRESENTING_EMAIL_ADDRESS, 
	PR_RCVD_REPRESENTING_EMAIL_ADDRESS_UNICODE
};
static const uint16_t n_cal_GetPropsList = G_N_ELEMENTS (cal_GetPropsList);

static const uint32_t cal_IDList[] = {
	PR_FID, 
	PR_MID
};
static const uint16_t n_cal_IDList = G_N_ELEMENTS (cal_IDList);

G_END_DECLS

#endif