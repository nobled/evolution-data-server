/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/*
 *  Copyright (C) 2000 Ximian Inc.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "camel-local-summary.h"
#include "camel/camel-mime-message.h"
#include "camel/camel-stream-null.h"
#include "camel/camel-file-utils.h"
#include "camel/camel-i18n.h"

#define w(x)
#define io(x)
#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/

#define CAMEL_LOCAL_SUMMARY_VERSION (1)

static CamelFolderSummaryClass *camel_local_summary_parent;

void
camel_local_summary_construct(CamelLocalSummary *new, CamelFolder *folder, const char *filename, const char *local_name, CamelIndex *index)
{
	//camel_folder_summary_set_build_content(CAMEL_FOLDER_SUMMARY(new), FALSE);
	//camel_folder_summary_set_filename(CAMEL_FOLDER_SUMMARY(new), filename);
	new->folder_path = g_strdup(local_name);
	new->index = index;
	if (index)
		camel_object_ref((CamelObject *)index);

	camel_folder_summary_disk_construct((CamelFolderSummaryDisk *)new, folder);
}

void camel_local_summary_check_force(CamelLocalSummary *cls)
{
	cls->check_force = 1;
}

int
camel_local_summary_check(CamelLocalSummary *cls, CamelFolderChangeInfo *changeinfo, CamelException *ex)
{
	return ((CamelLocalSummaryClass *)(CAMEL_OBJECT_GET_CLASS(cls)))->check(cls, changeinfo, ex);
}

int
camel_local_summary_sync(CamelLocalSummary *cls, gboolean expunge, CamelFolderChangeInfo *changeinfo, CamelException *ex)
{
	return ((CamelLocalSummaryClass *)(CAMEL_OBJECT_GET_CLASS(cls)))->sync(cls, expunge, changeinfo, ex);
}

static CamelMessageInfo *
local_message_info_alloc(CamelFolderSummary *s)
{
	return g_malloc0(sizeof(CamelLocalMessageInfo));
}

static int
local_summary_check(CamelLocalSummary *cls, CamelFolderChangeInfo *changeinfo, CamelException *ex)
{
	/* FIXME: sync index here ? */
	return 0;
}

static int
local_summary_sync(CamelLocalSummary *cls, gboolean expunge, CamelFolderChangeInfo *changeinfo, CamelException *ex)
{
	int ret = 0;

	camel_folder_summary_disk_sync((CamelFolderSummaryDisk *)cls, ex);
	if (ex && ex->id)
		ret = -1;

	if (cls->index && camel_index_sync(cls->index) == -1)
		g_warning ("Could not sync index for %s: %s", cls->folder_path, strerror (errno));

	return ret;
}

static void
camel_local_summary_init(CamelLocalSummary *obj)
{
	struct _CamelFolderSummary *s = (CamelFolderSummary *)obj;

	s = s;
}

static void
camel_local_summary_finalise(CamelObject *obj)
{
	CamelLocalSummary *mbs = CAMEL_LOCAL_SUMMARY(obj);

	if (mbs->index)
		camel_object_unref((CamelObject *)mbs->index);
	g_free(mbs->folder_path);
}

static void
camel_local_summary_class_init(CamelLocalSummaryClass *klass)
{
	CamelFolderSummaryClass *sklass = (CamelFolderSummaryClass *) klass;
	
	camel_local_summary_parent = CAMEL_FOLDER_SUMMARY_CLASS(camel_type_get_global_classfuncs(camel_folder_summary_get_type()));

	sklass->message_info_alloc = local_message_info_alloc;

	klass->check = local_summary_check;
	klass->sync = local_summary_sync;
}

CamelType
camel_local_summary_get_type(void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register(camel_folder_summary_disk_get_type(), "CamelLocalSummary",
					   sizeof (CamelLocalSummary),
					   sizeof (CamelLocalSummaryClass),
					   (CamelObjectClassInitFunc) camel_local_summary_class_init,
					   NULL,
					   (CamelObjectInitFunc) camel_local_summary_init,
					   (CamelObjectFinalizeFunc) camel_local_summary_finalise);
	}
	
	return type;
}
