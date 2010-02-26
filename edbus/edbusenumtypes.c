
/* Generated data (by glib-mkenums) */

#include <edbus.h>

/* enumerations from "evariant.h" */
GType
struct_struct_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;

  if (g_once_init_enter (&g_define_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { E_VARIANT_CLASS_BOOLEAN, "E_VARIANT_CLASS_BOOLEAN", "boolean" },
        { E_VARIANT_CLASS_BYTE, "E_VARIANT_CLASS_BYTE", "byte" },
        { E_VARIANT_CLASS_INT16, "E_VARIANT_CLASS_INT16", "int16" },
        { E_VARIANT_CLASS_UINT16, "E_VARIANT_CLASS_UINT16", "uint16" },
        { E_VARIANT_CLASS_INT32, "E_VARIANT_CLASS_INT32", "int32" },
        { E_VARIANT_CLASS_UINT32, "E_VARIANT_CLASS_UINT32", "uint32" },
        { E_VARIANT_CLASS_INT64, "E_VARIANT_CLASS_INT64", "int64" },
        { E_VARIANT_CLASS_UINT64, "E_VARIANT_CLASS_UINT64", "uint64" },
        { E_VARIANT_CLASS_HANDLE, "E_VARIANT_CLASS_HANDLE", "handle" },
        { E_VARIANT_CLASS_DOUBLE, "E_VARIANT_CLASS_DOUBLE", "double" },
        { E_VARIANT_CLASS_STRING, "E_VARIANT_CLASS_STRING", "string" },
        { E_VARIANT_CLASS_OBJECT_PATH, "E_VARIANT_CLASS_OBJECT_PATH", "object-path" },
        { E_VARIANT_CLASS_SIGNATURE, "E_VARIANT_CLASS_SIGNATURE", "signature" },
        { E_VARIANT_CLASS_VARIANT, "E_VARIANT_CLASS_VARIANT", "variant" },
        { E_VARIANT_CLASS_MAYBE, "E_VARIANT_CLASS_MAYBE", "maybe" },
        { E_VARIANT_CLASS_ARRAY, "E_VARIANT_CLASS_ARRAY", "array" },
        { E_VARIANT_CLASS_TUPLE, "E_VARIANT_CLASS_TUPLE", "tuple" },
        { E_VARIANT_CLASS_DICT_ENTRY, "E_VARIANT_CLASS_DICT_ENTRY", "dict-entry" },
        { 0, NULL, NULL }
      };
      GType g_define_type_id =
        g_enum_register_static (g_intern_static_string ("struct"), values);
      g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

  return g_define_type_id__volatile;
}

GType
e_variant_builder_error_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;

  if (g_once_init_enter (&g_define_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { E_VARIANT_BUILDER_ERROR_TOO_MANY, "E_VARIANT_BUILDER_ERROR_TOO_MANY", "too-many" },
        { E_VARIANT_BUILDER_ERROR_TOO_FEW, "E_VARIANT_BUILDER_ERROR_TOO_FEW", "too-few" },
        { E_VARIANT_BUILDER_ERROR_INFER, "E_VARIANT_BUILDER_ERROR_INFER", "infer" },
        { E_VARIANT_BUILDER_ERROR_TYPE, "E_VARIANT_BUILDER_ERROR_TYPE", "type" },
        { 0, NULL, NULL }
      };
      GType g_define_type_id =
        g_enum_register_static (g_intern_static_string ("EVariantBuilderError"), values);
      g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

  return g_define_type_id__volatile;
}

GType
e_variant_flags_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;

  if (g_once_init_enter (&g_define_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { E_VARIANT_LITTLE_ENDIAN, "E_VARIANT_LITTLE_ENDIAN", "little-endian" },
        { E_VARIANT_BIG_ENDIAN, "E_VARIANT_BIG_ENDIAN", "big-endian" },
        { E_VARIANT_TRUSTED, "E_VARIANT_TRUSTED", "trusted" },
        { E_VARIANT_LAZY_BYTESWAP, "E_VARIANT_LAZY_BYTESWAP", "lazy-byteswap" },
        { 0, NULL, NULL }
      };
      GType g_define_type_id =
        g_enum_register_static (g_intern_static_string ("EVariantFlags"), values);
      g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

  return g_define_type_id__volatile;
}

/* enumerations from "edbusenums.h" */
GType
g_bus_type_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;

  if (g_once_init_enter (&g_define_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { G_BUS_TYPE_NONE, "G_BUS_TYPE_NONE", "none" },
        { G_BUS_TYPE_SESSION, "G_BUS_TYPE_SESSION", "session" },
        { G_BUS_TYPE_SYSTEM, "G_BUS_TYPE_SYSTEM", "system" },
        { G_BUS_TYPE_STARTER, "G_BUS_TYPE_STARTER", "starter" },
        { 0, NULL, NULL }
      };
      GType g_define_type_id =
        g_enum_register_static (g_intern_static_string ("EDBusType"), values);
      g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

  return g_define_type_id__volatile;
}

GType
g_bus_name_owner_flags_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;

  if (g_once_init_enter (&g_define_type_id__volatile))
    {
      static const GFlagsValue values[] = {
        { G_BUS_NAME_OWNER_FLAGS_NONE, "G_BUS_NAME_OWNER_FLAGS_NONE", "none" },
        { G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT, "G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT", "allow-replacement" },
        { G_BUS_NAME_OWNER_FLAGS_REPLACE, "G_BUS_NAME_OWNER_FLAGS_REPLACE", "replace" },
        { 0, NULL, NULL }
      };
      GType g_define_type_id =
        g_flags_register_static (g_intern_static_string ("EDBusNameOwnerFlags"), values);
      g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

  return g_define_type_id__volatile;
}

GType
e_dbus_proxy_flags_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;

  if (g_once_init_enter (&g_define_type_id__volatile))
    {
      static const GFlagsValue values[] = {
        { E_DBUS_PROXY_FLAGS_NONE, "E_DBUS_PROXY_FLAGS_NONE", "none" },
        { E_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, "E_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES", "do-not-load-properties" },
        { E_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS, "E_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS", "do-not-connect-signals" },
        { 0, NULL, NULL }
      };
      GType g_define_type_id =
        g_flags_register_static (g_intern_static_string ("EDBusProxyFlags"), values);
      g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

  return g_define_type_id__volatile;
}

GType
e_dbus_error_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;

  if (g_once_init_enter (&g_define_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { E_DBUS_ERROR_FAILED, "E_DBUS_ERROR_FAILED", "failed" },
        { E_DBUS_ERROR_CANCELLED, "E_DBUS_ERROR_CANCELLED", "cancelled" },
        { E_DBUS_ERROR_CONVERSION_FAILED, "E_DBUS_ERROR_CONVERSION_FAILED", "conversion-failed" },
        { E_DBUS_ERROR_REMOTE_ERROR, "E_DBUS_ERROR_REMOTE_ERROR", "remote-error" },
        { E_DBUS_ERROR_DBUS_FAILED, "E_DBUS_ERROR_DBUS_FAILED", "dbus-failed" },
        { E_DBUS_ERROR_NO_MEMORY, "E_DBUS_ERROR_NO_MEMORY", "no-memory" },
        { E_DBUS_ERROR_SERVICE_UNKNOWN, "E_DBUS_ERROR_SERVICE_UNKNOWN", "service-unknown" },
        { E_DBUS_ERROR_NAME_HAS_NO_OWNER, "E_DBUS_ERROR_NAME_HAS_NO_OWNER", "name-has-no-owner" },
        { E_DBUS_ERROR_NO_REPLY, "E_DBUS_ERROR_NO_REPLY", "no-reply" },
        { E_DBUS_ERROR_IO_ERROR, "E_DBUS_ERROR_IO_ERROR", "io-error" },
        { E_DBUS_ERROR_BAD_ADDRESS, "E_DBUS_ERROR_BAD_ADDRESS", "bad-address" },
        { E_DBUS_ERROR_NOT_SUPPORTED, "E_DBUS_ERROR_NOT_SUPPORTED", "not-supported" },
        { E_DBUS_ERROR_LIMITS_EXCEEDED, "E_DBUS_ERROR_LIMITS_EXCEEDED", "limits-exceeded" },
        { E_DBUS_ERROR_ACCESS_DENIED, "E_DBUS_ERROR_ACCESS_DENIED", "access-denied" },
        { E_DBUS_ERROR_AUTH_FAILED, "E_DBUS_ERROR_AUTH_FAILED", "auth-failed" },
        { E_DBUS_ERROR_NO_SERVER, "E_DBUS_ERROR_NO_SERVER", "no-server" },
        { E_DBUS_ERROR_TIMEOUT, "E_DBUS_ERROR_TIMEOUT", "timeout" },
        { E_DBUS_ERROR_NO_NETWORK, "E_DBUS_ERROR_NO_NETWORK", "no-network" },
        { E_DBUS_ERROR_ADDRESS_IN_USE, "E_DBUS_ERROR_ADDRESS_IN_USE", "address-in-use" },
        { E_DBUS_ERROR_DISCONNECTED, "E_DBUS_ERROR_DISCONNECTED", "disconnected" },
        { E_DBUS_ERROR_INVALID_ARGS, "E_DBUS_ERROR_INVALID_ARGS", "invalid-args" },
        { E_DBUS_ERROR_FILE_NOT_FOUND, "E_DBUS_ERROR_FILE_NOT_FOUND", "file-not-found" },
        { E_DBUS_ERROR_FILE_EXISTS, "E_DBUS_ERROR_FILE_EXISTS", "file-exists" },
        { E_DBUS_ERROR_UNKNOWN_METHOD, "E_DBUS_ERROR_UNKNOWN_METHOD", "unknown-method" },
        { E_DBUS_ERROR_TIMED_OUT, "E_DBUS_ERROR_TIMED_OUT", "timed-out" },
        { E_DBUS_ERROR_MATCH_RULE_NOT_FOUND, "E_DBUS_ERROR_MATCH_RULE_NOT_FOUND", "match-rule-not-found" },
        { E_DBUS_ERROR_MATCH_RULE_INVALID, "E_DBUS_ERROR_MATCH_RULE_INVALID", "match-rule-invalid" },
        { E_DBUS_ERROR_SPAWN_EXEC_FAILED, "E_DBUS_ERROR_SPAWN_EXEC_FAILED", "spawn-exec-failed" },
        { E_DBUS_ERROR_SPAWN_FORK_FAILED, "E_DBUS_ERROR_SPAWN_FORK_FAILED", "spawn-fork-failed" },
        { E_DBUS_ERROR_SPAWN_CHILD_EXITED, "E_DBUS_ERROR_SPAWN_CHILD_EXITED", "spawn-child-exited" },
        { E_DBUS_ERROR_SPAWN_CHILD_SIGNALED, "E_DBUS_ERROR_SPAWN_CHILD_SIGNALED", "spawn-child-signaled" },
        { E_DBUS_ERROR_SPAWN_FAILED, "E_DBUS_ERROR_SPAWN_FAILED", "spawn-failed" },
        { E_DBUS_ERROR_SPAWN_SETUP_FAILED, "E_DBUS_ERROR_SPAWN_SETUP_FAILED", "spawn-setup-failed" },
        { E_DBUS_ERROR_SPAWN_CONFIG_INVALID, "E_DBUS_ERROR_SPAWN_CONFIG_INVALID", "spawn-config-invalid" },
        { E_DBUS_ERROR_SPAWN_SERVICE_INVALID, "E_DBUS_ERROR_SPAWN_SERVICE_INVALID", "spawn-service-invalid" },
        { E_DBUS_ERROR_SPAWN_SERVICE_NOT_FOUND, "E_DBUS_ERROR_SPAWN_SERVICE_NOT_FOUND", "spawn-service-not-found" },
        { E_DBUS_ERROR_SPAWN_PERMISSIONS_INVALID, "E_DBUS_ERROR_SPAWN_PERMISSIONS_INVALID", "spawn-permissions-invalid" },
        { E_DBUS_ERROR_SPAWN_FILE_INVALID, "E_DBUS_ERROR_SPAWN_FILE_INVALID", "spawn-file-invalid" },
        { E_DBUS_ERROR_SPAWN_NO_MEMORY, "E_DBUS_ERROR_SPAWN_NO_MEMORY", "spawn-no-memory" },
        { E_DBUS_ERROR_UNIX_PROCESS_ID_UNKNOWN, "E_DBUS_ERROR_UNIX_PROCESS_ID_UNKNOWN", "unix-process-id-unknown" },
        { E_DBUS_ERROR_INVALID_SIGNATURE, "E_DBUS_ERROR_INVALID_SIGNATURE", "invalid-signature" },
        { E_DBUS_ERROR_INVALID_FILE_CONTENT, "E_DBUS_ERROR_INVALID_FILE_CONTENT", "invalid-file-content" },
        { E_DBUS_ERROR_SELINUX_SECURITY_CONTEXT_UNKNOWN, "E_DBUS_ERROR_SELINUX_SECURITY_CONTEXT_UNKNOWN", "selinux-security-context-unknown" },
        { E_DBUS_ERROR_ADT_AUDIT_DATA_UNKNOWN, "E_DBUS_ERROR_ADT_AUDIT_DATA_UNKNOWN", "adt-audit-data-unknown" },
        { E_DBUS_ERROR_OBJECT_PATH_IN_USE, "E_DBUS_ERROR_OBJECT_PATH_IN_USE", "object-path-in-use" },
        { 0, NULL, NULL }
      };
      GType g_define_type_id =
        g_enum_register_static (g_intern_static_string ("EDBusError"), values);
      g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

  return g_define_type_id__volatile;
}

GType
e_dbus_property_info_flags_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;

  if (g_once_init_enter (&g_define_type_id__volatile))
    {
      static const GFlagsValue values[] = {
        { E_DBUS_PROPERTY_INFO_FLAGS_NONE, "E_DBUS_PROPERTY_INFO_FLAGS_NONE", "none" },
        { E_DBUS_PROPERTY_INFO_FLAGS_READABLE, "E_DBUS_PROPERTY_INFO_FLAGS_READABLE", "readable" },
        { E_DBUS_PROPERTY_INFO_FLAGS_WRITABLE, "E_DBUS_PROPERTY_INFO_FLAGS_WRITABLE", "writable" },
        { 0, NULL, NULL }
      };
      GType g_define_type_id =
        g_flags_register_static (g_intern_static_string ("EDBusPropertyInfoFlags"), values);
      g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

  return g_define_type_id__volatile;
}

GType
e_dbus_subtree_flags_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;

  if (g_once_init_enter (&g_define_type_id__volatile))
    {
      static const GFlagsValue values[] = {
        { E_DBUS_SUBTREE_FLAGS_NONE, "E_DBUS_SUBTREE_FLAGS_NONE", "none" },
        { E_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES, "E_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES", "dispatch-to-unenumerated-nodes" },
        { 0, NULL, NULL }
      };
      GType g_define_type_id =
        g_flags_register_static (g_intern_static_string ("EDBusSubtreeFlags"), values);
      g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

  return g_define_type_id__volatile;
}


/* Generated data ends here */

