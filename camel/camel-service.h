/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-service.h : Abstract class for an email service */
/*
 *
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 *          Michael Zucchi <notzed@ximian.com>
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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_SERVICE_H
#define CAMEL_SERVICE_H

#include <camel/camel-object.h>
#include <camel/camel-url.h>
#include <camel/camel-provider.h>
#include <camel/camel-operation.h>

/* Standard GObject macros */
#define CAMEL_TYPE_SERVICE \
	(camel_service_get_type ())
#define CAMEL_SERVICE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SERVICE, CamelService))
#define CAMEL_SERVICE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SERVICE, CamelServiceClass))
#define CAMEL_IS_SERVICE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SERVICE))
#define CAMEL_IS_SERVICE_CLASS(obj) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SERVICE))
#define CAMEL_SERVICE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SERVICE, CamelServiceClass))

G_BEGIN_DECLS

struct _CamelSession;

typedef struct _CamelService CamelService;
typedef struct _CamelServiceClass CamelServiceClass;
typedef struct _CamelServicePrivate CamelServicePrivate;

enum {
	CAMEL_SERVICE_ARG_FIRST  = CAMEL_ARG_FIRST + 100,
	CAMEL_SERVICE_ARG_USERNAME,
	CAMEL_SERVICE_ARG_AUTH,
	CAMEL_SERVICE_ARG_HOSTNAME,
	CAMEL_SERVICE_ARG_PORT,
	CAMEL_SERVICE_ARG_PATH
};

#define CAMEL_SERVICE_USERNAME     (CAMEL_SERVICE_ARG_USERNAME | CAMEL_ARG_STR)
#define CAMEL_SERVICE_AUTH         (CAMEL_SERVICE_ARG_AUTH | CAMEL_ARG_STR)
#define CAMEL_SERVICE_HOSTNAME     (CAMEL_SERVICE_ARG_HOSTNAME | CAMEL_ARG_STR)
#define CAMEL_SERVICE_PORT         (CAMEL_SERVICE_ARG_PORT | CAMEL_ARG_INT)
#define CAMEL_SERVICE_PATH         (CAMEL_SERVICE_ARG_PATH | CAMEL_ARG_STR)

typedef enum {
	CAMEL_SERVICE_DISCONNECTED,
	CAMEL_SERVICE_CONNECTING,
	CAMEL_SERVICE_CONNECTED,
	CAMEL_SERVICE_DISCONNECTING
} CamelServiceConnectionStatus;

struct _CamelService {
	CamelObject parent;
	CamelServicePrivate *priv;

	struct _CamelSession *session;
	CamelProvider *provider;
	CamelServiceConnectionStatus status;
	CamelOperation *connect_op;
	CamelURL *url;
};

struct _CamelServiceClass {
	CamelObjectClass parent_class;

	void      (*construct)         (CamelService *service,
					struct _CamelSession *session,
					CamelProvider *provider,
					CamelURL *url,
					CamelException *ex);

	gboolean  (*connect)           (CamelService *service,
					CamelException *ex);
	gboolean  (*disconnect)        (CamelService *service,
					gboolean clean,
					CamelException *ex);
	void      (*cancel_connect)    (CamelService *service);

	GList *   (*query_auth_types)  (CamelService *service,
					CamelException *ex);

	gchar *    (*get_name)          (CamelService *service,
					gboolean brief);
	gchar *    (*get_path)          (CamelService *service);
};

/* query_auth_types returns a GList of these */
typedef struct {
	const gchar *name;               /* user-friendly name */
	const gchar *description;
	const gchar *authproto;

	gboolean need_password;   /* needs a password to authenticate */
} CamelServiceAuthType;

/* public methods */
void                camel_service_construct          (CamelService *service,
						      struct _CamelSession *session,
						      CamelProvider *provider,
						      CamelURL *url,
						      CamelException *ex);
gboolean            camel_service_connect            (CamelService *service,
						      CamelException *ex);
gboolean            camel_service_disconnect         (CamelService *service,
						      gboolean clean,
						      CamelException *ex);
void                camel_service_cancel_connect     (CamelService *service);
gchar *              camel_service_get_url            (CamelService *service);
gchar *              camel_service_get_name           (CamelService *service,
						      gboolean brief);
gchar *              camel_service_get_path           (CamelService *service);
struct _CamelSession *      camel_service_get_session        (CamelService *service);
CamelProvider *     camel_service_get_provider       (CamelService *service);
GList *             camel_service_query_auth_types   (CamelService *service,
						      CamelException *ex);

GType camel_service_get_type (void);

G_END_DECLS

#endif /* CAMEL_SERVICE_H */
