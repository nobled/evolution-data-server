/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-service.c : Abstract class for an email service */

/*
 *
 * Author :
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gi18n-lib.h>

#include "camel-operation.h"
#include "camel-private.h"
#include "camel-service.h"
#include "camel-session.h"

#define d(x)
#define w(x)

#define CAMEL_SERVICE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_SERVICE, CamelServicePrivate))

static gpointer parent_class;

static void
service_finalize (GObject *object)
{
	CamelService *service = CAMEL_SERVICE (object);

	if (service->status == CAMEL_SERVICE_CONNECTED)
		CAMEL_SERVICE_GET_CLASS (service)->disconnect (service, TRUE, NULL);

	if (service->url)
		camel_url_free (service->url);

	if (service->session)
		g_object_unref (service->session);

	g_static_rec_mutex_free (&service->priv->connect_lock);
	g_static_mutex_free (&service->priv->connect_op_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gint
service_setv (CamelObject *object,
              GError **error,
              CamelArgV *args)
{
	CamelService *service = (CamelService *) object;
	CamelURL *url = service->url;
	gboolean reconnect = FALSE;
	guint32 tag;
	gint i;

	for (i = 0; i < args->argc; i++) {
		tag = args->argv[i].tag;

		/* make sure this is an arg we're supposed to handle */
		if ((tag & CAMEL_ARG_TAG) <= CAMEL_SERVICE_ARG_FIRST ||
		    (tag & CAMEL_ARG_TAG) >= CAMEL_SERVICE_ARG_FIRST + 100)
			continue;

		if (tag == CAMEL_SERVICE_USERNAME) {
			/* set the username */
			if (strcmp (url->user, args->argv[i].ca_str) != 0) {
				camel_url_set_user (url, args->argv[i].ca_str);
				reconnect = TRUE;
			}
		} else if (tag == CAMEL_SERVICE_AUTH) {
			/* set the auth mechanism */
			if (strcmp (url->authmech, args->argv[i].ca_str) != 0) {
				camel_url_set_authmech (url, args->argv[i].ca_str);
				reconnect = TRUE;
			}
		} else if (tag == CAMEL_SERVICE_HOSTNAME) {
			/* set the hostname */
			if (strcmp (url->host, args->argv[i].ca_str) != 0) {
				camel_url_set_host (url, args->argv[i].ca_str);
				reconnect = TRUE;
			}
		} else if (tag == CAMEL_SERVICE_PORT) {
			/* set the port */
			if (url->port != args->argv[i].ca_int) {
				camel_url_set_port (url, args->argv[i].ca_int);
				reconnect = TRUE;
			}
		} else if (tag == CAMEL_SERVICE_PATH) {
			/* set the path */
			if (strcmp (url->path, args->argv[i].ca_str) != 0) {
				camel_url_set_path (url, args->argv[i].ca_str);
				reconnect = TRUE;
			}
		} else {
			/* error? */
			continue;
		}

		/* let our parent know that we've handled this arg */
		camel_argv_ignore (args, i);
	}

	/* FIXME: what if we are in the process of connecting? */
	if (reconnect && service->status == CAMEL_SERVICE_CONNECTED) {
		/* reconnect the service using the new URL */
		if (camel_service_disconnect (service, TRUE, error))
			camel_service_connect (service, error);
	}

	/* Chain up to parent's setv() method. */
	return CAMEL_OBJECT_CLASS (parent_class)->setv (object, error, args);
}

static gint
service_getv (CamelObject *object,
              GError **error,
              CamelArgGetV *args)
{
	CamelService *service = (CamelService *) object;
	CamelURL *url = service->url;
	guint32 tag;
	gint i;

	for (i = 0; i < args->argc; i++) {
		tag = args->argv[i].tag;

		/* make sure this is an arg we're supposed to handle */
		if ((tag & CAMEL_ARG_TAG) <= CAMEL_SERVICE_ARG_FIRST ||
		    (tag & CAMEL_ARG_TAG) >= CAMEL_SERVICE_ARG_FIRST + 100)
			continue;

		switch (tag) {
		case CAMEL_SERVICE_USERNAME:
			/* get the username */
			*args->argv[i].ca_str = url->user;
			break;
		case CAMEL_SERVICE_AUTH:
			/* get the auth mechanism */
			*args->argv[i].ca_str = url->authmech;
			break;
		case CAMEL_SERVICE_HOSTNAME:
			/* get the hostname */
			*args->argv[i].ca_str = url->host;
			break;
		case CAMEL_SERVICE_PORT:
			/* get the port */
			*args->argv[i].ca_int = url->port;
			break;
		case CAMEL_SERVICE_PATH:
			/* get the path */
			*args->argv[i].ca_str = url->path;
			break;
		default:
			/* error? */
			break;
		}
	}

	/* Chain up to parent's getv() method. */
	return CAMEL_OBJECT_CLASS (parent_class)->getv (object, error, args);
}

static gboolean
service_construct (CamelService *service,
                   CamelSession *session,
                   CamelProvider *provider,
                   CamelURL *url,
                   GError **error)
{
	gchar *err, *url_string;

	if (CAMEL_PROVIDER_NEEDS (provider, CAMEL_URL_PART_USER) &&
	    (url->user == NULL || url->user[0] == '\0')) {
		err = _("URL '%s' needs a username component");
		goto fail;
	} else if (CAMEL_PROVIDER_NEEDS (provider, CAMEL_URL_PART_HOST) &&
		   (url->host == NULL || url->host[0] == '\0')) {
		err = _("URL '%s' needs a host component");
		goto fail;
	} else if (CAMEL_PROVIDER_NEEDS (provider, CAMEL_URL_PART_PATH) &&
		   (url->path == NULL || url->path[0] == '\0')) {
		err = _("URL '%s' needs a path component");
		goto fail;
	}

	service->provider = provider;
	service->url = camel_url_copy(url);
	service->session = session;
	g_object_ref (session);

	service->status = CAMEL_SERVICE_DISCONNECTED;

	return TRUE;

fail:
	url_string = camel_url_to_string(url, CAMEL_URL_HIDE_PASSWORD);
	g_set_error (
		error, CAMEL_SERVICE_ERROR,
		CAMEL_SERVICE_ERROR_URL_INVALID,
		err, url_string);
	g_free(url_string);

	return FALSE;
}

static gboolean
service_connect (CamelService *service,
                 GError **error)
{
	/* Things like the CamelMboxStore can validly
	 * not define a connect function. */
	 return TRUE;
}

static gboolean
service_disconnect (CamelService *service,
                    gboolean clean,
                    GError **error)
{
	/* We let people get away with not having a disconnect
	 * function -- CamelMboxStore, for example. */
	return TRUE;
}

static void
service_cancel_connect (CamelService *service)
{
	camel_operation_cancel (service->connect_op);
}

static GList *
service_query_auth_types (CamelService *service,
                          GError **error)
{
	return NULL;
}

static gchar *
service_get_name (CamelService *service,
                  gboolean brief)
{
	g_warning (
		"%s does not implement CamelServiceClass::get_name()",
		G_OBJECT_TYPE_NAME (service));

	return g_strdup (G_OBJECT_TYPE_NAME (service));
}

static gchar *
service_get_path (CamelService *service)
{
	CamelProvider *prov = service->provider;
	CamelURL *url = service->url;
	GString *gpath;
	gchar *path;

	/* A sort of ad-hoc default implementation that works for our
	 * current set of services.
	 */

	gpath = g_string_new (service->provider->protocol);
	if (CAMEL_PROVIDER_ALLOWS (prov, CAMEL_URL_PART_USER)) {
		if (CAMEL_PROVIDER_ALLOWS (prov, CAMEL_URL_PART_HOST)) {
			g_string_append_printf (gpath, "/%s@%s",
						url->user ? url->user : "",
						url->host ? url->host : "");

			if (url->port)
				g_string_append_printf (gpath, ":%d", url->port);
		} else {
			g_string_append_printf (gpath, "/%s%s", url->user ? url->user : "",
						CAMEL_PROVIDER_NEEDS (prov, CAMEL_URL_PART_USER) ? "" : "@");
		}
	} else if (CAMEL_PROVIDER_ALLOWS (prov, CAMEL_URL_PART_HOST)) {
		g_string_append_printf (gpath, "/%s%s",
					CAMEL_PROVIDER_NEEDS (prov, CAMEL_URL_PART_HOST) ? "" : "@",
					url->host ? url->host : "");

		if (url->port)
			g_string_append_printf (gpath, ":%d", url->port);
	}

	if (CAMEL_PROVIDER_NEEDS (prov, CAMEL_URL_PART_PATH))
		g_string_append_printf (gpath, "%s%s", *url->path == '/' ? "" : "/", url->path);

	path = gpath->str;
	g_string_free (gpath, FALSE);

	return path;
}

static void
service_class_init (CamelServiceClass *class)
{
	GObjectClass *object_class;
	CamelObjectClass *camel_object_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (CamelServicePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = service_finalize;

	camel_object_class = CAMEL_OBJECT_CLASS (class);
	camel_object_class->setv = service_setv;
	camel_object_class->getv = service_getv;

	class->construct = service_construct;
	class->connect = service_connect;
	class->disconnect = service_disconnect;
	class->cancel_connect = service_cancel_connect;
	class->query_auth_types = service_query_auth_types;
	class->get_name = service_get_name;
	class->get_path = service_get_path;
}

static void
service_init (CamelService *service)
{
	service->priv = CAMEL_SERVICE_GET_PRIVATE (service);

	g_static_rec_mutex_init (&service->priv->connect_lock);
	g_static_mutex_init (&service->priv->connect_op_lock);
}

GType
camel_service_get_type (void)
{
	static GType type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID))
		type = g_type_register_static_simple (
			CAMEL_TYPE_OBJECT,
			"CamelService",
			sizeof (CamelServiceClass),
			(GClassInitFunc) service_class_init,
			sizeof (CamelService),
			(GInstanceInitFunc) service_init,
			0);

	return type;
}

GQuark
camel_service_error_quark (void)
{
	static GQuark quark = 0;

	if (G_UNLIKELY (quark == 0)) {
		const gchar *string = "camel-service-error-quark";
		quark = g_quark_from_static_string (string);
	}

	return quark;
}

/**
 * camel_service_construct:
 * @service: a #CamelService object
 * @session: the #CamelSession for @service
 * @provider: the #CamelProvider associated with @service
 * @url: the default URL for the service (may be %NULL)
 * @error: return location for a #GError, or %NULL
 *
 * Constructs a #CamelService initialized with the given parameters.
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
camel_service_construct (CamelService *service,
                         CamelSession *session,
                         CamelProvider *provider,
                         CamelURL *url,
                         GError **error)
{
	CamelServiceClass *class;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), FALSE);
	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_val_if_fail (class->construct != NULL, FALSE);

	return class->construct (service, session, provider, url, error);
}

/**
 * camel_service_connect:
 * @service: a #CamelService object
 * @error: return location for a #GError, or %NULL
 *
 * Connect to the service using the parameters it was initialized
 * with.
 *
 * Returns: %TRUE if the connection is made or %FALSE otherwise
 **/
gboolean
camel_service_connect (CamelService *service,
                       GError **error)
{
	CamelServiceClass *class;
	gboolean ret = FALSE;
	gboolean unreg = FALSE;
	CamelOperation *connect_op;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), FALSE);
	g_return_val_if_fail (service->session != NULL, FALSE);
	g_return_val_if_fail (service->url != NULL, FALSE);

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_val_if_fail (class->connect != NULL, FALSE);

	CAMEL_SERVICE_REC_LOCK (service, connect_lock);

	if (service->status == CAMEL_SERVICE_CONNECTED) {
		CAMEL_SERVICE_REC_UNLOCK (service, connect_lock);
		return TRUE;
	}

	/* Register a separate operation for connecting, so that
	 * the offline code can cancel it. */
	CAMEL_SERVICE_LOCK (service, connect_op_lock);
	service->connect_op = camel_operation_registered ();
	if (!service->connect_op) {
		service->connect_op = camel_operation_new (NULL, NULL);
		camel_operation_register (service->connect_op);
		unreg = TRUE;
	}
	connect_op = service->connect_op;
	CAMEL_SERVICE_UNLOCK (service, connect_op_lock);

	service->status = CAMEL_SERVICE_CONNECTING;
	ret = class->connect (service, error);
	service->status = ret ? CAMEL_SERVICE_CONNECTED : CAMEL_SERVICE_DISCONNECTED;

	CAMEL_SERVICE_LOCK (service, connect_op_lock);
	if (connect_op) {
		if (unreg && service->connect_op)
			camel_operation_unregister (connect_op);

		camel_operation_unref (connect_op);
		service->connect_op = NULL;
	}
	CAMEL_SERVICE_UNLOCK (service, connect_op_lock);

	CAMEL_SERVICE_REC_UNLOCK (service, connect_lock);

	return ret;
}

/**
 * camel_service_disconnect:
 * @service: a #CamelService object
 * @clean: whether or not to try to disconnect cleanly
 * @error: return location for a #GError, or %NULL
 *
 * Disconnect from the service. If @clean is %FALSE, it should not
 * try to do any synchronizing or other cleanup of the connection.
 *
 * Returns: %TRUE if the disconnect was successful or %FALSE otherwise
 **/
gboolean
camel_service_disconnect (CamelService *service,
                          gboolean clean,
                          GError **error)
{
	CamelServiceClass *class;
	gboolean res = TRUE;
	gint unreg = FALSE;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), FALSE);

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_val_if_fail (class->disconnect != NULL, FALSE);

	CAMEL_SERVICE_REC_LOCK (service, connect_lock);

	if (service->status != CAMEL_SERVICE_DISCONNECTED
	    && service->status != CAMEL_SERVICE_DISCONNECTING) {
		CAMEL_SERVICE_LOCK (service, connect_op_lock);
		service->connect_op = camel_operation_registered ();
		if (!service->connect_op) {
			service->connect_op = camel_operation_new (NULL, NULL);
			camel_operation_register (service->connect_op);
			unreg = TRUE;
		}
		CAMEL_SERVICE_UNLOCK (service, connect_op_lock);

		service->status = CAMEL_SERVICE_DISCONNECTING;
		res = class->disconnect (service, clean, error);
		service->status = CAMEL_SERVICE_DISCONNECTED;

		CAMEL_SERVICE_LOCK (service, connect_op_lock);
		if (unreg)
			camel_operation_unregister (service->connect_op);

		camel_operation_unref (service->connect_op);
		service->connect_op = NULL;
		CAMEL_SERVICE_UNLOCK (service, connect_op_lock);
	}

	CAMEL_SERVICE_REC_UNLOCK (service, connect_lock);

		service->status = CAMEL_SERVICE_DISCONNECTED;
	return res;
}

/**
 * camel_service_cancel_connect:
 * @service: a #CamelService object
 *
 * If @service is currently attempting to connect to or disconnect
 * from a server, this causes it to stop and fail. Otherwise it is a
 * no-op.
 **/
void
camel_service_cancel_connect (CamelService *service)
{
	CamelServiceClass *class;

	g_return_if_fail (CAMEL_IS_SERVICE (service));

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_if_fail (class->cancel_connect != NULL);

	CAMEL_SERVICE_LOCK (service, connect_op_lock);
	if (service->connect_op)
		class->cancel_connect (service);
	CAMEL_SERVICE_UNLOCK (service, connect_op_lock);
}

/**
 * camel_service_get_url:
 * @service: a #CamelService object
 *
 * Gets the URL representing @service. The returned URL must be
 * freed when it is no longer needed. For security reasons, this
 * routine does not return the password.
 *
 * Returns: the URL representing @service
 **/
gchar *
camel_service_get_url (CamelService *service)
{
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	return camel_url_to_string (service->url, CAMEL_URL_HIDE_PASSWORD);
}

/**
 * camel_service_get_name:
 * @service: a #CamelService object
 * @brief: whether or not to use a briefer form
 *
 * This gets the name of the service in a "friendly" (suitable for
 * humans) form. If @brief is %TRUE, this should be a brief description
 * such as for use in the folder tree. If @brief is %FALSE, it should
 * be a more complete and mostly unambiguous description.
 *
 * Returns: a description of the service which the caller must free
 **/
gchar *
camel_service_get_name (CamelService *service,
                        gboolean brief)
{
	CamelServiceClass *class;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);
	g_return_val_if_fail (service->url, NULL);

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_val_if_fail (class->get_name != NULL, NULL);

	return class->get_name (service, brief);
}

/**
 * camel_service_get_path:
 * @service: a #CamelService object
 *
 * This gets a valid UNIX relative path describing @service, which
 * is guaranteed to be different from the path returned for any
 * different service. This path MUST start with the name of the
 * provider, followed by a "/", but after that, it is up to the
 * provider.
 *
 * Returns: the path, which the caller must free
 **/
gchar *
camel_service_get_path (CamelService *service)
{
	CamelServiceClass *class;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);
	g_return_val_if_fail (service->url, NULL);

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_val_if_fail (class->get_path != NULL, NULL);

	return class->get_path (service);
}

/**
 * camel_service_get_session:
 * @service: a #CamelService object
 *
 * Gets the #CamelSession associated with the service.
 *
 * Returns: the session
 **/
CamelSession *
camel_service_get_session (CamelService *service)
{
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	return service->session;
}

/**
 * camel_service_get_provider:
 * @service: a #CamelService object
 *
 * Gets the #CamelProvider associated with the service.
 *
 * Returns: the provider
 **/
CamelProvider *
camel_service_get_provider (CamelService *service)
{
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	return service->provider;
}

/**
 * camel_service_query_auth_types:
 * @service: a #CamelService object
 * @error: return location for a #GError, or %NULL
 *
 * This is used by the mail source wizard to get the list of
 * authentication types supported by the protocol, and information
 * about them.
 *
 * Returns: a list of #CamelServiceAuthType records. The caller
 * must free the list with #g_list_free when it is done with it.
 **/
GList *
camel_service_query_auth_types (CamelService *service,
                                GError **error)
{
	CamelServiceClass *class;
	GList *ret;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	class = CAMEL_SERVICE_GET_CLASS (service);
	g_return_val_if_fail (class->query_auth_types != NULL, NULL);

	/* Note that we get the connect lock here, which means the
	 * callee must not call the connect functions itself. */
	CAMEL_SERVICE_REC_LOCK (service, connect_lock);
	ret = class->query_auth_types (service, error);
	CAMEL_SERVICE_REC_UNLOCK (service, connect_lock);

	return ret;
}
