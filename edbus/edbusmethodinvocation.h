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

#ifndef __E_DBUS_METHOD_INVOCATION_H__
#define __E_DBUS_METHOD_INVOCATION_H__

#include <edbus/edbustypes.h>

G_BEGIN_DECLS

#define E_TYPE_DBUS_METHOD_INVOCATION         (e_dbus_method_invocation_get_type ())
#define E_DBUS_METHOD_INVOCATION(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_DBUS_METHOD_INVOCATION, EDBusMethodInvocation))
#define E_DBUS_METHOD_INVOCATION_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_DBUS_METHOD_INVOCATION, EDBusMethodInvocationClass))
#define E_DBUS_METHOD_INVOCATION_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), E_TYPE_DBUS_METHOD_INVOCATION, EDBusMethodInvocationClass))
#define G_IS_DBUS_METHOD_INVOCATION(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_DBUS_METHOD_INVOCATION))
#define G_IS_DBUS_METHOD_INVOCATION_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_DBUS_METHOD_INVOCATION))

typedef struct _EDBusMethodInvocationClass   EDBusMethodInvocationClass;
typedef struct _EDBusMethodInvocationPrivate EDBusMethodInvocationPrivate;

/**
 * EDBusMethodInvocation:
 *
 * The #EDBusMethodInvocation structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _EDBusMethodInvocation
{
  /*< private >*/
  GObject parent_instance;
  EDBusMethodInvocationPrivate *priv;
};

/**
 * EDBusMethodInvocationClass:
 *
 * Class structure for #EDBusMethodInvocation.
 */
struct _EDBusMethodInvocationClass
{
  /*< private >*/
  GObjectClass parent_class;

  /*< private >*/
  /* Padding for future expansion */
  void (*_g_reserved1) (void);
  void (*_g_reserved2) (void);
  void (*_g_reserved3) (void);
  void (*_g_reserved4) (void);
  void (*_g_reserved5) (void);
  void (*_g_reserved6) (void);
  void (*_g_reserved7) (void);
  void (*_g_reserved8) (void);
};

GType                  e_dbus_method_invocation_get_type             (void) G_GNUC_CONST;
EDBusMethodInvocation *e_dbus_method_invocation_new                  (const gchar           *sender,
                                                                      const gchar           *object_path,
                                                                      const gchar           *interface_name,
                                                                      const gchar           *method_name,
                                                                      EDBusConnection       *connection,
                                                                      EVariant              *parameters,
                                                                      gpointer               user_data);
const gchar           *e_dbus_method_invocation_get_sender           (EDBusMethodInvocation *invocation);
const gchar           *e_dbus_method_invocation_get_object_path      (EDBusMethodInvocation *invocation);
const gchar           *e_dbus_method_invocation_get_interface_name   (EDBusMethodInvocation *invocation);
const gchar           *e_dbus_method_invocation_get_method_name      (EDBusMethodInvocation *invocation);
EDBusConnection       *e_dbus_method_invocation_get_connection       (EDBusMethodInvocation *invocation);
EVariant              *e_dbus_method_invocation_get_parameters       (EDBusMethodInvocation *invocation);
gpointer               e_dbus_method_invocation_get_user_data        (EDBusMethodInvocation *invocation);

void                   e_dbus_method_invocation_return_value         (EDBusMethodInvocation *invocation,
                                                                      EVariant              *parameters);
void                   e_dbus_method_invocation_return_error         (EDBusMethodInvocation *invocation,
                                                                      GQuark                 domain,
                                                                      gint                   code,
                                                                      const gchar           *format,
                                                                      ...);
void                   e_dbus_method_invocation_return_error_valist  (EDBusMethodInvocation *invocation,
                                                                      GQuark                 domain,
                                                                      gint                   code,
                                                                      const gchar           *format,
                                                                      va_list                var_args);
void                   e_dbus_method_invocation_return_error_literal (EDBusMethodInvocation *invocation,
                                                                      GQuark                 domain,
                                                                      gint                   code,
                                                                      const gchar           *message);
void                   e_dbus_method_invocation_return_gerror        (EDBusMethodInvocation *invocation,
                                                                      const GError          *error);
void                   e_dbus_method_invocation_return_dbus_error    (EDBusMethodInvocation *invocation,
                                                                      const gchar           *error_name,
                                                                      const gchar           *error_message);

G_END_DECLS

#endif /* __E_DBUS_METHOD_INVOCATION_H__ */
