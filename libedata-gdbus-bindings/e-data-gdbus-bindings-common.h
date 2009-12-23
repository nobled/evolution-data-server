/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Travis Reitter (travis.reitter@collabora.co.uk)
 */

#ifndef _E_DATA_GDBUS_BINDINGS_COMMON_H
#define _E_DATA_GDBUS_BINDINGS_COMMON_H

#include <glib.h>

typedef struct {
        GCallback cb;
        gpointer user_data;
} Closure;

static void
closure_free (Closure *closure)
{
        g_slice_free (Closure, closure);
}

static gboolean
demarshal_retvals__VOID (GVariant *retvals)
{
        gboolean success = TRUE;

        if (retvals)
                g_variant_unref (retvals);
        else
                success = FALSE;

        return success;
}

static gboolean
demarshal_retvals__OBJPATH (GVariant *retvals, char **OUT_objpath1)
{
        gboolean success = TRUE;

        if (retvals) {
                const char *objpath1 = NULL;

                g_variant_get (retvals, "(o)", &objpath1);
                if (objpath1) {
                        *OUT_objpath1 = g_strdup (objpath1);
                } else {
                        success = FALSE;
                }

                g_variant_unref (retvals);
        } else {
                success = FALSE;
        }

        return success;
}

static gboolean
demarshal_retvals__STRING (GVariant *retvals, char **OUT_string1)
{
        gboolean success = TRUE;

        if (retvals) {
                const char *string1 = NULL;

                g_variant_get (retvals, "(s)", &string1);
                if (string1) {
                        *OUT_string1 = g_strdup (string1);
                } else {
                        success = FALSE;
                }

                g_variant_unref (retvals);
        } else {
                success = FALSE;
        }

        return success;
}

static gboolean
demarshal_retvals__STRINGVECTOR (GVariant *retvals, char ***OUT_strv1)
{
        gboolean success = TRUE;

        if (retvals) {
                GVariant *strv1_variant;
                char **strv1 = NULL;
                gint strv1_length;

                /* retvals contains a (as) with length 1; de-shell the
                 * array of strings from the tuple */
                strv1_variant = g_variant_get_child_value (retvals, 0);
                strv1 = g_variant_dup_strv (strv1_variant, &strv1_length);

                if (strv1) {
                        *OUT_strv1 = strv1;
                } else {
                        success = FALSE;
                }

                g_variant_unref (retvals);
        } else {
                success = FALSE;
        }

        return success;
}

static gboolean
demarshal_retvals__STRINGVECTOR_STRING (GVariant   *retvals,
					char     ***OUT_strv1,
					char      **OUT_string1)
{
        gboolean success = TRUE;

        if (retvals) {
                GVariant *strv1_variant;
                char **strv1 = NULL;
                GVariant *string1_variant;
                char *string1 = NULL;

                strv1_variant = g_variant_get_child_value (retvals, 0);
                strv1 = g_variant_dup_strv (strv1_variant, NULL);

                if (strv1) {
                        *OUT_strv1 = strv1;
                } else {
                        success = FALSE;
                }

                string1_variant = g_variant_get_child_value (retvals, 1);
                string1 = g_variant_dup_string (string1_variant, NULL);

                if (string1) {
                        *OUT_string1 = string1;
                } else {
                        success = FALSE;
                }

                g_variant_unref (retvals);
        } else {
                success = FALSE;
        }

        return success;
}

static gboolean
demarshal_retvals__GPTRARRAY_with_GVALUEARRAY_with_UINT_STRING_endwith_endwith
	(GVariant   *retvals,
	 GPtrArray **OUT_ptrarray1)
{
        gboolean success = TRUE;

        if (retvals) {
                GVariant *array1_variant;
		GVariantIter iter1;
                GPtrArray *ptrarray1 = NULL;
		const guint uint1_tmp;
		const char *string1_tmp = NULL;

		/* deshelling a (a(us)); there should always be exactly one
		 * a(us) in the outermost tuple */
                array1_variant = g_variant_get_child_value (retvals, 0);
		g_variant_iter_init (&iter1, array1_variant);

		/* return NULL instead of an empty GPtrArray* if there's nothing
		 * to put in it */
		if (g_variant_n_children (array1_variant) < 1) {
			goto empty_inner_array;
		}

		ptrarray1 = g_ptr_array_new ();

		while (g_variant_iter_next (&iter1, "(us)", &uint1_tmp, &string1_tmp)) {
			GValueArray *valuearray = NULL;
			GValue uint_value1 = {0};
			GValue string_value1 = {0};

			valuearray = g_value_array_new (2);
			g_value_init (&uint_value1, G_TYPE_UINT);
			g_value_init (&string_value1, G_TYPE_STRING);

			g_value_set_uint (&uint_value1, uint1_tmp);
			g_value_set_string (&string_value1, string1_tmp);

			g_value_array_append (valuearray, &uint_value1);
			g_value_array_append (valuearray, &string_value1);
			g_ptr_array_add (ptrarray1, valuearray);
		}

empty_inner_array:
		*OUT_ptrarray1 = ptrarray1;

                g_variant_unref (retvals);
        } else {
                success = FALSE;
        }

        return success;
}


typedef void (*reply__VOID) (GDBusProxy *proxy,
                             GError     *error,
                             gpointer    user_data);

typedef void (*reply__OBJPATH) (GDBusProxy *proxy,
				char       *OUT_path1,
				GError     *error,
				gpointer    user_data);

typedef void (*reply__STRING) (GDBusProxy *proxy,
                               char       *OUT_string1,
                               GError     *error,
                               gpointer    user_data);

typedef void (*reply__STRINGVECTOR) (GDBusProxy  *proxy,
                                     char       **OUT_strv1,
                                     GError      *error,
                                     gpointer     user_data);

typedef void (*reply__GPTRARRAY_with_GVALUEARRAY_with_UINT_STRING_endwith_endwith) (GDBusProxy *proxy,
    GPtrArray  *OUT_ptrarray1,
    GError     *error,
    gpointer    user_data);

#endif /* _E_DATA_GDBUS_BINDINGS_COMMON_H */
