/* EDBus - GLib D-Bus Library
 *
 * Copyright (C) 2008-2009 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#include "config.h"

#include <stdlib.h>
#include <glib/gi18n.h>

#include "gdbusconnection.h"
#include "gdbusmethodinvocation.h"
#include "gdbusintrospection.h"
#include "gdbusconversion.h"
#include "gdbuserror.h"
#include "gdbusprivate.h"

/**
 * SECTION:gdbusmethodinvocation
 * @short_description: Object for handling remote calls
 * @include: gdbus/gdbus.h
 *
 * Instances of the #EDBusMethodInvocation class are used when
 * handling D-Bus method calls. It provides a way to asynchronously
 * return results and errors.
 */

struct _EDBusMethodInvocationPrivate
{
  /* construct-only properties */
  gchar           *sender;
  gchar           *object_path;
  gchar           *interface_name;
  gchar           *method_name;
  EDBusConnection *connection;
  EVariant        *parameters;
  gpointer         user_data;
};

enum
{
  PROP_0,
  PROP_SENDER,
  PROP_OBJECT_PATH,
  PROP_INTERFACE_NAME,
  PROP_METHOD_NAME,
  PROP_CONNECTION,
  PROP_PARAMETERS,
  PROP_USER_DATA
};

G_DEFINE_TYPE (EDBusMethodInvocation, e_dbus_method_invocation, G_TYPE_OBJECT);

static void
e_dbus_method_invocation_finalize (GObject *object)
{
  EDBusMethodInvocation *invocation = E_DBUS_METHOD_INVOCATION (object);

  g_free (invocation->priv->sender);
  g_free (invocation->priv->object_path);
  g_free (invocation->priv->interface_name);
  g_free (invocation->priv->method_name);
  g_object_unref (invocation->priv->connection);
  e_variant_unref (invocation->priv->parameters);

  if (G_OBJECT_CLASS (e_dbus_method_invocation_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (e_dbus_method_invocation_parent_class)->finalize (object);
}

static void
e_dbus_method_invocation_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  EDBusMethodInvocation *invocation = E_DBUS_METHOD_INVOCATION (object);

  switch (prop_id)
    {
    case PROP_SENDER:
      g_value_set_string (value, e_dbus_method_invocation_get_sender (invocation));
      break;

    case PROP_OBJECT_PATH:
      g_value_set_string (value, e_dbus_method_invocation_get_object_path (invocation));
      break;

    case PROP_INTERFACE_NAME:
      g_value_set_string (value, e_dbus_method_invocation_get_interface_name (invocation));
      break;

    case PROP_METHOD_NAME:
      g_value_set_string (value, e_dbus_method_invocation_get_method_name (invocation));
      break;

    case PROP_CONNECTION:
      g_value_set_object (value, e_dbus_method_invocation_get_connection (invocation));
      break;

    case PROP_PARAMETERS:
      g_value_set_boxed (value, e_dbus_method_invocation_get_parameters (invocation));
      break;

    case PROP_USER_DATA:
      g_value_set_pointer (value, e_dbus_method_invocation_get_user_data (invocation));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
e_dbus_method_invocation_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  EDBusMethodInvocation *invocation = E_DBUS_METHOD_INVOCATION (object);

  switch (prop_id)
    {
    case PROP_SENDER:
      invocation->priv->sender = g_value_dup_string (value);
      break;

    case PROP_OBJECT_PATH:
      invocation->priv->object_path = g_value_dup_string (value);
      break;

    case PROP_INTERFACE_NAME:
      invocation->priv->interface_name = g_value_dup_string (value);
      break;

    case PROP_METHOD_NAME:
      invocation->priv->method_name = g_value_dup_string (value);
      break;

    case PROP_CONNECTION:
      invocation->priv->connection = g_value_dup_object (value);
      break;

    case PROP_PARAMETERS:
      invocation->priv->parameters = g_value_dup_boxed (value);
      break;

    case PROP_USER_DATA:
      invocation->priv->user_data = g_value_get_pointer (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}


static void
e_dbus_method_invocation_class_init (EDBusMethodInvocationClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize     = e_dbus_method_invocation_finalize;
  gobject_class->set_property = e_dbus_method_invocation_set_property;
  gobject_class->get_property = e_dbus_method_invocation_get_property;

  /**
   * EDBusMethodInvocation:sender:
   *
   * The bus name that invoked the method or %NULL if the connection is not a bus connection.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_SENDER,
                                   g_param_spec_string ("sender",
                                                        _("Sender"),
                                                        _("The bus name that invoked the method."),
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_STATIC_NICK));

  /**
   * EDBusMethodInvocation:object-path:
   *
   * The object path the method was invoked on.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_OBJECT_PATH,
                                   g_param_spec_string ("object-path",
                                                        _("Object Path"),
                                                        _("The object path the method was invoked on."),
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_STATIC_NICK));

  /**
   * EDBusMethodInvocation:interface-name:
   *
   * The name of the D-Bus interface the method was invoked on.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_INTERFACE_NAME,
                                   g_param_spec_string ("interface-name",
                                                        _("Interface Name"),
                                                        _("The name of the D-Bus interface the method was invoked on."),
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_STATIC_NICK));

  /**
   * EDBusMethodInvocation:method-name:
   *
   * The name of the method that was invoked.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_METHOD_NAME,
                                   g_param_spec_string ("method-name",
                                                        _("Method Name"),
                                                        _("The name of the method that was invoked."),
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_STATIC_NICK));

  /**
   * EDBusMethodInvocation:connection:
   *
   * The #EDBusConnection the method was invoked on.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_CONNECTION,
                                   g_param_spec_object ("connection",
                                                        _("Connection"),
                                                        _("The #EDBusConnection the method was invoked on."),
                                                        E_TYPE_DBUS_CONNECTION,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_STATIC_NICK));

  /**
   * EDBusMethodInvocation:parameters:
   *
   * The parameters as a #EVariant tuple.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_PARAMETERS,
                                   g_param_spec_boxed ("parameters",
                                                       _("Parameters"),
                                                       _("The parameters as a #EVariant tuple."),
                                                       E_TYPE_VARIANT,
                                                       G_PARAM_READABLE |
                                                       G_PARAM_WRITABLE |
                                                       G_PARAM_CONSTRUCT_ONLY |
                                                       G_PARAM_STATIC_NAME |
                                                       G_PARAM_STATIC_BLURB |
                                                       G_PARAM_STATIC_NICK));

  /**
   * EDBusMethodInvocation:user-data:
   *
   * The @user_data #gpointer passed to e_dbus_connection_register_object().
   */
  g_object_class_install_property (gobject_class,
                                   PROP_USER_DATA,
                                   g_param_spec_pointer ("user-data",
                                                        _("User Data"),
                                                        _("The gpointer passed to e_dbus_connection_register_object()."),
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_STATIC_NICK));

  g_type_class_add_private (klass, sizeof (EDBusMethodInvocationPrivate));
}

static void
e_dbus_method_invocation_init (EDBusMethodInvocation *invocation)
{
  invocation->priv = G_TYPE_INSTANCE_GET_PRIVATE (invocation,
                                                  E_TYPE_DBUS_METHOD_INVOCATION,
                                                  EDBusMethodInvocationPrivate);
}

/**
 * e_dbus_method_invocation_get_sender:
 * @invocation: A #EDBusMethodInvocation.
 *
 * Gets the bus name that invoked the method.
 *
 * Returns: A string. Do not free, it is owned by @invocation.
 */
const gchar *
e_dbus_method_invocation_get_sender (EDBusMethodInvocation *invocation)
{
  g_return_val_if_fail (G_IS_DBUS_METHOD_INVOCATION (invocation), NULL);
  return invocation->priv->sender;
}

/**
 * e_dbus_method_invocation_get_object_path:
 * @invocation: A #EDBusMethodInvocation.
 *
 * Gets the object path the method was invoked on.
 *
 * Returns: A string. Do not free, it is owned by @invocation.
 */
const gchar *
e_dbus_method_invocation_get_object_path (EDBusMethodInvocation *invocation)
{
  g_return_val_if_fail (G_IS_DBUS_METHOD_INVOCATION (invocation), NULL);
  return invocation->priv->object_path;
}

/**
 * e_dbus_method_invocation_get_interface_name:
 * @invocation: A #EDBusMethodInvocation.
 *
 * Gets the name of the D-Bus interface the method was invoked on.
 *
 * Returns: A string. Do not free, it is owned by @invocation.
 */
const gchar *
e_dbus_method_invocation_get_interface_name (EDBusMethodInvocation *invocation)
{
  g_return_val_if_fail (G_IS_DBUS_METHOD_INVOCATION (invocation), NULL);
  return invocation->priv->interface_name;
}

/**
 * e_dbus_method_invocation_get_method_name:
 * @invocation: A #EDBusMethodInvocation.
 *
 * Gets the name of the method that was invoked.
 *
 * Returns: A string. Do not free, it is owned by @invocation.
 */
const gchar *
e_dbus_method_invocation_get_method_name (EDBusMethodInvocation *invocation)
{
  g_return_val_if_fail (G_IS_DBUS_METHOD_INVOCATION (invocation), NULL);
  return invocation->priv->method_name;
}

/**
 * e_dbus_method_invocation_get_connection:
 * @invocation: A #EDBusMethodInvocation.
 *
 * Gets the #EDBusConnection the method was invoked on.
 *
 * Returns: A #EDBusConnection. Do not free, it is owned by @invocation.
 */
EDBusConnection *
e_dbus_method_invocation_get_connection (EDBusMethodInvocation *invocation)
{
  g_return_val_if_fail (G_IS_DBUS_METHOD_INVOCATION (invocation), NULL);
  return invocation->priv->connection;
}

/**
 * e_dbus_method_invocation_get_parameters:
 * @invocation: A #EDBusMethodInvocation.
 *
 * Gets the parameters of the method invocation.
 *
 * Returns: A #EVariant. Do not free, it is owned by @invocation.
 */
EVariant *
e_dbus_method_invocation_get_parameters (EDBusMethodInvocation *invocation)
{
  g_return_val_if_fail (G_IS_DBUS_METHOD_INVOCATION (invocation), NULL);
  return invocation->priv->parameters;
}

/**
 * e_dbus_method_invocation_get_user_data:
 * @invocation: A #EDBusMethodInvocation.
 *
 * Gets the @user_data #gpointer passed to e_dbus_connection_register_object().
 *
 * Returns: A #gpointer.
 */
gpointer
e_dbus_method_invocation_get_user_data (EDBusMethodInvocation *invocation)
{
  g_return_val_if_fail (G_IS_DBUS_METHOD_INVOCATION (invocation), NULL);
  return invocation->priv->user_data;
}

/**
 * e_dbus_method_invocation_new:
 * @sender: The bus name that invoked the method or %NULL if @connection is not a bus connection.
 * @object_path: The object path the method was invoked on.
 * @interface_name: The name of the D-Bus interface the method was invoked on.
 * @method_name: The name of the method that was invoked.
 * @connection: The #EDBusConnection the method was invoked on.
 * @parameters: The parameters as a #EVariant tuple.
 * @user_data: The @user_data #gpointer passed to e_dbus_connection_register_object().
 *
 * Creates a new #EDBusMethodInvocation object.
 *
 * Returns: A #EDBusMethodInvocation. Free with g_object_unref().
 */
EDBusMethodInvocation *
e_dbus_method_invocation_new (const gchar      *sender,
                              const gchar      *object_path,
                              const gchar      *interface_name,
                              const gchar      *method_name,
                              EDBusConnection  *connection,
                              EVariant         *parameters,
                              gpointer          user_data)
{
  g_return_val_if_fail (object_path != NULL, NULL);
  g_return_val_if_fail (interface_name != NULL, NULL);
  g_return_val_if_fail (method_name != NULL, NULL);
  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);
  g_return_val_if_fail (parameters != NULL, NULL);
  g_return_val_if_fail ((e_dbus_connection_get_bus_type (connection) == G_BUS_TYPE_NONE && sender == NULL) ||
                        (e_dbus_connection_get_bus_type (connection) != G_BUS_TYPE_NONE && sender != NULL), NULL);

  return E_DBUS_METHOD_INVOCATION (g_object_new (E_TYPE_DBUS_METHOD_INVOCATION,
                                                 "sender", sender,
                                                 "object-path", object_path,
                                                 "interface-name", interface_name,
                                                 "method-name", method_name,
                                                 "connection", connection,
                                                 "parameters", parameters,
                                                 "user-data", user_data,
                                                 NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * e_dbus_method_invocation_return_value:
 * @invocation: A #EDBusMethodInvocation.
 * @parameters: A #EVariant tuple with out parameters for the method or %NULL if not passing any parameters.
 *
 * Finishes handling a D-Bus method call by returning @parameters.
 *
 * It is an error if @parameters is not of the right format.
 *
 * This method will free @invocation, you cannot use it afterwards.
 */
void
e_dbus_method_invocation_return_value (EDBusMethodInvocation *invocation,
                                       EVariant              *parameters)
{
  DBusMessage *message;
  EDBusMethodInfo *method_info;
  DBusMessage *reply;
  GError *error;

  g_return_if_fail (G_IS_DBUS_METHOD_INVOCATION (invocation));
  g_return_if_fail ((parameters == NULL) || (e_variant_get_type_class (parameters) == E_VARIANT_CLASS_TUPLE));

  message = g_object_get_data (G_OBJECT (invocation), "dbus-1-message");
  g_assert (message != NULL);

  if (parameters != NULL)
    e_variant_ref_sink (parameters);

  /* if we have introspection data, check that the signature of @parameters is correct */
  method_info = g_object_get_data (G_OBJECT (invocation), "g-dbus-method-info");
  if (method_info != NULL)
    {
      gboolean pass;

      pass = FALSE;
      if (parameters == NULL)
        {
          if (g_strcmp0 (method_info->out_signature, "") == 0)
            pass = TRUE;
        }
      else
        {
          const gchar *type_string;
          gchar *sig_with_paren;

          /* this can probably be done a lot more efficiently */
          sig_with_paren = g_strdup_printf ("(%s)", method_info->out_signature);
          type_string = e_variant_get_type_string (parameters);
          if (g_strcmp0 (sig_with_paren, type_string) == 0)
            pass = TRUE;
          g_free (sig_with_paren);
        }

      if (!pass)
        {
          g_warning (_("Type of return value is incorrect, expected type `(%s)'"),
                     method_info->out_signature);
          goto out;
        }
    }

  reply = dbus_message_new_method_return (message);
  error = NULL;
  if (parameters != NULL)
    {
      if (!_e_dbus_gvariant_to_dbus_1 (reply,
                                       parameters,
                                       &error))
        {
          g_warning ("Error serializing return value to D-Bus format: %s", error->message);
          g_error_free (error);
          goto out;
        }
    }

  _e_dbus_connection_send_dbus_1_message (e_dbus_method_invocation_get_connection (invocation), reply);
  dbus_message_unref (reply);

 out:
  g_object_unref (invocation);
  if (parameters != NULL)
    e_variant_unref (parameters);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * e_dbus_method_invocation_return_error:
 * @invocation: A #EDBusMethodInvocation.
 * @domain: A #GQuark for the #GError error domain.
 * @code: The error code.
 * @format: printf()-style format.
 * @...: Parameters for @format.
 *
 * Finishes handling a D-Bus method call by returning an error.
 *
 * See e_dbus_error_encode_gerror() for details about what error name
 * will be returned on the wire. In a nutshell, if the given error is
 * registered using e_dbus_error_register_error() the name given
 * during registration is used. Otherwise, a name of the form
 * <literal>org.gtk.EDBus.UnmappedGError.Quark0x...</literal> is
 * used. This provides transparent mapping of #GError between
 * applications using EDBus.
 *
 * If you are writing an application intended to be portable,
 * <emphasis>always</emphasis> register errors with e_dbus_error_register_error()
 * or use e_dbus_method_invocation_return_dbus_error().
 *
 * This method will free @invocation, you cannot use it afterwards.
 */
void
e_dbus_method_invocation_return_error (EDBusMethodInvocation *invocation,
                                       GQuark                 domain,
                                       gint                   code,
                                       const gchar           *format,
                                       ...)
{
  va_list var_args;

  g_return_if_fail (G_IS_DBUS_METHOD_INVOCATION (invocation));
  g_return_if_fail (format != NULL);

  va_start (var_args, format);
  e_dbus_method_invocation_return_error_valist (invocation,
                                                domain,
                                                code,
                                                format,
                                                var_args);
  va_end (var_args);
}

/**
 * e_dbus_method_invocation_return_error_valist:
 * @invocation: A #EDBusMethodInvocation.
 * @domain: A #GQuark for the #GError error domain.
 * @code: The error code.
 * @format: printf()-style format.
 * @var_args: #va_list of parameters for @format.
 *
 * Like e_dbus_method_invocation_return_error() but intended for
 * language bindings.
 *
 * This method will free @invocation, you cannot use it afterwards.
 */
void
e_dbus_method_invocation_return_error_valist (EDBusMethodInvocation *invocation,
                                              GQuark                 domain,
                                              gint                   code,
                                              const gchar           *format,
                                              va_list                var_args)
{
  gchar *literal_message;

  g_return_if_fail (G_IS_DBUS_METHOD_INVOCATION (invocation));
  g_return_if_fail (format != NULL);

  literal_message = g_strdup_vprintf (format, var_args);
  e_dbus_method_invocation_return_error_literal (invocation,
                                                 domain,
                                                 code,
                                                 literal_message);
  g_free (literal_message);
}

/**
 * e_dbus_method_invocation_return_error_literal:
 * @invocation: A #EDBusMethodInvocation.
 * @domain: A #GQuark for the #GError error domain.
 * @code: The error code.
 * @message: The error message.
 *
 * Like e_dbus_method_invocation_return_error() but without printf()-style formatting.
 *
 * This method will free @invocation, you cannot use it afterwards.
 */
void
e_dbus_method_invocation_return_error_literal (EDBusMethodInvocation *invocation,
                                               GQuark                 domain,
                                               gint                   code,
                                               const gchar           *message)
{
  GError *error;

  g_return_if_fail (G_IS_DBUS_METHOD_INVOCATION (invocation));
  g_return_if_fail (message != NULL);

  error = g_error_new_literal (domain, code, message);
  e_dbus_method_invocation_return_gerror (invocation, error);
  g_error_free (error);
}

/**
 * e_dbus_method_invocation_return_gerror:
 * @invocation: A #EDBusMethodInvocation.
 * @error: A #GError.
 *
 * Like e_dbus_method_invocation_return_error() but takes a #GError
 * instead of the error domain, error code and message.
 *
 * This method will free @invocation, you cannot use it afterwards.
 */
void
e_dbus_method_invocation_return_gerror (EDBusMethodInvocation *invocation,
                                        const GError          *error)
{
  gchar *dbus_error_name;

  g_return_if_fail (G_IS_DBUS_METHOD_INVOCATION (invocation));
  g_return_if_fail (error != NULL);

  dbus_error_name = e_dbus_error_encode_gerror (error);

  e_dbus_method_invocation_return_dbus_error (invocation,
                                              dbus_error_name,
                                              error->message);
  g_free (dbus_error_name);
}

/**
 * e_dbus_method_invocation_return_dbus_error:
 * @invocation: A #EDBusMethodInvocation.
 * @error_name: A valid D-Bus error name.
 * @error_message: A valid D-Bus error message.
 *
 * Finishes handling a D-Bus method call by returning an error.
 *
 * This method will free @invocation, you cannot use it afterwards.
 */
void
e_dbus_method_invocation_return_dbus_error (EDBusMethodInvocation *invocation,
                                            const gchar           *error_name,
                                            const gchar           *error_message)
{
  DBusMessage *message;
  DBusMessage *reply;

  g_return_if_fail (G_IS_DBUS_METHOD_INVOCATION (invocation));
  g_return_if_fail (error_name != NULL);
  g_return_if_fail (error_message != NULL);

  message = g_object_get_data (G_OBJECT (invocation), "dbus-1-message");
  g_assert (message != NULL);

  reply = dbus_message_new_error (message,
                                  error_name,
                                  error_message);
  _e_dbus_connection_send_dbus_1_message (e_dbus_method_invocation_get_connection (invocation), reply);
  dbus_message_unref (reply);

  g_object_unref (invocation);
}
