/* GLib testing framework examples and tests
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

#include <edbus/edbus.h>
#include <unistd.h>
#include <string.h>

#include "tests.h"

/* all tests rely on a shared mainloop */
static GMainLoop *loop = NULL;

/* ---------------------------------------------------------------------------------------------------- */
/* Test that the method aspects of EDBusProxy works */
/* ---------------------------------------------------------------------------------------------------- */

static void
test_methods (EDBusConnection *connection,
              const gchar     *name,
              const gchar     *name_owner,
              EDBusProxy      *proxy)
{
  EVariant *result;
  GError *error;
  const gchar *str;
  gchar *dbus_error_name;

  /* check that we can invoke a method */
  error = NULL;
  result = e_dbus_proxy_invoke_method_sync (proxy,
                                            "HelloWorld",
                                            e_variant_new ("(s)", "Hey"),
                                            -1,
                                            NULL,
                                            &error);
  g_assert_no_error (error);
  g_assert (result != NULL);
  g_assert_cmpstr (e_variant_get_type_string (result), ==, "(s)");
  e_variant_get (result, "(s)", &str);
  g_assert_cmpstr (str, ==, "You greeted me with 'Hey'. Thanks!");
  e_variant_unref (result);

  /* Check that we can completely recover the returned error */
  result = e_dbus_proxy_invoke_method_sync (proxy,
                                            "HelloWorld",
                                            e_variant_new ("(s)", "Yo"),
                                            -1,
                                            NULL,
                                            &error);
  g_assert_error (error, E_DBUS_ERROR, E_DBUS_ERROR_REMOTE_ERROR);
  g_assert (e_dbus_error_is_remote_error (error));
  g_assert (e_dbus_error_is_remote_error (error));
  g_assert (result == NULL);
  dbus_error_name = e_dbus_error_get_remote_error (error);
  g_assert_cmpstr (dbus_error_name, ==, "com.example.TestException");
  g_free (dbus_error_name);
  g_assert (e_dbus_error_strip_remote_error (error));
  g_assert_cmpstr (error->message, ==, "Yo is not a proper greeting");
  g_clear_error (&error);

  /* Check that we get a timeout if the method handling is taking longer than timeout */
  error = NULL;
  result = e_dbus_proxy_invoke_method_sync (proxy,
                                            "Sleep",
                                            e_variant_new ("(i)", 500 /* msec */),
                                            100 /* msec */,
                                            NULL,
                                            &error);
  g_assert_error (error, E_DBUS_ERROR, E_DBUS_ERROR_NO_REPLY);
  g_assert (e_dbus_error_is_remote_error (error));
  g_assert (result == NULL);
  g_clear_error (&error);

  /* Check that proxy-default timeouts work. */
  g_assert_cmpint (e_dbus_proxy_get_default_timeout (proxy), ==, -1);

  /* the default timeout is 25000 msec so this should work */
  result = e_dbus_proxy_invoke_method_sync (proxy,
                                            "Sleep",
                                            e_variant_new ("(i)", 500 /* msec */),
                                            -1, /* use proxy default (e.g. -1 -> e.g. 25000 msec) */
                                            NULL,
                                            &error);
  g_assert_no_error (error);
  g_assert (result != NULL);
  g_assert_cmpstr (e_variant_get_type_string (result), ==, "()");
  e_variant_unref (result);

  /* now set the proxy-default timeout to 250 msec and try the 500 msec call - this should FAIL */
  e_dbus_proxy_set_default_timeout (proxy, 250);
  g_assert_cmpint (e_dbus_proxy_get_default_timeout (proxy), ==, 250);
  result = e_dbus_proxy_invoke_method_sync (proxy,
                                            "Sleep",
                                            e_variant_new ("(i)", 500 /* msec */),
                                            -1, /* use proxy default (e.g. 250 msec) */
                                            NULL,
                                            &error);
  g_assert_error (error, E_DBUS_ERROR, E_DBUS_ERROR_NO_REPLY);
  g_assert (e_dbus_error_is_remote_error (error));
  g_assert (result == NULL);
  g_clear_error (&error);

  /* clean up after ourselves */
  e_dbus_proxy_set_default_timeout (proxy, -1);
}

/* ---------------------------------------------------------------------------------------------------- */
/* Test that the property aspects of EDBusProxy works */
/* ---------------------------------------------------------------------------------------------------- */

static void
test_properties (EDBusConnection *connection,
                 const gchar     *name,
                 const gchar     *name_owner,
                 EDBusProxy      *proxy)
{
  GError *error;
  EVariant *variant;
  EVariant *variant2;
  EVariant *result;

  error = NULL;

  /**
   * Check that we can read cached properties.
   *
   * No need to test all properties - EVariant has already been tested
   */
  variant = e_dbus_proxy_get_cached_property (proxy, "y", &error);
  g_assert_no_error (error);
  g_assert (variant != NULL);
  g_assert_cmpint (e_variant_get_byte (variant), ==, 1);
  e_variant_unref (variant);
  variant = e_dbus_proxy_get_cached_property (proxy, "o", &error);
  g_assert_no_error (error);
  g_assert (variant != NULL);
  g_assert_cmpstr (e_variant_get_string (variant, NULL), ==, "/some/path");
  e_variant_unref (variant);

  /**
   * Now ask the service to change a property and check that #EDBusProxy::g-property-changed
   * is received. Also check that the cache is updated.
   */
  variant2 = e_variant_new_byte (42);
  result = e_dbus_proxy_invoke_method_sync (proxy,
                                            "FrobSetProperty",
                                            e_variant_new ("(sv)",
                                                           "y",
                                                           variant2),
                                            -1,
                                            NULL,
                                            &error);
  g_assert_no_error (error);
  g_assert (result != NULL);
  g_assert_cmpstr (e_variant_get_type_string (result), ==, "()");
  e_variant_unref (result);
  _g_assert_signal_received (proxy, "g-properties-changed");
  variant = e_dbus_proxy_get_cached_property (proxy, "y", &error);
  g_assert_no_error (error);
  g_assert (variant != NULL);
  g_assert_cmpint (e_variant_get_byte (variant), ==, 42);
  e_variant_unref (variant);
}

/* ---------------------------------------------------------------------------------------------------- */
/* Test that the signal aspects of EDBusProxy works */
/* ---------------------------------------------------------------------------------------------------- */

static void
test_proxy_signals_on_signal (EDBusProxy  *proxy,
                              const gchar *sender_name,
                              const gchar *signal_name,
                              EVariant    *parameters,
                              gpointer     user_data)
{
  GString *s = user_data;

  g_assert_cmpstr (signal_name, ==, "TestSignal");
  g_assert_cmpstr (e_variant_get_type_string (parameters), ==, "(sov)");

  e_variant_print_string (parameters, s, TRUE);
}

typedef struct
{
  GMainLoop *internal_loop;
  GString *s;
} TestSignalData;

static void
test_proxy_signals_on_emit_signal_cb (EDBusProxy   *proxy,
                                      GAsyncResult *res,
                                      gpointer      user_data)
{
  TestSignalData *data = user_data;
  GError *error;
  EVariant *result;

  error = NULL;
  result = e_dbus_proxy_invoke_method_finish (proxy,
                                              res,
                                              &error);
  g_assert_no_error (error);
  g_assert (result != NULL);
  g_assert_cmpstr (e_variant_get_type_string (result), ==, "()");
  e_variant_unref (result);

  /* check that the signal was recieved before we got the method result */
  g_assert (strlen (data->s->str) > 0);

  /* break out of the loop */
  g_main_loop_quit (data->internal_loop);
}

static void
test_signals (EDBusConnection *connection,
              const gchar     *name,
              const gchar     *name_owner,
              EDBusProxy      *proxy)
{
  GError *error;
  GString *s;
  gulong signal_handler_id;
  TestSignalData data;
  EVariant *result;

  error = NULL;

  /**
   * Ask the service to emit a signal and check that we receive it.
   *
   * Note that blocking calls don't block in the mainloop so wait for the signal (which
   * is dispatched before the method reply)
   */
  s = g_string_new (NULL);
  signal_handler_id = g_signal_connect (proxy,
                                        "g-signal",
                                        G_CALLBACK (test_proxy_signals_on_signal),
                                        s);

  result = e_dbus_proxy_invoke_method_sync (proxy,
                                            "EmitSignal",
                                            e_variant_new ("(so)",
                                                           "Accept the next proposition you hear",
                                                           "/some/path"),
                                            -1,
                                            NULL,
                                            &error);
  g_assert_no_error (error);
  g_assert (result != NULL);
  g_assert_cmpstr (e_variant_get_type_string (result), ==, "()");
  e_variant_unref (result);
  /* check that we haven't received the signal just yet */
  g_assert (strlen (s->str) == 0);
  /* and now wait for the signal */
  _g_assert_signal_received (proxy, "g-signal");
  g_assert_cmpstr (s->str,
                   ==,
                   "(\"Accept the next proposition you hear .. in bed!\", objectpath \"/some/path/in/bed\", <\"a variant\">)");
  g_signal_handler_disconnect (proxy, signal_handler_id);
  g_string_free (s, TRUE);

  /**
   * Now do this async to check the signal is received before the method returns.
   */
  s = g_string_new (NULL);
  data.internal_loop = g_main_loop_new (NULL, FALSE);
  data.s = s;
  signal_handler_id = g_signal_connect (proxy,
                                        "g-signal",
                                        G_CALLBACK (test_proxy_signals_on_signal),
                                        s);
  e_dbus_proxy_invoke_method (proxy,
                              "EmitSignal",
                              e_variant_new ("(so)",
                                             "You will make a great programmer",
                                             "/some/other/path"),
                              -1,
                              NULL,
                              (GAsyncReadyCallback) test_proxy_signals_on_emit_signal_cb,
                              &data);
  g_main_loop_run (data.internal_loop);
  g_main_loop_unref (data.internal_loop);
  g_assert_cmpstr (s->str,
                   ==,
                   "(\"You will make a great programmer .. in bed!\", objectpath \"/some/other/path/in/bed\", <\"a variant\">)");
  g_signal_handler_disconnect (proxy, signal_handler_id);
  g_string_free (s, TRUE);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_proxy_appeared (EDBusConnection *connection,
                   const gchar     *name,
                   const gchar     *name_owner,
                   EDBusProxy      *proxy,
                   gpointer         user_data)
{
  test_methods (connection, name, name_owner, proxy);
  test_properties (connection, name, name_owner, proxy);
  test_signals (connection, name, name_owner, proxy);

  g_main_loop_quit (loop);
}

static void
on_proxy_vanished (EDBusConnection *connection,
                   const gchar     *name,
                   gpointer         user_data)
{
}

static void
test_proxy (void)
{
  guint watcher_id;

  session_bus_up ();

  /* TODO: wait a bit for the bus to come up.. ideally session_bus_up() won't return
   * until one can connect to the bus but that's not how things work right now
   */
  usleep (500 * 1000);

  watcher_id = e_bus_watch_proxy (G_BUS_TYPE_SESSION,
                                  "com.example.TestService",
                                  "/com/example/TestObject",
                                  "com.example.Frob",
                                  E_TYPE_DBUS_PROXY,
                                  E_DBUS_PROXY_FLAGS_NONE,
                                  on_proxy_appeared,
                                  on_proxy_vanished,
                                  NULL,
                                  NULL);

  /* this is safe; testserver will exit once the bus goes away */
  g_assert (g_spawn_command_line_async ("./testserver.py", NULL));

  g_main_loop_run (loop);

  e_bus_unwatch_proxy (watcher_id);

  /* tear down bus */
  session_bus_down ();
}

/* ---------------------------------------------------------------------------------------------------- */

int
main (int   argc,
      char *argv[])
{
  g_type_init ();
  g_test_init (&argc, &argv, NULL);

  /* all the tests rely on a shared main loop */
  loop = g_main_loop_new (NULL, FALSE);

  /* all the tests use a session bus with a well-known address that we can bring up and down
   * using session_bus_up() and session_bus_down().
   */
  g_unsetenv ("DISPLAY");
  g_setenv ("DBUS_SESSION_BUS_ADDRESS", session_bus_get_temporary_address (), TRUE);

  g_test_add_func ("/gdbus/proxy", test_proxy);
  return g_test_run();
}
