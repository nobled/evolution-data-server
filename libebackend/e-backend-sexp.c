/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2009-2010 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Authors:
 *   Chris Lahey <clahey@ximian.com>
 *   Travis Reitter <travis.reitter@collabora.co.uk>
 */

#include <string.h>
#include "libedataserver/e-sexp.h"
#include "libedataserver/e-data-server-util.h"
#include "e-backend-sexp.h"

G_DEFINE_ABSTRACT_TYPE (EBackendSExp, e_backend_sexp, G_TYPE_OBJECT);

struct _EBackendSExpPrivate {
};

/* FIXME: this class is currently just used for superficial type-safety;
 * actually pull in what shallow commonality there is between EBookBackendSExp
 * and ECalBackendSExp */

/**
 * e_backend_sexp_new:
 * @text: an s-expression to parse
 *
 * Creates a new #EBackendSExp from @text.
 *
 * Return value: A new #EBackendSExp.
 **/
EBackendSExp *
e_backend_sexp_new (const gchar *text)
{
	EBackendSExp *sexp = g_object_new (E_TYPE_BACKEND_SEXP, NULL);

	return sexp;
}

static void
e_backend_sexp_class_init (EBackendSExpClass *klass)
{
}

static void
e_backend_sexp_init (EBackendSExp *sexp)
{
	EBackendSExpPrivate *priv;

	priv = g_new0 (EBackendSExpPrivate, 1);
	sexp->priv = priv;
}
