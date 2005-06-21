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
	const char *a = ap, *b = bp, *at, *bt;

	/* UID's are stored:
	   uid,sec.usec, sec & usec in hexadecimal format.
	   This is so we can sort them in time order */

	at = strrchr(a, ',');
	bt = strrchr(b, ',');
	g_assert(at && bt);

	/* just check uid part, if the same we have a match, ignoring time */
	if ((at-a) == (bt-b) && memcmp(a, b, at-a) == 0)
		return 0;

	/* otherwise, compare the times */
	return strcmp(at, bt);
}

/* CamelFolderSummaryDisk */
static int
pop3_decode(CamelFolderSummaryDisk *s, CamelMessageInfoDisk *mi, CamelRecordDecoder *crd)
{
	int tag, ver, res = -1;

	if (((CamelFolderSummaryDiskClass *)pop3_parent)->decode(s, mi, crd) != 0)
		return res;

	camel_record_decoder_reset(crd);
	while ((tag = camel_record_decoder_next_section(crd, &ver)) != CR_SECTION_INVALID) {
		switch (tag) {
		case CFS_POP3_SECTION_INFO:
			((CamelPOP3MessageInfo *)mi)->id = camel_record_decoder_int32(crd);
			res = 0;
			break;
		}
	}

	return res;
}

static void
pop3_encode(CamelFolderSummaryDisk *s, CamelMessageInfoDisk *mi, CamelRecordEncoder *cre)
{
	((CamelFolderSummaryDiskClass *)pop3_parent)->encode(s, mi, cre);

	camel_record_encoder_start_section(cre, CFS_POP3_SECTION_INFO, 0);
	camel_record_encoder_int32(cre, ((CamelPOP3MessageInfo *)mi)->id);
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

	//((CamelFolderSummaryClass *)klass)->search = pop3_search;

	//((CamelFolderSummaryClass *)klass)->view_add = pop3_view_add;
	//((CamelFolderSummaryClass *)klass)->view_delete = pop3_view_delete;
	//((CamelFolderSummaryClass *)klass)->view_free = pop3_view_free;
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
