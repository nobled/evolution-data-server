/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_TCP_STREAM_H
#define CAMEL_TCP_STREAM_H

#ifndef G_OS_WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
typedef struct linger CamelLinger;
#else
typedef struct {
	unsigned short l_onoff;
	unsigned short l_linger;
} CamelLinger;
#define socklen_t int
struct addrinfo;
#endif
#include <unistd.h>

#include <camel/camel-stream.h>

/* Standard GObject macros */
#define CAMEL_TYPE_TCP_STREAM \
	(camel_tcp_stream_get_type ())
#define CAMEL_TCP_STREAM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_TCP_STREAM, CamelTcpStream))
#define CAMEL_TCP_STREAM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_TCP_STREAM, CamelTcpStreamClass))
#define CAMEL_IS_TCP_STREAM(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_TCP_STREAM))
#define CAMEL_IS_TCP_STREAM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_TCP_STREAM))
#define CAMEL_TCP_STREAM_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_TCP_STREAM, CamelTcpStreamClass))

G_BEGIN_DECLS

typedef struct _CamelTcpStream CamelTcpStream;
typedef struct _CamelTcpStreamClass CamelTcpStreamClass;

typedef enum {
	CAMEL_SOCKOPT_NONBLOCKING,     /* nonblocking io */
	CAMEL_SOCKOPT_LINGER,          /* linger on close if data present */
	CAMEL_SOCKOPT_REUSEADDR,       /* allow local address reuse */
	CAMEL_SOCKOPT_KEEPALIVE,       /* keep connections alive */
	CAMEL_SOCKOPT_RECVBUFFERSIZE,  /* receive buffer size */
	CAMEL_SOCKOPT_SENDBUFFERSIZE,  /* send buffer size */

	CAMEL_SOCKOPT_IPTIMETOLIVE,    /* time to live */
	CAMEL_SOCKOPT_IPTYPEOFSERVICE, /* type of service and precedence */

	CAMEL_SOCKOPT_ADDMEMBER,       /* add an IP group membership */
	CAMEL_SOCKOPT_DROPMEMBER,      /* drop an IP group membership */
	CAMEL_SOCKOPT_MCASTINTERFACE,  /* multicast interface address */
	CAMEL_SOCKOPT_MCASTTIMETOLIVE, /* multicast timetolive */
	CAMEL_SOCKOPT_MCASTLOOPBACK,   /* multicast loopback */

	CAMEL_SOCKOPT_NODELAY,         /* don't delay send to coalesce packets */
	CAMEL_SOCKOPT_MAXSEGMENT,      /* maximum segment size */
	CAMEL_SOCKOPT_BROADCAST,       /* enable broadcast */
	CAMEL_SOCKOPT_LAST
} CamelSockOpt;

typedef struct _CamelSockOptData {
	CamelSockOpt option;
	union {
		guint       ip_ttl;              /* IP time to live */
		guint       mcast_ttl;           /* IP multicast time to live */
		guint       tos;                 /* IP type of service and precedence */
		gboolean    non_blocking;        /* Non-blocking (network) I/O */
		gboolean    reuse_addr;          /* Allow local address reuse */
		gboolean    keep_alive;          /* Keep connections alive */
		gboolean    mcast_loopback;      /* IP multicast loopback */
		gboolean    no_delay;            /* Don't delay send to coalesce packets */
		gboolean    broadcast;           /* Enable broadcast */
		gsize      max_segment;         /* Maximum segment size */
		gsize      recv_buffer_size;    /* Receive buffer size */
		gsize      send_buffer_size;    /* Send buffer size */
		CamelLinger linger;              /* Time to linger on close if data present */
	} value;
} CamelSockOptData;

struct _CamelTcpStream {
	CamelStream parent;
};

struct _CamelTcpStreamClass {
	CamelStreamClass parent_class;

	gint		(*connect)		(CamelTcpStream *stream,
						 struct addrinfo *host,
						 GError **error);
	gint		(*getsockopt)		(CamelTcpStream *stream,
						 CamelSockOptData *data);
	gint		(*setsockopt)		(CamelTcpStream *stream,
						 const CamelSockOptData *data);
	struct sockaddr *
			(*get_local_address)	(CamelTcpStream *stream,
						 socklen_t *len);
	struct sockaddr *
			(*get_remote_address)	(CamelTcpStream *stream,
						 socklen_t *len);
};

GType		camel_tcp_stream_get_type	(void);
gint		camel_tcp_stream_connect	(CamelTcpStream *stream,
						 struct addrinfo *host,
						 GError **error);
gint		camel_tcp_stream_getsockopt	(CamelTcpStream *stream,
						 CamelSockOptData *data);
gint		camel_tcp_stream_setsockopt	(CamelTcpStream *stream,
						 const CamelSockOptData *data);
struct sockaddr *
		camel_tcp_stream_get_local_address
						(CamelTcpStream *stream,
						 socklen_t *len);
struct sockaddr *
		camel_tcp_stream_get_remote_address
						(CamelTcpStream *stream,
						 socklen_t *len);

G_END_DECLS

#ifdef G_OS_WIN32
#undef socklen_t
#endif

#endif /* CAMEL_TCP_STREAM_H */
