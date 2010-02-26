/*
 * Copyright © 2007, 2008 Ryan Lortie
 * Copyright © 2010 Codethink Limited
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
 * Author: Ryan Lortie <desrt@desrt.ca>
 */

#ifndef __E_VARIANT_SERIALISER_H__
#define __E_VARIANT_SERIALISER_H__

#include "gvarianttypeinfo.h"

typedef struct
{
  EVariantTypeInfo *type_info;
  guchar           *data;
  gsize             size;
} EVariantSerialised;

/* deserialisation */
G_GNUC_INTERNAL
gsize                           e_variant_serialised_n_children         (EVariantSerialised        container);
G_GNUC_INTERNAL
EVariantSerialised              e_variant_serialised_get_child          (EVariantSerialised        container,
                                                                         gsize                     index);

/* serialisation */
typedef void                  (*EVariantSerialisedFiller)               (EVariantSerialised       *serialised,
                                                                         gpointer                  data);

G_GNUC_INTERNAL
gsize                           e_variant_serialiser_needed_size        (EVariantTypeInfo         *info,
                                                                         EVariantSerialisedFiller  gsv_filler,
                                                                         const gpointer           *children,
                                                                         gsize                     n_children);

G_GNUC_INTERNAL
void                            e_variant_serialiser_serialise          (EVariantSerialised        container,
                                                                         EVariantSerialisedFiller  gsv_filler,
                                                                         const gpointer           *children,
                                                                         gsize                     n_children);

/* misc */
G_GNUC_INTERNAL
gboolean                        e_variant_serialised_is_normal          (EVariantSerialised        value);
G_GNUC_INTERNAL
void                            e_variant_serialised_byteswap           (EVariantSerialised        value);

/* validation of strings */
G_GNUC_INTERNAL
gboolean                        e_variant_serialiser_is_string          (gconstpointer             data,
                                                                         gsize                     size);
G_GNUC_INTERNAL
gboolean                        e_variant_serialiser_is_object_path     (gconstpointer             data,
                                                                         gsize                     size);
G_GNUC_INTERNAL
gboolean                        e_variant_serialiser_is_signature       (gconstpointer             data,
                                                                         gsize                     size);

#endif /* __E_VARIANT_SERIALISER_H__ */
