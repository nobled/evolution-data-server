/*
 *  Copyright (C) 2000 Helix Code Inc.
 *
 *  Authors: Michael Zucchi <notzed@helixcode.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public License
 *  as published by the Free Software Foundation; either version 2 of
 *  the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _CAMEL_MBOX_SUMMARY_H
#define _CAMEL_MBOX_SUMMARY_H

#include <glib.h>
#include <camel/camel-folder.h>
#include <libibex/ibex.h>

typedef struct {
	CamelMessageContentInfo info;

	/* position in stream of this part */
	off_t pos;
	off_t bodypos;
	off_t endpos;
} CamelMboxMessageContentInfo;

typedef struct {
	CamelMessageInfo info;

	/* position of the xev header, if one exists */
	off_t xev_offset;
} CamelMboxMessageInfo;

typedef struct {
	int dirty;		/* if anything has changed */

	char *folder_path;
	char *summary_path;
	ibex *index;

	GPtrArray *messages;	/* array of messages matching mbox order */
	GHashTable *message_uid; /* index to messages by uid */

	int nextuid;

	time_t time;		/* time/size of folder's last update */
	size_t size;
} CamelMboxSummary;

CamelMboxSummary *camel_mbox_summary_new(const char *summary, const char *folder, ibex *index);
void camel_mbox_summary_unref(CamelMboxSummary *);

int camel_mbox_summary_load(CamelMboxSummary *);
int camel_mbox_summary_save(CamelMboxSummary *);
int camel_mbox_summary_check(CamelMboxSummary *);

CamelMboxMessageInfo *camel_mbox_summary_uid(CamelMboxSummary *s, const char *uid);
CamelMboxMessageInfo *camel_mbox_summary_index(CamelMboxSummary *, int index);
int camel_mbox_summary_message_count(CamelMboxSummary *);

#endif /* ! _CAMEL_MBOX_SUMMARY_H */
