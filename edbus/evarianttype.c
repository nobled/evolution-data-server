/*
 * Copyright © 2007, 2008 Ryan Lortie
 * Copyright © 2009, 2010 Codethink Limited
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the licence, or (at your option) any later version.
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
 *
 * Author: Ryan Lortie <desrt@desrt.ca>
 */

#include "evarianttype.h"

#include <glib/gtestutils.h>
#include <glib/gstrfuncs.h>

#include <string.h>

/**
 * SECTION: gvarianttype
 * @title: EVariantType
 * @short_description: introduction to the EVariant type system
 * @see_also: #EVariantType, #EVariant
 *
 * This section introduces the EVariant type system.  It is based, in
 * large part, on the DBus type system, with two major changes and some minor
 * lifting of restrictions.  The <ulink
 * url='http://dbus.freedesktop.org/doc/dbus-specification.html'>DBus
 * specification</ulink>, therefore, provides a significant amount of
 * information that is useful when working with EVariant.
 *
 * The first major change with respect to the DBus type system is the
 * introduction of maybe (or "nullable") types.  Any type in EVariant can be
 * converted to a maybe type, in which case, "nothing" (or "null") becomes a
 * valid value.  Maybe types have been added by introducing the
 * character "<literal>m</literal>" to type strings.
 *
 * The second major change is that the EVariant type system supports the
 * concept of "indefinite types" -- types that are less specific than
 * the normal types found in DBus.  For example, it is possible to speak
 * of "an array of any type" in EVariant, where the DBus type system
 * would require you to speak of "an array of integers" or "an array of
 * strings".  Indefinite types have been added by introducing the
 * characters "<literal>*</literal>", "<literal>?</literal>" and
 * "<literal>r</literal>" to type strings.
 *
 * Finally, all arbitrary restrictions relating to the complexity of
 * types are lifted along with the restriction that dictionary entries
 * may only appear nested inside of arrays.
 *
 * Just as in DBus, EVariant types are described with strings ("type
 * strings").  Subject to the differences mentioned above, these strings
 * are of the same form as those found in DBus.  Note, however: DBus
 * always works in terms of messages and therefore individual type
 * strings appear nowhere in its interface.  Instead, "signatures"
 * are a concatenation of the strings of the type of each argument in a
 * message.  EVariant deals with single values directly so EVariant type
 * strings always describe the type of exactly one value.  This means
 * that a DBus signature string is generally not a valid EVariant type
 * string -- except in the case that it is the signature of a message
 * containing exactly one argument.
 *
 * An indefinite type is similar in spirit to what may be called an
 * abstract type in other type systems.  No value can exist that has an
 * indefinite type as its type, but values can exist that have types
 * that are subtypes of indefinite types.  That is to say,
 * e_variant_get_type() will never return an indefinite type, but
 * calling e_variant_is_a() with an indefinite type may return %TRUE.
 * For example, you can not have a value that represents "an array of no
 * particular type", but you can have an "array of integers" which
 * certainly matches the type of "an array of no particular type", since
 * "array of integers" is a subtype of "array of no particular type".
 *
 * This is similar to how instances of abstract classes may not
 * directly exist in other type systems, but instances of their
 * non-abstract subtypes may.  For example, in GTK, no object that has
 * the type of #GtkBin can exist (since #GtkBin is an abstract class),
 * but a #GtkWindow can certainly be instantiated, and you would say
 * that the #GtkWindow is a #GtkBin (since #GtkWindow is a subclass of
 * #GtkBin).
 *
 * A detailed description of EVariant type strings is given here:
 *
 * <refsect2 id='gvariant-typestrings'>
 *  <title>EVariant Type Strings</title>
 *  <para>
 *   A EVariant type string can be any of the following:
 *  </para>
 *  <itemizedlist>
 *   <listitem>
 *    <para>
 *     any basic type string (listed below)
 *    </para>
 *   </listitem>
 *   <listitem>
 *    <para>
 *     "<literal>v</literal>", "<literal>r</literal>" or
 *     "<literal>*</literal>"
 *    </para>
 *   </listitem>
 *   <listitem>
 *    <para>
 *     one of the characters '<literal>a</literal>' or
 *     '<literal>m</literal>', followed by another type string
 *    </para>
 *   </listitem>
 *   <listitem>
 *    <para>
 *     the character '<literal>(</literal>', followed by a concatenation
 *     of zero or more other type strings, followed by the character
 *     '<literal>)</literal>'
 *    </para>
 *   </listitem>
 *   <listitem>
 *    <para>
 *     the character '<literal>{</literal>', followed by a basic type
 *     string (see below), followed by another type string, followed by
 *     the character '<literal>}</literal>'
 *    </para>
 *   </listitem>
 *  </itemizedlist>
 *  <para>
 *   A basic type string describes a basic type (as per
 *   e_variant_type_is_basic()) and is always a single
 *   character in length.  The valid basic type strings are
 *   "<literal>b</literal>", "<literal>y</literal>",
 *   "<literal>n</literal>", "<literal>q</literal>",
 *   "<literal>i</literal>", "<literal>u</literal>",
 *   "<literal>x</literal>", "<literal>t</literal>",
 *   "<literal>h</literal>", "<literal>d</literal>",
 *   "<literal>s</literal>", "<literal>o</literal>",
 *   "<literal>g</literal>" and "<literal>?</literal>".
 *  </para>
 *  <para>
 *   The above definition is recursive to arbitrary depth.
 *   "<literal>aaaaai</literal>" and "<literal>(ui(nq((y)))s)</literal>"
 *   are both valid type strings, as is
 *   "<literal>a(aa(ui)(qna{ya(yd)}))</literal>".
 *  </para>
 *  <para>
 *   The meaning of each of the characters is as follows:
 *  </para>
 *  <informaltable>
 *   <tgroup cols='2'>
 *    <tbody>
 *     <row>
 *      <entry>
 *       <para>
 *        <emphasis role='strong'>Character</emphasis>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        <emphasis role='strong'>Meaning</emphasis>
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>b</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        the type string of %E_VARIANT_TYPE_BOOLEAN; a boolean value.
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>y</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        the type string of %E_VARIANT_TYPE_BYTE; a byte.
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>n</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        the type string of %E_VARIANT_TYPE_INT16; a signed 16 bit
 *        integer.
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>q</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        the type string of %E_VARIANT_TYPE_UINT16; an unsigned 16 bit
 *        integer.
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>i</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        the type string of %E_VARIANT_TYPE_INT32; a signed 32 bit
 *        integer.
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>u</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        the type string of %E_VARIANT_TYPE_UINT32; an unsigned 32 bit
 *        integer.
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>x</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        the type string of %E_VARIANT_TYPE_INT64; a signed 64 bit
 *        integer.
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>t</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        the type string of %E_VARIANT_TYPE_UINT64; an unsigned 64 bit
 *        integer.
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>h</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        the type string of %E_VARIANT_TYPE_HANDLE; a signed 32 bit
 *        value that, by convention, is used as an index into an array
 *        of file descriptors that are sent alongside a DBus message.
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>d</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        the type string of %E_VARIANT_TYPE_DOUBLE; a double precision
 *        floating point value.
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>s</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        the type string of %E_VARIANT_TYPE_STRING; a string.
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>o</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        the type string of %E_VARIANT_TYPE_OBJECT_PATH; a string in
 *        the form of a DBus object path.
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>g</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        the type string of %E_VARIANT_TYPE_STRING; a string in the
 *        form of a DBus type signature.
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>?</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        the type string of %E_VARIANT_TYPE_BASIC; an indefinite type
 *        that is a supertype of any of the basic types.
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>v</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        the type string of %E_VARIANT_TYPE_VARIANT; a container type
 *        that contain any other type of value.
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>a</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        used as a prefix on another type string to mean an array of
 *        that type; the type string "<literal>ai</literal>", for
 *        example, is the type of an array of 32 bit signed integers.
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>m</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        used as a prefix on another type string to mean a "maybe", or
 *        "nullable", version of that type; the type string
 *        "<literal>ms</literal>", for example, is the type of a value
 *        that maybe contains a string, or maybe contains nothing.
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>()</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        used to enclose zero or more other concatenated type strings
 *        to create a tuple type; the type string
 *        "<literal>(is)</literal>", for example, is the type of a pair
 *        of an integer and a string.
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>r</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        the type string of %E_VARIANT_TYPE_TUPLE; an indefinite type
 *        that is a supertype of any tuple type, regardless of the
 *        number of items.
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>{}</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        used to enclose a basic type string concatenated with another
 *        type string to create a dictionary entry type, which usually
 *        appears inside of an array to form a dictionary; the type
 *        string "<literal>a{sd}</literal>", for example, is the type of
 *        a dictionary that maps strings to double precision floating
 *        point values.
 *       </para>
 *       <para>
 *        The first type (the basic type) is the key type and the second
 *        type is the value type.  The reason that the first type is
 *        restricted to being a basic type is so that it can easily be
 *        hashed.
 *       </para>
 *      </entry>
 *     </row>
 *     <row>
 *      <entry>
 *       <para>
 *        <literal>*</literal>
 *       </para>
 *      </entry>
 *      <entry>
 *       <para>
 *        the type string of %E_VARIANT_TYPE_ANY; the indefinite type
 *        that is a supertype of all types.  Note that, as with all type
 *        strings, this character represents exactly one type.  It
 *        cannot be used inside of tuples to mean "any number of items".
 *       </para>
 *      </entry>
 *     </row>
 *    </tbody>
 *   </tgroup>
 *  </informaltable>
 *  <para>
 *   Any type string of a container that contains an indefinite type is,
 *   itself, an indefinite type.  For example, the type string
 *   "<literal>a*</literal>" (corresponding to %E_VARIANT_TYPE_ARRAY) is
 *   an indefinite type that is a supertype of every array type.
 *   "<literal>(*s)</literal>" is a supertype of all tuples that
 *   contain exactly two items where the second item is a string.
 *  </para>
 *  <para>
 *   "<literal>a{?*}</literal>" is an indefinite type that is a
 *   supertype of all arrays containing dictionary entries where the key
 *   is any basic type and the value is any type at all.  This is, by
 *   definition, a dictionary, so this type string corresponds to
 *   %E_VARIANT_TYPE_DICTIONARY.  Note that, due to the restriction that
 *   the key of a dictionary entry must be a basic type,
 *   "<literal>{**}</literal>" is not a valid type string.
 *  </para>
 * </refsect2>
 */


static gboolean
e_variant_type_check (const EVariantType *type)
{
  const gchar *type_string;

  if (type == NULL)
    return FALSE;

  type_string = (const gchar *) type;
#ifndef G_DISABLE_CHECKS
  return e_variant_type_string_scan (type_string, NULL, NULL);
#else
  return TRUE;
#endif
}

/**
 * e_variant_type_string_scan:
 * @string: a pointer to any string
 * @limit: the end of @string, or %NULL
 * @endptr: location to store the end pointer, or %NULL
 * @returns: %TRUE if a valid type string was found
 *
 * Scan for a single complete and valid EVariant type string in @string.
 * The memory pointed to by @limit (or bytes beyond it) is never
 * accessed.
 *
 * If a valid type string is found, @endptr is updated to point to the
 * first character past the end of the string that was found and %TRUE
 * is returned.
 *
 * If there is no valid type string starting at @string, or if the type
 * string does not end before @limit then %FALSE is returned.
 *
 * For the simple case of checking if a string is a valid type string,
 * see e_variant_type_string_is_valid().
 *
 * Since: 2.24
 **/
gboolean
e_variant_type_string_scan (const gchar  *string,
                            const gchar  *limit,
                            const gchar **endptr)
{
  g_return_val_if_fail (string != NULL, FALSE);

  if (string == limit || *string == '\0')
    return FALSE;

  switch (*string++)
    {
    case '(':
      while (string == limit || *string != ')')
        if (!e_variant_type_string_scan (string, limit, &string))
          return FALSE;

      string++;
      break;

    case '{':
      if (string == limit || *string == '\0' ||                    /* { */
          !strchr ("bynqihuxtdsog?", *string++) ||                 /* key */
          !e_variant_type_string_scan (string, limit, &string) ||  /* value */
          string == limit || *string++ != '}')                     /* } */
        return FALSE;

      break;

    case 'm': case 'a':
      return e_variant_type_string_scan (string, limit, endptr);

    case 'b': case 'y': case 'n': case 'q': case 'i': case 'u':
    case 'x': case 't': case 'd': case 's': case 'o': case 'g':
    case 'v': case 'r': case '*': case '?': case 'h':
      break;

    default:
      return FALSE;
    }

  if (endptr != NULL)
    *endptr = string;

  return TRUE;
}

/**
 * e_variant_type_string_is_valid:
 * @type_string: a pointer to any string
 * @returns: %TRUE if @type_string is exactly one valid type string
 *
 * Checks if @type_string is a valid EVariant type string.  This call is
 * equivalent to calling e_variant_type_string_scan() and confirming
 * that the following character is a nul terminator.
 *
 * Since 2.24
 **/
gboolean
e_variant_type_string_is_valid (const gchar *type_string)
{
  const gchar *endptr;

  g_return_val_if_fail (type_string != NULL, FALSE);

  if (!e_variant_type_string_scan (type_string, NULL, &endptr))
    return FALSE;

  return *endptr == '\0';
}

/**
 * e_variant_type_free:
 * @type: a #EVariantType, or %NULL
 *
 * Frees a #EVariantType that was allocated with
 * e_variant_type_copy(), e_variant_type_new() or one of the container
 * type constructor functions.
 *
 * In the case that @type is %NULL, this function does nothing.
 *
 * Since 2.24
 **/
void
e_variant_type_free (EVariantType *type)
{
  g_return_if_fail (type == NULL || e_variant_type_check (type));

  g_free (type);
}

/**
 * e_variant_type_copy:
 * @type: a #EVariantType
 * @returns: a new #EVariantType
 *
 * Makes a copy of a #EVariantType.  It is appropriate to call
 * e_variant_type_free() on the return value.  @type may not be %NULL.
 *
 * Since 2.24
 **/
EVariantType *
e_variant_type_copy (const EVariantType *type)
{
  gsize length;
  gchar *new;

  g_return_val_if_fail (e_variant_type_check (type), NULL);

  length = e_variant_type_get_string_length (type);
  new = g_malloc (length + 1);

  memcpy (new, type, length);
  new[length] = '\0';

  return (EVariantType *) new;
}

/**
 * e_variant_type_new:
 * @type_string: a valid EVariant type string
 * @returns: a new #EVariantType
 *
 * Creates a new #EVariantType corresponding to the type string given
 * by @type_string.  It is appropriate to call e_variant_type_free() on
 * the return value.
 *
 * It is a programmer error to call this function with an invalid type
 * string.  Use e_variant_type_string_is_valid() if you are unsure.
 *
 * Since: 2.24
 */
EVariantType *
e_variant_type_new (const gchar *type_string)
{
  g_return_val_if_fail (type_string != NULL, NULL);

  return e_variant_type_copy (E_VARIANT_TYPE (type_string));
}

/**
 * e_variant_type_get_string_length:
 * @type: a #EVariantType
 * @returns: the length of the corresponding type string
 *
 * Returns the length of the type string corresponding to the given
 * @type.  This function must be used to determine the valid extent of
 * the memory region returned by e_variant_type_peek_string().
 *
 * Since 2.24
 **/
gsize
e_variant_type_get_string_length (const EVariantType *type)
{
  const gchar *type_string = (const gchar *) type;
  gint brackets = 0;
  gsize index = 0;

  g_return_val_if_fail (e_variant_type_check (type), 0);

  do
    {
      while (type_string[index] == 'a' || type_string[index] == 'm')
        index++;

      if (type_string[index] == '(' || type_string[index] == '{')
        brackets++;

      else if (type_string[index] == ')' || type_string[index] == '}')
        brackets--;

      index++;
    }
  while (brackets);

  return index;
}

/**
 * e_variant_type_peek_string:
 * @type: a #EVariantType
 * @returns: the corresponding type string (not nul-terminated)
 *
 * Returns the type string corresponding to the given @type.  The
 * result is not nul-terminated; in order to determine its length you
 * must call e_variant_type_get_string_length().
 *
 * To get a nul-terminated string, see e_variant_type_dup_string().
 *
 * Since 2.24
 **/
const gchar *
e_variant_type_peek_string (const EVariantType *type)
{
  g_return_val_if_fail (e_variant_type_check (type), NULL);

  return (const gchar *) type;
}

/**
 * e_variant_type_dup_string:
 * @type: a #EVariantType
 * @returns: the corresponding type string
 *
 * Returns a newly-allocated copy of the type string corresponding to
 * @type.  The returned string is nul-terminated.  It is appropriate to
 * call g_free() on the return value.
 *
 * Since 2.24
 **/
gchar *
e_variant_type_dup_string (const EVariantType *type)
{
  g_return_val_if_fail (e_variant_type_check (type), NULL);

  return g_strndup (e_variant_type_peek_string (type),
                    e_variant_type_get_string_length (type));
}

/**
 * e_variant_type_is_definite:
 * @type: a #EVariantType
 * @returns: %TRUE if @type is definite
 *
 * Determines if the given @type is definite (ie: not indefinite).
 *
 * A type is definite if its type string does not contain any indefinite
 * type characters ('*', '?', or 'r').
 *
 * A #EVariant instance may not have an indefinite type, so calling
 * this function on the result of e_variant_get_type() will always
 * result in %TRUE being returned.  Calling this function on an
 * indefinite type like %E_VARIANT_TYPE_ARRAY, however, will result in
 * %FALSE being returned.
 *
 * Since 2.24
 **/
gboolean
e_variant_type_is_definite (const EVariantType *type)
{
  const gchar *type_string;
  gsize type_length;
  gsize i;

  g_return_val_if_fail (e_variant_type_check (type), FALSE);

  type_length = e_variant_type_get_string_length (type);
  type_string = e_variant_type_peek_string (type);

  for (i = 0; i < type_length; i++)
    if (type_string[i] == '*' ||
        type_string[i] == '?' ||
        type_string[i] == 'r')
      return FALSE;

  return TRUE;
}

/**
 * e_variant_type_is_container:
 * @type: a #EVariantType
 * @returns: %TRUE if @type is a container type
 *
 * Determines if the given @type is a container type.
 *
 * Container types are any array, maybe, tuple, or dictionary
 * entry types plus the variant type.
 *
 * This function returns %TRUE for any indefinite type for which every
 * definite subtype is a container -- %E_VARIANT_TYPE_ARRAY, for
 * example.
 *
 * Since 2.24
 **/
gboolean
e_variant_type_is_container (const EVariantType *type)
{
  gchar first_char;

  g_return_val_if_fail (e_variant_type_check (type), FALSE);

  first_char = e_variant_type_peek_string (type)[0];
  switch (first_char)
  {
    case 'a':
    case 'm':
    case 'r':
    case '(':
    case '{':
    case 'v':
      return TRUE;

    default:
      return FALSE;
  }
}

/**
 * e_variant_type_is_basic:
 * @type: a #EVariantType
 * @returns: %TRUE if @type is a basic type
 *
 * Determines if the given @type is a basic type.
 *
 * Basic types are booleans, bytes, integers, doubles, strings, object
 * paths and signatures.
 *
 * Only a basic type may be used as the key of a dictionary entry.
 *
 * This function returns %FALSE for all indefinite types except
 * %E_VARIANT_TYPE_BASIC.
 *
 * Since 2.24
 **/
gboolean
e_variant_type_is_basic (const EVariantType *type)
{
  gchar first_char;

  g_return_val_if_fail (e_variant_type_check (type), FALSE);

  first_char = e_variant_type_peek_string (type)[0];
  switch (first_char)
  {
    case 'b':
    case 'y':
    case 'n':
    case 'q':
    case 'i':
    case 'h':
    case 'u':
    case 't':
    case 'x':
    case 'd':
    case 's':
    case 'o':
    case 'g':
    case '?':
      return TRUE;

    default:
      return FALSE;
  }
}

/**
 * e_variant_type_is_maybe:
 * @type: a #EVariantType
 * @returns: %TRUE if @type is a maybe type
 *
 * Determines if the given @type is a maybe type.  This is true if the
 * type string for @type starts with an 'm'.
 *
 * This function returns %TRUE for any indefinite type for which every
 * definite subtype is a maybe type -- %E_VARIANT_TYPE_MAYBE, for
 * example.
 *
 * Since 2.24
 **/
gboolean
e_variant_type_is_maybe (const EVariantType *type)
{
  g_return_val_if_fail (e_variant_type_check (type), FALSE);

  return e_variant_type_peek_string (type)[0] == 'm';
}

/**
 * e_variant_type_is_array:
 * @type: a #EVariantType
 * @returns: %TRUE if @type is an array type
 *
 * Determines if the given @type is an array type.  This is true if the
 * type string for @type starts with an 'a'.
 *
 * This function returns %TRUE for any indefinite type for which every
 * definite subtype is an array type -- %E_VARIANT_TYPE_ARRAY, for
 * example.
 *
 * Since 2.24
 **/
gboolean
e_variant_type_is_array (const EVariantType *type)
{
  g_return_val_if_fail (e_variant_type_check (type), FALSE);

  return e_variant_type_peek_string (type)[0] == 'a';
}

/**
 * e_variant_type_is_tuple:
 * @type: a #EVariantType
 * @returns: %TRUE if @type is a tuple type
 *
 * Determines if the given @type is a tuple type.  This is true if the
 * type string for @type starts with a '(' or if @type is
 * %E_VARIANT_TYPE_TUPLE.
 *
 * This function returns %TRUE for any indefinite type for which every
 * definite subtype is a tuple type -- %E_VARIANT_TYPE_TUPLE, for
 * example.
 *
 * Since 2.24
 **/
gboolean
e_variant_type_is_tuple (const EVariantType *type)
{
  gchar type_char;

  g_return_val_if_fail (e_variant_type_check (type), FALSE);

  type_char = e_variant_type_peek_string (type)[0];
  return type_char == 'r' || type_char == '(';
}

/**
 * e_variant_type_is_dict_entry:
 * @type: a #EVariantType
 * @returns: %TRUE if @type is a dictionary entry type
 *
 * Determines if the given @type is a dictionary entry type.  This is
 * true if the type string for @type starts with a '{'.
 *
 * This function returns %TRUE for any indefinite type for which every
 * definite subtype is a dictionary entry type --
 * %E_VARIANT_TYPE_DICT_ENTRY, for example.
 *
 * Since 2.24
 **/
gboolean
e_variant_type_is_dict_entry (const EVariantType *type)
{
  g_return_val_if_fail (e_variant_type_check (type), FALSE);

  return e_variant_type_peek_string (type)[0] == '{';
}

/**
 * e_variant_type_hash:
 * @type: a #EVariantType
 * @returns: the hash value
 *
 * Hashes @type.
 *
 * The argument type of @type is only #gconstpointer to allow use with
 * #GHashTable without function pointer casting.  A valid
 * #EVariantType must be provided.
 *
 * Since 2.24
 **/
guint
e_variant_type_hash (gconstpointer type)
{
  const gchar *type_string;
  guint value = 0;
  gsize length;
  gsize i;

  g_return_val_if_fail (e_variant_type_check (type), 0);

  type_string = e_variant_type_peek_string (type);
  length = e_variant_type_get_string_length (type);

  for (i = 0; i < length; i++)
    value = (value << 5) - value + type_string[i];

  return value;
}

/**
 * e_variant_type_equal:
 * @type1: a #EVariantType
 * @type2: a #EVariantType
 * @returns: %TRUE if @type1 and @type2 are exactly equal
 *
 * Compares @type1 and @type2 for equality.
 *
 * Only returns %TRUE if the types are exactly equal.  Even if one type
 * is an indefinite type and the other is a subtype of it, %FALSE will
 * be returned if they are not exactly equal.  If you want to check for
 * subtypes, use e_variant_type_is_subtype_of().
 *
 * The argument types of @type1 and @type2 are only #gconstpointer to
 * allow use with #GHashTable without function pointer casting.  For
 * both arguments, a valid #EVariantType must be provided.
 *
 * Since 2.24
 **/
gboolean
e_variant_type_equal (gconstpointer type1,
                      gconstpointer type2)
{
  const gchar *string1, *string2;
  gsize size1, size2;

  g_return_val_if_fail (e_variant_type_check (type1), FALSE);
  g_return_val_if_fail (e_variant_type_check (type2), FALSE);

  if (type1 == type2)
    return TRUE;

  size1 = e_variant_type_get_string_length (type1);
  size2 = e_variant_type_get_string_length (type2);

  if (size1 != size2)
    return FALSE;

  string1 = e_variant_type_peek_string (type1);
  string2 = e_variant_type_peek_string (type2);

  return memcmp (string1, string2, size1) == 0;
}

/**
 * e_variant_type_is_subtype_of:
 * @type: a #EVariantType
 * @supertype: a #EVariantType
 * @returns: %TRUE if @type is a subtype of @supertype
 *
 * Checks if @type is a subtype of @supertype.
 *
 * This function returns %TRUE if @type is a subtype of @supertype.  All
 * types are considered to be subtypes of themselves.  Aside from that,
 * only indefinite types can have subtypes.
 *
 * Since 2.24
 **/
gboolean
e_variant_type_is_subtype_of (const EVariantType *type,
                              const EVariantType *supertype)
{
  const gchar *supertype_string;
  const gchar *supertype_end;
  const gchar *type_string;

  g_return_val_if_fail (e_variant_type_check (type), FALSE);
  g_return_val_if_fail (e_variant_type_check (supertype), FALSE);

  supertype_string = e_variant_type_peek_string (supertype);
  type_string = e_variant_type_peek_string (type);

  supertype_end = supertype_string +
                  e_variant_type_get_string_length (supertype);

  /* we know that type and supertype are both well-formed, so it's
   * safe to treat this merely as a text processing problem.
   */
  while (supertype_string < supertype_end)
    {
      char supertype_char = *supertype_string++;

      if (supertype_char == *type_string)
        type_string++;

      else if (*type_string == ')')
        return FALSE;

      else
        {
          const EVariantType *target_type = (EVariantType *) type_string;

          switch (supertype_char)
            {
            case 'r':
              if (!e_variant_type_is_tuple (target_type))
                return FALSE;
              break;

            case '*':
              break;

            case '?':
              if (!e_variant_type_is_basic (target_type))
                return FALSE;
              break;

            default:
              return FALSE;
            }

          type_string += e_variant_type_get_string_length (target_type);
        }
    }

  return TRUE;
}

/**
 * e_variant_type_element:
 * @type: an array or maybe #EVariantType
 * @returns: the element type of @type
 *
 * Determines the element type of an array or maybe type.
 *
 * This function may only be used with array or maybe types.
 *
 * Since 2.24
 **/
const EVariantType *
e_variant_type_element (const EVariantType *type)
{
  const gchar *type_string;

  g_return_val_if_fail (e_variant_type_check (type), NULL);

  type_string = e_variant_type_peek_string (type);

  g_assert (type_string[0] == 'a' || type_string[0] == 'm');

  return (const EVariantType *) &type_string[1];
}

/**
 * e_variant_type_first:
 * @type: a tuple or dictionary entry #EVariantType
 * @returns: the first item type of @type, or %NULL
 *
 * Determines the first item type of a tuple or dictionary entry
 * type.
 *
 * This function may only be used with tuple or dictionary entry types,
 * but must not be used with the generic tuple type
 * %E_VARIANT_TYPE_TUPLE.
 *
 * In the case of a dictionary entry type, this returns the type of
 * the key.
 *
 * %NULL is returned in case of @type being %E_VARIANT_TYPE_UNIT.
 *
 * This call, together with e_variant_type_next() provides an iterator
 * interface over tuple and dictionary entry types.
 *
 * Since 2.24
 **/
const EVariantType *
e_variant_type_first (const EVariantType *type)
{
  const gchar *type_string;

  g_return_val_if_fail (e_variant_type_check (type), NULL);

  type_string = e_variant_type_peek_string (type);
  g_assert (type_string[0] == '(' || type_string[0] == '{');

  if (type_string[1] == ')')
    return NULL;

  return (const EVariantType *) &type_string[1];
}

/**
 * e_variant_type_next:
 * @type: a #EVariantType from a previous call
 * @returns: the next #EVariantType after @type, or %NULL
 *
 * Determines the next item type of a tuple or dictionary entry
 * type.
 *
 * @type must be the result of a previous call to
 * e_variant_type_first() or e_variant_type_next().
 *
 * If called on the key type of a dictionary entry then this call
 * returns the value type.  If called on the value type of a dictionary
 * entry then this call returns %NULL.
 *
 * For tuples, %NULL is returned when @type is the last item in a tuple.
 *
 * Since 2.24
 **/
const EVariantType *
e_variant_type_next (const EVariantType *type)
{
  const gchar *type_string;

  g_return_val_if_fail (e_variant_type_check (type), NULL);

  type_string = e_variant_type_peek_string (type);
  type_string += e_variant_type_get_string_length (type);

  if (*type_string == ')' || *type_string == '}')
    return NULL;

  return (const EVariantType *) type_string;
}

/**
 * e_variant_type_n_items:
 * @type: a tuple or dictionary entry #EVariantType
 * @returns: the number of items in @type
 *
 * Determines the number of items contained in a tuple or
 * dictionary entry type.
 *
 * This function may only be used with tuple or dictionary entry types,
 * but must not be used with the generic tuple type
 * %E_VARIANT_TYPE_TUPLE.
 *
 * In the case of a dictionary entry type, this function will always
 * return 2.
 *
 * Since 2.24
 **/
gsize
e_variant_type_n_items (const EVariantType *type)
{
  gsize count = 0;

  g_return_val_if_fail (e_variant_type_check (type), 0);

  for (type = e_variant_type_first (type);
       type;
       type = e_variant_type_next (type))
    count++;

  return count;
}

/**
 * e_variant_type_key:
 * @type: a dictionary entry #EVariantType
 * @returns: the key type of the dictionary entry
 *
 * Determines the key type of a dictionary entry type.
 *
 * This function may only be used with a dictionary entry type.  Other
 * than the additional restriction, this call is equivalent to
 * e_variant_type_first().
 *
 * Since 2.24
 **/
const EVariantType *
e_variant_type_key (const EVariantType *type)
{
  const gchar *type_string;

  g_return_val_if_fail (e_variant_type_check (type), NULL);

  type_string = e_variant_type_peek_string (type);
  g_assert (type_string[0] == '{');

  return (const EVariantType *) &type_string[1];
}

/**
 * e_variant_type_value:
 * @type: a dictionary entry #EVariantType
 * @returns: the value type of the dictionary entry
 *
 * Determines the value type of a dictionary entry type.
 *
 * This function may only be used with a dictionary entry type.
 *
 * Since 2.24
 **/
const EVariantType *
e_variant_type_value (const EVariantType *type)
{
  const gchar *type_string;

  g_return_val_if_fail (e_variant_type_check (type), NULL);

  type_string = e_variant_type_peek_string (type);
  g_assert (type_string[0] == '{');

  return e_variant_type_next (e_variant_type_key (type));
}

/**
 * e_variant_type_new_tuple:
 * @items: an array of #EVariantTypes, one for each item
 * @length: the length of @items, or -1
 * @returns: a new tuple #EVariantType
 *
 * Constructs a new tuple type, from @items.
 *
 * @length is the number of items in @items, or -1 to indicate that
 * @items is %NULL-terminated.
 *
 * It is appropriate to call e_variant_type_free() on the return value.
 *
 * Since 2.24
 **/
static EVariantType *
e_variant_type_new_tuple_slow (const EVariantType * const *items,
                               gint                        length)
{
  /* the "slow" version is needed in case the static buffer of 1024
   * bytes is exceeded when running the normal version.  this will
   * happen only in truly insane code, so it can be slow.
   */
  GString *string;
  gsize i;

  string = g_string_new ("(");
  for (i = 0; i < length; i++)
    {
      const EVariantType *type;
      gsize size;

      g_return_val_if_fail (e_variant_type_check (items[i]), NULL);

      type = items[i];
      size = e_variant_type_get_string_length (type);
      g_string_append_len (string, (const gchar *) type, size);
    }
  g_string_append_c (string, ')');

  return (EVariantType *) g_string_free (string, FALSE);
}

EVariantType *
e_variant_type_new_tuple (const EVariantType * const *items,
                          gint                        length)
{
  char buffer[1024];
  gsize offset;
  gsize i;

  g_return_val_if_fail (length == 0 || items != NULL, NULL);

  if (length < 0)
    for (length = 0; items[length] != NULL; length++);

  offset = 0;
  buffer[offset++] = '(';

  for (i = 0; i < length; i++)
    {
      const EVariantType *type;
      gsize size;

      g_return_val_if_fail (e_variant_type_check (items[i]), NULL);

      type = items[i];
      size = e_variant_type_get_string_length (type);

      if (offset + size >= sizeof buffer) /* leave room for ')' */
        return e_variant_type_new_tuple_slow (items, length);

      memcpy (&buffer[offset], type, size);
      offset += size;
    }

  g_assert (offset < sizeof buffer);
  buffer[offset++] = ')';

  return (EVariantType *) g_memdup (buffer, offset);
}

/**
 * e_variant_type_new_array:
 * @element: a #EVariantType
 * @returns: a new array #EVariantType
 *
 * Constructs the type corresponding to an array of elements of the
 * type @type.
 *
 * It is appropriate to call e_variant_type_free() on the return value.
 *
 * Since 2.24
 **/
EVariantType *
e_variant_type_new_array (const EVariantType *element)
{
  gsize size;
  gchar *new;

  g_return_val_if_fail (e_variant_type_check (element), NULL);

  size = e_variant_type_get_string_length (element);
  new = g_malloc (size + 1);

  new[0] = 'a';
  memcpy (new + 1, element, size);

  return (EVariantType *) new;
}

/**
 * e_variant_type_new_maybe:
 * @element: a #EVariantType
 * @returns: a new maybe #EVariantType
 *
 * Constructs the type corresponding to a maybe instance containing
 * type @type or Nothing.
 *
 * It is appropriate to call e_variant_type_free() on the return value.
 *
 * Since 2.24
 **/
EVariantType *
e_variant_type_new_maybe (const EVariantType *element)
{
  gsize size;
  gchar *new;

  g_return_val_if_fail (e_variant_type_check (element), NULL);

  size = e_variant_type_get_string_length (element);
  new = g_malloc (size + 1);

  new[0] = 'm';
  memcpy (new + 1, element, size);

  return (EVariantType *) new;
}

/**
 * e_variant_type_new_dict_entry:
 * @key: a basic #EVariantType
 * @value: a #EVariantType
 * @returns: a new dictionary entry #EVariantType
 *
 * Constructs the type corresponding to a dictionary entry with a key
 * of type @key and a value of type @value.
 *
 * It is appropriate to call e_variant_type_free() on the return value.
 *
 * Since 2.24
 **/
EVariantType *
e_variant_type_new_dict_entry (const EVariantType *key,
                               const EVariantType *value)
{
  gsize keysize, valsize;
  gchar *new;

  g_return_val_if_fail (e_variant_type_check (key), NULL);
  g_return_val_if_fail (e_variant_type_check (value), NULL);

  keysize = e_variant_type_get_string_length (key);
  valsize = e_variant_type_get_string_length (value);

  new = g_malloc (1 + keysize + valsize + 1);

  new[0] = '{';
  memcpy (new + 1, key, keysize);
  memcpy (new + 1 + keysize, value, valsize);
  new[1 + keysize + valsize] = '}';

  return (EVariantType *) new;
}

/* private */
const EVariantType *
e_variant_type_checked_ (const gchar *type_string)
{
  g_return_val_if_fail (e_variant_type_string_is_valid (type_string), NULL);
  return (const EVariantType *) type_string;
}
