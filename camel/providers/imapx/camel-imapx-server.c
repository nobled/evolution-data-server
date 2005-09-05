
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <string.h>
#include <glib.h>

// fixme, use own type funcs
#include <ctype.h>

#ifdef HAVE_NSS
#include <nspr.h>
#include <prio.h>
#include <prerror.h>
#include <prerr.h>
#endif

#include <camel/camel-object.h>
#include <libedataserver/e-msgport.h>
#include <camel/camel-url.h>
#include <camel/camel-session.h>
#include <camel/camel-stream-fs.h>
#include <camel/camel-stream-null.h>
#include <camel/camel-stream-mem.h>
#include <camel/camel-stream-filter.h>
#include <camel/camel-mime-filter-canon.h>
#include <camel/camel-mime-message.h>
#include <camel/camel-net-utils.h>
#include <camel/camel-tcp-stream-ssl.h>
#include <camel/camel-tcp-stream-raw.h>

#include <camel/camel-sasl.h>
#include <camel/camel-i18n.h>
#include <camel/camel-file-utils.h>

#include "camel-imapx-utils.h"
#include "camel-imapx-exception.h"
#include "camel-imapx-stream.h"
#include "camel-imapx-server.h"
#include "camel-imapx-folder.h"
#include "camel-imapx-store.h"
#include "camel-imapx-summary.h"

#define c(x) 
#define e(x) 

#define CFS_CLASS(x) ((CamelFolderSummaryClass *)((CamelObject *)x)->klass)

#define CIF(x) ((CamelIMAPXFolder *)x)

#define QUEUE_LOCK(x) (g_mutex_lock((x)->queue_lock))
#define QUEUE_UNLOCK(x) (g_mutex_unlock((x)->queue_lock))

/* All comms with server go here */


/* Try pipelining fetch requests, 'in bits' */
#define MULTI_FETCH
#define MULTI_SIZE (8196)

/* How many outstanding commands do we allow before we just queue them? */
#define MAX_COMMANDS (10)

struct _uidset_state {
	struct _CamelIMAPXEngine *ie;
	int entries, uids;
	int total, limit;
	guint32 start;
	guint32 last;
};

struct _CamelIMAPXCommand;
void imapx_uidset_init(struct _uidset_state *ss, int total, int limit);
int imapx_uidset_done(struct _uidset_state *ss, struct _CamelIMAPXCommand *ic);
int imapx_uidset_add(struct _uidset_state *ss, struct _CamelIMAPXCommand *ic, const char *uid);


typedef struct _CamelIMAPXCommandPart CamelIMAPXCommandPart;
typedef struct _CamelIMAPXCommand CamelIMAPXCommand;

typedef enum {
	CAMEL_IMAPX_COMMAND_SIMPLE = 0,
	CAMEL_IMAPX_COMMAND_DATAWRAPPER,
	CAMEL_IMAPX_COMMAND_STREAM,
	CAMEL_IMAPX_COMMAND_AUTH,
	CAMEL_IMAPX_COMMAND_FILE,
	CAMEL_IMAPX_COMMAND_STRING,
	CAMEL_IMAPX_COMMAND_MASK = 0xff,
	CAMEL_IMAPX_COMMAND_CONTINUATION = 0x8000 /* does this command expect continuation? */
} camel_imapx_command_part_t;

struct _CamelIMAPXCommandPart {
	struct _CamelIMAPXCommandPart *next;
	struct _CamelIMAPXCommandPart *prev;

	struct _CamelIMAPXCommand *parent;

	int data_size;
	char *data;

	camel_imapx_command_part_t type;

	int ob_size;
	void *ob;
};

typedef int (*CamelIMAPXEngineFunc)(struct _CamelIMAPXServer *engine, guint32 id, void *data);
typedef void (*CamelIMAPXCommandFunc)(struct _CamelIMAPXServer *engine, struct _CamelIMAPXCommand *);

struct _CamelIMAPXCommand {
	struct _CamelIMAPXCommand *next, *prev;

	char pri;

	const char *name;	/* command name/type (e.g. FETCH) */

	char *select;		/* folder to select */

	struct _status_info *status; /* status for command, indicates it is complete if != NULL */

	guint32 tag;

	struct _CamelStreamMem *mem;	/* for building the part TOOD: just use a GString? */
	EDList parts;
	CamelIMAPXCommandPart *current;

	CamelIMAPXCommandFunc complete;
	struct _CamelIMAPXJob *job;
};

CamelIMAPXCommand *camel_imapx_command_new(const char *name, const char *select, const char *fmt, ...);
void camel_imapx_command_add(CamelIMAPXCommand *ic, const char *fmt, ...);
void camel_imapx_command_free(CamelIMAPXCommand *ic);
void camel_imapx_command_close(CamelIMAPXCommand *ic);

/* states for the connection? */
enum {
	IMAPX_DISCONNECTED,
	IMAPX_CONNECTED,
	IMAPX_AUTHENTICATED,
	IMAPX_SELECTED
};

struct _refresh_info {
	char *uid;
	guint32 server_flags;
	CamelFlag *server_user_flags;
};

enum {
	IMAPX_JOB_GET_MESSAGE,
	IMAPX_JOB_APPEND_MESSAGE,
	IMAPX_JOB_REFRESH_INFO,
	IMAPX_JOB_SYNC_CHANGES,
	IMAPX_JOB_EXPUNGE,
	IMAPX_JOB_LIST,
};

struct _imapx_flag_change {
	GPtrArray *infos;
	char *name;
};

typedef struct _CamelIMAPXJob CamelIMAPXJob;
struct _CamelIMAPXJob {
	EMsg msg;

	CamelException *ex;

	void (*start)(CamelIMAPXServer *is, struct _CamelIMAPXJob *job);

	// ??
	//CamelOperation *op;

	int noreply:1;		/* dont wait for reply */
	char type;		/* operation type */
	char pri;		/* the command priority */
	short commands;		/* counts how many commands are outstanding */

	CamelFolder *folder;

	union {
		struct {
			/* in: uid requested */
			char *uid;
			/* in/out: message content stream output */
			CamelStream *stream;
			/* working variables */
			size_t body_offset;
			ssize_t body_len;
			size_t fetch_offset;
		} get_message;
		struct {
			/* array of refresh info's */
			GArray *infos;
			/* used for biulding uidset stuff */
			int index;
			int last_index;
			struct _uidset_state uidset;
			/* changes during refresh */
			CamelChangeInfo *changes;
		} refresh_info;
		struct {
			GPtrArray *infos;
			guint32 on_set;
			guint32 off_set;
			GArray *on_user; /* imapx_flag_change */
			GArray *off_user;
		} sync_changes;
		struct {
			char *path;
			CamelMessageInfo *info;
		} append_message;
		struct {
			char *pattern;
			guint32 flags;
			GHashTable *folders;
		} list;
	} u;
};

enum {
	USE_SSL_NEVER,
	USE_SSL_ALWAYS,
	USE_SSL_WHEN_POSSIBLE
};

#define SSL_PORT_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_SSL2 | CAMEL_TCP_STREAM_SSL_ENABLE_SSL3)
#define STARTTLS_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_TLS)



static void imapx_select(CamelIMAPXServer *is, CamelFolder *folder);



/*
  this creates a uid (or sequence number) set directly into a command,
  if total is set, then we break it up into total uids. (i.e. command time)
  if limit is set, then we break it up into limit entries (i.e. command length)
*/
void
imapx_uidset_init(struct _uidset_state *ss, int total, int limit)
{
	ss->uids = 0;
	ss->entries = 0;
	ss->start = 0;
	ss->last = 0;
	ss->total = total;
	ss->limit = limit;
}

int
imapx_uidset_done(struct _uidset_state *ss, CamelIMAPXCommand *ic)
{
	int ret = 0;

	if (ss->last != 0 && ss->last != ss->start) {
		camel_imapx_command_add(ic, ":%d", ss->last);
	}

	ret = ss->last != 0;

	ss->start = 0;
	ss->last = 0;
	ss->uids = 0;
	ss->entries = 0;

	return ret;
}

int
imapx_uidset_add(struct _uidset_state *ss, CamelIMAPXCommand *ic, const char *uid)
{
	guint32 uidn;

	uidn = strtoul(uid, NULL, 10);
	if (uidn == 0)
		return -1;

	ss->uids++;

	printf("uidset add '%s'\n", uid);

	if (ss->last == 0) {
		printf(" start\n");
		camel_imapx_command_add(ic, "%d", uidn);
		ss->entries++;
		ss->start = uidn;
	} else {
		if (ss->last != uidn-1) {
			if (ss->last == ss->start) {
				printf(" ,next\n");
				camel_imapx_command_add(ic, ",%d", uidn);
				ss->entries++;
			} else {
				printf(" :range\n");
				camel_imapx_command_add(ic, ":%d,%d", ss->last, uidn);
				ss->entries+=2;
			}
			ss->start = uidn;
		}
	}

	ss->last = uidn;

	if ((ss->limit && ss->entries >= ss->limit)
	    || (ss->total && ss->uids >= ss->total)) {
		printf(" done, %d entries, %d uids\n", ss->entries, ss->uids);
		imapx_uidset_done(ss, ic);
		return 1;
	}

	return 0;
}






static void
imapx_command_add_part(CamelIMAPXCommand *ic, camel_imapx_command_part_t type, void *o)
{
	CamelIMAPXCommandPart *cp;
	CamelStreamNull *null;
	unsigned int ob_size = 0;

	/* TODO: literal+? */
	
	switch(type & CAMEL_IMAPX_COMMAND_MASK) {
	case CAMEL_IMAPX_COMMAND_DATAWRAPPER:
	case CAMEL_IMAPX_COMMAND_STREAM: {
		CamelObject *ob = o;

		/* TODO: seekable streams we could just seek to the end and back */
		null = (CamelStreamNull *)camel_stream_null_new();
		if ( (type & CAMEL_IMAPX_COMMAND_MASK) == CAMEL_IMAPX_COMMAND_DATAWRAPPER) {
			camel_data_wrapper_write_to_stream((CamelDataWrapper *)ob, (CamelStream *)null);
		} else {
			camel_stream_reset((CamelStream *)ob);
			camel_stream_write_to_stream((CamelStream *)ob, (CamelStream *)null);
			camel_stream_reset((CamelStream *)ob);
		}
		type |= CAMEL_IMAPX_COMMAND_CONTINUATION;
		camel_object_ref(ob);
		ob_size = null->written;
		camel_object_unref((CamelObject *)null);
		camel_stream_printf((CamelStream *)ic->mem, "{%u}", ob_size);
		break;
	}
	case CAMEL_IMAPX_COMMAND_AUTH: {
		CamelObject *ob = o;

		/* we presume we'll need to get additional data only if we're not authenticated yet */
		camel_object_ref(ob);
		camel_stream_printf((CamelStream *)ic->mem, "%s", ((CamelSasl *)ob)->mech);
		if (!camel_sasl_authenticated((CamelSasl *)ob))
			type |= CAMEL_IMAPX_COMMAND_CONTINUATION;
		break;
	}
	case CAMEL_IMAPX_COMMAND_FILE: {
		char *path = o;
		struct stat st;

		if (stat(path, &st) == 0) {
			o = g_strdup(o);
			ob_size = st.st_size;
		} else
			o = NULL;

		camel_stream_printf((CamelStream *)ic->mem, "{%u}", ob_size);
		type |= CAMEL_IMAPX_COMMAND_CONTINUATION;
		break;
	}
	case CAMEL_IMAPX_COMMAND_STRING:
		o = g_strdup(o);
		ob_size = strlen(o);
		camel_stream_printf((CamelStream *)ic->mem, "{%u}", ob_size);
		type |= CAMEL_IMAPX_COMMAND_CONTINUATION;
		break;
	default:
		ob_size = 0;
	}

	cp = g_malloc0(sizeof(*cp));
	cp->type = type;
	cp->ob_size = ob_size;
	cp->ob = o;
	cp->data_size = ic->mem->buffer->len;
	cp->data = g_malloc(cp->data_size+1);
	memcpy(cp->data, ic->mem->buffer->data, cp->data_size);
	cp->data[cp->data_size] = 0;

	camel_stream_reset((CamelStream *)ic->mem);
	/* FIXME: hackish? */
	g_byte_array_set_size(ic->mem->buffer, 0);

	e_dlist_addtail(&ic->parts, (EDListNode *)cp);
}

static void
imapx_command_addv(CamelIMAPXCommand *ic, const char *fmt, va_list ap)
{
	const unsigned char *p, *ps, *start;
	unsigned char c;
	unsigned int width;
	char ch;
	int llong;
	int left;
	int fill;
	int zero;
	char *s;
	char *P;
	int d;
	long int l;
	guint32 f;
	CamelFlag *F;
	CamelStream *S;
	CamelDataWrapper *D;
	CamelSasl *A;
	char buffer[16];

	c(printf("adding command, fmt = '%s'\n", fmt));

	p = fmt;
	ps = fmt;
	while ( ( c = *p++ ) ) {
		switch(c) {
		case '%':
			if (*p == '%') {
				camel_stream_write((CamelStream *)ic->mem, ps, p-ps);
				p++;
				ps = p;
			} else {
				camel_stream_write((CamelStream *)ic->mem, ps, p-ps-1);
				start = p-1;
				width = 0;
				left = FALSE;
				fill = FALSE;
				zero = FALSE;
				llong = FALSE;

				do {
					c = *p++;
					if (c == '0')
						zero = TRUE;
					else if ( c== '-')
						left = TRUE;
					else
						break;
				} while (c);

				do {
					// FIXME: ascii isdigit
					if (isdigit(c))
						width = width * 10 + (c-'0');
					else
						break;
				} while ((c = *p++));

				if (c == 'l') {
					llong = TRUE;
					c = *p++;
				}

				switch(c) {
				case 'A': /* auth object - sasl auth, treat as special kind of continuation */
					A = va_arg(ap, CamelSasl *);
					imapx_command_add_part(ic, CAMEL_IMAPX_COMMAND_AUTH, A);
					break;
				case 'S': /* stream */
					S = va_arg(ap, CamelStream *);
					c(printf("got stream '%p'\n", S));
					imapx_command_add_part(ic, CAMEL_IMAPX_COMMAND_STREAM, S);
					break;
				case 'D': /* datawrapper */
					D = va_arg(ap, CamelDataWrapper *);
					c(printf("got data wrapper '%p'\n", D));
					imapx_command_add_part(ic, CAMEL_IMAPX_COMMAND_DATAWRAPPER, D);
					break;
				case 'P': /* filename path */
					P = va_arg(ap, char *);
					c(printf("got file path '%s'\n", P));
					imapx_command_add_part(ic, CAMEL_IMAPX_COMMAND_FILE, P);
					break;
				case 't': /* token */
					s = va_arg(ap, char *);
					camel_stream_write((CamelStream *)ic->mem, s, strlen(s));
					break;
				case 's': /* simple string */
					s = va_arg(ap, char *);
					c(printf("got string '%s'\n", s));
					if (*s) {
						unsigned char mask = imapx_is_mask(s);

						if (mask & IMAPX_TYPE_ATOM_CHAR)
							camel_stream_write((CamelStream *)ic->mem, s, strlen(s));
						else if (mask & IMAPX_TYPE_TEXT_CHAR) {
							camel_stream_write((CamelStream *)ic->mem, "\"", 1);
							while (*s) {
								char *start = s;

								while (*s && imapx_is_quoted_char(*s))
									s++;
								camel_stream_write((CamelStream *)ic->mem, start, s-start);
								if (*s) {
									camel_stream_write((CamelStream *)ic->mem, "\\", 1);
									camel_stream_write((CamelStream *)ic->mem, s, 1);
									s++;
								}
							}
							camel_stream_write((CamelStream *)ic->mem, "\"", 1);
						} else {
							imapx_command_add_part(ic, CAMEL_IMAPX_COMMAND_STRING, s);
						}
					} else {
						camel_stream_write((CamelStream *)ic->mem, "\"\"", 2);
					}
					break;
				case 'f': /* imap folder name */
					s = va_arg(ap, char *);
					c(printf("got folder '%s'\n", s));
					/* FIXME: encode folder name */
					/* FIXME: namespace? */
					camel_stream_printf((CamelStream *)ic->mem, "\"%s\"", s?s:"");
					break;
				case 'F': /* IMAP flags set */
					f = va_arg(ap, guint32);
					F = va_arg(ap, CamelFlag *);
					imap_write_flags((CamelStream *)ic->mem, f, F);
					break;
				case 'c':
					d = va_arg(ap, int);
					ch = d;
					camel_stream_write((CamelStream *)ic->mem, &ch, 1);
					break;
				case 'd': /* int/unsigned */
				case 'u':
					if (llong) {
						l = va_arg(ap, long int);
						c(printf("got long int '%d'\n", (int)l));
						memcpy(buffer, start, p-start);
						buffer[p-start] = 0;
						camel_stream_printf((CamelStream *)ic->mem, buffer, l);
					} else {
						d = va_arg(ap, int);
						c(printf("got int '%d'\n", d));
						memcpy(buffer, start, p-start);
						buffer[p-start] = 0;
						camel_stream_printf((CamelStream *)ic->mem, buffer, d);
					}
					break;
				}

				ps = p;
			}
			break;
		case '\\':	/* only for \\ really, we dont support \n\r etc at all */
			c = *p;
			if (c) {
				g_assert(c == '\\');
				camel_stream_write((CamelStream *)ic->mem, ps, p-ps);
				p++;
				ps = p;
			}
		}
	}

	camel_stream_write((CamelStream *)ic->mem, ps, p-ps-1);
}












CamelIMAPXCommand *
camel_imapx_command_new(const char *name, const char *select, const char *fmt, ...)
{
	CamelIMAPXCommand *ic;
	static int tag = 0;
	va_list ap;

	ic = g_malloc0(sizeof(*ic));
	ic->tag = tag++;
	ic->name = name;
	ic->mem = (CamelStreamMem *)camel_stream_mem_new();
	ic->select = g_strdup(select);
	e_dlist_init(&ic->parts);

	if (fmt && fmt[0]) {
		va_start(ap, fmt);
		imapx_command_addv(ic, fmt, ap);
		va_end(ap);
	}

	return ic;
}

void
camel_imapx_command_add(CamelIMAPXCommand *ic, const char *fmt, ...)
{
	va_list ap;

	g_assert(ic->mem);	/* gets reset on queue */

	if (fmt && fmt[0]) {
		va_start(ap, fmt);
		imapx_command_addv(ic, fmt, ap);
		va_end(ap);
	}
}

void
camel_imapx_command_free(CamelIMAPXCommand *ic)
{
	CamelIMAPXCommandPart *cp;

	if (ic == NULL)
		return;

	if (ic->mem)
		camel_object_unref((CamelObject *)ic->mem);
	imap_free_status(ic->status);
	g_free(ic->select);

	while ( (cp = ((CamelIMAPXCommandPart *)e_dlist_remhead(&ic->parts))) ) {
		g_free(cp->data);
		if (cp->ob) {
			switch (cp->type & CAMEL_IMAPX_COMMAND_MASK) {
			case CAMEL_IMAPX_COMMAND_FILE:
			case CAMEL_IMAPX_COMMAND_STRING:
				g_free(cp->ob);
				break;
			default:
				camel_object_unref(cp->ob);
			}
		}
		g_free(cp);
	}

	g_free(ic);
}

void
camel_imapx_command_close(CamelIMAPXCommand *ic)
{
	if (ic->mem) {
		c(printf("completing command buffer is [%d] '%.*s'\n", ic->mem->buffer->len, (int)ic->mem->buffer->len, ic->mem->buffer->data));
		if (ic->mem->buffer->len > 0)
			imapx_command_add_part(ic, CAMEL_IMAPX_COMMAND_SIMPLE, NULL);
	
		camel_object_unref((CamelObject *)ic->mem);
		ic->mem = NULL;
	}
}

/* FIXME: error handling */
#if 0
void
camel_imapx_engine_command_queue(CamelIMAPXEngine *imap, CamelIMAPXCommand *ic)
{
	CamelIMAPXCommandPart *cp;

	if (ic->mem) {
		c(printf("completing command buffer is [%d] '%.*s'\n", ic->mem->buffer->len, (int)ic->mem->buffer->len, ic->mem->buffer->data));
		if (ic->mem->buffer->len > 0)
			imapx_command_add_part(imap, ic, CAMEL_IMAPX_COMMAND_SIMPLE, NULL);
	
		camel_object_unref((CamelObject *)ic->mem);
		ic->mem = NULL;
	}

	/* now have completed command? */
}
#endif

/* Get a path into the cache, works like maildir, but isn't */
static char *
imapx_get_path_uid(CamelIMAPXServer *is, CamelFolder *folder, const char *bit, const char *uid)
{
	char *dir, *path;

	// big fixme of course, we need to create the path if it doesn't exist,
	// base it on the server, blah blah
	if (bit == NULL)
		bit = strchr(uid, '-') == NULL?"cur":"new";
	dir = g_strdup_printf("/tmp/imap-cache/%s/%s", folder->full_name, bit);

	camel_mkdir(dir, 0777);
	path = g_strdup_printf("%s/%s", dir, uid);
	g_free(dir);

	return path;
}

/* Must hold QUEUE_LOCK */
static void
imapx_command_start(CamelIMAPXServer *imap, CamelIMAPXCommand *ic)
{
	CamelIMAPXCommandPart *cp;

	camel_imapx_command_close(ic);

	/* FIXME: assert the selected folder == ic->selected */

	cp = (CamelIMAPXCommandPart *)ic->parts.head;
	g_assert(cp->next);

	ic->current = cp;

	/* TODO: If we support literal+ we should be able to write the whole command out
	   at this point .... >here< */

	if (cp->type & CAMEL_IMAPX_COMMAND_CONTINUATION)
		imap->literal = ic;

	e_dlist_addtail(&imap->active, (EDListNode *)ic);

	printf("Staring command (active=%d,%s) %c%05u %s\r\n", e_dlist_length(&imap->active), imap->literal?" literal":"", imap->tagprefix, ic->tag, cp->data);
	camel_stream_printf((CamelStream *)imap->stream, "%c%05u %s\r\n", imap->tagprefix, ic->tag, cp->data);
}

/* must have QUEUE lock */
static void
imapx_command_start_next(CamelIMAPXServer *imap)
{
	CamelIMAPXCommand *ic, *nc;
	int count = 0;
	int pri = -128;

	/* See if we can start another task yet.

	If we're waiting for a literal, we cannot proceed.

	If we're about to change the folder we're
	looking at from user-direction, we dont proceed.

	If we have a folder selected, first see if any
	jobs are waiting on it, but only if they are
	at least as high priority as anything we
	have running.

	If we dont, select the first folder required,
	then queue all the outstanding jobs on it, that
	are at least as high priority as the first.

	This is very shitty code!
	*/

	printf("** Starting next command\n");

	if (imap->literal != NULL || imap->select_pending != NULL) {
		printf("* no, waiting for literal/pending select '%s'\n", imap->select_pending->full_name);
		return;
	}

	ic = (CamelIMAPXCommand *)imap->queue.head;
	nc = ic->next;
	if (nc == NULL) {
		printf("* no, no jobs\n");
		return;
	}

	/* See if any queued jobs on this select first */
	if (imap->select) {
		printf("- we're selected on '%s', current jobs?\n", imap->select);
		for (ic = (CamelIMAPXCommand *)imap->active.head;ic->next;ic=ic->next) {
			printf("-  %3d '%s'\n", (int)ic->pri, ic->name);
			if (ic->pri > pri)
				pri = ic->pri;
			count++;
			if (count > MAX_COMMANDS) {
				printf("** too many jobs busy, waiting for results for now\n");
				return;
			}
		}

		printf("-- Checking job queue\n");
		count = 0;
		ic = (CamelIMAPXCommand *)imap->queue.head;
		nc = ic->next;
		while (nc && imap->literal == NULL && count < MAX_COMMANDS && ic->pri >= pri) {
			printf("-- %3d '%s'?\n", (int)ic->pri, ic->name);
			if (ic->select == NULL || strcmp(ic->select, imap->select) == 0) {
				printf("--> starting '%s'\n", ic->name);
				pri = ic->pri;
				e_dlist_remove((EDListNode *)ic);
				imapx_command_start(imap, ic);
				count++;
			}
			ic = nc;
			nc = nc->next;
		}

		if (count)
			return;

		ic = (CamelIMAPXCommand *)imap->queue.head;
	}

	/* If we need to select a folder for the first command, do it now, once
	   it is complete it will re-call us if it succeeded */
	if (ic->job->folder) {
		imapx_select(imap, ic->job->folder);
	} else {
		pri = ic->pri;
		nc = ic->next;
		count = 0;
		while (nc && imap->literal == NULL && count < MAX_COMMANDS && ic->pri >= pri) {
			if (ic->select == NULL || (imap->select && strcmp(ic->select, imap->select))) {
				printf("* queueing job %3d '%s'\n", (int)ic->pri, ic->name);
				pri = ic->pri;
				e_dlist_remove((EDListNode *)ic);
				imapx_command_start(imap, ic);
				count++;
			}
			ic = nc;
			nc = nc->next;
		}
	}
}

static void
imapx_command_queue(CamelIMAPXServer *imap, CamelIMAPXCommand *ic)
{
	CamelIMAPXCommand *scan;

	/* We enqueue in priority order, new messages have
	   higher priority than older messages with the same priority */

	camel_imapx_command_close(ic);

	printf("enqueue job '%.*s'\n", ((CamelIMAPXCommandPart *)ic->parts.head)->data_size, ((CamelIMAPXCommandPart *)ic->parts.head)->data);

	QUEUE_LOCK(imap);

	scan = (CamelIMAPXCommand *)imap->queue.head;
	if (scan->next == NULL)
		e_dlist_addtail(&imap->queue, (EDListNode *)ic);
	else {
		while (scan->next) {
			if (ic->pri >= scan->pri)
				break;
			scan = scan->next;
		}

		scan->prev->next = ic;
		ic->next = scan;
		ic->prev = scan->prev;
		scan->prev = ic;
	}

	imapx_command_start_next(imap);

	QUEUE_UNLOCK(imap);
}

/* Must have QUEUE lock */
static CamelIMAPXCommand *
imapx_find_command_tag(CamelIMAPXServer *imap, unsigned int tag)
{
	CamelIMAPXCommand *ic;

	ic = imap->literal;
	if (ic && ic->tag == tag)
		return ic;

	for (ic = (CamelIMAPXCommand *)imap->active.head;ic->next;ic=ic->next)
		if (ic->tag == tag)
			return ic;

	return NULL;
}

/* Must not have QUEUE lock */
static CamelIMAPXJob *
imapx_find_job(CamelIMAPXServer *imap, int type, const char *uid)
{
	CamelIMAPXJob *job;

	QUEUE_LOCK(imap);

	for (job = (CamelIMAPXJob *)imap->jobs.head;job->msg.ln.next;job = (CamelIMAPXJob *)job->msg.ln.next) {
		if (job->type != type)
			continue;

		switch (type) {
		case IMAPX_JOB_GET_MESSAGE:
			if (imap->select
			    && strcmp(job->folder->full_name, imap->select) == 0
			    && strcmp(job->u.get_message.uid, uid) == 0)
				goto found;
			break;
		case IMAPX_JOB_REFRESH_INFO:
			if (imap->select
			    && strcmp(job->folder->full_name, imap->select) == 0)
				goto found;
			break;
		case IMAPX_JOB_LIST:
			goto found;
		}
	}

	job = NULL;
found:

	QUEUE_UNLOCK(imap);

	return job;
}

/* Process all expunged results we had from the last command.
   This can be somewhat slow ... */
static void
imapx_expunged(CamelIMAPXServer *imap)
{
	int count = 1, index=0, expunge;
	const CamelMessageInfo *iterinfo;
	CamelIterator *iter;

	g_assert(imap->select_folder);

	if (imap->expunged->len == 0)
		return;

	printf("Processing '%d' expunges\n", imap->expunged->len);

	expunge = g_array_index(imap->expunged, guint32, index++);
	iter = camel_folder_summary_search(imap->select_folder->summary, NULL, NULL, NULL, NULL);
	while ((iterinfo = camel_iterator_next(iter, NULL))) {
		if (count == expunge) {
			printf("expunging '%d' - '%s'\n", expunge, camel_message_info_subject(iterinfo));
			camel_folder_summary_remove(imap->select_folder->summary, (CamelMessageInfo *)iterinfo);
			if (index >= imap->expunged->len)
				break;
			expunge = g_array_index(imap->expunged, guint32, index++);
		} else
			//FIXME: skip over offline uids
			count++;
	}
	camel_iterator_free(iter);
	g_array_set_size(imap->expunged, 0);
}

/* handle any untagged responses */
static int
imapx_untagged(CamelIMAPXServer *imap)
{
	unsigned int id, len;
	unsigned char *token, *p, c;
	int tok;
	struct _status_info *sinfo;
	
	e(printf("got untagged response\n"));
	id = 0;
	tok = camel_imapx_stream_token(imap->stream, &token, &len);
	if (tok == IMAP_TOK_INT) {
		id = strtoul(token, NULL, 10);
		tok = camel_imapx_stream_token(imap->stream, &token, &len);
	}

	if (tok == '\n')
		camel_exception_throw(1, "truncated server response");

	e(printf("Have token '%s' id %d\n", token, id));
	p = token;
	while ((c = *p))
		*p++ = toupper(c);

	switch (imap_tokenise(token, len)) {
	case IMAP_CAPABILITY:
		if (imap->cinfo)
			imap_free_capability(imap->cinfo);
		imap->cinfo = imap_parse_capability(imap->stream);
		printf("got capability flags %08x\n", imap->cinfo->capa);
		return 0;
	case IMAP_EXPUNGE: {
		guint32 expunge = id;

		printf("expunged: %d\n", id);
		g_array_append_val(imap->expunged, expunge);
		break;
	}
	case IMAP_EXISTS:
		printf("exists: %d\n", id);
		imap->exists = id;
		break;
	case IMAP_FLAGS: {
		guint32 flags;

		imap_parse_flags(imap->stream, &flags, NULL);

		printf("flags: %08x\n", flags);
		break;
	}
	case IMAP_FETCH: {
		struct _fetch_info *finfo;

		finfo = imap_parse_fetch(imap->stream);

		//imap_dump_fetch(finfo);

		if ((finfo->got & (FETCH_BODY|FETCH_UID)) == (FETCH_BODY|FETCH_UID)) {
			CamelIMAPXJob *job = imapx_find_job(imap, IMAPX_JOB_GET_MESSAGE, finfo->uid);

			/* This must've been a get-message request, fill out the body stream,
			   in the right spot */

			if (job) {
#ifdef MULTI_FETCH
				job->u.get_message.body_offset = finfo->offset;
				camel_seekable_stream_seek((CamelSeekableStream *)job->u.get_message.stream, finfo->offset, CAMEL_STREAM_SET);
#endif
				job->u.get_message.body_len = camel_stream_write_to_stream(finfo->body, job->u.get_message.stream);
				if (job->u.get_message.body_len == -1) {
					camel_exception_setv(job->ex, 1, "error writing to cache stream: %s\n", g_strerror(errno));
					camel_object_unref(job->u.get_message.stream);
					job->u.get_message.stream = NULL;
				}
			}
		}

		if ((finfo->got & (FETCH_FLAGS|FETCH_UID)) == (FETCH_FLAGS|FETCH_UID)) {
			CamelIMAPXJob *job = imapx_find_job(imap, IMAPX_JOB_REFRESH_INFO, NULL);

			/* This is either a refresh_info job, check to see if it is and update
			   if so, otherwise it must've been an unsolicited response, so update
			   the summary to match */

			if (job) {
				struct _refresh_info r;

				r.uid = finfo->uid;
				finfo->uid = NULL;
				r.server_flags = finfo->flags;
				r.server_user_flags = finfo->user_flags;
				finfo->user_flags = NULL;
				g_array_append_val(job->u.refresh_info.infos, r);
			} else {
				printf("Unsolicited flags response '%s' %08x\n", finfo->uid, finfo->flags);
				// TODO, we need the folder as well as the name in the select field.
			}
		}

		if ((finfo->got & (FETCH_HEADER|FETCH_UID)) == (FETCH_HEADER|FETCH_UID)) {
			CamelIMAPXJob *job = imapx_find_job(imap, IMAPX_JOB_REFRESH_INFO, NULL);

			/* This must be a refresh info job as well, but it has asked for
			   new messages to be added to the index */

			if (job) {
				CamelMimeParser *mp;
				CamelMessageInfo *mi;

				/* Do we want to save these headers for later too?  Do we care? */

				mp = camel_mime_parser_new();
				camel_mime_parser_init_with_stream(mp, finfo->header);
				mi = camel_message_info_new_from_parser(job->folder->summary, mp);
				camel_object_unref(mp);

				if (mi) {
					GArray *infos = job->u.refresh_info.infos;
					int i = job->u.refresh_info.last_index;

					/* This is rather inefficent, but should be ok if we're expecting it
					   since we break each fetch into lots of 100 */
					mi->uid = g_strdup(finfo->uid);
					for (i=0;i<infos->len;i++) {
						struct _refresh_info *r = &g_array_index(infos, struct _refresh_info, i);

						if (r->uid && !strcmp(r->uid, finfo->uid)) {
							((CamelMessageInfoBase *)mi)->flags = r->server_flags;
							((CamelIMAPXMessageInfo *)mi)->server_flags = r->server_flags;
							camel_flag_list_copy(&((CamelMessageInfoBase *)mi)->user_flags, &r->server_user_flags);
							((CamelIMAPXMessageInfo *)mi)->server_user_flags = r->server_user_flags;
							break;
						}
					}
					camel_folder_summary_add(job->folder->summary, mi);
				}
			}
		}

		imap_free_fetch(finfo);
		break;
	}
	case IMAP_LIST: case IMAP_LSUB: {
		struct _list_info *linfo = imap_parse_list(imap->stream);
		CamelIMAPXJob *job = imapx_find_job(imap, IMAPX_JOB_LIST, linfo->name);

		// TODO: we want to make sure the names match?

		printf("list: '%s' (%c)\n", linfo->name, linfo->separator);
		if (job && g_hash_table_lookup(job->u.list.folders, linfo->name) == NULL) {
			g_hash_table_insert(job->u.list.folders, linfo->name, linfo);
		} else {
			g_warning("got list response but no current listing job happening?\n");
			imap_free_list(linfo);
		}
		break;
	}
	case IMAP_RECENT:
		printf("recent: %d\n", id);
		imap->recent = id;
		break;
	case IMAP_BYE: case IMAP_OK: case IMAP_NO: case IMAP_BAD: case IMAP_PREAUTH:
		/* TODO: validate which ones of these can happen as unsolicited responses */
		/* TODO: handle bye/preauth differently */
		camel_imapx_stream_ungettoken(imap->stream, tok, token, len);
		sinfo = imap_parse_status(imap->stream);
		camel_object_trigger_event(imap, "status", sinfo);
		switch(sinfo->condition) {
		case IMAP_READ_WRITE:
			imap->mode = IMAPX_MODE_READ|IMAPX_MODE_WRITE;
			printf("folder is read-write\n");
			break;
		case IMAP_READ_ONLY:
			imap->mode = IMAPX_MODE_READ;
			printf("folder is read-only\n");
			break;
		case IMAP_UIDVALIDITY:
			imap->uidvalidity = sinfo->u.uidvalidity;
			break;
		case IMAP_UNSEEN:
			imap->unseen = sinfo->u.unseen;
			break;
		case IMAP_PERMANENTFLAGS:
			imap->permanentflags = sinfo->u.permanentflags;
			break;
		case IMAP_ALERT:
			printf("ALERT!: %s\n", sinfo->text);
			break;
		case IMAP_PARSE:
			printf("PARSE: %s\n", sinfo->text);
			break;
		default:
			break;
		}
		imap_free_status(sinfo);
		return 0;
	default:
		/* unknown response, just ignore it */
		printf("unknown token: %s\n", token);
	}

	return camel_imapx_stream_skip(imap->stream);
}

/* handle any continuation requests
   either data continuations, or auth continuation */
static int
imapx_continuation(CamelIMAPXServer *imap)
{
	CamelIMAPXCommand *ic, *newliteral = NULL;
	CamelIMAPXCommandPart *cp;
	
	printf("got continuation response\n");

	/* The 'literal' pointer is like a write-lock, nothing else
	   can write while we have it ... so we dont need any
	   ohter lock here.  All other writes go through
	   queue-lock */

	ic = imap->literal;
	if (ic == NULL) {
		camel_imapx_stream_skip(imap->stream);
		printf("got continuation response with no outstanding continuation requests?\n");
		return 1;
	}

	printf("got continuation response for data\n");
	cp = ic->current;
	switch(cp->type & CAMEL_IMAPX_COMMAND_MASK) {
	case CAMEL_IMAPX_COMMAND_DATAWRAPPER:
		printf("writing data wrapper to literal\n");
		camel_data_wrapper_write_to_stream((CamelDataWrapper *)cp->ob, (CamelStream *)imap->stream);
		break;
	case CAMEL_IMAPX_COMMAND_STREAM:
		printf("writing stream to literal\n");
		camel_stream_write_to_stream((CamelStream *)cp->ob, (CamelStream *)imap->stream);
		break;
	case CAMEL_IMAPX_COMMAND_AUTH: {
		CamelException *ex = camel_exception_new();
		char *resp;
		unsigned char *token;
		int tok, len;
		
		tok = camel_imapx_stream_token(imap->stream, &token, &len);
		resp = camel_sasl_challenge_base64((CamelSasl *)cp->ob, token, ex);
		if (camel_exception_is_set(ex))
			camel_exception_throw_ex(ex);
		camel_exception_free(ex);
		
		printf("got auth continuation, feeding token '%s' back to auth mech\n", resp);
		
		camel_stream_write((CamelStream *)imap->stream, resp, strlen(resp));
		
		/* we want to keep getting called until we get a status reponse from the server
		   ignore what sasl tells us */
		newliteral = ic;
		
		break; }
	case CAMEL_IMAPX_COMMAND_FILE: {
		CamelStream *file;

		printf("writing file '%s' to literal\n", (char *)cp->ob);

		// FIXME: errors
		if (cp->ob && (file = camel_stream_fs_new_with_name(cp->ob, O_RDONLY, 0))) {
			camel_stream_write_to_stream(file, (CamelStream *)imap->stream);
			camel_object_unref(file);
		} else if (cp->ob_size > 0) {
			// Server is expecting data ... ummm, send it zeros?  abort?
		}
		break; }
	case CAMEL_IMAPX_COMMAND_STRING:
		camel_stream_write((CamelStream *)imap->stream, cp->ob, cp->ob_size);
		break;
	default:
		/* should we just ignore? */
		imap->literal = NULL;
		camel_exception_throw(1, "continuation response for non-continuation request");
	}
	
	camel_imapx_stream_skip(imap->stream);
	
	cp = cp->next;
	if (cp->next) {
		ic->current = cp;
		printf("next part of command \"A%05u: %s\"\n", ic->tag, cp->data);
		camel_stream_printf((CamelStream *)imap->stream, "%s\r\n", cp->data);
		if (cp->type & CAMEL_IMAPX_COMMAND_CONTINUATION) {
			newliteral = ic;
		} else {
			g_assert(cp->next->next == NULL);
		}
	} else {
		printf("%p: queueing continuation\n", ic);
		camel_stream_printf((CamelStream *)imap->stream, "\r\n");
	}

	QUEUE_LOCK(imap);
	imap->literal = newliteral;

	imapx_command_start_next(imap);
	QUEUE_UNLOCK(imap);

	return 1;
}

/* handle a completion line */
static int
imapx_completion(CamelIMAPXServer *imap, unsigned char *token, int len)
{
	CamelIMAPXCommand * volatile ic;
	unsigned int tag;

	if (token[0] != imap->tagprefix)
		camel_exception_throw(1, "Server sent unexpected response: %s", token);

	tag = strtoul(token+1, NULL, 10);

	QUEUE_LOCK(imap);
	if ((ic = imapx_find_command_tag(imap, tag)) == NULL) {
		QUEUE_UNLOCK(imap);
		camel_exception_throw(1, "got response tag unexpectedly: %s", token);
	}

	printf("Got completion response for command %05u '%s'\n", ic->tag, ic->name);

	e_dlist_remove((EDListNode *)ic);
	e_dlist_addtail(&imap->done, (EDListNode *)ic);
	if (imap->literal == ic)
		imap->literal = NULL;

	if (ic->current->next->next) {
		QUEUE_UNLOCK(imap);
		camel_exception_throw(1, "command still has unsent parts?", ic->name);
	}

	/* A follow-on command might've already queued a new literal since were were done with ours? */
//	if (imap->literal != NULL) {
//		QUEUE_UNLOCK(imap);
//		camel_exception_throw(1, "command still has outstanding continuation", imap->literal->name);
//	}

	QUEUE_UNLOCK(imap);
	ic->status = imap_parse_status(imap->stream);

	if (ic->complete)
		ic->complete(imap, ic);

	if (imap->expunged->len)
		imapx_expunged(imap);

	QUEUE_LOCK(imap);
	if (ic->complete) {
		e_dlist_remove((EDListNode *)ic);
		camel_imapx_command_free(ic);
	}
	imapx_command_start_next(imap);
	QUEUE_UNLOCK(imap);

	return 1;
}

static void
imapx_step(CamelIMAPXServer *is)
/* throws IO,PARSE exception */
{
	unsigned int len;
	unsigned char *token;
	int tok;

	// poll ?  wait for other stuff? loop?
	tok = camel_imapx_stream_token(is->stream, &token, &len);
	if (tok == '*')
		imapx_untagged(is);
	else if (tok == IMAP_TOK_TOKEN)
		imapx_completion(is, token, len);
	else if (tok == '+')
		imapx_continuation(is);
	else
		camel_exception_throw(1, "unexpected server response: %s", token);
}

/* Used to run 1 command synchronously,
   use for capa, login, and selecting only. */
static void
imapx_command_run(CamelIMAPXServer *is, CamelIMAPXCommand *ic)
/* throws IO,PARSE exception */
{
	camel_imapx_command_close(ic);
	QUEUE_LOCK(is);
	g_assert(e_dlist_empty(&is->active));
	imapx_command_start(is, ic);
	QUEUE_UNLOCK(is);
	do {
		imapx_step(is);
	} while (ic->status == NULL);

	e_dlist_remove((EDListNode *)ic);
}

/* ********************************************************************** */
static void
imapx_select_done(CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{

	if (ic->status->result != IMAP_OK) {
		EDList failed = E_DLIST_INITIALISER(failed);
		CamelIMAPXCommand *cw, *cn;

		printf("Select failed\n");

		QUEUE_LOCK(is);
		cw = (CamelIMAPXCommand *)is->queue.head;
		cn = cw->next;
		while (cn) {
			if (cw->select && strcmp(cw->select, is->select_pending->full_name) == 0) {
				e_dlist_remove((EDListNode *)cw);
				e_dlist_addtail(&failed, (EDListNode *)cw);
			}
			cw = cn;
			cn = cn->next;
		}
		QUEUE_UNLOCK(is);

		cw = (CamelIMAPXCommand *)failed.head;
		cn = cw->next;
		while (cn) {
			cw->status = imap_copy_status(ic->status);
			cw->complete(is, cw);
			camel_imapx_command_free(cw);
			cw = cn;
			cn = cn->next;
		}

		camel_object_unref(is->select_pending);
	} else {
		printf("Select ok!\n");

		is->select_folder = is->select_pending;
		is->select = g_strdup(is->select_folder->full_name);
		is->state = IMAPX_SELECTED;
#if 0
		/* This must trigger a complete index rebuild! */
		if (is->uidvalidity && is->uidvalidity != ((CamelIMAPXSummary *)is->select_folder->summary)->uidvalidity)
			g_warning("uidvalidity doesn't match!");

		/* This should trigger a new messages scan */
		if (is->exists != is->select_folder->summary->root_view->total_count)
			g_warning("exists is %d our summary is %d and summary exists is %d\n", is->exists,
				  is->select_folder->summary->root_view->total_count,
				  ((CamelIMAPXSummary *)is->select_folder->summary)->exists);
#endif
	}

	is->select_pending = NULL;
}

static void
imapx_select(CamelIMAPXServer *is, CamelFolder *folder)
{
	CamelIMAPXCommand *ic;

	/* Select is complicated by the fact we may have commands
	   active on the server for a different selection.

	   So this waits for any commands to complete, selects the
	   new folder, and halts the queuing of any new commands.
	   It is assumed whomever called is us about to issue
	   a high-priority command anyway */

	/* TODO check locking here, pending_select will do
	   most of the work for normal commands, but not
	   for another select */

	if (is->select_pending)
		return;

	if (is->select && strcmp(is->select, folder->full_name) == 0)
		return;

	is->select_pending = folder;
	camel_object_ref(folder);
	if (is->select_folder) {
		while (!e_dlist_empty(&is->active)) {
			QUEUE_UNLOCK(is);
			imapx_step(is);
			QUEUE_LOCK(is);
		}
		g_free(is->select);
		camel_object_unref(is->select_folder);
		is->select = NULL;
		is->select_folder = NULL;
	}

	is->uidvalidity = 0;
	is->unseen = 0;
	is->permanentflags = 0;
	is->exists = 0;
	is->recent = 0;
	is->mode = 0;

	/* Hrm, what about reconnecting? */
	is->state = IMAPX_AUTHENTICATED;

	ic = camel_imapx_command_new("SELECT", NULL, "SELECT %s", CIF(folder)->raw_name);
	ic->complete = imapx_select_done;
	imapx_command_start(is, ic);
}

static void
imapx_connect(CamelIMAPXServer *is, int ssl_mode, int try_starttls)
/* throws IO exception */
{
	CamelStream * volatile tcp_stream = NULL;
	int ret;

	CAMEL_TRY {
#ifdef HAVE_SSL
		const char *mode;
#endif
		unsigned char *buffer;
		int len;
		char *serv;
		const char *port = NULL;
		struct addrinfo *ai, hints = { 0 };
		CamelException ex = { 0 };
		CamelIMAPXCommand *ic;

		if (is->url->port) {
			serv = g_alloca(16);
			sprintf(serv, "%d", is->url->port);
		} else {
			serv = "imap";
			port = "143";
		}
#ifdef HAVE_SSL
		mode = camel_url_get_param(is->url, "use_ssl");
		if (mode && strcmp(mode, "never") != 0) {
			if (!strcmp(mode, "when-possible")) {
				tcp_stream = camel_tcp_stream_ssl_new_raw(is->session, is->url->host, STARTTLS_FLAGS);
			} else {
				if (is->url->port == 0) {
					serv = "imaps";
					port = "993";
				}
				tcp_stream = camel_tcp_stream_ssl_new(is->session, is->url->host, SSL_PORT_FLAGS);
			}
		} else {
			tcp_stream = camel_tcp_stream_raw_new ();
		}
#else	
		tcp_stream = camel_tcp_stream_raw_new ();
#endif /* HAVE_SSL */

		hints.ai_socktype = SOCK_STREAM;
		ai = camel_getaddrinfo(is->url->host, serv, &hints, &ex);
		if (ex.id && ex.id != CAMEL_EXCEPTION_USER_CANCEL && port != NULL) {
			camel_exception_clear(&ex);
			ai = camel_getaddrinfo(is->url->host, port, &hints, &ex);
		}

		if (ex.id)
			camel_exception_throw_ex(&ex);
	
		ret = camel_tcp_stream_connect(CAMEL_TCP_STREAM(tcp_stream), ai);
		camel_freeaddrinfo(ai);
		if (ret == -1) {
			if (errno == EINTR)
				camel_exception_throw(CAMEL_EXCEPTION_USER_CANCEL, _("Connection cancelled"));
			else
				camel_exception_throw(CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
						      _("Could not connect to %s (port %s): %s"),
						      is->url->host, serv, g_strerror(errno));
		}

		is->stream = (CamelIMAPXStream *)camel_imapx_stream_new(tcp_stream);
		camel_object_unref(tcp_stream);
		tcp_stream = NULL;

		camel_imapx_stream_gets(is->stream, &buffer, &len);
		printf("Got greeting '%.*s'\n", len, buffer);

		ic = camel_imapx_command_new("CAPABILITY", NULL, "CAPABILITY");
		imapx_command_run(is, ic);
		camel_imapx_command_free(ic);
	} CAMEL_CATCH(e) {
		if (tcp_stream)
			camel_object_unref(tcp_stream);
		camel_exception_throw_ex(e);
	} CAMEL_DONE;
}

static void
imapx_reconnect(CamelIMAPXServer *is)
{
retry:
	CAMEL_TRY {
		CamelSasl *sasl;
		CamelIMAPXCommand *ic;

		imapx_connect(is, 0, 0);

		if (is->url->passwd == NULL) {
			CamelException ex = { 0 };
			char *prompt = g_strdup_printf(_("%sPlease enter the IMAP password for %s@%s"), "", is->url->user, is->url->host);

			is->url->passwd = camel_session_get_password(is->session, (CamelService *)is->store,
								     camel_url_get_param(is->url, "auth-domain"),
								     prompt, "password", 0, &ex);
			g_free(prompt);
			if (ex.id)
				camel_exception_throw_ex(&ex);
		}

		if (is->url->authmech
		    && (sasl = camel_sasl_new("imap", is->url->authmech, NULL))) {
			ic = camel_imapx_command_new("AUTHENTICATE", NULL, "AUTHENTICATE %A", sasl);
			camel_object_unref(sasl);
		} else {
			ic = camel_imapx_command_new("LOGIN", NULL, "LOGIN %s %s", is->url->user, is->url->passwd);
		}

		// TODO freeing data?
		imapx_command_run(is, ic);
		if (ic->status->result != IMAP_OK)
			camel_exception_throw(CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE, "Login failed: %s", ic->status->text);
		camel_imapx_command_free(ic);

		/* After login we re-capa */
		if (is->cinfo) {
			imap_free_capability(is->cinfo);
			is->cinfo = NULL;
		}
		ic = camel_imapx_command_new("CAPABILITY", NULL, "CAPABILITY");
		imapx_command_run(is, ic);
		camel_imapx_command_free(ic);
		is->state = IMAPX_AUTHENTICATED;
	} CAMEL_CATCH(e) {
		/* Shrug, either way this re-loops back ... */
		if (TRUE /*e->ex != CAMEL_EXCEPTION_USER_CANCEL*/) {
			printf("Re Connection failed: %s\n", e->desc);
			sleep(5);
			// camelexception_done?
			goto retry;
		}
	} CAMEL_DONE;
}

/* ********************************************************************** */

static void
imapx_job_done(CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job = ic->job;

	e_dlist_remove((EDListNode *)job);
	if (job->noreply) {
		camel_exception_clear(job->ex);
		g_free(job);
	} else
		e_msgport_reply((EMsg *)job);
}

/* ********************************************************************** */

static void
imapx_job_get_message_done(CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job = ic->job;

	/* We either have more to fetch (partial mode?), we are complete,
	   or we failed.  Failure is handled in the fetch code, so
	   we just return the job, or keep it alive with more requests */

	// FIXME FIXME Who and how do we free the command???

	job->commands--;

	if (ic->status->result != IMAP_OK) {
		job->u.get_message.body_len = -1;
		if (job->u.get_message.stream) {
			camel_object_unref(job->u.get_message.stream);
			job->u.get_message.stream = 0;
		}

		camel_exception_setv(job->ex, 1, "Error retriving message: %s", ic->status->text);
	}

#ifdef MULTI_FETCH
	if (job->u.get_message.body_len == MULTI_SIZE) {
		ic = camel_imapx_command_new("FETCH", job->folder->full_name,
					     "UID FETCH %t (BODY.PEEK[]", job->u.get_message.uid);
		camel_imapx_command_add(ic, "<%u.%u>", job->u.get_message.fetch_offset, MULTI_SIZE);
		camel_imapx_command_add(ic, ")");
		ic->complete = imapx_job_get_message_done;
		ic->job = job;
		ic->pri = job->pri;
		job->u.get_message.fetch_offset += MULTI_SIZE;
		job->commands++;
		imapx_command_queue(is, ic);
	}
#endif
	if (job->commands == 0)
		imapx_job_done(is, ic);
}

static void
imapx_job_get_message_start(CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;
#ifdef MULTI_FETCH
	int i;
#endif
	// FIXME: MUST ensure we never try to get the same message
	// twice at the same time.

	/* If this is a high-priority get, then we also
	   select the folder to make sure it runs immmediately ...

	   This doesn't work yet, so we always force a select every time
	*/
	//imapx_select(is, job->folder);
#ifdef MULTI_FETCH
	for (i=0;i<3;i++) {
		ic = camel_imapx_command_new("FETCH", job->folder->full_name,
						     "UID FETCH %t (BODY.PEEK[]", job->u.get_message.uid);
		camel_imapx_command_add(ic, "<%u.%u>", job->u.get_message.fetch_offset, MULTI_SIZE);
		camel_imapx_command_add(ic, ")");
		ic->complete = imapx_job_get_message_done;
		ic->job = job;
		ic->pri = job->pri;
		job->u.get_message.fetch_offset += MULTI_SIZE;
		job->commands++;
		imapx_command_queue(is, ic);
	}
#else
	ic = camel_imapx_command_new("FETCH", job->folder->full_name,
				     "UID FETCH %t (BODY.PEEK[])", job->u.get_message.uid);
	ic->complete = imapx_job_get_message_done;
	ic->job = job;
	ic->pri = job->pri;
	job->commands++;
	imapx_command_queue(is, ic);
#endif
}

/* ********************************************************************** */

static void
imapx_job_append_message_done(CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job = ic->job;
	CamelMessageInfo *mi;
	char *cur;

	/* Append done.  If we the server supports UIDPLUS we will get an APPENDUID response
	   with the new uid.  This lets us move the message we have directly to the cache
	   and also create a correctly numbered MessageInfo, without losing any information.
	   Otherwise we have to wait for the server to less us know it was appended. */

	if (ic->status->result == IMAP_OK) {
		if (ic->status->condition == IMAP_APPENDUID) {
			printf("Got appenduid %d %d\n", (int)ic->status->u.appenduid.uidvalidity, (int)ic->status->u.appenduid.uid);
			if (ic->status->u.appenduid.uidvalidity == is->uidvalidity) {
				mi = camel_message_info_clone(job->u.append_message.info);
				mi->uid = g_strdup_printf("%u", (unsigned int)ic->status->u.appenduid.uid);
				cur = imapx_get_path_uid(is, job->folder, NULL, mi->uid);
				printf("Moving cache item %s to %s\n", job->u.append_message.path, cur);
				link(job->u.append_message.path, cur);
				g_free(cur);
				camel_folder_summary_add(job->folder->summary, mi);
				camel_message_info_free(mi);
			} else {
				printf("but uidvalidity changed, uh ...\n");
			}
		}
		camel_folder_summary_remove(job->folder->summary, job->u.append_message.info);
		// should the folder-summary remove the file ?
		unlink(job->u.append_message.path);
	} else {
		camel_exception_setv(job->ex, 1, "Error appending message: %s", ic->status->text);
	}

	camel_message_info_free(job->u.append_message.info);
	g_free(job->u.append_message.path);
	camel_object_unref(job->folder);

	imapx_job_done(is, ic);
}

static void
imapx_job_append_message_start(CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;

	// FIXME: We dont need anything selected for this command to run ... 
	//imapx_select(is, job->folder);
	/* TODO: we could supply the original append date from the file timestamp */
	ic = camel_imapx_command_new("APPEND", NULL,
				     "APPEND %f %F %P",
				     job->folder->full_name,
				     ((CamelMessageInfoBase *)job->u.append_message.info)->flags,
				     ((CamelMessageInfoBase *)job->u.append_message.info)->user_flags,
				     job->u.append_message.path);
	ic->complete = imapx_job_append_message_done;
	ic->job = job;
	ic->pri = job->pri;
	job->commands++;
	imapx_command_queue(is, ic);
}

/* ********************************************************************** */

static int
imapx_refresh_info_uid_cmp(const void *ap, const void *bp)
{
	unsigned int av, bv;

	av = strtoul((const char *)ap, NULL, 10);
	bv = strtoul((const char *)bp, NULL, 10);

	if (av<bv)
		return -1;
	else if (av>bv)
		return 1;
	else
		return 0;
}

static int
imapx_refresh_info_cmp(const void *ap, const void *bp)
{
	const struct _refresh_info *a = ap;
	const struct _refresh_info *b = bp;

	return imapx_refresh_info_uid_cmp(a->uid, b->uid);
}

/* skips over non-server uids (pending appends) */
static const CamelMessageInfo *
imapx_iterator_next(CamelIterator *iter, CamelException *ex)
{
	const CamelMessageInfo *iterinfo;

	while ((iterinfo = camel_iterator_next(iter, NULL))
	       && strchr(camel_message_info_uid(iterinfo), '-') != NULL)
		printf("Ignoring offline uid '%s'\n", camel_message_info_uid(iterinfo));

	return iterinfo;
}

static void
imapx_job_refresh_info_step_done(CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job = ic->job;
	int i = job->u.refresh_info.index;
	GArray *infos = job->u.refresh_info.infos;

	if (camel_change_info_changed(job->u.refresh_info.changes))
		camel_object_trigger_event(job->folder, "folder_changed", job->u.refresh_info.changes);
	camel_change_info_clear(job->u.refresh_info.changes);

	if (i<infos->len) {
		ic = camel_imapx_command_new("FETCH", job->folder->full_name, "UID FETCH ");
		ic->complete = imapx_job_refresh_info_step_done;
		ic->job = job;
		job->u.refresh_info.last_index = i;

		for (;i<infos->len;i++) {
			int res;
			struct _refresh_info *r = &g_array_index(infos, struct _refresh_info, i);

			if (r->uid) {
				res = imapx_uidset_add(&job->u.refresh_info.uidset, ic, r->uid);
				if (res == 1) {
					camel_imapx_command_add(ic, " (RFC822.SIZE RFC822.HEADER)");
					job->u.refresh_info.index = i;
					imapx_command_queue(is, ic);
					return;
				}
			}
		}

		job->u.refresh_info.index = i;
		if (imapx_uidset_done(&job->u.refresh_info.uidset, ic)) {
			camel_imapx_command_add(ic, " (RFC822.SIZE RFC822.HEADER)");
			imapx_command_queue(is, ic);
			return;
		}
	}

	for (i=0;i<infos->len;i++) {
		struct _refresh_info *r = &g_array_index(infos, struct _refresh_info, i);

		g_free(r->uid);
	}
	g_array_free(job->u.refresh_info.infos, TRUE);
	e_dlist_remove((EDListNode *)job);
	e_msgport_reply((EMsg *)job);
}

static void
imapx_job_refresh_info_done(CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job = ic->job;
	int i;
	GArray *infos = job->u.refresh_info.infos;

	if (ic->status->result == IMAP_OK) {
		GCompareDataFunc uid_cmp = CFS_CLASS(job->folder->summary)->uid_cmp;
		CamelIterator *iter;
		const CamelMessageInfo *iterinfo;
		CamelIMAPXMessageInfo *info;
		CamelFolderSummary *s = job->folder->summary;
		int i, count=0;

		/* Here we do the typical sort/iterate/merge loop.
		   If the server flags dont match what we had, we modify our
		   flags to pick up what the server now has - but we merge
		   not overwrite */

		/* FIXME: We also have to check the offline directory for
		   anything missing in our summary, and also queue up jobs
		   for all outstanding messages to be uploaded */

		qsort(infos->data, infos->len, sizeof(struct _refresh_info), imapx_refresh_info_cmp);

		iter = camel_folder_summary_search(s, NULL, NULL, NULL, NULL);
		iterinfo = imapx_iterator_next(iter, NULL);

		for (i=0;i<infos->len;i++) {
			struct _refresh_info *r = &g_array_index(infos, struct _refresh_info, i);

			while (iterinfo && uid_cmp(camel_message_info_uid(iterinfo), r->uid, s) < 0) {
				printf("Message %s vanished\n", iterinfo->uid);
				camel_change_info_remove(job->u.refresh_info.changes, iterinfo);
				camel_folder_summary_remove(s, (CamelMessageInfo *)iterinfo);
				iterinfo = imapx_iterator_next(iter, NULL);
			}

			info = NULL;
			if (iterinfo && uid_cmp(iterinfo->uid, r->uid, s) == 0) {
				printf("already have '%s'\n", r->uid);
				info = (CamelIMAPXMessageInfo *)iterinfo;
				if (info->server_flags !=  r->server_flags
				    && camel_message_info_set_flags((CamelMessageInfo *)info, info->server_flags ^ r->server_flags, r->server_flags))
					camel_change_info_change(job->u.refresh_info.changes, iterinfo);
				iterinfo = imapx_iterator_next(iter, NULL);
				g_free(r->uid);
				r->uid = NULL;
			} else {
				count++;
			}
		}

		while (iterinfo) {
			printf("Message %s vanished\n", iterinfo->uid);
			camel_change_info_remove(job->u.refresh_info.changes, iterinfo);
			camel_folder_summary_remove(s, (CamelMessageInfo *)iterinfo);
			iterinfo = imapx_iterator_next(iter, NULL);
		}

		if (camel_change_info_changed(job->u.refresh_info.changes))
			camel_object_trigger_event(job->folder, "folder_changed", job->u.refresh_info.changes);
		camel_change_info_clear(job->u.refresh_info.changes);

		/* If we have any new messages, download their headers, but only a few (100?) at a time */
		if (count) {
			imapx_uidset_init(&job->u.refresh_info.uidset, 100, 0);
			imapx_job_refresh_info_step_done(is, ic);
			return;
		}
	} else {
		camel_exception_setv(job->ex, 1, "Error retriving message: %s", ic->status->text);
	}

	for (i=0;i<infos->len;i++) {
		struct _refresh_info *r = &g_array_index(infos, struct _refresh_info, i);

		g_free(r->uid);
	}
	g_array_free(job->u.refresh_info.infos, TRUE);
	e_dlist_remove((EDListNode *)job);
	e_msgport_reply((EMsg *)job);
}

static void
imapx_job_refresh_info_start(CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;

	// temp ... we shouldn't/dont want to force select?  or do we?
	//imapx_select(is, job->folder);
	ic = camel_imapx_command_new("FETCH", job->folder->full_name,
				     "FETCH 1:* (UID FLAGS)");
	ic->job = job;
	ic->complete = imapx_job_refresh_info_done;
	job->u.refresh_info.infos = g_array_new(0, 0, sizeof(struct _refresh_info));
	imapx_command_queue(is, ic);
}

/* ********************************************************************** */

static void
imapx_job_expunge_start(CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;

	//imapx_select(is, job->folder);
	ic = camel_imapx_command_new("EXPUNGE", job->folder->full_name, "EXPUNGE");
	ic->job = job;
	ic->complete = imapx_job_done;
	imapx_command_queue(is, ic);
}

/* ********************************************************************** */

static void
imapx_job_list_start(CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;

	ic = camel_imapx_command_new("LIST", NULL, "%s \"\" %s",
				     (job->u.list.flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED)?"LSUB":"LIST",
				     job->u.list.pattern);
	ic->pri = job->pri;
	ic->job = job;
	ic->complete = imapx_job_done;
	imapx_command_queue(is, ic);
}

/* ********************************************************************** */

/* FIXME: this is basically a copy of the same in camel-imapx-utils.c */
static struct {
	char *name;
	guint32 flag;
} flags_table[] = {
	{ "\\ANSWERED", CAMEL_MESSAGE_ANSWERED },
	{ "\\DELETED", CAMEL_MESSAGE_DELETED },
	{ "\\DRAFT", CAMEL_MESSAGE_DRAFT },
	{ "\\FLAGGED", CAMEL_MESSAGE_FLAGGED },
	{ "\\SEEN", CAMEL_MESSAGE_SEEN },
	/* { "\\RECENT", CAMEL_IMAPX_MESSAGE_RECENT }, */
};

/*
  flags 00101000
 sflags 01001000
 ^      01100000
~flags  11010111
&       01000000

&flags  00100000
*/

static void
imapx_job_sync_changes_done(CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job = ic->job;

	job->commands--;

	/* If this worked, we should really just update the changes that we sucessfully
	   stored, so we dont have to worry about sending them again ...
	   But then we'd have to track which uid's we actually updated, so its easier
	   just to refresh all of the ones we got.

	   Not that ... given all the asynchronicity going on, we're guaranteed
	   that what we just set is actually what is on the server now .. but
	   if it isn't, i guess we'll fix up next refresh */
	   
	if (ic->status->result != IMAP_OK && !camel_exception_is_set(job->ex))
		camel_exception_setv(job->ex, 1, "Error syncing changes: %s", ic->status->text);

	if (job->commands == 0) {
		if (!camel_exception_is_set(job->ex)) {
			int i;

			for (i=0;i<job->u.sync_changes.infos->len;i++) {
				CamelIMAPXMessageInfo *info = job->u.sync_changes.infos->pdata[i];

				info->server_flags = ((CamelMessageInfoBase *)info)->flags & CAMEL_IMAPX_SERVER_FLAGS;

				/* FIXME: move over user flags too */
			}
		}
		e_dlist_remove((EDListNode *)job);
		e_msgport_reply((EMsg *)job);
	}
}

static void
imapx_job_sync_changes_start(CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	guint32 i, j;
	struct _uidset_state ss;
	GPtrArray *infos = job->u.sync_changes.infos;
	int on;

	for (on=0;on<2;on++) {
		guint32 orset = on?job->u.sync_changes.on_set:job->u.sync_changes.off_set;
		GArray *user_set = on?job->u.sync_changes.on_user:job->u.sync_changes.off_user;

		for (j=0;j<sizeof(flags_table)/sizeof(flags_table[0]);j++) {
			guint32 flag = flags_table[j].flag;
			CamelIMAPXCommand *ic = NULL;

			if ((orset & flag) == 0)
				continue;

			printf("checking/storing %s flags '%s'\n", on?"on":"off", flags_table[j].name);
			imapx_uidset_init(&ss, 0, 100);
			for (i=0;i<infos->len;i++) {
				CamelIMAPXMessageInfo *info = infos->pdata[i];
				guint32 flags = ((CamelMessageInfoBase *)info)->flags & CAMEL_IMAPX_SERVER_FLAGS;
				guint32 sflags = info->server_flags & CAMEL_IMAPX_SERVER_FLAGS;
				int send = 0;

				if ( (on && (((flags ^ sflags) & flags) & flag))
				     || (!on && (((flags ^ sflags) & ~flags) & flag))) {
					if (ic == NULL) {
						ic = camel_imapx_command_new("STORE", job->folder->full_name, "UID STORE ");
						ic->complete = imapx_job_sync_changes_done;
						ic->job = job;
						ic->pri = job->pri;
					}
					send = imapx_uidset_add(&ss, ic, camel_message_info_uid(info));
				}
				if (send || (i == infos->len-1 && imapx_uidset_done(&ss, ic))) {
					job->commands++;
					camel_imapx_command_add(ic, " %tFLAGS.SILENT (%t)", on?"+":"-", flags_table[j].name);
					imapx_command_queue(is, ic);
					ic = NULL;
				}
			}
		}

		if (user_set) {
			CamelIMAPXCommand *ic = NULL;

			for (j=0;j<user_set->len;j++) {
				struct _imapx_flag_change *c = &g_array_index(user_set, struct _imapx_flag_change, i);

				for (i=0;i<c->infos->len;i++) {
					CamelIMAPXMessageInfo *info = c->infos->pdata[i];

					if (ic == NULL) {
						ic = camel_imapx_command_new("STORE", job->folder->full_name, "UID STORE ");
						ic->complete = imapx_job_sync_changes_done;
						ic->job = job;
						ic->pri = job->pri;
					}
					if (imapx_uidset_add(&ss, ic, camel_message_info_uid(info))
					    || (i==c->infos->len-1 && imapx_uidset_done(&ss, ic))) {
						job->commands++;
						camel_imapx_command_add(ic, " %tFLAGS.SILENT (%t)", on?"+":"-", c->name);
						imapx_command_queue(is, ic);
						ic = NULL;
					}
				}
			}
		}
	}

	/* Since this may start in another thread ... we need to
	   lock the commands count, ho hum */

	if (job->commands == 0) {
		printf("Hmm, we didn't have any work to do afterall?  hmm, this isn't right\n");

		e_dlist_remove((EDListNode *)job);
		e_msgport_reply((EMsg *)job);
	}
}

/* ********************************************************************** */

static void *
imapx_server_loop(void *d)
{
	CamelIMAPXServer *is = d;
	CamelIMAPXJob *job;

	/*
	  The main processing (reading) loop.

	  Incoming requests are added as jobs and tasks from other threads,
	  we just read the results from the server continously, and match
	  them up with the queued tasks as they come back.

	  Of course this loop can also initiate its own commands as well.

	  So, multiple threads can submit jobs, and write to the
	  stream (issue: locking stream for write?), but only this
	  thread can ever read from the stream.  This simplifies
	  locking, and greatly simplifies working out when new
	  work is ready.
	*/

	printf("imapx server loop started\n");

	// FIXME: handle exceptions
	while (1) {
		CAMEL_TRY {
			if (!is->stream)
				imapx_reconnect(is);

			job = (CamelIMAPXJob *)e_msgport_get(is->port);
			if (job) {
				e_dlist_addtail(&is->jobs, (EDListNode *)job);
				job->start(is, job);
			}

			if (!e_dlist_empty(&is->active)
			    || camel_imapx_stream_buffered(is->stream))
				imapx_step(is);
			else
				e_msgport_wait(is->port);
#if 0
			/* TODO:
			   This poll stuff wont work - we might block
			   waiting for results inside loops etc.

			   Requires a different approach:

			   New commands are queued in other threads as well
			   as this thread, and get pipelined over the socket.

			   Main area of locking required is command_queue
			   and command_start_next, the 'literal' command,
			   the jobs queue, the active queue, the queue
			   queue. */

			/* if ssl stream ... */
#ifdef HAVE_SSL
			{
				PRPollDesc pollfds[2] = { };
				int res;

				printf("\nGoing to sleep waiting for work to do\n\n");

				pollfds[0].fd = camel_tcp_stream_ssl_sockfd((CamelTcpStreamSSL *)is->stream->source);
				pollfds[0].in_flags = PR_POLL_READ;
				pollfds[1].fd = e_msgport_prfd(is->port);
				pollfds[1].in_flags = PR_POLL_READ;

				res = PR_Poll(pollfds, 2, PR_TicksPerSecond() / 10);
				if (res == -1)
					sleep(1) /* ?? */ ;
				else if ((pollfds[0].out_flags & PR_POLL_READ)) {
					printf(" * woken * have data ready\n");
					do {
						/* This is quite shitty, it will often block on each
						   part of the decode, causing significant
						   processing delays. */
						imapx_step(is);
					} while (camel_imapx_stream_buffered(is->stream));
				} else if (pollfds[1].out_flags & PR_POLL_READ) {
					printf(" * woken * have new job\n");
					/* job is handled in main loop */
				}
			}
#else
			{
				struct pollfd[2] = { 0 };
				int res;

				pollfd[0].fd = ((CamelTcpStreamRaw *)is->stream->source)->sockfd;
				pollfd[0].events = POLLIN;
				pollfd[1].fd = e_msgport_fd(is->port);
				pollfd[1].events = POLLIN;

				res = poll(pollfd, 2, 1000*30);
				if (res == -1)
					sleep(1) /* ?? */ ;
				else if (res == 0)
					/* timed out */;
				else if (pollfds[0].revents & POLLIN) {
					do {
						imapx_step(is);
					} while (camel_imapx_stream_buffered(is->stream));
				}
			}
#endif
#endif
		} CAMEL_CATCH(e) {
			printf("######### Got main loop exception: %s\n", e->desc);
			sleep(1);
		} CAMEL_DONE;
	}

	return NULL;
}

static void
imapx_server_class_init(CamelIMAPXServerClass *ieclass)
{
	ieclass->tagprefix = 'A';

//	camel_object_class_add_event((CamelObjectClass *)ieclass, "status", NULL);
}

static void
imapx_server_init(CamelIMAPXServer *ie, CamelIMAPXServerClass *ieclass)
{
	e_dlist_init(&ie->queue);
	e_dlist_init(&ie->active);
	e_dlist_init(&ie->done);
	e_dlist_init(&ie->jobs);

	ie->queue_lock = g_mutex_new();

	ie->tagprefix = ieclass->tagprefix;
	ieclass->tagprefix++;
	if (ieclass->tagprefix > 'Z')
		ieclass->tagprefix = 'A';
	ie->tagprefix = 'A';

	ie->state = IMAPX_DISCONNECTED;

	ie->port = e_msgport_new();

	ie->expunged = g_array_new(FALSE, FALSE, sizeof(guint32));
}

static void
imapx_server_finalise(CamelIMAPXServer *ie, CamelIMAPXServerClass *ieclass)
{
	g_mutex_free(ie->queue_lock);

	g_array_free(ie->expunged, TRUE);
}

CamelType
camel_imapx_server_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (
			camel_object_get_type (),
			"CamelIMAPXServer",
			sizeof (CamelIMAPXServer),
			sizeof (CamelIMAPXServerClass),
			(CamelObjectClassInitFunc) imapx_server_class_init,
			NULL,
			(CamelObjectInitFunc) imapx_server_init,
			(CamelObjectFinalizeFunc) imapx_server_finalise);
	}
	
	return type;
}

CamelIMAPXServer *
camel_imapx_server_new(CamelStore *store, CamelURL *url)
{
	CamelIMAPXServer *is = (CamelIMAPXServer *)camel_object_new(camel_imapx_server_get_type());

	is->session = ((CamelService *)store)->session;
	camel_object_ref(is->session);
	is->store = store;

	is->url = camel_url_copy(url);
	camel_url_set_user(is->url, "camel");
	camel_url_set_passwd(is->url, "camel");

	return is;
}

/* Client commands */

void
camel_imapx_server_connect(CamelIMAPXServer *is, int state)
{
	if (state) {
		pthread_t id;

		pthread_create(&id, NULL, imapx_server_loop, is);
	} else {
		/* tell processing thread to die, and wait till it does? */
	}
}

static void
imapx_run_job(CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	EMsgPort *reply;

	if (!job->noreply) {
		reply = e_msgport_new();
		job->msg.reply_port = reply;
	}

	/* Umm, so all these jobs 'select' first, which means reading(!)
	   we can't read from this thread ... hrm ... */
	if (FALSE /*is->state >= IMAPX_AUTHENTICATED*/) {
		/* NB: Must catch exceptions, cleanup/etc if we fail here? */
		QUEUE_LOCK(is);
		e_dlist_addhead(&is->jobs, (EDListNode *)job);
		QUEUE_UNLOCK(is);
		job->start(is, job);
	} else {
		e_msgport_put(is->port, (EMsg *)job);
	}

	if (!job->noreply) {
		e_msgport_wait(reply);
		g_assert(e_msgport_get(reply) == (EMsg *)job);
		e_msgport_destroy(reply);
	}
}

static CamelStream *
imapx_server_get_message(CamelIMAPXServer *is, CamelFolder *folder, const char *uid, int pri, CamelException *ex)
{
	CamelStream *stream;
	CamelIMAPXJob *job;
	char *tmp, *name;

	/* Get a message, we either get it from the local cache,
	   Or we ask for it, which will put it in the local cache,
	   then return that copy */

	/* FIXME: The storage logic should use camel-data-cache,
	   which handles concurrent adds properly.
	   EXCEPT!  It wont handle the 'new' dir directly ... do we care? */

	name = imapx_get_path_uid(is, folder, NULL, uid);
	stream = camel_stream_fs_new_with_name(name, O_RDONLY, 0);
	if (stream) {
		g_free(name);
		return stream;
	} else if (strchr(uid, '-')) {
		camel_exception_setv(ex, 2, "Offline message vanished from disk: %s", uid);
		g_free(name);
		camel_object_unref(stream);
		return NULL;
	}

	tmp = imapx_get_path_uid(is, folder, "tmp", uid);

	job = g_malloc0(sizeof(*job));
	job->pri = pri;
	job->type = IMAPX_JOB_GET_MESSAGE;
	job->start = imapx_job_get_message_start;
	job->folder = folder;
	job->u.get_message.uid = (char *)uid;
	job->u.get_message.stream = camel_stream_fs_new_with_name(tmp, O_WRONLY|O_CREAT|O_TRUNC, 0666);
	if (job->u.get_message.stream == NULL) {
		g_free(tmp);
		tmp = NULL;
		job->u.get_message.stream = camel_stream_mem_new();
	}
	job->ex = ex;

	imapx_run_job(is, job);

	stream = job->u.get_message.stream;
	g_free(job);

	if (stream) {
		if (tmp == NULL)
			camel_stream_reset(stream);
		else {
			if (camel_stream_flush(stream) == 0 && camel_stream_close(stream) == 0) {
				camel_object_unref(stream);
				stream = NULL;
				if (link(tmp, name) == 0)
					stream = camel_stream_fs_new_with_name(name, O_RDONLY, 0);
			} else {
				camel_exception_setv(ex, 1, "closing tmp stream failed: %s", g_strerror(errno));
				camel_object_unref(stream);
				stream = NULL;
			}
			unlink(tmp);
		}
	}

	g_free(tmp);
	g_free(name);

	return stream;
}

CamelStream *
camel_imapx_server_get_message(CamelIMAPXServer *is, CamelFolder *folder, const char *uid, CamelException *ex)
{
	return imapx_server_get_message(is, folder, uid, 100, ex);
}

void
camel_imapx_server_append_message(CamelIMAPXServer *is, CamelFolder *folder, CamelMimeMessage *message, const CamelMessageInfo *mi, CamelException *ex)
{
	char *uid = NULL, *tmp = NULL, *new = NULL;
	CamelStream *stream, *filter;
	CamelMimeFilter *canon;
	CamelIMAPXJob *job;
	CamelMessageInfo *info;
	int res;

	/* Append just assumes we have no/a dodgy connection.  We dump stuff into the 'new'
	   directory, and let the summary know it's there.  Then we fire off a no-reply
	   job which will asynchronously upload the message at some point in the future,
	   and fix up the summary to match */

	// FIXME: assign a real uid!  start with last known uid, add some maildir-like stuff?
	do {
		static int nextappend;

		g_free(uid);
		g_free(tmp);
		uid = g_strdup_printf("%s-%d", "1000", nextappend++);
		tmp = imapx_get_path_uid(is, folder, "tmp", uid);
		stream = camel_stream_fs_new_with_name(tmp, O_WRONLY|O_CREAT|O_EXCL, 0666);
	} while (stream == NULL && (errno == EINTR || errno == EEXIST));

	if (stream == NULL) {
		camel_exception_setv(ex, 2, "Cannot create spool file: %s", g_strerror(errno));
		goto fail;
	}

	filter = (CamelStream *)camel_stream_filter_new_with_stream(stream);
	camel_object_unref(stream);
	canon = camel_mime_filter_canon_new(CAMEL_MIME_FILTER_CANON_CRLF);
	camel_stream_filter_add((CamelStreamFilter *)filter, canon);
	res = camel_data_wrapper_write_to_stream((CamelDataWrapper *)message, filter);
	camel_object_unref(canon);
	camel_object_unref(filter);
	
	if (res == -1) {
		camel_exception_setv(ex, 2, "Cannot create spool file: %s", g_strerror(errno));
		goto fail;
	}

	new = imapx_get_path_uid(is, folder, "new", uid);
	if (link(tmp, new) == -1) {
		camel_exception_setv(ex, 2, "Cannot create spool file: %s", g_strerror(errno));
		goto fail;
	}

	info = camel_message_info_new_from_message(folder->summary, message, mi);
	info->uid = uid;
	uid = NULL;
	camel_folder_summary_add(folder->summary, info);

	// FIXME

	/* So, we actually just want to let the server loop that
	   messages need appending, i think.  This is so the same
	   mechanism is used for normal uploading as well as
	   offline re-syncing when we go back online */

	job = g_malloc0(sizeof(*job));
	job->pri = -60;
	job->type = IMAPX_JOB_APPEND_MESSAGE;
	job->noreply = 1;
	job->start = imapx_job_append_message_start;
	job->folder = folder;
	camel_object_ref(folder);
	job->u.append_message.info = info;
	job->u.append_message.path = new;
	new = NULL;

	imapx_run_job(is, job);
fail:
	if (tmp)
		unlink(tmp);
	g_free(uid);
	g_free(tmp);
	g_free(new);
}

#include "camel-imapx-store.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static CamelIterator *threaditer;
static CamelFolder *threadfolder;

static void *
getmsgs(void *d)
{
	CamelIMAPXServer *is = ((CamelIMAPXStore *)threadfolder->parent_store)->server;
	const CamelMessageInfo *info;
	CamelException ex = { 0 };

	/* FIXME: detach? */

	printf("Checking thread, downloading messages in the background ...\n");

	pthread_mutex_lock(&lock);
	while ((info = imapx_iterator_next(threaditer, NULL))) {
		char *cur = imapx_get_path_uid(is, threadfolder, NULL, camel_message_info_uid(info));
		char *uid = g_strdup(camel_message_info_uid(info));
		struct stat st;

		pthread_mutex_unlock(&lock);

		if (stat(cur, &st) == -1 && errno == ENOENT) {
			CamelStream *stream;

			printf(" getting uncached message '%s'\n", uid);
			stream = imapx_server_get_message(is, threadfolder, uid, -100, &ex);
			if (stream)
				camel_object_unref(stream);
			camel_exception_clear(&ex);
		} else
			printf(" already cached message '%s'\n", uid);

		g_free(uid);
		g_free(cur);
		pthread_mutex_lock(&lock);
	}
	pthread_mutex_unlock(&lock);

	return NULL;
}

void
camel_imapx_server_refresh_info(CamelIMAPXServer *is, CamelFolder *folder, CamelException *ex)
{
	CamelIMAPXJob *job;

	job = g_malloc0(sizeof(*job));
	job->type = IMAPX_JOB_REFRESH_INFO;
	job->start = imapx_job_refresh_info_start;
	job->folder = folder;
	job->ex = ex;
	job->u.refresh_info.changes = camel_change_info_new(NULL);

	imapx_run_job(is, job);

	if (camel_change_info_changed(job->u.refresh_info.changes))
		camel_object_trigger_event(folder, "folder_changed", job->u.refresh_info.changes);
	camel_change_info_free(job->u.refresh_info.changes);

	g_free(job);

	{
		int i;
		int c = 3;
		pthread_t ids[10];

		threadfolder = folder;
		threaditer = camel_folder_search(folder, NULL, NULL, NULL, NULL);
		for (i=0;i<c;i++)
			pthread_create(&ids[i], NULL, getmsgs, NULL);

		for (i=0;i<c;i++)
			pthread_join(ids[i], NULL);
		camel_iterator_free(threaditer);
	}
}

static void
imapx_sync_free_user(GArray *user_set)
{
	int i;

	if (user_set == NULL)
		return;

	for (i=0;i<user_set->len;i++)
		g_ptr_array_free(g_array_index(user_set, struct _imapx_flag_change, i).infos, TRUE);
	g_array_free(user_set, TRUE);
}

void
camel_imapx_server_sync_changes(CamelIMAPXServer *is, CamelFolder *folder, GPtrArray *infos, CamelException *ex)
{
	guint i, on_orset, off_orset;
	GArray *on_user, *off_user;
	CamelIMAPXMessageInfo *info;
	CamelIMAPXJob *job;

	/* We calculate two masks, a mask of all flags which have been
	   turned off and a mask of all flags which have been turned
	   on. If either of these aren't 0, then we have work to do,
	   and we fire off a job to do it.

	   User flags are a bit more tricky, we rely on the user
	   flags being sorted, and then we create a bunch of lists;
	   one for each flag being turned off, including each
	   info being turned off, and one for each flag being turned on.
	*/

	off_orset = on_orset = 0;
	for (i=0;i<infos->len;i++) {
		guint32 flags, sflags;
		CamelFlag *uflags, *suflags;

		info = infos->pdata[i];
		flags = ((CamelMessageInfoBase *)info)->flags & CAMEL_IMAPX_SERVER_FLAGS;
		sflags = info->server_flags & CAMEL_IMAPX_SERVER_FLAGS;
		if (flags != sflags) {
			off_orset |= ( flags ^ sflags ) & ~flags;
			on_orset |= (flags ^ sflags) & flags;
		}

		uflags = ((CamelMessageInfoBase *)info)->user_flags;
		suflags = info->server_user_flags;
		while (uflags || suflags) {
			int res;

			if (uflags) {
				if (suflags)
					res = strcmp(uflags->name, suflags->name);
				else
					res = -1;
			} else {
				res = 1;
			}

			if (res == 0) {
				uflags = uflags->next;
				suflags = suflags->next;
			} else {
				GArray *user_set;
				CamelFlag *user_flag;
				struct _imapx_flag_change *change, add;

				if (res < 0) {
					if (on_user == NULL)
						on_user = g_array_new(sizeof(*change), 0, 0);
					user_set = on_user;
					user_flag = uflags;
					uflags = uflags->next;
				} else {
					if (off_user == NULL)
						off_user = g_array_new(sizeof(*change), 0, 0);
					user_set = off_user;
					user_flag = suflags;
					suflags = suflags->next;
				}

				/* Could sort this and binary search */
				for (i=0;i<user_set->len;i++) {
					change = &g_array_index(user_set, struct _imapx_flag_change, i);
					if (strcmp(change->name, user_flag->name) == 0)
						goto found;
				}
				add.name = g_strdup(user_flag->name);
				add.infos = g_ptr_array_new();
				g_array_append_val(user_set, add);
				change = &add;
			found:
				g_ptr_array_add(change->infos, info);
			}
		}
	}

	if ((on_orset|off_orset) == 0 && on_user == NULL && off_user == NULL)
		return;

	job = g_malloc0(sizeof(*job));
	job->type = IMAPX_JOB_SYNC_CHANGES;
	job->start = imapx_job_sync_changes_start;
	job->pri = -50;
	job->folder = folder;
	job->ex = ex;
	job->u.sync_changes.infos = infos;
	job->u.sync_changes.on_set = on_orset;
	job->u.sync_changes.off_set = off_orset;
	job->u.sync_changes.on_user = on_user;
	job->u.sync_changes.off_user = off_user;

	imapx_run_job(is, job);

	g_free(job);

	imapx_sync_free_user(on_user);
	imapx_sync_free_user(off_user);
}

/* expunge-uids? */
void
camel_imapx_server_expunge(CamelIMAPXServer *is, CamelFolder *folder, CamelException *ex)
{
	CamelIMAPXJob *job;

	/* Do we really care to wait for this one to finish? */

	job = g_malloc0(sizeof(*job));
	job->type = IMAPX_JOB_EXPUNGE;
	job->start = imapx_job_expunge_start;
	job->pri = -120;
	job->folder = folder;
	job->ex = ex;

	imapx_run_job(is, job);

	g_free(job);
}

static guint
imapx_name_hash(gconstpointer key)
{
	if (g_ascii_strcasecmp(key, "INBOX") == 0)
		return g_str_hash("INBOX");
	else
		return g_str_hash(key);
}

static gint
imapx_name_equal(gconstpointer a, gconstpointer b)
{
	gconstpointer aname = a, bname = b;

	if (g_ascii_strcasecmp(a, "INBOX") == 0)
		aname = "INBOX";
	if (g_ascii_strcasecmp(b, "INBOX") == 0)
		bname = "INBOX";
	return g_str_equal(aname, bname);
}

static void
imapx_list_flatten(void *k, void *v, void *d)
{
	GPtrArray *folders = d;

	g_ptr_array_add(folders, v);
}

static int
imapx_list_cmp(const void *ap, const void *bp)
{
	struct _list_info *a = ((struct _list_info **)ap)[0];
	struct _list_info *b = ((struct _list_info **)bp)[0];

	return strcmp(a->name, b->name);
}

GPtrArray *
camel_imapx_server_list(CamelIMAPXServer *is, const char *top, guint32 flags, CamelException *ex)
{
	CamelIMAPXJob *job;
	GPtrArray *folders;

	job = g_malloc0(sizeof(*job));
	job->type = IMAPX_JOB_LIST;
	job->start = imapx_job_list_start;
	job->pri = -80;
	job->ex = ex;
	job->u.list.flags = flags;
	job->u.list.folders = g_hash_table_new(imapx_name_hash, imapx_name_equal);
	job->u.list.pattern = g_alloca(strlen(top)+5);
	if (flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE)
		sprintf(job->u.list.pattern, "%s*", top);
	else
		sprintf(job->u.list.pattern, "%s", top);

	imapx_run_job(is, job);

	folders = g_ptr_array_new();
	g_hash_table_foreach(job->u.list.folders, imapx_list_flatten, folders);
	g_hash_table_destroy(job->u.list.folders);

	g_free(job);

	qsort(folders->pdata, folders->len, sizeof(folders->pdata[0]), imapx_list_cmp);

	return folders;
}
