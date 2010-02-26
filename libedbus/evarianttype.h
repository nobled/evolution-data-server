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

#ifndef __E_VARIANT_TYPE_H__
#define __E_VARIANT_TYPE_H__

#include <glib/gmessages.h>
#include <glib/gtypes.h>

G_BEGIN_DECLS

/**
 * EVariantType:
 *
 * A type in the EVariant type system.
 *
 * Two types may not be compared by value; use e_variant_type_equal() or
 * e_variant_type_is_subtype().  May be copied using
 * e_variant_type_copy() and freed using e_variant_type_free().
 **/
typedef struct _EVariantType EVariantType;

/**
 * E_VARIANT_TYPE_BOOLEAN:
 *
 * The type of a value that can be either %TRUE or %FALSE.
 **/
#define E_VARIANT_TYPE_BOOLEAN              ((const EVariantType *) "b")

/**
 * E_VARIANT_TYPE_BYTE:
 *
 * The type of an integer value that can range from 0 to 255.
 **/
#define E_VARIANT_TYPE_BYTE                 ((const EVariantType *) "y")

/**
 * E_VARIANT_TYPE_INT16:
 *
 * The type of an integer value that can range from -32768 to 32767.
 **/
#define E_VARIANT_TYPE_INT16                ((const EVariantType *) "n")

/**
 * E_VARIANT_TYPE_UINT16:
 *
 * The type of an integer value that can range from 0 to 65535.
 * There were about this many people living in Toronto in the 1870s.
 **/
#define E_VARIANT_TYPE_UINT16               ((const EVariantType *) "q")

/**
 * E_VARIANT_TYPE_INT32:
 *
 * The type of an integer value that can range from -2147483648 to
 * 2147483647.
 **/
#define E_VARIANT_TYPE_INT32                ((const EVariantType *) "i")

/**
 * E_VARIANT_TYPE_UINT32:
 *
 * The type of an integer value that can range from 0 to 4294967295.
 * That's one number for everyone who was around in the late 1970s.
 **/
#define E_VARIANT_TYPE_UINT32               ((const EVariantType *) "u")

/**
 * E_VARIANT_TYPE_INT64:
 *
 * The type of an integer value that can range from
 * -9223372036854775808 to 9223372036854775807.
 **/
#define E_VARIANT_TYPE_INT64                ((const EVariantType *) "x")

/**
 * E_VARIANT_TYPE_UINT64:
 *
 * The type of an integer value that can range from 0 to
 * 18446744073709551616.  That's a really big number, but a Rubik's
 * cube can have a bit more than twice as many possible positions.
 **/
#define E_VARIANT_TYPE_UINT64               ((const EVariantType *) "t")

/**
 * E_VARIANT_TYPE_DOUBLE:
 *
 * The type of a double precision IEEE754 floating point number.
 * These guys go up to about 1.80e308 (plus and minus) but miss out on
 * some numbers in between.  In any case, that's far greater than the
 * estimated number of fundamental particles in the observable
 * universe.
 **/
#define E_VARIANT_TYPE_DOUBLE               ((const EVariantType *) "d")

/**
 * E_VARIANT_TYPE_STRING:
 *
 * The type of a string.  "" is a string.  %NULL is not a string.
 **/
#define E_VARIANT_TYPE_STRING               ((const EVariantType *) "s")

/**
 * E_VARIANT_TYPE_OBJECT_PATH:
 *
 * The type of a DBus object reference.  These are strings of a
 * specific format used to identify objects at a given destination on
 * the bus.
 *
 * If you are not interacting with DBus, then there is no reason to make
 * use of this type.  If you are, then the DBus specification contains a
 * precise description of valid object paths.
 **/
#define E_VARIANT_TYPE_OBJECT_PATH          ((const EVariantType *) "o")

/**
 * E_VARIANT_TYPE_SIGNATURE:
 *
 * The type of a DBus type signature.  These are strings of a specific
 * format used as type signatures for DBus methods and messages.
 *
 * If you are not interacting with DBus, then there is no reason to make
 * use of this type.  If you are, then the DBus specification contains a
 * precise description of valid signature strings.
 **/
#define E_VARIANT_TYPE_SIGNATURE            ((const EVariantType *) "g")

/**
 * E_VARIANT_TYPE_VARIANT:
 *
 * The type of a box that contains any other value (including another
 * variant).
 **/
#define E_VARIANT_TYPE_VARIANT              ((const EVariantType *) "v")

/**
 * E_VARIANT_TYPE_HANDLE:
 *
 * The type of a 32bit signed integer value, that by convention, is used
 * as an index into an array of file descriptors that are sent alongside
 * a DBus message.
 *
 * If you are not interacting with DBus, then there is no reason to make
 * use of this type.
 **/
#define E_VARIANT_TYPE_HANDLE               ((const EVariantType *) "h")

/**
 * E_VARIANT_TYPE_UNIT:
 *
 * The empty tuple type.  Has only one instance.  Known also as "triv"
 * or "void".
 **/
#define E_VARIANT_TYPE_UNIT                 ((const EVariantType *) "()")

/**
 * E_VARIANT_TYPE_ANY:
 *
 * An indefinite type that is a supertype of every type (including
 * itself).
 **/
#define E_VARIANT_TYPE_ANY                  ((const EVariantType *) "*")

/**
 * E_VARIANT_TYPE_BASIC:
 *
 * An indefinite type that is a supertype of every basic (ie:
 * non-container) type.
 **/
#define E_VARIANT_TYPE_BASIC                ((const EVariantType *) "?")

/**
 * E_VARIANT_TYPE_MAYBE:
 *
 * An indefinite type that is a supertype of every maybe type.
 **/
#define E_VARIANT_TYPE_MAYBE                ((const EVariantType *) "m*")

/**
 * E_VARIANT_TYPE_ARRAY:
 *
 * An indefinite type that is a supertype of every array type.
 **/
#define E_VARIANT_TYPE_ARRAY                ((const EVariantType *) "a*")

/**
 * E_VARIANT_TYPE_TUPLE:
 *
 * An indefinite type that is a supertype of every tuple type,
 * regardless of the number of items in the tuple.
 **/
#define E_VARIANT_TYPE_TUPLE                ((const EVariantType *) "r")

/**
 * E_VARIANT_TYPE_DICT_ENTRY:
 *
 * An indefinite type that is a supertype of every dictionary entry
 * type.
 **/
#define E_VARIANT_TYPE_DICT_ENTRY           ((const EVariantType *) "{?*}")

/**
 * E_VARIANT_TYPE_DICTIONARY:
 *
 * An indefinite type that is a supertype of every dictionary type --
 * that is, any array type that has an element type equal to any
 * dictionary entry type.
 **/
#define E_VARIANT_TYPE_DICTIONARY           ((const EVariantType *) "a{?*}")

/**
 * E_VARIANT_TYPE:
 * @type_string: a well-formed #EVariantType type string
 *
 * Converts a string to a const #EVariantType.  Depending on the
 * current debugging level, this function may perform a runtime check
 * to ensure that @string is a valid EVariant type string.
 *
 * It is always a programmer error to use this macro with an invalid
 * type string.
 *
 * Since 2.24
 **/
#ifndef G_DISABLE_CHECKS
# define E_VARIANT_TYPE(type_string)            (e_variant_type_checked_ ((type_string)))
#else
# define E_VARIANT_TYPE(type_string)            ((const EVariantType *) (type_string))
#endif

/* type string checking */
gboolean                        e_variant_type_string_is_valid          (const gchar         *type_string);
gboolean                        e_variant_type_string_scan              (const gchar         *string,
                                                                         const gchar         *limit,
                                                                         const gchar        **endptr);

/* create/destroy */
void                            e_variant_type_free                     (EVariantType        *type);
EVariantType *                  e_variant_type_copy                     (const EVariantType  *type);
EVariantType *                  e_variant_type_new                      (const gchar         *type_string);

/* getters */
gsize                           e_variant_type_get_string_length        (const EVariantType  *type);
const gchar *                   e_variant_type_peek_string              (const EVariantType  *type);
gchar *                         e_variant_type_dup_string               (const EVariantType  *type);

/* classification */
gboolean                        e_variant_type_is_definite              (const EVariantType  *type);
gboolean                        e_variant_type_is_container             (const EVariantType  *type);
gboolean                        e_variant_type_is_basic                 (const EVariantType  *type);
gboolean                        e_variant_type_is_maybe                 (const EVariantType  *type);
gboolean                        e_variant_type_is_array                 (const EVariantType  *type);
gboolean                        e_variant_type_is_tuple                 (const EVariantType  *type);
gboolean                        e_variant_type_is_dict_entry            (const EVariantType  *type);

/* for hash tables */
guint                           e_variant_type_hash                     (gconstpointer        type);
gboolean                        e_variant_type_equal                    (gconstpointer        type1,
                                                                         gconstpointer        type2);

/* subtypes */
gboolean                        e_variant_type_is_subtype_of            (const EVariantType  *type,
                                                                         const EVariantType  *supertype);

/* type iterator interface */
const EVariantType *            e_variant_type_element                  (const EVariantType  *type);
const EVariantType *            e_variant_type_first                    (const EVariantType  *type);
const EVariantType *            e_variant_type_next                     (const EVariantType  *type);
gsize                           e_variant_type_n_items                  (const EVariantType  *type);
const EVariantType *            e_variant_type_key                      (const EVariantType  *type);
const EVariantType *            e_variant_type_value                    (const EVariantType  *type);

/* constructors */
EVariantType *                  e_variant_type_new_array                (const EVariantType  *element);
EVariantType *                  e_variant_type_new_maybe                (const EVariantType  *element);
EVariantType *                  e_variant_type_new_tuple                (const EVariantType * const *items,
                                                                         gint                 length);
EVariantType *                  e_variant_type_new_dict_entry           (const EVariantType  *key,
                                                                         const EVariantType  *value);

/*< private >*/
const EVariantType *            e_variant_type_checked_                 (const gchar *);

G_END_DECLS

#endif /* __E_VARIANT_TYPE_H__ */
