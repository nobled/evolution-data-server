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
demarshal_retvals__VOID (EVariant *retvals)
{
        gboolean success = TRUE;

        if (retvals)
                e_variant_unref (retvals);
        else
                success = FALSE;

        return success;
}

static gboolean
demarshal_retvals__OBJPATH (EVariant *retvals, char **OUT_objpath1)
{
        gboolean success = TRUE;

        if (retvals) {
                const char *objpath1 = NULL;

                e_variant_get (retvals, "(o)", &objpath1);
                if (objpath1) {
                        *OUT_objpath1 = g_strdup (objpath1);
                } else {
                        success = FALSE;
                }

                e_variant_unref (retvals);
        } else {
                success = FALSE;
        }

        return success;
}

static gboolean
demarshal_retvals__STRING (EVariant *retvals, char **OUT_string1)
{
        gboolean success = TRUE;

        if (retvals) {
                const char *string1 = NULL;

                e_variant_get (retvals, "(s)", &string1);
                if (string1) {
                        *OUT_string1 = g_strdup (string1);
                } else {
                        success = FALSE;
                }

                e_variant_unref (retvals);
        } else {
                success = FALSE;
        }

        return success;
}

static void
string_dup_from_variant (EVariant  *variant,
			 guint      child_position,
			 char     **dest,
			 gboolean  *success)
{
	EVariant *strine_variant;
	char *string = NULL;

	strine_variant = e_variant_get_child_value (variant, child_position);
	string = e_variant_dup_string (strine_variant, NULL);

	if (string) {
		*dest = string;
	} else {
		*success = FALSE;
	}
}

static void
strv_dup_from_variant (EVariant   *variant,
		       guint       child_position,
		       char     ***dest,
		       gboolean   *success)
{
	EVariant *strv_variant;
	char **strv = NULL;

	strv_variant = e_variant_get_child_value (variant, child_position);
	strv = e_variant_dup_strv (strv_variant, NULL);

	if (strv) {
		*dest = strv;
	} else {
		*success = FALSE;
	}
}

static gboolean
demarshal_retvals__STRINGVECTOR (EVariant *retvals, char ***OUT_strv1)
{
        gboolean success = TRUE;

        if (retvals) {
		strv_dup_from_variant (retvals, 0, OUT_strv1, &success);
                e_variant_unref (retvals);
        } else {
                success = FALSE;
        }

        return success;
}

static gboolean
demarshal_retvals__STRINGVECTOR_STRING (EVariant   *retvals,
					char     ***OUT_strv1,
					char      **OUT_string1)
{
        gboolean success = TRUE;

        if (retvals) {
		string_dup_from_variant (retvals, 1, OUT_string1, &success);
                e_variant_unref (retvals);
        } else {
                success = FALSE;
        }

        return success;
}

static gboolean
demarshal_retvals__STRINGVECTOR_STRINGVECTOR_STRINGVECTOR (EVariant   *retvals,
							   char     ***OUT_strv1,
							   char     ***OUT_strv2,
							   char     ***OUT_strv3)
{
        gboolean success = TRUE;

        if (retvals) {
		strv_dup_from_variant (retvals, 0, OUT_strv1, &success);
		strv_dup_from_variant (retvals, 1, OUT_strv2, &success);
		strv_dup_from_variant (retvals, 2, OUT_strv3, &success);
                e_variant_unref (retvals);
        } else {
                success = FALSE;
        }

        return success;
}

static gboolean
demarshal_retvals__GPTRARRAY_with_GVALUEARRAY_with_UINT_STRING_endwith_endwith
	(EVariant   *retvals,
	 GPtrArray **OUT_ptrarray1)
{
        gboolean success = TRUE;

        if (retvals) {
                EVariant *array1_variant;
		EVariantIter iter1;
                GPtrArray *ptrarray1 = NULL;
		const guint uint1_tmp;
		const char *string1_tmp = NULL;

		/* deshelling a (a(us)); there should always be exactly one
		 * a(us) in the outermost tuple */
                array1_variant = e_variant_get_child_value (retvals, 0);
		e_variant_iter_init (&iter1, array1_variant);

		/* return NULL instead of an empty GPtrArray* if there's nothing
		 * to put in it */
		if (e_variant_n_children (array1_variant) < 1) {
			goto empty_inner_array;
		}

		ptrarray1 = g_ptr_array_new ();

		while (e_variant_iter_next (&iter1, "(us)", &uint1_tmp, &string1_tmp)) {
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

                e_variant_unref (retvals);
        } else {
                success = FALSE;
        }

        return success;
}


typedef void (*reply__VOID) (EDBusProxy *proxy,
                             GError     *error,
                             gpointer    user_data);

typedef void (*reply__OBJPATH) (EDBusProxy *proxy,
				char       *OUT_path1,
				GError     *error,
				gpointer    user_data);

typedef void (*reply__STRING) (EDBusProxy *proxy,
                               char       *OUT_string1,
                               GError     *error,
                               gpointer    user_data);

typedef void (*reply__STRINGVECTOR) (EDBusProxy  *proxy,
                                     char       **OUT_strv1,
                                     GError      *error,
                                     gpointer     user_data);

typedef void (*reply__GPTRARRAY_with_GVALUEARRAY_with_UINT_STRING_endwith_endwith) (EDBusProxy *proxy,
    GPtrArray  *OUT_ptrarray1,
    GError     *error,
    gpointer    user_data);

#endif /* _E_DATA_GDBUS_BINDINGS_COMMON_H */
