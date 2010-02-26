
#include <edbus/edbus.h>
#include <stdlib.h>

/* ---------------------------------------------------------------------------------------------------- */

static EDBusNodeInfo *introspection_data = NULL;

/* Introspection data for the service we are exporting */
static const gchar introspection_xml[] =
  "<node>"
  "  <interface name='org.gtk.EDBus.TestInterface'>"
  "    <method name='HelloWorld'>"
  "      <arg type='s' name='greeting' direction='in'/>"
  "      <arg type='s' name='response' direction='out'/>"
  "    </method>"
  "    <method name='EmitSignal'>"
  "      <arg type='d' name='speed_in_mph' direction='in'/>"
  "    </method>"
  "    <signal name='VelocityChanged'>"
  "      <arg type='d' name='speed_in_mph'/>"
  "      <arg type='s' name='speed_as_string'/>"
  "    </signal>"
  "    <property type='s' name='FluxCapicitorName' access='read'/>"
  "    <property type='s' name='Title' access='readwrite'/>"
  "    <property type='s' name='ReadingAlwaysThrowsError' access='read'/>"
  "    <property type='s' name='WritingAlwaysThrowsError' access='readwrite'/>"
  "    <property type='s' name='OnlyWritable' access='write'/>"
  "    <property type='s' name='Foo' access='read'/>"
  "    <property type='s' name='Bar' access='read'/>"
  "  </interface>"
  "</node>";

/* ---------------------------------------------------------------------------------------------------- */

static void
handle_method_call (EDBusConnection       *connection,
                    gpointer               user_data,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    EVariant              *parameters,
                    EDBusMethodInvocation *invocation)
{
  if (g_strcmp0 (method_name, "HelloWorld") == 0)
    {
      const gchar *greeting;

      e_variant_get (parameters, "(s)", &greeting);

      if (g_strcmp0 (greeting, "Return Unregistered") == 0)
        {
          e_dbus_method_invocation_return_error (invocation,
                                                 G_IO_ERROR,
                                                 G_IO_ERROR_FAILED_HANDLED,
                                                 "As requested, here's a GError not registered (G_IO_ERROR_FAILED_HANDLED)");
        }
      else if (g_strcmp0 (greeting, "Return Registered") == 0)
        {
          e_dbus_method_invocation_return_error (invocation,
                                                 E_DBUS_ERROR,
                                                 E_DBUS_ERROR_MATCH_RULE_NOT_FOUND,
                                                 "As requested, here's a GError that is registered (E_DBUS_ERROR_MATCH_RULE_NOT_FOUND)");
        }
      else if (g_strcmp0 (greeting, "Return Raw") == 0)
        {
          e_dbus_method_invocation_return_dbus_error (invocation,
                                                      "org.gtk.EDBus.SomeErrorName",
                                                      "As requested, here's a raw D-Bus error");
        }
      else
        {
          gchar *response;
          response = g_strdup_printf ("You greeted me with '%s'. Thanks!", greeting);
          e_dbus_method_invocation_return_value (invocation,
                                                 e_variant_new ("(s)", response));
          g_free (response);
        }
    }
  else if (g_strcmp0 (method_name, "EmitSignal") == 0)
    {
      GError *local_error;
      gdouble speed_in_mph;
      gchar *speed_as_string;

      e_variant_get (parameters, "(d)", &speed_in_mph);
      speed_as_string = g_strdup_printf ("%g mph!", speed_in_mph);

      local_error = NULL;
      e_dbus_connection_emit_signal (connection,
                                     NULL,
                                     object_path,
                                     interface_name,
                                     "VelocityChanged",
                                     e_variant_new ("(ds)",
                                                    speed_in_mph,
                                                    speed_as_string),
                                     &local_error);
      g_assert_no_error (local_error);
      g_free (speed_as_string);

      e_dbus_method_invocation_return_value (invocation, NULL);
    }
}

static gchar *_global_title = NULL;

static gboolean swap_a_and_b = FALSE;

static EVariant *
handle_get_property (EDBusConnection  *connection,
                     gpointer          user_data,
                     const gchar      *sender,
                     const gchar      *object_path,
                     const gchar      *interface_name,
                     const gchar      *property_name,
                     GError          **error)
{
  EVariant *ret;

  ret = NULL;
  if (g_strcmp0 (property_name, "FluxCapicitorName") == 0)
    {
      ret = e_variant_new_string ("DeLorean");
    }
  else if (g_strcmp0 (property_name, "Title") == 0)
    {
      if (_global_title == NULL)
        _global_title = g_strdup ("Back To C!");
      ret = e_variant_new_string (_global_title);
    }
  else if (g_strcmp0 (property_name, "ReadingAlwaysThrowsError") == 0)
    {
      g_set_error (error,
                   E_DBUS_ERROR,
                   E_DBUS_ERROR_FAILED,
                   "Hello %s. I thought I said reading this property "
                   "always results in an error. kthxbye",
                   sender);
    }
  else if (g_strcmp0 (property_name, "WritingAlwaysThrowsError") == 0)
    {
      ret = e_variant_new_string ("There's no home like home");
    }
  else if (g_strcmp0 (property_name, "Foo") == 0)
    {
      ret = e_variant_new_string (swap_a_and_b ? "Tock" : "Tick");
    }
  else if (g_strcmp0 (property_name, "Bar") == 0)
    {
      ret = e_variant_new_string (swap_a_and_b ? "Tick" : "Tock");
    }

  return ret;
}

static gboolean
handle_set_property (EDBusConnection  *connection,
                     gpointer          user_data,
                     const gchar      *sender,
                     const gchar      *object_path,
                     const gchar      *interface_name,
                     const gchar      *property_name,
                     EVariant         *value,
                     GError          **error)
{
  if (g_strcmp0 (property_name, "Title") == 0)
    {
      if (g_strcmp0 (_global_title, e_variant_get_string (value, NULL)) != 0)
        {
          EVariantBuilder *builder;
          GError *local_error;

          g_free (_global_title);
          _global_title = e_variant_dup_string (value, NULL);

          local_error = NULL;
          builder = e_variant_builder_new (E_VARIANT_TYPE_ARRAY);
          e_variant_builder_add (builder,
                                 "{sv}",
                                 "Title",
                                 e_variant_new_string (_global_title));
          e_dbus_connection_emit_signal (connection,
                                         NULL,
                                         object_path,
                                         "org.freedesktop.DBus.Properties",
                                         "PropertiesChanged",
                                         e_variant_new ("(sa{sv})",
                                                        interface_name,
                                                        builder),
                                         &local_error);
          g_assert_no_error (local_error);
        }
    }
  else if (g_strcmp0 (property_name, "ReadingAlwaysThrowsError") == 0)
    {
      /* do nothing - they can't read it after all! */
    }
  else if (g_strcmp0 (property_name, "WritingAlwaysThrowsError") == 0)
    {
      g_set_error (error,
                   E_DBUS_ERROR,
                   E_DBUS_ERROR_FAILED,
                   "Hello AGAIN %s. I thought I said writing this property "
                   "always results in an error. kthxbye",
                   sender);
    }

  return *error == NULL;
}


/* for now */
static const EDBusInterfaceVTable interface_vtable =
{
  handle_method_call,
  handle_get_property,
  handle_set_property
};

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_timeout_cb (gpointer user_data)
{
  EDBusConnection *connection = E_DBUS_CONNECTION (user_data);
  EVariantBuilder *builder;
  GError *error;

  swap_a_and_b = !swap_a_and_b;

  error = NULL;
  builder = e_variant_builder_new (E_VARIANT_TYPE_ARRAY);
  e_variant_builder_add (builder,
                         "{sv}",
                         "Foo",
                         e_variant_new_string (swap_a_and_b ? "Tock" : "Tick"));
  e_variant_builder_add (builder,
                         "{sv}",
                         "Bar",
                         e_variant_new_string (swap_a_and_b ? "Tick" : "Tock"));
  e_dbus_connection_emit_signal (connection,
                                 NULL,
                                 "/org/gtk/EDBus/TestObject",
                                 "org.freedesktop.DBus.Properties",
                                 "PropertiesChanged",
                                 e_variant_new ("(sa{sv})",
                                                "org.gtk.EDBus.TestInterface",
                                                builder),
                                 &error);
  g_assert_no_error (error);


  return TRUE;
}

static void
on_name_acquired (EDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  guint registration_id;

  registration_id = e_dbus_connection_register_object (connection,
                                                       "/org/gtk/EDBus/TestObject",
                                                       "org.gtk.EDBus.TestInterface",
                                                       &introspection_data->interfaces[0],
                                                       &interface_vtable,
                                                       NULL,  /* user_data */
                                                       NULL,  /* user_data_free_func */
                                                       NULL); /* GError** */
  g_assert (registration_id > 0);

  /* swap value of properties Foo and Bar every two seconds */
  g_timeout_add_seconds (2,
                         on_timeout_cb,
                         connection);
}

static void
on_name_lost (EDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  exit (1);
}

int
main (int argc, char *argv[])
{
  guint owner_id;
  GMainLoop *loop;

  g_type_init ();

  /* We are lazy here - we don't want to manually provide
   * the introspection data structures - so we just build
   * them from XML.
   */
  introspection_data = e_dbus_node_info_new_for_xml (introspection_xml, NULL);
  g_assert (introspection_data != NULL);

  owner_id = e_bus_own_name (G_BUS_TYPE_SESSION,
                             "org.gtk.EDBus.TestServer",
                             G_BUS_NAME_OWNER_FLAGS_NONE,
                             on_name_acquired,
                             on_name_lost,
                             NULL,
                             NULL);

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  e_bus_unown_name (owner_id);

  e_dbus_node_info_free (introspection_data);

  return 0;
}
