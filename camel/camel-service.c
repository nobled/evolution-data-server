/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-service.c : Abstract class for an email service */

/*
 *
 * Author :
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright 1999, 2000 Helix Code, Inc. (http://www.helixcode.com)
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
#include <config.h>
#include "camel-service.h"
#include "camel-session.h"
#include "camel-exception.h"

#include <ctype.h>
#include <stdlib.h>

#include "camel-private.h"

static CamelObjectClass *parent_class = NULL;

/* Returns the class for a CamelService */
#define CSERV_CLASS(so) CAMEL_SERVICE_CLASS (CAMEL_OBJECT_GET_CLASS(so))

static gboolean service_connect(CamelService *service, CamelException *ex);
static gboolean service_disconnect(CamelService *service, gboolean clean,
				   CamelException *ex);
/*static gboolean is_connected (CamelService *service);*/
static GList *  query_auth_types_func (CamelService *service, CamelException *ex);
static void     free_auth_types (CamelService *service, GList *authtypes);
static char *   get_name (CamelService *service, gboolean brief);
static char *   get_path (CamelService *service);
static gboolean check_url (CamelService *service, CamelException *ex);


static void
camel_service_class_init (CamelServiceClass *camel_service_class)
{
	parent_class = camel_type_get_global_classfuncs (CAMEL_OBJECT_TYPE);

	/* virtual method definition */
	camel_service_class->connect = service_connect;
	camel_service_class->disconnect = service_disconnect;
	/*camel_service_class->is_connected = is_connected;*/
	camel_service_class->query_auth_types_connected = query_auth_types_func;
	camel_service_class->query_auth_types_generic = query_auth_types_func;
	camel_service_class->free_auth_types = free_auth_types;
	camel_service_class->get_name = get_name;
	camel_service_class->get_path = get_path;
}

static void
camel_service_init (void *o, void *k)
{
	CamelService *service = o;

	service->priv = g_malloc0(sizeof(*service->priv));
#ifdef ENABLE_THREADS
	service->priv->connect_lock = e_mutex_new(E_MUTEX_REC);
#endif
}

static void
camel_service_finalize (CamelObject *object)
{
	CamelService *camel_service = CAMEL_SERVICE (object);

	if (camel_service->connected) {
		CamelException ex;

		/*g_warning ("camel_service_finalize: finalizing while still connected!");*/
		camel_exception_init (&ex);
		CSERV_CLASS (camel_service)->disconnect (camel_service, FALSE, &ex);
		if (camel_exception_is_set (&ex)) {
			g_warning ("camel_service_finalize: silent disconnect failure: %s",
				   camel_exception_get_description(&ex));
		}
		camel_exception_clear (&ex);
	}

	if (camel_service->url)
		camel_url_free (camel_service->url);
	if (camel_service->session)
		camel_object_unref (CAMEL_OBJECT (camel_service->session));

#ifdef ENABLE_THREADS
	e_mutex_destroy(camel_service->priv->connect_lock);
#endif
	g_free(camel_service->priv);
}



CamelType
camel_service_get_type (void)
{
	static CamelType camel_service_type = CAMEL_INVALID_TYPE;

	if (camel_service_type == CAMEL_INVALID_TYPE) {
		camel_service_type = camel_type_register( CAMEL_OBJECT_TYPE, "CamelService",
							  sizeof (CamelService),
							  sizeof (CamelServiceClass),
							  (CamelObjectClassInitFunc) camel_service_class_init,
							  NULL,
							  (CamelObjectInitFunc) camel_service_init,
							  camel_service_finalize );
	}

	return camel_service_type;
}

static gboolean
check_url (CamelService *service, CamelException *ex)
{
	char *url_string;

	if (((service->provider->url_flags & CAMEL_URL_NEED_USER)
	     == CAMEL_URL_NEED_USER) &&
	    (service->url->user == NULL || service->url->user[0] == '\0')) {
		url_string = camel_url_to_string (service->url, FALSE);
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_URL_INVALID,
				      _("URL '%s' needs a username component"),
				      url_string);
		g_free (url_string);
		return FALSE;
	} else if (((service->provider->url_flags & CAMEL_URL_NEED_HOST)
		    == CAMEL_URL_NEED_HOST) &&
		   (service->url->host == NULL || service->url->host[0] == '\0')) {
		url_string = camel_url_to_string (service->url, FALSE);
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_URL_INVALID,
				      _("URL '%s' needs a host component"),
				      url_string);
		g_free (url_string);
		return FALSE;
	} else if (((service->provider->url_flags & CAMEL_URL_NEED_PATH)
		    == CAMEL_URL_NEED_PATH) &&
		   (service->url->path == NULL || service->url->path[0] == '\0')) {
		url_string = camel_url_to_string (service->url, FALSE);
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_URL_INVALID,
				      _("URL '%s' needs a path component"),
				      url_string);
		g_free (url_string);
		return FALSE;
	}

	return TRUE;
}

/**
 * camel_service_new: create a new CamelService or subtype
 * @type: the CamelType of the class to create
 * @session: the session for the service
 * @provider: the service's provider
 * @url: the default URL for the service (may be NULL)
 * @ex: a CamelException
 *
 * Creates a new CamelService (or one of its subtypes), initialized
 * with the given parameters.
 *
 * Return value: the CamelService, or NULL.
 **/
CamelService *
camel_service_new (CamelType type, CamelSession *session,
		   CamelProvider *provider, CamelURL *url,
		   CamelException *ex)
{
	CamelService *service;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), NULL);

	service = CAMEL_SERVICE (camel_object_new (type));

	/*service->connect_level = 0;*/

	service->provider = provider;
	service->url = url;
	if (!url->empty && !check_url (service, ex)) {
		camel_object_unref (CAMEL_OBJECT (service));
		return NULL;
	}

	service->session = session;
	camel_object_ref (CAMEL_OBJECT (session));

	service->connected = FALSE;

	return service;
}


static gboolean
service_connect (CamelService *service, CamelException *ex)
{
	/* Things like the CamelMboxStore can validly
	 * not define a connect function.
	 */
	 return TRUE;
}

/**
 * camel_service_connect:
 * @service: CamelService object
 * @ex: a CamelException
 *
 * Connect to the service using the parameters it was initialized
 * with.
 *
 * Return value: whether or not the connection succeeded
 **/

gboolean
camel_service_connect (CamelService *service, CamelException *ex)
{
	gboolean ret = FALSE;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), FALSE);
	g_return_val_if_fail (service->session != NULL, FALSE);
	g_return_val_if_fail (service->url != NULL, FALSE);

	CAMEL_SERVICE_LOCK(service, connect_lock);

	if (service->connected) {
		/* But we're still connected, so no exception
		 * and return true.
		 */
		g_warning ("camel_service_connect: trying to connect to an already connected service");
		ret = TRUE;
	} else if (CSERV_CLASS (service)->connect (service, ex)) {
		service->connected = TRUE;
		ret = TRUE;
	}

	CAMEL_SERVICE_UNLOCK(service, connect_lock);

	return ret;
}

static gboolean
service_disconnect (CamelService *service, gboolean clean, CamelException *ex)
{
	/*service->connect_level--;*/

	/* We let people get away with not having a disconnect
	 * function -- CamelMboxStore, for example. 
	 */

	return TRUE;
}

/**
 * camel_service_disconnect:
 * @service: CamelService object
 * @clean: whether or not to try to disconnect cleanly.
 * @ex: a CamelException
 *
 * Disconnect from the service. If @clean is %FALSE, it should not
 * try to do any synchronizing or other cleanup of the connection.
 *
 * Return value: whether or not the disconnection succeeded without
 * errors. (Consult @ex if %FALSE.)
 **/
gboolean
camel_service_disconnect (CamelService *service, gboolean clean,
			  CamelException *ex)
{
	gboolean res = TRUE;

	CAMEL_SERVICE_LOCK(service, connect_lock);

	if (service->connected) {
		res = CSERV_CLASS (service)->disconnect (service, clean, ex);
		service->connected = FALSE;
	}

	CAMEL_SERVICE_UNLOCK(service, connect_lock);

	return res;
}

/**
 * camel_service_get_url:
 * @service: a service
 *
 * Returns the URL representing a service. The returned URL must be
 * freed when it is no longer needed. For security reasons, this
 * routine does not return the password.
 *
 * Return value: the url name
 **/
char *
camel_service_get_url (CamelService *service)
{
	return camel_url_to_string(service->url, FALSE);
}


static char *
get_name (CamelService *service, gboolean brief)
{
	g_warning ("CamelService::get_name not implemented for `%s'",
		   camel_type_to_name (CAMEL_OBJECT_GET_TYPE (service)));
	return g_strdup ("???");
}		

/**
 * camel_service_get_name:
 * @service: the service
 * @brief: whether or not to use a briefer form
 *
 * This gets the name of the service in a "friendly" (suitable for
 * humans) form. If @brief is %TRUE, this should be a brief description
 * such as for use in the folder tree. If @brief is %FALSE, it should
 * be a more complete and mostly unambiguous description.
 *
 * Return value: the description, which the caller must free.
 **/
char *
camel_service_get_name (CamelService *service, gboolean brief)
{
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);
	g_return_val_if_fail (service->url, NULL);

	return CSERV_CLASS (service)->get_name (service, brief);
}


static char *
get_path (CamelService *service)
{
	GString *gpath;
	char *path;
	CamelURL *url = service->url;
	int flags = service->provider->url_flags;

	/* A sort of ad-hoc default implementation that works for our
	 * current set of services.
	 */

	gpath = g_string_new (service->provider->protocol);
	if (flags & CAMEL_URL_ALLOW_USER) {
		if (flags & CAMEL_URL_ALLOW_HOST) {
			g_string_sprintfa (gpath, "/%s@%s",
					   url->user ? url->user : "",
					   url->host ? url->host : "");
		} else {
			g_string_sprintfa (gpath, "/%s%s",
			   url->user ? url->user : "",
			   ((flags & CAMEL_URL_NEED_USER) == CAMEL_URL_NEED_USER) ? "" : "@");
		}
	} else if (flags & CAMEL_URL_ALLOW_HOST) {
		g_string_sprintfa (gpath, "/%s%s",
		   ((flags & CAMEL_URL_NEED_HOST) == CAMEL_URL_NEED_HOST) ? "" : "@",
		   url->host ? url->host : "");
	}
	if ((flags & CAMEL_URL_NEED_PATH) == CAMEL_URL_NEED_PATH) {
		g_string_sprintfa (gpath, "%s%s",
				   *url->path == '/' ? "" : "/",
				   url->path);
	}

	path = gpath->str;
	g_string_free (gpath, FALSE);
	return path;
}		

/**
 * camel_service_get_path:
 * @service: the service
 *
 * This gets a valid UNIX relative path describing the service, which
 * is guaranteed to be different from the path returned for any
 * different service. This path MUST start with the name of the
 * provider, followed by a "/", but after that, it is up to the
 * provider.
 *
 * Return value: the path, which the caller must free.
 **/
char *
camel_service_get_path (CamelService *service)
{
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);
	g_return_val_if_fail (service->url, NULL);

	return CSERV_CLASS (service)->get_path (service);
}


/**
 * camel_service_get_session:
 * @service: a service
 *
 * Returns the CamelSession associated with the service.
 *
 * Return value: the session
 **/
CamelSession *
camel_service_get_session (CamelService *service)
{
	return service->session;
}

/**
 * camel_service_get_provider:
 * @service: a service
 *
 * Returns the CamelProvider associated with the service.
 *
 * Return value: the provider
 **/
CamelProvider *
camel_service_get_provider (CamelService *service)
{
	return service->provider;
}

GList *
query_auth_types_func (CamelService *service, CamelException *ex)
{
	return NULL;
}

/**
 * camel_service_query_auth_types:
 * @service: a CamelService
 * @ex: a CamelException
 *
 * This is used by the mail source wizard to get the list of
 * authentication types supported by the protocol, and information
 * about them.
 *
 * This may be called on a service with or without an associated URL.
 * If there is no URL, the routine must return a generic answer. If
 * the service does have a URL, the routine SHOULD connect to the
 * server and query what authentication mechanisms it supports. If
 * it cannot do that for any reason, it should set @ex accordingly.
 *
 * Return value: a list of CamelServiceAuthType records. The caller
 * must free the list by calling camel_service_free_auth_types when
 * it is done.
 **/
GList *
camel_service_query_auth_types (CamelService *service, CamelException *ex)
{
	GList *ret;

	/* note that we get the connect lock here, which means the callee
	   must not call the connect functions itself */
	CAMEL_SERVICE_LOCK(service, connect_lock);

	if (service->url->empty)
		ret = CSERV_CLASS (service)->query_auth_types_generic (service, ex);
	else
		ret = CSERV_CLASS (service)->query_auth_types_connected (service, ex);

	CAMEL_SERVICE_UNLOCK(service, connect_lock);

	return ret;
}

static void
free_auth_types (CamelService *service, GList *authtypes)
{
	;
}

/**
 * camel_service_free_auth_types:
 * @service: the service
 * @authtypes: the list of authtypes
 *
 * This frees the data allocated by camel_service_query_auth_types().
 **/
void
camel_service_free_auth_types (CamelService *service, GList *authtypes)
{
	CSERV_CLASS (service)->free_auth_types (service, authtypes);
}


/* URL utility routines */

/**
 * camel_service_gethost:
 * @service: a CamelService
 * @ex: a CamelException
 *
 * This is a convenience function to do a gethostbyname on the host
 * for the service's URL.
 *
 * Return value: a (statically-allocated) hostent.
 **/
struct hostent *
camel_service_gethost (CamelService *service, CamelException *ex)
{
	struct hostent *h;
	char *hostname;

#warning "This needs to use gethostbyname_r()"

	if (service->url->host)
		hostname = service->url->host;
	else
		hostname = "localhost";
	h = gethostbyname (hostname);
	if (!h) {
		extern int h_errno;

		if (h_errno == HOST_NOT_FOUND || h_errno == NO_DATA) {
			camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_URL_INVALID,
					      _("No such host %s."), hostname);
		} else {
			camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
					      _("Temporarily unable to look "
						"up hostname %s."), hostname);
		}
		return NULL;
	}

	return h;
}
