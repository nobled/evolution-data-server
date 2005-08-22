/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright(C) 2005 Novell Inc.
 *
 *  Authors:
 *    Michael Zucchi <notzed@ximian.com>
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
#include "camel-imapx-summary.h"
#include "camel-imapx-server.h"
#include "camel-imapx-store.h"
#include <camel/camel-folder.h>

static CamelFolderSummaryDiskClass *imapx_parent;

CamelFolderSummary *
camel_imapx_summary_new(struct _CamelFolder *folder)
{
	CamelFolderSummary *s = (CamelFolderSummary *)camel_object_new(camel_imapx_summary_get_type());

	camel_folder_summary_disk_construct((CamelFolderSummaryDisk *)s, folder);

	return s;
}

static int
imapx_uid_cmp(const void *ap, const void *bp, void *data)
{
	const char *a = ap, *b = bp;
	char *ae, *be;
	unsigned long av, bv;

	av = strtoul(a, &ae, 10);
	bv = strtoul(b, &be, 10);

	if (av < bv)
		return -1;
	else if (av > bv)
		return 1;

	if (*ae == '-')
		ae++;
	if (*be == '-')
		be++;

	return strcmp(ae, be);
}

static int
imapx_decode(CamelFolderSummaryDisk *s, CamelMessageInfoDisk *mi, CamelRecordDecoder *crd)
{
	int tag, ver;

	if (((CamelFolderSummaryDiskClass *)imapx_parent)->decode(s, mi, crd) != 0)
		return -1;

	camel_record_decoder_reset(crd);
	while ((tag = camel_record_decoder_next_section(crd, &ver)) != CR_SECTION_INVALID) {
		switch (tag) {
		case CFS_IMAPX_SECTION_INFO:
			((CamelIMAPXMessageInfo *)mi)->server_flags = camel_record_decoder_int32(crd);
			break;
		}
	}

	return 0;
}

static void
imapx_encode(CamelFolderSummaryDisk *s, CamelMessageInfoDisk *mi, CamelRecordEncoder *cre)
{
	((CamelFolderSummaryDiskClass *)imapx_parent)->encode(s, mi, cre);

	camel_record_encoder_start_section(cre, CFS_IMAPX_SECTION_INFO, 0);
	camel_record_encoder_sizet(cre, ((CamelIMAPXMessageInfo *)mi)->server_flags);
	camel_record_encoder_end_section(cre);
}

static void
imapx_sync_changes(CamelFolderSummaryDisk *cds, GPtrArray *changes, CamelException *ex)
{
	CamelFolder *folder = ((CamelFolderSummary *)cds)->folder;
	CamelIMAPXServer *is = ((CamelIMAPXStore *)folder->parent_store)->server;

	camel_imapx_server_sync_changes(is, folder, changes, ex);
	// do we care if it fails?
	camel_exception_clear(ex);

	((CamelFolderSummaryDiskClass *)imapx_parent)->sync(cds, changes, ex);
}

/* ********************************************************************** */

static void
camel_imapx_summary_class_init(CamelIMAPXSummaryClass *klass)
{
	imapx_parent = (CamelFolderSummaryDiskClass *)camel_folder_summary_disk_get_type();

	((CamelFolderSummaryClass *)klass)->messageinfo_sizeof = sizeof(CamelIMAPXMessageInfo);
	((CamelFolderSummaryClass *)klass)->uid_cmp = imapx_uid_cmp;

	((CamelFolderSummaryDiskClass *)klass)->decode = imapx_decode;
	((CamelFolderSummaryDiskClass *)klass)->encode = imapx_encode;

	((CamelFolderSummaryDiskClass *)klass)->sync = imapx_sync_changes;
}

static void
camel_imapx_summary_init(CamelIMAPXSummary *obj)
{
	CamelFolderSummary *s =(CamelFolderSummary *)obj;

	s = s;
}

CamelType
camel_imapx_summary_get_type(void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register(
			camel_folder_summary_disk_get_type(), "CamelIMAPXSummary",
			sizeof(CamelIMAPXSummary),
			sizeof(CamelIMAPXSummaryClass),
			(CamelObjectClassInitFunc) camel_imapx_summary_class_init,
			NULL,
			(CamelObjectInitFunc) camel_imapx_summary_init,
			NULL);
	}

	return type;
}
