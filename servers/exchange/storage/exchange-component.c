/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* Copyright (C) 2004 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "exchange-component.h"

#include <unistd.h>

#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-main.h>

#include "e-storage-set.h"
//#include "e-storage-set-view.h"

#include "exchange-account.h"
#include "exchange-config-listener.h"
#include "exchange-hierarchy-gal.h"
//#include "exchange-oof.h"
#include "exchange-storage.h"
#include "e-folder-exchange.h"

//#include "mail-stub-listener.h"
//#include "mail-stub-exchange.h"

//#include "xc-backend-view.h"

//#include "exchange-migrate.h" 

#define d(x)

#define PARENT_TYPE bonobo_object_get_type ()
static BonoboObjectClass *parent_class = NULL;
static gboolean idle_do_interactive (gpointer user_data);

struct ExchangeComponentPrivate {
	GdkNativeWindow xid;

	EFolderTypeRegistry *folder_type_registry;

	ExchangeConfigListener *config_listener;
	//ExchangeOfflineListener *offline_listener;
	GSList *accounts;

	GSList *views;
};


typedef struct {
	ExchangeAccount *account;
//	MailStubListener *msl;
} ExchangeComponentAccount;

static void
free_account (ExchangeComponentAccount *baccount)
{
	g_object_unref (baccount->account);
	// SURF :g_object_unref (baccount->msl);
	g_free (baccount);
}

static void
dispose (GObject *object)
{
	ExchangeComponentPrivate *priv = EXCHANGE_COMPONENT (object)->priv;
	GSList *p;

	if (priv->folder_type_registry) {
		g_object_unref (priv->folder_type_registry);
		priv->folder_type_registry = NULL;
	}

	if (priv->config_listener) {
		g_object_unref (priv->config_listener);
		priv->config_listener = NULL;
	}

	if (priv->accounts) {
		for (p = priv->accounts; p; p = p->next)
			free_account (p->data);
		g_slist_free (priv->accounts);
		priv->accounts = NULL;
	}

	if (priv->views) {
		for (p = priv->views; p; p = p->next)
			g_object_unref (p->data);
		g_slist_free (priv->views);
		priv->views = NULL;
	}
#if 0 
SURF :
	if (priv->offline_listener) {
		g_object_unref (priv->offline_listener);
		priv->offline_listener = NULL;
	}
#endif
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->finalize (object);
}
#if 0 
SURF :
static void
impl_createControls (PortableServer_Servant servant,
		     Bonobo_Control *sidebar_control,
		     Bonobo_Control *view_control,
		     Bonobo_Control *statusbar_control,
		     CORBA_Environment *ev)
{
	ExchangeComponent *component = EXCHANGE_COMPONENT (bonobo_object_from_servant (servant));
	ExchangeComponentPrivate *priv = component->priv;
	XCBackendView *view;
	BonoboControl *control = NULL;

	d(printf("createControls...\n"));

	view = xc_backend_view_new (priv->config_listener,
				    priv->folder_type_registry);
	if (view)
		priv->views = g_slist_append (priv->views, control);

	control = xc_backend_view_get_sidebar (view);
	*sidebar_control =
		CORBA_Object_duplicate (BONOBO_OBJREF (control), ev);

	control = xc_backend_view_get_statusbar (view);
	*statusbar_control =
		CORBA_Object_duplicate (BONOBO_OBJREF (control), ev);

	control = xc_backend_view_get_view (view);
	*view_control =
		CORBA_Object_duplicate (BONOBO_OBJREF (control), ev);
}

static void
impl_upgradeFromVersion (PortableServer_Servant servant,
			 const CORBA_short major,
			 const CORBA_short minor,
			 const CORBA_short revision,
			 CORBA_Environment *ev)
{
	ExchangeComponent *component = EXCHANGE_COMPONENT (bonobo_object_from_servant (servant));
	ExchangeAccount *account;
	const gchar *base_directory=NULL;

	d(printf("upgradeFromVersion %d %d %d\n", major, minor, revision));

	account = exchange_component_get_account_for_uri (component, NULL);
	if (account) {
		base_directory = g_build_filename (g_get_home_dir (),
						   ".evolution",
						   "exchange",
						   account->account_filename,
						   NULL);

		exchange_migrate(major, minor, revision, 
				 base_directory, account->account_filename);
	}
}

static CORBA_boolean
impl_requestQuit (PortableServer_Servant servant,
		  CORBA_Environment *ev)
{
	d(printf("requestQuit\n"));

	/* FIXME */
	return TRUE;
}

/* This returns %TRUE all the time, so if set as an idle callback it
   effectively causes each and every nested glib mainloop to be quit.  */
static gboolean
idle_quit (gpointer user_data)
{
	bonobo_main_quit ();
	return TRUE;
}

static CORBA_boolean
impl_quit (PortableServer_Servant servant,
	   CORBA_Environment *ev)
{
	d(printf("quit\n"));

	g_timeout_add (500, idle_quit, NULL);
	return TRUE;
}

static gboolean
idle_do_interactive (gpointer user_data)
{
	ExchangeComponent *component = user_data;
	ExchangeComponentPrivate *priv = component->priv;
	ExchangeComponentAccount *baccount;
	GSList *acc;

	for (acc = priv->accounts; acc; acc = acc->next) {
		baccount = acc->data;
		if (exchange_component_is_interactive (component))
			exchange_oof_init (baccount->account, priv->xid);
	}
	return FALSE;
}

static void
impl_interactive (PortableServer_Servant servant,
		  const CORBA_boolean now_interactive,
		  const CORBA_unsigned_long new_view_xid,
		  CORBA_Environment *ev)
{
	ExchangeComponent *component = EXCHANGE_COMPONENT (bonobo_object_from_servant (servant));
	ExchangeComponentPrivate *priv = component->priv;

	d(printf("interactive? %s, xid %lu\n", now_interactive ? "yes" : "no", new_view_xid));

	if (now_interactive) {
		priv->xid = new_view_xid;
		g_idle_add (idle_do_interactive, component);
	} else
		priv->xid = 0;
}
#endif
#if 0 
SURF :
static void
new_connection (MailStubListener *listener, int cmd_fd, int status_fd,
		ExchangeComponentAccount *baccount)
{
	MailStub *mse;
	ExchangeAccount *account = baccount->account;
	int mode;

	g_object_ref (account);

	exchange_account_is_offline (account, &mode);
	if (mode != ONLINE_MODE) {
		mse = mail_stub_exchange_new (account, cmd_fd, status_fd);
		goto end;
	}

	if (exchange_account_connect (account))
		mse = mail_stub_exchange_new (account, cmd_fd, status_fd);
	else {
		close (cmd_fd);
		close (status_fd);
	}
end:
	g_object_unref (account);
}
#endif

static void
config_listener_account_created (ExchangeConfigListener *config_listener,
				 ExchangeAccount *account,
				 gpointer user_data)
{
	ExchangeComponent *component = user_data;
	ExchangeComponentPrivate *priv = component->priv;
	ExchangeComponentAccount *baccount;
	char *path;

	baccount = g_new0 (ExchangeComponentAccount, 1);
	baccount->account = g_object_ref (account);

	path = g_strdup_printf ("/tmp/.exchange-%s/%s",
				g_get_user_name (),
				account->account_filename);
/* SURF :
	baccount->msl = mail_stub_listener_new (path);
	g_signal_connect (baccount->msl, "new_connection",
			  G_CALLBACK (new_connection), baccount);
	g_free (path);
*/

	priv->accounts = g_slist_prepend (priv->accounts, baccount);
/* SURF :
	if (priv->xid)
		exchange_oof_init (account, priv->xid);
*/
}

static void
config_listener_account_removed (ExchangeConfigListener *config_listener,
				 ExchangeAccount *account,
				 gpointer user_data)
{
	ExchangeComponent *component = user_data;
	ExchangeComponentPrivate *priv = component->priv;
	ExchangeComponentAccount *baccount;
	GSList *acc;

	for (acc = priv->accounts; acc; acc = acc->next) {
		baccount = acc->data;
		if (baccount->account == account) {
			priv->accounts = g_slist_remove (priv->accounts, baccount);
			free_account (baccount);
			return;
		}
	}
}


static struct {
	char *type, *display_name, *icon;
} folder_types[] = {
	{ "mail", N_("Mail"), "stock_folder" },
	{ "mail/public", NULL, "stock_folder" },

	{ "contacts", N_("Contacts"), "stock_contact" },
	{ "contacts/public", NULL, "stock_contact" },
	{ "contacts/ldap", NULL, "stock_contact" },

	{ "tasks", N_("Tasks"), "stock_todo" },
	{ "tasks/public", NULL, "stock_todo" },

	{ "calendar", N_("Calendar"), "stock_calendar" },
	{ "calendar/public", NULL, "stock_calendar" }
};
static int n_folder_types = G_N_ELEMENTS (folder_types);

static void
setup_folder_type_registry (ExchangeComponent *component)
{
	ExchangeComponentPrivate *priv = component->priv;
	int i;

	priv->folder_type_registry = e_folder_type_registry_new ();
	for (i = 0; i < n_folder_types; i++) {
		e_folder_type_registry_register_type (priv->folder_type_registry,
						      folder_types[i].type,
						      folder_types[i].icon,
						      _(folder_types[i].display_name),
						      NULL,
						      folder_types[i].display_name != NULL,
						      0, NULL);
	}
}

static void
exchange_component_class_init (ExchangeComponentClass *klass)
{
	// SURF : POA_GNOME_Evolution_Component__epv *epv = &klass->epv;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = dispose;
	object_class->finalize = finalize;
#if 0
SURF :
	epv->createControls     = impl_createControls;
	epv->upgradeFromVersion = impl_upgradeFromVersion;
	epv->requestQuit        = impl_requestQuit;
	epv->quit               = impl_quit;
	epv->interactive        = impl_interactive;
#endif

}

static void
exchange_component_init (ExchangeComponent *component)
{
	ExchangeComponentPrivate *priv;

	priv = component->priv = g_new0 (ExchangeComponentPrivate, 1);

	setup_folder_type_registry (component);

       	priv->config_listener = exchange_config_listener_new ();
	g_signal_connect (priv->config_listener, "exchange_account_created",
			  G_CALLBACK (config_listener_account_created),
			  component);
	g_signal_connect (priv->config_listener, "exchange_account_removed",
			  G_CALLBACK (config_listener_account_removed),
			  component);

}

// SURF : BONOBO_TYPE_FUNC_FULL (ExchangeComponent, GNOME_Evolution_Component, PARENT_TYPE, exchange_component)

#if 0 
SURF: 
void
exchange_component_set_offline_listener (ExchangeComponent *component, 
					 ExchangeOfflineListener *listener)
{
	ExchangeComponentPrivate *priv;

	g_return_if_fail (EXCHANGE_IS_COMPONENT (component));

	priv = component->priv;
	priv->offline_listener = listener;
}
#endif

ExchangeComponent *
exchange_component_new (void)
{
	return g_object_new (EXCHANGE_TYPE_COMPONENT, NULL);
}


ExchangeAccount *
exchange_component_get_account_for_uri (ExchangeComponent *component,
					const char *uri)
{
	ExchangeComponentPrivate *priv = component->priv;
	ExchangeComponentAccount *baccount;
	GSList *acc;

	for (acc = priv->accounts; acc; acc = acc->next) {
		baccount = acc->data;

		/* Kludge for while we don't support multiple accounts */
		if (!uri)
			return baccount->account;

		if (exchange_account_get_folder (baccount->account, uri))
			return baccount->account;
		/* FIXME : Handle multiple accounts */
	}
	return NULL;
}

#if 0
 SURF :
void
exchange_component_is_offline (ExchangeComponent *component, int *state)
{
	g_return_if_fail (EXCHANGE_IS_COMPONENT (component));

	exchange_is_offline (component->priv->offline_listener, state);
}
#endif
gboolean
exchange_component_is_interactive (ExchangeComponent *component)
{
	return component->priv->xid != 0;
}

G_DEFINE_TYPE (ExchangeComponent, exchange_component, G_TYPE_OBJECT)
