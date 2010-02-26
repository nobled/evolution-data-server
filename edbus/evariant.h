/*
 * Copyright © 2007, 2008 Ryan Lortie
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
 */

#ifndef __E_VARIANT_H__
#define __E_VARIANT_H__

#include "evarianttype.h"
#include <glib.h>
#include <glib-object.h>
#include <stdarg.h>

typedef struct _EVariant        EVariant;
typedef struct _EVariantIter    EVariantIter;
typedef struct _EVariantBuilder EVariantBuilder;
typedef enum   _EVariantClass   EVariantClass;

/* compatibility bits yanked from other pieces of glib */
#ifndef GSIZE_FROM_LE
#  define GSIZE_FROM_LE(val)      (GSIZE_TO_LE (val))
#  define GSSIZE_FROM_LE(val)     (GSSIZE_TO_LE (val))
// tad of a hack for now, but safe I hope
#  if GLIB_SIZEOF_SIZE_T == 4
#    define GSIZE_TO_LE(val)   ((gsize) GUINT32_TO_LE (val))
#  elif GLIB_SIZEOF_SIZE_T == 8
#    define GSIZE_TO_LE(val)   ((gsize) GUINT64_TO_LE (val))
#  else
#    error "Weirdo architecture - needs an updated glib"
#  endif
#endif
#ifndef E_TYPE_VARIANT
#  define        E_TYPE_VARIANT (e_variant_get_gtype ())
GType   e_variant_get_gtype     (void)  G_GNUC_CONST;
#endif

enum _EVariantClass
{
  E_VARIANT_CLASS_BOOLEAN       = 'b',
  E_VARIANT_CLASS_BYTE          = 'y',
  E_VARIANT_CLASS_INT16         = 'n',
  E_VARIANT_CLASS_UINT16        = 'q',
  E_VARIANT_CLASS_INT32         = 'i',
  E_VARIANT_CLASS_UINT32        = 'u',
  E_VARIANT_CLASS_INT64         = 'x',
  E_VARIANT_CLASS_UINT64        = 't',
  E_VARIANT_CLASS_HANDLE        = 'h',
  E_VARIANT_CLASS_DOUBLE        = 'd',
  E_VARIANT_CLASS_STRING        = 's',
  E_VARIANT_CLASS_OBJECT_PATH   = 'o',
  E_VARIANT_CLASS_SIGNATURE     = 'g',
  E_VARIANT_CLASS_VARIANT       = 'v',
  E_VARIANT_CLASS_MAYBE         = 'm',
  E_VARIANT_CLASS_ARRAY         = 'a',
  E_VARIANT_CLASS_TUPLE         = '(',
  E_VARIANT_CLASS_DICT_ENTRY    = '{'
};

struct _EVariantIter
{
  gpointer priv[8];
};

G_BEGIN_DECLS

EVariant *                      e_variant_ref                           (EVariant             *value);
EVariant *                      e_variant_ref_sink                      (EVariant             *value);
void                            e_variant_unref                         (EVariant             *value);
void                            e_variant_flatten                       (EVariant             *value);

const EVariantType *            e_variant_get_type                      (EVariant             *value);
const gchar *                   e_variant_get_type_string               (EVariant             *value);
gboolean                        e_variant_is_basic                      (EVariant             *value);
gboolean                        e_variant_is_container                  (EVariant             *value);
gboolean                        e_variant_has_type                      (EVariant             *value,
                                                                         const EVariantType   *pattern);

/* varargs construct/deconstruct */
EVariant *                      e_variant_new                           (const gchar          *format_string,
                                                                         ...);
void                            e_variant_get                           (EVariant             *value,
                                                                         const gchar          *format_string,
                                                                         ...);

gboolean                        e_variant_format_string_scan            (const gchar          *string,
                                                                         const gchar          *limit,
                                                                         const gchar         **endptr);
EVariantType *                  e_variant_format_string_scan_type       (const gchar          *string,
                                                                         const gchar          *limit,
                                                                         const gchar         **endptr);
EVariant *                      e_variant_new_va                        (gpointer              must_be_null,
                                                                         const gchar          *format_string,
                                                                         const gchar         **endptr,
                                                                         va_list              *app);
void                            e_variant_get_va                        (EVariant             *value,
                                                                         gpointer              must_be_null,
                                                                         const gchar          *format_string,
                                                                         const gchar         **endptr,
                                                                         va_list              *app);

/* constructors */
EVariant *                      e_variant_new_boolean                   (gboolean              boolean);
EVariant *                      e_variant_new_byte                      (guint8                byte);
EVariant *                      e_variant_new_uint16                    (guint16               uint16);
EVariant *                      e_variant_new_int16                     (gint16                int16);
EVariant *                      e_variant_new_uint32                    (guint32               uint32);
EVariant *                      e_variant_new_int32                     (gint32                int32);
EVariant *                      e_variant_new_uint64                    (guint64               uint64);
EVariant *                      e_variant_new_int64                     (gint64                int64);
EVariant *                      e_variant_new_double                    (gdouble               floating);
EVariant *                      e_variant_new_string                    (const gchar          *string);
EVariant *                      e_variant_new_object_path               (const gchar          *string);
gboolean                        e_variant_is_object_path                (const gchar          *string);
EVariant *                      e_variant_new_signature                 (const gchar          *string);
gboolean                        e_variant_is_signature                  (const gchar          *string);
EVariant *                      e_variant_new_variant                   (EVariant             *value);
EVariant *                      e_variant_new_handle                    (gint32                handle);
EVariant *                      e_variant_new_strv                      (const gchar * const  *strv,
                                                                         gint                  length);

/* deconstructors */
gboolean                        e_variant_get_boolean                   (EVariant             *value);
guint8                          e_variant_get_byte                      (EVariant             *value);
guint16                         e_variant_get_uint16                    (EVariant             *value);
gint16                          e_variant_get_int16                     (EVariant             *value);
guint32                         e_variant_get_uint32                    (EVariant             *value);
gint32                          e_variant_get_int32                     (EVariant             *value);
guint64                         e_variant_get_uint64                    (EVariant             *value);
gint64                          e_variant_get_int64                     (EVariant             *value);
gdouble                         e_variant_get_double                    (EVariant             *value);
const gchar *                   e_variant_get_string                    (EVariant             *value,
                                                                         gsize                *length);
gchar *                         e_variant_dup_string                    (EVariant             *value,
                                                                         gsize                *length);
const gchar **                  e_variant_get_strv                      (EVariant             *value,
                                                                         gint                 *length);
gchar **                        e_variant_dup_strv                      (EVariant             *value,
                                                                         gint                 *length);
EVariant *                      e_variant_get_variant                   (EVariant             *value);
gint32                          e_variant_get_handle                    (EVariant             *value);
gconstpointer                   e_variant_get_fixed                     (EVariant             *value,
                                                                         gsize                 size);
gconstpointer                   e_variant_get_fixed_array               (EVariant             *value,
                                                                         gsize                 elem_size,
                                                                         gsize                *length);
gsize                           e_variant_n_children                    (EVariant             *value);
EVariant *                      e_variant_get_child_value               (EVariant             *value,
                                                                         gsize                 index);
void                            e_variant_get_child                     (EVariant             *value,
                                                                         gint                  index,
                                                                         const gchar          *format_string,
                                                                         ...);
EVariant *                      e_variant_lookup_value                  (EVariant             *dictionary,
                                                                         const gchar          *key);
gboolean                        e_variant_lookup                        (EVariant             *dictionary,
                                                                         const gchar          *key,
                                                                         const gchar          *format_string,
                                                                         ...);

/* EVariantIter */
gsize                           e_variant_iter_init                     (EVariantIter         *iter,
                                                                         EVariant             *value);
EVariant *                      e_variant_iter_next_value               (EVariantIter         *iter);
void                            e_variant_iter_cancel                   (EVariantIter         *iter);
gboolean                        e_variant_iter_was_cancelled            (EVariantIter         *iter);
gboolean                        e_variant_iter_next                     (EVariantIter         *iter,
                                                                         const gchar          *format_string,
                                                                         ...);

/* EVariantBuilder */
void                            e_variant_builder_add_value             (EVariantBuilder      *builder,
                                                                         EVariant             *value);
void                            e_variant_builder_add                   (EVariantBuilder      *builder,
                                                                         const gchar          *format_string,
                                                                         ...);
EVariantBuilder *               e_variant_builder_open                  (EVariantBuilder      *parent,
                                                                         const EVariantType   *type);
EVariantBuilder *               e_variant_builder_close                 (EVariantBuilder      *child);
gboolean                        e_variant_builder_check_add             (EVariantBuilder      *builder,
                                                                         const EVariantType   *type,
                                                                         GError              **error);
gboolean                        e_variant_builder_check_end             (EVariantBuilder      *builder,
                                                                         GError              **error);
EVariantBuilder *               e_variant_builder_new                   (const EVariantType   *type);
EVariant *                      e_variant_builder_end                   (EVariantBuilder      *builder);
void                            e_variant_builder_cancel                (EVariantBuilder      *builder);

#define E_VARIANT_BUILDER_ERROR \
    (g_quark_from_static_string ("g-variant-builder-error-quark"))

typedef enum
{
  E_VARIANT_BUILDER_ERROR_TOO_MANY,
  E_VARIANT_BUILDER_ERROR_TOO_FEW,
  E_VARIANT_BUILDER_ERROR_INFER,
  E_VARIANT_BUILDER_ERROR_TYPE
} EVariantBuilderError;

/* text printing/parsing */
typedef struct
{
  const gchar *start;
  const gchar *end;
  gchar *error;
} EVariantParseError;

gchar *                         e_variant_print                         (EVariant             *value,
                                                                         gboolean              type_annotate);
GString *                       e_variant_print_string                  (EVariant             *value,
                                                                         GString              *string,
                                                                         gboolean              type_annotate);
EVariant *                      e_variant_parse                         (const gchar          *text,
                                                                         gint                  text_length,
                                                                         const EVariantType   *type,
                                                                         GError              **error);
EVariant *                      e_variant_parse_full                    (const gchar          *text,
                                                                         const gchar          *limit,
                                                                         const gchar         **endptr,
                                                                         const EVariantType   *type,
                                                                         EVariantParseError   *error);
EVariant *                      e_variant_new_parsed                    (const gchar          *format,
                                                                         ...);
EVariant *                      e_variant_new_parsed_va                 (const gchar          *format,
                                                                         va_list              *app);

/* markup printing/parsing */
gchar *                         e_variant_markup_print                  (EVariant             *value,
                                                                         gboolean              newlines,
                                                                         gint                  indentation,
                                                                         gint                  tabstop);
GString *                       e_variant_markup_print_string           (EVariant             *value,
                                                                         GString              *string,
                                                                         gboolean              newlines,
                                                                         gint                  indentation,
                                                                         gint                  tabstop);
void                            e_variant_markup_subparser_start        (GMarkupParseContext  *context,
                                                                         const EVariantType   *type);
EVariant *                      e_variant_markup_subparser_end          (GMarkupParseContext  *context,
                                                                         GError              **error);
GMarkupParseContext *           e_variant_markup_parse_context_new      (GMarkupParseFlags     flags,
                                                                         const EVariantType   *type);
EVariant *                      e_variant_markup_parse_context_end      (GMarkupParseContext  *context,
                                                                         GError              **error);
EVariant *                      e_variant_markup_parse                  (const gchar          *text,
                                                                         gssize                text_len,
                                                                         const EVariantType   *type,
                                                                         GError              **error);

/* load/store serialised format */
typedef enum
{
  E_VARIANT_LITTLE_ENDIAN       = G_LITTLE_ENDIAN,
  E_VARIANT_BIG_ENDIAN          = G_BIG_ENDIAN,
  E_VARIANT_TRUSTED             = 0x00010000,
  E_VARIANT_LAZY_BYTESWAP       = 0x00020000,
} EVariantFlags;

EVariant *                      e_variant_load                          (const EVariantType  *type,
                                                                         gconstpointer        data,
                                                                         gsize                size,
                                                                         EVariantFlags        flags);
EVariant *                      e_variant_from_slice                    (const EVariantType  *type,
                                                                         gpointer             slice,
                                                                         gsize                size,
                                                                         EVariantFlags        flags);
EVariant *                      e_variant_from_data                     (const EVariantType  *type,
                                                                         gconstpointer        data,
                                                                         gsize                size,
                                                                         EVariantFlags        flags,
                                                                         GDestroyNotify       notify,
                                                                         gpointer             user_data);
EVariant *                      e_variant_from_file                     (const EVariantType  *type,
                                                                         const gchar         *filename,
                                                                         EVariantFlags        flags,
                                                                         GError              **error);

void                            e_variant_store                         (EVariant            *value,
                                                                         gpointer             data);
gconstpointer                   e_variant_get_data                      (EVariant            *value);
gsize                           e_variant_get_size                      (EVariant            *value);

#define E_VARIANT_JUST ((gboolean *) "truetrue")

EVariantClass                   e_variant_classify                      (EVariant            *value);

#define e_variant_get_type_class e_variant_classify

G_END_DECLS

#endif /* __E_VARIANT_H__ */
