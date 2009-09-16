/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors:
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
 */

#include "camel-news-address.h"

GType
camel_news_address_get_type (void)
{
	static guint type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID))
		type = g_type_register_static_simple (
			CAMEL_TYPE_ADDRESS,
			"CamelNewsAddress",
			sizeof (CamelNewsAddressClass),
			(GClassInitFunc) NULL,
			sizeof (CamelNewsAddress),
			(GInstanceInitFunc) NULL,
			0);

	return type;
}

/**
 * camel_news_address_new:
 *
 * Create a new CamelNewsAddress object.
 *
 * Return value: A new CamelNewsAddress widget.
 **/
CamelNewsAddress *
camel_news_address_new (void)
{
	return g_object_new (CAMEL_TYPE_NEWS_ADDRESS, NULL);
}
