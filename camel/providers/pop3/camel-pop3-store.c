/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-pop3-store.c : class for a pop3 store */

/* 
 * Authors:
 *   Dan Winship <danw@helixcode.com>
 *
 * Copyright (C) 2000 Helix Code, Inc. (www.helixcode.com)
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#define d(x)

#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "camel-pop3-store.h"
#include "camel-pop3-folder.h"
#include "camel-stream-buffer.h"
#include "camel-stream-fs.h"
#include "camel-session.h"
#include "camel-exception.h"
#include "camel-url.h"
#include "md5-utils.h"

/* Specified in RFC 1939 */
#define POP3_PORT 110

#ifdef HAVE_KRB4
/* Specified nowhere */
#define KPOP_PORT 1109

#include <krb.h>
#endif

static CamelServiceClass *service_class = NULL;

static void finalize (CamelObject *object);

static gboolean pop3_connect (CamelService *service, CamelException *ex);
static gboolean pop3_disconnect (CamelService *service, CamelException *ex);
static GList *query_auth_types (CamelService *service, CamelException *ex);
static void free_auth_types (CamelService *service, GList *authtypes);
static char *get_name (CamelService *service, gboolean brief);

static CamelFolder *get_folder (CamelStore *store, const char *folder_name, 
				gboolean create, CamelException *ex);
static char *get_folder_name (CamelStore *store, const char *folder_name, 
			      CamelException *ex);
static char *get_root_folder_name (CamelStore *store, CamelException *ex);

static int pop3_get_response (CamelPop3Store *store, char **ret);


static void
camel_pop3_store_class_init (CamelPop3StoreClass *camel_pop3_store_class)
{
	CamelServiceClass *camel_service_class =
		CAMEL_SERVICE_CLASS (camel_pop3_store_class);
	CamelStoreClass *camel_store_class =
		CAMEL_STORE_CLASS (camel_pop3_store_class);
	
	service_class = CAMEL_SERVICE_CLASS(camel_type_get_global_classfuncs (camel_service_get_type ()));

	/* virtual method overload */
	camel_service_class->connect = pop3_connect;
	camel_service_class->disconnect = pop3_disconnect;
	camel_service_class->query_auth_types = query_auth_types;
	camel_service_class->free_auth_types = free_auth_types;
	camel_service_class->get_name = get_name;

	camel_store_class->get_folder = get_folder;
	camel_store_class->get_folder_name = get_folder_name;
	camel_store_class->get_root_folder_name = get_root_folder_name;
}



static void
camel_pop3_store_init (gpointer object, gpointer klass)
{
	CamelService *service = CAMEL_SERVICE (object);

	service->url_flags = (CAMEL_SERVICE_URL_NEED_USER | CAMEL_SERVICE_URL_NEED_HOST);
}

CamelType
camel_pop3_store_get_type (void)
{
	static CamelType camel_pop3_store_type = CAMEL_INVALID_TYPE;

	if (!camel_pop3_store_type) {
		camel_pop3_store_type = camel_type_register (CAMEL_STORE_TYPE, "CamelPop3Store",
							     sizeof (CamelPop3Store),
							     sizeof (CamelPop3StoreClass),
							     (CamelObjectClassInitFunc) camel_pop3_store_class_init,
							     NULL,
							     (CamelObjectInitFunc) camel_pop3_store_init,
							     finalize);
	}

	return camel_pop3_store_type;
}

static void
finalize (CamelObject *object)
{
	CamelPop3Store *pop3_store = CAMEL_POP3_STORE (object);
	CamelException ex;

	camel_exception_init (&ex);
	pop3_disconnect (CAMEL_SERVICE (object), &ex);
	camel_exception_clear (&ex);

	if (pop3_store->apop_timestamp)
		g_free (pop3_store->apop_timestamp);
}

static CamelServiceAuthType password_authtype = {
	"Password",

	"This option will connect to the POP server using a plaintext "
	"password. This is the only option supported by many POP servers.",

	"",
	TRUE
};

static CamelServiceAuthType apop_authtype = {
	"APOP",

	"This option will connect to the POP server using an encrypted "
	"password via the APOP protocol. This may not work for all users "
	"even on servers that claim to support it.",

	"+APOP",
	TRUE
};

#ifdef HAVE_KRB4
static CamelServiceAuthType kpop_authtype = {
	"Kerberos 4 (KPOP)",

	"This will connect to the POP server and use Kerberos 4 "
	"to authenticate to it.",

	"+KPOP",
	FALSE
};
#endif

static gboolean
connect_to_server (CamelService *service, gboolean real, CamelException *ex)
{
	CamelPop3Store *store = CAMEL_POP3_STORE (service);
	struct hostent *h;
	struct sockaddr_in sin;
	int fd, status;
	char *buf, *apoptime, *apopend;
#ifdef HAVE_KRB4
	gboolean kpop = (service->url->port == KPOP_PORT);
#endif

	h = camel_service_gethost (service, ex);
	if (!h)
		return FALSE;

	sin.sin_family = h->h_addrtype;
	if (service->url->port)
		sin.sin_port = htons (service->url->port);
	else
		sin.sin_port = htons (POP3_PORT);
	memcpy (&sin.sin_addr, h->h_addr, sizeof (sin.sin_addr));

	fd = socket (h->h_addrtype, SOCK_STREAM, 0);
	if (fd == -1 ||
	    connect (fd, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
		if (real) {
			camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
					      "Could not connect to %s: %s",
					      h->h_name, g_strerror(errno));
		}
		if (fd > -1)
			close (fd);
		return FALSE;
	}

#ifdef HAVE_KRB4
	if (kpop) {
		KTEXT_ST ticket_st;
		MSG_DAT msg_data;
		CREDENTIALS cred;
		Key_schedule schedule;
		char *hostname;

		/* Need to copy hostname, because krb_realmofhost will
		 * call gethostbyname as well, and gethostbyname uses
		 * static storage.
		 */
		hostname = g_strdup (h->h_name);
		status = krb_sendauth (0, fd, &ticket_st, "pop", hostname,
				       krb_realmofhost (hostname), 0,
				       &msg_data, &cred, schedule,
				       NULL, NULL, "KPOPV0.1");
		g_free (hostname);
		if (status != KSUCCESS) {
			if (real) {
				camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
						      "Could not authenticate "
						      "to KPOP server: %s",
						      krb_err_txt[status]);
			}
			close (fd);
			return FALSE;
		}

		if (!service->url->passwd)
			service->url->passwd = g_strdup (service->url->user);
	}
#endif /* HAVE_KRB4 */

	store->ostream = camel_stream_fs_new_with_fd (fd);
	store->istream = camel_stream_buffer_new (store->ostream,
						  CAMEL_STREAM_BUFFER_READ);

	/* Read the greeting, check status */
	status = pop3_get_response (store, &buf);
	if (status != CAMEL_POP3_OK) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      "%s: %s", status == CAMEL_POP3_ERR ?
				      "Error connecting to POP server" :
				      "Error reading greeting from POP server",
				      buf);
		g_free (buf);
		pop3_disconnect (service, ex);
		return FALSE;
	}

	apoptime = strchr (buf, '<');
	apopend = apoptime ? strchr (apoptime, '>') : NULL;
	if (apopend) {
		store->apop_timestamp = g_strndup (apoptime,
						   apopend - apoptime + 1);
		memmove (apoptime, apopend + 1, strlen (apopend + 1));
	}
	store->implementation = buf;

	/* Check extensions */
	store->login_delay = -1;
	store->supports_top = -1;
	store->supports_uidl = -1;
	store->expires = -1;

	status = camel_pop3_command (store, NULL, "CAPA");
	if (status == CAMEL_POP3_OK) {
		char *p;
		int len;

		buf = camel_pop3_command_get_additional_data (store, ex);
		if (camel_exception_is_set (ex)) {
			pop3_disconnect (service, ex);
			return FALSE;
		}

		p = buf;
		while (*p) {
			len = strcspn (p, "\n");
			if (!strncmp (p, "IMPLEMENTATION ", 15)) {
				g_free (store->implementation);
				store->implementation =
					g_strndup (p + 15, len - 15);
			} else if (len == 3 && !strncmp (p, "TOP", 3))
				store->supports_top = TRUE;
			else if (len == 4 && !strncmp (p, "UIDL", 4))
				store->supports_uidl = TRUE;
			else if (!strncmp (p, "LOGIN-DELAY ", 12))
				store->login_delay = atoi (p + 12);
			else if (!strncmp (p, "EXPIRE NEVER", 12))
				store->expires = FALSE;
			else if (!strncmp (p, "EXPIRE ", 7))
				store->expires = TRUE;

			p += len;
			if (*p)
				p++;
		}

		g_free (buf);
	}

	return TRUE;
}

static GList *
query_auth_types (CamelService *service, CamelException *ex)
{
	CamelPop3Store *store = CAMEL_POP3_STORE (service);
	GList *ret = NULL;
	gboolean passwd = TRUE, apop = TRUE;
#ifdef HAVE_KRB4
	gboolean kpop = TRUE;
	int saved_port;
#endif

	if (service->url && !service->url->empty) {
		passwd = connect_to_server (service, FALSE, ex);
		if (camel_exception_get_id (ex) != CAMEL_EXCEPTION_NONE)
			return NULL;
		apop = store->apop_timestamp != NULL;
		if (passwd)
			pop3_disconnect (service, ex);
#ifdef HAVE_KRB4
		saved_port = service->url->port;
		service->url->port = KPOP_PORT;
		kpop = connect_to_server (service, FALSE, ex);
		service->url->port = saved_port;
		if (camel_exception_get_id (ex) != CAMEL_EXCEPTION_NONE)
			return NULL;
		if (kpop)
			pop3_disconnect (service, ex);
#endif
	}

	if (passwd)
		ret = g_list_append (ret, &password_authtype);
	if (apop)
		ret = g_list_append (ret, &apop_authtype);
#ifdef HAVE_KRB4
	if (kpop)
		ret = g_list_append (ret, &kpop_authtype);
#endif

	if (!ret) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      "Could not connect to POP server on "
				      "%s.", service->url->host);
	}				      

	return ret;
}

static void
free_auth_types (CamelService *service, GList *authtypes)
{
	g_list_free (authtypes);
}

static char *
get_name (CamelService *service, gboolean brief)
{
	if (brief)
		return g_strdup_printf ("POP server %s", service->url->host);
	else {
		return g_strdup_printf ("POP service for %s on %s",
					service->url->user,
					service->url->host);
	}
}


/**
 * camel_pop3_store_expunge:
 * @store: the store
 * @ex: a CamelException
 *
 * Expunge messages from the store. This will result in the connection
 * being closed, which may cause later commands to fail if they can't
 * reconnect.
 **/
void
camel_pop3_store_expunge (CamelPop3Store *store, CamelException *ex)
{
	camel_pop3_command (store, NULL, "QUIT");
	pop3_disconnect (CAMEL_SERVICE (store), ex);
}

static gboolean
pop3_connect (CamelService *service, CamelException *ex)
{
	CamelPop3Store *store = CAMEL_POP3_STORE (service);
	int status;
	char *msg, *errbuf = NULL;
	gboolean authenticated = FALSE;
	gboolean auth_supported = TRUE;
	gboolean kpop = FALSE;

#ifdef HAVE_KRB4
	kpop = (service->url->authmech &&
		!strcmp (service->url->authmech, "+KPOP"));

	if (kpop && service->url->port == 0)
		service->url->port = KPOP_PORT;
#endif

	d(printf ("POP3: Connecting to %s\n", service->url->host));
	if (!connect_to_server (service, TRUE, ex))
		return FALSE;

	while (auth_supported && !authenticated) {
		if (errbuf) {
			/* We need to un-cache the password before prompting again */
			camel_session_query_authenticator (camel_service_get_session (service),
							   CAMEL_AUTHENTICATOR_TELL, NULL,
							   TRUE, service, "password", ex);
			g_free (service->url->passwd);
			service->url->passwd = NULL;
		}
		
		/* The KPOP code will have set the password to be the username
		 * in connect_to_server. Password and APOP are the only other
		 * cases, and they both need a password.
		 */
		if (!service->url->passwd) {
			char *prompt;

			prompt = g_strdup_printf ("%sPlease enter the POP3 password for %s@%s",
						  errbuf ? errbuf : "",
						  service->url->user,
						  service->url->host);
			g_free (errbuf);
			errbuf = NULL;
			service->url->passwd = camel_session_query_authenticator (
				camel_service_get_session (service),
				CAMEL_AUTHENTICATOR_ASK, prompt, TRUE,
				service, "password", ex);
			
			g_free (prompt);
			if (!service->url->passwd) {
				pop3_disconnect (service, ex);
				return FALSE;
			}
		}

		if (!service->url->authmech || kpop) {
			status = camel_pop3_command (store, &msg, "USER %s", service->url->user);
			if (status != CAMEL_POP3_OK) {
				if (kpop)
					goto lose;

				errbuf = g_strdup_printf ("Unable to connect to POP server.\n"
							  "Error sending username: %s\n\n",
							  msg ? msg : "(Unknown)");

				g_free (msg);
				continue;
			}
			g_free (msg);

			status = camel_pop3_command (store, &msg, "PASS %s", service->url->passwd);
		} else if (!strcmp (service->url->authmech, "+APOP")
			   && store->apop_timestamp) {
			char *secret, md5asc[33], *d;
			unsigned char md5sum[16], *s;
			
			secret = g_strdup_printf ("%s%s", store->apop_timestamp, service->url->passwd);
			md5_get_digest (secret, strlen (secret), md5sum);
			g_free (secret);
			
			for (s = md5sum, d = md5asc; d < md5asc + 32; s++, d += 2)
				sprintf (d, "%.2x", *s);
			
			status = camel_pop3_command (store, &msg, "APOP %s %s", service->url->user, md5asc);
		} else {
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE,
					     "No support for requested authentication mechanism.");
			
			auth_supported = FALSE;
			goto lose;
		}
		
		if (status != CAMEL_POP3_OK) {
			errbuf = g_strdup_printf ("Unable to connect to POP server.\n"
						  "Error sending password: %s\n\n",
						  msg ? msg : "(Unknown)");

			g_free (msg);
			continue;
		}

		g_free (msg);
		authenticated = TRUE;
	}

	g_free (errbuf);

	service_class->connect (service, ex);
	return TRUE;

 lose:
	/* Uncache the password. */
	camel_session_query_authenticator (camel_service_get_session (service),
					   CAMEL_AUTHENTICATOR_TELL, NULL,
					   TRUE, service, "password", ex);

	if (auth_supported && !authenticated) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE,
				      "Unable to authenticate to POP server: %s",
				      msg ? msg : "(Unknown)");
	}

	g_free (errbuf);
	
	pop3_disconnect (service, ex);
	return FALSE;
}

static gboolean
pop3_disconnect (CamelService *service, CamelException *ex)
{
	CamelPop3Store *store = CAMEL_POP3_STORE (service);

	if (!service_class->disconnect (service, ex))
		return FALSE;

	d(printf ("POP3: Disconnecting from %s\n", service->url->host));

	if (store->ostream) {
		camel_object_unref (CAMEL_OBJECT (store->ostream));
		store->ostream = NULL;
	}
	if (store->istream) {
		camel_object_unref (CAMEL_OBJECT (store->istream));
		store->istream = NULL;
	}

	if (store->apop_timestamp) {
		g_free (store->apop_timestamp);
		store->apop_timestamp = NULL;
	}

	return TRUE;
}

static CamelFolder *
get_folder (CamelStore *store, const char *folder_name,
	    gboolean create, CamelException *ex)
{
	CamelService *service = CAMEL_SERVICE (store);

	if (!camel_service_is_connected (service)) {
		if (!camel_service_connect (service, ex))
			return NULL;
	}
	return camel_pop3_folder_new (store, ex);
}

static char *
get_folder_name (CamelStore *store, const char *folder_name,
		 CamelException *ex)
{
	if (!g_strcasecmp (folder_name, "inbox"))
		return g_strdup ("inbox");
	else {
		camel_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_INVALID,
				      "No such folder `%s'.", folder_name);
		return NULL;
	}
}

static char *
get_root_folder_name (CamelStore *store, CamelException *ex)
{
	return g_strdup ("inbox");
}


/**
 * camel_pop3_command: Send a command to a POP3 server.
 * @store: the POP3 store
 * @ret: a pointer to return the full server response in
 * @fmt: a printf-style format string, followed by arguments
 *
 * This command sends the command specified by @fmt and the following
 * arguments to the connected POP3 store specified by @store. It then
 * reads the server's response and parses out the status code. If
 * the caller passed a non-NULL pointer for @ret, camel_pop3_command
 * will set it to point to an buffer containing the rest of the
 * response from the POP3 server. (If @ret was passed but there was
 * no extended response, @ret will be set to NULL.) The caller must
 * free this buffer when it is done with it.
 *
 * Return value: one of CAMEL_POP3_OK (command executed successfully),
 * CAMEL_POP3_ERR (command encounted an error), or CAMEL_POP3_FAIL
 * (a protocol-level error occurred, and Camel is uncertain of the
 * result of the command.)
 **/
int
camel_pop3_command (CamelPop3Store *store, char **ret, char *fmt, ...)
{
	char *cmdbuf;
	va_list ap;

	if (!store->ostream) {
		CamelException ex;

		camel_exception_init (&ex);
		if (!camel_service_connect (CAMEL_SERVICE (store), &ex)) {
			if (ret)
				*ret = g_strdup (camel_exception_get_description (&ex));
			camel_exception_clear (&ex);
			return CAMEL_POP3_FAIL;
		}
	}

	va_start (ap, fmt);
	cmdbuf = g_strdup_vprintf (fmt, ap);
	va_end (ap);

#if d(!)0
	if (!strncmp (cmdbuf, "PASS", 4))
		printf ("POP3: >>> PASS xxx\n");
	else
		printf ("POP3: >>> %s\n", cmdbuf);
#endif

	/* Send the command */
	if (camel_stream_printf (store->ostream, "%s\r\n", cmdbuf) == -1) {
		g_free (cmdbuf);
		if (*ret)
			*ret = g_strdup (g_strerror (errno));
		d(printf ("POP3: !!! %s\n", g_strerror (errno)));
		return CAMEL_POP3_FAIL;
	}
	g_free (cmdbuf);

	return pop3_get_response (store, ret);
}

static int
pop3_get_response (CamelPop3Store *store, char **ret)
{
	char *respbuf;
	int status;

	respbuf = camel_stream_buffer_read_line (
		CAMEL_STREAM_BUFFER (store->istream));
	if (respbuf == NULL) {
		if (ret)
			*ret = g_strdup (g_strerror (errno));
		d(printf ("POP3: !!! %s\n", g_strerror (errno)));
		return CAMEL_POP3_FAIL;
	}
	d(printf ("POP3: <<< %s\n", respbuf));

	if (!strncmp (respbuf, "+OK", 3))
		status = CAMEL_POP3_OK;
	else if (!strncmp (respbuf, "-ERR", 4))
		status = CAMEL_POP3_ERR;
	else
		status = CAMEL_POP3_FAIL;

	if (ret) {
		if (status != CAMEL_POP3_FAIL) {
			*ret = strchr (respbuf, ' ');
			if (*ret)
				*ret = g_strdup (*ret + 1);
		} else
			*ret = NULL;
	}
	g_free (respbuf);

	return status;
}

/**
 * camel_pop3_command_get_additional_data: get "additional data" from
 * a POP3 command.
 * @store: the POP3 store
 *
 * This command gets the additional data returned by "multi-line" POP
 * commands, such as LIST, RETR, TOP, and UIDL. This command _must_
 * be called after a successful (CAMEL_POP3_OK) call to
 * camel_pop3_command for a command that has a multi-line response.
 * The returned data is un-byte-stuffed, and has lines termined by
 * newlines rather than CR/LF pairs.
 *
 * Return value: the data, which the caller must free.
 **/
char *
camel_pop3_command_get_additional_data (CamelPop3Store *store, CamelException *ex)
{
	CamelStreamBuffer *stream = CAMEL_STREAM_BUFFER (store->istream);
	GPtrArray *data;
	char *buf, *p;
	int i, len = 0, status = CAMEL_POP3_OK;

	data = g_ptr_array_new ();
	while (1) {
		buf = camel_stream_buffer_read_line (stream);
		if (!buf) {
			status = CAMEL_POP3_FAIL;
			break;
		}

		if (!strcmp (buf, "."))
			break;

		g_ptr_array_add (data, buf);
		len += strlen (buf) + 1;
	}
	g_free (buf);

	if (status == CAMEL_POP3_OK) {
		buf = g_malloc0 (len + 1);

		for (i = 0, p = buf; i < data->len; i++) {
			char *ptr, *datap;

			datap = (char *) data->pdata[i];
			ptr = (*datap == '.') ? datap + 1 : datap;
			len = strlen (ptr);
#if d(!)0
			if (i == data->len - 1)
				printf ("POP3: <<<<<< %s\n", ptr);
			else if (i == 0)
				printf ("POP3: <<<<<< %s...\n", ptr);
#endif
			memcpy (p, ptr, len);
			p += len;
			*p++ = '\n';
		}
		*p = '\0';
	} else
		buf = NULL;

	for (i = 0; i < data->len; i++)
		g_free (data->pdata[i]);
	g_ptr_array_free (data, TRUE);

	return buf;
}
