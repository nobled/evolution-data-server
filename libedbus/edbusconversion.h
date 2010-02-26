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

#if !defined (E_DBUS_COMPILATION)
#error "gdbusconversion.h is a private header file."
#endif

#ifndef __E_DBUS_CONVERSION_H__
#define __E_DBUS_CONVERSION_H__

#include <gdbus/gdbustypes.h>
#include <dbus/dbus.h>

G_BEGIN_DECLS

EVariant *_e_dbus_dbus_1_to_gvariant (DBusMessage  *message,
                                      GError      **error);

gboolean _e_dbus_gvariant_to_dbus_1 (DBusMessage  *message,
                                     EVariant     *value,
                                     GError      **error);


G_END_DECLS

#endif /* __E_DBUS_CONVERSION_H__ */
