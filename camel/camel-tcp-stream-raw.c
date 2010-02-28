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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>

#include <gio/gio.h>

#include "camel-file-utils.h"
#include "camel-operation.h"
#include "camel-tcp-stream-raw.h"

#ifndef G_OS_WIN32
#define SOCKET_ERROR_CODE() errno
#define SOCKET_CLOSE(fd) close (fd)
#define SOCKET_ERROR_IS_EINPROGRESS() (errno == EINPROGRESS)
#define SOCKET_ERROR_IS_EINTR() (errno == EINTR)
#else
#define SOCKET_ERROR_CODE() WSAGetLastError ()
#define SOCKET_CLOSE(fd) closesocket (fd)
#define SOCKET_ERROR_IS_EINPROGRESS() (WSAGetLastError () == WSAEWOULDBLOCK)
#define SOCKET_ERROR_IS_EINTR() 0 /* No WSAEINTR in WinSock2 */
#undef ETIMEDOUT		/* In case pthreads-win32's <pthread.h> bogusly defined it */
#define ETIMEDOUT EAGAIN
#endif

G_DEFINE_TYPE (CamelTcpStreamRaw, camel_tcp_stream_raw, CAMEL_TYPE_TCP_STREAM)

#ifdef SIMULATE_FLAKY_NETWORK
static gssize
flaky_tcp_write (gint fd, const gchar *buffer, gsize buflen)
{
	gsize len = buflen;
	gssize nwritten;
	gint val;

	if (buflen == 0)
		return 0;

	val = 1 + (gint) (10.0 * rand () / (RAND_MAX + 1.0));

	switch (val) {
	case 1:
		printf ("flaky_tcp_write (%d, ..., %d): (-1) EINTR\n", fd, buflen);
		errno = EINTR;
		return -1;
	case 2:
		printf ("flaky_tcp_write (%d, ..., %d): (-1) EAGAIN\n", fd, buflen);
		errno = EAGAIN;
		return -1;
	case 3:
		printf ("flaky_tcp_write (%d, ..., %d): (-1) EWOULDBLOCK\n", fd, buflen);
		errno = EWOULDBLOCK;
		return -1;
	case 4:
	case 5:
	case 6:
		len = 1 + (gsize) (buflen * rand () / (RAND_MAX + 1.0));
		len = MIN (len, buflen);
		/* fall through... */
	default:
		printf ("flaky_tcp_write (%d, ..., %d): (%d) '%.*s'", fd, buflen, len, (gint) len, buffer);
		nwritten = write (fd, buffer, len);
		if (nwritten < 0)
			printf (" errno => %s\n", g_strerror (errno));
		else if (nwritten < len)
			printf (" only wrote %d bytes\n", nwritten);
		else
			printf ("\n");

		return nwritten;
	}
}

#define write(fd, buffer, buflen) flaky_tcp_write (fd, buffer, buflen)

static gssize
flaky_tcp_read (gint fd, gchar *buffer, gsize buflen)
{
	gsize len = buflen;
	gssize nread;
	gint val;

	if (buflen == 0)
		return 0;

	val = 1 + (gint) (10.0 * rand () / (RAND_MAX + 1.0));

	switch (val) {
	case 1:
		printf ("flaky_tcp_read (%d, ..., %d): (-1) EINTR\n", fd, buflen);
		errno = EINTR;
		return -1;
	case 2:
		printf ("flaky_tcp_read (%d, ..., %d): (-1) EAGAIN\n", fd, buflen);
		errno = EAGAIN;
		return -1;
	case 3:
		printf ("flaky_tcp_read (%d, ..., %d): (-1) EWOULDBLOCK\n", fd, buflen);
		errno = EWOULDBLOCK;
		return -1;
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
	case 10:
		len = 1 + (gsize) (10.0 * rand () / (RAND_MAX + 1.0));
		len = MIN (len, buflen);
		/* fall through... */
	default:
		printf ("flaky_tcp_read (%d, ..., %d): (%d)", fd, buflen, len);
		nread = read (fd, buffer, len);
		if (nread < 0)
			printf (" errno => %s\n", g_strerror (errno));
		else if (nread < len)
			printf (" only read %d bytes\n", nread);
		else
			printf ("\n");

		return nread;
	}
}

#define read(fd, buffer, buflen) flaky_tcp_read (fd, buffer, buflen)

#endif /* SIMULATE_FLAKY_NETWORK */

static gint
get_sockopt_level (const CamelSockOptData *data)
{
	switch (data->option) {
	case CAMEL_SOCKOPT_MAXSEGMENT:
	case CAMEL_SOCKOPT_NODELAY:
		return IPPROTO_TCP;
	default:
		return SOL_SOCKET;
	}
}

static gint
get_sockopt_optname (const CamelSockOptData *data)
{
	switch (data->option) {
#ifdef TCP_MAXSEG
	case CAMEL_SOCKOPT_MAXSEGMENT:
		return TCP_MAXSEG;
#endif
	case CAMEL_SOCKOPT_NODELAY:
		return TCP_NODELAY;
	case CAMEL_SOCKOPT_BROADCAST:
		return SO_BROADCAST;
	case CAMEL_SOCKOPT_KEEPALIVE:
		return SO_KEEPALIVE;
	case CAMEL_SOCKOPT_LINGER:
		return SO_LINGER;
	case CAMEL_SOCKOPT_RECVBUFFERSIZE:
		return SO_RCVBUF;
	case CAMEL_SOCKOPT_SENDBUFFERSIZE:
		return SO_SNDBUF;
	case CAMEL_SOCKOPT_REUSEADDR:
		return SO_REUSEADDR;
	case CAMEL_SOCKOPT_IPTYPEOFSERVICE:
		return SO_TYPE;
	default:
		return -1;
	}
}

/* this is a 'cancellable' connect, cancellable from camel_operation_cancel etc */
/* returns -1 & errno == EINTR if the connection was cancelled */
static gint
socket_connect(struct addrinfo *h)
{
	struct timeval tv;
	socklen_t len;
	gint cancel_fd;
	gint errnosav;
	gint ret, fd;

	/* see if we're cancelled yet */
	if (camel_operation_cancel_check (NULL)) {
		errno = EINTR;
		return -1;
	}

	if (h->ai_socktype != SOCK_STREAM) {
		errno = EINVAL;
		return -1;
	}

	if ((fd = socket (h->ai_family, SOCK_STREAM, 0)) == -1)
		return -1;

	cancel_fd = camel_operation_cancel_fd (NULL);
	if (cancel_fd == -1) {
		if (connect (fd, h->ai_addr, h->ai_addrlen) == -1) {
			/* Yeah, errno is meaningless on Win32 after a
			 * Winsock call fails, but I doubt the callers
			 * check errno anyway.
			 */
			errnosav = errno;
			SOCKET_CLOSE (fd);
			errno = errnosav;
			return -1;
		}

		return fd;
	} else {
#ifndef G_OS_WIN32
		gint flags;
#endif
		gint fdmax, status;
		fd_set rdset, wrset;

#ifndef G_OS_WIN32
		flags = fcntl (fd, F_GETFL);
		fcntl (fd, F_SETFL, flags | O_NONBLOCK);
#else
		{
			u_long yes = 1;
			ioctlsocket (fd, FIONBIO, &yes);
		}
#endif
		if (connect (fd, h->ai_addr, h->ai_addrlen) == 0) {
#ifndef G_OS_WIN32
			fcntl (fd, F_SETFL, flags);
#else
			{
				u_long no = 0;
				ioctlsocket (fd, FIONBIO, &no);
			}
#endif
			return fd;
		}

		if (!SOCKET_ERROR_IS_EINPROGRESS ()) {
			errnosav = errno;
			SOCKET_CLOSE (fd);
			errno = errnosav;
			return -1;
		}

		do {
			FD_ZERO (&rdset);
			FD_ZERO (&wrset);
			FD_SET (fd, &wrset);
			FD_SET (cancel_fd, &rdset);
			fdmax = MAX (fd, cancel_fd) + 1;
			tv.tv_sec = 60 * 4;
			tv.tv_usec = 0;

			status = select (fdmax, &rdset, &wrset, NULL, &tv);
		} while (status == -1 && SOCKET_ERROR_IS_EINTR ());

		if (status <= 0) {
			SOCKET_CLOSE (fd);
			errno = ETIMEDOUT;
			return -1;
		}

		if (cancel_fd != -1 && FD_ISSET (cancel_fd, &rdset)) {
			SOCKET_CLOSE (fd);
			errno = EINTR;
			return -1;
		} else {
			len = sizeof (gint);

			if (getsockopt (fd, SOL_SOCKET, SO_ERROR, (gchar *) &ret, &len) == -1) {
				errnosav = errno;
				SOCKET_CLOSE (fd);
				errno = errnosav;
				return -1;
			}

			if (ret != 0) {
				SOCKET_CLOSE (fd);
				errno = ret;
				return -1;
			}
		}
#ifndef G_OS_WIN32
		fcntl (fd, F_SETFL, flags);
#else
		{
			u_long no = 0;
			ioctlsocket (fd, FIONBIO, &no);
		}
#endif
	}

	return fd;
}

static void
tcp_stream_raw_finalize (GObject *object)
{
	CamelTcpStreamRaw *stream = CAMEL_TCP_STREAM_RAW (object);

	if (stream->sockfd != -1)
		SOCKET_CLOSE (stream->sockfd);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_tcp_stream_raw_parent_class)->finalize (object);
}

static gssize
tcp_stream_raw_read (CamelStream *stream,
                     gchar *buffer,
                     gsize n,
                     GError **error)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);

	return camel_read_socket (raw->sockfd, buffer, n, error);
}

static gssize
tcp_stream_raw_write (CamelStream *stream,
                      const gchar *buffer,
                      gsize n,
                      GError **error)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);

	return camel_write_socket (raw->sockfd, buffer, n, error);
}

static gint
tcp_stream_raw_flush (CamelStream *stream,
                      GError **error)
{
	return 0;
}

static gint
tcp_stream_raw_close (CamelStream *stream,
                      GError **error)
{
	if (SOCKET_CLOSE (((CamelTcpStreamRaw *)stream)->sockfd) == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			"%s", g_strerror (errno));
		return -1;
	}

	((CamelTcpStreamRaw *)stream)->sockfd = -1;
	return 0;
}

static gint
tcp_stream_raw_connect (CamelTcpStream *stream,
                        struct addrinfo *host,
                        GError **error)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);

	g_return_val_if_fail (host != NULL, -1);

	while (host) {
		raw->sockfd = socket_connect(host);
		if (raw->sockfd != -1)
			return 0;

		host = host->ai_next;
	}

	return -1;
}

static gint
tcp_stream_raw_getsockopt (CamelTcpStream *stream,
                           CamelSockOptData *data)
{
	gint optname, optlen;

	if ((optname = get_sockopt_optname (data)) == -1)
		return -1;

	if (data->option == CAMEL_SOCKOPT_NONBLOCKING) {
#ifndef G_OS_WIN32
		gint flags;

		flags = fcntl (((CamelTcpStreamRaw *)stream)->sockfd, F_GETFL);
		if (flags == -1)
			return -1;

		data->value.non_blocking = flags & O_NONBLOCK ? TRUE : FALSE;
#else
		data->value.non_blocking = ((CamelTcpStreamRaw *)stream)->is_nonblocking;
#endif
		return 0;
	}

	return getsockopt (((CamelTcpStreamRaw *)stream)->sockfd,
			   get_sockopt_level (data),
			   optname,
			   (gpointer) &data->value,
			   (socklen_t *) &optlen);
}

static gint
tcp_stream_raw_setsockopt (CamelTcpStream *stream,
                           const CamelSockOptData *data)
{
	gint optname;

	if ((optname = get_sockopt_optname (data)) == -1)
		return -1;

	if (data->option == CAMEL_SOCKOPT_NONBLOCKING) {
#ifndef G_OS_WIN32
		gint flags, set;

		flags = fcntl (((CamelTcpStreamRaw *)stream)->sockfd, F_GETFL);
		if (flags == -1)
			return -1;

		set = data->value.non_blocking ? O_NONBLOCK : 0;
		flags = (flags & ~O_NONBLOCK) | set;

		if (fcntl (((CamelTcpStreamRaw *)stream)->sockfd, F_SETFL, flags) == -1)
			return -1;
#else
		u_long fionbio = data->value.non_blocking ? 1 : 0;
		if (ioctlsocket (((CamelTcpStreamRaw *)stream)->sockfd, FIONBIO, &fionbio) == SOCKET_ERROR)
			return -1;
		((CamelTcpStreamRaw *)stream)->is_nonblocking = data->value.non_blocking ? 1 : 0;
#endif
		return 0;
	}

	return setsockopt (((CamelTcpStreamRaw *)stream)->sockfd,
			   get_sockopt_level (data),
			   optname,
			   (gpointer) &data->value,
			   sizeof (data->value));
}

static struct sockaddr *
tcp_stream_raw_get_local_address (CamelTcpStream *stream,
                                  socklen_t *len)
{
#ifdef ENABLE_IPv6
	struct sockaddr_in6 sin;
#else
	struct sockaddr_in sin;
#endif
	struct sockaddr *saddr = (struct sockaddr *)&sin;

	*len = sizeof(sin);
	if (getsockname (CAMEL_TCP_STREAM_RAW (stream)->sockfd, saddr, len) == -1)
		return NULL;

	saddr = g_malloc(*len);
	memcpy(saddr, &sin, *len);

	return saddr;
}

static struct sockaddr *
tcp_stream_raw_get_remote_address (CamelTcpStream *stream, socklen_t *len)
{
#ifdef ENABLE_IPv6
	struct sockaddr_in6 sin;
#else
	struct sockaddr_in sin;
#endif
	struct sockaddr *saddr = (struct sockaddr *)&sin;

	*len = sizeof(sin);
	if (getpeername (CAMEL_TCP_STREAM_RAW (stream)->sockfd, saddr, len) == -1)
		return NULL;

	saddr = g_malloc(*len);
	memcpy(saddr, &sin, *len);

	return saddr;
}

static void
camel_tcp_stream_raw_class_init (CamelTcpStreamRawClass *class)
{
	GObjectClass *object_class;
	CamelStreamClass *stream_class;
	CamelTcpStreamClass *tcp_stream_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = tcp_stream_raw_finalize;

	stream_class = CAMEL_STREAM_CLASS (class);
	stream_class->read = tcp_stream_raw_read;
	stream_class->write = tcp_stream_raw_write;
	stream_class->flush = tcp_stream_raw_flush;
	stream_class->close = tcp_stream_raw_close;

	tcp_stream_class = CAMEL_TCP_STREAM_CLASS (class);
	tcp_stream_class->connect = tcp_stream_raw_connect;
	tcp_stream_class->getsockopt = tcp_stream_raw_getsockopt;
	tcp_stream_class->setsockopt = tcp_stream_raw_setsockopt;
	tcp_stream_class->get_local_address = tcp_stream_raw_get_local_address;
	tcp_stream_class->get_remote_address = tcp_stream_raw_get_remote_address;
}

static void
camel_tcp_stream_raw_init (CamelTcpStreamRaw *stream)
{
	stream->sockfd = -1;
}

/**
 * camel_tcp_stream_raw_new:
 *
 * Create a new #CamelTcpStreamRaw object.
 *
 * Returns: a new #CamelTcpStream object
 **/
CamelStream *
camel_tcp_stream_raw_new (void)
{
	return g_object_new (CAMEL_TYPE_TCP_STREAM_RAW, NULL);
}
