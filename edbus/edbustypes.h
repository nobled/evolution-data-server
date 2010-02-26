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

#ifndef __E_DBUS_TYPES_H__
#define __E_DBUS_TYPES_H__

#include "evariant.h"
#include <edbus/edbusenums.h>

G_BEGIN_DECLS

typedef struct _EDBusConnection       EDBusConnection;
typedef struct _EDBusServer           EDBusServer;
typedef struct _EDBusProxy            EDBusProxy;
typedef struct _EDBusMethodInvocation EDBusMethodInvocation;

typedef struct _EDBusInterfaceVTable  EDBusInterfaceVTable;
typedef struct _EDBusSubtreeVTable    EDBusSubtreeVTable;

typedef struct _EDBusAnnotationInfo   EDBusAnnotationInfo;
typedef struct _EDBusArgInfo          EDBusArgInfo;
typedef struct _EDBusMethodInfo       EDBusMethodInfo;
typedef struct _EDBusSignalInfo       EDBusSignalInfo;
typedef struct _EDBusPropertyInfo     EDBusPropertyInfo;
typedef struct _EDBusInterfaceInfo    EDBusInterfaceInfo;
typedef struct _EDBusNodeInfo         EDBusNodeInfo;

G_END_DECLS

#endif /* __E_DBUS_TYPES_H__ */
