/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-vcf-factory.c - VCF contact backend factory.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * Authors: Chris Toshok <toshok@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "libebackend/e-data-server-module.h"
#include "libedata-book/e-book-backend-factory.h"
#include "e-book-backend-vcf.h"

E_BOOK_BACKEND_FACTORY_SIMPLE (vcf, VCF, e_book_backend_vcf_new)

static GType  vcf_type;

void
eds_module_initialize (GTypeModule *module)
{
	vcf_type = _vcf_factory_get_type (module);
}

void
eds_module_shutdown   (void)
{
}

void
eds_module_list_types (const GType **types, gint *num_types)
{
	*types = & vcf_type;
	*num_types = 1;
}

