/*
 * Copyright © 2009 Sam Thursfield
 * Copyright © 2009 Codethink Limited
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
 *
 * Authors: Sam Thursfield
 *          Ryan Lortie <desrt@desrt.ca>
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <glib.h>
#include "gvariant.h"

/**
 * e_variant_print:
 * @value: a #EVariant
 * @type_annotate: %TRUE if type information should be included in
 *                 the output
 * @returns: a newly-allocated string holding the result.
 *
 * Pretty-prints @value in the format understood by e_variant_parse().
 *
 * If @type_annotate is %TRUE, then type information is included in
 * the output.
 */
gchar *
e_variant_print (EVariant *value,
                 gboolean type_annotate)
{
  return g_string_free (e_variant_print_string (value, NULL, type_annotate),
                        FALSE);
};

/**
 * e_variant_print_string:
 * @value: a #EVariant
 * @string: a #GString, or %NULL
 * @type_annotate: %TRUE if type information should be included in
 *                 the output
 * @returns: a #GString containing the string
 *
 * Behaves as e_variant_print(), but operates on a #GString.
 *
 * If @string is non-%NULL then it is appended to and returned.  Else,
 * a new empty #GString is allocated and it is returned.
 **/
GString *
e_variant_print_string (EVariant *value,
                        GString *string,
                        gboolean type_annotate)
{
  const EVariantType *type;

  type = e_variant_get_type (value);

  if G_UNLIKELY (string == NULL)
      string = g_string_new (NULL);

  switch (e_variant_classify (value))
  {
    case E_VARIANT_CLASS_ARRAY:
      {
        EVariantIter iter;

        if (e_variant_iter_init (&iter, value))
          {
            EVariant *element;

            g_string_append_c (string, '[');

            /* only type annotate the first element (if requested) */
            while ((element = e_variant_iter_next_value (&iter)))
              {
                e_variant_print_string (element, string, type_annotate);
                g_string_append (string, ", ");
                type_annotate = FALSE;
              }
            g_string_truncate (string, string->len - 2);

            g_string_append_c (string, ']');
          }
        else
          {
            /* if there are no elements then we must type
             * annotate the array itself (if requested)
             */
            if (type_annotate)
              g_string_append_printf (string, "@%s ",
                                      e_variant_get_type_string (value));
            g_string_append (string, "[]");
          }
        break;
      }

    case E_VARIANT_CLASS_VARIANT:
      {
        EVariant *child = e_variant_get_variant (value);

        /* Always annotate types in nested variants, because they are
         * (by nature) of variable type.
         */
        g_string_append_c (string, '<');
        e_variant_print_string (child, string, TRUE);
        g_string_append_c (string, '>');

        e_variant_unref (child);
        break;
      }

    case E_VARIANT_CLASS_TUPLE:
      {
        EVariantIter iter;

        g_string_append_c (string, '(');
        if (e_variant_iter_init (&iter, value))
          {
            EVariant *element;

            while ((element = e_variant_iter_next_value (&iter)))
              {
                e_variant_print_string (element, string, type_annotate);
                g_string_append (string, ", ");
              }
            g_string_truncate (string, string->len - 2);
          }
        g_string_append_c (string, ')');

        break;
      }

    case E_VARIANT_CLASS_DICT_ENTRY:
      {
        EVariantIter iter;
        EVariant *element;

        g_string_append_c (string, '{');

        if (e_variant_iter_init (&iter, value))
          {
            while ((element = e_variant_iter_next_value (&iter)))
              {
                e_variant_print_string (element, string, type_annotate);

                g_string_append_c (string, ':');

                element = e_variant_iter_next_value (&iter);
                e_variant_print_string (element, string, type_annotate);
                g_string_append (string, ", ");
              }
            g_string_truncate (string, string->len - 2);

            g_string_append_c (string, '}');
          }

        break;
      }

    case E_VARIANT_CLASS_BOOLEAN:
      if (e_variant_get_boolean (value))
        g_string_append (string, "true");
      else
        g_string_append (string, "false");
      break;

    /*case E_VARIANT_CLASS_MAYBE:
      {
        if (e_variant_n_children (value))
          {
            EVariant *element;

            g_string_append (string, "<maybe>");
            e_variant_markup_newline (string, newlines);

            element = e_variant_get_child_value (value, 0);
            e_variant_markup_print (element, string,
                                    newlines, indentation, tabstop);
            e_variant_unref (element);

            e_variant_markup_indent (string, indentation - tabstop);
            g_string_append (string, "</maybe>");
          }
        else
          g_string_append_printf (string, "<nothing type='%s'/>",
                                  e_variant_get_type_string (value));

        break;
      }*/

    case E_VARIANT_CLASS_STRING:
      {
        const gchar *str = e_variant_get_string (value, NULL);
        gchar *escaped = g_strescape (str, NULL);

        g_string_append_printf (string, "\"%s\"", escaped);

        g_free (escaped);
        break;
      }

    case E_VARIANT_CLASS_BYTE:
      if (type_annotate)
        g_string_append (string, "byte ");
      g_string_append_printf (string, "0x%02x",
                              e_variant_get_byte (value));
      break;

    case E_VARIANT_CLASS_INT16:
      if (type_annotate)
        g_string_append (string, "int16 ");
      g_string_append_printf (string, "%"G_GINT16_FORMAT,
                              e_variant_get_int16 (value));
      break;

    case E_VARIANT_CLASS_UINT16:
      if (type_annotate)
        g_string_append (string, "uint16 ");
      g_string_append_printf (string, "%"G_GUINT16_FORMAT,
                              e_variant_get_uint16 (value));
      break;

    case E_VARIANT_CLASS_INT32:
      /* Never annotate this type because it is the default for numbers
       * (and this is a *pretty* printer)
       */
      g_string_append_printf (string, "%"G_GINT32_FORMAT,
                              e_variant_get_int32 (value));
      break;

    case E_VARIANT_CLASS_HANDLE:
      if (type_annotate)
        g_string_append (string, "handle ");
      g_string_append_printf (string, "%"G_GINT32_FORMAT,
                              e_variant_get_handle (value));
      break;

    case E_VARIANT_CLASS_UINT32:
      if (type_annotate)
        g_string_append (string, "uint32 ");
      g_string_append_printf (string, "%"G_GUINT32_FORMAT,
                              e_variant_get_uint32 (value));
      break;

    case E_VARIANT_CLASS_INT64:
      if (type_annotate)
        g_string_append (string, "int64 ");
      g_string_append_printf (string, "%"G_GINT64_FORMAT,
                              e_variant_get_int64 (value));
      break;

    case E_VARIANT_CLASS_UINT64:
      if (type_annotate)
        g_string_append (string, "uint64 ");
      g_string_append_printf (string, "%"G_GUINT64_FORMAT,
                              e_variant_get_uint64 (value));
      break;

    case E_VARIANT_CLASS_DOUBLE:
      {
        gchar buffer[100];
        gint i;

        g_ascii_dtostr (buffer, sizeof buffer, e_variant_get_double (value));

        for (i = 0; buffer[i]; i++)
          if (buffer[i] == '.' || buffer[i] == 'e' ||
              buffer[i] == 'n' || buffer[i] == 'N')
            break;

        /* if there is no '.' or 'e' in the float then add one */
        if (buffer[i] == '\0')
          {
            buffer[i++] = '.';
            buffer[i++] = '0';
            buffer[i++] = '\0';
          }

        g_string_append (string, buffer);
        break;
      }

    case E_VARIANT_CLASS_OBJECT_PATH:
      if (type_annotate)
        g_string_append (string, "objectpath ");
      g_string_append_printf (string, "\"%s\"",
                              e_variant_get_string (value, NULL));
      break;

    case E_VARIANT_CLASS_SIGNATURE:
      if (type_annotate)
        g_string_append (string, "signature");
      g_string_append_printf (string, "\"%s\"",
                              e_variant_get_string (value, NULL));
      break;

    default:
      g_error ("e_variant_print: sorry... not handled yet: %s",
               e_variant_get_type_string (value));
  }

  return string;
};
