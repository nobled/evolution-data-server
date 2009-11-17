/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-session.h : Abstract class for an email session */

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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_SESSION_H
#define CAMEL_SESSION_H

#include <camel/camel-filter-driver.h>
#include <camel/camel-junk-plugin.h>
#include <camel/camel-msgport.h>
#include <camel/camel-object.h>
#include <camel/camel-provider.h>
#include <camel/camel-service.h>

/* Standard GObject macros */
#define CAMEL_TYPE_SESSION \
	(camel_session_get_type ())
#define CAMEL_SESSION(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SESSION, CamelSession))
#define CAMEL_SESSION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SESSION, CamelSessionClass))
#define CAMEL_IS_SESSION(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SESSION))
#define CAMEL_IS_SESSION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SESSION))
#define CAMEL_SESSION_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SESSION, CamelSessionClass))

G_BEGIN_DECLS

typedef struct _CamelSession CamelSession;
typedef struct _CamelSessionClass CamelSessionClass;
typedef struct _CamelSessionPrivate CamelSessionPrivate;

typedef gboolean (*CamelTimeoutCallback) (gpointer data);
typedef enum {
	CAMEL_SESSION_ALERT_INFO,
	CAMEL_SESSION_ALERT_WARNING,
	CAMEL_SESSION_ALERT_ERROR
} CamelSessionAlertType;

enum {
	CAMEL_SESSION_PASSWORD_REPROMPT = 1 << 0,
	CAMEL_SESSION_PASSWORD_SECRET = 1 << 2,
	CAMEL_SESSION_PASSWORD_STATIC = 1 << 3,
	CAMEL_SESSION_PASSPHRASE = 1 << 4
};

struct _CamelSession {
	CamelObject parent;
	CamelSessionPrivate *priv;

	gchar *storage_path;
	CamelJunkPlugin *junk_plugin;

	guint online:1;
	guint check_junk:1;
	guint network_state:1;
};

typedef struct _CamelSessionThreadOps CamelSessionThreadOps;
typedef struct _CamelSessionThreadMsg CamelSessionThreadMsg;

struct _CamelSessionClass {
	CamelObjectClass parent_class;

	CamelService *  (*get_service)       (CamelSession *session,
					      const gchar *url_string,
					      CamelProviderType type,
					      CamelException *ex);
	gchar *          (*get_storage_path)  (CamelSession *session,
					      CamelService *service,
					      CamelException *ex);

	gchar *          (*get_password)      (CamelSession *session,
					      CamelService *service,
					      const gchar *domain,
					      const gchar *prompt,
					      const gchar *item,
					      guint32 flags,
					      CamelException *ex);
	void            (*forget_password)   (CamelSession *session,
					      CamelService *service,
					      const gchar *domain,
					      const gchar *item,
					      CamelException *ex);
	gboolean        (*alert_user)        (CamelSession *session,
					      CamelSessionAlertType type,
					      const gchar *prompt,
					      gboolean cancel);

	CamelFilterDriver * (*get_filter_driver) (CamelSession *session,
						  const gchar *type,
						  CamelException *ex);

	/* mechanism for creating and maintaining multiple threads of control */
	gpointer           (*thread_msg_new)    (CamelSession *session,
					      CamelSessionThreadOps *ops,
					      guint size);
	void            (*thread_msg_free)   (CamelSession *session,
					      CamelSessionThreadMsg *msg);
	gint             (*thread_queue)      (CamelSession *session,
					      CamelSessionThreadMsg *msg,
					      gint flags);
	void            (*thread_wait)       (CamelSession *session,
					      gint id);
	void            (*thread_status)     (CamelSession *session,
					      CamelSessionThreadMsg *msg,
					      const gchar *text,
					      gint pc);
	gboolean        (*lookup_addressbook)(CamelSession *session,
					      const gchar *name);
	void		(*forward_to)        (CamelSession *session,
					      struct _CamelFolder *folder,
					      struct _CamelMimeMessage *message,
					      const gchar *address,
					      CamelException *ex);
};

GType camel_session_get_type (void);

void            camel_session_construct             (CamelSession *session,
						     const gchar *storage_path);

CamelService *  camel_session_get_service           (CamelSession *session,
						     const gchar *url_string,
						     CamelProviderType type,
						     CamelException *ex);
CamelService *  camel_session_get_service_connected (CamelSession *session,
						     const gchar *url_string,
						     CamelProviderType type,
						     CamelException *ex);

#define camel_session_get_store(session, url_string, ex) \
	((CamelStore *) camel_session_get_service_connected \
	(session, url_string, CAMEL_PROVIDER_STORE, ex))
#define camel_session_get_transport(session, url_string, ex) \
	((CamelTransport *) camel_session_get_service_connected \
	(session, url_string, CAMEL_PROVIDER_TRANSPORT, ex))

gchar *             camel_session_get_storage_path   (CamelSession *session,
						     CamelService *service,
						     CamelException *ex);

gchar *             camel_session_get_password       (CamelSession *session,
						     CamelService *service,
						     const gchar *domain,
						     const gchar *prompt,
						     const gchar *item,
						     guint32 flags,
						     CamelException *ex);
void               camel_session_forget_password    (CamelSession *session,
						     CamelService *service,
						     const gchar *domain,
						     const gchar *item,
						     CamelException *ex);
gboolean           camel_session_alert_user         (CamelSession *session,
						     CamelSessionAlertType type,
						     const gchar *prompt,
						     gboolean cancel);

gchar *             camel_session_build_password_prompt
						    (const gchar *type,
						     const gchar *user,
						     const gchar *host);

gboolean           camel_session_is_online          (CamelSession *session);
void               camel_session_set_online         (CamelSession *session,
						     gboolean online);

CamelFilterDriver *camel_session_get_filter_driver  (CamelSession *session,
						     const gchar *type,
						     CamelException *ex);

gboolean           camel_session_check_junk         (CamelSession *session);
void               camel_session_set_check_junk     (CamelSession *session,
						     gboolean check_junk);

struct _CamelSessionThreadOps {
	void (*receive)(CamelSession *session, struct _CamelSessionThreadMsg *m);
	void (*free)(CamelSession *session, struct _CamelSessionThreadMsg *m);
};

struct _CamelSessionThreadMsg {
	CamelMsg msg;

	gint id;

	CamelException ex;
	CamelSessionThreadOps *ops;
	struct _CamelOperation *op;
	CamelSession *session;

	gpointer data; /* free for implementation to define, not used by camel, do not use in client code */
	/* user fields follow */
};

gpointer              camel_session_thread_msg_new     (CamelSession *session,
						     CamelSessionThreadOps *ops,
						     guint size);
void               camel_session_thread_msg_free    (CamelSession *session,
						     CamelSessionThreadMsg *msg);
gint                camel_session_thread_queue       (CamelSession *session,
						     CamelSessionThreadMsg *msg,
						     gint flags);
void               camel_session_thread_wait        (CamelSession *session,
						     gint id);
gboolean           camel_session_get_network_state  (CamelSession *session);
void               camel_session_set_network_state  (CamelSession *session,
						     gboolean network_state);
const GHashTable * camel_session_get_junk_headers   (CamelSession *session);
void               camel_session_set_junk_headers   (CamelSession *session,
						     const gchar **headers,
						     const gchar **values,
						     gint len);
gboolean           camel_session_lookup_addressbook (CamelSession *session,
						     const gchar *name);

void		   camel_session_forward_to         (CamelSession *session,
						     struct _CamelFolder *folder,
						     struct _CamelMimeMessage *message,
						     const gchar *address,
						     CamelException *ex);

G_END_DECLS

#endif /* CAMEL_SESSION_H */
