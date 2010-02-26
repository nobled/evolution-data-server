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

#ifndef __E_VARIANT_PRIVATE_H__
#define __E_VARIANT_PRIVATE_H__

#include "evarianttypeinfo.h"
#include "evariant.h"

/* gvariant-core.c */
EVariant *                      e_variant_new_tree                      (const EVariantType  *type,
                                                                         EVariant           **children,
                                                                         gsize                n_children,
                                                                         gboolean             trusted);
void                            e_variant_assert_invariant              (EVariant            *value);
gboolean                        e_variant_is_trusted                    (EVariant            *value);
void                            e_variant_dump_data                     (EVariant            *value);
EVariant *                      e_variant_load_fixed                    (const EVariantType  *type,
                                                                         gconstpointer        data,
                                                                         gsize                n_items);
gboolean                        e_variant_iter_should_free              (EVariantIter        *iter);
EVariant *                      e_variant_deep_copy                     (EVariant            *value);

/* do not use -- only for test cases */
gboolean                        e_variant_is_normal_                    (EVariant            *value);

#endif /* __E_VARIANT_PRIVATE_H__ */
