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

#ifndef __E_DBUS_H__
#define __E_DBUS_H__

#define __E_DBUS_H_INSIDE__

#include <edbus/edbustypes.h>
#include <edbus/edbusenumtypes.h>
#include <edbus/edbusconnection.h>
#include <edbus/edbuserror.h>
#include <edbus/edbusnameowning.h>
#include <edbus/edbusnamewatching.h>
#include <edbus/edbusproxywatching.h>
#include <edbus/edbusproxy.h>
#include <edbus/edbusintrospection.h>
#include <edbus/edbusmethodinvocation.h>
#include <edbus/edbusserver.h>

extern void e_dbus_threads_init (void);

#undef __E_DBUS_H_INSIDE__

#endif /* __E_DBUS_H__ */