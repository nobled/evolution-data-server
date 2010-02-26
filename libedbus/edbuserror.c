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

#include "gdbuserror.h"
#include "gdbusenums.h"
#include "gdbusenumtypes.h"
#include "gdbusprivate.h"

/**
 * SECTION:gdbuserror
 * @title: EDBusError
 * @short_description: Error helper functions
 * @include: gdbus/gdbus.h
 *
 * Error helper functions for EDBus.
 *
 * All facilities in EDBus (such as e_dbus_connection_invoke_method_sync())
 * that return errors as a result of remote method invocations returning
 * errors use #GError. To check if a returned #GError is the result of
 * a remote error, use e_dbus_error_is_remote_error(). To get
 * the actual D-Bus error name, use e_dbus_error_get_remote_error().
 *
 * In addition, facilities that are used to return errors to a remote
 * caller also use #GError. See e_dbus_method_invocation_return_error()
 * for discussion about how the D-Bus error name is set.
 *
 * Applications that receive and return errors typically wants to
 * associate a #GError domain with a set of D-Bus errors in order to
 * automatically map from wire-format errors to #GError and back. This
 * is typically done in the function returning the #GQuark for the
 * error domain:
 * <programlisting>
 * #define FOO_BAR_ERROR (foo_bar_error_quark ())
 *
 * [...]
 *
 * typedef enum
 * {
 *   FOO_BAR_ERROR_FAILED,
 *   FOO_BAR_ERROR_ANOTHER_ERROR,
 *   FOO_BAR_ERROR_SOME_THIRD_ERROR,
 *   FOO_BAR_ERROR_MAX_ERROR_NUM      /<!-- -->*< skip >*<!-- -->/
 * } FooBarError;
 *
 * [...]
 *
 * GQuark
 * foo_bar_error_quark (void)
 * {
 *   static volatile gsize quark_volatile = 0;
 *
 *   if (g_once_init_enter (&quark_volatile))
 *     {
 *       guint n;
 *       GQuark quark;
 *       static const struct
 *       {
 *         gint error_code;
 *         const gchar *dbus_error_name;
 *       } error_mapping[] =
 *           {
 *             {FOO_BAR_ERROR_FAILED,           "org.project.Foo.Bar.Error.Failed"},
 *             {FOO_BAR_ERROR_ANOTHER_ERROR,    "org.project.Foo.Bar.Error.AnotherError"},
 *             {FOO_BAR_ERROR_SOME_THIRD_ERROR, "org.project.Foo.Bar.Error.SomeThirdError"},
 *             {-1, NULL}
 *           };
 *
 *       quark = g_quark_from_static_string ("foo-bar-error-quark");
 *
 *       for (n = 0; error_mapping[n].dbus_error_name != NULL; n++)
 *         {
 *           g_assert (e_dbus_error_register_error (quark, /<!-- -->* Can't use FOO_BAR_ERROR here because of reentrancy *<!-- -->/
 *                                                  error_mapping[n].error_code,
 *                                                  error_mapping[n].dbus_error_name));
 *         }
 *       g_assert (n == FOO_BAR_ERROR_MAX_ERROR_NUM);
 *       g_once_init_leave (&quark_volatile, quark);
 *     }
 *
 *   return (GQuark) quark_volatile;
 * }
 * </programlisting>
 * With this setup, a server can transparently pass e.g. %FOO_BAR_ERROR_ANOTHER_ERROR and
 * clients will see the D-Bus error name <literal>org.project.Foo.Bar.Error.AnotherError</literal>.
 * If the client is using EDBus, the client will see also %FOO_BAR_ERROR_ANOTHER_ERROR instead
 * of %E_DBUS_ERROR_REMOTE_ERROR. Note that EDBus clients can still recover
 * <literal>org.project.Foo.Bar.Error.AnotherError</literal> using e_dbus_error_get_remote_error().
 */

/**
 * e_dbus_error_quark:
 *
 * Gets the EDBus Error Quark.
 *
 * Return value: a #GQuark.
 **/
GQuark
e_dbus_error_quark (void)
{
  static volatile gsize quark_volatile = 0;

  if (g_once_init_enter (&quark_volatile))
    {
      guint n;
      GQuark quark;
      static const struct
      {
        gint error_code;
        const gchar *dbus_error_name;
      } error_mapping[] =
          {
            {E_DBUS_ERROR_DBUS_FAILED,                      "org.freedesktop.DBus.Error.Failed"},
            {E_DBUS_ERROR_NO_MEMORY,                        "org.freedesktop.DBus.Error.NoMemory"},
            {E_DBUS_ERROR_SERVICE_UNKNOWN,                  "org.freedesktop.DBus.Error.ServiceUnknown"},
            {E_DBUS_ERROR_NAME_HAS_NO_OWNER,                "org.freedesktop.DBus.Error.NameHasNoOwner"},
            {E_DBUS_ERROR_NO_REPLY,                         "org.freedesktop.DBus.Error.NoReply"},
            {E_DBUS_ERROR_IO_ERROR,                         "org.freedesktop.DBus.Error.IOError"},
            {E_DBUS_ERROR_BAD_ADDRESS,                      "org.freedesktop.DBus.Error.BadAddress"},
            {E_DBUS_ERROR_NOT_SUPPORTED,                    "org.freedesktop.DBus.Error.NotSupported"},
            {E_DBUS_ERROR_LIMITS_EXCEEDED,                  "org.freedesktop.DBus.Error.LimitsExceeded"},
            {E_DBUS_ERROR_ACCESS_DENIED,                    "org.freedesktop.DBus.Error.AccessDenied"},
            {E_DBUS_ERROR_AUTH_FAILED,                      "org.freedesktop.DBus.Error.AuthFailed"},
            {E_DBUS_ERROR_NO_SERVER,                        "org.freedesktop.DBus.Error.NoServer"},
            {E_DBUS_ERROR_TIMEOUT,                          "org.freedesktop.DBus.Error.Timeout"},
            {E_DBUS_ERROR_NO_NETWORK,                       "org.freedesktop.DBus.Error.NoNetwork"},
            {E_DBUS_ERROR_ADDRESS_IN_USE,                   "org.freedesktop.DBus.Error.AddressInUse"},
            {E_DBUS_ERROR_DISCONNECTED,                     "org.freedesktop.DBus.Error.Disconnected"},
            {E_DBUS_ERROR_INVALID_ARGS,                     "org.freedesktop.DBus.Error.InvalidArgs"},
            {E_DBUS_ERROR_FILE_NOT_FOUND,                   "org.freedesktop.DBus.Error.FileNotFound"},
            {E_DBUS_ERROR_FILE_EXISTS,                      "org.freedesktop.DBus.Error.FileExists"},
            {E_DBUS_ERROR_UNKNOWN_METHOD,                   "org.freedesktop.DBus.Error.UnknownMethod"},
            {E_DBUS_ERROR_TIMED_OUT,                        "org.freedesktop.DBus.Error.TimedOut"},
            {E_DBUS_ERROR_MATCH_RULE_NOT_FOUND,             "org.freedesktop.DBus.Error.MatchRuleNotFound"},
            {E_DBUS_ERROR_MATCH_RULE_INVALID,               "org.freedesktop.DBus.Error.MatchRuleInvalid"},
            {E_DBUS_ERROR_SPAWN_EXEC_FAILED,                "org.freedesktop.DBus.Error.Spawn.ExecFailed"},
            {E_DBUS_ERROR_SPAWN_FORK_FAILED,                "org.freedesktop.DBus.Error.Spawn.ForkFailed"},
            {E_DBUS_ERROR_SPAWN_CHILD_EXITED,               "org.freedesktop.DBus.Error.Spawn.ChildExited"},
            {E_DBUS_ERROR_SPAWN_CHILD_SIGNALED,             "org.freedesktop.DBus.Error.Spawn.ChildSignaled"},
            {E_DBUS_ERROR_SPAWN_FAILED,                     "org.freedesktop.DBus.Error.Spawn.Failed"},
            {E_DBUS_ERROR_SPAWN_SETUP_FAILED,               "org.freedesktop.DBus.Error.Spawn.FailedToSetup"},
            {E_DBUS_ERROR_SPAWN_CONFIG_INVALID,             "org.freedesktop.DBus.Error.Spawn.ConfigInvalid"},
            {E_DBUS_ERROR_SPAWN_SERVICE_INVALID,            "org.freedesktop.DBus.Error.Spawn.ServiceNotValid"},
            {E_DBUS_ERROR_SPAWN_SERVICE_NOT_FOUND,          "org.freedesktop.DBus.Error.Spawn.ServiceNotFound"},
            {E_DBUS_ERROR_SPAWN_PERMISSIONS_INVALID,        "org.freedesktop.DBus.Error.Spawn.PermissionsInvalid"},
            {E_DBUS_ERROR_SPAWN_FILE_INVALID,               "org.freedesktop.DBus.Error.Spawn.FileInvalid"},
            {E_DBUS_ERROR_SPAWN_NO_MEMORY,                  "org.freedesktop.DBus.Error.Spawn.NoMemory"},
            {E_DBUS_ERROR_UNIX_PROCESS_ID_UNKNOWN,          "org.freedesktop.DBus.Error.UnixProcessIdUnknown"},
            {E_DBUS_ERROR_INVALID_SIGNATURE,                "org.freedesktop.DBus.Error.InvalidSignature"},
            {E_DBUS_ERROR_INVALID_FILE_CONTENT,             "org.freedesktop.DBus.Error.InvalidFileContent"},
            {E_DBUS_ERROR_SELINUX_SECURITY_CONTEXT_UNKNOWN, "org.freedesktop.DBus.Error.SELinuxSecurityContextUnknown"},
            {E_DBUS_ERROR_ADT_AUDIT_DATA_UNKNOWN,           "org.freedesktop.DBus.Error.AdtAuditDataUnknown"},
            {E_DBUS_ERROR_OBJECT_PATH_IN_USE,               "org.freedesktop.DBus.Error.ObjectPathInUse"},
            {-1, NULL}
          };

      quark = g_quark_from_static_string ("g-dbus-error-quark");

      for (n = 0; error_mapping[n].dbus_error_name != NULL; n++)
        {
          g_assert (e_dbus_error_register_error (quark, /* Can't use E_DBUS_ERROR here because of reentrancy ;-) */
                                                 error_mapping[n].error_code,
                                                 error_mapping[n].dbus_error_name));
        }
      g_assert (n == _E_DBUS_ERROR_MAX_DBUS_ERROR - 1000);

      g_once_init_leave (&quark_volatile, quark);
    }

  return (GQuark) quark_volatile;
}

static gboolean
_e_dbus_error_decode_gerror (const gchar *dbus_name,
                             GQuark      *out_error_domain,
                             gint        *out_error_code)
{
  gboolean ret;
  guint n;
  GString *s;
  gchar *domain_quark_string;

  ret = FALSE;
  s = NULL;

  if (g_str_has_prefix (dbus_name, "org.gtk.EDBus.UnmappedGError.Quark0x"))
    {
      s = g_string_new (NULL);

      for (n = sizeof "org.gtk.EDBus.UnmappedGError.Quark0x" - 1;
           dbus_name[n] != '.' && dbus_name[n] != '\0';
           n++)
        {
          guint nibble_top;
          guint nibble_bottom;

          nibble_top = dbus_name[n];
          if (nibble_top >= '0' && nibble_top <= '9')
            nibble_top -= '0';
          else if (nibble_top >= 'a' && nibble_top <= 'f')
            nibble_top -= ('a' - 10);
          else
            goto not_mapped;

          n++;

          nibble_bottom = dbus_name[n];
          if (nibble_bottom >= '0' && nibble_bottom <= '9')
            nibble_bottom -= '0';
          else if (nibble_bottom >= 'a' && nibble_bottom <= 'f')
            nibble_bottom -= ('a' - 10);
          else
            goto not_mapped;

          g_string_append_c (s, (nibble_top<<4) | nibble_bottom);

        }

      if (!g_str_has_prefix (dbus_name + n, ".Code"))
        goto not_mapped;

      domain_quark_string = g_string_free (s, FALSE);
      s = NULL;

      if (out_error_domain != NULL)
        *out_error_domain = g_quark_from_string (domain_quark_string);
      g_free (domain_quark_string);

      if (out_error_code != NULL)
        *out_error_code = atoi (dbus_name + n + sizeof ".Code" - 1);

      ret = TRUE;
    }

 not_mapped:

  if (s != NULL)
    g_string_free (s, TRUE);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GQuark error_domain;
  gint   error_code;
} QuarkCodePair;

static guint
quark_code_pair_hash_func (const QuarkCodePair *pair)
{
  gint val;
  val = pair->error_domain + pair->error_code;
  return g_int_hash (&val);
}

static gboolean
quark_code_pair_equal_func (const QuarkCodePair *a,
                            const QuarkCodePair *b)
{
  return (a->error_domain == b->error_domain) && (a->error_code == b->error_code);
}

typedef struct
{
  QuarkCodePair pair;
  gchar *dbus_error_name;
} RegisteredError;

static void
registered_error_free (RegisteredError *re)
{
  g_free (re->dbus_error_name);
  g_free (re);
}

G_LOCK_DEFINE_STATIC (error_lock);

/* maps from QuarkCodePair* -> RegisteredError* */
static GHashTable *quark_code_pair_to_re = NULL;

/* maps from gchar* -> RegisteredError* */
static GHashTable *dbus_error_name_to_re = NULL;

/**
 * e_dbus_error_register_error:
 * @error_domain: A #GQuark for a error domain.
 * @error_code: An error code.
 * @dbus_error_name: A D-Bus error name.
 *
 * Creates an association to map between @dbus_error_name and
 * #GError<!-- -->s specified by @error_domain and @error_code.
 *
 * This is typically done in the routine that returns the #GQuark for
 * an error domain.
 *
 * Returns: %TRUE unless an association already existed.
 */
gboolean
e_dbus_error_register_error (GQuark       error_domain,
                             gint         error_code,
                             const gchar *dbus_error_name)
{
  gboolean ret;
  QuarkCodePair pair;
  RegisteredError *re;

  g_return_val_if_fail (dbus_error_name != NULL, FALSE);

  ret = FALSE;

  G_LOCK (error_lock);

  if (quark_code_pair_to_re == NULL)
    {
      g_assert (dbus_error_name_to_re == NULL); /* check invariant */
      quark_code_pair_to_re = g_hash_table_new ((GHashFunc) quark_code_pair_hash_func,
                                                (GEqualFunc) quark_code_pair_equal_func);
      dbus_error_name_to_re = g_hash_table_new_full (g_str_hash,
                                                     g_str_equal,
                                                     NULL,
                                                     (GDestroyNotify) registered_error_free);
    }

  if (g_hash_table_lookup (dbus_error_name_to_re, dbus_error_name) != NULL)
    goto out;

  pair.error_domain = error_domain;
  pair.error_code = error_code;

  if (g_hash_table_lookup (quark_code_pair_to_re, &pair) != NULL)
    goto out;

  re = g_new0 (RegisteredError, 1);
  re->pair = pair;
  re->dbus_error_name = g_strdup (dbus_error_name);

  g_hash_table_insert (quark_code_pair_to_re, &(re->pair), re);
  g_hash_table_insert (dbus_error_name_to_re, re->dbus_error_name, re);

  ret = TRUE;

 out:
  G_UNLOCK (error_lock);
  return ret;
}

/**
 * e_dbus_error_unregister_error:
 * @error_domain: A #GQuark for a error domain.
 * @error_code: An error code.
 * @dbus_error_name: A D-Bus error name.
 *
 * Destroys an association previously set up with e_dbus_error_register_error().
 *
 * Returns: %TRUE if the association was found and destroyed, %FALSE otherwise.
 */
gboolean
e_dbus_error_unregister_error (GQuark       error_domain,
                               gint         error_code,
                               const gchar *dbus_error_name)
{
  gboolean ret;
  RegisteredError *re;
  guint hash_size;

  ret = FALSE;

  G_LOCK (error_lock);

  if (dbus_error_name_to_re == NULL)
    {
      g_assert (quark_code_pair_to_re == NULL); /* check invariant */
      goto out;
    }

  re = g_hash_table_lookup (dbus_error_name_to_re, dbus_error_name);
  if (re == NULL)
    {
      QuarkCodePair pair;
      pair.error_domain = error_domain;
      pair.error_code = error_code;
      g_assert (g_hash_table_lookup (quark_code_pair_to_re, &pair) == NULL); /* check invariant */
      goto out;
    }
  g_assert (g_hash_table_lookup (quark_code_pair_to_re, &(re->pair)) == re); /* check invariant */

  g_assert (g_hash_table_remove (quark_code_pair_to_re, &(re->pair)));
  g_assert (g_hash_table_remove (dbus_error_name_to_re, re));

  /* destroy hashes if empty */
  hash_size = g_hash_table_size (dbus_error_name_to_re);
  if (hash_size == 0)
    {
      g_assert (g_hash_table_size (quark_code_pair_to_re) == 0); /* check invariant */

      g_hash_table_unref (dbus_error_name_to_re);
      dbus_error_name_to_re = NULL;
      g_hash_table_unref (quark_code_pair_to_re);
      quark_code_pair_to_re = NULL;
    }
  else
    {
      g_assert (g_hash_table_size (quark_code_pair_to_re) == hash_size); /* check invariant */
    }

 out:
  G_UNLOCK (error_lock);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * e_dbus_error_is_remote_error:
 * @error: A #GError.
 *
 * Checks if @error represents an error from a remote process. If so,
 * use e_dbus_error_get_remote_error() to get the name of the error.
 *
 * Returns: %TRUE if @error represents an error from a remote process,
 * %FALSE otherwise.
 */
gboolean
e_dbus_error_is_remote_error (const GError *error)
{
  g_return_val_if_fail (error != NULL, FALSE);
  return g_str_has_prefix (error->message, "EDBus.Error:");
}


/**
 * e_dbus_error_get_remote_error:
 * @error: A #GError.
 *
 * Gets the D-Bus error name used for @error, if any.
 *
 * This function is guaranteed to return a D-Bus error name for all #GError<!-- -->s returned from
 * functions handling remote method calls (e.g. e_dbus_connection_invoke_method_finish())
 * unless e_dbus_error_strip_remote_error() has been used on @error.
 *
 * Returns: An allocated string or %NULL if the D-Bus error name could not be found. Free with g_free().
 */
gchar *
e_dbus_error_get_remote_error (const GError *error)
{
  RegisteredError *re;
  gchar *ret;
  volatile GQuark error_domain;

  g_return_val_if_fail (error != NULL, NULL);

  /* Ensure that the E_DBUS_ERROR is registered using e_dbus_error_register_error() */
  error_domain = E_DBUS_ERROR;

  ret = NULL;

  G_LOCK (error_lock);

  re = NULL;
  if (quark_code_pair_to_re != NULL)
    {
      QuarkCodePair pair;
      pair.error_domain = error->domain;
      pair.error_code = error->code;
      g_assert (dbus_error_name_to_re != NULL); /* check invariant */
      re = g_hash_table_lookup (quark_code_pair_to_re, &pair);
    }

  if (re != NULL)
    {
      ret = g_strdup (re->dbus_error_name);
    }
  else
    {
      if (g_str_has_prefix (error->message, "EDBus.Error:"))
        {
          const gchar *begin;
          const gchar *end;
          begin = error->message + sizeof ("EDBus.Error:") -1;
          end = strstr (begin, ":");
          if (end != NULL && end[1] == ' ')
            {
              ret = g_strndup (begin, end - begin);
            }
        }
    }

  G_UNLOCK (error_lock);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * e_dbus_error_new_for_dbus_error:
 * @dbus_error_name: D-Bus error name.
 * @dbus_error_message: D-Bus error message.
 *
 * Creates a #GError based on the contents of @dbus_error_name and
 * @dbus_error_message.
 *
 * Errors registered with e_dbus_error_register_error() will be looked
 * up using @dbus_error_name and if a match is found, the error domain
 * and code is used. Applications can use e_dbus_error_get_remote_error()
 * to recover @dbus_error_name.
 *
 * If a match against a registered error is not found and the D-Bus
 * error name is in a form as returned by e_dbus_error_encode_gerror()
 * the error domain and code encoded in the name is used to
 * create the #GError. Also, @dbus_error_name is added to the error message
 * such that it can be recovered with e_dbus_error_get_remote_error().
 *
 * Otherwise, a #GError with the error code %E_DBUS_ERROR_REMOTE_ERROR
 * in the #E_DBUS_ERROR error domain is returned. Also, @dbus_error_name is
 * added to the error message such that it can be recovered with
 * e_dbus_error_get_remote_error().
 *
 * In all three cases, @dbus_error_name can always be recovered from the
 * returned #GError using the e_dbus_error_get_remote_error() function
 * (unless e_dbus_error_strip_remote_error() hasn't been used on the returned error).
 *
 * This function is typically only used in object mappings to prepare
 * #GError instances for applications. Regular applications should not use
 * it.
 *
 * Returns: An allocated #GError. Free with g_error_free().
 */
GError *
e_dbus_error_new_for_dbus_error (const gchar *dbus_error_name,
                                 const gchar *dbus_error_message)
{
  GError *error;
  RegisteredError *re;
  volatile GQuark error_domain;

  g_return_val_if_fail (dbus_error_name != NULL, NULL);
  g_return_val_if_fail (dbus_error_message != NULL, NULL);

  /* Ensure that the E_DBUS_ERROR is registered using e_dbus_error_register_error() */
  error_domain = E_DBUS_ERROR;

  G_LOCK (error_lock);

  re = NULL;
  if (dbus_error_name_to_re != NULL)
    {
      g_assert (quark_code_pair_to_re != NULL); /* check invariant */
      re = g_hash_table_lookup (dbus_error_name_to_re, dbus_error_name);
    }

  if (re != NULL)
    {
      error = g_error_new (re->pair.error_domain,
                           re->pair.error_code,
                           "EDBus.Error:%s: %s",
                           dbus_error_name,
                           dbus_error_message);
    }
  else
    {
      GQuark error_domain;
      gint error_code;

      if (_e_dbus_error_decode_gerror (dbus_error_name,
                                       &error_domain,
                                       &error_code))
        {
          error = g_error_new (error_domain,
                               error_code,
                               "EDBus.Error:%s: %s",
                               dbus_error_name,
                               dbus_error_message);
        }
      else
        {
          error = g_error_new (E_DBUS_ERROR,
                               E_DBUS_ERROR_REMOTE_ERROR,
                               "EDBus.Error:%s: %s",
                               dbus_error_name,
                               dbus_error_message);
        }
    }

  G_UNLOCK (error_lock);
  return error;
}

/**
 * e_dbus_error_set_dbus_error:
 * @error: A pointer to a #GError or %NULL.
 * @dbus_error_name: D-Bus error name.
 * @dbus_error_message: D-Bus error message.
 * @format: printf()-style format to prepend to @dbus_error_message or %NULL.
 * @...: Arguments for @format.
 *
 * Does nothing if @error is %NULL. Otherwise sets *@error to
 * a new #GError created with e_dbus_error_new_for_dbus_error()
 * with @dbus_error_message prepend with @format (unless %NULL).
 */
void
e_dbus_error_set_dbus_error (GError      **error,
                             const gchar  *dbus_error_name,
                             const gchar  *dbus_error_message,
                             const gchar  *format,
                             ...)
{
  g_return_if_fail (dbus_error_name != NULL);
  g_return_if_fail (dbus_error_message != NULL);

  if (error == NULL)
    return;

  if (format == NULL)
    {
      *error = e_dbus_error_new_for_dbus_error (dbus_error_name, dbus_error_message);
    }
  else
    {
      va_list var_args;
      va_start (var_args, format);
      e_dbus_error_set_dbus_error_valist (error,
                                          dbus_error_name,
                                          dbus_error_message,
                                          format,
                                          var_args);
      va_end (var_args);
    }
}

/**
 * e_dbus_error_set_dbus_error_valist:
 * @error: A pointer to a #GError or %NULL.
 * @dbus_error_name: D-Bus error name.
 * @dbus_error_message: D-Bus error message.
 * @format: printf()-style format to prepend to @dbus_error_message or %NULL.
 * @var_args: Arguments for @format.
 *
 * Like e_dbus_error_set_dbus_error() but intended for language bindings.
 */
void
e_dbus_error_set_dbus_error_valist (GError      **error,
                                    const gchar  *dbus_error_name,
                                    const gchar  *dbus_error_message,
                                    const gchar  *format,
                                    va_list       var_args)
{
  g_return_if_fail (dbus_error_name != NULL);
  g_return_if_fail (dbus_error_message != NULL);

  if (error == NULL)
    return;

  if (format != NULL)
    {
      gchar *message;
      gchar *s;
      message = g_strdup_vprintf (format, var_args);
      s = g_strdup_printf ("%s: %s", message, dbus_error_message);
      *error = e_dbus_error_new_for_dbus_error (dbus_error_name, s);
      g_free (s);
      g_free (message);
    }
  else
    {
      *error = e_dbus_error_new_for_dbus_error (dbus_error_name, dbus_error_message);
    }
}

/**
 * e_dbus_error_strip_remote_error:
 * @error: A #GError.
 *
 * Looks for extra information in the error message used to recover
 * the D-Bus error name and strips it if found. If stripped, the
 * message field in @error will correspond exactly to what was
 * received on the wire.
 *
 * This is typically used when presenting errors to the end user.
 *
 * Returns: %TRUE if information was stripped, %FALSE otherwise.
 */
gboolean
e_dbus_error_strip_remote_error (GError *error)
{
  gboolean ret;

  g_return_val_if_fail (error != NULL, FALSE);

  ret = FALSE;

  if (g_str_has_prefix (error->message, "EDBus.Error:"))
    {
      const gchar *begin;
      const gchar *end;
      gchar *new_message;

      begin = error->message + sizeof ("EDBus.Error:") -1;
      end = strstr (begin, ":");
      if (end != NULL && end[1] == ' ')
        {
          new_message = g_strdup (end + 2);
          g_free (error->message);
          error->message = new_message;
          ret = TRUE;
        }
    }

  return ret;
}

/**
 * e_dbus_error_encode_gerror:
 * @error: A #GError.
 *
 * Creates a D-Bus error name to use for @error. If @error matches
 * a registered error (cf. e_dbus_error_register_error()), the corresponding
 * D-Bus error name will be returned.
 *
 * Otherwise the a name of the form
 * <literal>org.gtk.EDBus.UnmappedGError.Quark0xHEXENCODED_QUARK_NAME_.Code_ERROR_CODE</literal>
 * will be used. This allows other EDBus applications to map the error
 * on the wire back to a #GError using e_dbus_error_new_for_dbus_error().
 *
 * This function is typically only used in object mappings to put a
 * #GError on the wire. Regular applications should not use it.
 *
 * Returns: A D-Bus error name (never %NULL). Free with g_free().
 */
gchar *
e_dbus_error_encode_gerror (const GError *error)
{
  RegisteredError *re;
  gchar *error_name;
  volatile GQuark error_domain;

  g_return_val_if_fail (error != NULL, NULL);

  /* Ensure that the E_DBUS_ERROR is registered using e_dbus_error_register_error() */
  error_domain = E_DBUS_ERROR;

  error_name = NULL;

  G_LOCK (error_lock);
  re = NULL;
  if (quark_code_pair_to_re != NULL)
    {
      QuarkCodePair pair;
      pair.error_domain = error->domain;
      pair.error_code = error->code;
      g_assert (dbus_error_name_to_re != NULL); /* check invariant */
      re = g_hash_table_lookup (quark_code_pair_to_re, &pair);
    }
  if (re != NULL)
    {
      error_name = g_strdup (re->dbus_error_name);
      G_UNLOCK (error_lock);
    }
  else
    {
      const gchar *domain_as_string;
      GString *s;
      guint n;

      G_UNLOCK (error_lock);

      /* We can't make a lot of assumptions about what domain_as_string
       * looks like and D-Bus is extremely picky about error names so
       * hex-encode it for transport across the wire.
       */
      domain_as_string = g_quark_to_string (error->domain);
      s = g_string_new ("org.gtk.EDBus.UnmappedGError.Quark0x");
      for (n = 0; domain_as_string[n] != 0; n++)
        {
          guint nibble_top;
          guint nibble_bottom;
          nibble_top = ((int) domain_as_string[n]) >> 4;
          nibble_bottom = ((int) domain_as_string[n]) & 0x0f;
          if (nibble_top < 10)
            nibble_top += '0';
          else
            nibble_top += 'a' - 10;
          if (nibble_bottom < 10)
            nibble_bottom += '0';
          else
            nibble_bottom += 'a' - 10;
          g_string_append_c (s, nibble_top);
          g_string_append_c (s, nibble_bottom);
        }
      g_string_append_printf (s, ".Code%d", error->code);
      error_name = g_string_free (s, FALSE);
    }

  return error_name;
}
