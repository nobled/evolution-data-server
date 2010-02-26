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

#ifndef __E_DBUS_INTROSPECTION_H__
#define __E_DBUS_INTROSPECTION_H__

#include <edbus/edbustypes.h>

G_BEGIN_DECLS

/**
 * EDBusAnnotationInfo:
 * @key: The name of the annotation, e.g. <literal>org.freedesktop.DBus.Deprecated</literal>
 * @value: The value of the annotation.
 * @annotations: A pointer to an array of annotations for the annotation or %NULL if there are no annotations.
 *
 * Information about an annotation.
 *
 * By convention, an array of annotations is always terminated by an element
 * where @key is %NULL.
 */
struct _EDBusAnnotationInfo
{
  const gchar                *key;
  const gchar                *value;
  const EDBusAnnotationInfo  *annotations;
};

/**
 * EDBusArgInfo:
 * @name: Name of the argument, e.g. @unix_user_id.
 * @signature: D-Bus signature of the argument (a single complete type).
 * @annotations: A pointer to an array of annotations for the argument or %NULL if there are no annotations.
 *
 * Information about an argument for a method or a signal.
 */
struct _EDBusArgInfo
{
  const gchar                *name;
  const gchar                *signature;
  const EDBusAnnotationInfo  *annotations;
};

/**
 * EDBusMethodInfo:
 * @name: The name of the D-Bus method, e.g. @RequestName.
 * @in_signature: The combined D-Bus signature of all arguments passed to the method (@in_num_args complete types).
 * @in_num_args: Number of arguments passed to the method.
 * @in_args: A pointer to an array of @in_num_args #EDBusArgInfo structures or %NULL if @in_num_args is 0.
 * @out_signature: The combined D-Bus signature of all arguments the method returns (@out_num_args complete types).
 * @out_num_args: Number of arguments the method returns.
 * @out_args: A pointer to an array of @out_num_args #EDBusArgInfo structures or %NULL if @out_num_args is 0.
 * @annotations: A pointer to an array of annotations for the method or %NULL if there are no annotations.
 *
 * Information about a method on an D-Bus interface.
 */
struct _EDBusMethodInfo
{
  const gchar                *name;

  const gchar                *in_signature;
  guint                       in_num_args;
  const EDBusArgInfo         *in_args;

  const gchar                *out_signature;
  guint                       out_num_args;
  const EDBusArgInfo         *out_args;

  const EDBusAnnotationInfo  *annotations;
};

/**
 * EDBusSignalInfo:
 * @name: The name of the D-Bus signal, e.g. @NameOwnerChanged.
 * @signature: The combined D-Bus signature of all arguments of the signal (@num_args complete types).
 * @num_args: Number of arguments of the signal.
 * @args: A pointer to an array of @num_args #EDBusArgInfo structures or %NULL if @num_args is 0.
 * @annotations: A pointer to an array of annotations for the signal or %NULL if there are no annotations.
 *
 * Information about a signal on a D-Bus interface.
 */
struct _EDBusSignalInfo
{
  const gchar                *name;

  const gchar                *signature;
  guint                       num_args;
  const EDBusArgInfo         *args;

  const EDBusAnnotationInfo  *annotations;
};

/**
 * EDBusPropertyInfo:
 * @name: The name of the D-Bus property, e.g. @SupportedFilesystems.
 * @signature: The D-Bus signature of the property (a single complete type).
 * @flags: Access control flags for the property.
 * @annotations: A pointer to an array of annotations for the property or %NULL if there are no annotations.
 *
 * Information about a D-Bus property on a D-Bus interface.
 */
struct _EDBusPropertyInfo
{
  const gchar                *name;
  const gchar                *signature;
  EDBusPropertyInfoFlags      flags;
  const EDBusAnnotationInfo  *annotations;
};

/**
 * EDBusInterfaceInfo:
 * @name: The name of the D-Bus interface, e.g. <literal>org.freedesktop.DBus.Properties</literal>.
 * @num_methods: Number of methods on the interface.
 * @methods: A pointer to an array of @num_methods #EDBusMethodInfo structures or %NULL if @num_methods is 0.
 * @num_signals: Number of signals on the interface.
 * @signals: A pointer to an array of @num_signals #EDBusSignalInfo structures or %NULL if @num_signals is 0.
 * @num_properties: Number of properties on the interface.
 * @properties: A pointer to an array of @num_properties #EDBusPropertyInfo structures or %NULL if @num_properties is 0.
 * @annotations: A pointer to an array of annotations for the interface or %NULL if there are no annotations.
 *
 * Information about a D-Bus interface.
 */
struct _EDBusInterfaceInfo
{
  const gchar                *name;

  guint                       num_methods;
  const EDBusMethodInfo      *methods;

  guint                       num_signals;
  const EDBusSignalInfo      *signals;

  guint                       num_properties;
  const EDBusPropertyInfo    *properties;

  const EDBusAnnotationInfo  *annotations;
};

/**
 * EDBusNodeInfo:
 * @path: The path of the node or %NULL if omitted. Note that this may be a relative path. See the D-Bus specification for more details.
 * @num_interfaces: Number of interfaces of the node.
 * @interfaces: A pointer to an array of @num_interfaces #EDBusInterfaceInfo structures or %NULL if @num_interfaces is 0.
 * @num_nodes: Number of child nodes.
 * @nodes: A pointer to an array of @num_nodes #EDBusNodeInfo structures or %NULL if @num_nodes is 0.
 * @annotations: A pointer to an array of annotations for the node or %NULL if there are no annotations.
 *
 * Information about nodes in a remote object hierarchy.
 */
struct _EDBusNodeInfo
{
  const gchar                *path;

  guint                       num_interfaces;
  const EDBusInterfaceInfo   *interfaces;

  guint                       num_nodes;
  const EDBusNodeInfo        *nodes;

  const EDBusAnnotationInfo  *annotations;
};

const gchar              *e_dbus_annotation_info_lookup          (const EDBusAnnotationInfo *annotations,
                                                                  const gchar               *name);
const EDBusMethodInfo    *e_dbus_interface_info_lookup_method    (const EDBusInterfaceInfo  *interface_info,
                                                                  const gchar               *name);
const EDBusSignalInfo    *e_dbus_interface_info_lookup_signal    (const EDBusInterfaceInfo  *interface_info,
                                                                  const gchar               *name);
const EDBusPropertyInfo  *e_dbus_interface_info_lookup_property  (const EDBusInterfaceInfo  *interface_info,
                                                                  const gchar               *name);
void                      e_dbus_interface_info_generate_xml     (const EDBusInterfaceInfo  *interface_info,
                                                                  guint                      indent,
                                                                  GString                   *string_builder);

EDBusNodeInfo            *e_dbus_node_info_new_for_xml           (const gchar               *xml_data,
                                                                  GError                   **error);
const EDBusInterfaceInfo *e_dbus_node_info_lookup_interface      (const EDBusNodeInfo       *node_info,
                                                                  const gchar               *name);
void                      e_dbus_node_info_free                  (EDBusNodeInfo             *node_info);
void                      e_dbus_node_info_generate_xml          (const EDBusNodeInfo       *node_info,
                                                                  guint                      indent,
                                                                  GString                   *string_builder);

G_END_DECLS

#endif /* __E_DBUS_INTROSPECTION_H__ */
