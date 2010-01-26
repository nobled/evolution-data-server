/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2006 OpenedHand Ltd
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of version 2.1 of the GNU Lesser General Public License as
 * published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Author: Ross Burton <ross@linux.intel.com>
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <dbus/dbus-protocol.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus-glib-bindings.h>
#include <libebackend/e-data-server-module.h>
#include <libebackend/e-offline-listener.h>
#include <libebackend/e-data-factory.h>
#include "e-book-backend-factory.h"
#include "e-data-book-factory.h"
#include "e-data-book.h"
#include "e-book-backend.h"
#include "e-book-backend-factory.h"

#define d(x)

static void impl_BookFactory_getBook(EDataBookFactory *factory, const gchar *IN_uri, DBusGMethodInvocation *context);
#include "e-data-book-factory-glue.h"

/* Convenience macro to test and set a GError/return on failure */
#define g_set_error_val_if_fail(test, returnval, error, domain, code) G_STMT_START{ \
		if G_LIKELY (test) {} else {				\
			g_set_error (error, domain, code, #test);	\
			g_warning(#test " failed");			\
			return (returnval);				\
		}							\
	}G_STMT_END

G_DEFINE_TYPE(EDataBookFactory, e_data_book_factory, E_TYPE_DATA_FACTORY);

#define E_DATA_BOOK_FACTORY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), E_TYPE_DATA_BOOK_FACTORY, EDataBookFactoryPrivate))

struct _EDataBookFactoryPrivate {
};

/* Create the EDataBookFactory error quark */
GQuark
e_data_book_factory_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("e_data_book_factory_error");
	return quark;
}

static EDataBook*
data_new (EBookBackend *backend,
	  ESource      *source)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return e_data_book_new (backend, source);
}

static GType
get_backend_type ()
{
	return E_TYPE_BOOK_BACKEND_FACTORY;
}

static const char*
get_dbus_name_format (EDataBookFactory *factory)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_FACTORY (factory), NULL);

	return "/org/gnome/evolution/dataserver/addressbook/%d/%u";
}

/**
 * e_data_book_factory_set_backend_mode:
 * @factory: A bookendar factory.
 * @mode: Online mode to set.
 *
 * Sets the online mode for all backends created by the given factory.
 */
void
e_data_book_factory_set_backend_mode (EDataBookFactory *factory, gint mode)
{
	e_data_factory_set_backend_mode (E_DATA_FACTORY (factory), mode);
}

static void
e_data_book_factory_class_init (EDataBookFactoryClass *e_data_book_factory_class)
{
	EDataFactoryClass *parent_class = E_DATA_FACTORY_CLASS (e_data_book_factory_class);

	/* XXX: do some ugly casting to avoid breaking API */
	parent_class->data_new = (EData* (*)(EBackend*, ESource*)) data_new;
	parent_class->get_backend_type = get_backend_type;
	parent_class->get_dbus_name_format = (const char* (*)(EDataFactory*)) get_dbus_name_format;

	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (e_data_book_factory_class), &dbus_glib_e_data_book_factory_object_info);
}

/* Instance init */
static void
e_data_book_factory_init (EDataBookFactory *factory)
{
}

static void
impl_BookFactory_getBook (EDataBookFactory      *factory,
			  const gchar           *IN_source,
			  DBusGMethodInvocation *context)
{
	e_data_factory_publish_data (E_DATA_FACTORY (factory), IN_source, context);
}

#define E_DATA_BOOK_FACTORY_SERVICE_NAME "org.gnome.evolution.dataserver.AddressBook"
#define E_DATA_BOOK_FACTORY_OBJECT_PATH "/org/gnome/evolution/dataserver/addressbook/BookFactory"

gint
main (gint argc, gchar **argv)
{
	EDataBookFactory *factory;
	GMainLoop *loop;

	g_type_init ();
	g_set_prgname (E_PRGNAME);
	if (!g_thread_supported ()) g_thread_init (NULL);

	loop = g_main_loop_new (NULL, FALSE);

	factory = g_object_new (E_TYPE_DATA_BOOK_FACTORY, NULL);

	return e_data_factory_main (argc, argv, E_DATA_FACTORY (factory), loop, E_DATA_BOOK_FACTORY_SERVICE_NAME, E_DATA_BOOK_FACTORY_OBJECT_PATH);
}
