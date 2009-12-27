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

/* NOTE: This is the default implementation of CamelTcpStreamSSL,
 * used when the Mozilla NSS libraries are used.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_NSS

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <nspr.h>
#include <prio.h>
#include <prerror.h>
#include <prerr.h>
#include <secerr.h>
#include <sslerr.h>
#include "nss.h"    /* Don't use <> here or it will include the system nss.h instead */
#include <ssl.h>
#include <cert.h>
#include <certdb.h>
#include <pk11func.h>

#include <gio/gio.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "camel-certdb.h"
#include "camel-file-utils.h"
#include "camel-operation.h"
#include "camel-private.h"
#include "camel-session.h"
#include "camel-stream-fs.h"
#include "camel-tcp-stream-ssl.h"

#define IO_TIMEOUT (PR_TicksPerSecond() * 4 * 60)
#define CONNECT_TIMEOUT (PR_TicksPerSecond () * 4 * 60)

#define CAMEL_TCP_STREAM_SSL_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_TCP_STREAM_SSL, CamelTcpStreamSSLPrivate))

static gpointer parent_class;

static gssize stream_read (CamelStream *stream, gchar *buffer, gsize n, GError **error);
static gssize stream_write (CamelStream *stream, const gchar *buffer, gsize n, GError **error);
static gint stream_flush  (CamelStream *stream, GError **error);
static gint stream_close  (CamelStream *stream, GError **error);

static PRFileDesc *enable_ssl (CamelTcpStreamSSL *ssl, PRFileDesc *fd);

static gint stream_connect    (CamelTcpStream *stream, struct addrinfo *host, GError **error);
static gint stream_getsockopt (CamelTcpStream *stream, CamelSockOptData *data);
static gint stream_setsockopt (CamelTcpStream *stream, const CamelSockOptData *data);
static struct sockaddr *stream_get_local_address (CamelTcpStream *stream, socklen_t *len);
static struct sockaddr *stream_get_remote_address (CamelTcpStream *stream, socklen_t *len);

struct _CamelTcpStreamSSLPrivate {
	PRFileDesc *sockfd;

	struct _CamelSession *session;
	gchar *expected_host;
	gboolean ssl_mode;
	guint32 flags;
};

static void
tcp_stream_ssl_dispose (GObject *object)
{
	CamelTcpStreamSSLPrivate *priv;

	priv = CAMEL_TCP_STREAM_SSL_GET_PRIVATE (object);

	if (priv->session != NULL) {
		g_object_unref (priv->session);
		priv->session = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
tcp_stream_ssl_finalize (GObject *object)
{
	CamelTcpStreamSSLPrivate *priv;

	priv = CAMEL_TCP_STREAM_SSL_GET_PRIVATE (object);

	if (priv->sockfd != NULL) {
		PR_Shutdown (priv->sockfd, PR_SHUTDOWN_BOTH);
		PR_Close (priv->sockfd);
	}

	g_free (priv->expected_host);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
tcp_stream_ssl_class_init (CamelTcpStreamSSLClass *class)
{
	GObjectClass *object_class;
	CamelStreamClass *stream_class;
	CamelTcpStreamClass *tcp_stream_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (CamelTcpStreamSSLPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = tcp_stream_ssl_dispose;
	object_class->finalize = tcp_stream_ssl_finalize;

	stream_class = CAMEL_STREAM_CLASS (class);
	stream_class->read = stream_read;
	stream_class->write = stream_write;
	stream_class->flush = stream_flush;
	stream_class->close = stream_close;

	tcp_stream_class = CAMEL_TCP_STREAM_CLASS (class);
	tcp_stream_class->connect = stream_connect;
	tcp_stream_class->getsockopt = stream_getsockopt;
	tcp_stream_class->setsockopt = stream_setsockopt;
	tcp_stream_class->get_local_address  = stream_get_local_address;
	tcp_stream_class->get_remote_address = stream_get_remote_address;
}

static void
tcp_stream_ssl_init (CamelTcpStreamSSL *stream)
{
	stream->priv = CAMEL_TCP_STREAM_SSL_GET_PRIVATE (stream);
}

GType
camel_tcp_stream_ssl_get_type (void)
{
	static GType type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID))
		type = g_type_register_static_simple (
			CAMEL_TYPE_TCP_STREAM,
			"CamelTcpStreamSSL",
			sizeof (CamelTcpStreamSSLClass),
			(GClassInitFunc) tcp_stream_ssl_class_init,
			sizeof (CamelTcpStreamSSL),
			(GInstanceInitFunc) tcp_stream_ssl_init,
			0);

	return type;
}

/**
 * camel_tcp_stream_ssl_new:
 * @session: an active #CamelSession object
 * @expected_host: host that the stream is expected to connect with
 * @flags: a bitwise combination of any of
 * #CAMEL_TCP_STREAM_SSL_ENABLE_SSL2,
 * #CAMEL_TCP_STREAM_SSL_ENABLE_SSL3 or
 * #CAMEL_TCP_STREAM_SSL_ENABLE_TLS
 *
 * Since the SSL certificate authenticator may need to prompt the
 * user, a #CamelSession is needed. @expected_host is needed as a
 * protection against an MITM attack.
 *
 * Returns: a new #CamelTcpStreamSSL stream preset in SSL mode
 **/
CamelStream *
camel_tcp_stream_ssl_new (CamelSession *session, const gchar *expected_host, guint32 flags)
{
	CamelTcpStreamSSL *stream;

	g_assert(CAMEL_IS_SESSION(session));

	stream = g_object_new (CAMEL_TYPE_TCP_STREAM_SSL, NULL);

	stream->priv->session = g_object_ref (session);
	stream->priv->expected_host = g_strdup (expected_host);
	stream->priv->ssl_mode = TRUE;
	stream->priv->flags = flags;

	return CAMEL_STREAM (stream);
}

/**
 * camel_tcp_stream_ssl_new_raw:
 * @session: an active #CamelSession object
 * @expected_host: host that the stream is expected to connect with
 * @flags: a bitwise combination of any of
 * #CAMEL_TCP_STREAM_SSL_ENABLE_SSL2,
 * #CAMEL_TCP_STREAM_SSL_ENABLE_SSL3 or
 * #CAMEL_TCP_STREAM_SSL_ENABLE_TLS
 *
 * Since the SSL certificate authenticator may need to prompt the
 * user, a CamelSession is needed. @expected_host is needed as a
 * protection against an MITM attack.
 *
 * Returns: a new #CamelTcpStreamSSL stream not yet toggled into SSL mode
 **/
CamelStream *
camel_tcp_stream_ssl_new_raw (CamelSession *session, const gchar *expected_host, guint32 flags)
{
	CamelTcpStreamSSL *stream;

	g_assert(CAMEL_IS_SESSION(session));

	stream = g_object_new (CAMEL_TYPE_TCP_STREAM_SSL, NULL);

	stream->priv->session = g_object_ref (session);
	stream->priv->expected_host = g_strdup (expected_host);
	stream->priv->ssl_mode = FALSE;
	stream->priv->flags = flags;

	return CAMEL_STREAM (stream);
}

static void
set_errno (gint code)
{
	/* FIXME: this should handle more. */
	switch (code) {
	case PR_INVALID_ARGUMENT_ERROR:
		errno = EINVAL;
		break;
	case PR_PENDING_INTERRUPT_ERROR:
		errno = EINTR;
		break;
	case PR_IO_PENDING_ERROR:
		errno = EAGAIN;
		break;
#ifdef EWOULDBLOCK
	case PR_WOULD_BLOCK_ERROR:
		errno = EWOULDBLOCK;
		break;
#endif
#ifdef EINPROGRESS
	case PR_IN_PROGRESS_ERROR:
		errno = EINPROGRESS;
		break;
#endif
#ifdef EALREADY
	case PR_ALREADY_INITIATED_ERROR:
		errno = EALREADY;
		break;
#endif
#ifdef EHOSTUNREACH
	case PR_NETWORK_UNREACHABLE_ERROR:
		errno = EHOSTUNREACH;
		break;
#endif
#ifdef ECONNREFUSED
	case PR_CONNECT_REFUSED_ERROR:
		errno = ECONNREFUSED;
		break;
#endif
#ifdef ETIMEDOUT
	case PR_CONNECT_TIMEOUT_ERROR:
	case PR_IO_TIMEOUT_ERROR:
		errno = ETIMEDOUT;
		break;
#endif
#ifdef ENOTCONN
	case PR_NOT_CONNECTED_ERROR:
		errno = ENOTCONN;
		break;
#endif
#ifdef ECONNRESET
	case PR_CONNECT_RESET_ERROR:
		errno = ECONNRESET;
		break;
#endif
	case PR_IO_ERROR:
	default:
		errno = EIO;
		break;
	}
}

/**
 * camel_tcp_stream_ssl_enable_ssl:
 * @ssl: a #CamelTcpStreamSSL object
 *
 * Toggles an ssl-capable stream into ssl mode (if it isn't already).
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_tcp_stream_ssl_enable_ssl (CamelTcpStreamSSL *ssl)
{
	PRFileDesc *fd;

	g_return_val_if_fail (CAMEL_IS_TCP_STREAM_SSL (ssl), -1);

	if (ssl->priv->sockfd && !ssl->priv->ssl_mode) {
		if (!(fd = enable_ssl (ssl, NULL))) {
			set_errno (PR_GetError ());
			return -1;
		}

		ssl->priv->sockfd = fd;

		if (SSL_ResetHandshake (fd, FALSE) == SECFailure) {
			set_errno (PR_GetError ());
			return -1;
		}

		if (SSL_ForceHandshake (fd) == SECFailure) {
			set_errno (PR_GetError ());
			return -1;
		}
	}

	ssl->priv->ssl_mode = TRUE;

	return 0;
}

static gssize
stream_read (CamelStream *stream, gchar *buffer, gsize n, GError **error)
{
	CamelTcpStreamSSL *tcp_stream_ssl = CAMEL_TCP_STREAM_SSL (stream);
	PRFileDesc *cancel_fd;
	gssize nread;

	if (camel_operation_cancel_check (NULL)) {
		g_set_error (
			error, G_IO_ERROR,
			G_IO_ERROR_CANCELLED,
			_("Cancelled read"));
		errno = EINTR;
		return -1;
	}

	cancel_fd = camel_operation_cancel_prfd (NULL);
	if (cancel_fd == NULL) {
		do {
			nread = PR_Read (tcp_stream_ssl->priv->sockfd, buffer, n);
			if (nread == -1)
				set_errno (PR_GetError ());
		} while (nread == -1 && (PR_GetError () == PR_PENDING_INTERRUPT_ERROR ||
					 PR_GetError () == PR_IO_PENDING_ERROR ||
					 PR_GetError () == PR_WOULD_BLOCK_ERROR));
	} else {
		PRSocketOptionData sockopts;
		PRPollDesc pollfds[2];
		gboolean nonblock;
		gint error;

		/* get O_NONBLOCK options */
		sockopts.option = PR_SockOpt_Nonblocking;
		PR_GetSocketOption (tcp_stream_ssl->priv->sockfd, &sockopts);
		sockopts.option = PR_SockOpt_Nonblocking;
		nonblock = sockopts.value.non_blocking;
		sockopts.value.non_blocking = TRUE;
		PR_SetSocketOption (tcp_stream_ssl->priv->sockfd, &sockopts);

		pollfds[0].fd = tcp_stream_ssl->priv->sockfd;
		pollfds[0].in_flags = PR_POLL_READ;
		pollfds[1].fd = cancel_fd;
		pollfds[1].in_flags = PR_POLL_READ;

		do {
			PRInt32 res;

			pollfds[0].out_flags = 0;
			pollfds[1].out_flags = 0;
			nread = -1;

			res = PR_Poll(pollfds, 2, IO_TIMEOUT);
			if (res == -1)
				set_errno(PR_GetError());
			else if (res == 0) {
#ifdef ETIMEDOUT
				errno = ETIMEDOUT;
#else
				errno = EIO;
#endif
				goto failed;
			} else if (pollfds[1].out_flags == PR_POLL_READ) {
				errno = EINTR;
				goto failed;
			} else {
				do {
					nread = PR_Read (tcp_stream_ssl->priv->sockfd, buffer, n);
					if (nread == -1)
						set_errno (PR_GetError ());
				} while (nread == -1 && PR_GetError () == PR_PENDING_INTERRUPT_ERROR);
			}
		} while (nread == -1 && (PR_GetError () == PR_PENDING_INTERRUPT_ERROR ||
					 PR_GetError () == PR_IO_PENDING_ERROR ||
					 PR_GetError () == PR_WOULD_BLOCK_ERROR));

		/* restore O_NONBLOCK options */
	failed:
		error = errno;
		sockopts.option = PR_SockOpt_Nonblocking;
		sockopts.value.non_blocking = nonblock;
		PR_SetSocketOption (tcp_stream_ssl->priv->sockfd, &sockopts);
		errno = error;
	}

	return nread;
}

static gssize
stream_write (CamelStream *stream,
              const gchar *buffer,
              gsize n,
              GError **error)
{
	CamelTcpStreamSSL *tcp_stream_ssl = CAMEL_TCP_STREAM_SSL (stream);
	gssize w, written = 0;
	PRFileDesc *cancel_fd;

	if (camel_operation_cancel_check (NULL)) {
		g_set_error (
			error, G_IO_ERROR,
			G_IO_ERROR_CANCELLED,
			_("Cancelled write"));
		errno = EINTR;
		return -1;
	}

	cancel_fd = camel_operation_cancel_prfd (NULL);
	if (cancel_fd == NULL) {
		do {
			do {
				w = PR_Write (tcp_stream_ssl->priv->sockfd, buffer + written, n - written);
				if (w == -1)
					set_errno (PR_GetError ());
			} while (w == -1 && (PR_GetError () == PR_PENDING_INTERRUPT_ERROR ||
					     PR_GetError () == PR_IO_PENDING_ERROR ||
					     PR_GetError () == PR_WOULD_BLOCK_ERROR));

			if (w > 0)
				written += w;
		} while (w != -1 && written < n);
	} else {
		PRSocketOptionData sockopts;
		PRPollDesc pollfds[2];
		gboolean nonblock;
		gint error;

		/* get O_NONBLOCK options */
		sockopts.option = PR_SockOpt_Nonblocking;
		PR_GetSocketOption (tcp_stream_ssl->priv->sockfd, &sockopts);
		sockopts.option = PR_SockOpt_Nonblocking;
		nonblock = sockopts.value.non_blocking;
		sockopts.value.non_blocking = TRUE;
		PR_SetSocketOption (tcp_stream_ssl->priv->sockfd, &sockopts);

		pollfds[0].fd = tcp_stream_ssl->priv->sockfd;
		pollfds[0].in_flags = PR_POLL_WRITE;
		pollfds[1].fd = cancel_fd;
		pollfds[1].in_flags = PR_POLL_READ;

		do {
			PRInt32 res;

			pollfds[0].out_flags = 0;
			pollfds[1].out_flags = 0;
			w = -1;

			res = PR_Poll (pollfds, 2, IO_TIMEOUT);
			if (res == -1) {
				set_errno(PR_GetError());
				if (PR_GetError () == PR_PENDING_INTERRUPT_ERROR)
					w = 0;
			} else if (res == 0) {
#ifdef ETIMEDOUT
				errno = ETIMEDOUT;
#else
				errno = EIO;
#endif
			} else if (pollfds[1].out_flags == PR_POLL_READ) {
				errno = EINTR;
			} else {
				do {
					w = PR_Write (tcp_stream_ssl->priv->sockfd, buffer + written, n - written);
					if (w == -1)
						set_errno (PR_GetError ());
				} while (w == -1 && PR_GetError () == PR_PENDING_INTERRUPT_ERROR);

				if (w == -1) {
					if (PR_GetError () == PR_IO_PENDING_ERROR ||
					    PR_GetError () == PR_WOULD_BLOCK_ERROR)
						w = 0;
				} else
					written += w;
			}
		} while (w != -1 && written < n);

		/* restore O_NONBLOCK options */
		error = errno;
		sockopts.option = PR_SockOpt_Nonblocking;
		sockopts.value.non_blocking = nonblock;
		PR_SetSocketOption (tcp_stream_ssl->priv->sockfd, &sockopts);
		errno = error;
	}

	if (errno != 0) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			"%s", g_strerror (errno));
		return -1;
	}

	return written;
}

static gint
stream_flush (CamelStream *stream,
              GError **error)
{
	/*return PR_Sync (((CamelTcpStreamSSL *)stream)->priv->sockfd);*/
	return 0;
}

static gint
stream_close (CamelStream *stream,
              GError **error)
{
	if (((CamelTcpStreamSSL *)stream)->priv->sockfd == NULL) {
		g_set_error (
			error, G_IO_ERROR,
			G_IO_ERROR_INVALID_ARGUMENT,
			_("Cannot close SSL stream"));
		errno = EINVAL;
		return -1;
	}

	PR_Shutdown (((CamelTcpStreamSSL *)stream)->priv->sockfd, PR_SHUTDOWN_BOTH);
	if (PR_Close (((CamelTcpStreamSSL *)stream)->priv->sockfd) == PR_FAILURE)
		return -1;

	((CamelTcpStreamSSL *)stream)->priv->sockfd = NULL;

	return 0;
}

#if 0
/* Since this is default implementation, let NSS handle it. */
static SECStatus
ssl_get_client_auth (gpointer data, PRFileDesc *sockfd,
		     struct CERTDistNamesStr *caNames,
		     struct CERTCertificateStr **pRetCert,
		     struct SECKEYPrivateKeyStr **pRetKey)
{
	SECStatus status = SECFailure;
	SECKEYPrivateKey *privkey;
	CERTCertificate *cert;
	gpointer proto_win;

	proto_win = SSL_RevealPinArg (sockfd);

	if ((gchar *) data) {
		cert = PK11_FindCertFromNickname ((gchar *) data, proto_win);
		if (cert) {
			privKey = PK11_FindKeyByAnyCert (cert, proto_win);
			if (privkey) {
				status = SECSuccess;
			} else {
				CERT_DestroyCertificate (cert);
			}
		}
	} else {
		/* no nickname given, automatically find the right cert */
		CERTCertNicknames *names;
		gint i;

		names = CERT_GetCertNicknames (CERT_GetDefaultCertDB (),
					       SEC_CERT_NICKNAMES_USER,
					       proto_win);

		if (names != NULL) {
			for (i = 0; i < names->numnicknames; i++) {
				cert = PK11_FindCertFromNickname (names->nicknames[i],
								  proto_win);
				if (!cert)
					continue;

				/* Only check unexpired certs */
				if (CERT_CheckCertValidTimes (cert, PR_Now (), PR_FALSE) != secCertTimeValid) {
					CERT_DestroyCertificate (cert);
					continue;
				}

				status = NSS_CmpCertChainWCANames (cert, caNames);
				if (status == SECSuccess) {
					privkey = PK11_FindKeyByAnyCert (cert, proto_win);
					if (privkey)
						break;

					status = SECFailure;
					break;
				}

				CERT_FreeNicknames (names);
			}
		}
	}

	if (status == SECSuccess) {
		*pRetCert = cert;
		*pRetKey  = privkey;
	}

	return status;
}
#endif

#if 0
/* Since this is the default NSS implementation, no need for us to use this. */
static SECStatus
ssl_auth_cert (gpointer data, PRFileDesc *sockfd, PRBool checksig, PRBool is_server)
{
	CERTCertificate *cert;
	SECStatus status;
	gpointer pinarg;
	gchar *host;

	cert = SSL_PeerCertificate (sockfd);
	pinarg = SSL_RevealPinArg (sockfd);
	status = CERT_VerifyCertNow ((CERTCertDBHandle *)data, cert,
				     checksig, certUsageSSLClient, pinarg);

	if (status != SECSuccess)
		return SECFailure;

	/* Certificate is OK.  Since this is the client side of an SSL
	 * connection, we need to verify that the name field in the cert
	 * matches the desired hostname.  This is our defense against
	 * man-in-the-middle attacks.
	 */

	/* SSL_RevealURL returns a hostname, not a URL. */
	host = SSL_RevealURL (sockfd);

	if (host && *host) {
		status = CERT_VerifyCertName (cert, host);
	} else {
		PR_SetError (SSL_ERROR_BAD_CERT_DOMAIN, 0);
		status = SECFailure;
	}

	if (host)
		PR_Free (host);

	return secStatus;
}
#endif

CamelCert *camel_certdb_nss_cert_get(CamelCertDB *certdb, CERTCertificate *cert);
CamelCert *camel_certdb_nss_cert_add(CamelCertDB *certdb, CERTCertificate *cert);
void camel_certdb_nss_cert_set(CamelCertDB *certdb, CamelCert *ccert, CERTCertificate *cert);

static gchar *
cert_fingerprint(CERTCertificate *cert)
{
	GChecksum *checksum;
	guint8 *digest;
	gsize length;
	guchar fingerprint[50], *f;
	gint i;
	const gchar tohex[16] = "0123456789abcdef";

	length = g_checksum_type_get_length (G_CHECKSUM_MD5);
	digest = g_alloca (length);

	checksum = g_checksum_new (G_CHECKSUM_MD5);
	g_checksum_update (checksum, cert->derCert.data, cert->derCert.len);
	g_checksum_get_digest (checksum, digest, &length);
	g_checksum_free (checksum);

	for (i=0,f = fingerprint; i < length; i++) {
		guint c = digest[i];

		*f++ = tohex[(c >> 4) & 0xf];
		*f++ = tohex[c & 0xf];
#ifndef G_OS_WIN32
		*f++ = ':';
#else
		/* The fingerprint is used as a file name, can't have
		 * colons in file names. Use underscore instead.
		 */
		*f++ = '_';
#endif
	}

	fingerprint[47] = 0;

	return g_strdup((gchar *) fingerprint);
}

/* lookup a cert uses fingerprint to index an on-disk file */
CamelCert *
camel_certdb_nss_cert_get(CamelCertDB *certdb, CERTCertificate *cert)
{
	gchar *fingerprint;
	CamelCert *ccert;

	fingerprint = cert_fingerprint (cert);
	ccert = camel_certdb_get_cert (certdb, fingerprint);
	if (ccert == NULL) {
		g_free (fingerprint);
		return ccert;
	}

	if (ccert->rawcert == NULL) {
		GByteArray *array;
		gchar *filename;
		gchar *contents;
		gsize length;
		GError *error = NULL;

		filename = g_build_filename (
			g_get_home_dir (), ".camel_certs", fingerprint, NULL);
		g_file_get_contents (filename, &contents, &length, &error);
		if (error != NULL) {
			g_warning (
				"Could not load cert %s: %s",
				filename, error->message);
			g_error_free (error);

			camel_cert_set_trust (
				certdb, ccert, CAMEL_CERT_TRUST_UNKNOWN);
			camel_certdb_touch (certdb);
			g_free (fingerprint);
			g_free (filename);

			return ccert;
		}
		g_free (filename);

		array = g_byte_array_sized_new (length);
		g_byte_array_append (array, (guint8 *) contents, length);
		g_free (contents);

		ccert->rawcert = array;
	}

	g_free(fingerprint);
	if (ccert->rawcert->len != cert->derCert.len
	    || memcmp(ccert->rawcert->data, cert->derCert.data, cert->derCert.len) != 0) {
		g_warning("rawcert != derCer");
		camel_cert_set_trust(certdb, ccert, CAMEL_CERT_TRUST_UNKNOWN);
		camel_certdb_touch(certdb);
	}

	return ccert;
}

/* add a cert to the certdb */
CamelCert *
camel_certdb_nss_cert_add(CamelCertDB *certdb, CERTCertificate *cert)
{
	CamelCert *ccert;
	gchar *fingerprint;

	fingerprint = cert_fingerprint(cert);

	ccert = camel_certdb_cert_new(certdb);
	camel_cert_set_issuer(certdb, ccert, CERT_NameToAscii(&cert->issuer));
	camel_cert_set_subject(certdb, ccert, CERT_NameToAscii(&cert->subject));
	/* hostname is set in caller */
	/*camel_cert_set_hostname(certdb, ccert, ssl->priv->expected_host);*/
	camel_cert_set_fingerprint(certdb, ccert, fingerprint);
	camel_cert_set_trust(certdb, ccert, CAMEL_CERT_TRUST_UNKNOWN);
	g_free(fingerprint);

	camel_certdb_nss_cert_set(certdb, ccert, cert);

	camel_certdb_add(certdb, ccert);

	return ccert;
}

/* set the 'raw' cert (& save it) */
void
camel_certdb_nss_cert_set(CamelCertDB *certdb, CamelCert *ccert, CERTCertificate *cert)
{
	gchar *dir, *path, *fingerprint;
	CamelStream *stream;
	struct stat st;

	fingerprint = ccert->fingerprint;

	if (ccert->rawcert == NULL)
		ccert->rawcert = g_byte_array_new ();

	g_byte_array_set_size (ccert->rawcert, cert->derCert.len);
	memcpy (ccert->rawcert->data, cert->derCert.data, cert->derCert.len);

#ifndef G_OS_WIN32
	dir = g_strdup_printf ("%s/.camel_certs", getenv ("HOME"));
#else
	dir = g_build_filename (g_get_home_dir (), ".camel_certs", NULL);
#endif
	if (g_stat (dir, &st) == -1 && g_mkdir (dir, 0700) == -1) {
		g_warning ("Could not create cert directory '%s': %s", dir, g_strerror (errno));
		g_free (dir);
		return;
	}

	path = g_strdup_printf ("%s/%s", dir, fingerprint);
	g_free (dir);

	stream = camel_stream_fs_new_with_name (
		path, O_WRONLY | O_CREAT | O_TRUNC, 0600, NULL);
	if (stream != NULL) {
		if (camel_stream_write (
			stream, (const gchar *) ccert->rawcert->data,
			ccert->rawcert->len, NULL) == -1) {
			g_warning ("Could not save cert: %s: %s", path, g_strerror (errno));
			g_unlink (path);
		}
		camel_stream_close (stream, NULL);
		g_object_unref (stream);
	} else {
		g_warning ("Could not save cert: %s: %s", path, g_strerror (errno));
	}

	g_free (path);
}

#if 0
/* used by the mozilla-like code below */
static gchar *
get_nickname(CERTCertificate *cert)
{
	gchar *server, *nick = NULL;
	gint i;
	PRBool status = PR_TRUE;

	server = CERT_GetCommonName(&cert->subject);
	if (server == NULL)
		return NULL;

	for (i=1;status == PR_TRUE;i++) {
		if (nick) {
			g_free(nick);
			nick = g_strdup_printf("%s #%d", server, i);
		} else {
			nick = g_strdup(server);
		}
		status = SEC_CertNicknameConflict(server, &cert->derSubject, cert->dbhandle);
	}

	return nick;
}
#endif

static SECStatus
ssl_bad_cert (gpointer data, PRFileDesc *sockfd)
{
	gboolean accept;
	CamelCertDB *certdb = NULL;
	CamelCert *ccert = NULL;
	gchar *prompt, *cert_str, *fingerprint;
	CamelTcpStreamSSL *ssl;
	CERTCertificate *cert;
	SECStatus status = SECFailure;

	g_return_val_if_fail (data != NULL, SECFailure);
	g_return_val_if_fail (CAMEL_IS_TCP_STREAM_SSL (data), SECFailure);

	ssl = data;

	cert = SSL_PeerCertificate (sockfd);
	if (cert == NULL)
		return SECFailure;

	certdb = camel_certdb_get_default();
	ccert = camel_certdb_nss_cert_get(certdb, cert);
	if (ccert == NULL) {
		ccert = camel_certdb_nss_cert_add(certdb, cert);
		camel_cert_set_hostname(certdb, ccert, ssl->priv->expected_host);
	}

	if (ccert->trust == CAMEL_CERT_TRUST_UNKNOWN) {
		status = CERT_VerifyCertNow(cert->dbhandle, cert, TRUE, certUsageSSLClient, NULL);
		fingerprint = cert_fingerprint(cert);
		cert_str = g_strdup_printf (_("Issuer:            %s\n"
					      "Subject:           %s\n"
					      "Fingerprint:       %s\n"
					      "Signature:         %s"),
					    CERT_NameToAscii (&cert->issuer),
					    CERT_NameToAscii (&cert->subject),
					    fingerprint, status == SECSuccess?_("GOOD"):_("BAD"));
		g_free(fingerprint);

		/* construct our user prompt */
		prompt = g_strdup_printf (_("SSL Certificate check for %s:\n\n%s\n\nDo you wish to accept?"),
					  ssl->priv->expected_host, cert_str);
		g_free (cert_str);

		/* query the user to find out if we want to accept this certificate */
		accept = camel_session_alert_user (ssl->priv->session, CAMEL_SESSION_ALERT_WARNING, prompt, TRUE);
		g_free(prompt);
		if (accept) {
			camel_certdb_nss_cert_set(certdb, ccert, cert);
			camel_cert_set_trust(certdb, ccert, CAMEL_CERT_TRUST_FULLY);
			camel_certdb_touch(certdb);
		}
	} else {
		accept = ccert->trust != CAMEL_CERT_TRUST_NEVER;
	}

	camel_certdb_cert_unref(certdb, ccert);
	g_object_unref (certdb);

	return accept ? SECSuccess : SECFailure;

#if 0
	gint i, error;
	CERTCertTrust trust;
	SECItem *certs[1];
	gint go = 1;
	gchar *host, *nick;

	error = PR_GetError();

	/* This code is basically what mozilla does - however it doesn't seem to work here
	   very reliably :-/ */
	while (go && status != SECSuccess) {
		gchar *prompt = NULL;

		printf("looping, error '%d'\n", error);

		switch (error) {
		case SEC_ERROR_UNKNOWN_ISSUER:
		case SEC_ERROR_CA_CERT_INVALID:
		case SEC_ERROR_UNTRUSTED_ISSUER:
		case SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE:
			/* add certificate */
			printf("unknown issuer, adding ... \n");
			prompt = g_strdup_printf(_("Certificate problem: %s\nIssuer: %s"), cert->subjectName, cert->issuerName);

			if (camel_session_alert_user(ssl->priv->session, CAMEL_SESSION_ALERT_WARNING, prompt, TRUE)) {

				nick = get_nickname(cert);
				if (NULL == nick) {
					g_free(prompt);
					status = SECFailure;
					break;
				}

				printf("adding cert '%s'\n", nick);

				if (!cert->trust) {
					cert->trust = (CERTCertTrust*)PORT_ArenaZAlloc(cert->arena, sizeof(CERTCertTrust));
					CERT_DecodeTrustString(cert->trust, "P");
				}

				certs[0] = &cert->derCert;
				/*CERT_ImportCerts (cert->dbhandle, certUsageSSLServer, 1, certs, NULL, TRUE, FALSE, nick);*/
				CERT_ImportCerts(cert->dbhandle, certUsageUserCertImport, 1, certs, NULL, TRUE, FALSE, nick);
				g_free(nick);

				printf(" cert type %08x\n", cert->nsCertType);

				memset((gpointer)&trust, 0, sizeof(trust));
				if (CERT_GetCertTrust(cert, &trust) != SECSuccess) {
					CERT_DecodeTrustString(&trust, "P");
				}
				trust.sslFlags |= CERTDB_VALID_PEER | CERTDB_TRUSTED;
				if (CERT_ChangeCertTrust(cert->dbhandle, cert, &trust) != SECSuccess) {
					printf("couldn't change cert trust?\n");
				}

				/*status = SECSuccess;*/
#if 1
				/* re-verify? */
				status = CERT_VerifyCertNow(cert->dbhandle, cert, TRUE, certUsageSSLServer, NULL);
				error = PR_GetError();
				printf("re-verify status %d, error %d\n", status, error);
#endif

				printf(" cert type %08x\n", cert->nsCertType);
			} else {
				printf("failed/cancelled\n");
				go = 0;
			}

			break;
		case SSL_ERROR_BAD_CERT_DOMAIN:
			printf("bad domain\n");

			prompt = g_strdup_printf(_("Bad certificate domain: %s\nIssuer: %s"), cert->subjectName, cert->issuerName);

			if (camel_session_alert_user (ssl->priv->session, CAMEL_SESSION_ALERT_WARNING, prompt, TRUE)) {
				host = SSL_RevealURL(sockfd);
				status = CERT_AddOKDomainName(cert, host);
				printf("add ok domain name : %s\n", status == SECFailure?"fail":"ok");
				error = PR_GetError();
				if (status == SECFailure)
					go = 0;
			} else {
				go = 0;
			}

			break;

		case SEC_ERROR_EXPIRED_CERTIFICATE:
			printf("expired\n");

			prompt = g_strdup_printf(_("Certificate expired: %s\nIssuer: %s"), cert->subjectName, cert->issuerName);

			if (camel_session_alert_user(ssl->priv->session, CAMEL_SESSION_ALERT_WARNING, prompt, TRUE)) {
				cert->timeOK = PR_TRUE;
				status = CERT_VerifyCertNow(cert->dbhandle, cert, TRUE, certUsageSSLClient, NULL);
				error = PR_GetError();
				if (status == SECFailure)
					go = 0;
			} else {
				go = 0;
			}

			break;

		case SEC_ERROR_CRL_EXPIRED:
			printf("crl expired\n");

			prompt = g_strdup_printf(_("Certificate revocation list expired: %s\nIssuer: %s"), cert->subjectName, cert->issuerName);

			if (camel_session_alert_user(ssl->priv->session, CAMEL_SESSION_ALERT_WARNING, prompt, TRUE)) {
				host = SSL_RevealURL(sockfd);
				status = CERT_AddOKDomainName(cert, host);
			}

			go = 0;
			break;

		default:
			printf("generic error\n");
			go = 0;
			break;
		}

		g_free(prompt);
	}

	CERT_DestroyCertificate(cert);

	return status;
#endif
}

static PRFileDesc *
enable_ssl (CamelTcpStreamSSL *ssl, PRFileDesc *fd)
{
	PRFileDesc *ssl_fd;

	ssl_fd = SSL_ImportFD (NULL, fd ? fd : ssl->priv->sockfd);
	if (!ssl_fd)
		return NULL;

	SSL_OptionSet (ssl_fd, SSL_SECURITY, PR_TRUE);

	if (ssl->priv->flags & CAMEL_TCP_STREAM_SSL_ENABLE_SSL2) {
		SSL_OptionSet (ssl_fd, SSL_ENABLE_SSL2, PR_TRUE);
		SSL_OptionSet (ssl_fd, SSL_V2_COMPATIBLE_HELLO, PR_TRUE);
	} else {
		SSL_OptionSet (ssl_fd, SSL_ENABLE_SSL2, PR_FALSE);
		SSL_OptionSet (ssl_fd, SSL_V2_COMPATIBLE_HELLO, PR_FALSE);
	}

	if (ssl->priv->flags & CAMEL_TCP_STREAM_SSL_ENABLE_SSL3)
		SSL_OptionSet (ssl_fd, SSL_ENABLE_SSL3, PR_TRUE);
	else
		SSL_OptionSet (ssl_fd, SSL_ENABLE_SSL3, PR_FALSE);

	if (ssl->priv->flags & CAMEL_TCP_STREAM_SSL_ENABLE_TLS)
		SSL_OptionSet (ssl_fd, SSL_ENABLE_TLS, PR_TRUE);
	else
		SSL_OptionSet (ssl_fd, SSL_ENABLE_TLS, PR_FALSE);

	SSL_SetURL (ssl_fd, ssl->priv->expected_host);

	/* NSS provides a default implementation for the SSL_GetClientAuthDataHook callback
	 * but does not enable it by default. It must be explicltly requested by the application.
	 * See: http://www.mozilla.org/projects/security/pki/nss/ref/ssl/sslfnc.html#1126622 */
	SSL_GetClientAuthDataHook (ssl_fd, (SSLGetClientAuthData)&NSS_GetClientAuthData, NULL );

	/* NSS provides _and_ installs a default implementation for the
	 * SSL_AuthCertificateHook callback so we _don't_ need to install one. */
	SSL_BadCertHook (ssl_fd, ssl_bad_cert, ssl);

	ssl->priv->ssl_mode = TRUE;

	return ssl_fd;
}

static gint
sockaddr_to_praddr(struct sockaddr *s, gint len, PRNetAddr *addr)
{
	/* We assume the ip addresses are the same size - they have to be anyway.
	   We could probably just use memcpy *shrug* */

	memset(addr, 0, sizeof(*addr));

	if (s->sa_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)s;

		if (len < sizeof(*sin))
			return -1;

		addr->inet.family = PR_AF_INET;
		addr->inet.port = sin->sin_port;
		memcpy(&addr->inet.ip, &sin->sin_addr, sizeof(addr->inet.ip));

		return 0;
	}
#ifdef ENABLE_IPv6
	else if (s->sa_family == PR_AF_INET6) {
		struct sockaddr_in6 *sin = (struct sockaddr_in6 *)s;

		if (len < sizeof(*sin))
			return -1;

		addr->ipv6.family = PR_AF_INET6;
		addr->ipv6.port = sin->sin6_port;
		addr->ipv6.flowinfo = sin->sin6_flowinfo;
		memcpy(&addr->ipv6.ip, &sin->sin6_addr, sizeof(addr->ipv6.ip));
		addr->ipv6.scope_id = sin->sin6_scope_id;

		return 0;
	}
#endif

	return -1;
}

static gint
socket_connect(CamelTcpStream *stream, struct addrinfo *host)
{
	CamelTcpStreamSSL *ssl = CAMEL_TCP_STREAM_SSL (stream);
	PRNetAddr netaddr;
	PRFileDesc *fd, *cancel_fd;

	if (sockaddr_to_praddr(host->ai_addr, host->ai_addrlen, &netaddr) != 0) {
		errno = EINVAL;
		return -1;
	}

	fd = PR_OpenTCPSocket(netaddr.raw.family);
	if (fd == NULL) {
		set_errno (PR_GetError ());
		return -1;
	}

	if (ssl->priv->ssl_mode) {
		PRFileDesc *ssl_fd;

		ssl_fd = enable_ssl (ssl, fd);
		if (ssl_fd == NULL) {
			gint errnosave;

			set_errno (PR_GetError ());
			errnosave = errno;
			PR_Shutdown (fd, PR_SHUTDOWN_BOTH);
			PR_Close (fd);
			errno = errnosave;

			return -1;
		}

		fd = ssl_fd;
	}

	cancel_fd = camel_operation_cancel_prfd(NULL);

	if (PR_Connect (fd, &netaddr, cancel_fd?0:CONNECT_TIMEOUT) == PR_FAILURE) {
		gint errnosave;

		set_errno (PR_GetError ());
		if (PR_GetError () == PR_IN_PROGRESS_ERROR ||
		    (cancel_fd && (PR_GetError () == PR_CONNECT_TIMEOUT_ERROR ||
				   PR_GetError () == PR_IO_TIMEOUT_ERROR))) {
			gboolean connected = FALSE;
			PRPollDesc poll[2];

			poll[0].fd = fd;
			poll[0].in_flags = PR_POLL_WRITE | PR_POLL_EXCEPT;
			poll[1].fd = cancel_fd;
			poll[1].in_flags = PR_POLL_READ;

			do {
				poll[0].out_flags = 0;
				poll[1].out_flags = 0;

				if (PR_Poll (poll, cancel_fd?2:1, CONNECT_TIMEOUT) == PR_FAILURE) {
					set_errno (PR_GetError ());
					goto exception;
				}

				if (poll[1].out_flags == PR_POLL_READ) {
					errno = EINTR;
					goto exception;
				}

				if (PR_ConnectContinue(fd, poll[0].out_flags) == PR_FAILURE) {
					set_errno (PR_GetError ());
					if (PR_GetError () != PR_IN_PROGRESS_ERROR)
						goto exception;
				} else {
					connected = TRUE;
				}
			} while (!connected);
		} else {
		exception:
			errnosave = errno;
			PR_Shutdown (fd, PR_SHUTDOWN_BOTH);
			PR_Close (fd);
			ssl->priv->sockfd = NULL;
			errno = errnosave;

			return -1;
		}

		errno = 0;
	}

	ssl->priv->sockfd = fd;

	return 0;
}

static gint
stream_connect (CamelTcpStream *stream,
                struct addrinfo *host,
                GError **error)
{
	while (host) {
		if (socket_connect(stream, host) == 0)
			return 0;
		host = host->ai_next;
	}

	return -1;
}

static gint
stream_getsockopt (CamelTcpStream *stream, CamelSockOptData *data)
{
	PRSocketOptionData sodata;

	memset ((gpointer) &sodata, 0, sizeof (sodata));
	memcpy ((gpointer) &sodata, (gpointer) data, sizeof (CamelSockOptData));

	if (PR_GetSocketOption (((CamelTcpStreamSSL *)stream)->priv->sockfd, &sodata) == PR_FAILURE)
		return -1;

	memcpy ((gpointer) data, (gpointer) &sodata, sizeof (CamelSockOptData));

	return 0;
}

static gint
stream_setsockopt (CamelTcpStream *stream, const CamelSockOptData *data)
{
	PRSocketOptionData sodata;

	memset ((gpointer) &sodata, 0, sizeof (sodata));
	memcpy ((gpointer) &sodata, (gpointer) data, sizeof (CamelSockOptData));

	if (PR_SetSocketOption (((CamelTcpStreamSSL *)stream)->priv->sockfd, &sodata) == PR_FAILURE)
		return -1;

	return 0;
}

static struct sockaddr *
sockaddr_from_praddr(PRNetAddr *addr, socklen_t *len)
{
	/* We assume the ip addresses are the same size - they have to be anyway */

	if (addr->raw.family == PR_AF_INET) {
		struct sockaddr_in *sin = g_malloc0(sizeof(*sin));

		sin->sin_family = AF_INET;
		sin->sin_port = addr->inet.port;
		memcpy(&sin->sin_addr, &addr->inet.ip, sizeof(sin->sin_addr));
		*len = sizeof(*sin);

		return (struct sockaddr *)sin;
	}
#ifdef ENABLE_IPv6
	else if (addr->raw.family == PR_AF_INET6) {
		struct sockaddr_in6 *sin = g_malloc0(sizeof(*sin));

		sin->sin6_family = AF_INET6;
		sin->sin6_port = addr->ipv6.port;
		sin->sin6_flowinfo = addr->ipv6.flowinfo;
		memcpy(&sin->sin6_addr, &addr->ipv6.ip, sizeof(sin->sin6_addr));
		sin->sin6_scope_id = addr->ipv6.scope_id;
		*len = sizeof(*sin);

		return (struct sockaddr *)sin;
	}
#endif

	return NULL;
}

static struct sockaddr *
stream_get_local_address(CamelTcpStream *stream, socklen_t *len)
{
	PRFileDesc *sockfd = CAMEL_TCP_STREAM_SSL (stream)->priv->sockfd;
	PRNetAddr addr;

	if (PR_GetSockName(sockfd, &addr) != PR_SUCCESS)
		return NULL;

	return sockaddr_from_praddr(&addr, len);
}

static struct sockaddr *
stream_get_remote_address (CamelTcpStream *stream, socklen_t *len)
{
	PRFileDesc *sockfd = CAMEL_TCP_STREAM_SSL (stream)->priv->sockfd;
	PRNetAddr addr;

	if (PR_GetPeerName(sockfd, &addr) != PR_SUCCESS)
		return NULL;

	return sockaddr_from_praddr(&addr, len);
}

PRFileDesc *
camel_tcp_stream_ssl_sockfd (CamelTcpStreamSSL *stream)
{
	return stream->priv->sockfd;
}

#endif /* HAVE_NSS */
