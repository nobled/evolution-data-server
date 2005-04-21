/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2003, 2004 Novell, Inc. */

#ifndef __EXCHANGE_COMPONENT_H__
#define __EXCHANGE_COMPONENT_H__

#include <bonobo/bonobo-object.h>
//#include <shell/Evolution.h>
#include "exchange-types.h"
//#include "exchange-offline-listener.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define EXCHANGE_TYPE_COMPONENT               (exchange_component_get_type ())
#define EXCHANGE_COMPONENT(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXCHANGE_TYPE_COMPONENT, ExchangeComponent))
#define EXCHANGE_COMPONENT_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), EXCHANGE_TYPE_COMPONENT, ExchangeComponentClass))
#define EXCHANGE_IS_COMPONENT(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_TYPE_COMPONENT))
#define EXCHANGE_IS_COMPONENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), EXCHANGE_TYPE_COMPONENT))

typedef struct ExchangeComponent                ExchangeComponent;
typedef struct ExchangeComponentPrivate         ExchangeComponentPrivate;
typedef struct ExchangeComponentClass           ExchangeComponentClass;

struct ExchangeComponent {
	BonoboObject parent;
	
	ExchangeComponentPrivate *priv;
};

struct ExchangeComponentClass {
	BonoboObjectClass parent_class;

	//POA_GNOME_Evolution_Component__epv epv;
};

// SURF : extern ExchangeComponent *global_exchange_component;

GType              exchange_component_get_type (void);
ExchangeComponent *exchange_component_new      (void);

ExchangeAccount   *exchange_component_get_account_for_uri (ExchangeComponent *component,
							   const char        *uri);
gboolean           exchange_component_is_interactive      (ExchangeComponent *component);
void           exchange_component_is_offline      (ExchangeComponent *component, int *state);

//void exchange_component_set_offline_listener (ExchangeComponent *component, 
						//ExchangeOfflineListener *listener);

#define EXCHANGE_COMPONENT_FACTORY_IID  "OAFIID:GNOME_Evolution_Exchange_Component_Factory:" BASE_VERSION
#define EXCHANGE_COMPONENT_IID		"OAFIID:GNOME_Evolution_Exchange_Component:" BASE_VERSION
#define EXCHANGE_CALENDAR_FACTORY_ID	"OAFIID:GNOME_Evolution_Exchange_Connector_CalFactory:" BASE_VERSION
#define EXCHANGE_ADDRESSBOOK_FACTORY_ID	"OAFIID:GNOME_Evolution_Exchange_Connector_BookFactory:" BASE_VERSION
#define EXCHANGE_AUTOCONFIG_WIZARD_ID	"OAFIID:GNOME_Evolution_Exchange_Connector_Startup_Wizard:" BASE_VERSION

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_COMPONENT_H__ */
