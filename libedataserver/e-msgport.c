/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include <pthread.h>

#include <glib.h>

#ifdef HAVE_NSS
#include <nspr.h>
#endif

#ifdef G_OS_WIN32
#include <winsock2.h>
#endif

#include "e-msgport.h"
#include "e-data-server-util.h"

#define m(x)			/* msgport debug */
#define t(x)			/* thread debug */
#define c(x)			/* cache debug */

#ifdef G_OS_WIN32
#define E_CLOSE(socket) closesocket (socket)
#define E_READ(socket,buf,nbytes) recv(socket,buf,nbytes,0)
#define E_WRITE(socket,buf,nbytes) send(socket,buf,nbytes,0)
#define E_IS_SOCKET_ERROR(status) ((status) == SOCKET_ERROR)
#define E_IS_INVALID_SOCKET(socket) ((socket) == INVALID_SOCKET)
#define E_IS_STATUS_INTR() 0 /* No WSAEINTR errors in WinSock2  */
#else
#define E_CLOSE(socket) close (socket)
#define E_READ(socket,buf,nbytes) read(socket,buf,nbytes)
#define E_WRITE(socket,buf,nbytes) write(socket,buf,nbytes)
#define E_IS_SOCKET_ERROR(status) ((status) == -1)
#define E_IS_INVALID_SOCKET(socket) ((socket) < 0)
#define E_IS_STATUS_INTR() (errno == EINTR)
#endif

static gint
e_pipe (gint *fds)
{
#ifndef G_OS_WIN32
	if (pipe (fds) != -1)
		return 0;

	fds[0] = -1;
	fds[1] = -1;

	return -1;
#else
	SOCKET temp, socket1 = -1, socket2 = -1;
	struct sockaddr_in saddr;
	gint len;
	u_long arg;
	fd_set read_set, write_set;
	struct timeval tv;

	temp = socket (AF_INET, SOCK_STREAM, 0);

	if (temp == INVALID_SOCKET) {
		goto out0;
	}

	arg = 1;
	if (ioctlsocket (temp, FIONBIO, &arg) == SOCKET_ERROR) {
		goto out0;
	}

	memset (&saddr, 0, sizeof (saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = 0;
	saddr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

	if (bind (temp, (struct sockaddr *)&saddr, sizeof (saddr))) {
		goto out0;
	}

	if (listen (temp, 1) == SOCKET_ERROR) {
		goto out0;
	}

	len = sizeof (saddr);
	if (getsockname (temp, (struct sockaddr *)&saddr, &len)) {
		goto out0;
	}

	socket1 = socket (AF_INET, SOCK_STREAM, 0);

	if (socket1 == INVALID_SOCKET) {
		goto out0;
	}

	arg = 1;
	if (ioctlsocket (socket1, FIONBIO, &arg) == SOCKET_ERROR) {
		goto out1;
	}

	if (connect (socket1, (struct sockaddr  *)&saddr, len) != SOCKET_ERROR ||
	    WSAGetLastError () != WSAEWOULDBLOCK) {
		goto out1;
	}

	FD_ZERO (&read_set);
	FD_SET (temp, &read_set);

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if (select (0, &read_set, NULL, NULL, NULL) == SOCKET_ERROR) {
		goto out1;
	}

	if (!FD_ISSET (temp, &read_set)) {
		goto out1;
	}

	socket2 = accept (temp, (struct sockaddr *) &saddr, &len);
	if (socket2 == INVALID_SOCKET) {
		goto out1;
	}

	FD_ZERO (&write_set);
	FD_SET (socket1, &write_set);

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if (select (0, NULL, &write_set, NULL, NULL) == SOCKET_ERROR) {
		goto out2;
	}

	if (!FD_ISSET (socket1, &write_set)) {
		goto out2;
	}

	arg = 0;
	if (ioctlsocket (socket1, FIONBIO, &arg) == SOCKET_ERROR) {
		goto out2;
	}

	arg = 0;
	if (ioctlsocket (socket2, FIONBIO, &arg) == SOCKET_ERROR) {
		goto out2;
	}

	fds[0] = socket1;
	fds[1] = socket2;

	closesocket (temp);

	return 0;

out2:
	closesocket (socket2);
out1:
	closesocket (socket1);
out0:
	closesocket (temp);
	errno = EMFILE;		/* FIXME: use the real syscall errno? */

	fds[0] = -1;
	fds[1] = -1;

	return -1;

#endif
}

void e_dlist_init(EDList *v)
{
        v->head = (EDListNode *)&v->tail;
        v->tail = NULL;
        v->tailpred = (EDListNode *)&v->head;
}

EDListNode *e_dlist_addhead(EDList *l, EDListNode *n)
{
        n->next = l->head;
        n->prev = (EDListNode *)&l->head;
        l->head->prev = n;
        l->head = n;
        return n;
}

EDListNode *e_dlist_addtail(EDList *l, EDListNode *n)
{
        n->next = (EDListNode *)&l->tail;
        n->prev = l->tailpred;
        l->tailpred->next = n;
        l->tailpred = n;
        return n;
}

EDListNode *e_dlist_remove(EDListNode *n)
{
        n->next->prev = n->prev;
        n->prev->next = n->next;
        return n;
}

EDListNode *e_dlist_remhead(EDList *l)
{
	EDListNode *n, *nn;

	n = l->head;
	nn = n->next;
	if (nn) {
		nn->prev = n->prev;
		l->head = nn;
		return n;
	}
	return NULL;
}

EDListNode *e_dlist_remtail(EDList *l)
{
	EDListNode *n, *np;

	n = l->tailpred;
	np = n->prev;
	if (np) {
		np->next = n->next;
		l->tailpred = np;
		return n;
	}
	return NULL;
}

gint e_dlist_empty(EDList *l)
{
	return (l->head == (EDListNode *)&l->tail);
}

gint e_dlist_length(EDList *l)
{
	EDListNode *n, *nn;
	gint count = 0;

	n = l->head;
	nn = n->next;
	while (nn) {
		count++;
		n = nn;
		nn = n->next;
	}

	return count;
}

struct _EMCache {
	GMutex *lock;
	GHashTable *key_table;
	EDList lru_list;
	gsize node_size;
	gint node_count;
	time_t timeout;
	GFreeFunc node_free;
};

/**
 * em_cache_new:
 * @timeout:
 * @nodesize:
 * @nodefree:
 *
 * Setup a new timeout cache.  @nodesize is the size of nodes in the
 * cache, and @nodefree will be called to free YOUR content.
 *
 * Return value:
 **/
EMCache *
em_cache_new(time_t timeout, gsize nodesize, GFreeFunc nodefree)
{
	struct _EMCache *emc;

	emc = g_malloc0(sizeof(*emc));
	emc->node_size = nodesize;
	emc->key_table = g_hash_table_new(g_str_hash, g_str_equal);
	emc->node_free = nodefree;
	e_dlist_init(&emc->lru_list);
	emc->lock = g_mutex_new();
	emc->timeout = timeout;

	return emc;
}

/**
 * em_cache_destroy:
 * @emc:
 *
 * destroy the cache, duh.
 **/
void
em_cache_destroy(EMCache *emc)
{
	em_cache_clear(emc);
	g_mutex_free(emc->lock);
	g_free(emc);
}

/**
 * em_cache_lookup:
 * @emc:
 * @key:
 *
 * Lookup a cache node.  once you're finished with it, you need to
 * unref it.
 *
 * Return value:
 **/
EMCacheNode *
em_cache_lookup(EMCache *emc, const gchar *key)
{
	EMCacheNode *n;

	g_mutex_lock(emc->lock);
	n = g_hash_table_lookup(emc->key_table, key);
	if (n) {
		e_dlist_remove((EDListNode *)n);
		e_dlist_addhead(&emc->lru_list, (EDListNode *)n);
		n->stamp = time(NULL);
		n->ref_count++;
	}
	g_mutex_unlock(emc->lock);

	c(printf("looking up '%s' %s\n", key, n?"found":"not found"));

	return n;
}

/**
 * em_cache_node_new:
 * @emc:
 * @key:
 *
 * Create a new key'd cache node.  The node will not be added to the
 * cache until you insert it.
 *
 * Return value:
 **/
EMCacheNode *
em_cache_node_new(EMCache *emc, const gchar *key)
{
	EMCacheNode *n;

	/* this could use memchunks, but its probably overkill */
	n = g_malloc0(emc->node_size);
	n->key = g_strdup(key);

	return n;
}

/**
 * em_cache_node_unref:
 * @emc:
 * @n:
 *
 * unref a cache node, you can only unref nodes which have been looked
 * up.
 **/
void
em_cache_node_unref(EMCache *emc, EMCacheNode *n)
{
	g_mutex_lock(emc->lock);
	g_assert(n->ref_count > 0);
	n->ref_count--;
	g_mutex_unlock(emc->lock);
}

/**
 * em_cache_add:
 * @emc:
 * @n:
 *
 * Add a cache node to the cache, once added the memory is owned by
 * the cache.  If there are conflicts and the old node is still in
 * use, then the new node is not added, otherwise it is added and any
 * nodes older than the expire time are flushed.
 **/
void
em_cache_add(EMCache *emc, EMCacheNode *n)
{
	EMCacheNode *old, *prev;
	EDList old_nodes;

	e_dlist_init(&old_nodes);

	g_mutex_lock(emc->lock);
	old = g_hash_table_lookup(emc->key_table, n->key);
	if (old != NULL) {
		if (old->ref_count == 0) {
			g_hash_table_remove(emc->key_table, old->key);
			e_dlist_remove((EDListNode *)old);
			e_dlist_addtail(&old_nodes, (EDListNode *)old);
			goto insert;
		} else {
			e_dlist_addtail(&old_nodes, (EDListNode *)n);
		}
	} else {
		time_t now;
	insert:
		now = time(NULL);
		g_hash_table_insert(emc->key_table, n->key, n);
		e_dlist_addhead(&emc->lru_list, (EDListNode *)n);
		n->stamp = now;
		emc->node_count++;

		c(printf("inserting node %s\n", n->key));

		old = (EMCacheNode *)emc->lru_list.tailpred;
		prev = old->prev;
		while (prev && old->stamp < now - emc->timeout) {
			if (old->ref_count == 0) {
				c(printf("expiring node %s\n", old->key));
				g_hash_table_remove(emc->key_table, old->key);
				e_dlist_remove((EDListNode *)old);
				e_dlist_addtail(&old_nodes, (EDListNode *)old);
			}
			old = prev;
			prev = prev->prev;
		}
	}

	g_mutex_unlock(emc->lock);

	while ((old = (EMCacheNode *)e_dlist_remhead(&old_nodes))) {
		emc->node_free(old);
		g_free(old->key);
		g_free(old);
	}
}

/**
 * em_cache_clear:
 * @emc:
 *
 * clear the cache.  just for api completeness.
 **/
void
em_cache_clear(EMCache *emc)
{
	EMCacheNode *node;
	EDList old_nodes;

	e_dlist_init(&old_nodes);
	g_mutex_lock(emc->lock);
	while ((node = (EMCacheNode *)e_dlist_remhead(&emc->lru_list)))
		e_dlist_addtail(&old_nodes, (EDListNode *)node);
	g_mutex_unlock(emc->lock);

	while ((node = (EMCacheNode *)e_dlist_remhead(&old_nodes))) {
		emc->node_free(node);
		g_free(node->key);
		g_free(node);
	}
}

struct _EMsgPort {
	GAsyncQueue *queue;
	gint pipe[2];  /* on Win32, actually a pair of SOCKETs */
#ifdef HAVE_NSS
	PRFileDesc *prpipe[2];
#endif
};

/* message flags */
enum {
	MSG_FLAG_SYNC_WITH_PIPE    = 1 << 0,
	MSG_FLAG_SYNC_WITH_PR_PIPE = 1 << 1
};

#ifdef HAVE_NSS
static gint
e_prpipe (PRFileDesc **fds)
{
#ifdef G_OS_WIN32
	if (PR_NewTCPSocketPair (fds) != PR_FAILURE)
		return 0;
#else
	if (PR_CreatePipe (&fds[0], &fds[1]) != PR_FAILURE)
		return 0;
#endif

	fds[0] = NULL;
	fds[1] = NULL;

	return -1;
}
#endif

static void
msgport_sync_with_pipe (gint fd)
{
	gchar buffer[1];

	while (fd >= 0) {
		if (E_READ (fd, buffer, 1) > 0)
			break;
		else if (!E_IS_STATUS_INTR ()) {
			g_warning ("%s: Failed to read from pipe: %s",
				G_STRFUNC, g_strerror (errno));
			break;
		}
	}
}

#ifdef HAVE_NSS
static void
msgport_sync_with_prpipe (PRFileDesc *prfd)
{
	gchar buffer[1];

	while (prfd != NULL) {
		if (PR_Read (prfd, buffer, 1) > 0)
			break;
		else if (PR_GetError () != PR_PENDING_INTERRUPT_ERROR) {
			gchar *text = g_alloca (PR_GetErrorTextLength ());
			PR_GetErrorText (text);
			g_warning ("%s: Failed to read from NSPR pipe: %s",
				G_STRFUNC, text);
			break;
		}
	}
}
#endif

EMsgPort *
e_msgport_new (void)
{
	EMsgPort *msgport;

	msgport = g_slice_new (EMsgPort);
	msgport->queue = g_async_queue_new ();
	msgport->pipe[0] = -1;
	msgport->pipe[1] = -1;
#ifdef HAVE_NSS
	msgport->prpipe[0] = NULL;
	msgport->prpipe[1] = NULL;
#endif

	return msgport;
}

void
e_msgport_destroy (EMsgPort *msgport)
{
	g_return_if_fail (msgport != NULL);

	if (msgport->pipe[0] >= 0) {
		E_CLOSE (msgport->pipe[0]);
		E_CLOSE (msgport->pipe[1]);
	}
#ifdef HAVE_NSS
	if (msgport->prpipe[0] != NULL) {
		PR_Close (msgport->prpipe[0]);
		PR_Close (msgport->prpipe[1]);
	}
#endif

	g_async_queue_unref (msgport->queue);
	g_slice_free (EMsgPort, msgport);
}

gint
e_msgport_fd (EMsgPort *msgport)
{
	gint fd;

	g_return_val_if_fail (msgport != NULL, -1);

	g_async_queue_lock (msgport->queue);
	fd = msgport->pipe[0];
	if (fd < 0 && e_pipe (msgport->pipe) == 0)
		fd = msgport->pipe[0];
	g_async_queue_unlock (msgport->queue);

	return fd;
}

#ifdef HAVE_NSS
PRFileDesc *
e_msgport_prfd (EMsgPort *msgport)
{
	PRFileDesc *prfd;

	g_return_val_if_fail (msgport != NULL, NULL);

	g_async_queue_lock (msgport->queue);
	prfd = msgport->prpipe[0];
	if (prfd == NULL && e_prpipe (msgport->prpipe) == 0)
		prfd = msgport->prpipe[0];
	g_async_queue_unlock (msgport->queue);

	return prfd;
}
#endif

void
e_msgport_put (EMsgPort *msgport, EMsg *msg)
{
	gint fd;
#ifdef HAVE_NSS
	PRFileDesc *prfd;
#endif

	g_return_if_fail (msgport != NULL);
	g_return_if_fail (msg != NULL);

	g_async_queue_lock (msgport->queue);

	msg->flags = 0;

	fd = msgport->pipe[1];
	while (fd >= 0) {
		if (E_WRITE (fd, "E", 1) > 0) {
			msg->flags |= MSG_FLAG_SYNC_WITH_PIPE;
			break;
		} else if (!E_IS_STATUS_INTR ()) {
			g_warning ("%s: Failed to write to pipe: %s",
				G_STRFUNC, g_strerror (errno));
			break;
		}
	}

#ifdef HAVE_NSS
	prfd = msgport->prpipe[1];
	while (prfd != NULL) {
		if (PR_Write (prfd, "E", 1) > 0) {
			msg->flags |= MSG_FLAG_SYNC_WITH_PR_PIPE;
			break;
		} else if (PR_GetError () != PR_PENDING_INTERRUPT_ERROR) {
			gchar *text = g_alloca (PR_GetErrorTextLength ());
			PR_GetErrorText (text);
			g_warning ("%s: Failed to write to NSPR pipe: %s",
				G_STRFUNC, text);
			break;
		}
	}
#endif

	g_async_queue_push_unlocked (msgport->queue, msg);
	g_async_queue_unlock (msgport->queue);
}

EMsg *
e_msgport_wait (EMsgPort *msgport)
{
	EMsg *msg;

	g_return_val_if_fail (msgport != NULL, NULL);

	g_async_queue_lock (msgport->queue);

	msg = g_async_queue_pop_unlocked (msgport->queue);

	g_assert (msg != NULL);

	if (msg->flags & MSG_FLAG_SYNC_WITH_PIPE)
		msgport_sync_with_pipe (msgport->pipe[0]);
#ifdef HAVE_NSS
	if (msg->flags & MSG_FLAG_SYNC_WITH_PR_PIPE)
		msgport_sync_with_prpipe (msgport->prpipe[0]);
#endif

	g_async_queue_unlock (msgport->queue);

	return msg;
}

EMsg *
e_msgport_get (EMsgPort *msgport)
{
	EMsg *msg;

	g_return_val_if_fail (msgport != NULL, NULL);

	g_async_queue_lock (msgport->queue);

	msg = g_async_queue_try_pop_unlocked (msgport->queue);

	if (msg != NULL && msg->flags & MSG_FLAG_SYNC_WITH_PIPE)
		msgport_sync_with_pipe (msgport->pipe[0]);
#ifdef HAVE_NSS
	if (msg != NULL && msg->flags & MSG_FLAG_SYNC_WITH_PR_PIPE)
		msgport_sync_with_prpipe (msgport->prpipe[0]);
#endif

	g_async_queue_unlock (msgport->queue);

	return msg;
}

void
e_msgport_reply (EMsg *msg)
{
	g_return_if_fail (msg != NULL);

	if (msg->reply_port)
		e_msgport_put (msg->reply_port, msg);

	/* else lost? */
}

#ifndef EDS_DISABLE_DEPRECATED

struct _thread_info {
	pthread_t id;
	gint busy;
};

struct _EThread {
	struct _EThread *next;
	struct _EThread *prev;

	EMsgPort *server_port;
	EMsgPort *reply_port;
	pthread_mutex_t mutex;
	e_thread_t type;
	gint queue_limit;

	gint waiting;		/* if we are waiting for a new message, count of waiting processes */
	pthread_t id;		/* our running child thread */
	gint have_thread;
	GList *id_list;		/* if THREAD_NEW, then a list of our child threads in thread_info structs */

	EThreadFunc destroy;
	gpointer destroy_data;

	EThreadFunc received;
	gpointer received_data;

	EThreadFunc lost;
	gpointer lost_data;
};

/* All active threads */
static EDList ethread_list = E_DLIST_INITIALISER(ethread_list);
static pthread_mutex_t ethread_lock = PTHREAD_MUTEX_INITIALIZER;

#define E_THREAD_QUIT_REPLYPORT ((struct _EMsgPort *)~0)

static void thread_destroy_msg(EThread *e, EMsg *m);

static struct _thread_info *thread_find(EThread *e, pthread_t id)
{
	GList *node;
	struct _thread_info *info;

	node = e->id_list;
	while (node) {
		info = node->data;
		if (pthread_equal (info->id, id))
			return info;
		node = node->next;
	}
	return NULL;
}

#if 0
static void thread_remove(EThread *e, pthread_t id)
{
	GList *node;
	struct _thread_info *info;

	node = e->id_list;
	while (node) {
		info = node->data;
		if (pthread_equal (info->id, id)) {
			e->id_list = g_list_remove(e->id_list, info);
			g_free(info);
		}
		node = node->next;
	}
}
#endif

EThread *e_thread_new(e_thread_t type)
{
	EThread *e;

	e = g_malloc0(sizeof(*e));
	pthread_mutex_init(&e->mutex, NULL);
	e->type = type;
	e->server_port = e_msgport_new();
	e->have_thread = FALSE;
	e->queue_limit = INT_MAX;

	pthread_mutex_lock(&ethread_lock);
	e_dlist_addtail(&ethread_list, (EDListNode *)e);
	pthread_mutex_unlock(&ethread_lock);

	return e;
}

/* close down the threads & resources etc */
void e_thread_destroy(EThread *e)
{
	gint busy = FALSE;
	EMsg *msg;
	struct _thread_info *info;
	GList *l;

	/* make sure we soak up all the messages first */
	while ((msg = e_msgport_get(e->server_port))) {
		thread_destroy_msg(e, msg);
	}

	pthread_mutex_lock(&e->mutex);

	switch (e->type) {
	case E_THREAD_QUEUE:
	case E_THREAD_DROP:
		/* if we have a thread, 'kill' it */
		if (e->have_thread) {
			pthread_t id = e->id;
			t(printf("Sending thread '%" G_GUINT64_FORMAT "' quit message\n", e_util_pthread_id(id)));

			e->have_thread = FALSE;

			msg = g_malloc0(sizeof(*msg));
			msg->reply_port = E_THREAD_QUIT_REPLYPORT;
			e_msgport_put(e->server_port, msg);

			pthread_mutex_unlock(&e->mutex);
			t(printf("Joining thread '%" G_GUINT64_FORMAT "'\n", e_util_pthread_id(id)));
			pthread_join(id, NULL);
			t(printf("Joined thread '%" G_GUINT64_FORMAT "'!\n", e_util_pthread_id(id)));
			pthread_mutex_lock(&e->mutex);
		}
		busy = e->have_thread;
		break;
	case E_THREAD_NEW:
		/* first, send everyone a quit message */
		l = e->id_list;
		while (l) {
			info = l->data;
			t(printf("Sending thread '%" G_GUINT64_FORMAT "' quit message\n", e_util_pthread_id(info->id)));
			msg = g_malloc0(sizeof(*msg));
			msg->reply_port = E_THREAD_QUIT_REPLYPORT;
			e_msgport_put(e->server_port, msg);
			l = l->next;
		}

		/* then, wait for everyone to quit */
		while (e->id_list) {
			info = e->id_list->data;
			e->id_list = g_list_remove(e->id_list, info);
			pthread_mutex_unlock(&e->mutex);
			t(printf("Joining thread '%" G_GUINT64_FORMAT "'\n", e_util_pthread_id(info->id)));
			pthread_join(info->id, NULL);
			t(printf("Joined thread '%" G_GUINT64_FORMAT "'!\n", e_util_pthread_id(info->id)));
			pthread_mutex_lock(&e->mutex);
			g_free(info);
		}
		busy = g_list_length(e->id_list) != 0;
		break;
	}

	pthread_mutex_unlock(&e->mutex);

	/* and clean up, if we can */
	if (busy) {
		g_warning("threads were busy, leaked EThread");
		return;
	}

	pthread_mutex_lock(&ethread_lock);
	e_dlist_remove((EDListNode *)e);
	pthread_mutex_unlock(&ethread_lock);

	pthread_mutex_destroy(&e->mutex);
	e_msgport_destroy(e->server_port);
	g_free(e);
}

/* set the queue maximum depth, what happens when the queue
   fills up depends on the queue type */
void e_thread_set_queue_limit(EThread *e, gint limit)
{
	e->queue_limit = limit;
}

/* set a msg destroy callback, this can not call any e_thread functions on @e */
void e_thread_set_msg_destroy(EThread *e, EThreadFunc destroy, gpointer data)
{
	pthread_mutex_lock(&e->mutex);
	e->destroy = destroy;
	e->destroy_data = data;
	pthread_mutex_unlock(&e->mutex);
}

/* set a message lost callback, called if any message is discarded */
void e_thread_set_msg_lost(EThread *e, EThreadFunc lost, gpointer data)
{
	pthread_mutex_lock(&e->mutex);
	e->lost = lost;
	e->lost_data = lost;
	pthread_mutex_unlock(&e->mutex);
}

/* set a reply port, if set, then send messages back once finished */
void e_thread_set_reply_port(EThread *e, EMsgPort *reply_port)
{
	e->reply_port = reply_port;
}

/* set a received data callback */
void e_thread_set_msg_received(EThread *e, EThreadFunc received, gpointer data)
{
	pthread_mutex_lock(&e->mutex);
	e->received = received;
	e->received_data = data;
	pthread_mutex_unlock(&e->mutex);
}

/* find out if we're busy doing any work, e==NULL, check for all work */
gint e_thread_busy(EThread *e)
{
	gint busy = FALSE;

	if (e == NULL) {
		pthread_mutex_lock(&ethread_lock);
		e = (EThread *)ethread_list.head;
		while (e->next && !busy) {
			busy = e_thread_busy(e);
			e = e->next;
		}
		pthread_mutex_unlock(&ethread_lock);
	} else {
		pthread_mutex_lock(&e->mutex);
		switch (e->type) {
		case E_THREAD_QUEUE:
		case E_THREAD_DROP:
			busy = e->waiting != 1 && e->have_thread;
			break;
		case E_THREAD_NEW:
			busy = e->waiting != g_list_length(e->id_list);
			break;
		}
		pthread_mutex_unlock(&e->mutex);
	}

	return busy;
}

static void
thread_destroy_msg(EThread *e, EMsg *m)
{
	EThreadFunc func;
	gpointer func_data;

	/* we do this so we never get an incomplete/unmatched callback + data */
	pthread_mutex_lock(&e->mutex);
	func = e->destroy;
	func_data = e->destroy_data;
	pthread_mutex_unlock(&e->mutex);

	if (func)
		func(e, m, func_data);
}

static void
thread_received_msg(EThread *e, EMsg *m)
{
	EThreadFunc func;
	gpointer func_data;

	/* we do this so we never get an incomplete/unmatched callback + data */
	pthread_mutex_lock(&e->mutex);
	func = e->received;
	func_data = e->received_data;
	pthread_mutex_unlock(&e->mutex);

	if (func)
		func(e, m, func_data);
	else
		g_warning("No processing callback for EThread, message unprocessed");
}

static void
thread_lost_msg(EThread *e, EMsg *m)
{
	EThreadFunc func;
	gpointer func_data;

	/* we do this so we never get an incomplete/unmatched callback + data */
	pthread_mutex_lock(&e->mutex);
	func = e->lost;
	func_data = e->lost_data;
	pthread_mutex_unlock(&e->mutex);

	if (func)
		func(e, m, func_data);
}

/* the actual thread dispatcher */
static gpointer
thread_dispatch(gpointer din)
{
	EThread *e = din;
	EMsg *m;
	struct _thread_info *info;
	pthread_t self = pthread_self();

	t(printf("dispatch thread started: %" G_GUINT64_FORMAT "\n", e_util_pthread_id(self)));

	while (1) {
		pthread_mutex_lock(&e->mutex);
		m = e_msgport_get(e->server_port);
		if (m == NULL) {
			/* nothing to do?  If we are a 'new' type thread, just quit.
			   Otherwise, go into waiting (can be cancelled here) */
			info = thread_find(e, self);
			if (info)
				info->busy = FALSE;
			e->waiting++;
			pthread_mutex_unlock(&e->mutex);
			m = e_msgport_wait(e->server_port);
			pthread_mutex_lock(&e->mutex);
			e->waiting--;
		}

		if (m->reply_port == E_THREAD_QUIT_REPLYPORT) {
			t(printf("Thread %" G_GUINT64_FORMAT " got quit message\n", e_util_pthread_id(self)));
			/* Handle a quit message, say we're quitting, free the message, and break out of the loop */
			info = thread_find(e, self);
			if (info)
				info->busy = 2;
			pthread_mutex_unlock(&e->mutex);
			g_free(m);
			break;
		} else {
			info = thread_find(e, self);
			if (info)
				info->busy = TRUE;
		}
		pthread_mutex_unlock(&e->mutex);

		t(printf("got message in dispatch thread\n"));

		/* process it */
		thread_received_msg(e, m);

		/* if we have a reply port, send it back, otherwise, lose it */
		if (m->reply_port) {
			e_msgport_reply(m);
		} else {
			thread_destroy_msg(e, m);
		}
	}

	return NULL;
}

/* send a message to the thread, start thread if necessary */
void e_thread_put(EThread *e, EMsg *msg)
{
	pthread_t id;
	EMsg *dmsg = NULL;

	pthread_mutex_lock(&e->mutex);

	/* the caller forgot to tell us what to do, well, we can't do anything can we */
	if (e->received == NULL) {
		pthread_mutex_unlock(&e->mutex);
		g_warning("EThread called with no receiver function, no work to do!");
		thread_destroy_msg(e, msg);
		return;
	}

	msg->reply_port = e->reply_port;

	switch (e->type) {
	case E_THREAD_QUEUE:
		/* if the queue is full, lose this new addition */
		if (g_async_queue_length(e->server_port->queue) < e->queue_limit) {
			e_msgport_put(e->server_port, msg);
		} else {
			printf("queue limit reached, dropping new message\n");
			dmsg = msg;
		}
		break;
	case E_THREAD_DROP:
		/* if the queue is full, lose the oldest (unprocessed) message */
		if (g_async_queue_length(e->server_port->queue) < e->queue_limit) {
			e_msgport_put(e->server_port, msg);
		} else {
			printf("queue limit reached, dropping old message\n");
			e_msgport_put(e->server_port, msg);
			dmsg = e_msgport_get(e->server_port);
		}
		break;
	case E_THREAD_NEW:
		/* it is possible that an existing thread can catch this message, so
		   we might create a thread with no work to do.
		   but that doesn't matter, the other alternative that it be lost is worse */
		e_msgport_put(e->server_port, msg);
		if (e->waiting == 0
		    && g_list_length(e->id_list) < e->queue_limit
		    && pthread_create(&id, NULL, thread_dispatch, e) == 0) {
			struct _thread_info *info = g_malloc0(sizeof(*info));
			t(printf("created NEW thread %" G_GUINT64_FORMAT "\n", e_util_pthread_id(id)));
			info->id = id;
			info->busy = TRUE;
			e->id_list = g_list_append(e->id_list, info);
		}
		pthread_mutex_unlock(&e->mutex);
		return;
	}

	/* create the thread, if there is none to receive it yet */
	if (!e->have_thread) {
		gint err;

		if ((err = pthread_create(&e->id, NULL, thread_dispatch, e)) != 0) {
			g_warning("Could not create dispatcher thread, message queued?: %s", g_strerror(err));
		} else {
			e->have_thread = TRUE;
		}
	}

	pthread_mutex_unlock(&e->mutex);

	if (dmsg) {
		thread_lost_msg(e, dmsg);
		thread_destroy_msg(e, dmsg);
	}
}
#endif     /* EDS_DISABLE_DEPRECATED */

/* yet-another-mutex interface */
struct _EMutex {
	gint type;
	pthread_t owner;
	gint have_owner;
	short waiters;
	short depth;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
};

/* sigh, this is just painful to have to need, but recursive
   read/write, etc mutexes just aren't very common in thread
   implementations */
/* TODO: Just make it use recursive mutexes if they are available */
EMutex *e_mutex_new(e_mutex_t type)
{
	struct _EMutex *m;

	m = g_malloc(sizeof(*m));
	m->type = type;
	m->waiters = 0;
	m->depth = 0;
	m->have_owner = FALSE;

	switch (type) {
	case E_MUTEX_SIMPLE:
		pthread_mutex_init(&m->mutex, NULL);
		break;
	case E_MUTEX_REC:
		pthread_mutex_init(&m->mutex, NULL);
		pthread_cond_init(&m->cond, NULL);
		break;
		/* read / write ?  flags for same? */
	}

	return m;
}

gint e_mutex_destroy(EMutex *m)
{
	gint ret = 0;

	switch (m->type) {
	case E_MUTEX_SIMPLE:
		ret = pthread_mutex_destroy(&m->mutex);
		if (ret == -1)
			g_warning("EMutex destroy failed: %s", g_strerror(errno));
		g_free(m);
		break;
	case E_MUTEX_REC:
		ret = pthread_mutex_destroy(&m->mutex);
		if (ret == -1)
			g_warning("EMutex destroy failed: %s", g_strerror(errno));
		ret = pthread_cond_destroy(&m->cond);
		if (ret == -1)
			g_warning("EMutex destroy failed: %s", g_strerror(errno));
		g_free(m);

	}
	return ret;
}

gint e_mutex_lock(EMutex *m)
{
	pthread_t id;
	gint err;

	switch (m->type) {
	case E_MUTEX_SIMPLE:
		return pthread_mutex_lock(&m->mutex);
	case E_MUTEX_REC:
		id = pthread_self();
		if ((err = pthread_mutex_lock(&m->mutex)) != 0)
			return err;
		while (1) {
			if (!m->have_owner) {
				m->owner = id;
				m->have_owner = TRUE;
				m->depth = 1;
				break;
			} else if (pthread_equal (id, m->owner)) {
				m->depth++;
				break;
			} else {
				m->waiters++;
				if ((err = pthread_cond_wait(&m->cond, &m->mutex)) != 0)
					return err;
				m->waiters--;
			}
		}
		return pthread_mutex_unlock(&m->mutex);
	}

	return EINVAL;
}

gint e_mutex_unlock(EMutex *m)
{
	gint err;

	switch (m->type) {
	case E_MUTEX_SIMPLE:
		return pthread_mutex_unlock(&m->mutex);
	case E_MUTEX_REC:
		if ((err = pthread_mutex_lock(&m->mutex)) != 0)
			return err;
		g_assert(m->have_owner && pthread_equal(m->owner, pthread_self()));

		m->depth--;
		if (m->depth == 0) {
			m->have_owner = FALSE;
			if (m->waiters > 0)
				pthread_cond_signal(&m->cond);
		}
		return pthread_mutex_unlock(&m->mutex);
	}

	errno = EINVAL;
	return -1;
}

void e_mutex_assert_locked(EMutex *m)
{
	g_return_if_fail (m->type == E_MUTEX_REC);
	pthread_mutex_lock(&m->mutex);
	g_assert(m->have_owner && pthread_equal(m->owner, pthread_self()));
	pthread_mutex_unlock(&m->mutex);
}

gint e_mutex_cond_wait(gpointer vcond, EMutex *m)
{
	gint ret;
	pthread_cond_t *cond = vcond;

	switch (m->type) {
	case E_MUTEX_SIMPLE:
		return pthread_cond_wait(cond, &m->mutex);
	case E_MUTEX_REC:
		if ((ret = pthread_mutex_lock(&m->mutex)) != 0)
			return ret;
		g_assert(m->have_owner && pthread_equal(m->owner, pthread_self()));
		ret = pthread_cond_wait(cond, &m->mutex);
		g_assert(m->have_owner && pthread_equal(m->owner, pthread_self()));
		pthread_mutex_unlock(&m->mutex);
		return ret;
	default:
		g_return_val_if_reached(-1);
	}
}

#ifdef STANDALONE

static EMsgPort *server_port;

gpointer fdserver(gpointer data)
{
	gint fd;
	EMsg *msg;
	gint id = (gint)data;
	fd_set rfds;

	fd = e_msgport_fd(server_port);

	while (1) {
		gint count = 0;

		printf("server %d: waiting on fd %d\n", id, fd);
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		select(fd+1, &rfds, NULL, NULL, NULL);
		printf("server %d: Got async notification, checking for messages\n", id);
		while ((msg = e_msgport_get(server_port))) {
			printf("server %d: got message\n", id);
			g_usleep(1000000);
			printf("server %d: replying\n", id);
			e_msgport_reply(msg);
			count++;
		}
		printf("server %d: got %d messages\n", id, count);
	}
}

gpointer server(gpointer data)
{
	EMsg *msg;
	gint id = (gint)data;

	while (1) {
		printf("server %d: waiting\n", id);
		msg = e_msgport_wait(server_port);
		if (msg) {
			printf("server %d: got message\n", id);
			g_usleep(1000000);
			printf("server %d: replying\n", id);
			e_msgport_reply(msg);
		} else {
			printf("server %d: didn't get message\n", id);
		}
	}
	return NULL;
}

gpointer client(gpointer data)
{
	EMsg *msg;
	EMsgPort *replyport;
	gint i;

	replyport = e_msgport_new();
	msg = g_malloc0(sizeof(*msg));
	msg->reply_port = replyport;
	for (i=0;i<10;i++) {
		/* synchronous operation */
		printf("client: sending\n");
		e_msgport_put(server_port, msg);
		printf("client: waiting for reply\n");
		e_msgport_wait(replyport);
		printf("client: got reply\n");
	}

	printf("client: sleeping ...\n");
	g_usleep(2000000);
	printf("client: sending multiple\n");

	for (i=0;i<10;i++) {
		msg = g_malloc0(sizeof(*msg));
		msg->reply_port = replyport;
		e_msgport_put(server_port, msg);
	}

	printf("client: receiving multiple\n");
	for (i=0;i<10;i++) {
		msg = e_msgport_wait(replyport);
		g_free(msg);
	}

	printf("client: done\n");
	return NULL;
}

gint main(gint argc, gchar **argv)
{
	pthread_t serverid, clientid;

	g_thread_init(NULL);

#ifdef G_OS_WIN32
	{
		WSADATA wsadata;
		if (WSAStartup (MAKEWORD (2, 0), &wsadata) != 0)
			g_error ("Windows Sockets could not be initialized");
	}
#endif

	server_port = e_msgport_new();

	/*pthread_create(&serverid, NULL, server, (gpointer)1);*/
	pthread_create(&serverid, NULL, fdserver, (gpointer)1);
	pthread_create(&clientid, NULL, client, NULL);

	g_usleep(60000000);

	return 0;
}
#endif
