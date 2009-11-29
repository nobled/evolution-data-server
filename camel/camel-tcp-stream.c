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

#include <string.h>

#include "camel-tcp-stream.h"

#define w(x)

static gpointer parent_class;

static void
tcp_stream_class_init (CamelTcpStreamClass *class)
{
	parent_class = g_type_class_peek_parent (class);
}

GType
camel_tcp_stream_get_type (void)
{
	static GType type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID))
		type = g_type_register_static_simple (
			CAMEL_TYPE_STREAM,
			"CamelTcpStream",
			sizeof (CamelTcpStreamClass),
			(GClassInitFunc) tcp_stream_class_init,
			sizeof (CamelTcpStream),
			(GInstanceInitFunc) NULL,
			0);

	return type;
}

/**
 * camel_tcp_stream_connect:
 * @stream: a #CamelTcpStream object
 * @host: a linked list of addrinfo structures to try to connect, in
 * the order of most likely to least likely to work.
 * @error: return location for a #GError, or %NULL
 *
 * Create a socket and connect based upon the data provided.
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_tcp_stream_connect (CamelTcpStream *stream,
                          struct addrinfo *host,
                          GError **error)
{
	CamelTcpStreamClass *class;

	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), -1);

	class = CAMEL_TCP_STREAM_GET_CLASS (stream);
	g_return_val_if_fail (class->connect != NULL, -1);

	return class->connect (stream, host, error);
}

/**
 * camel_tcp_stream_getsockopt:
 * @stream: a #CamelTcpStream object
 * @data: socket option data
 *
 * Get the socket options set on the stream and populate @data.
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_tcp_stream_getsockopt (CamelTcpStream *stream,
                             CamelSockOptData *data)
{
	CamelTcpStreamClass *class;

	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), -1);

	class = CAMEL_TCP_STREAM_GET_CLASS (stream);
	g_return_val_if_fail (class->getsockopt != NULL, -1);

	return class->getsockopt (stream, data);
}

/**
 * camel_tcp_stream_setsockopt:
 * @stream: a #CamelTcpStream object
 * @data: socket option data
 *
 * Set the socket options contained in @data on the stream.
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_tcp_stream_setsockopt (CamelTcpStream *stream,
                             const CamelSockOptData *data)
{
	CamelTcpStreamClass *class;

	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), -1);

	class = CAMEL_TCP_STREAM_GET_CLASS (stream);
	g_return_val_if_fail (class->setsockopt != NULL, -1);

	return class->setsockopt (stream, data);
}

/**
 * camel_tcp_stream_get_local_address:
 * @stream: a #CamelTcpStream object
 * @len: pointer to address length which must be supplied
 *
 * Get the local address of @stream.
 *
 * Returns: the stream's local address (which must be freed with
 * #g_free) if the stream is connected, or %NULL if not
 **/
struct sockaddr *
camel_tcp_stream_get_local_address (CamelTcpStream *stream,
                                    socklen_t *len)
{
	CamelTcpStreamClass *class;

	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), NULL);
	g_return_val_if_fail (len != NULL, NULL);

	class = CAMEL_TCP_STREAM_GET_CLASS (stream);
	g_return_val_if_fail (class->get_local_address != NULL, NULL);

	return class->get_local_address (stream, len);
}

/**
 * camel_tcp_stream_get_remote_address:
 * @stream: a #CamelTcpStream object
 * @len: pointer to address length, which must be supplied
 *
 * Get the remote address of @stream.
 *
 * Returns: the stream's remote address (which must be freed with
 * #g_free) if the stream is connected, or %NULL if not.
 **/
struct sockaddr *
camel_tcp_stream_get_remote_address (CamelTcpStream *stream,
                                     socklen_t *len)
{
	CamelTcpStreamClass *class;

	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), NULL);
	g_return_val_if_fail (len != NULL, NULL);

	class = CAMEL_TCP_STREAM_GET_CLASS (stream);
	g_return_val_if_fail (class->get_remote_address != NULL, NULL);

	return class->get_remote_address (stream, len);
}
