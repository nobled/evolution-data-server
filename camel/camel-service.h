/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-service.h : Abstract class for an email service */

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


#ifndef CAMEL_SERVICE_H
#define CAMEL_SERVICE_H 1


#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include <camel/camel-object.h>
#include <camel/camel-url.h>
#include <camel/camel-provider.h>
#include <netdb.h>

#define CAMEL_SERVICE_TYPE     (camel_service_get_type ())
#define CAMEL_SERVICE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_SERVICE_TYPE, CamelService))
#define CAMEL_SERVICE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_SERVICE_TYPE, CamelServiceClass))
#define CAMEL_IS_SERVICE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_SERVICE_TYPE))


struct _CamelService {
	CamelObject parent_object;
	struct _CamelServicePrivate *priv;

	CamelSession *session;
	CamelProvider *provider;
	gboolean connected;
	CamelURL *url;
};


typedef struct {
	CamelObjectClass parent_class;

	gboolean  (*connect)           (CamelService *service, 
					CamelException *ex);
	gboolean  (*disconnect)        (CamelService *service,
					gboolean clean,
					CamelException *ex);

	/*gboolean  (*is_connected)      (CamelService *service);*/

	GList *   (*query_auth_types_connected)  (CamelService *service,
						  CamelException *ex);
	GList *   (*query_auth_types_generic)  (CamelService *service,
						CamelException *ex);
	void      (*free_auth_types)   (CamelService *service,
					GList *authtypes);

	char *    (*get_name)          (CamelService *service,
					gboolean brief);
	char *    (*get_path)          (CamelService *service);

} CamelServiceClass;


/* query_auth_types returns a GList of these */
typedef struct {
	char *name, *description, *authproto;
	gboolean need_password;
} CamelServiceAuthType;


/* public methods */
CamelService *      camel_service_new                (CamelType type, 
						      CamelSession *session,
						      CamelProvider *provider,
						      CamelURL *url, 
						      CamelException *ex);
gboolean            camel_service_connect            (CamelService *service, 
						      CamelException *ex);
gboolean            camel_service_disconnect         (CamelService *service,
						      gboolean clean,
						      CamelException *ex);
char *              camel_service_get_url            (CamelService *service);
char *              camel_service_get_name           (CamelService *service,
						      gboolean brief);
char *              camel_service_get_path           (CamelService *service);
CamelSession *      camel_service_get_session        (CamelService *service);
CamelProvider *     camel_service_get_provider       (CamelService *service);
GList *             camel_service_query_auth_types   (CamelService *service,
						      CamelException *ex);
void                camel_service_free_auth_types    (CamelService *service,
						      GList *authtypes);

/* convenience functions */
struct hostent *    camel_service_gethost            (CamelService *service,
						      CamelException *ex);


/* Standard Camel function */
CamelType camel_service_get_type (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_SERVICE_H */

