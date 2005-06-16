/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Not Zed <notzed@lostzed.mmc.com.au>
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

#include <ctype.h>

#include "camel-maildir-summary.h"
#include <camel/camel-mime-message.h>
#include <camel/camel-operation.h>
#include <camel/camel-record.h>

#include "camel-private.h"
#include "libedataserver/e-memory.h"
#include "camel-i18n.h"

#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/
#define io(x)

#define CAMEL_MAILDIR_SUMMARY_VERSION (0x2000)

#define CFS_CLASS(x) ((CamelFolderSummaryClass *)((CamelObject *)x)->klass)

#define _PRIVATE(x) (((CamelMaildirSummary *)(x))->priv)

struct _CamelMaildirSummaryPrivate {
	char *hostname;
};

static CamelLocalSummaryClass *parent_class;

/**
 * camel_maildir_summary_new:
 * @folder: parent folder.
 * @filename: Path to root of this maildir directory (containing new/tmp/cur directories).
 * @index: Index if one is reqiured.
 *
 * Create a new CamelMaildirSummary object.
 * 
 * Return value: A new #CamelMaildirSummary object.
 **/
CamelMaildirSummary	*camel_maildir_summary_new(struct _CamelFolder *folder, const char *filename, const char *maildirdir, CamelIndex *index)
{
	CamelMaildirSummary *o = (CamelMaildirSummary *)camel_object_new(camel_maildir_summary_get_type ());

	camel_local_summary_construct((CamelLocalSummary *)o, folder, filename, maildirdir, index);
	return o;
}

char *camel_maildir_summary_next_uid(CamelMaildirSummary *mds)
{
	struct timeval tv;
	static unsigned int step=1;

	/* See http://cr.yp.to/proto/maildir.html */

	gettimeofday(&tv, NULL);
	return g_strdup_printf("%ld.M%uP%uQ%u.%s", (long int)tv.tv_sec, (unsigned int)tv.tv_usec, getpid(), step++, mds->priv->hostname);
}

/* the 'standard' maildir flags.  must be defined in sorted order. */
static struct {
	char flag;
	guint32 flagbit;
} flagbits[] = {
	{ 'D', CAMEL_MESSAGE_DRAFT },
	{ 'F', CAMEL_MESSAGE_FLAGGED },
	/*{ 'P', CAMEL_MESSAGE_FORWARDED },*/
	{ 'R', CAMEL_MESSAGE_ANSWERED },
	{ 'S', CAMEL_MESSAGE_SEEN },
	{ 'T', CAMEL_MESSAGE_DELETED },
};

/* convert the uid + flags into a unique:info maildir format */
char *camel_maildir_summary_info_to_name(const CamelMaildirMessageInfo *info)
{
	const char *uid;
	char *p, *buf;
	int i;
	guint32 flags = ((CamelMessageInfoBase *)info)->flags;

	uid = camel_message_info_uid (info);
	buf = g_alloca (strlen (uid) + strlen (":2,") +  (sizeof (flagbits) / sizeof (flagbits[0])) + 1);
	p = buf + sprintf (buf, "%s:2,", uid);
	for (i = 0; i < sizeof (flagbits) / sizeof (flagbits[0]); i++) {
		if (flags & flagbits[i].flagbit)
			*p++ = flagbits[i].flag;
	}
	*p = 0;
	
	return g_strdup(buf);
}

static int
safe_equal(const char *a, const char *b)
{
	if (a == NULL)
		return b == NULL;
	else if (b == NULL)
		return FALSE;

	return strcmp(a, b) == 0;
}

static void
maildir_info_to_ext(CamelMaildirMessageInfo *info, char ext[16])
{
	char *p;
	int i;

	strcpy(ext, "2,");
	p = ext+2;
	for (i = 0; i < sizeof (flagbits) / sizeof (flagbits[0]); i++) {
		if (((CamelMessageInfoBase *)info)->flags & flagbits[i].flagbit)
			*p++ = flagbits[i].flag;
	}
	*p = 0;
}

/* returns 0 if the info matches (or there was none), otherwise we changed it */
static void maildir_name_to_info(CamelMaildirMessageInfo *info, const char *name, const char *ext)
{
	const char *p;
	char c;
	guint32 flags = ((CamelMessageInfoBase *)info)->flags;
	guint32 set = 0;	/* what we set */
	guint32 all = 0;	/* all flags */
	int i;

	/* If the folder is flagged, we changed it, so dont 'fix' the flags here */
	if ((flags & CAMEL_MESSAGE_FOLDER_FLAGGED) == 0
	    && ext && !strncmp(ext, "2,", 2)) {
		p = ext+2;
		while ((c = *p++)) {
			/* we could assume that the flags are in order, but its just as easy not to require */
			for (i=0;i<sizeof(flagbits)/sizeof(flagbits[0]);i++) {
				if (flagbits[i].flag == c && (flags & flagbits[i].flagbit) == 0) {
					set |= flagbits[i].flagbit;
				}
			}
		}
		all = CAMEL_MESSAGE_DRAFT|CAMEL_MESSAGE_FLAGGED|CAMEL_MESSAGE_ANSWERED|CAMEL_MESSAGE_SEEN|CAMEL_MESSAGE_DELETED;
	}

	/* we always need to make sure the name matches though */
	if (!safe_equal(ext, info->ext)) {
		g_free(info->ext);
		info->ext = g_strdup(ext);
		all = set = CAMEL_MESSAGE_FOLDER_FLAGGED;
	}

	if (all)
		camel_message_info_set_flags((CamelMessageInfo *)info, all, set);

	if (((CamelMessageInfoBase *)info)->uid == NULL)
		((CamelMessageInfoBase *)info)->uid = g_strdup(name);
}

static CamelMessageInfo *
maildir_info_new(CamelMaildirSummary *mds, const char *name, const char *ext)
{
	char *filename;
	int fd;
	CamelMimeParser *mp;
	CamelMessageInfo *info;

	if (ext)
		filename = g_strdup_printf("%s/cur/%s:%s", ((CamelLocalSummary *)mds)->folder_path, name, ext);
	else
		filename = g_strdup_printf("%s/cur/%s", ((CamelLocalSummary *)mds)->folder_path, name);

	fd = open(filename, O_RDONLY);
	if (fd == -1)
		return NULL;

	mp = camel_mime_parser_new();
	camel_mime_parser_scan_from(mp, FALSE);
	camel_mime_parser_init_with_fd(mp, fd);

	info = camel_message_info_new_from_parser((CamelFolderSummary *)mds, mp);

	/* INDEX CONTENT HERE */

	camel_object_unref(mp);

	if (info)
		maildir_name_to_info((CamelMaildirMessageInfo *)info, name, ext);

	return info;
}

/* virtual methods */

static void maildir_message_info_free(CamelMessageInfo *mi)
{
	g_free(((CamelMaildirMessageInfo *)mi)->ext);

	((CamelFolderSummaryClass *) parent_class)->message_info_free(mi);
}

static CamelMessageInfo *maildir_message_info_new_from_message(CamelFolderSummary *s, CamelMimeMessage *msg, const CamelMessageInfo *info)
{
	CamelMessageInfo *mi = ((CamelFolderSummaryClass *)parent_class)->message_info_new_from_message(s, msg, info);

	if (mi) {
		char ext[8];

		maildir_info_to_ext((CamelMaildirMessageInfo *)mi, ext);
		((CamelMaildirMessageInfo *)mi)->ext = g_strdup(ext);
	}

	return mi;
}

static int
maildir_uid_cmp(const void *a, const void *b, void *d)
{
	unsigned long av, bv;
	char *ae, *be;

	/* We only need the uid to be approximately
	   time.somethingelse
	   To get a reasonable sort order.
	   We just strcmp everything that isn't a number if the numbers are equal,
	   guaranteeing that we always have a static sort order */

	av = strtoul(a, &ae, 10);
	bv = strtoul(b, &be, 10);

	if (av < bv)
		return -1;
	else if (av > bv)
		return 1;

	return strcmp(ae, be);
}

static int
maildir_decode(CamelFolderSummaryDisk *s, CamelMessageInfoDisk *mi, CamelRecordDecoder *crd)
{
	int tag, ver, res = -1;

	io(printf("loading mdir message info\n"));

	if (((CamelFolderSummaryDiskClass *)parent_class)->decode(s, mi, crd) != 0)
		return res;

	camel_record_decoder_reset(crd);
	while ((tag = camel_record_decoder_next_section(crd, &ver)) != CR_SECTION_INVALID) {
		switch (tag) {
		case CFS_MDIR_SECTION_INFO:
			((CamelMaildirMessageInfo *)mi)->ext = g_strdup(camel_record_decoder_string(crd));
			res = 0;
			break;
		}
	}

	return res;
}

static void
maildir_encode(CamelFolderSummaryDisk *s, CamelMessageInfoDisk *mi, CamelRecordEncoder *cre)
{
	io(printf("saving mdir message info\n"));

	((CamelFolderSummaryDiskClass *)parent_class)->encode(s, mi, cre);

	camel_record_encoder_start_section(cre, CFS_MDIR_SECTION_INFO, 0);
	camel_record_encoder_string(cre, ((CamelMaildirMessageInfo *)mi)->ext);
	camel_record_encoder_end_section(cre);
}

/* Checks for mail new.  We assume the names are in good format */
static int
maildir_summary_check_new(CamelLocalSummary *cls, CamelFolderChangeInfo *changes, CamelException *ex)
{
	DIR *dir;
	struct dirent *d;
	GString *new, *cur;
	int newlen, curlen;

	new = g_string_new(cls->folder_path);
	g_string_append(new, "/new/");

	dir = opendir(new->str);
	if (dir == NULL) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot check folder: %s: %s"),
				     new->str, g_strerror(errno));
		g_string_free(new, TRUE);
		return -1;
	}

	newlen = new->len;
	cur = g_string_new(cls->folder_path);
	g_string_append(cur, "/cur/");
	curlen = cur->len;
	while ( (d = readdir(dir)) ) {
		CamelMessageInfo *info;

		if (d->d_name[0] == '.')
			continue;

		g_string_truncate(cur, curlen);
		g_string_truncate(new, newlen);
		g_string_append_printf(cur, "%s:2,", d->d_name);
		g_string_append_printf(new, "%s", d->d_name);

		if (link(new->str, cur->str) != 0)
			continue;
		unlink(new->str);

		info = maildir_info_new((CamelMaildirSummary *)cls, d->d_name, "2,");
		camel_folder_change_info_add_uid(changes, info->uid);
		camel_folder_change_info_recent_uid(changes, info->uid);
		camel_folder_summary_add((CamelFolderSummary *)cls, info);
		camel_message_info_free(info);
	}
	closedir(dir);

	g_string_free(new, TRUE);
	g_string_free(cur, TRUE);

	return 0;
}

static int
maildir_uid_ptr_cmp(const void *a, const void *b, void *d)
{
	GCompareDataFunc uid_cmp = CFS_CLASS(d)->uid_cmp;

	return uid_cmp(*((const void **)a), *((const void **)b), d);
}

/* Checks for new mail that crept in unknowingly, or changed flags, optionally expunges */
static int
maildir_summary_check_cur(CamelLocalSummary *cls, int expunge, CamelFolderChangeInfo *changes, CamelException *ex)
{
	DIR *dir;
	struct dirent *d;
	CamelMessageInfo *info;
	const CamelMessageInfo *iterinfo;
	CamelMessageIterator *iter;
	CamelFolderSummary *s = (CamelFolderSummary *)cls;
	int i;
	char *cur;
	GPtrArray *names;
	GString *name;
	GCompareDataFunc uid_cmp = CFS_CLASS(s)->uid_cmp;

	/* We use gstrings so we can cheaply build names if we need to rename the files */
	cur = g_alloca(strlen(cls->folder_path) + 8);
	sprintf(cur, "%s/cur", cls->folder_path);

	dir = opendir(cur);
	if (dir == NULL)
		return -1;

	camel_operation_start(NULL, _("Checking folder consistency"));

	/* Build a list of all files, split names into 'type' + 'uid' + 'ext'(enssion)
	   Pointer added points to the uid so we can sort it */

	names = g_ptr_array_new();
	while ( (d = readdir(dir)) ) {
		char *uid, *tmp;
		int len;

		if (d->d_name[0] == '.')
			continue;

		len = strlen(d->d_name);
		uid = g_malloc(len+2);
		strcpy(uid+1, d->d_name);
		tmp = strchr(uid+1, ':');
		if (tmp) {
			uid[0] = 1;
			tmp[0] = 0;
		} else
			uid[0] = 0;

		g_ptr_array_add(names, uid+1);
	}
	closedir(dir);

	g_qsort_with_data(names->pdata, names->len, sizeof(names->pdata[0]), maildir_uid_ptr_cmp, s);

	/* Now perform an intersection with the summary
	   Any new messages: add them
	   Existing: Expunge if we should, check flags match otherwise
	   Not there: Remove them */

	name = g_string_new("");

	iter = camel_folder_summary_search(s, NULL, NULL, NULL, NULL);
	iterinfo = camel_message_iterator_next(iter, NULL);
	for (i=0;i<names->len;i++) {
		char *uid = names->pdata[i];
		char *ext;

		camel_operation_progress(NULL, i*100/names->len);

		if (uid[-1] == 0)
			ext = NULL;
		else
			ext = uid+strlen(uid)+1;

		while (iterinfo && uid_cmp(iterinfo->uid, uid, s) < 0) {
			camel_folder_change_info_remove_uid(changes, camel_message_info_uid(iterinfo));
			camel_folder_summary_remove(s, (CamelMessageInfo *)iterinfo);
			iterinfo = camel_message_iterator_next(iter, NULL);
		}

		if (iterinfo && uid_cmp(iterinfo->uid, uid, s) == 0) {
			CamelMaildirMessageInfo *mi = (CamelMaildirMessageInfo *)iterinfo;

			if (expunge && (((CamelMessageInfoBase *)mi)->flags & CAMEL_MESSAGE_DELETED)) {
				if (ext)
					g_string_printf(name, "%s/cur/%s:%s", cls->folder_path, uid, ext);
				else
					g_string_printf(name, "%s/cur/%s", cls->folder_path, uid);
				unlink(name->str);
				camel_folder_change_info_remove_uid(changes, camel_message_info_uid(iterinfo));
				camel_folder_summary_remove(s, (CamelMessageInfo *)iterinfo);
			} else {
				maildir_name_to_info(mi, uid, ext);
			}
			iterinfo = camel_message_iterator_next(iter, NULL);
		} else {
			/* TODO: do we fix no-extension names here?? */
			info = maildir_info_new((CamelMaildirSummary *)cls, uid, ext);
			camel_folder_change_info_add_uid(changes, uid);
			camel_folder_summary_add(s, info);
			camel_message_info_free(info);
		}

		g_free(uid-1);
	}

	g_string_free(name, TRUE);
	g_ptr_array_free(names, TRUE);

	while (iterinfo) {
		camel_folder_change_info_remove_uid(changes, camel_message_info_uid(iterinfo));
		camel_folder_summary_remove(s, (CamelMessageInfo *)iterinfo);
		iterinfo = camel_message_iterator_next(iter, NULL);
	}
	camel_message_iterator_free(iter);

	camel_operation_end(NULL);

	return 0;
}

static int
maildir_summary_check(CamelLocalSummary *cls, CamelFolderChangeInfo *changes, CamelException *ex)
{
	/* We only check for new mail */
	maildir_summary_check_new(cls, changes, ex);

	return ((CamelLocalSummaryClass *)parent_class)->check(cls, changes, ex);
}

/* sync the summary with the ondisk files. */
static int
maildir_summary_sync(CamelLocalSummary *cls, gboolean expunge, CamelFolderChangeInfo *changes, CamelException *ex)
{
	maildir_summary_check_cur(cls, expunge, changes, ex);
	maildir_summary_check_new(cls, changes, ex);

	return ((CamelLocalSummaryClass *)parent_class)->sync(cls, expunge, changes, ex);
}

static void
maildir_sync_changes(CamelFolderSummaryDisk *cds, GPtrArray *changes, CamelException *ex)
{
	GString *cur0, *cur1;
	int i, curlen;

	printf("start: maildir '%s' sync changes\n", ((CamelLocalSummary *)cds)->folder_path);

	/* Note that even though maildir is unlocked, we are locked in-process,
	   This is to ensure the sync code here and the check code above
	   aren't in a race to update the summary.

	   I think ... */
#if 0
	cur0 = g_string_new(((CamelLocalSummary *)cds)->folder_path);
	g_string_append(cur0, "/cur/");
	curlen = cur0->len;
	cur1 = g_string_new(cur0->str);

	for (i = 0; i < changes->len; i++) {
		CamelMaildirMessageInfo *info = changes->pdata[i];
		char ext[16];

		maildir_info_to_ext(info, ext);
		if (info->ext == NULL || strcmp(info->ext, ext) != 0) {
			g_string_truncate(cur0, curlen);
			g_string_truncate(cur1, curlen);
			g_string_append(cur0, ((CamelMessageInfoBase *)info)->uid);
			g_string_append(cur1, ((CamelMessageInfoBase *)info)->uid);
			if (info->ext) {
				g_string_append_c(cur0, ':');
				g_string_append(cur0, info->ext);
			}
			g_string_append_c(cur1, ':');
			g_string_append(cur1, ext);

			if (link(cur0->str, cur1->str) == 0)
				unlink(cur0->str);
			g_free(info->ext);
			info->ext = g_strdup(ext);
		}

		((CamelMessageInfoBase *)changes->pdata[i])->flags &= ~CAMEL_MESSAGE_FOLDER_FLAGGED;
	}

	g_string_free(cur0, TRUE);
	g_string_free(cur1, TRUE);
#endif

	((CamelFolderSummaryDiskClass *)parent_class)->sync(cds, changes, ex);

	printf("end  : maildir '%s' sync changes\n", ((CamelLocalSummary *)cds)->folder_path);
}

static void
camel_maildir_summary_class_init (CamelMaildirSummaryClass *klass)
{
	parent_class = (CamelLocalSummaryClass *)camel_local_summary_get_type();

	((CamelFolderSummaryClass *)klass)->messageinfo_sizeof = sizeof(CamelMaildirMessageInfo);
	((CamelFolderSummaryClass *)klass)->uid_cmp = maildir_uid_cmp;
	((CamelFolderSummaryClass *)klass)->message_info_free = maildir_message_info_free;

	((CamelFolderSummaryClass *)klass)->message_info_new_from_message = maildir_message_info_new_from_message;

	((CamelFolderSummaryDiskClass *)klass)->encode = maildir_encode;
	((CamelFolderSummaryDiskClass *)klass)->decode = maildir_decode;

	((CamelFolderSummaryDiskClass *)klass)->sync = maildir_sync_changes;

	((CamelLocalSummaryClass *)klass)->check = maildir_summary_check;
	((CamelLocalSummaryClass *)klass)->sync = maildir_summary_sync;
}

static void
camel_maildir_summary_init (CamelMaildirSummary *o)
{
	char hostname[256];

	o->priv = g_malloc0(sizeof(*o->priv));

	if (gethostname(hostname, 256) == 0) {
		GString *h = g_string_new("");
		char *p = hostname, c;

		while ((c=*p++)) {
			if (c == '/')
				g_string_append(h, "\\057");
			else if (c == ':')
				g_string_append(h, "\\072");
			else
				g_string_append_c(h, c);
			
		}
		o->priv->hostname = g_strdup(h->str);
		g_string_free(h, TRUE);
	} else {
		o->priv->hostname = g_strdup("localhost");
	}
}

static void
camel_maildir_summary_finalise(CamelObject *obj)
{
	CamelMaildirSummary *o = (CamelMaildirSummary *)obj;

	g_free(o->priv->hostname);
	g_free(o->priv);
}

CamelType
camel_maildir_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register(camel_local_summary_get_type (), "CamelMaildirSummary",
					   sizeof(CamelMaildirSummary),
					   sizeof(CamelMaildirSummaryClass),
					   (CamelObjectClassInitFunc)camel_maildir_summary_class_init,
					   NULL,
					   (CamelObjectInitFunc)camel_maildir_summary_init,
					   (CamelObjectFinalizeFunc)camel_maildir_summary_finalise);
	}
	
	return type;
}
