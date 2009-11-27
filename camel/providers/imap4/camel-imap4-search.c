/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  Camel
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
 *           Michael Zucchi <notzed@novell.com>
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
 *  Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <ctype.h>

#include "camel-imap4-command.h"
#include "camel-imap4-engine.h"
#include "camel-imap4-search.h"
#include "camel-imap4-stream.h"
#include "camel-imap4-utils.h"

static void camel_imap4_search_class_init (CamelIMAP4SearchClass *class);
static void camel_imap4_search_init (CamelIMAP4Search *search, CamelIMAP4SearchClass *class);
static void imap4_search_finalize (CamelObject *object);

static ESExpResult *imap4_body_contains (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search);

static gpointer parent_class;

GType
camel_imap4_search_get_type (void)
{
	static GType type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID))
		type = camel_type_register (
			CAMEL_TYPE_FOLDER_SEARCH,
			"CamelIMAP4Search",
			sizeof (CamelIMAP4Search),
			sizeof (CamelIMAP4SearchClass),
			(GClassInitFunc) camel_imap4_search_class_init,
			NULL,
			(GInstanceInitFunc) camel_imap4_search_init,
			(GObjectFinalizeFunc) imap4_search_finalize);

	return type;
}

static void
camel_imap4_search_class_init (CamelIMAP4SearchClass *class)
{
	CamelFolderSearchClass *search_class = (CamelFolderSearchClass *) class;

	parent_class = g_type_class_peek_parent (class);

	search_class->body_contains = imap4_body_contains;
}

static void
camel_imap4_search_init (CamelIMAP4Search *search, CamelIMAP4SearchClass *class)
{
	search->engine = NULL;
}

static void
imap4_search_finalize (CamelObject *object)
{
	;
}

CamelFolderSearch *
camel_imap4_search_new (CamelIMAP4Engine *engine, const gchar *cachedir)
{
	CamelIMAP4Search *search;

	search = g_object_new (CAMEL_TYPE_IMAP4_SEARCH, NULL);
	camel_folder_search_construct ((CamelFolderSearch *) search);
	search->engine = engine;

	return (CamelFolderSearch *) search;
}

static gint
untagged_search (CamelIMAP4Engine *engine, CamelIMAP4Command *ic, guint32 index, camel_imap4_token_t *token, GError **error)
{
	CamelFolderSummary *summary = ((CamelFolder *) engine->folder)->summary;
	GPtrArray *matches = ic->user_data;
	CamelMessageInfo *info;
	gchar uid[12];

	while (1) {
		if (camel_imap4_engine_next_token (engine, token, ex) == -1)
			return -1;

		if (token->token == '\n')
			break;

		if (token->token != CAMEL_IMAP4_TOKEN_NUMBER || token->v.number == 0)
			goto unexpected;

		sprintf (uid, "%u", token->v.number);
		if ((info = camel_folder_summary_uid (summary, uid))) {
			g_ptr_array_add (matches, (gchar *) camel_message_info_uid (info));
			camel_message_info_free (info);
		}
	}

	return 0;

 unexpected:

	camel_imap4_utils_set_unexpected_token_error (ex, engine, token);

	return -1;
}

static ESExpResult *
imap4_body_contains (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search)
{
	CamelIMAP4Search *imap4_search = (CamelIMAP4Search *) search;
	CamelIMAP4Engine *engine = imap4_search->engine;
	GPtrArray *strings, *matches, *infos;
	register const guchar *inptr;
	gboolean utf8_search = FALSE;
	GPtrArray *summary_set;
	CamelMessageInfo *info;
	CamelIMAP4Command *ic;
	const gchar *expr;
	ESExpResult *r;
	gint id, i, n;
	gsize used;
	gchar *set;

	if (((CamelOfflineStore *) engine->service)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return parent_class->body_contains (f, argc, argv, search);

	summary_set = search->summary_set ? search->summary_set : search->summary;

	/* check the simple cases */
	if (argc == 0 || summary_set->len == 0) {
		/* match nothing */
		if (search->current) {
			r = e_sexp_result_new (f, ESEXP_RES_BOOL);
			r->value.bool = FALSE;
		} else {
			r = e_sexp_result_new (f, ESEXP_RES_ARRAY_PTR);
			r->value.ptrarray = g_ptr_array_new ();
		}

		return r;
	} else if (argc == 1 && argv[0]->type == ESEXP_RES_STRING && argv[0]->value.string[0] == '\0') {
		/* match everything */
		if (search->current) {
			r = e_sexp_result_new (f, ESEXP_RES_BOOL);
			r->value.bool = TRUE;
		} else {
			r = e_sexp_result_new (f, ESEXP_RES_ARRAY_PTR);
			r->value.ptrarray = g_ptr_array_new ();
			g_ptr_array_set_size (r->value.ptrarray, summary_set->len);
			r->value.ptrarray->len = summary_set->len;
			for (i = 0; i < summary_set->len; i++) {
				info = g_ptr_array_index (summary_set, i);
				r->value.ptrarray->pdata[i] = (gchar *) camel_message_info_uid (info);
			}
		}

		return r;
	}

	strings = g_ptr_array_new ();
	for (i = 0; i < argc; i++) {
		if (argv[i]->type == ESEXP_RES_STRING && argv[i]->value.string[0] != '\0') {
			g_ptr_array_add (strings, argv[i]->value.string);
			if (!utf8_search) {
				inptr = (guchar *) argv[i]->value.string;
				while (*inptr != '\0') {
					if (!isascii ((gint) *inptr)) {
						utf8_search = TRUE;
						break;
					}

					inptr++;
				}
			}
		}
	}

	if (strings->len == 0) {
		/* match everything */
		g_ptr_array_free (strings, TRUE);

		if (search->current) {
			r = e_sexp_result_new (f, ESEXP_RES_BOOL);
			r->value.bool = TRUE;
		} else {
			r = e_sexp_result_new (f, ESEXP_RES_ARRAY_PTR);
			r->value.ptrarray = g_ptr_array_new ();
			g_ptr_array_set_size (r->value.ptrarray, summary_set->len);
			r->value.ptrarray->len = summary_set->len;
			for (i = 0; i < summary_set->len; i++) {
				info = g_ptr_array_index (summary_set, i);
				r->value.ptrarray->pdata[i] = (gchar *) camel_message_info_uid (info);
			}
		}

		return r;
	}

	g_ptr_array_add (strings, NULL);
	matches = g_ptr_array_new ();
	infos = g_ptr_array_new ();

	if (search->current) {
		g_ptr_array_add (infos, search->current);
	} else {
		g_ptr_array_set_size (infos, summary_set->len);
		infos->len = summary_set->len;
		for (i = 0; i < summary_set->len; i++)
			infos->pdata[i] = summary_set->pdata[i];
	}

 retry:
	if (utf8_search && (engine->capa & CAMEL_IMAP4_CAPABILITY_utf8_search))
		expr = "UID SEARCH CHARSET UTF-8 UID %s BODY %V\r\n";
	else
		expr = "UID SEARCH UID %s BODY %V\r\n";

	used = strlen (expr) + (5 * (strings->len - 2));

	for (i = 0; i < infos->len; i += n) {
		n = camel_imap4_get_uid_set (engine, search->folder->summary, infos, i, used, &set);

		ic = camel_imap4_engine_queue (engine, search->folder, expr, set, strings->pdata);
		camel_imap4_command_register_untagged (ic, "SEARCH", untagged_search);
		ic->user_data = matches;
		g_free (set);

		while ((id = camel_imap4_engine_iterate (engine)) < ic->id && id != -1)
			;

		if (id == -1 || ic->status != CAMEL_IMAP4_COMMAND_COMPLETE) {
			camel_imap4_command_unref (ic);
			goto done;
		}

		if (ic->result == CAMEL_IMAP4_RESULT_NO && utf8_search && (engine->capa & CAMEL_IMAP4_CAPABILITY_utf8_search)) {
			gint j;

			/* might be because the server is lame and doesn't support UTF-8 */
			for (j = 0; j < ic->resp_codes->len; j++) {
				CamelIMAP4RespCode *resp = ic->resp_codes->pdata[j];

				if (resp->code == CAMEL_IMAP4_RESP_CODE_BADCHARSET) {
					engine->capa &= ~CAMEL_IMAP4_CAPABILITY_utf8_search;
					camel_imap4_command_unref (ic);
					goto retry;
				}
			}
		}

		if (ic->result != CAMEL_IMAP4_RESULT_OK) {
			camel_imap4_command_unref (ic);
			break;
		}

		camel_imap4_command_unref (ic);
	}

 done:

	g_ptr_array_free (strings, TRUE);
	g_ptr_array_free (infos, TRUE);

	if (search->current) {
		const gchar *uid;

		uid = camel_message_info_uid (search->current);
		r = e_sexp_result_new (f, ESEXP_RES_BOOL);
		r->value.bool = FALSE;
		for (i = 0; i < matches->len; i++) {
			if (!strcmp (matches->pdata[i], uid)) {
				r->value.bool = TRUE;
				break;
			}
		}

		g_ptr_array_free (matches, TRUE);
	} else {
		r = e_sexp_result_new (f, ESEXP_RES_ARRAY_PTR);
		r->value.ptrarray = matches;
	}

	return r;
}
