/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of version 2 of the GNU Lesser General Public
 *  License as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public 
 *  License along with this program; if not, write to: 
 *  Free Software Foundation, 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 *
 */

#ifndef __E_BOOK_BACKEND_GALLDAP_H__
#define __E_BOOK_BACKEND_GALLDAP_H__

#include "libedata-book/e-book-backend.h"

#ifdef SUNLDAP
/*   copy from openldap ldap.h   */
#define LDAP_RANGE(n,x,y)      (((x) <= (n)) && ((n) <= (y)))
#define LDAP_NAME_ERROR(n)     LDAP_RANGE((n), 0x20, 0x24)
#define LBER_USE_DER			0x01
#define LDAP_CONTROL_PAGEDRESULTS      "1.2.840.113556.1.4.319"
#endif

typedef struct _EBookBackendGALLDAPPrivate EBookBackendGALLDAPPrivate;

typedef struct {
	EBookBackend             parent_object;
	EBookBackendGALLDAPPrivate *priv;
} EBookBackendGALLDAP;

typedef struct {
	EBookBackendClass parent_class;
} EBookBackendGALLDAPClass;

EBookBackend *e_book_backend_galldap_new      (void);
GType       e_book_backend_galldap_get_type (void);

#define E_TYPE_BOOK_BACKEND_GALLDAP        (e_book_backend_galldap_get_type ())
#define E_BOOK_BACKEND_GALLDAP(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_BACKEND_GALLDAP, EBookBackendGALLDAP))
#define E_BOOK_BACKEND_GALLDAPLDAP_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST ((k), E_TYPE_BOOK_BACKEND_GALLDAP, EBookBackendGALLDAPClass))
#define E_IS_BOOK_BACKEND_GALLDAP(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_BACKEND_GALLDAP))
#define E_IS_BOOK_BACKEND_GALLDAP_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_BACKEND_GALLDAP))

#endif /* ! __E_BOOK_BACKEND_GALLDAP_H__ */

