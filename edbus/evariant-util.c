/*
 * Copyright © 2007, 2008 Ryan Lortie
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <string.h>
#include <glib.h>

#include "evariant-private.h"

/**
 * EVariantIter:
 *
 * An opaque structure type used to iterate over a container #EVariant
 * instance.
 *
 * The iter must be initialised with a call to e_variant_iter_init()
 * before using it.  After that, e_variant_iter_next() will return the
 * child values, in order.
 *
 * The iter may maintain a reference to the container #EVariant until
 * e_variant_iter_next() returns %NULL.  For this reason, it is
 * essential that you call e_variant_iter_next() until %NULL is
 * returned.  If you want to abort iterating part way through then use
 * e_variant_iter_cancel().
 */
typedef struct
{
  EVariant *value;
  EVariant *child;
  gsize length;
  gsize offset;
  gboolean cancelled;
} EVariantIterReal;

/**
 * e_variant_iter_init:
 * @iter: a #EVariantIter
 * @value: a container #EVariant instance
 * @returns: the number of items in the container
 *
 * Initialises the fields of a #EVariantIter and perpare to iterate
 * over the contents of @value.
 *
 * @iter is allowed to be completely uninitialised prior to this call;
 * it does not, for example, have to be cleared to zeros.  For this
 * reason, if @iter was already being used, you should first cancel it
 * with e_variant_iter_cancel().
 *
 * After this call, @iter holds a reference to @value.  The reference
 * will be automatically dropped once all values have been iterated
 * over or manually by calling e_variant_iter_cancel().
 *
 * This function returns the number of times that
 * e_variant_iter_next() will return non-%NULL.  You're not expected to
 * use this value, but it's there incase you wanted to know.
 **/
gsize
e_variant_iter_init (EVariantIter *iter,
                     EVariant     *value)
{
  EVariantIterReal *real = (EVariantIterReal *) iter;

  g_return_val_if_fail (iter != NULL, 0);
  g_return_val_if_fail (value != NULL, 0);

  g_assert (sizeof (EVariantIterReal) <= sizeof (EVariantIter));

  real->cancelled = FALSE;
  real->length = e_variant_n_children (value);
  real->offset = 0;
  real->child = NULL;

  if (real->length)
    real->value = e_variant_ref (value);
  else
    real->value = NULL;

  return real->length;
}

/**
 * e_variant_iter_next_value:
 * @iter: a #EVariantIter
 * @returns: a #EVariant for the next child
 *
 * Retreives the next child value from @iter.  In the event that no
 * more child values exist, %NULL is returned and @iter drops its
 * reference to the value that it was created with.
 *
 * The return value of this function is internally cached by the
 * @iter, so you don't have to unref it when you're done.  For this
 * reason, thought, it is important to ensure that you call
 * e_variant_iter_next() one last time, even if you know the number of
 * items in the container.
 *
 * It is permissable to call this function on a cancelled iter, in
 * which case %NULL is returned and nothing else happens.
 **/
EVariant *
e_variant_iter_next_value (EVariantIter *iter)
{
  EVariantIterReal *real = (EVariantIterReal *) iter;

  g_return_val_if_fail (iter != NULL, NULL);

  if (real->child)
    {
      e_variant_unref (real->child);
      real->child = NULL;
    }

  if (real->value == NULL)
    return NULL;

  real->child = e_variant_get_child_value (real->value, real->offset++);

  if (real->offset == real->length)
    {
      e_variant_unref (real->value);
      real->value = NULL;
    }

  return real->child;
}

/**
 * e_variant_iter_cancel:
 * @iter: a #EVariantIter
 *
 * Causes @iter to drop its reference to the container that it was
 * created with.  You need to call this on an iter if, for some
 * reason, you stopped iterating before reading the end.
 *
 * You do not need to call this in the normal case of visiting all of
 * the elements.  In this case, the reference will be automatically
 * dropped by e_variant_iter_next() just before it returns %NULL.
 *
 * It is permissable to call this function more than once on the same
 * iter.  It is permissable to call this function after the last
 * value.
 **/
void
e_variant_iter_cancel (EVariantIter *iter)
{
  EVariantIterReal *real = (EVariantIterReal *) iter;

  g_return_if_fail (iter != NULL);

  real->cancelled = TRUE;

  if (real->value)
    {
      e_variant_unref (real->value);
      real->value = NULL;
    }

  if (real->child)
    {
      e_variant_unref (real->child);
      real->child = NULL;
    }
}

/**
 * e_variant_iter_was_cancelled:
 * @iter: a #EVariantIter
 * @returns: %TRUE if e_variant_iter_cancel() was called
 *
 * Determines if e_variant_iter_cancel() was called on @iter.
 **/
gboolean
e_variant_iter_was_cancelled (EVariantIter *iter)
{
  EVariantIterReal *real = (EVariantIterReal *) iter;

  g_return_val_if_fail (iter != NULL, FALSE);

  return real->cancelled;
}

/* private */
gboolean
e_variant_iter_should_free (EVariantIter *iter)
{
  EVariantIterReal *real = (EVariantIterReal *) iter;

  return real->child != NULL;
}

/**
 * e_variant_has_type:
 * @value: a #EVariant instance
 * @pattern: a #EVariantType
 * @returns: %TRUE if the type of @value matches @type
 *
 * Checks if a value has a type matching the provided type.
 **/
gboolean
e_variant_has_type (EVariant           *value,
                    const EVariantType *type)
{
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  return e_variant_type_is_subtype_of (e_variant_get_type (value), type);
}

/**
 * e_variant_new_boolean:
 * @boolean: a #gboolean value
 * @returns: a new boolean #EVariant instance
 *
 * Creates a new boolean #EVariant instance -- either %TRUE or %FALSE.
 **/
EVariant *
e_variant_new_boolean (gboolean boolean)
{
  guint8 byte = !!boolean;

  return e_variant_load (E_VARIANT_TYPE_BOOLEAN,
                         &byte, 1, E_VARIANT_TRUSTED);
}

/**
 * e_variant_new_byte:
 * @byte: a #guint8 value
 * @returns: a new byte #EVariant instance
 *
 * Creates a new byte #EVariant instance.
 **/
EVariant *
e_variant_new_byte (guint8 byte)
{
  return e_variant_load (E_VARIANT_TYPE_BYTE,
                         &byte, 1, E_VARIANT_TRUSTED);
}

/**
 * e_variant_new_int16:
 * @int16: a #gint16 value
 * @returns: a new int16 #EVariant instance
 *
 * Creates a new int16 #EVariant instance.
 **/
EVariant *
e_variant_new_int16 (gint16 int16)
{
  return e_variant_load (E_VARIANT_TYPE_INT16,
                         &int16, 2, E_VARIANT_TRUSTED);
}

/**
 * e_variant_new_uint16:
 * @uint16: a #guint16 value
 * @returns: a new uint16 #EVariant instance
 *
 * Creates a new uint16 #EVariant instance.
 **/
EVariant *
e_variant_new_uint16 (guint16 uint16)
{
  return e_variant_load (E_VARIANT_TYPE_UINT16,
                         &uint16, 2, E_VARIANT_TRUSTED);
}

/**
 * e_variant_new_int32:
 * @int32: a #gint32 value
 * @returns: a new int32 #EVariant instance
 *
 * Creates a new int32 #EVariant instance.
 **/
EVariant *
e_variant_new_int32 (gint32 int32)
{
  return e_variant_load (E_VARIANT_TYPE_INT32,
                         &int32, 4, E_VARIANT_TRUSTED);
}

/**
 * e_variant_new_handle:
 * @handle: a #gint32 value
 * @returns: a new handle #EVariant instance
 *
 * Creates a new int32 #EVariant instance.
 **/
EVariant *
e_variant_new_handle (gint32 handle)
{
  return e_variant_load (E_VARIANT_TYPE_HANDLE,
                         &handle, 4, E_VARIANT_TRUSTED);
}

/**
 * e_variant_new_uint32:
 * @uint32: a #guint32 value
 * @returns: a new uint32 #EVariant instance
 *
 * Creates a new uint32 #EVariant instance.
 **/
EVariant *
e_variant_new_uint32 (guint32 uint32)
{
  return e_variant_load (E_VARIANT_TYPE_UINT32,
                         &uint32, 4, E_VARIANT_TRUSTED);
}

/**
 * e_variant_new_int64:
 * @int64: a #gint64 value
 * @returns: a new int64 #EVariant instance
 *
 * Creates a new int64 #EVariant instance.
 **/
EVariant *
e_variant_new_int64 (gint64 int64)
{
  return e_variant_load (E_VARIANT_TYPE_INT64,
                         &int64, 8, E_VARIANT_TRUSTED);
}

/**
 * e_variant_new_uint64:
 * @uint64: a #guint64 value
 * @returns: a new uint64 #EVariant instance
 *
 * Creates a new uint64 #EVariant instance.
 **/
EVariant *
e_variant_new_uint64 (guint64 uint64)
{
  return e_variant_load (E_VARIANT_TYPE_UINT64,
                         &uint64, 8, E_VARIANT_TRUSTED);
}

/**
 * e_variant_new_double:
 * @floating: a #gdouble floating point value
 * @returns: a new double #EVariant instance
 *
 * Creates a new double #EVariant instance.
 **/
EVariant *
e_variant_new_double (gdouble floating)
{
  return e_variant_load (E_VARIANT_TYPE_DOUBLE,
                         &floating, 8, E_VARIANT_TRUSTED);
}

/**
 * e_variant_new_string:
 * @string: a normal C nul-terminated string
 * @returns: a new string #EVariant instance
 *
 * Creates a string #EVariant with the contents of @string.
 **/
EVariant *
e_variant_new_string (const gchar *string)
{
  return e_variant_load (E_VARIANT_TYPE_STRING,
                         string, strlen (string) + 1, E_VARIANT_TRUSTED);
}

/**
 * e_variant_new_object_path:
 * @string: a normal C nul-terminated string
 * @returns: a new object path #EVariant instance
 *
 * Creates a DBus object path #EVariant with the contents of @string.
 * @string must be a valid DBus object path.  Use
 * e_variant_is_object_path() if you're not sure.
 **/
EVariant *
e_variant_new_object_path (const gchar *string)
{
  g_return_val_if_fail (e_variant_is_object_path (string), NULL);

  return e_variant_load (E_VARIANT_TYPE_OBJECT_PATH,
                         string, strlen (string) + 1,
                         E_VARIANT_TRUSTED);
}

/**
 * e_variant_is_object_path:
 * @string: a normal C nul-terminated string
 * @returns: %TRUE if @string is a DBus object path
 *
 * Determines if a given string is a valid DBus object path.  You
 * should ensure that a string is a valid DBus object path before
 * passing it to e_variant_new_object_path().
 *
 * A valid object path starts with '/' followed by zero or more
 * sequences of characters separated by '/' characters.  Each sequence
 * must contain only the characters "[A-Z][a-z][0-9]_".  No sequence
 * (including the one following the final '/' character) may be empty.
 **/
gboolean
e_variant_is_object_path (const gchar *string)
{
  /* according to DBus specification */
  gsize i;

  g_return_val_if_fail (string != NULL, FALSE);

  /* The path must begin with an ASCII '/' (integer 47) character */
  if (string[0] != '/')
    return FALSE;

  for (i = 1; string[i]; i++)
    /* Each element must only contain the ASCII characters
     * "[A-Z][a-z][0-9]_"
     */
    if (g_ascii_isalnum (string[i]) || string[i] == '_')
      ;

    /* must consist of elements separated by slash characters. */
    else if (string[i] == '/')
      {
        /* No element may be the empty string. */
        /* Multiple '/' characters cannot occur in sequence. */
        if (string[i - 1] == '/')
          return FALSE;
      }

    else
      return FALSE;

  /* A trailing '/' character is not allowed unless the path is the
   * root path (a single '/' character).
   */
  if (i > 1 && string[i - 1] == '/')
    return FALSE;

  return TRUE;
}

/**
 * e_variant_new_signature:
 * @string: a normal C nul-terminated string
 * @returns: a new signature #EVariant instance
 *
 * Creates a DBus type signature #EVariant with the contents of
 * @string.  @string must be a valid DBus type signature.  Use
 * e_variant_is_signature() if you're not sure.
 **/
EVariant *
e_variant_new_signature (const gchar *string)
{
  g_return_val_if_fail (e_variant_is_signature (string), NULL);

  return e_variant_load (E_VARIANT_TYPE_SIGNATURE,
                         string, strlen (string) + 1,
                         E_VARIANT_TRUSTED);
}

/**
 * e_variant_is_signature:
 * @string: a normal C nul-terminated string
 * @returns: %TRUE if @string is a DBus type signature
 *
 * Determines if a given string is a valid DBus type signature.  You
 * should ensure that a string is a valid DBus object path before
 * passing it to e_variant_new_signature().
 *
 * DBus type signatures consist of zero or more definite #EVariantType
 * strings in sequence.
 **/
gboolean
e_variant_is_signature (const gchar *string)
{
  gsize first_invalid;

  g_return_val_if_fail (string != NULL, FALSE);

  /* make sure no non-definite characters appear */
  first_invalid = strspn (string, "ybnqihuxtdvmasog(){}");
  if (string[first_invalid])
    return FALSE;

  /* make sure each type string is well-formed */
  while (*string)
    if (!e_variant_type_string_scan (string, NULL, &string))
      return FALSE;

  return TRUE;
}

/**
 * e_variant_new_variant:
 * @value: a #GVariance instance
 * @returns: a new variant #EVariant instance
 *
 * Boxes @value.  The result is a #EVariant instance representing a
 * variant containing the original value.
 **/
EVariant *
e_variant_new_variant (EVariant *value)
{
  EVariant **children;

  g_return_val_if_fail (value != NULL, NULL);

  children = g_slice_new (EVariant *);
  children[0] = e_variant_ref_sink (value);

  return e_variant_new_tree (E_VARIANT_TYPE_VARIANT,
                             children, 1,
                             e_variant_is_trusted (value));
}

/**
 * e_variant_get_boolean:
 * @value: a boolean #EVariant instance
 * @returns: %TRUE or %FALSE
 *
 * Returns the boolean value of @value.
 *
 * It is an error to call this function with a @value of any type
 * other than %E_VARIANT_TYPE_BOOLEAN.
 **/
gboolean
e_variant_get_boolean (EVariant *value)
{
  guint8 byte;

  g_return_val_if_fail (value != NULL, FALSE);
  g_assert (e_variant_has_type (value, E_VARIANT_TYPE_BOOLEAN));
  e_variant_store (value, &byte);

  return byte;
}

/**
 * e_variant_get_byte:
 * @value: a byte #EVariant instance
 * @returns: a #guchar
 *
 * Returns the byte value of @value.
 *
 * It is an error to call this function with a @value of any type
 * other than %E_VARIANT_TYPE_BYTE.
 **/
guint8
e_variant_get_byte (EVariant *value)
{
  guint8 byte;

  g_return_val_if_fail (value != NULL, 0);
  g_assert (e_variant_has_type (value, E_VARIANT_TYPE_BYTE));
  e_variant_store (value, &byte);

  return byte;
}

/**
 * e_variant_get_int16:
 * @value: a int16 #EVariant instance
 * @returns: a #gint16
 *
 * Returns the 16-bit signed integer value of @value.
 *
 * It is an error to call this function with a @value of any type
 * other than %E_VARIANT_TYPE_INT16.
 **/
gint16
e_variant_get_int16 (EVariant *value)
{
  gint16 int16;

  g_return_val_if_fail (value != NULL, 0);
  g_assert (e_variant_has_type (value, E_VARIANT_TYPE_INT16));
  e_variant_store (value, &int16);

  return int16;
}

/**
 * e_variant_get_uint16:
 * @value: a uint16 #EVariant instance
 * @returns: a #guint16
 *
 * Returns the 16-bit unsigned integer value of @value.
 *
 * It is an error to call this function with a @value of any type
 * other than %E_VARIANT_TYPE_UINT16.
 **/
guint16
e_variant_get_uint16 (EVariant *value)
{
  guint16 uint16;

  g_return_val_if_fail (value != NULL, 0);
  g_assert (e_variant_has_type (value, E_VARIANT_TYPE_UINT16));
  e_variant_store (value, &uint16);

  return uint16;
}

/**
 * e_variant_get_int32:
 * @value: a int32 #EVariant instance
 * @returns: a #gint32
 *
 * Returns the 32-bit signed integer value of @value.
 *
 * It is an error to call this function with a @value of any type
 * other than %E_VARIANT_TYPE_INT32.
 **/
gint32
e_variant_get_int32 (EVariant *value)
{
  gint32 int32;

  g_return_val_if_fail (value != NULL, 0);
  g_assert (e_variant_has_type (value, E_VARIANT_TYPE_INT32));
  e_variant_store (value, &int32);

  return int32;
}

/**
 * e_variant_get_handle:
 * @value: a handle #EVariant instance
 * @returns: a #gint32
 *
 * Returns the 32-bit signed integer value of @value.
 *
 * It is an error to call this function with a @value of any type
 * other than %E_VARIANT_TYPE_HANDLE.
 **/
gint32
e_variant_get_handle (EVariant *value)
{
  gint32 int32;

  g_return_val_if_fail (value != NULL, 0);
  g_assert (e_variant_has_type (value, E_VARIANT_TYPE_HANDLE));
  e_variant_store (value, &int32);

  return int32;
}

/**
 * e_variant_get_uint32:
 * @value: a uint32 #EVariant instance
 * @returns: a #guint32
 *
 * Returns the 32-bit unsigned integer value of @value.
 *
 * It is an error to call this function with a @value of any type
 * other than %E_VARIANT_TYPE_UINT32.
 **/
guint32
e_variant_get_uint32 (EVariant *value)
{
  guint32 uint32;

  g_return_val_if_fail (value != NULL, 0);
  g_assert (e_variant_has_type (value, E_VARIANT_TYPE_UINT32));
  e_variant_store (value, &uint32);

  return uint32;
}

/**
 * e_variant_get_int64:
 * @value: a int64 #EVariant instance
 * @returns: a #gint64
 *
 * Returns the 64-bit signed integer value of @value.
 *
 * It is an error to call this function with a @value of any type
 * other than %E_VARIANT_TYPE_INT64.
 **/
gint64
e_variant_get_int64 (EVariant *value)
{
  gint64 int64;

  g_return_val_if_fail (value != NULL, 0);
  g_assert (e_variant_has_type (value, E_VARIANT_TYPE_INT64));
  e_variant_store (value, &int64);

  return int64;
}

/**
 * e_variant_get_uint64:
 * @value: a uint64 #EVariant instance
 * @returns: a #guint64
 *
 * Returns the 64-bit unsigned integer value of @value.
 *
 * It is an error to call this function with a @value of any type
 * other than %E_VARIANT_TYPE_UINT64.
 **/
guint64
e_variant_get_uint64 (EVariant *value)
{
  guint64 uint64;

  g_return_val_if_fail (value != NULL, 0);
  g_assert (e_variant_has_type (value, E_VARIANT_TYPE_UINT64));
  e_variant_store (value, &uint64);

  return uint64;
}

/**
 * e_variant_get_double:
 * @value: a double #EVariant instance
 * @returns: a #gdouble
 *
 * Returns the double precision floating point value of @value.
 *
 * It is an error to call this function with a @value of any type
 * other than %E_VARIANT_TYPE_DOUBLE.
 **/
gdouble
e_variant_get_double (EVariant *value)
{
  gdouble floating;

  g_return_val_if_fail (value != NULL, 0);
  g_assert (e_variant_has_type (value, E_VARIANT_TYPE_DOUBLE));
  e_variant_store (value, &floating);

  return floating;
}

/**
 * e_variant_get_string:
 * @value: a string #EVariant instance
 * @length: a pointer to a #gsize, to store the length
 * @returns: the constant string
 *
 * Returns the string value of a #EVariant instance with a string
 * type.  This includes the types %E_VARIANT_TYPE_STRING,
 * %E_VARIANT_TYPE_OBJECT_PATH and %E_VARIANT_TYPE_SIGNATURE.
 *
 * If @length is non-%NULL then the length of the string (in bytes) is
 * returned there.  For trusted values, this information is already
 * known.  For untrusted values, a strlen() will be performed.
 *
 * It is an error to call this function with a @value of any type
 * other than those three.
 *
 * The return value remains valid as long as @value exists.
 **/
const gchar *
e_variant_get_string (EVariant *value,
                      gsize    *length)
{
  EVariantClass class;
  gboolean valid_string;
  const gchar *string;
  gssize size;

  g_return_val_if_fail (value != NULL, NULL);
  class = e_variant_classify (value);
  g_return_val_if_fail (class == E_VARIANT_CLASS_STRING ||
                        class == E_VARIANT_CLASS_OBJECT_PATH ||
                        class == E_VARIANT_CLASS_SIGNATURE, NULL);

  string = e_variant_get_data (value);
  size = e_variant_get_size (value);

  if (e_variant_is_trusted (value))
    {
      if (length)
        *length = size - 1;

      return string;
    }

  valid_string = string != NULL && size > 0 && string[size - 1] == '\0';

  switch (class)
  {
    case E_VARIANT_CLASS_STRING:
      if (valid_string)
        break;

      if (length)
        *length = 0;

      return "";

    case E_VARIANT_CLASS_OBJECT_PATH:
      if (valid_string && e_variant_is_object_path (string))
        break;

      if (length)
        *length = 1;

      return "/";

    case E_VARIANT_CLASS_SIGNATURE:
      if (valid_string && e_variant_is_signature (string))
        break;

      if (length)
        *length = 0;

      return "";

    default:
      g_assert_not_reached ();
  }

  if (length)
    *length = strlen (string);

  return string;
}

/**
 * e_variant_dup_string:
 * @value: a string #EVariant instance
 * @length: a pointer to a #gsize, to store the length
 * @returns: a newly allocated string
 *
 * Similar to e_variant_get_string() except that instead of returning
 * a constant string, the string is duplicated.
 *
 * The return value must be freed using g_free().
 **/
gchar *
e_variant_dup_string (EVariant *value,
                      gsize    *length)
{
  EVariantClass class;
  int size;

  g_return_val_if_fail (value != NULL, NULL);
  class = e_variant_classify (value);
  g_return_val_if_fail (class == E_VARIANT_CLASS_STRING ||
                        class == E_VARIANT_CLASS_OBJECT_PATH ||
                        class == E_VARIANT_CLASS_SIGNATURE, NULL);

  size = e_variant_get_size (value);

  if (length)
    *length = size - 1;

  return g_memdup (e_variant_get_data (value), size);
}

/**
 * e_variant_get_variant:
 * @value: a variant #GVariance instance
 * @returns: the item contained in the variant
 *
 * Unboxes @value.  The result is the #EVariant instance that was
 * contained in @value.
 **/
EVariant *
e_variant_get_variant (EVariant *value)
{
  g_return_val_if_fail (value != NULL, NULL);
  g_assert (e_variant_has_type (value, E_VARIANT_TYPE_VARIANT));

  return e_variant_get_child_value (value, 0);
}

/**
 * EVariantBuilder:
 *
 * An opaque type used to build container #EVariant instances one
 * child value at a time.
 */
struct _EVariantBuilder
{
  EVariantBuilder *parent;

  EVariantClass container_class;
  EVariantType *type;
  const EVariantType *expected;
  /* expected2 is always definite.  it is set if adding a second
   * element to an array (or open sub-builder thereof).  it is
   * required to ensure the following works correctly:
   *
   *   b = new(array, "a**");
   *   sb = open(b, array, "ai");
   *   b = close (sb);
   *
   *   sb = open (b, array, "a*");   <-- valid
   *   add (sb, "u", 1234);          <-- must fail here
   *
   * the 'valid' call is valid since the "a*" is no more general than
   * the element type of "aa*" (in fact, it is exactly equal) but the
   * 'must fail' call still needs to know that it requires element
   * type 'i'.  this requires keeping track of the two types
   * separately.
   */
  const EVariantType *expected2;
  gsize min_items;
  gsize max_items;

  EVariant **children;
  gsize children_allocated;
  gsize offset;
  int has_child : 1;
  int trusted : 1;
};

/**
 * E_VARIANT_BUILDER_ERROR:
 *
 * Error domain for #EVariantBuilder. Errors in this domain will be
 * from the #EVariantBuilderError enumeration.  See #GError for
 * information on error domains.
 **/
/**
 * EVariantBuilderError:
 * @E_VARIANT_BUILDER_ERROR_TOO_MANY: too many items have been added
 * (returned by e_variant_builder_check_add())
 * @E_VARIANT_BUILDER_ERROR_TOO_FEW: too few items have been added
 * (returned by e_variant_builder_check_end())
 * @E_VARIANT_BUILDER_ERROR_INFER: unable to infer the type of an
 * array or maybe (returned by e_variant_builder_check_end())
 * @E_VARIANT_BUILDER_ERROR_TYPE: the value is of the incorrect type
 * (returned by e_variant_builder_check_add())
 *
 * Errors codes returned by e_variant_builder_check_add() and
 * e_variant_builder_check_end().
 */

static void
e_variant_builder_resize (EVariantBuilder *builder,
                          int              new_allocated)
{
  EVariant **new_children;
  int i;

  g_assert_cmpint (builder->offset, <=, new_allocated);

  new_children = g_slice_alloc (sizeof (EVariant *) * new_allocated);

  for (i = 0; i < builder->offset; i++)
    new_children[i] = builder->children[i];

  g_slice_free1 (sizeof (EVariant **) * builder->children_allocated,
                 builder->children);
  builder->children = new_children;
  builder->children_allocated = new_allocated;
}

/**
 * e_variant_builder_add_value:
 * @builder: a #EVariantBuilder
 * @value: a #EVariant
 *
 * Adds @value to @builder.
 *
 * It is an error to call this function if @builder has an outstanding
 * child.  It is an error to call this function in any case that
 * e_variant_builder_check_add() would return %FALSE.
 **/
void
e_variant_builder_add_value (EVariantBuilder *builder,
                             EVariant        *value)
{
  g_return_if_fail (builder != NULL && value != NULL);
  g_return_if_fail (e_variant_builder_check_add (builder,
                                                 e_variant_get_type (value),
                                                 NULL));

  builder->trusted &= e_variant_is_trusted (value);

  if (builder->container_class == E_VARIANT_CLASS_TUPLE ||
      builder->container_class == E_VARIANT_CLASS_DICT_ENTRY)
    {
      if (builder->expected)
        builder->expected = e_variant_type_next (builder->expected);

      if (builder->expected2)
        builder->expected2 = e_variant_type_next (builder->expected2);
    }

  if (builder->container_class == E_VARIANT_CLASS_ARRAY &&
      builder->expected2 == NULL)
    builder->expected2 = e_variant_get_type (value);

  if (builder->offset == builder->children_allocated)
    e_variant_builder_resize (builder, builder->children_allocated * 2);

  builder->children[builder->offset++] = e_variant_ref_sink (value);
}

/**
 * e_variant_builder_open:
 * @parent: a #EVariantBuilder
 * @type: a #EVariantType, or %NULL
 * @returns: a new (child) #EVariantBuilder
 *
 * Opens a subcontainer inside the given @parent.
 *
 * It is not permissible to use any other builder calls with @parent
 * until @e_variant_builder_close() is called on the return value of
 * this function.
 *
 * It is an error to call this function if @parent has an outstanding
 * child.  It is an error to call this function in any case that
 * e_variant_builder_check_add() would return %FALSE.  It is an error
 * to call this function in any case that it's an error to call
 * e_variant_builder_new().
 *
 * If @type is %NULL and @parent was given type information, that
 * information is passed down to the subcontainer and constrains what
 * values may be added to it.
 **/
EVariantBuilder *
e_variant_builder_open (EVariantBuilder    *parent,
                        const EVariantType *type)
{
  EVariantBuilder *child;

  g_return_val_if_fail (parent != NULL && type != NULL, NULL);
  g_return_val_if_fail (e_variant_builder_check_add (parent, type, NULL),
                        NULL);
  g_return_val_if_fail (!parent->has_child, NULL);

  child = e_variant_builder_new (type);

  if (parent->expected2)
    {
      if (e_variant_type_is_maybe (type) || e_variant_type_is_array (type))
        child->expected2 = e_variant_type_element (parent->expected2);

      if (e_variant_type_is_tuple (type) ||
          e_variant_type_is_dict_entry (type))
        child->expected2 = e_variant_type_first (parent->expected2);

      /* in variant case, we don't want to propagate the type */
    }

  parent->has_child = TRUE;
  child->parent = parent;

  return child;
}

/**
 * e_variant_builder_close:
 * @child: a #EVariantBuilder
 * @returns: the original parent of @child
 *
 * This function closes a builder that was created with a call to
 * e_variant_builder_open().
 *
 * It is an error to call this function on a builder that was created
 * using e_variant_builder_new().  It is an error to call this
 * function if @child has an outstanding child.  It is an error to
 * call this function in any case that e_variant_builder_check_end()
 * would return %FALSE.
 **/
EVariantBuilder *
e_variant_builder_close (EVariantBuilder *child)
{
  EVariantBuilder *parent;
  EVariant *value;

  g_return_val_if_fail (child != NULL, NULL);
  g_return_val_if_fail (child->has_child == FALSE, NULL);
  g_return_val_if_fail (child->parent != NULL, NULL);
  g_assert (child->parent->has_child);

  parent = child->parent;
  parent->has_child = FALSE;
  parent = child->parent;
  child->parent = NULL;

  value = e_variant_builder_end (child);
  e_variant_builder_add_value (parent, value);

  return parent;
}

/**
 * e_variant_builder_new:
 * @tclass: a container #EVariantClass
 * @type: a type contained in @tclass, or %NULL
 * @returns: a #EVariantBuilder
 *
 * Creates a new #EVariantBuilder.
 *
 * @tclass must be specified and must be a container type.
 *
 * If @type is given, it constrains the child values that it is
 * permissible to add.  If @tclass is not %E_VARIANT_CLASS_VARIANT
 * then @type must be contained in @tclass and will match the type of
 * the final value.  If @tclass is %E_VARIANT_CLASS_VARIANT then
 * @type must match the value that must be added to the variant.
 *
 * After the builder is created, values are added using
 * e_variant_builder_add_value().
 *
 * After all the child values are added, e_variant_builder_end() ends
 * the process.
 **/
EVariantBuilder *
e_variant_builder_new (const EVariantType *type)
{
  EVariantBuilder *builder;

  g_return_val_if_fail (type != NULL, NULL);
  g_return_val_if_fail (e_variant_type_is_container (type), NULL);

  builder = g_slice_new (EVariantBuilder);
  builder->parent = NULL;
  builder->offset = 0;
  builder->has_child = FALSE;
  builder->type = e_variant_type_copy (type);
  builder->expected = NULL;
  builder->trusted = TRUE;
  builder->expected2 = NULL;

  switch (*(const gchar *) type)
    {
    case E_VARIANT_CLASS_VARIANT:
      builder->container_class = E_VARIANT_CLASS_VARIANT;
      builder->children_allocated = 1;
      builder->expected = NULL;
      builder->min_items = 1;
      builder->max_items = 1;
      break;

    case E_VARIANT_CLASS_ARRAY:
      builder->container_class = E_VARIANT_CLASS_ARRAY;
      builder->children_allocated = 8;
      builder->expected = e_variant_type_element (builder->type);
      builder->min_items = 0;
      builder->max_items = -1;
      break;

    case E_VARIANT_CLASS_MAYBE:
      builder->container_class = E_VARIANT_CLASS_MAYBE;
      builder->children_allocated = 1;
      builder->expected = e_variant_type_element (builder->type);
      builder->min_items = 0;
      builder->max_items = 1;
      break;

    case E_VARIANT_CLASS_DICT_ENTRY:
      builder->container_class = E_VARIANT_CLASS_DICT_ENTRY;
      builder->children_allocated = 2;
      builder->expected = e_variant_type_key (builder->type);
      builder->min_items = 2;
      builder->max_items = 2;
      break;

    case 'r': /* E_VARIANT_TYPE_TUPLE was given */
      builder->container_class = E_VARIANT_CLASS_TUPLE;
      builder->children_allocated = 8;
      builder->expected = NULL;
      builder->min_items = 0;
      builder->max_items = -1;
      break;

    case E_VARIANT_CLASS_TUPLE: /* a definite tuple type was given */
      builder->container_class = E_VARIANT_CLASS_TUPLE;
      builder->children_allocated = e_variant_type_n_items (type);
      builder->expected = e_variant_type_first (builder->type);
      builder->min_items = builder->children_allocated;
      builder->max_items = builder->children_allocated;
      break;

    default:
      g_assert_not_reached ();
   }

  builder->children = g_slice_alloc (sizeof (EVariant *) *
                                     builder->children_allocated);

  return builder;
}

/**
 * e_variant_builder_end:
 * @builder: a #EVariantBuilder
 * @returns: a new, floating, #EVariant
 *
 * Ends the builder process and returns the constructed value.
 *
 * It is an error to call this function on a #EVariantBuilder created
 * by a call to e_variant_builder_open().  It is an error to call this
 * function if @builder has an outstanding child.  It is an error to
 * call this function in any case that e_variant_builder_check_end()
 * would return %FALSE.
 **/
EVariant *
e_variant_builder_end (EVariantBuilder *builder)
{
  EVariantType *my_type;
  EVariant *value;

  g_return_val_if_fail (builder != NULL, NULL);
  g_return_val_if_fail (builder->parent == NULL, NULL);
  g_return_val_if_fail (e_variant_builder_check_end (builder, NULL), NULL);

  e_variant_builder_resize (builder, builder->offset);

  if (e_variant_type_is_definite (builder->type))
    {
      my_type = e_variant_type_copy (builder->type);
    }
  else
    {
      switch (builder->container_class)
        {
        case E_VARIANT_CLASS_MAYBE:
          {
            const EVariantType *child_type;

            child_type = e_variant_get_type (builder->children[0]);
            my_type = e_variant_type_new_maybe (child_type);
          }
          break;

        case E_VARIANT_CLASS_ARRAY:
          {
            const EVariantType *child_type;

            child_type = e_variant_get_type (builder->children[0]);
            my_type = e_variant_type_new_array (child_type);
          }
          break;

        case E_VARIANT_CLASS_TUPLE:
          {
            const EVariantType **types;
            gint i;

            types = g_new (const EVariantType *, builder->offset);
            for (i = 0; i < builder->offset; i++)
              types[i] = e_variant_get_type (builder->children[i]);
            my_type = e_variant_type_new_tuple (types, i);
            g_free (types);
          }
          break;

        case E_VARIANT_CLASS_DICT_ENTRY:
          {
            const EVariantType *key_type, *value_type;

            key_type = e_variant_get_type (builder->children[0]);
            value_type = e_variant_get_type (builder->children[1]);
            my_type = e_variant_type_new_dict_entry (key_type, value_type);
          }
        break;

        case E_VARIANT_CLASS_VARIANT:
          /* 'v' is surely a definite type, so this should never be hit */
        default:
          g_assert_not_reached ();
        }
    }

  value = e_variant_new_tree (my_type, builder->children,
                              builder->offset, builder->trusted);

  e_variant_type_free (builder->type);
  g_slice_free (EVariantBuilder, builder);
  e_variant_type_free (my_type);

  return value;
}

/**
 * e_variant_builder_check_end:
 * @builder: a #EVariantBuilder
 * @error: a #GError
 * @returns: %TRUE if ending is safe
 *
 * Checks if a call to e_variant_builder_end() or
 * e_variant_builder_close() would succeed.
 *
 * It is an error to call this function if @builder has a child (ie:
 * e_variant_builder_open() has been used on @builder and
 * e_variant_builder_close() has not yet been called).
 *
 * This function checks that a sufficient number of items have been
 * added to the builder.  For dictionary entries, for example, it
 * ensures that 2 items were added.
 *
 * This function also checks that array and maybe builders that were
 * created without definite type information contain at least one item
 * (without which it would be impossible to infer the definite type).
 *
 * If some sort of error (either too few items were added or type
 * inference is not possible) prevents the builder from being ended
 * then %FALSE is returned and @error is set.
 **/
gboolean
e_variant_builder_check_end (EVariantBuilder  *builder,
                             GError          **error)
{
  g_return_val_if_fail (builder != NULL, FALSE);
  g_return_val_if_fail (builder->has_child == FALSE, FALSE);

  /* this function needs to check two things:
   *
   * 1) that we have the number of items required
   * 2) in the case of an array or maybe type, either:
   *      a) we have a definite type already
   *      b) we have an item from which to infer the type
   */

  if (builder->offset < builder->min_items)
    {
      gchar *type_str;

      type_str = e_variant_type_dup_string (builder->type);
      g_set_error (error, E_VARIANT_BUILDER_ERROR,
                   E_VARIANT_BUILDER_ERROR_TOO_FEW,
                   "this container (type '%s') must contain %"G_GSIZE_FORMAT
                   " values but only %"G_GSIZE_FORMAT "have been given",
                   type_str, builder->min_items, builder->offset);
      g_free (type_str);

      return FALSE;
    }

  if (!e_variant_type_is_definite (builder->type) &&
      (builder->container_class == E_VARIANT_CLASS_MAYBE ||
       builder->container_class == E_VARIANT_CLASS_ARRAY) &&
      builder->offset == 0)
    {
      g_set_error (error, E_VARIANT_BUILDER_ERROR,
                   E_VARIANT_BUILDER_ERROR_INFER,
                   "unable to infer type with no values present");
      return FALSE;
    }

  return TRUE;
}

/**
 * e_variant_builder_check_add:
 * @builder: a #EVariantBuilder
 * @tclass: a #EVariantClass
 * @type: a #EVariantType, or %NULL
 * @error: a #GError
 * @returns: %TRUE if adding is safe
 *
 * Does all sorts of checks to ensure that it is safe to call
 * e_variant_builder_add() or e_variant_builder_open().
 *
 * It is an error to call this function if @builder has a child (ie:
 * e_variant_builder_open() has been used on @builder and
 * e_variant_builder_close() has not yet been called).
 *
 * It is an error to call this function with an invalid @tclass
 * (including %E_VARIANT_CLASS_INVALID) or a class that's not the
 * smallest class for some definite type (for example,
 * %E_VARIANT_CLASS_ALL).
 *
 * If @type is non-%NULL this function first checks that it is a
 * member of @tclass (except, as with e_variant_builder_new(), if
 * @tclass is %E_VARIANT_CLASS_VARIANT then any @type is OK).
 *
 * The function then checks if any child of class @tclass (and type
 * @type, if given) would be suitable for adding to the builder.  If
 * @type is non-%NULL and is non-definite then all definite types
 * matching @type must be suitable for adding (ie: @type must be equal
 * to or less general than the type expected by the builder).
 *
 * In the case of an array that already has at least one item in it,
 * this function performs an additional check to ensure that @tclass
 * and @type match the items already in the array.  @type, if given,
 * need not be definite in order for this check to pass.
 *
 * Errors are flagged in the event that the builder contains too many
 * items or the addition would cause a type error.
 *
 * If @tclass is specified and is a container type and @type is not
 * given then there is no guarantee that adding a value of that class
 * would not fail.  Calling e_variant_builder_open() with that @tclass
 * (and @type as %NULL) would succeed, though.
 *
 * In the case that any error is detected @error is set and %FALSE is
 * returned.
 **/
gboolean
e_variant_builder_check_add (EVariantBuilder     *builder,
                             const EVariantType  *type,
                             GError             **error)
{
  g_return_val_if_fail (builder != NULL, FALSE);
  g_return_val_if_fail (type != NULL, FALSE);
  g_return_val_if_fail (builder->has_child == FALSE, FALSE);

  /* this function needs to check two things:
   *
   * 1) that we have not exceeded the number of allowable items
   * 2) that the incoming type matches the expected types like so:
   *
   *      expected2 <= type <= expected
   *
   * but since expected2 or expected could be %NULL, we need explicit checks:
   *
   *   type <= expected
   *   expected2 <= type
   *
   * (we already know expected2 <= expected)
   */

  if (builder->offset == builder->max_items)
    {
      gchar *type_str;

      type_str = e_variant_type_dup_string (builder->type);
      g_set_error (error, E_VARIANT_BUILDER_ERROR,
                   E_VARIANT_BUILDER_ERROR_TOO_MANY,
                   "this container (type '%s') may not contain more than"
                   " %"G_GSIZE_FORMAT " values", type_str, builder->offset);
      g_free (type_str);

      return FALSE;
    }

  /* type <= expected */
  if (builder->expected &&
      !e_variant_type_is_subtype_of (type, builder->expected))
    {
      gchar *expected_str, *type_str;

      expected_str = e_variant_type_dup_string (builder->expected);
      type_str = e_variant_type_dup_string (type);
      g_set_error (error, E_VARIANT_BUILDER_ERROR,
                   E_VARIANT_BUILDER_ERROR_TYPE,
                   "type '%s' does not match expected type '%s'",
                   type_str, expected_str);
      g_free (expected_str);
      g_free (type_str);
      return FALSE;
    }

  /* expected2 <= type */
  if (builder->expected2 &&
      !e_variant_type_is_subtype_of (builder->expected2, type))
    {
      g_set_error (error, E_VARIANT_BUILDER_ERROR,
                   E_VARIANT_BUILDER_ERROR_TYPE,
                   "all elements in an array must have the same type");
      return FALSE;
    }

  return TRUE;
}

/**
 * e_variant_builder_cancel:
 * @builder: a #EVariantBuilder
 *
 * Cancels the build process.  All memory associated with @builder is
 * freed.  If the builder was created with e_variant_builder_open()
 * then all ancestors are also freed.
 **/
void
e_variant_builder_cancel (EVariantBuilder *builder)
{
  EVariantBuilder *parent;

  g_return_if_fail (builder != NULL);

  do
    {
      gsize i;

      for (i = 0; i < builder->offset; i++)
        e_variant_unref (builder->children[i]);

      g_slice_free1 (sizeof (EVariant *) * builder->children_allocated,
                     builder->children);

      if (builder->type)
        e_variant_type_free (builder->type);

      parent = builder->parent;
      g_slice_free (EVariantBuilder, builder);
    }
  while ((builder = parent));
}

/**
 * e_variant_flatten:
 * @value: a #EVariant instance
 *
 * Flattens @value.
 *
 * This is a strange function with no direct effects but some
 * noteworthy side-effects.  Essentially, it ensures that @value is in
 * its most favourable form.  This involves ensuring that the value is
 * serialised and in machine byte order.  The investment of time now
 * can pay off by allowing shorter access times for future calls and
 * typically results in a reduction of memory consumption.
 *
 * A value received over the network or read from the disk in machine
 * byte order is already flattened.
 *
 * Some of the effects of this call are that any future accesses to
 * the data of @value (or children taken from it after flattening)
 * will occur in O(1) time.  Also, any data accessed from such a child
 * value will continue to be valid even after the child has been
 * destroyed, as long as @value still exists (since the contents of
 * the children are now serialised as part of the parent).
 **/
void
e_variant_flatten (EVariant *value)
{
  g_return_if_fail (value != NULL);
  e_variant_get_data (value);
}

/**
 * e_variant_get_type_string:
 * @value: a #EVariant
 * @returns: the type string for the type of @value
 *
 * Returns the type string of @value.  Unlike the result of calling
 * e_variant_type_peek_string(), this string is nul-terminated.  This
 * string belongs to #EVariant and must not be freed.
 **/
const gchar *
e_variant_get_type_string (EVariant *value)
{
  g_return_val_if_fail (value != NULL, NULL);
  return (const gchar *) e_variant_get_type (value);
}

/**
 * e_variant_is_basic:
 * @value: a #EVariant
 * @returns: %TRUE if @value has a basic type
 *
 * Determines if @value has a basic type.  Values with basic types may
 * be used as the keys of dictionary entries.
 *
 * This function is the exact opposite of e_variant_is_container().
 **/
gboolean
e_variant_is_basic (EVariant *value)
{
  g_return_val_if_fail (value != NULL, FALSE);
  return e_variant_type_is_basic (e_variant_get_type (value));
}

/**
 * e_variant_is_container:
 * @value: a #EVariant
 * @returns: %TRUE if @value has a basic type
 *
 * Determines if @value has a container type.  Values with container
 * types may be used with the functions e_variant_n_children() and
 * e_variant_get_child().
 *
 * This function is the exact opposite of e_variant_is_basic().
 **/
gboolean
e_variant_is_container (EVariant *value)
{
  g_return_val_if_fail (value != NULL, FALSE);
  return e_variant_type_is_container (e_variant_get_type (value));
}

#include <stdio.h>
void
e_variant_dump_data (EVariant *value)
{
  const guchar *data;
  const guchar *end;
  char row[3*16+2];
  gsize data_size;
  gsize i;

  g_return_if_fail (value != NULL);
  data_size = e_variant_get_size (value);

  g_debug ("EVariant at %p (type '%s', %" G_GSIZE_FORMAT " bytes):",
           value, e_variant_get_type_string (value), data_size);

  data = e_variant_get_data (value);
  end = data + data_size;

  i = 0;
  row[3*16+1] = '\0';
  while (data < end || (i & 15))
    {
      if ((i & 15) == (((gsize) data) & 15) && data < end)
        sprintf (&row[3 * (i & 15) + (i & 8)/8], "%02x  ", *data++);
      else
        sprintf (&row[3 * (i & 15) + (i & 8)/8], "    ");

      if ((++i & 15) == 0)
        {
          g_debug ("   %s", row);
          memset (row, 'q', 3 * 16 + 1);
        }
    }

  g_debug ("==");
}

EVariant *
e_variant_deep_copy (EVariant *value)
{
  switch (e_variant_classify (value))
  {
    case E_VARIANT_CLASS_BOOLEAN:
      return e_variant_new_boolean (e_variant_get_boolean (value));

    case E_VARIANT_CLASS_BYTE:
      return e_variant_new_byte (e_variant_get_byte (value));

    case E_VARIANT_CLASS_INT16:
      return e_variant_new_int16 (e_variant_get_int16 (value));

    case E_VARIANT_CLASS_UINT16:
      return e_variant_new_uint16 (e_variant_get_uint16 (value));

    case E_VARIANT_CLASS_INT32:
      return e_variant_new_int32 (e_variant_get_int32 (value));

    case E_VARIANT_CLASS_UINT32:
      return e_variant_new_uint32 (e_variant_get_uint32 (value));

    case E_VARIANT_CLASS_INT64:
      return e_variant_new_int64 (e_variant_get_int64 (value));

    case E_VARIANT_CLASS_UINT64:
      return e_variant_new_uint64 (e_variant_get_uint64 (value));

    case E_VARIANT_CLASS_DOUBLE:
      return e_variant_new_double (e_variant_get_double (value));

    case E_VARIANT_CLASS_STRING:
      return e_variant_new_string (e_variant_get_string (value, NULL));

    case E_VARIANT_CLASS_OBJECT_PATH:
      return e_variant_new_object_path (e_variant_get_string (value, NULL));

    case E_VARIANT_CLASS_SIGNATURE:
      return e_variant_new_signature (e_variant_get_string (value, NULL));

    case E_VARIANT_CLASS_VARIANT:
      {
        EVariant *inside, *new;

        inside = e_variant_get_variant (value);
        new = e_variant_new_variant (e_variant_deep_copy (inside));
        e_variant_unref (inside);

        return new;
      }

    case E_VARIANT_CLASS_HANDLE:
      return e_variant_new_handle (e_variant_get_handle (value));

    case E_VARIANT_CLASS_MAYBE:
    case E_VARIANT_CLASS_ARRAY:
    case E_VARIANT_CLASS_TUPLE:
    case E_VARIANT_CLASS_DICT_ENTRY:
      {
        EVariantBuilder *builder;
        EVariantIter iter;
        EVariant *child;

        builder = e_variant_builder_new (e_variant_get_type (value));
        e_variant_iter_init (&iter, value);

        while ((child = e_variant_iter_next_value (&iter)))
          e_variant_builder_add_value (builder, e_variant_deep_copy (child));

        return e_variant_builder_end (builder);
      }

    default:
      g_assert_not_reached ();
  }
}

/**
 * e_variant_new_strv:
 * @strv: an array of strings
 * @length: the length of @strv, or -1
 * @returns: a new floating #EVariant instance
 *
 * Constructs an array of strings #EVariant from the given array of
 * strings.
 *
 * If @length is not -1 then it gives the maximum length of @strv.  In
 * any case, a %NULL pointer in @strv is taken as a terminator.
 **/
EVariant *
e_variant_new_strv (const gchar * const *strv,
                    gint                 length)
{
  EVariantBuilder *builder;

  g_return_val_if_fail (strv != NULL || length == 0, NULL);

  builder = e_variant_builder_new (E_VARIANT_TYPE ("as"));
  while (length-- && *strv)
    e_variant_builder_add (builder, "s", *strv++);

  return e_variant_builder_end (builder);
}

/**
 * e_variant_get_strv:
 * @value: an array of strings #EVariant
 * @length: the length of the result, or %NULL
 * @returns: an array of constant strings
 *
 * Gets the contents of an array of strings #EVariant.  This call
 * makes a shallow copy; the return result should be released with
 * g_free(), but the individual strings must not be modified.
 *
 * If @length is non-%NULL then the number of elements in the result
 * is stored there.  In any case, the resulting array will be
 * %NULL-terminated.
 *
 * For an empty array, @length will be set to 0 and a pointer to a
 * %NULL pointer will be returned.
 **/
const gchar **
e_variant_get_strv (EVariant *value,
                    gint     *length)
{
  const gchar **result;
  gint my_length;
  gint i;

  g_return_val_if_fail (value != NULL, NULL);
  g_return_val_if_fail (e_variant_has_type (value, E_VARIANT_TYPE ("as")),
                        NULL);

  e_variant_flatten (value);

  my_length = e_variant_n_children (value);
  result = g_new (const gchar *, my_length + 1);

  if (length)
    *length = my_length;

  for (i = 0; i < my_length; i++)
    {
      EVariant *child = e_variant_get_child_value (value, i);
      result[i] = e_variant_get_string (child, NULL);
      e_variant_unref (child);
    }
  result[i] = NULL;

  return result;
}

/**
 * e_variant_dup_strv:
 * @value: an array of strings #EVariant
 * @length: the length of the result, or %NULL
 * @returns: an array of constant strings
 *
 * Gets the contents of an array of strings #EVariant.  This call
 * makes a deep copy; the return result should be released with
 * g_strfreev().
 *
 * If @length is non-%NULL then the number of elements in the result
 * is stored there.  In any case, the resulting array will be
 * %NULL-terminated.
 *
 * For an empty array, @length will be set to 0 and a pointer to a
 * %NULL pointer will be returned.
 **/
gchar **
e_variant_dup_strv (EVariant *value,
                    gint     *length)
{
  gchar **result;
  gint my_length;
  gint i;

  g_return_val_if_fail (value != NULL, NULL);
  g_return_val_if_fail (e_variant_has_type (value, E_VARIANT_TYPE ("as")),
                        NULL);

  e_variant_flatten (value);

  my_length = e_variant_n_children (value);
  result = g_new (gchar *, my_length + 1);

  if (length)
    *length = my_length;

  for (i = 0; i < my_length; i++)
    {
      EVariant *child = e_variant_get_child_value (value, i);
      result[i] = e_variant_dup_string (child, NULL);
      e_variant_unref (child);
    }
  result[i] = NULL;

  return result;
}

/**
 * e_variant_lookup_value:
 * @dictionary: a #EVariant dictionary, keyed by strings
 * @key: a string to lookup in the dictionary
 * @returns: the value corresponding to @key, or %NULL

 * Looks up @key in @dictionary.  This is essentially a convenience
 * function for dealing with the extremely common case of a dictionary
 * keyed by strings.
 *
 * In the case that the key is found, the corresponding value is
 * returned; not the dictionary entry.  If the key is not found then
 * this function returns %NULL.
 **/
EVariant *
e_variant_lookup_value (EVariant    *dictionary,
                        const gchar *key)
{
  EVariantIter iter;
  const gchar *_key;
  EVariant *value;
  EVariant *result = NULL;

  g_return_val_if_fail (dictionary != NULL, NULL);
  g_return_val_if_fail (key != NULL, NULL);

  e_variant_iter_init (&iter, dictionary);
  while (e_variant_iter_next (&iter, "{&s*}", &_key, &value))
    if (strcmp (_key, key) == 0)
      {
        result = e_variant_ref (value);
        e_variant_iter_cancel (&iter);
      }

  return result;
}

/**
 * e_variant_from_file:
 * @type: the #EVariantType of the new variant
 * @filename: the filename to load from
 * @flags: zero or more #EVariantFlags
 * @error: a pointer to a #GError, or %NULL
 * @returns: a new #EVariant instance, or %NULL
 *
 * Utility function to load a #EVariant from the contents of a file,
 * using a #GMappedFile.
 *
 * This function attempts to open @filename using #GMappedFile and then
 * calls e_variant_from_data() on the result.  As with that function,
 * @type may be %NULL.
 **/
EVariant *
e_variant_from_file (const EVariantType *type,
                     const gchar        *filename,
                     EVariantFlags       flags,
                     GError             **error)
{
  GMappedFile *mapped;
  gconstpointer data;
  gsize size;

  g_return_val_if_fail (filename != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  mapped = g_mapped_file_new (filename, FALSE, error);

  if (mapped == NULL)
    return NULL;

  data = g_mapped_file_get_contents (mapped);
  size = g_mapped_file_get_length (mapped);

  if (size == 0)
  /* #595535 */
    data = NULL;

  return e_variant_from_data (type, data, size, flags,
                              (GDestroyNotify) g_mapped_file_unref,
                              mapped);
}
