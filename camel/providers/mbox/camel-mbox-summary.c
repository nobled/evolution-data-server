/*
 *  Copyright (C) 2000 Helix Code Inc.
 *
 *  Authors: Michael Zucchi <notzed@helixcode.com>
 *
 *  This program is free software; you can redistribute it and/or 
 *  modify it under the terms of the GNU General Public License as 
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 *  USA
 */

#include "camel-mbox-summary.h"
#include <camel/camel-mime-message.h>

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define io(x)

#define CAMEL_MBOX_SUMMARY_VERSION (0x1000)

struct _CamelMboxSummaryPrivate {
};

#define _PRIVATE(o) (((CamelMboxSummary *)(o))->priv)

static int summary_header_load(CamelFolderSummary *, FILE *);
static int summary_header_save(CamelFolderSummary *, FILE *);

static CamelMessageInfo * message_info_new(CamelFolderSummary *, struct _header_raw *);
static CamelMessageInfo * message_info_new_from_parser(CamelFolderSummary *, CamelMimeParser *);
static CamelMessageInfo * message_info_load(CamelFolderSummary *, FILE *);
static int		  message_info_save(CamelFolderSummary *, FILE *, CamelMessageInfo *);
/*static void		  message_info_free(CamelFolderSummary *, CamelMessageInfo *);*/

static void camel_mbox_summary_class_init (CamelMboxSummaryClass *klass);
static void camel_mbox_summary_init       (CamelMboxSummary *obj);
static void camel_mbox_summary_finalise   (GtkObject *obj);

static CamelFolderSummaryClass *camel_mbox_summary_parent;

enum SIGNALS {
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

guint
camel_mbox_summary_get_type (void)
{
	static guint type = 0;
	
	if (!type) {
		GtkTypeInfo type_info = {
			"CamelMboxSummary",
			sizeof (CamelMboxSummary),
			sizeof (CamelMboxSummaryClass),
			(GtkClassInitFunc) camel_mbox_summary_class_init,
			(GtkObjectInitFunc) camel_mbox_summary_init,
			(GtkArgSetFunc) NULL,
			(GtkArgGetFunc) NULL
		};
		
		type = gtk_type_unique (camel_folder_summary_get_type (), &type_info);
	}
	
	return type;
}

static void
camel_mbox_summary_class_init (CamelMboxSummaryClass *klass)
{
	GtkObjectClass *object_class = (GtkObjectClass *) klass;
	CamelFolderSummaryClass *sklass = (CamelFolderSummaryClass *) klass;
	
	camel_mbox_summary_parent = gtk_type_class (camel_folder_summary_get_type ());

	object_class->finalize = camel_mbox_summary_finalise;

	sklass->summary_header_load = summary_header_load;
	sklass->summary_header_save = summary_header_save;

	sklass->message_info_new  = message_info_new;
	sklass->message_info_new_from_parser = message_info_new_from_parser;
	sklass->message_info_load = message_info_load;
	sklass->message_info_save = message_info_save;
	/*sklass->message_info_free = message_info_free;*/

	gtk_object_class_add_signals (object_class, signals, LAST_SIGNAL);
}

static void
camel_mbox_summary_init (CamelMboxSummary *obj)
{
	struct _CamelMboxSummaryPrivate *p;
	struct _CamelFolderSummary *s = (CamelFolderSummary *)obj;

	p = _PRIVATE(obj) = g_malloc0(sizeof(*p));

	/* subclasses need to set the right instance data sizes */
	s->message_info_size = sizeof(CamelMboxMessageInfo);
	s->content_info_size = sizeof(CamelMboxMessageContentInfo);

	/* and a unique file version */
	s->version = CAMEL_MBOX_SUMMARY_VERSION;
}

static void
camel_mbox_summary_finalise (GtkObject *obj)
{
	((GtkObjectClass *)(camel_mbox_summary_parent))->finalize((GtkObject *)obj);
}

/**
 * camel_mbox_summary_new:
 *
 * Create a new CamelMboxSummary object.
 * 
 * Return value: A new CamelMboxSummary widget.
 **/
CamelMboxSummary *
camel_mbox_summary_new (const char *filename, const char *mbox_name, ibex *index)
{
	CamelMboxSummary *new = CAMEL_MBOX_SUMMARY ( gtk_type_new (camel_mbox_summary_get_type ()));
	if (new) {
		/* ?? */
		camel_folder_summary_set_build_content((CamelFolderSummary *)new, TRUE);
		camel_folder_summary_set_filename((CamelFolderSummary *)new, filename);
		new->folder_name = g_strdup(mbox_name);
		new->index = index;
	}
	return new;
}


static int summary_header_load(CamelFolderSummary *s, FILE *in)
{
	CamelMboxSummary *mbs = (CamelMboxSummary *)s;

	return camel_folder_summary_decode_uint32(in, &mbs->folder_size);
}

static int summary_header_save(CamelFolderSummary *s, FILE *out)
{
	CamelMboxSummary *mbs = (CamelMboxSummary *)s;

	return camel_folder_summary_encode_uint32(out, mbs->folder_size);
}

static unsigned int
header_evolution_decode(const char *in, guint32 *uid, guint32 *flags)
{
        char *header;
        if (in
            && (header = header_token_decode(in))) {
                if (strlen(header) == strlen("00000000-0000")
                    && sscanf(header, "%08x-%04x", uid, flags) == 2) {
                        g_free(header);
                        return *uid;
                }
                g_free(header);
        }

        return ~0;
}

static CamelMessageInfo * message_info_new(CamelFolderSummary *s, struct _header_raw *h)
{
	CamelMessageInfo *mi;

	mi = ((CamelFolderSummaryClass *)camel_mbox_summary_parent)->message_info_new(s, h);
	if (mi) {
		const char *xev;
		guint32 uid, flags;
		CamelMboxMessageInfo *mbi = (CamelMboxMessageInfo *)mi;

		xev = header_raw_find(&h, "X-Evolution", NULL);
		if (xev
		    && header_evolution_decode(xev, &uid, &flags) != ~0) {
			g_free(mi->uid);
			mi->uid = g_strdup_printf("%u", uid);
			mi->flags = flags;
		} else {
			/* to indicate it has no xev header? */
			mi->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
			mi->uid = g_strdup_printf("%u", camel_folder_summary_next_uid(s));
		}
		mbi->frompos = -1;
	}
	return mi;
}

static CamelMessageInfo * message_info_new_from_parser(CamelFolderSummary *s, CamelMimeParser *mp)
{
	CamelMessageInfo *mi;
	CamelMboxSummary *mbs = (CamelMboxSummary *)s;

	mi = ((CamelFolderSummaryClass *)camel_mbox_summary_parent)->message_info_new_from_parser(s, mp);
	if (mi) {
		CamelMboxMessageInfo *mbi = (CamelMboxMessageInfo *)mi;

		mbi->frompos = camel_mime_parser_tell_start_from(mp);

		/* do we want to index this message as we add it, as well? */
		if (mbs->index_force
		    || (mi->flags & CAMEL_MESSAGE_FOLDER_FLAGGED) != 0
		    || !ibex_contains_name(mbs->index, mi->uid))
			camel_folder_summary_set_index(s, mbs->index);
		else
			camel_folder_summary_set_index(s, NULL);
	}
	return mi;
}

static CamelMessageInfo * message_info_load(CamelFolderSummary *s, FILE *in)
{
	CamelMessageInfo *mi;

	io(printf("loading mbox message info\n"));

	mi = ((CamelFolderSummaryClass *)camel_mbox_summary_parent)->message_info_load(s, in);
	if (mi) {
		CamelMboxMessageInfo *mbi = (CamelMboxMessageInfo *)mi;

		camel_folder_summary_decode_uint32(in, &mbi->frompos);
	}
	return mi;
}

static int		  message_info_save(CamelFolderSummary *s, FILE *out, CamelMessageInfo *mi)
{
	CamelMboxMessageInfo *mbi = (CamelMboxMessageInfo *)mi;

	io(printf("saving mbox message info\n"));

	((CamelFolderSummaryClass *)camel_mbox_summary_parent)->message_info_save(s, out, mi);

	return camel_folder_summary_encode_uint32(out, mbi->frompos);
}

static int
summary_rebuild(CamelMboxSummary *mbs, off_t offset)
{
	CamelMimeParser *mp;
	int fd;
	int ok = 0;

	printf("(re)Building summary from %d\n", (int)offset);

	fd = open(mbs->folder_name, O_RDONLY);
	mp = camel_mime_parser_new();
	camel_mime_parser_init_with_fd(mp, fd);
	camel_mime_parser_scan_from(mp, TRUE);
	camel_mime_parser_seek(mp, offset, SEEK_SET);

	if (offset > 0) {
		if (camel_mime_parser_step(mp, NULL, NULL) == HSCAN_FROM) {
			if (camel_mime_parser_tell_start_from(mp) != offset) {
				g_warning("The next message didn't start where I expected\nbuilding summary from start");
				camel_mime_parser_drop_step(mp);
				offset = 0;
				camel_mime_parser_seek(mp, offset, SEEK_SET);
				camel_folder_summary_clear((CamelFolderSummary *)mbs);
			} else {
				camel_mime_parser_unstep(mp);
			}
		} else {
			gtk_object_unref((GtkObject *)mp);
			/* end of file - no content? */
			return 0;
		}
	}

	while (camel_mime_parser_step(mp, NULL, NULL) == HSCAN_FROM) {
		CamelMessageInfo *info;

		info = camel_folder_summary_add_from_parser((CamelFolderSummary *)mbs, mp);
		if (info == NULL) {
			ok = -1;
			break;
		}

		g_assert(camel_mime_parser_step(mp, NULL, NULL) == HSCAN_FROM_END);
	}

	gtk_object_unref((GtkObject *)mp);

	return ok;
}

int
camel_mbox_summary_load(CamelMboxSummary *mbs)
{
	CamelFolderSummary *s = (CamelFolderSummary *)mbs;
	struct stat st;
	int ret = 0;

	if (camel_folder_summary_load(s) == -1) {
		printf("No summary\n");
		return summary_rebuild(mbs, 0);
	}

	/* is the summary out of date? */
	if (stat(mbs->folder_name, &st) == -1) {
		camel_folder_summary_clear(s);
		printf("Cannot summarise folder: '%s': %s\n", mbs->folder_name, strerror(errno));
		return -1;
	}

	g_assert(sizeof(size_t) == sizeof(int));

	printf("mbox size = %d, summary size = %d\n", st.st_size, mbs->folder_size);
	printf("mbox date = %d, summary date = %d\n", st.st_mtime, s->time);

	if (st.st_size != mbs->folder_size || st.st_mtime != s->time) {
		if (mbs->folder_size < st.st_size) {
			printf("Summary for smaller mbox\n");
			ret = summary_rebuild(mbs, mbs->folder_size);
		} else {
			camel_folder_summary_clear(s);
			printf("Summary for bigger mbox\n");
			ret = summary_rebuild(mbs, 0);
		}

		printf("return = %d\n", ret);

		if (ret != -1) {
			mbs->folder_size = st.st_size;
			s->time = st.st_mtime;
			camel_folder_summary_save(s);
		}
	}

	/* presumably the summary is ok - message extraction will
	   check this again anyway */
	return 0;
}
