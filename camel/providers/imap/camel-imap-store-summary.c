/*
 * Copyright (C) 2002 Ximian Inc.
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

#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "camel-imap-store-summary.h"

#include "camel-file-utils.h"

#include "hash-table-utils.h"
#include "e-util/md5-utils.h"
#include "e-util/e-memory.h"

#include "camel-private.h"

#define d(x)
#define io(x)			/* io debug */

#define CAMEL_IMAP_STORE_SUMMARY_VERSION_0 (0)

#define CAMEL_IMAP_STORE_SUMMARY_VERSION (0)

#define _PRIVATE(o) (((CamelImapStoreSummary *)(o))->priv)

static int summary_header_load(CamelStoreSummary *, FILE *);
static int summary_header_save(CamelStoreSummary *, FILE *);

/*static CamelStoreInfo * store_info_new(CamelStoreSummary *, const char *);*/
static CamelStoreInfo * store_info_load(CamelStoreSummary *, FILE *);
static int		 store_info_save(CamelStoreSummary *, FILE *, CamelStoreInfo *);
static void		 store_info_free(CamelStoreSummary *, CamelStoreInfo *);

static const char *store_info_string(CamelStoreSummary *, const CamelStoreInfo *, int);
static void store_info_set_string(CamelStoreSummary *, CamelStoreInfo *, int, const char *);

static void camel_imap_store_summary_class_init (CamelImapStoreSummaryClass *klass);
static void camel_imap_store_summary_init       (CamelImapStoreSummary *obj);
static void camel_imap_store_summary_finalise   (CamelObject *obj);

static CamelStoreSummaryClass *camel_imap_store_summary_parent;

static void
camel_imap_store_summary_class_init (CamelImapStoreSummaryClass *klass)
{
	CamelStoreSummaryClass *ssklass = (CamelStoreSummaryClass *)klass;

	ssklass->summary_header_load = summary_header_load;
	ssklass->summary_header_save = summary_header_save;

	/*ssklass->store_info_new  = store_info_new;*/
	ssklass->store_info_load = store_info_load;
	ssklass->store_info_save = store_info_save;
	ssklass->store_info_free = store_info_free;

	ssklass->store_info_string = store_info_string;
	ssklass->store_info_set_string = store_info_set_string;
}

static void
camel_imap_store_summary_init (CamelImapStoreSummary *s)
{
	/*struct _CamelImapStoreSummaryPrivate *p;

	  p = _PRIVATE(s) = g_malloc0(sizeof(*p));*/

	((CamelStoreSummary *)s)->store_info_size = sizeof(CamelImapStoreInfo);
	s->version = CAMEL_IMAP_STORE_SUMMARY_VERSION;
}

static void
camel_imap_store_summary_finalise (CamelObject *obj)
{
	/*struct _CamelImapStoreSummaryPrivate *p;*/
	/*CamelImapStoreSummary *s = (CamelImapStoreSummary *)obj;*/

	/*p = _PRIVATE(obj);
	  g_free(p);*/
}

CamelType
camel_imap_store_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		camel_imap_store_summary_parent = (CamelStoreSummaryClass *)camel_store_summary_get_type();
		type = camel_type_register((CamelType)camel_imap_store_summary_parent, "CamelImapStoreSummary",
					   sizeof (CamelImapStoreSummary),
					   sizeof (CamelImapStoreSummaryClass),
					   (CamelObjectClassInitFunc) camel_imap_store_summary_class_init,
					   NULL,
					   (CamelObjectInitFunc) camel_imap_store_summary_init,
					   (CamelObjectFinalizeFunc) camel_imap_store_summary_finalise);
	}
	
	return type;
}

/**
 * camel_imap_store_summary_new:
 *
 * Create a new CamelImapStoreSummary object.
 * 
 * Return value: A new CamelImapStoreSummary widget.
 **/
CamelImapStoreSummary *
camel_imap_store_summary_new (void)
{
	CamelImapStoreSummary *new = CAMEL_IMAP_STORE_SUMMARY ( camel_object_new (camel_imap_store_summary_get_type ()));

	return new;
}

/**
 * camel_imap_store_summary_full_name:
 * @s: 
 * @path: 
 * 
 * Retrieve a summary item by full name.
 *
 * A referenced to the summary item is returned, which may be
 * ref'd or free'd as appropriate.
 * 
 * Return value: The summary item, or NULL if the @full_name name
 * is not available.
 * It must be freed using camel_store_summary_info_free().
 **/
CamelImapStoreInfo *
camel_imap_store_summary_full_name(CamelImapStoreSummary *s, const char *full_name)
{
	int count, i;
	CamelImapStoreInfo *info;

	count = camel_store_summary_count((CamelStoreSummary *)s);
	for (i=0;i<count;i++) {
		info = (CamelImapStoreInfo *)camel_store_summary_index((CamelStoreSummary *)s, i);
		if (info) {
			if (strcmp(info->full_name, full_name) == 0)
				return info;
			camel_store_summary_info_free((CamelStoreSummary *)s, (CamelStoreInfo *)info);
		}
	}

	return NULL;
}

static int
summary_header_load(CamelStoreSummary *s, FILE *in)
{
	gint32 version;
	
	if (camel_imap_store_summary_parent->summary_header_load((CamelStoreSummary *)s, in) == -1
	    || camel_file_util_decode_fixed_int32(in, &version) == -1)
		return -1;

	s->version = version;

	if (version < CAMEL_IMAP_STORE_SUMMARY_VERSION_0) {
		g_warning("Store summary header version too low");
		return -1;
	}

	return 0;
}

static int
summary_header_save(CamelStoreSummary *s, FILE *out)
{
	/* always write as latest version */
	if (camel_imap_store_summary_parent->summary_header_save((CamelStoreSummary *)s, out) == -1
	    || camel_file_util_encode_fixed_int32(out, CAMEL_IMAP_STORE_SUMMARY_VERSION) == -1)
		return -1;

	return 0;
}

static CamelStoreInfo *
store_info_load(CamelStoreSummary *s, FILE *in)
{
	CamelImapStoreInfo *mi;

	mi = (CamelImapStoreInfo *)camel_imap_store_summary_parent->store_info_load(s, in);
	if (mi) {
		if (camel_file_util_decode_string(in, &mi->full_name) == -1) {
			camel_store_summary_info_free(s, (CamelStoreInfo *)mi);
			mi = NULL;
		}
	}

	return (CamelStoreInfo *)mi;
}

static int
store_info_save(CamelStoreSummary *s, FILE *out, CamelStoreInfo *mi)
{
	CamelImapStoreInfo *isi = (CamelImapStoreInfo *)mi;

	if (camel_imap_store_summary_parent->store_info_save(s, out, mi) == -1
	    || camel_file_util_encode_string(out, isi->full_name) == -1)
		return -1;

	return 0;
}

static void
store_info_free(CamelStoreSummary *s, CamelStoreInfo *mi)
{
	CamelImapStoreInfo *isi = (CamelImapStoreInfo *)mi;

	g_free(isi->full_name);
	camel_imap_store_summary_parent->store_info_free(s, mi);
}

static const char *
store_info_string(CamelStoreSummary *s, const CamelStoreInfo *mi, int type)
{
	CamelImapStoreInfo *isi = (CamelImapStoreInfo *)mi;

	/* FIXME: Locks? */

	g_assert (mi != NULL);

	switch (type) {
	case CAMEL_IMAP_STORE_INFO_FULL_NAME:
		return isi->full_name;
	default:
		return camel_imap_store_summary_parent->store_info_string(s, mi, type);
	}
}

static void
store_info_set_string(CamelStoreSummary *s, CamelStoreInfo *mi, int type, const char *str)
{
	CamelImapStoreInfo *isi = (CamelImapStoreInfo *)mi;

	g_assert(mi != NULL);

	switch(type) {
	case CAMEL_IMAP_STORE_INFO_FULL_NAME:
		isi->full_name = g_strdup(str);
		break;
	default:
		camel_imap_store_summary_parent->store_info_set_string(s, mi, type, str);
		break;
	}
}
