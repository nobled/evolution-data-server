/* Evolution calendar factory
 *
 * Copyright (C) 2000-2003 Ximian, Inc.
 *
 * Authors: 
 *   Federico Mena-Quintero <federico@ximian.com>
 *   JP Rosevear <jpr@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#include <bonobo-activation/bonobo-activation.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-main.h>
#include <libedataserver/e-url.h>
#include <libedataserver/e-source.h>
#include "e-cal-backend.h"
#include "e-data-cal.h"
#include "e-data-cal-factory.h"

#define PARENT_TYPE                BONOBO_TYPE_OBJECT
#define DEFAULT_E_DATA_CAL_FACTORY_OAF_ID "OAFIID:GNOME_Evolution_DataServer_CalFactory:" BASE_VERSION

static BonoboObjectClass *parent_class;

/* Private part of the CalFactory structure */
struct _EDataCalFactoryPrivate {
	/* Hash table from URI method strings to GType * for backend class types */
	GHashTable *methods;

	/* Hash table from GnomeVFSURI structures to CalBackend objects */
	GHashTable *backends;

	/* OAFIID of the factory */
	char *iid;

	/* Whether we have been registered with OAF yet */
	guint registered : 1;
};

/* Signal IDs */
enum SIGNALS {
	LAST_CALENDAR_GONE,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

/* Opening calendars */
static icalcomponent_kind
calobjtype_to_icalkind (const GNOME_Evolution_Calendar_CalObjType type)
{
	switch (type){
	case GNOME_Evolution_Calendar_TYPE_EVENT:
		return ICAL_VEVENT_COMPONENT;
	case GNOME_Evolution_Calendar_TYPE_TODO:
		return ICAL_VTODO_COMPONENT;
	case GNOME_Evolution_Calendar_TYPE_JOURNAL:
		return ICAL_VJOURNAL_COMPONENT;
	}
	
	return ICAL_NO_COMPONENT;
}

static GType
get_backend_type (GHashTable *methods, const char *method, icalcomponent_kind kind)
{
	GHashTable *kinds;
	GType type;
	
	kinds = g_hash_table_lookup (methods, method);
	if (!kinds)
		return 0;

	type = GPOINTER_TO_INT (g_hash_table_lookup (kinds, GINT_TO_POINTER (kind)));

	return type;
}

/* Looks up a calendar backend in a factory's hash table of uri->cal.  If
 * *non-NULL, orig_uri_return will be set to point to the original key in the
 * *hash table.
 */
static ECalBackend *
lookup_backend (EDataCalFactory *factory, const char *uristr)
{
	EDataCalFactoryPrivate *priv;
	EUri *uri;
	ECalBackend *backend;
	char *tmp;

	priv = factory->priv;

	uri = e_uri_new (uristr);
	if (!uri)
		return NULL;

	tmp = e_uri_to_string (uri, FALSE);
	backend = g_hash_table_lookup (priv->backends, tmp);
	g_free (tmp);
	e_uri_free (uri);

	return backend;
}

/* Callback used when a backend loses its last connected client */
static void
backend_last_client_gone_cb (ECalBackend *backend, gpointer data)
{
	EDataCalFactory *factory;
	EDataCalFactoryPrivate *priv;
	ECalBackend *ret_backend;
	const char *uristr;

	fprintf (stderr, "backend_last_client_gone_cb() called!\n");

	factory = E_DATA_CAL_FACTORY (data);
	priv = factory->priv;

	/* Remove the backend from the hash table */

	uristr = e_cal_backend_get_uri (backend);
	g_assert (uristr != NULL);

	ret_backend = lookup_backend (factory, uristr);
	g_assert (ret_backend != NULL);
	g_assert (ret_backend == backend);

	g_hash_table_remove (priv->backends, uristr);

	/* Notify upstream if there are no more backends */

	if (g_hash_table_size (priv->backends) == 0)
		g_signal_emit (G_OBJECT (factory), signals[LAST_CALENDAR_GONE], 0);
}



static GNOME_Evolution_Calendar_Cal
impl_CalFactory_getCal (PortableServer_Servant servant,
			const CORBA_char *source_xml,
			const GNOME_Evolution_Calendar_CalObjType type,
			const GNOME_Evolution_Calendar_CalListener listener,
			CORBA_Environment *ev)
{
	EDataCalFactory *factory;
	EDataCalFactoryPrivate *priv;
	EDataCal *cal = CORBA_OBJECT_NIL;
	ECalBackend *backend;
	CORBA_Environment ev2;
	GNOME_Evolution_Calendar_CalListener listener_copy;
	GType backend_type;
	ESource *source;
	char *str_uri;
	EUri *uri;
	char *uri_type_string;
	
	factory = E_DATA_CAL_FACTORY (bonobo_object_from_servant (servant));
	priv = factory->priv;

	source = e_source_new_from_standalone_xml (source_xml);
	if (!source) {
		bonobo_exception_set (ev, ex_GNOME_Evolution_Calendar_CalFactory_InvalidURI);

		return CORBA_OBJECT_NIL;
	}

	/* Get the URI so we can extract the protocol */
	str_uri = e_source_get_uri (source);
	if (!str_uri) {
		g_object_unref (source);
		bonobo_exception_set (ev, ex_GNOME_Evolution_Calendar_CalFactory_InvalidURI);

		return CORBA_OBJECT_NIL;
	}

	/* Parse the uri */
	uri = e_uri_new (str_uri);
	g_free (str_uri);

	if (!uri) {
		bonobo_exception_set (ev, ex_GNOME_Evolution_Calendar_CalFactory_InvalidURI);

		return CORBA_OBJECT_NIL;
	}
	uri_type_string = g_strdup_printf ("%s:%d", e_uri_to_string (uri, FALSE), type);	

	/* Find the associated backend type (if any) */
	backend_type = get_backend_type (priv->methods, uri->protocol, calobjtype_to_icalkind (type));
	if (!backend_type) {
		/* FIXME Distinguish between method and kind failures? */
		bonobo_exception_set (ev, ex_GNOME_Evolution_Calendar_CalFactory_UnsupportedMethod);
		goto cleanup;
	}
		
	/* Duplicate the listener object */
	CORBA_exception_init (&ev2);
	listener_copy = CORBA_Object_duplicate (listener, &ev2);

	if (BONOBO_EX (&ev2)) {
		g_warning (G_STRLOC ": could not duplicate the listener");
		bonobo_exception_set (ev, ex_GNOME_Evolution_Calendar_CalFactory_NilListener);
		CORBA_exception_free (&ev2);
		goto cleanup;
	}
	CORBA_exception_free (&ev2);

	/* Look for an existing backend */
	backend = lookup_backend (factory, uri_type_string);
	if (!backend) {
		/* There was no existing backend, create a new one */
		backend = g_object_new (backend_type, "source", source, "kind", calobjtype_to_icalkind (type), NULL);
		if (!backend) {
			g_warning (G_STRLOC ": could not instantiate backend");
			bonobo_exception_set (ev, ex_GNOME_Evolution_Calendar_CalFactory_UnsupportedMethod);
			goto cleanup;
		}

		/* Track the backend */
		g_hash_table_insert (priv->backends, g_strdup (uri_type_string), backend);

		g_signal_connect (G_OBJECT (backend), "last_client_gone",
				  G_CALLBACK (backend_last_client_gone_cb),
				  factory);
	}
	
	/* Create the corba calendar */
	cal = e_data_cal_new (backend, uri_type_string, listener);
	if (!cal) {
		g_warning (G_STRLOC ": could not create the corba calendar");
		bonobo_exception_set (ev, ex_GNOME_Evolution_Calendar_CalFactory_UnsupportedMethod);
		goto cleanup;
	}

	/* Let the backend know about its clients corba clients */
	e_cal_backend_add_client (backend, cal);
	
 cleanup:
	e_uri_free (uri);
	g_free (uri_type_string);
	g_object_unref (source);

	return CORBA_Object_duplicate (BONOBO_OBJREF (cal), ev);
}



/**
 * e_data_cal_factory_new:
 * @void:
 *
 * Creates a new #EDataCalFactory object.
 *
 * Return value: A newly-created #EDataCalFactory, or NULL if its corresponding CORBA
 * object could not be created.
 **/
EDataCalFactory *
e_data_cal_factory_new (void)
{
	EDataCalFactory *factory;

	factory = g_object_new (E_TYPE_DATA_CAL_FACTORY, 
				"poa", bonobo_poa_get_threaded (ORBIT_THREAD_HINT_PER_REQUEST, NULL), 
				NULL);

	return factory;
}

/* Destroy handler for the calendar */
static void
e_data_cal_factory_finalize (GObject *object)
{
	EDataCalFactory *factory;
	EDataCalFactoryPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_DATA_CAL_FACTORY (object));

	factory = E_DATA_CAL_FACTORY (object);
	priv = factory->priv;

	g_hash_table_destroy (priv->methods);
	priv->methods = NULL;

	/* Should we assert that there are no more backends? */
	g_hash_table_destroy (priv->backends);
	priv->backends = NULL;

	if (priv->registered) {
		bonobo_activation_active_server_unregister (priv->iid, BONOBO_OBJREF (factory));
		priv->registered = FALSE;
	}
	g_free (priv->iid);

	g_free (priv);
	factory->priv = NULL;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/* Class initialization function for the calendar factory */
static void
e_data_cal_factory_class_init (EDataCalFactoryClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;
	POA_GNOME_Evolution_Calendar_CalFactory__epv *epv = &klass->epv;

	parent_class = g_type_class_peek_parent (klass);

	signals[LAST_CALENDAR_GONE] =
		g_signal_new ("last_calendar_gone",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (EDataCalFactoryClass, last_calendar_gone),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	/* Class method overrides */
	object_class->finalize = e_data_cal_factory_finalize;

	/* Epv methods */
	epv->getCal = impl_CalFactory_getCal;
}

/* Object initialization function for the calendar factory */
static void
e_data_cal_factory_init (EDataCalFactory *factory, EDataCalFactoryClass *klass)
{
	EDataCalFactoryPrivate *priv;

	priv = g_new0 (EDataCalFactoryPrivate, 1);
	factory->priv = priv;

	priv->methods = g_hash_table_new_full (g_str_hash, g_str_equal, 
					       (GDestroyNotify) g_free, (GDestroyNotify) g_hash_table_destroy);
	priv->backends = g_hash_table_new_full (g_str_hash, g_str_equal, 
						(GDestroyNotify) g_free, (GDestroyNotify) g_object_unref);
	priv->registered = FALSE;
}

BONOBO_TYPE_FUNC_FULL (EDataCalFactory,
		       GNOME_Evolution_Calendar_CalFactory,
		       PARENT_TYPE,
		       e_data_cal_factory);

/**
 * e_data_cal_factory_register_storage:
 * @factory: A calendar factory.
 * @iid: OAFIID for the factory to be registered.
 * 
 * Registers a calendar factory with the OAF object activation daemon.  This
 * function must be called before any clients can activate the factory.
 * 
 * Return value: TRUE on success, FALSE otherwise.
 **/
gboolean
e_data_cal_factory_register_storage (EDataCalFactory *factory, const char *iid)
{
	EDataCalFactoryPrivate *priv;
	Bonobo_RegistrationResult result;
	char *tmp_iid;

	g_return_val_if_fail (factory != NULL, FALSE);
	g_return_val_if_fail (E_IS_DATA_CAL_FACTORY (factory), FALSE);

	priv = factory->priv;

	g_return_val_if_fail (!priv->registered, FALSE);

	/* if iid is NULL, use the default factory OAFIID */
	if (iid)
		tmp_iid = g_strdup (iid);
	else
		tmp_iid = g_strdup (DEFAULT_E_DATA_CAL_FACTORY_OAF_ID);

	result = bonobo_activation_active_server_register (tmp_iid, BONOBO_OBJREF (factory));

	switch (result) {
	case Bonobo_ACTIVATION_REG_SUCCESS:
		priv->registered = TRUE;
		priv->iid = tmp_iid;
		return TRUE;

	case Bonobo_ACTIVATION_REG_NOT_LISTED:
		g_warning (G_STRLOC ": cannot register the calendar factory %s (not listed)", tmp_iid);
		break;

	case Bonobo_ACTIVATION_REG_ALREADY_ACTIVE:
		g_warning (G_STRLOC ": cannot register the calendar factory (already active)");
		break;

	case Bonobo_ACTIVATION_REG_ERROR:
	default:
		g_warning (G_STRLOC ": cannot register the calendar factory (generic error)");
		break;
	}

	g_free (tmp_iid);

	return FALSE;
}

/**
 * e_data_cal_factory_register_method:
 * @factory: A calendar factory.
 * @method: Method for the URI, i.e. "http", "file", etc.
 * @backend_type: Class type of the backend to create for this @method.
 * 
 * Registers the type of a #ECalBackend subclass that will be used to handle URIs
 * with a particular method.  When the factory is asked to open a particular
 * URI, it will look in its list of registered methods and create a backend of
 * the appropriate type.
 **/
void
e_data_cal_factory_register_method (EDataCalFactory *factory, const char *method, icalcomponent_kind kind, GType backend_type)
{
	EDataCalFactoryPrivate *priv;
	char *method_str;
	GHashTable *kinds;
	GType type;
	
	g_return_if_fail (factory != NULL);
	g_return_if_fail (E_IS_DATA_CAL_FACTORY (factory));
	g_return_if_fail (method != NULL);
	g_return_if_fail (backend_type != 0);
	g_return_if_fail (g_type_is_a (backend_type, E_TYPE_CAL_BACKEND));

	priv = factory->priv;

	method_str = g_ascii_strdown (method, -1);

	kinds = g_hash_table_lookup (priv->methods, method_str);
	if (kinds) {
		type = GPOINTER_TO_INT (g_hash_table_lookup (kinds, GINT_TO_POINTER (kind)));
		if (type) {
			g_warning (G_STRLOC ": method `%s' already registered", method_str);
			g_free (method_str);

			return;
		}

		g_free (method_str);
	} else {
		kinds = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, NULL);
		g_hash_table_insert (priv->methods, method_str, kinds);
	}
	
	g_hash_table_insert (kinds, GINT_TO_POINTER (kind), GINT_TO_POINTER (backend_type));
}

/**
 * e_data_cal_factory_get_n_backends
 * @factory: A calendar factory.
 *
 * Get the number of backends currently active in the given factory.
 *
 * Returns: the number of backends.
 */
int
e_data_cal_factory_get_n_backends (EDataCalFactory *factory)
{
	EDataCalFactoryPrivate *priv;

	g_return_val_if_fail (E_IS_DATA_CAL_FACTORY (factory), 0);

	priv = factory->priv;
	return g_hash_table_size (priv->backends);
}

/* Frees a uri/backend pair from the backends hash table */
static void
dump_backend (gpointer key, gpointer value, gpointer data)
{
	char *uri;
	ECalBackend *backend;

	uri = key;
	backend = value;

	g_message ("  %s: %p", uri, backend);
}

void
e_data_cal_factory_dump_active_backends   (EDataCalFactory *factory)
{
	EDataCalFactoryPrivate *priv;

	g_message ("Active PCS backends");

	priv = factory->priv;
	g_hash_table_foreach (priv->backends, dump_backend, NULL);
}
