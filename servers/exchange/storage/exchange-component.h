/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2003, 2004 Novell, Inc. */

#ifndef __EXCHANGE_COMPONENT_H__
#define __EXCHANGE_COMPONENT_H__

#include <glib-object.h>
#include "exchange-types.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define EXCHANGE_TYPE_COMPONENT               (exchange_component_get_type ())
#define EXCHANGE_COMPONENT(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXCHANGE_TYPE_COMPONENT, ExchangeComponent))
#define EXCHANGE_COMPONENT_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), EXCHANGE_TYPE_COMPONENT, ExchangeComponentClass))
#define EXCHANGE_IS_COMPONENT(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_TYPE_COMPONENT))
#define EXCHANGE_IS_COMPONENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), EXCHANGE_TYPE_COMPONENT))

typedef struct _ExchangeComponent                ExchangeComponent;
typedef struct _ExchangeComponentPrivate         ExchangeComponentPrivate;
typedef struct _ExchangeComponentClass           ExchangeComponentClass;

struct _ExchangeComponent {
	GObject parent;
	
	ExchangeComponentPrivate *priv;
};

struct _ExchangeComponentClass {
	GObjectClass parent_class;

};

extern ExchangeComponent *global_exchange_component;

GType              exchange_component_get_type (void);
/* SURF : Tried out something here
ExchangeComponent *exchange_component_new      (const char *uri,
						const char *user,
						const char *passwd);
*/
ExchangeComponent *exchange_component_new (void);

ExchangeAccount   *exchange_component_get_account_for_uri (
					ExchangeComponent *component,
					const char        *uri);

gboolean           exchange_component_is_interactive (
					ExchangeComponent *component);

void               exchange_component_is_offline (
					ExchangeComponent *component, 
					int *state);

//void exchange_component_set_offline_listener (ExchangeComponent *component, 
						//ExchangeOfflineListener *listener);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_COMPONENT_H__ */
