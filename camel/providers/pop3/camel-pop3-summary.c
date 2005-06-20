/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright(C) 2005 Novell Inc.
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include <camel/camel-record.h>
#include "camel-pop3-summary.h"
#include "camel-pop3-store.h"

static CamelFolderSummaryDiskClass *pop3_parent;

CamelFolderSummary *
camel_pop3_summary_new(struct _CamelFolder *folder)
{
	CamelFolderSummary *s = (CamelFolderSummary *)camel_object_new(camel_pop3_summary_get_type());

	/* TODO: we want to override the creation of the trash/junk views.
	   We definitely dont support junk at least */
	camel_folder_summary_disk_construct((CamelFolderSummaryDisk *)s, folder);

	return s;
}

/* CamelFolderSummary overrides */
static int
pop3_uid_cmp(const void *ap, const void *bp, void *d)
{
	/* So, we dont really know what the uid will be from
	   pop, so we have to find some better way to define
	   the uid so it can be sorted properly ... ho hum */
	return strcmp((char *)ap, (char *)bp);
}

static void
pop3_message_info_free(CamelMessageInfo *mi)
{
	g_free(mi->uid);
	g_free(mi);
}

static const void *
pop3_info_ptr(const CamelMessageInfo *mi, int id)
{
	switch (id) {
	case CAMEL_MESSAGE_INFO_SUBJECT:
	case CAMEL_MESSAGE_INFO_FROM:
	case CAMEL_MESSAGE_INFO_TO:
	case CAMEL_MESSAGE_INFO_CC:
	case CAMEL_MESSAGE_INFO_MLIST:
		return "";
	case CAMEL_MESSAGE_INFO_MESSAGE_ID:
	case CAMEL_MESSAGE_INFO_REFERENCES:
	case CAMEL_MESSAGE_INFO_USER_FLAGS:
	case CAMEL_MESSAGE_INFO_USER_TAGS:
		return NULL;
	default:
		abort();
	}
}

static guint32
pop3_info_uint32(const CamelMessageInfo *mi, int id)
{
	switch (id) {
	case CAMEL_MESSAGE_INFO_FLAGS:
		return ((const CamelPOP3MessageInfo *)mi)->flags;
	case CAMEL_MESSAGE_INFO_SIZE:
		return ((const CamelPOP3MessageInfo *)mi)->size;
	default:
		abort();
	}
}

static time_t
pop3_info_time(const CamelMessageInfo *mi, int id)
{
	switch (id) {
	case CAMEL_MESSAGE_INFO_DATE_SENT:
	case CAMEL_MESSAGE_INFO_DATE_RECEIVED:
		return ((const CamelPOP3MessageInfo *)mi)->date;
	default:
		abort();
	}
}

static gboolean
pop3_info_user_flag(const CamelMessageInfo *mi, const char *id)
{
	return FALSE;
}

static const char *
pop3_info_user_tag(const CamelMessageInfo *mi, const char *id)
{
	return NULL;
}

static gboolean
pop3_info_set_flags(CamelMessageInfo *info, guint32 mask, guint32 set)
{
	guint32 old, diff;
	CamelPOP3MessageInfo *mi = (CamelPOP3MessageInfo *)info;
	CamelFolderView *v = info->summary->root_view;

	old = mi->flags;
	mask &= CAMEL_POP3_SERVER_FLAGS;
	diff = (set ^ old) & mask;
	if (diff) {
		mi->flags = (old & ~mask) | (set & mask);

		v->touched = 1;
		if (diff & CAMEL_MESSAGE_DELETED) {
			if (set & CAMEL_MESSAGE_DELETED) {
				v->visible_count--;
				v->deleted_count++;
			} else {
				v->visible_count++;
				v->deleted_count--;
			}
		}

		camel_message_info_changed(info, FALSE);
	}

	return diff != 0;
}

static gboolean
pop3_info_set_user_flag(CamelMessageInfo *info, const char *name, gboolean value)
{
	return FALSE;
}

static gboolean
pop3_info_set_user_tag(CamelMessageInfo *info, const char *name, const char *value)
{
	return FALSE;
}

/* CamelFolderSummaryDisk */
static int
pop3_decode(CamelFolderSummaryDisk *s, CamelMessageInfoDisk *mi, CamelRecordDecoder *crd)
{
	int tag, ver;

	camel_record_decoder_reset(crd);
	while ((tag = camel_record_decoder_next_section(crd, &ver)) != CR_SECTION_INVALID) {
		switch (tag) {
		case CFS_POP3_SECTION_INFO:
			((CamelPOP3MessageInfo *)mi)->flags = camel_record_decoder_int32(crd);
			((CamelPOP3MessageInfo *)mi)->size = camel_record_decoder_int32(crd);
			((CamelPOP3MessageInfo *)mi)->date = camel_record_decoder_timet(crd);
			break;
		}
	}

	return 0;
}

static void
pop3_encode(CamelFolderSummaryDisk *s, CamelMessageInfoDisk *mi, CamelRecordEncoder *cre)
{
	/* We cannot call parent class since we have our own messageionfo
	   which isn't actually a camelmessageinfodisk either */

	camel_record_encoder_start_section(cre, CFS_POP3_SECTION_INFO, 0);
	camel_record_encoder_int32(cre, ((CamelPOP3MessageInfo *)mi)->flags);
	camel_record_encoder_int32(cre, ((CamelPOP3MessageInfo *)mi)->size);
	camel_record_encoder_timet(cre, ((CamelPOP3MessageInfo *)mi)->date);
	camel_record_encoder_end_section(cre);
}

/* ********************************************************************** */

static void
camel_pop3_summary_class_init(CamelPOP3SummaryClass *klass)
{
	pop3_parent = (CamelFolderSummaryDiskClass *)camel_folder_summary_disk_get_type();

	((CamelFolderSummaryClass *)klass)->messageinfo_sizeof = sizeof(CamelPOP3MessageInfo);

	((CamelFolderSummaryDiskClass *)klass)->decode = pop3_decode;
	((CamelFolderSummaryDiskClass *)klass)->encode = pop3_encode;

	((CamelFolderSummaryClass *)klass)->uid_cmp = pop3_uid_cmp;

	// can't rename
	//((CamelFolderSummaryClass *)klass)->rename = pop3_rename;

	/* We need to override most info methods since the base class
	   works on a different messageinfo object */

	((CamelFolderSummaryClass *)klass)->message_info_free = pop3_message_info_free;

	//((CamelFolderSummaryClass *)klass)->search = pop3_search;

	//((CamelFolderSummaryClass *)klass)->view_add = pop3_view_add;
	//((CamelFolderSummaryClass *)klass)->view_delete = pop3_view_delete;
	//((CamelFolderSummaryClass *)klass)->view_free = pop3_view_free;

	((CamelFolderSummaryClass *)klass)->info_ptr = pop3_info_ptr;
	((CamelFolderSummaryClass *)klass)->info_uint32 = pop3_info_uint32;
	((CamelFolderSummaryClass *)klass)->info_time = pop3_info_time;
	((CamelFolderSummaryClass *)klass)->info_user_flag = pop3_info_user_flag;
	((CamelFolderSummaryClass *)klass)->info_user_tag = pop3_info_user_tag;

	((CamelFolderSummaryClass *)klass)->info_set_user_flag = pop3_info_set_user_flag;
	((CamelFolderSummaryClass *)klass)->info_set_user_tag = pop3_info_set_user_tag;
	((CamelFolderSummaryClass *)klass)->info_set_flags = pop3_info_set_flags;
}

static void
camel_pop3_summary_init(CamelPOP3Summary *obj)
{
	CamelFolderSummary *s =(CamelFolderSummary *)obj;

	s = s;
}

CamelType
camel_pop3_summary_get_type(void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register(
			camel_folder_summary_disk_get_type(), "CamelPOP3Summary",
			sizeof(CamelPOP3Summary),
			sizeof(CamelPOP3SummaryClass),
			(CamelObjectClassInitFunc) camel_pop3_summary_class_init,
			NULL,
			(CamelObjectInitFunc) camel_pop3_summary_init,
			NULL);
	}

	return type;
}
