/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-message-cache.c: Class for a Camel cache.
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 2001 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 2 of the GNU General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include "camel-i18n.h"
#include "camel-data-cache.h"
#include "camel-exception.h"
#include "camel-stream-fs.h"
#include "camel-stream-mem.h"
#include "camel-file-utils.h"

extern int camel_verbose_debug;
#define dd(x) (camel_verbose_debug?(x):0)
#define d(x)

/* how many 'bits' of hash are used to key the toplevel directory */
#define CAMEL_DATA_CACHE_BITS (6)
#define CAMEL_DATA_CACHE_MASK ((1<<CAMEL_DATA_CACHE_BITS)-1)

/* timeout before a cache dir is checked again for expired entries,
   once an hour should be enough */
#define CAMEL_DATA_CACHE_CYCLE_TIME (60*60)

struct _cdc_entry {
	char *ckey;
	char *tmp;
	char *real;
	GCond *cond;
};

struct _CamelDataCachePrivate {
	GMutex *busy_lock;
	GHashTable *busy_table, *busy_stream_table;

	int expire_inc;
	time_t expire_last[1<<CAMEL_DATA_CACHE_BITS];
};

static CamelObject *camel_data_cache_parent;

static void
data_cache_expire(CamelDataCache *cdc, const char *path, const char *keep, time_t now)
{
	DIR *dir;
	struct dirent *d;
	GString *s;
	struct stat st;

	dir = opendir(path);
	if (dir == NULL)
		return;

	s = g_string_new("");
	while ( (d = readdir(dir)) ) {
		if (strcmp(d->d_name, keep) == 0)
			continue;
		
		g_string_printf (s, "%s/%s", path, d->d_name);
		dd(printf("Checking '%s' for expiry\n", s->str));
		if (stat(s->str, &st) == 0
		    && S_ISREG(st.st_mode)
		    && ((cdc->expire_age != -1 && st.st_mtime + cdc->expire_age < now)
			|| (cdc->expire_access != -1 && st.st_atime + cdc->expire_access < now))) {
			dd(printf("Has expired!  Removing!\n"));
			unlink(s->str);
		}
	}
	g_string_free(s, TRUE);
	closedir(dir);
}

/* Since we have to stat the directory anyway, we use this opportunity to
   lazily expire old data.
   If it is this directories 'turn', and we haven't done it for CYCLE_TIME seconds,
   then we perform an expiry run */
static char *
data_cache_path(CamelDataCache *cdc, const char *path, const char *key)
{
	char *dir, *real, *tmp;
	guint32 hash;

	hash = g_str_hash(key);
	hash = (hash>>5)&CAMEL_DATA_CACHE_MASK;
	if (path) {
		dir = alloca(strlen(cdc->path) + strlen(path) + 8);
		sprintf(dir, "%s/%s/%02x", cdc->path, path, hash);
	} else {
		dir = alloca(strlen(cdc->path) + 8);
		sprintf(dir, "%s/%02x", cdc->path, hash);
	}
	if (access(dir, F_OK) == -1) {
		camel_mkdir (dir, 0700);
	} else if (cdc->priv->expire_inc == hash
		   && (cdc->expire_age != -1 || cdc->expire_access != -1)) {
		time_t now;

		dd(printf("Checking expire cycle time on dir '%s'\n", dir));

		/* This has a race, but at worst we re-run an expire cycle which is safe */
		now = time(0);
		if (cdc->priv->expire_last[hash] + CAMEL_DATA_CACHE_CYCLE_TIME < now) {
			cdc->priv->expire_last[hash] = now;
			data_cache_expire(cdc, dir, key, now);
		}
		cdc->priv->expire_inc = (cdc->priv->expire_inc + 1) & CAMEL_DATA_CACHE_MASK;
	}

	tmp = camel_file_util_safe_filename(key);
	real = g_strdup_printf("%s/%s", dir, tmp);
	g_free(tmp);

	return real;
}

static void data_cache_class_init(CamelDataCacheClass *klass)
{
	camel_data_cache_parent = (CamelObject *)camel_object_get_type ();

	klass->path = data_cache_path;
}

static void data_cache_init(CamelDataCache *cdc, CamelDataCacheClass *klass)
{
	struct _CamelDataCachePrivate *p;

	p = cdc->priv = g_malloc0(sizeof(*cdc->priv));
	p->busy_lock = g_mutex_new();
	p->busy_table = g_hash_table_new(g_str_hash, g_str_equal);
	p->busy_stream_table = g_hash_table_new(NULL, NULL);
}

static void data_cache_finalise(CamelDataCache *cdc)
{
	struct _CamelDataCachePrivate *p;

	p = cdc->priv;

	g_assert(g_hash_table_size(p->busy_table) == 0);
	g_assert(g_hash_table_size(p->busy_stream_table) == 0);

	g_hash_table_destroy(p->busy_table);
	g_hash_table_destroy(p->busy_stream_table);
	g_mutex_free(p->busy_lock);
	g_free(p);
	
	g_free (cdc->path);
}

CamelType
camel_data_cache_get_type(void)
{
	static CamelType camel_data_cache_type = CAMEL_INVALID_TYPE;
	
	if (camel_data_cache_type == CAMEL_INVALID_TYPE) {
		camel_data_cache_type = camel_type_register(
			CAMEL_OBJECT_TYPE, "CamelDataCache",
			sizeof (CamelDataCache),
			sizeof (CamelDataCacheClass),
			(CamelObjectClassInitFunc) data_cache_class_init,
			NULL,
			(CamelObjectInitFunc) data_cache_init,
			(CamelObjectFinalizeFunc) data_cache_finalise);
	}

	return camel_data_cache_type;
}

/**
 * camel_data_cache_new:
 * @path: Base path of cache, subdirectories will be created here.
 * @flags: Open flags, none defined.
 * @ex: 
 * 
 * Create a new data cache.
 * 
 * Return value: A new cache object, or NULL if the base path cannot
 * be written to.
 **/
CamelDataCache *
camel_data_cache_new(const char *path, guint32 flags, CamelException *ex)
{
	CamelDataCache *cdc;

	if (camel_mkdir (path, 0700) == -1) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Unable to create cache path"));
		return NULL;
	}

	cdc = (CamelDataCache *)camel_object_new(CAMEL_DATA_CACHE_TYPE);

	cdc->path = g_strdup(path);
	cdc->flags = flags;
	cdc->expire_age = -1;
	cdc->expire_access = -1;

	return cdc;
}

/**
 * camel_data_cache_set_expire_age:
 * @cdc: 
 * @when: Timeout for age expiry, or -1 to disable.
 * 
 * Set the cache expiration policy for aged entries.
 * 
 * Items in the cache older than @when seconds may be
 * flushed at any time.  Items are expired in a lazy
 * manner, so it is indeterminate when the items will
 * physically be removed.
 *
 * Note you can set both an age and an access limit.  The
 * age acts as a hard limit on cache entries.
 **/
void
camel_data_cache_set_expire_age(CamelDataCache *cdc, time_t when)
{
	cdc->expire_age = when;
}

/**
 * camel_data_cache_set_expire_access:
 * @cdc: 
 * @when: Timeout for access, or -1 to disable access expiry.
 * 
 * Set the cache expiration policy for access times.
 *
 * Items in the cache which haven't been accessed for @when
 * seconds may be expired at any time.  Items are expired in a lazy
 * manner, so it is indeterminate when the items will
 * physically be removed.
 *
 * Note you can set both an age and an access limit.  The
 * age acts as a hard limit on cache entries.
 **/
void
camel_data_cache_set_expire_access(CamelDataCache *cdc, time_t when)
{
	cdc->expire_access = when;
}

/* Convert a path/key into a filesystem path.
   On exit, the path should exist as well */
char *
camel_data_cache_path(CamelDataCache *cdc, const char *path, const char *key)
{
	return ((CamelDataCacheClass *)((CamelObject *)cdc)->klass)->path(cdc, path, key);
}

/* should be virtual too? */
static char *
cdc_tmp_path(CamelDataCache *cdc, const char *path, const char *key)
{
	char *dir;

	if (path) {
		dir = g_alloca(strlen(cdc->path)+strlen(path)+20);
		sprintf(dir, "%s/%s/tmp", cdc->path, path);
	} else {
		dir = g_alloca(strlen(cdc->path)+20);
		sprintf(dir, "%s/tmp", cdc->path);
	}

	camel_mkdir(dir, 0777);

	return g_strdup_printf("%s/%08x", dir, g_str_hash(key));
}

/* MUST only be called from abort or commit */
static void
centry_free(struct _cdc_entry *centry)
{
	g_cond_broadcast(centry->cond);
	g_free(centry->ckey);
	g_free(centry->tmp);
	g_free(centry->real);
	g_cond_free(centry->cond);
	g_free(centry);
}

/* Commit a stream to the cache.  Returning a stream to the committed
   data.  The stream passed must not have been closed yet, and must
   not be re-used after calling this function.
 */
void
camel_data_cache_commit(CamelDataCache *cdc, CamelStream *stream, CamelException *ex)
{
	struct _cdc_entry *centry;

	g_assert(((CamelObject *)stream)->ref_count == 1);

	/* Overwrite anything in the cache already.  If for whatever
	   reason we fail on an otherwise good commit, we
	   just return the original stream.

	   If we were removed while this was running, or another add
	   was called on the same object, then the link will fail
	   automatically, etc. */

	g_mutex_lock(cdc->priv->busy_lock);
	centry = g_hash_table_lookup(cdc->priv->busy_stream_table, stream);
	g_assert(centry != NULL);

	g_hash_table_remove(cdc->priv->busy_stream_table, stream);
	g_hash_table_remove(cdc->priv->busy_table, centry->ckey);

	if (camel_stream_flush(stream) == -1
	    || camel_stream_close(stream) == -1
	    || link(centry->tmp, centry->real) == -1) {
		if (errno == EINTR)
			camel_exception_setv(ex, CAMEL_EXCEPTION_USER_CANCEL, _("Cancelled."));
		else
			camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, _("Error writing to cache: %s"), g_strerror (errno));
	}

	camel_object_unref(stream);
	unlink(centry->tmp);

	centry_free(centry);
	g_mutex_unlock(cdc->priv->busy_lock);
}

void
camel_data_cache_abort(CamelDataCache *cdc, CamelStream *stream)
{
	struct _cdc_entry *centry;

	g_mutex_lock(cdc->priv->busy_lock);

	centry = g_hash_table_lookup(cdc->priv->busy_stream_table, stream);
	g_assert(centry != NULL);

	g_hash_table_remove(cdc->priv->busy_stream_table, stream);
	g_hash_table_remove(cdc->priv->busy_table, centry->ckey);
	camel_object_unref(stream);
	unlink(centry->tmp);
	centry_free(centry);

	g_mutex_unlock(cdc->priv->busy_lock);
}

/**
 * camel_data_cache_get:
 * @cdc: 
 * @path: Path to the (sub) cache the item exists in.
 * @key: Key for the cache item.
 * @reserve: reserve stream
 * @ex: 
 * 
 * Lookup an item in the cache.  If the item exists, a stream
 * is returned for the item.  Each caller gets its own
 * unique stream.
 * 
 * If reserve is non-null, and the item is not present,
 * then it is atomically reserved for loading.  Once
 * finished with it must be either commited or aborted.
 *
 * Return value: A cache item, or NULL if the cache item does not exist.
 **/
CamelStream *
camel_data_cache_get(CamelDataCache *cdc, const char *path, const char *key, CamelStream **reserve, CamelException *ex)
{
	char *real, *ckey;
	CamelStream *stream;
	struct _cdc_entry *centry;

	/* First we try to lookup the entry on disk.  If it is there,
	   it must be valid and it must be done.  Lets go ...

	   If not, check to see if anyone is waiting to write one
	   out.  We need to loop incase another add() comes
	   along and overwrites the first.  Then, we just
	   try to open the file, which will exist if commit()
	   ran successfully, or wont if we aborted and is
	   no use anyway.

	   If after all that we can't open a stream, and reserve
	   is supplied, we atomically reserve the name.
	*/

	if (reserve)
		*reserve = NULL;

	real = camel_data_cache_path(cdc, path, key);
	stream = camel_stream_fs_new_with_name(real, O_RDONLY, 0);
	if (stream)
		goto done;

	ckey = g_strdup_printf("%s/%s", path, key);

	g_mutex_lock(cdc->priv->busy_lock);

	centry = g_hash_table_lookup(cdc->priv->busy_table, ckey);
	if (centry) {
		do {
			g_cond_wait(centry->cond, cdc->priv->busy_lock);
			centry = g_hash_table_lookup(cdc->priv->busy_table, ckey);
		} while (centry);
		stream = camel_stream_fs_new_with_name(real, O_RDONLY, 0);
	}
	if (stream == NULL && reserve) {
		char * tmp = cdc_tmp_path(cdc, path, key);

		unlink(real);
		unlink(tmp);

		*reserve = camel_stream_fs_new_with_name(tmp, O_RDWR|O_CREAT|O_TRUNC, 0666);
		if (*reserve) {
			centry = g_malloc(sizeof(*centry));
			centry->ckey = ckey;
			centry->tmp = tmp;
			centry->real = real;
			centry->cond = g_cond_new();
			g_hash_table_insert(cdc->priv->busy_table, centry->ckey, centry);
			g_hash_table_insert(cdc->priv->busy_stream_table, *reserve, centry);
			ckey = NULL;
			real = NULL;
		} else {
			if (errno == EINTR)
				camel_exception_setv(ex, CAMEL_EXCEPTION_USER_CANCEL, _("Cancelled."));
			else
				camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, _("Error writing to cache: %s"), g_strerror(errno));
			g_free(tmp);
		}
	}

	g_mutex_unlock(cdc->priv->busy_lock);

	g_free(ckey);
done:
	g_free(real);

	return stream;
}

/**
 * camel_data_cache_remove:
 * @cdc: 
 * @path: 
 * @key: 
 * 
 * Remove/expire a cache item.
 **/
void
camel_data_cache_remove(CamelDataCache *cdc, const char *path, const char *key)
{
	char *real, *tmp;

	/* By unlinking tmp first, we should automagically
	   synchronise properly with the commit code, its
	   not like we should really need to anyway */

	real = camel_data_cache_path(cdc, path, key);
	tmp = cdc_tmp_path(cdc, path, key);

	unlink(tmp);
	unlink(real);

	g_free(tmp);
	g_free(real);
}

/**
 * camel_data_cache_rename:
 * @cache: 
 * @old: 
 * @new: 
 * @ex: 
 * 
 * Rename a cache path.  All cache items accessed from the old path
 * are accessible using the new path.
 *
 * CURRENTLY UNIMPLEMENTED
 * 
 * Return value: -1 on error.
 **/
int camel_data_cache_rename(CamelDataCache *cache,
			    const char *old, const char *new, CamelException *ex)
{
	/* blah dont care yet */
	return -1;
}

/**
 * camel_data_cache_clear:
 * @cache: 
 * @path: Path to clear, or NULL to clear all items in
 * all paths.
 * @ex: 
 * 
 * Clear all items in a given cache path or all items in the cache.
 * 
 * CURRENTLY_UNIMPLEMENTED
 *
 * Return value: -1 on error.
 **/
int
camel_data_cache_clear(CamelDataCache *cache, const char *path, CamelException *ex)
{
	/* nor for this? */
	return -1;
}
