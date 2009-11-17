/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gstdio.h>

#include <libedataserver/e-memory.h>

#include "camel-file-utils.h"
#include "camel-private.h"
#include "camel-store-summary.h"
#include "camel-url.h"

#define d(x)
#define io(x)			/* io debug */

/* possible versions, for versioning changes */
#define CAMEL_STORE_SUMMARY_VERSION_0 (1)
#define CAMEL_STORE_SUMMARY_VERSION_2 (2)

/* current version */
#define CAMEL_STORE_SUMMARY_VERSION (2)

#define CAMEL_STORE_SUMMARY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_STORE_SUMMARY, CamelStoreSummaryPrivate))

static gpointer parent_class;

static void
store_summary_finalize (GObject *object)
{
	CamelStoreSummary *summary = CAMEL_STORE_SUMMARY (object);

	camel_store_summary_clear (summary);
	g_ptr_array_free (summary->folders, TRUE);
	g_hash_table_destroy (summary->folders_path);

	g_free (summary->summary_path);

	if (summary->store_info_chunks != NULL)
		e_memchunk_destroy (summary->store_info_chunks);

	g_mutex_free (summary->priv->summary_lock);
	g_mutex_free (summary->priv->io_lock);
	g_mutex_free (summary->priv->alloc_lock);
	g_mutex_free (summary->priv->ref_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gint
store_summary_summary_header_load (CamelStoreSummary *summary,
                                   FILE *in)
{
	gint32 version, flags, count;
	time_t time;

	fseek (in, 0, SEEK_SET);

	io (printf ("Loading header\n"));

	if (camel_file_util_decode_fixed_int32 (in, &version) == -1
	    || camel_file_util_decode_fixed_int32 (in, &flags) == -1
	    || camel_file_util_decode_time_t (in, &time) == -1
	    || camel_file_util_decode_fixed_int32 (in, &count) == -1) {
		return -1;
	}

	summary->flags = flags;
	summary->time = time;
	summary->count = count;
	summary->version = version;

	if (version < CAMEL_STORE_SUMMARY_VERSION_0) {
		g_warning ("Store summary header version too low");
		return -1;
	}

	return 0;
}

static gint
store_summary_summary_header_save (CamelStoreSummary *summary,
                                   FILE *out)
{
	fseek (out, 0, SEEK_SET);

	io (printf ("Savining header\n"));

	/* always write latest version */
	camel_file_util_encode_fixed_int32 (out, CAMEL_STORE_SUMMARY_VERSION);
	camel_file_util_encode_fixed_int32 (out, summary->flags);
	camel_file_util_encode_time_t (out, summary->time);

	return camel_file_util_encode_fixed_int32 (
		out, camel_store_summary_count (summary));
}

static CamelStoreInfo *
store_summary_store_info_new (CamelStoreSummary *summary,
                              const gchar *path)
{
	CamelStoreInfo *info;

	info = camel_store_summary_info_new (summary);

	info->path = g_strdup (path);
	info->unread = CAMEL_STORE_INFO_FOLDER_UNKNOWN;
	info->total = CAMEL_STORE_INFO_FOLDER_UNKNOWN;

	return info;
}

static CamelStoreInfo *
store_summary_store_info_load (CamelStoreSummary *summary,
                               FILE *in)
{
	CamelStoreInfo *info;

	info = camel_store_summary_info_new (summary);

	io (printf ("Loading folder info\n"));

	camel_file_util_decode_string (in, &info->path);
	camel_file_util_decode_uint32 (in, &info->flags);
	camel_file_util_decode_uint32 (in, &info->unread);
	camel_file_util_decode_uint32 (in, &info->total);

	/* Ok, brown paper bag bug - prior to version 2 of the file, flags are
	   stored using the bit number, not the bit. Try to recover as best we can */
	if (summary->version < CAMEL_STORE_SUMMARY_VERSION_2) {
		guint32 flags = 0;

		if (info->flags & 1)
			flags |= CAMEL_STORE_INFO_FOLDER_NOSELECT;
		if (info->flags & 2)
			flags |= CAMEL_STORE_INFO_FOLDER_READONLY;
		if (info->flags & 3)
			flags |= CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
		if (info->flags & 4)
			flags |= CAMEL_STORE_INFO_FOLDER_FLAGGED;

		info->flags = flags;
	}

	if (!ferror (in))
		return info;

	camel_store_summary_info_free (summary, info);

	return NULL;
}

static gint
store_summary_store_info_save (CamelStoreSummary *summary,
                               FILE *out,
                               CamelStoreInfo *info)
{
	io (printf ("Saving folder info\n"));

	camel_file_util_encode_string (
		out, camel_store_info_path (summary, info));
	camel_file_util_encode_uint32 (out, info->flags);
	camel_file_util_encode_uint32 (out, info->unread);
	camel_file_util_encode_uint32 (out, info->total);

	return ferror (out);
}

static void
store_summary_store_info_free (CamelStoreSummary *summary,
                               CamelStoreInfo *info)
{
	g_free (info->path);
	g_free (info->uri);
	g_slice_free1 (summary->store_info_size, info);
}

static const gchar *
store_summary_store_info_string (CamelStoreSummary *summary,
                                 const CamelStoreInfo *info,
                                 gint type)
{
	const gchar *p;

	/* FIXME: Locks? */

	g_assert (info != NULL);

	switch (type) {
	case CAMEL_STORE_INFO_PATH:
		return info->path;
	case CAMEL_STORE_INFO_NAME:
		p = strrchr (info->path, '/');
		if (p)
			return p+1;
		else
			return info->path;
	case CAMEL_STORE_INFO_URI:
		if (info->uri == NULL) {
			CamelURL *uri;

			uri = camel_url_new_with_base (summary->uri_base, info->path);
			((CamelStoreInfo *)info)->uri = camel_url_to_string (uri, 0);
			camel_url_free (uri);
		}
		return info->uri;
	}

	return "";
}

static void
store_summary_store_info_set_string (CamelStoreSummary *summary,
                                     CamelStoreInfo *info,
                                     gint type,
                                     const gchar *str)
{
	const gchar *p;
	gchar *v;
	gint len;

	g_assert (info != NULL);

	switch (type) {
	case CAMEL_STORE_INFO_PATH:
		CAMEL_STORE_SUMMARY_LOCK (summary, summary_lock);
		g_hash_table_remove (summary->folders_path, (gchar *)camel_store_info_path (summary, info));
		g_free (info->path);
		g_free (info->uri);
		info->path = g_strdup (str);
		g_hash_table_insert (summary->folders_path, (gchar *)camel_store_info_path (summary, info), info);
		summary->flags |= CAMEL_STORE_SUMMARY_DIRTY;
		CAMEL_STORE_SUMMARY_UNLOCK (summary, summary_lock);
		break;
	case CAMEL_STORE_INFO_NAME:
		CAMEL_STORE_SUMMARY_LOCK (summary, summary_lock);
		g_hash_table_remove (summary->folders_path, (gchar *)camel_store_info_path (summary, info));
		p = strrchr (info->path, '/');
		if (p) {
			len = p-info->path+1;
			v = g_malloc (len+strlen (str)+1);
			memcpy (v, info->path, len);
			strcpy (v+len, str);
		} else {
			v = g_strdup (str);
		}
		g_free (info->path);
		info->path = v;
		g_hash_table_insert (summary->folders_path, (gchar *)camel_store_info_path (summary, info), info);
		CAMEL_STORE_SUMMARY_UNLOCK (summary, summary_lock);
		break;
	case CAMEL_STORE_INFO_URI:
		g_warning ("Cannot set store info uri, aborting");
		abort ();
		break;
	}
}

static void
store_summary_class_init (CamelStoreSummaryClass *class)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (CamelStoreSummaryPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = store_summary_finalize;

	class->summary_header_load = store_summary_summary_header_load;
	class->summary_header_save = store_summary_summary_header_save;
	class->store_info_new  = store_summary_store_info_new;
	class->store_info_load = store_summary_store_info_load;
	class->store_info_save = store_summary_store_info_save;
	class->store_info_free = store_summary_store_info_free;
	class->store_info_string = store_summary_store_info_string;
	class->store_info_set_string = store_summary_store_info_set_string;
}

static void
store_summary_init (CamelStoreSummary *summary)
{
	summary->priv = CAMEL_STORE_SUMMARY_GET_PRIVATE (summary);

	summary->store_info_size = sizeof (CamelStoreInfo);

	summary->store_info_chunks = NULL;

	summary->version = CAMEL_STORE_SUMMARY_VERSION;
	summary->flags = 0;
	summary->count = 0;
	summary->time = 0;

	summary->folders = g_ptr_array_new ();
	summary->folders_path = g_hash_table_new (g_str_hash, g_str_equal);

	summary->priv->summary_lock = g_mutex_new ();
	summary->priv->io_lock = g_mutex_new ();
	summary->priv->alloc_lock = g_mutex_new ();
	summary->priv->ref_lock = g_mutex_new ();
}

GType
camel_store_summary_get_type (void)
{
	static GType type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID))
		type = g_type_register_static_simple (
			CAMEL_TYPE_OBJECT,
			"CamelStoreSummary",
			sizeof (CamelStoreSummaryClass),
			(GClassInitFunc) store_summary_class_init,
			sizeof (CamelStoreSummary),
			(GInstanceInitFunc) store_summary_init,
			0);

	return type;
}

/**
 * camel_store_summary_new:
 *
 * Create a new #CamelStoreSummary object.
 *
 * Returns: a new #CamelStoreSummary object
 **/
CamelStoreSummary *
camel_store_summary_new (void)
{
	return g_object_new (CAMEL_TYPE_STORE_SUMMARY, NULL);
}

/**
 * camel_store_summary_set_filename:
 * @summary: a #CamelStoreSummary
 * @filename: a filename
 *
 * Set the filename where the summary will be loaded to/saved from.
 **/
void
camel_store_summary_set_filename (CamelStoreSummary *s, const gchar *name)
{
	CAMEL_STORE_SUMMARY_LOCK (s, summary_lock);

	g_free (s->summary_path);
	s->summary_path = g_strdup (name);

	CAMEL_STORE_SUMMARY_UNLOCK (s, summary_lock);
}

/**
 * camel_store_summary_set_uri_base:
 * @summary: a #CamelStoreSummary object
 * @base: a #CamelURL
 *
 * Sets the base URI for the summary.
 **/
void
camel_store_summary_set_uri_base (CamelStoreSummary *s, CamelURL *base)
{
	CAMEL_STORE_SUMMARY_LOCK (s, summary_lock);

	if (s->uri_base)
		camel_url_free (s->uri_base);
	s->uri_base = camel_url_new_with_base (base, "");

	CAMEL_STORE_SUMMARY_UNLOCK (s, summary_lock);
}

/**
 * camel_store_summary_count:
 * @summary: a #CamelStoreSummary object
 *
 * Get the number of summary items stored in this summary.
 *
 * Returns: the number of items gint he summary.
 **/
gint
camel_store_summary_count (CamelStoreSummary *s)
{
	return s->folders->len;
}

/**
 * camel_store_summary_index:
 * @summary: a #CamelStoreSummary object
 * @index: record index
 *
 * Retrieve a summary item by index number.
 *
 * A referenced to the summary item is returned, which may be ref'd or
 * free'd as appropriate.
 *
 * It must be freed using #camel_store_summary_info_free.
 *
 * Returns: the summary item, or %NULL if @index is out of range
 **/
CamelStoreInfo *
camel_store_summary_index (CamelStoreSummary *s, gint i)
{
	CamelStoreInfo *info = NULL;

	CAMEL_STORE_SUMMARY_LOCK (s, ref_lock);
	CAMEL_STORE_SUMMARY_LOCK (s, summary_lock);

	if (i<s->folders->len)
		info = g_ptr_array_index (s->folders, i);

	CAMEL_STORE_SUMMARY_UNLOCK (s, summary_lock);

	if (info)
		info->refcount++;

	CAMEL_STORE_SUMMARY_UNLOCK (s, ref_lock);

	return info;
}

/**
 * camel_store_summary_array:
 * @summary: a #CamelStoreSummary object
 *
 * Obtain a copy of the summary array.  This is done atomically,
 * so cannot contain empty entries.
 *
 * It must be freed using #camel_store_summary_array_free.
 *
 * Returns: the summary array
 **/
GPtrArray *
camel_store_summary_array (CamelStoreSummary *s)
{
	CamelStoreInfo *info;
	GPtrArray *res = g_ptr_array_new ();
	gint i;

	CAMEL_STORE_SUMMARY_LOCK (s, ref_lock);
	CAMEL_STORE_SUMMARY_LOCK (s, summary_lock);

	g_ptr_array_set_size (res, s->folders->len);
	for (i=0;i<s->folders->len;i++) {
		info = res->pdata[i] = g_ptr_array_index (s->folders, i);
		info->refcount++;
	}

	CAMEL_STORE_SUMMARY_UNLOCK (s, summary_lock);
	CAMEL_STORE_SUMMARY_UNLOCK (s, ref_lock);

	return res;
}

/**
 * camel_store_summary_array_free:
 * @summary: a #CamelStoreSummary object
 * @array: the summary array as gotten from #camel_store_summary_array
 *
 * Free the folder summary array.
 **/
void
camel_store_summary_array_free (CamelStoreSummary *s, GPtrArray *array)
{
	gint i;

	for (i=0;i<array->len;i++)
		camel_store_summary_info_free (s, array->pdata[i]);

	g_ptr_array_free (array, TRUE);
}

/**
 * camel_store_summary_path:
 * @summary: a #CamelStoreSummary object
 * @path: path to the item
 *
 * Retrieve a summary item by path name.
 *
 * A referenced to the summary item is returned, which may be ref'd or
 * free'd as appropriate.
 *
 * It must be freed using #camel_store_summary_info_free.
 *
 * Returns: the summary item, or %NULL if the @path name is not
 * available
 **/
CamelStoreInfo *
camel_store_summary_path (CamelStoreSummary *s, const gchar *path)
{
	CamelStoreInfo *info;

	CAMEL_STORE_SUMMARY_LOCK (s, ref_lock);
	CAMEL_STORE_SUMMARY_LOCK (s, summary_lock);

	info = g_hash_table_lookup (s->folders_path, path);

	CAMEL_STORE_SUMMARY_UNLOCK (s, summary_lock);

	if (info)
		info->refcount++;

	CAMEL_STORE_SUMMARY_UNLOCK (s, ref_lock);

	return info;
}

/**
 * camel_store_summary_load:
 * @summary: a #CamelStoreSummary object
 *
 * Load the summary off disk.
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_store_summary_load (CamelStoreSummary *s)
{
	FILE *in;
	gint i;
	CamelStoreInfo *info;
	CamelStoreSummaryClass *class;

	g_return_val_if_fail (CAMEL_IS_STORE_SUMMARY (s), -1);
	g_return_val_if_fail (s->summary_path != NULL, -1);

	in = g_fopen (s->summary_path, "rb");
	if (in == NULL)
		return -1;

	class = CAMEL_STORE_SUMMARY_GET_CLASS (s);
	g_return_val_if_fail (class->store_info_load != NULL, -1);

	CAMEL_STORE_SUMMARY_LOCK (s, io_lock);
	if (class->summary_header_load (s, in) == -1)
		goto error;

	/* now read in each message ... */
	for (i=0;i<s->count;i++) {
		info = class->store_info_load (s, in);

		if (info == NULL)
			goto error;

		camel_store_summary_add (s, info);
	}

	CAMEL_STORE_SUMMARY_UNLOCK (s, io_lock);

	if (fclose (in) != 0)
		return -1;

	s->flags &= ~CAMEL_STORE_SUMMARY_DIRTY;

	return 0;

error:
	i = ferror (in);
	g_warning ("Cannot load summary file: %s", g_strerror (ferror (in)));
	CAMEL_STORE_SUMMARY_UNLOCK (s, io_lock);
	fclose (in);
	s->flags |= ~CAMEL_STORE_SUMMARY_DIRTY;
	errno = i;

	return -1;
}

/**
 * camel_store_summary_save:
 * @summary: a #CamelStoreSummary object
 *
 * Writes the summary to disk.  The summary is only written if changes
 * have occured.
 *
 * Returns: %0 on succes or %-1 on fail
 **/
gint
camel_store_summary_save (CamelStoreSummary *s)
{
	FILE *out;
	gint fd;
	gint i;
	guint32 count;
	CamelStoreInfo *info;
	CamelStoreSummaryClass *class;

	g_return_val_if_fail (CAMEL_IS_STORE_SUMMARY (s), -1);
	g_return_val_if_fail (s->summary_path != NULL, -1);

	io (printf ("** saving summary\n"));

	if ((s->flags & CAMEL_STORE_SUMMARY_DIRTY) == 0) {
		io (printf ("**  summary clean no save\n"));
		return 0;
	}

	fd = g_open (s->summary_path, O_RDWR|O_CREAT|O_TRUNC|O_BINARY, 0600);
	if (fd == -1) {
		io (printf ("**  open error: %s\n", g_strerror (errno)));
		return -1;
	}

	out = fdopen (fd, "wb");
	if ( out == NULL ) {
		i = errno;
		printf ("**  fdopen error: %s\n", g_strerror (errno));
		close (fd);
		errno = i;
		return -1;
	}

	class = CAMEL_STORE_SUMMARY_GET_CLASS (s);
	g_return_val_if_fail (class->summary_header_save != NULL, -1);

	io (printf ("saving header\n"));

	CAMEL_STORE_SUMMARY_LOCK (s, io_lock);

	if (class->summary_header_save (s, out) == -1) {
		i = errno;
		fclose (out);
		CAMEL_STORE_SUMMARY_UNLOCK (s, io_lock);
		errno = i;
		return -1;
	}

	/* now write out each message ... */

	/* FIXME: Locking? */

	count = s->folders->len;
	for (i=0;i<count;i++) {
		info = s->folders->pdata[i];
		class->store_info_save (s, out, info);
	}

	CAMEL_STORE_SUMMARY_UNLOCK (s, io_lock);

	if (fflush (out) != 0 || fsync (fileno (out)) == -1) {
		i = errno;
		fclose (out);
		errno = i;
		return -1;
	}

	if (fclose (out) != 0)
		return -1;

	s->flags &= ~CAMEL_STORE_SUMMARY_DIRTY;
	return 0;
}

/**
 * camel_store_summary_header_load:
 * @summary: a #CamelStoreSummary object
 *
 * Only load the header information from the summary,
 * keep the rest on disk.  This should only be done on
 * a fresh summary object.
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_store_summary_header_load (CamelStoreSummary *s)
{
	CamelStoreSummaryClass *class;
	FILE *in;
	gint ret;

	g_return_val_if_fail (CAMEL_IS_STORE_SUMMARY (s), -1);
	g_return_val_if_fail (s->summary_path != NULL, -1);

	in = g_fopen (s->summary_path, "rb");
	if (in == NULL)
		return -1;

	class = CAMEL_STORE_SUMMARY_GET_CLASS (s);
	g_return_val_if_fail (class->summary_header_load != NULL, -1);

	CAMEL_STORE_SUMMARY_LOCK (s, io_lock);
	ret = class->summary_header_load (s, in);
	CAMEL_STORE_SUMMARY_UNLOCK (s, io_lock);

	fclose (in);
	s->flags &= ~CAMEL_STORE_SUMMARY_DIRTY;
	return ret;
}

/**
 * camel_store_summary_add:
 * @summary: a #CamelStoreSummary object
 * @info: a #CamelStoreInfo
 *
 * Adds a new @info record to the summary.  If @info->uid is %NULL,
 * then a new uid is automatically re-assigned by calling
 * #camel_store_summary_next_uid_string.
 *
 * The @info record should have been generated by calling one of the
 * info_new_*() functions, as it will be free'd based on the summary
 * class.  And MUST NOT be allocated directly using malloc.
 **/
void
camel_store_summary_add (CamelStoreSummary *s, CamelStoreInfo *info)
{
	if (info == NULL)
		return;

	if (camel_store_info_path (s, info) == NULL) {
		g_warning ("Trying to add a folder info with missing required path name\n");
		return;
	}

	CAMEL_STORE_SUMMARY_LOCK (s, summary_lock);

	g_ptr_array_add (s->folders, info);
	g_hash_table_insert (s->folders_path, (gchar *)camel_store_info_path (s, info), info);
	s->flags |= CAMEL_STORE_SUMMARY_DIRTY;

	CAMEL_STORE_SUMMARY_UNLOCK (s, summary_lock);
}

/**
 * camel_store_summary_add_from_path:
 * @summary: a #CamelStoreSummary object
 * @path: item path
 *
 * Build a new info record based on the name, and add it to the summary.
 *
 * Returns: the newly added record
 **/
CamelStoreInfo *
camel_store_summary_add_from_path (CamelStoreSummary *s, const gchar *path)
{
	CamelStoreInfo *info;

	CAMEL_STORE_SUMMARY_LOCK (s, summary_lock);

	info = g_hash_table_lookup (s->folders_path, path);
	if (info != NULL) {
		g_warning ("Trying to add folder '%s' to summary that already has it", path);
		info = NULL;
	} else {
		info = camel_store_summary_info_new_from_path (s, path);
		g_ptr_array_add (s->folders, info);
		g_hash_table_insert (s->folders_path, (gchar *)camel_store_info_path (s, info), info);
		s->flags |= CAMEL_STORE_SUMMARY_DIRTY;
	}

	CAMEL_STORE_SUMMARY_UNLOCK (s, summary_lock);

	return info;
}

/**
 * camel_store_summary_info_new_from_path:
 * @summary: a #CamelStoreSummary object
 * @path: item path
 *
 * Create a new info record from a name.
 *
 * This info record MUST be freed using
 * #camel_store_summary_info_free, #camel_store_info_free will not
 * work.
 *
 * Returns: the #CamelStoreInfo associated with @path
 **/
CamelStoreInfo *
camel_store_summary_info_new_from_path (CamelStoreSummary *s,
                                        const gchar *path)
{
	CamelStoreSummaryClass *class;

	g_return_val_if_fail (CAMEL_IS_STORE_SUMMARY (s), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	class = CAMEL_STORE_SUMMARY_GET_CLASS (s);
	g_return_val_if_fail (class->store_info_new != NULL, NULL);

	return class->store_info_new (s, path);
}

/**
 * camel_store_summary_info_free:
 * @summary: a #CamelStoreSummary object
 * @info: a #CamelStoreInfo
 *
 * Unref and potentially free @info, and all associated memory.
 **/
void
camel_store_summary_info_free (CamelStoreSummary *s, CamelStoreInfo *info)
{
	CamelStoreSummaryClass *class;

	g_return_if_fail (CAMEL_IS_STORE_SUMMARY (s));
	g_return_if_fail (info != NULL);

	class = CAMEL_STORE_SUMMARY_GET_CLASS (s);
	g_return_if_fail (class->store_info_free != NULL);

	CAMEL_STORE_SUMMARY_LOCK (s, ref_lock);

	g_assert (info->refcount >= 1);

	info->refcount--;
	if (info->refcount > 0) {
		CAMEL_STORE_SUMMARY_UNLOCK (s, ref_lock);
		return;
	}

	CAMEL_STORE_SUMMARY_UNLOCK (s, ref_lock);

	class->store_info_free (s, info);
}

/**
 * camel_store_summary_info_ref:
 * @summary: a #CamelStoreSummary object
 * @info: a #CamelStoreInfo
 *
 * Add an extra reference to @info.
 **/
void
camel_store_summary_info_ref (CamelStoreSummary *s, CamelStoreInfo *info)
{
	g_assert (info);
	g_assert (s);

	CAMEL_STORE_SUMMARY_LOCK (s, ref_lock);
	g_assert (info->refcount >= 1);
	info->refcount++;
	CAMEL_STORE_SUMMARY_UNLOCK (s, ref_lock);
}

/**
 * camel_store_summary_touch:
 * @summary: a #CamelStoreSummary object
 *
 * Mark the summary as changed, so that a save will force it to be
 * written back to disk.
 **/
void
camel_store_summary_touch (CamelStoreSummary *s)
{
	CAMEL_STORE_SUMMARY_LOCK (s, summary_lock);
	s->flags |= CAMEL_STORE_SUMMARY_DIRTY;
	CAMEL_STORE_SUMMARY_UNLOCK (s, summary_lock);
}

/**
 * camel_store_summary_clear:
 * @summary: a #CamelStoreSummary object
 *
 * Empty the summary contents.
 **/
void
camel_store_summary_clear (CamelStoreSummary *s)
{
	gint i;

	CAMEL_STORE_SUMMARY_LOCK (s, summary_lock);
	if (camel_store_summary_count (s) == 0) {
		CAMEL_STORE_SUMMARY_UNLOCK (s, summary_lock);
		return;
	}

	for (i=0;i<s->folders->len;i++)
		camel_store_summary_info_free (s, s->folders->pdata[i]);

	g_ptr_array_set_size (s->folders, 0);
	g_hash_table_destroy (s->folders_path);
	s->folders_path = g_hash_table_new (g_str_hash, g_str_equal);
	s->flags |= CAMEL_STORE_SUMMARY_DIRTY;
	CAMEL_STORE_SUMMARY_UNLOCK (s, summary_lock);
}

/**
 * camel_store_summary_remove:
 * @summary: a #CamelStoreSummary object
 * @info: a #CamelStoreInfo
 *
 * Remove a specific @info record from the summary.
 **/
void
camel_store_summary_remove (CamelStoreSummary *s, CamelStoreInfo *info)
{
	CAMEL_STORE_SUMMARY_LOCK (s, summary_lock);
	g_hash_table_remove (s->folders_path, camel_store_info_path (s, info));
	g_ptr_array_remove (s->folders, info);
	s->flags |= CAMEL_STORE_SUMMARY_DIRTY;
	CAMEL_STORE_SUMMARY_UNLOCK (s, summary_lock);

	camel_store_summary_info_free (s, info);
}

/**
 * camel_store_summary_remove_path:
 * @summary: a #CamelStoreSummary object
 * @path: item path
 *
 * Remove a specific info record from the summary, by @path.
 **/
void
camel_store_summary_remove_path (CamelStoreSummary *s, const gchar *path)
{
        CamelStoreInfo *oldinfo;
        gchar *oldpath;

	CAMEL_STORE_SUMMARY_LOCK (s, ref_lock);
	CAMEL_STORE_SUMMARY_LOCK (s, summary_lock);
        if (g_hash_table_lookup_extended (s->folders_path, path, (gpointer)&oldpath, (gpointer)&oldinfo)) {
		/* make sure it doesn't vanish while we're removing it */
		oldinfo->refcount++;
		CAMEL_STORE_SUMMARY_UNLOCK (s, summary_lock);
		CAMEL_STORE_SUMMARY_UNLOCK (s, ref_lock);
		camel_store_summary_remove (s, oldinfo);
		camel_store_summary_info_free (s, oldinfo);
        } else {
		CAMEL_STORE_SUMMARY_UNLOCK (s, summary_lock);
		CAMEL_STORE_SUMMARY_UNLOCK (s, ref_lock);
	}
}

/**
 * camel_store_summary_remove_index:
 * @summary: a #CamelStoreSummary object
 * @index: item index
 *
 * Remove a specific info record from the summary, by index.
 **/
void
camel_store_summary_remove_index (CamelStoreSummary *s, gint index)
{
	CAMEL_STORE_SUMMARY_LOCK (s, summary_lock);
	if (index < s->folders->len) {
		CamelStoreInfo *info = s->folders->pdata[index];

		g_hash_table_remove (s->folders_path, camel_store_info_path (s, info));
		g_ptr_array_remove_index (s->folders, index);
		s->flags |= CAMEL_STORE_SUMMARY_DIRTY;

		CAMEL_STORE_SUMMARY_UNLOCK (s, summary_lock);
		camel_store_summary_info_free (s, info);
	} else {
		CAMEL_STORE_SUMMARY_UNLOCK (s, summary_lock);
	}
}

/**
 * camel_store_summary_info_new:
 * @summary: a #CamelStoreSummary object
 *
 * Allocate a new #CamelStoreInfo, suitable for adding to this
 * summary.
 *
 * Returns: the newly allocated #CamelStoreInfo
 **/
CamelStoreInfo *
camel_store_summary_info_new (CamelStoreSummary *s)
{
	CamelStoreInfo *info;

	info = g_slice_alloc0 (s->store_info_size);
	info->refcount = 1;
	return info;
}

/**
 * camel_store_info_string:
 * @summary: a #CamelStoreSummary object
 * @info: a #CamelStoreInfo
 * @type: specific string being requested
 *
 * Get a specific string from the @info.
 *
 * Returns: the string value
 **/
const gchar *
camel_store_info_string (CamelStoreSummary *s,
                         const CamelStoreInfo *info,
                         gint type)
{
	CamelStoreSummaryClass *class;

	g_return_val_if_fail (CAMEL_IS_STORE_SUMMARY (s), NULL);
	g_return_val_if_fail (info != NULL, NULL);

	class = CAMEL_STORE_SUMMARY_GET_CLASS (s);
	g_return_val_if_fail (class->store_info_string != NULL, NULL);

	return class->store_info_string (s, info, type);
}

/**
 * camel_store_info_set_string:
 * @summary: a #CamelStoreSummary object
 * @info: a #CamelStoreInfo
 * @type: specific string being set
 * @value: string value to set
 *
 * Set a specific string on the @info.
 **/
void
camel_store_info_set_string (CamelStoreSummary *s,
                             CamelStoreInfo *info,
                             gint type,
                             const gchar *value)
{
	CamelStoreSummaryClass *class;

	/* XXX Can 'value' be NULL? */
	g_return_if_fail (CAMEL_IS_STORE_SUMMARY (s));
	g_return_if_fail (info != NULL);

	class = CAMEL_STORE_SUMMARY_GET_CLASS (s);
	g_return_if_fail (class->store_info_set_string != NULL);

	class->store_info_set_string (s, info, type, value);
}

