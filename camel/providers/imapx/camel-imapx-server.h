/*
 *  Copyright (C) 2005 Novell Inc.
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

#ifndef _CAMEL_IMAPX_SERVER_H
#define _CAMEL_IMAPX_SERVER_H

#include <libedataserver/e-msgport.h>

struct _CamelFolder;
struct _CamelException;
struct _CamelMimeMessage;
struct _CamelMessageInfo;

#define CAMEL_IMAPX_SERVER(obj)         CAMEL_CHECK_CAST (obj, camel_imapx_server_get_type (), CamelIMAPPServer)
#define CAMEL_IMAPX_SERVER_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_imapx_server_get_type (), CamelIMAPPServerClass)
#define CAMEL_IS_IMAPX_SERVER(obj)      CAMEL_CHECK_TYPE (obj, camel_imapx_server_get_type ())

typedef struct _CamelIMAPXServer CamelIMAPXServer;
typedef struct _CamelIMAPXServerClass CamelIMAPXServerClass;

#define IMAPX_MODE_READ (1<<0)
#define IMAPX_MODE_WRITE (1<<1)

struct _CamelIMAPXServer {
	CamelObject cobject;

	struct _CamelStore *store;
	struct _CamelSession *session;

	/* Info about the current connection */
	struct _CamelURL *url;
	struct _CamelIMAPXStream *stream;
	struct _capability_info *cinfo;

	/* incoming jobs */
	EMsgPort *port;
	EDList jobs;

	char tagprefix;
	int state:4;

	/* Current command/work queue.  All commands are stored in one list,
	   all the time, so they can be cleaned up in exception cases */
	void *queue_lock;
	struct _CamelIMAPXCommand *literal;
	EDList queue;
	EDList active;
	EDList done;

	/* info on currently selected folder */
	struct _CamelFolder *select_folder;
	char *select;
	struct _CamelFolderChangeInfo *changes;
	struct _CamelFolder *select_pending;
	guint32 permanentflags;
	guint32 uidvalidity;
	guint32 unseen;
	guint32 exists;
	guint32 recent;
	guint32 mode;

	/* any expunges that happened from the last command, they are
	   processed after the command completes. */
	GArray *expunged;
};

struct _CamelIMAPXServerClass {
	CamelObjectClass cclass;

	char tagprefix;
};

CamelType               camel_imapx_server_get_type     (void);
CamelIMAPXServer *camel_imapx_server_new(struct _CamelStore *store, struct _CamelURL *url);

void camel_imapx_server_connect(CamelIMAPXServer *is, int state);

GPtrArray *camel_imapx_server_list(CamelIMAPXServer *is, const char *top, guint32 flags, CamelException *ex);

void camel_imapx_server_refresh_info(CamelIMAPXServer *is, CamelFolder *folder, struct _CamelException *ex);
void camel_imapx_server_sync_changes(CamelIMAPXServer *is, CamelFolder *folder, GPtrArray *infos, CamelException *ex);
void camel_imapx_server_expunge(CamelIMAPXServer *is, CamelFolder *folder, CamelException *ex);

CamelStream *camel_imapx_server_get_message(CamelIMAPXServer *is, CamelFolder *folder, const char *uid, struct _CamelException *ex);
void camel_imapx_server_append_message(CamelIMAPXServer *is, CamelFolder *folder, struct _CamelMimeMessage *message, const struct _CamelMessageInfo *mi, CamelException *ex);

#endif /* ! _CAMEL_IMAPX_SERVER_H */
