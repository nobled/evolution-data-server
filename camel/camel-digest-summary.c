/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "camel-digest-summary.h"

#define CAMEL_DIGEST_SUMMARY_VERSION 0

static gpointer parent_class;

static void
digest_summary_class_init (CamelDigestSummaryClass *class)
{
	CamelFolderSummaryClass *folder_summary_class;

	parent_class = g_type_class_peek_parent (class);

	folder_summary_class = CAMEL_FOLDER_SUMMARY_CLASS (class);
	folder_summary_class->message_info_size = sizeof (CamelMessageInfo);
	folder_summary_class->content_info_size = sizeof (CamelMessageContentInfo);
}

static void
digest_summary_init (CamelDigestSummary *digest_summary)
{
	CamelFolderSummary *summary;

	summary = CAMEL_FOLDER_SUMMARY (digest_summary);

	/* and a unique file version */
	summary->version += CAMEL_DIGEST_SUMMARY_VERSION;
}

GType
camel_digest_summary_get_type(void)
{
	static GType type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID))
		type = g_type_register_static_simple (
			CAMEL_TYPE_FOLDER_SUMMARY,
			"CamelDigestSummary",
			sizeof (CamelDigestSummaryClass),
			(GClassInitFunc) digest_summary_class_init,
			sizeof (CamelDigestSummary),
			(GInstanceInitFunc) digest_summary_init,
			0);

	return type;
}

CamelFolderSummary *
camel_digest_summary_new (void)
{
	return g_object_new (CAMEL_TYPE_DIGEST_SUMMARY, NULL);
}
