#include <edbus/edbus.h>

static gchar *opt_name         = NULL;
static gchar *opt_object_path  = NULL;
static gchar *opt_interface    = NULL;
static gboolean opt_system_bus = FALSE;

static GOptionEntry opt_entries[] =
{
  { "name", 'n', 0, G_OPTION_ARG_STRING, &opt_name, "Name of the remote object to watch", NULL },
  { "object-path", 'o', 0, G_OPTION_ARG_STRING, &opt_object_path, "Object path of the remote object", NULL },
  { "interface", 'i', 0, G_OPTION_ARG_STRING, &opt_interface, "D-Bus interface of remote object", NULL },
  { "system-bus", 's', 0, G_OPTION_ARG_NONE, &opt_system_bus, "Use the system-bus instead of the session-bus", NULL },
  { NULL}
};

static void
print_properties (EDBusProxy *proxy)
{
  gchar **property_names;
  guint n;

  g_print ("    properties:\n");

  property_names = e_dbus_proxy_get_cached_property_names (proxy, NULL);
  for (n = 0; property_names != NULL && property_names[n] != NULL; n++)
    {
      const gchar *key = property_names[n];
      EVariant *value;
      gchar *value_str;
      value = e_dbus_proxy_get_cached_property (proxy, key, NULL);
      value_str = e_variant_print (value, TRUE);
      g_print ("      %s -> %s\n", key, value_str);
      e_variant_unref (value);
      g_free (value_str);
    }
  g_strfreev (property_names);
}

static void
on_properties_changed (EDBusProxy *proxy,
                       GHashTable *changed_properties,
                       gpointer    user_data)
{
  GHashTableIter iter;
  const gchar *key;
  EVariant *value;

  g_print (" *** Properties Changed:\n");

  g_hash_table_iter_init (&iter, changed_properties);
  while (g_hash_table_iter_next (&iter, (gpointer) &key, (gpointer) &value))
    {
      gchar *value_str;
      value_str = e_variant_print (value, TRUE);
      g_print ("      %s -> %s\n", key, value_str);
      g_free (value_str);
    }
}

static void
on_signal (EDBusProxy *proxy,
           gchar      *sender_name,
           gchar      *signal_name,
           EVariant   *parameters,
           gpointer    user_data)
{
  gchar *parameters_str;

  parameters_str = e_variant_print (parameters, TRUE);
  g_print (" *** Received Signal: %s: %s\n",
           signal_name,
           parameters_str);
  g_free (parameters_str);
}

static void
on_proxy_appeared (EDBusConnection *connection,
                   const gchar     *name,
                   const gchar     *name_owner,
                   EDBusProxy      *proxy,
                   gpointer         user_data)
{
  g_print ("+++ Acquired proxy object for remote object owned by %s\n"
           "    bus:          %s\n"
           "    name:         %s\n"
           "    object path:  %s\n"
           "    interface:    %s\n",
           name_owner,
           opt_system_bus ? "System Bus" : "Session Bus",
           opt_name,
           opt_object_path,
           opt_interface);

  print_properties (proxy);

  g_signal_connect (proxy,
                    "g-properties-changed",
                    G_CALLBACK (on_properties_changed),
                    NULL);

  g_signal_connect (proxy,
                    "g-signal",
                    G_CALLBACK (on_signal),
                    NULL);
}

static void
on_proxy_vanished (EDBusConnection *connection,
                   const gchar     *name,
                   gpointer         user_data)
{
  g_print ("--- Cannot create proxy object for\n"
           "    bus:          %s\n"
           "    name:         %s\n"
           "    object path:  %s\n"
           "    interface:    %s\n",
           opt_system_bus ? "System Bus" : "Session Bus",
           opt_name,
           opt_object_path,
           opt_interface);
}

int
main (int argc, char *argv[])
{
  guint watcher_id;
  GMainLoop *loop;
  GOptionContext *opt_context;
  GError *error;

  g_type_init ();

  opt_context = g_option_context_new ("e_bus_watch_proxy() example");
  g_option_context_set_summary (opt_context,
                                "Example: to watch the object of example-server, use:\n"
                                "\n"
                                "  ./example-watch-proxy -n org.gtk.EDBus.TestServer  \\\n"
                                "                        -o /org/gtk/EDBus/TestObject \\\n"
                                "                        -i org.gtk.EDBus.TestInterface");
  g_option_context_add_main_entries (opt_context, opt_entries, NULL);
  error = NULL;
  if (!g_option_context_parse (opt_context, &argc, &argv, &error))
    {
      g_printerr ("Error parsing options: %s", error->message);
      goto out;
    }
  if (opt_name == NULL || opt_object_path == NULL || opt_interface == NULL)
    {
      g_printerr ("Incorrect usage, try --help.\n");
      goto out;
    }

  watcher_id = e_bus_watch_proxy (opt_system_bus ? G_BUS_TYPE_SYSTEM : G_BUS_TYPE_SESSION,
                                  opt_name,
                                  opt_object_path,
                                  opt_interface,
                                  E_TYPE_DBUS_PROXY,
                                  E_DBUS_PROXY_FLAGS_NONE,
                                  on_proxy_appeared,
                                  on_proxy_vanished,
                                  NULL,
                                  NULL);

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  e_bus_unwatch_proxy (watcher_id);

 out:
  g_option_context_free (opt_context);
  g_free (opt_name);
  g_free (opt_object_path);
  g_free (opt_interface);

  return 0;
}
