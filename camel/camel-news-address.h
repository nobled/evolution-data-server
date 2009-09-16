/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Michael Zucchi <NotZed@ximian.com>
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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_DISABLE_DEPRECATED

#ifndef CAMEL_NEWS_ADDRESS_H
#define CAMEL_NEWS_ADDRESS_H

#include <camel/camel-address.h>

/* Standard GObject macros */
#define CAMEL_TYPE_NEWS_ADDRESS \
	(camel_news_address_get_type ())
#define CAMEL_NEWS_ADDRESS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_NEWS_ADDRESS, CamelNewsAddress))
#define CAMEL_NEWS_ADDRESS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_NEWS_ADDRESS, CamelNewsAddressClass))
#define CAMEL_IS_NEWS_ADDRESS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_NEWS_ADDRESS))
#define CAMEL_IS_NEWS_ADDRESS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_NEWS_ADDRESS))
#define CAMEL_NEWS_ADDRESS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_NEWS_ADDRESS, CamelNewsAddressClass))

G_BEGIN_DECLS

typedef struct _CamelNewsAddress CamelNewsAddress;
typedef struct _CamelNewsAddressClass CamelNewsAddressClass;
typedef struct _CamelNewsAddressPrivate CamelNewsAddressPrivate;

struct _CamelNewsAddress {
	CamelAddress parent;
	CamelNewsAddressPrivate *priv;
};

struct _CamelNewsAddressClass {
	CamelAddressClass parent_class;
};

GType		camel_news_address_get_type	(void);
CamelNewsAddress      *camel_news_address_new	(void);

G_END_DECLS

#endif /* CAMEL_NEWS_ADDRESS_H */

#endif /* CAMEL_DISABLE_DEPRECATED */
