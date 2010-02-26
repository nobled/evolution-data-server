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

#if !defined (__E_DBUS_H_INSIDE__) && !defined (E_DBUS_COMPILATION)
#error "Only <edbus/edbus.h> can be included directly."
#endif

#ifndef __E_DBUS_ENUMS_H__
#define __E_DBUS_ENUMS_H__

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

/**
 * EDBusType:
 * @G_BUS_TYPE_NONE: Not a message bus connection.
 * @G_BUS_TYPE_SESSION: The login session message bus.
 * @G_BUS_TYPE_SYSTEM: The system-wide message bus.
 * @G_BUS_TYPE_STARTER: Connect to the bus that activated the program.
 *
 * An enumeration to specify the type of a #EDBusConnection.
 */
typedef enum
{
  G_BUS_TYPE_NONE    = -1,
  G_BUS_TYPE_SESSION = 0,
  G_BUS_TYPE_SYSTEM  = 1,
  G_BUS_TYPE_STARTER = 2
} EDBusType;

/**
 * EDBusNameOwnerFlags:
 * @G_BUS_NAME_OWNER_FLAGS_NONE: No flags set.
 * @G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT: Allow another message connection to take the name.
 * @G_BUS_NAME_OWNER_FLAGS_REPLACE: If another message bus connection
 * owns the name and have specified #G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT, then
 * take the name from the other connection.
 *
 * Flags used when constructing a #GBusNameOwner.
 */
typedef enum
{
  G_BUS_NAME_OWNER_FLAGS_NONE = 0,                    /*< nick=none >*/
  G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT = (1<<0),  /*< nick=allow-replacement >*/
  G_BUS_NAME_OWNER_FLAGS_REPLACE = (1<<1),            /*< nick=replace >*/
} EDBusNameOwnerFlags;

/**
 * EDBusProxyFlags:
 * @E_DBUS_PROXY_FLAGS_NONE: No flags set.
 * @E_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES: Don't load properties.
 * @E_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS: Don't connect to signals on the remote object.
 *
 * Flags used when constructing an instance of a #EDBusProxy derived class.
 */
typedef enum
{
  E_DBUS_PROXY_FLAGS_NONE = 0,                        /*< nick=none >*/
  E_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES = (1<<0), /*< nick=do-not-load-properties >*/
  E_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS = (1<<1), /*< nick=do-not-connect-signals >*/
} EDBusProxyFlags;

/**
 * EDBusError:
 * @E_DBUS_ERROR_FAILED: The operation failed.
 * @E_DBUS_ERROR_CANCELLED: The operation was cancelled.
 * @E_DBUS_ERROR_CONVERSION_FAILED: An attempt was made to send #EVariant value that isn't
 * compatible with the D-Bus protocol.
 * @E_DBUS_ERROR_REMOTE_ERROR: A remote object generated an error
 * that doesn't correspond to a locally registered #GError error domain.
 * Use e_dbus_error_get_dbus_error_name() to extract the D-Bus error name
 * and e_dbus_error_strip() to get fix up the message so it matches what
 * was received on the wire.
 * @E_DBUS_ERROR_DBUS_FAILED:
 * A generic error; "something went wrong" - see the error message for
 * more.
 * @E_DBUS_ERROR_NO_MEMORY:
 * There was not enough memory to complete an operation.
 * @E_DBUS_ERROR_SERVICE_UNKNOWN:
 * The bus doesn't know how to launch a service to supply the bus name
 * you wanted.
 * @E_DBUS_ERROR_NAME_HAS_NO_OWNER:
 * The bus name you referenced doesn't exist (i.e. no application owns
 * it).
 * @E_DBUS_ERROR_NO_REPLY:
 * No reply to a message expecting one, usually means a timeout occurred.
 * @E_DBUS_ERROR_IO_ERROR:
 * Something went wrong reading or writing to a socket, for example.
 * @E_DBUS_ERROR_BAD_ADDRESS:
 * A D-Bus bus address was malformed.
 * @E_DBUS_ERROR_NOT_SUPPORTED:
 * Requested operation isn't supported (like ENOSYS on UNIX).
 * @E_DBUS_ERROR_LIMITS_EXCEEDED:
 * Some limited resource is exhausted.
 * @E_DBUS_ERROR_ACCESS_DENIED:
 * Security restrictions don't allow doing what you're trying to do.
 * @E_DBUS_ERROR_AUTH_FAILED:
 * Authentication didn't work.
 * @E_DBUS_ERROR_NO_SERVER:
 * Unable to connect to server (probably caused by ECONNREFUSED on a
 * socket).
 * @E_DBUS_ERROR_TIMEOUT:
 * Certain timeout errors, possibly ETIMEDOUT on a socket.  Note that
 * %E_DBUS_ERROR_NO_REPLY is used for message reply timeouts. Warning:
 * this is confusingly-named given that %E_DBUS_ERROR_TIMED_OUT also
 * exists. We can't fix it for compatibility reasons so just be
 * careful.
 * @E_DBUS_ERROR_NO_NETWORK:
 * No network access (probably ENETUNREACH on a socket).
 * @E_DBUS_ERROR_ADDRESS_IN_USE:
 * Can't bind a socket since its address is in use (i.e. EADDRINUSE).
 * @E_DBUS_ERROR_DISCONNECTED:
 * The connection is disconnected and you're trying to use it.
 * @E_DBUS_ERROR_INVALID_ARGS:
 * Invalid arguments passed to a method call.
 * @E_DBUS_ERROR_FILE_NOT_FOUND:
 * Missing file.
 * @E_DBUS_ERROR_FILE_EXISTS:
 * Existing file and the operation you're using does not silently overwrite.
 * @E_DBUS_ERROR_UNKNOWN_METHOD:
 * Method name you invoked isn't known by the object you invoked it on.
 * @E_DBUS_ERROR_TIMED_OUT:
 * Certain timeout errors, e.g. while starting a service. Warning: this is
 * confusingly-named given that %E_DBUS_ERROR_TIMEOUT also exists. We
 * can't fix it for compatibility reasons so just be careful.
 * @E_DBUS_ERROR_MATCH_RULE_NOT_FOUND:
 * Tried to remove or modify a match rule that didn't exist.
 * @E_DBUS_ERROR_MATCH_RULE_INVALID:
 * The match rule isn't syntactically valid.
 * @E_DBUS_ERROR_SPAWN_EXEC_FAILED:
 * While starting a new process, the exec() call failed.
 * @E_DBUS_ERROR_SPAWN_FORK_FAILED:
 * While starting a new process, the fork() call failed.
 * @E_DBUS_ERROR_SPAWN_CHILD_EXITED:
 * While starting a new process, the child exited with a status code.
 * @E_DBUS_ERROR_SPAWN_CHILD_SIGNALED:
 * While starting a new process, the child exited on a signal.
 * @E_DBUS_ERROR_SPAWN_FAILED:
 * While starting a new process, something went wrong.
 * @E_DBUS_ERROR_SPAWN_SETUP_FAILED:
 * We failed to setup the environment correctly.
 * @E_DBUS_ERROR_SPAWN_CONFIG_INVALID:
 * We failed to setup the config parser correctly.
 * @E_DBUS_ERROR_SPAWN_SERVICE_INVALID:
 * Bus name was not valid.
 * @E_DBUS_ERROR_SPAWN_SERVICE_NOT_FOUND:
 * Service file not found in system-services directory.
 * @E_DBUS_ERROR_SPAWN_PERMISSIONS_INVALID:
 * Permissions are incorrect on the setuid helper.
 * @E_DBUS_ERROR_SPAWN_FILE_INVALID:
 * Service file invalid (Name, User or Exec missing).
 * @E_DBUS_ERROR_SPAWN_NO_MEMORY:
 * Tried to get a UNIX process ID and it wasn't available.
 * @E_DBUS_ERROR_UNIX_PROCESS_ID_UNKNOWN:
 * Tried to get a UNIX process ID and it wasn't available.
 * @E_DBUS_ERROR_INVALID_SIGNATURE:
 * A type signature is not valid.
 * @E_DBUS_ERROR_INVALID_FILE_CONTENT:
 * A file contains invalid syntax or is otherwise broken.
 * @E_DBUS_ERROR_SELINUX_SECURITY_CONTEXT_UNKNOWN:
 * Asked for SELinux security context and it wasn't available.
 * @E_DBUS_ERROR_ADT_AUDIT_DATA_UNKNOWN:
 * Asked for ADT audit data and it wasn't available.
 * @E_DBUS_ERROR_OBJECT_PATH_IN_USE:
 * There's already an object with the requested object path.
 * @_E_DBUS_ERROR_MAX_DBUS_ERROR: This is a private member - the value is subject to change.
 *
 * Error codes.
 */
typedef enum
{
  E_DBUS_ERROR_FAILED,
  E_DBUS_ERROR_CANCELLED,
  E_DBUS_ERROR_CONVERSION_FAILED,
  E_DBUS_ERROR_REMOTE_ERROR,

  /* Well-known errors in the org.freedesktop.DBus.Error namespace */
  E_DBUS_ERROR_DBUS_FAILED            = 1000,    /* org.freedesktop.DBus.Error.Failed */
  E_DBUS_ERROR_NO_MEMORY,                        /* org.freedesktop.DBus.Error.NoMemory */
  E_DBUS_ERROR_SERVICE_UNKNOWN,                  /* org.freedesktop.DBus.Error.ServiceUnknown */
  E_DBUS_ERROR_NAME_HAS_NO_OWNER,                /* org.freedesktop.DBus.Error.NameHasNoOwner */
  E_DBUS_ERROR_NO_REPLY,                         /* org.freedesktop.DBus.Error.NoReply */
  E_DBUS_ERROR_IO_ERROR,                         /* org.freedesktop.DBus.Error.IOError */
  E_DBUS_ERROR_BAD_ADDRESS,                      /* org.freedesktop.DBus.Error.BadAddress */
  E_DBUS_ERROR_NOT_SUPPORTED,                    /* org.freedesktop.DBus.Error.NotSupported */
  E_DBUS_ERROR_LIMITS_EXCEEDED,                  /* org.freedesktop.DBus.Error.LimitsExceeded */
  E_DBUS_ERROR_ACCESS_DENIED,                    /* org.freedesktop.DBus.Error.AccessDenied */
  E_DBUS_ERROR_AUTH_FAILED,                      /* org.freedesktop.DBus.Error.AuthFailed */
  E_DBUS_ERROR_NO_SERVER,                        /* org.freedesktop.DBus.Error.NoServer */
  E_DBUS_ERROR_TIMEOUT,                          /* org.freedesktop.DBus.Error.Timeout */
  E_DBUS_ERROR_NO_NETWORK,                       /* org.freedesktop.DBus.Error.NoNetwork */
  E_DBUS_ERROR_ADDRESS_IN_USE,                   /* org.freedesktop.DBus.Error.AddressInUse */
  E_DBUS_ERROR_DISCONNECTED,                     /* org.freedesktop.DBus.Error.Disconnected */
  E_DBUS_ERROR_INVALID_ARGS,                     /* org.freedesktop.DBus.Error.InvalidArgs */
  E_DBUS_ERROR_FILE_NOT_FOUND,                   /* org.freedesktop.DBus.Error.FileNotFound */
  E_DBUS_ERROR_FILE_EXISTS,                      /* org.freedesktop.DBus.Error.FileExists */
  E_DBUS_ERROR_UNKNOWN_METHOD,                   /* org.freedesktop.DBus.Error.UnknownMethod */
  E_DBUS_ERROR_TIMED_OUT,                        /* org.freedesktop.DBus.Error.TimedOut */
  E_DBUS_ERROR_MATCH_RULE_NOT_FOUND,             /* org.freedesktop.DBus.Error.MatchRuleNotFound */
  E_DBUS_ERROR_MATCH_RULE_INVALID,               /* org.freedesktop.DBus.Error.MatchRuleInvalid */
  E_DBUS_ERROR_SPAWN_EXEC_FAILED,                /* org.freedesktop.DBus.Error.Spawn.ExecFailed */
  E_DBUS_ERROR_SPAWN_FORK_FAILED,                /* org.freedesktop.DBus.Error.Spawn.ForkFailed */
  E_DBUS_ERROR_SPAWN_CHILD_EXITED,               /* org.freedesktop.DBus.Error.Spawn.ChildExited */
  E_DBUS_ERROR_SPAWN_CHILD_SIGNALED,             /* org.freedesktop.DBus.Error.Spawn.ChildSignaled */
  E_DBUS_ERROR_SPAWN_FAILED,                     /* org.freedesktop.DBus.Error.Spawn.Failed */
  E_DBUS_ERROR_SPAWN_SETUP_FAILED,               /* org.freedesktop.DBus.Error.Spawn.FailedToSetup */
  E_DBUS_ERROR_SPAWN_CONFIG_INVALID,             /* org.freedesktop.DBus.Error.Spawn.ConfigInvalid */
  E_DBUS_ERROR_SPAWN_SERVICE_INVALID,            /* org.freedesktop.DBus.Error.Spawn.ServiceNotValid */
  E_DBUS_ERROR_SPAWN_SERVICE_NOT_FOUND,          /* org.freedesktop.DBus.Error.Spawn.ServiceNotFound */
  E_DBUS_ERROR_SPAWN_PERMISSIONS_INVALID,        /* org.freedesktop.DBus.Error.Spawn.PermissionsInvalid */
  E_DBUS_ERROR_SPAWN_FILE_INVALID,               /* org.freedesktop.DBus.Error.Spawn.FileInvalid */
  E_DBUS_ERROR_SPAWN_NO_MEMORY,                  /* org.freedesktop.DBus.Error.Spawn.NoMemory */
  E_DBUS_ERROR_UNIX_PROCESS_ID_UNKNOWN,          /* org.freedesktop.DBus.Error.UnixProcessIdUnknown */
  E_DBUS_ERROR_INVALID_SIGNATURE,                /* org.freedesktop.DBus.Error.InvalidSignature */
  E_DBUS_ERROR_INVALID_FILE_CONTENT,             /* org.freedesktop.DBus.Error.InvalidFileContent */
  E_DBUS_ERROR_SELINUX_SECURITY_CONTEXT_UNKNOWN, /* org.freedesktop.DBus.Error.SELinuxSecurityContextUnknown */
  E_DBUS_ERROR_ADT_AUDIT_DATA_UNKNOWN,           /* org.freedesktop.DBus.Error.AdtAuditDataUnknown */
  E_DBUS_ERROR_OBJECT_PATH_IN_USE,               /* org.freedesktop.DBus.Error.ObjectPathInUse */

  /* This is a private member - the value is subject to change */
  _E_DBUS_ERROR_MAX_DBUS_ERROR                   /*< skip >*/
} EDBusError;

/**
 * EDBusPropertyInfoFlags:
 * @E_DBUS_PROPERTY_INFO_FLAGS_NONE: No flags set.
 * @E_DBUS_PROPERTY_INFO_FLAGS_READABLE: Property is readable.
 * @E_DBUS_PROPERTY_INFO_FLAGS_WRITABLE: Property is writable.
 *
 * Flags describing the access control of a D-Bus property.
 */
typedef enum
{
  E_DBUS_PROPERTY_INFO_FLAGS_NONE = 0,
  E_DBUS_PROPERTY_INFO_FLAGS_READABLE = (1<<0),
  E_DBUS_PROPERTY_INFO_FLAGS_WRITABLE = (1<<1),
} EDBusPropertyInfoFlags;

/**
 * EDBusSubtreeFlags:
 * @E_DBUS_SUBTREE_FLAGS_NONE: No flags set.
 * @E_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES: Method calls to objects not in the enumerated range
 *                                                       will still be dispatched. This is useful if you want
 *                                                       to dynamically spawn objects in the subtree.
 *
 * Flags passed to e_dbus_connection_register_subtree().
 */
typedef enum
{
  E_DBUS_SUBTREE_FLAGS_NONE = 0,
  E_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES = (1<<0),
} EDBusSubtreeFlags;

G_END_DECLS

#endif /* __E_DBUS_ENUMS_H__ */
