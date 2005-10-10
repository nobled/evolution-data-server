/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-ldap.c - LDAP contact backend.
 *
 * Copyright (C) 2005 Novell, Inc.
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Chris Toshok <toshok@ximian.com>
 *          Hans Petter Jansson <hpj@novell.com>
 */

#define DEBUG

#ifdef HAVE_CONFIG_H
#include <config.h>  
#endif

#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

#ifdef DEBUG
#define LDAP_DEBUG
#define LDAP_DEBUG_ADD
#endif
#include <ldap.h>
#ifdef DEBUG
#undef LDAP_DEBUG
#endif

#define d(x) x

#if LDAP_VENDOR_VERSION > 20000
#define OPENLDAP2
#else
#define OPENLDAP1
#endif

#ifdef OPENLDAP2
#include <ldap_schema.h>
#endif

#include <sys/time.h>

#include <glib/gi18n-lib.h>
#include <libedataserver/e-sexp.h>
#include <libebook/e-contact.h>

#include <libedata-book/e-book-backend-sexp.h>
#include <libedata-book/e-data-book.h>
#include <libedata-book/e-data-book-view.h>
#include <libedata-book/e-book-backend-cache.h>
#include <libedata-book/e-book-backend-summary.h>
#include "e-book-backend-ldap.h"

/* this is broken currently, don't enable it */
/*#define ENABLE_SASL_BINDS*/

typedef enum {
	E_BOOK_BACKEND_LDAP_TLS_NO,
	E_BOOK_BACKEND_LDAP_TLS_ALWAYS,
	E_BOOK_BACKEND_LDAP_TLS_WHEN_POSSIBLE,
} EBookBackendLDAPUseTLS;

/* interval for our poll_ldap timeout */
#define LDAP_POLL_INTERVAL 20

/* timeout for ldap_result */
#define LDAP_RESULT_TIMEOUT_MILLIS 10

#define TV_TO_MILLIS(timeval) ((timeval).tv_sec * 1000 + (timeval).tv_usec / 1000)

/* the objectClasses we need */
#define TOP                  "top"
#define PERSON               "person"
#define ORGANIZATIONALPERSON "organizationalPerson"
#define INETORGPERSON        "inetOrgPerson"
#define CALENTRY             "calEntry"
#define EVOLUTIONPERSON      "evolutionPerson"

static gchar *query_prop_to_ldap(gchar *query_prop);
static gchar *e_book_backend_ldap_build_query (EBookBackendLDAP *bl, const char *query);

static EBookBackendClass *e_book_backend_ldap_parent_class;
typedef struct LDAPOp LDAPOp;


struct _EBookBackendLDAPPrivate {
	gboolean connected;

	gchar    *ldap_host;   /* the hostname of the server */
	int      ldap_port;    /* the port of the server */
	char     *schema_dn;   /* the base dn for schema information */
	gchar    *ldap_rootdn; /* the base dn of our searches */
	int      ldap_scope;   /* the scope used for searches */
	int      ldap_limit;   /* the search limit */
	int      ldap_timeout; /* the search timeout */

	gchar   *auth_dn;
	gchar   *auth_passwd;

	gboolean ldap_v3;      /* TRUE if the server supports protocol
                                  revision 3 (necessary for TLS) */
	gboolean starttls;     /* TRUE if the *library* supports
                                  starttls.  will be false if openssl
                                  was not built into openldap. */
	EBookBackendLDAPUseTLS use_tls;

	LDAP     *ldap;

	GList    *supported_fields;
	GList    *supported_auth_methods;

	EBookBackendCache *cache;

	/* whether or not there's support for the objectclass we need
           to store all our additional fields */
	gboolean evolutionPersonSupported;
	gboolean calEntrySupported;
	gboolean evolutionPersonChecked;
	gboolean marked_for_offline;
	
	int mode;
	/* our operations */
	GStaticRecMutex op_hash_mutex;
	GHashTable *id_to_op;
	int active_ops;
	int poll_timeout;

	/* summary file related */
	char *summary_file_name;
	gboolean is_summary_ready;
	EBookBackendSummary *summary;

	/* for future use */
	void *reserved1;
	void *reserved2;
	void *reserved3;
	void *reserved4;
};

typedef void (*LDAPOpHandler)(LDAPOp *op, LDAPMessage *res);
typedef void (*LDAPOpDtor)(LDAPOp *op);

struct LDAPOp {
	LDAPOpHandler  handler;
	LDAPOpDtor     dtor;
	EBookBackend  *backend;
	EDataBook     *book;
	EDataBookView *view;
	guint32        opid; /* the libebook operation id */
	int            id;   /* the ldap msg id */
};

static void     ldap_op_add (LDAPOp *op, EBookBackend *backend, EDataBook *book,
			     EDataBookView *view, int opid, int msgid, LDAPOpHandler handler, LDAPOpDtor dtor);
static void     ldap_op_finished (LDAPOp *op);

static gboolean poll_ldap (EBookBackendLDAP *bl);

static EContact *build_contact_from_entry (LDAP *ldap, LDAPMessage *e, GList **existing_objectclasses);

static void email_populate (EContact *contact, char **values);
static struct berval** email_ber (EContact *contact);
static gboolean email_compare (EContact *contact1, EContact *contact2);

static void homephone_populate (EContact *contact, char **values);
static struct berval** homephone_ber (EContact *contact);
static gboolean homephone_compare (EContact *contact1, EContact *contact2);

static void business_populate (EContact *contact, char **values);
static struct berval** business_ber (EContact *contact);
static gboolean business_compare (EContact *contact1, EContact *contact2);

static void anniversary_populate (EContact *contact, char **values);
static struct berval** anniversary_ber (EContact *contact);
static gboolean anniversary_compare (EContact *contact1, EContact *contact2);

static void birthday_populate (EContact *contact, char **values);
static struct berval** birthday_ber (EContact *contact);
static gboolean birthday_compare (EContact *contact1, EContact *contact2);

static void category_populate (EContact *contact, char **values);
static struct berval** category_ber (EContact *contact);
static gboolean category_compare (EContact *contact1, EContact *contact2);

static void home_address_populate(EContact * card, char **values);
static struct berval **home_address_ber(EContact * card);
static gboolean home_address_compare(EContact * ecard1, EContact * ecard2);

static void work_address_populate(EContact * card, char **values);
static struct berval **work_address_ber(EContact * card);
static gboolean work_address_compare(EContact * ecard1, EContact * ecard2);

static void other_address_populate(EContact * card, char **values);
static struct berval **other_address_ber(EContact * card);
static gboolean other_address_compare(EContact * ecard1, EContact * ecard2);

static void photo_populate (EContact *contact, struct berval **ber_values);
static struct berval **photo_ber (EContact * contact);
static gboolean photo_compare(EContact * ecard1, EContact * ecard2);

static void cert_populate (EContact *contact, struct berval **ber_values);

struct prop_info {
	EContactField field_id;
	char *ldap_attr;
#define PROP_TYPE_STRING   0x01
#define PROP_TYPE_COMPLEX  0x02
#define PROP_TYPE_BINARY   0x04
#define PROP_DN            0x08
#define PROP_EVOLVE        0x10
#define PROP_WRITE_ONLY    0x20
	int prop_type;

	/* the remaining items are only used for the TYPE_COMPLEX props */

	/* used when reading from the ldap server populates EContact with the values in **values. */
	void (*populate_contact_func)(EContact *contact, char **values);
	/* used when writing to an ldap server.  returns a NULL terminated array of berval*'s */
	struct berval** (*ber_func)(EContact *contact);
	/* used to compare list attributes */
	gboolean (*compare_func)(EContact *contact1, EContact *contact2);

	void (*binary_populate_contact_func)(EContact *contact, struct berval **ber_values);

} prop_info[] = {

#define BINARY_PROP(fid,a,ctor,ber,cmp) {fid, a, PROP_TYPE_BINARY, NULL, ber, cmp, ctor}
#define COMPLEX_PROP(fid,a,ctor,ber,cmp) {fid, a, PROP_TYPE_COMPLEX, ctor, ber, cmp}
#define E_COMPLEX_PROP(fid,a,ctor,ber,cmp) {fid, a, PROP_TYPE_COMPLEX | PROP_EVOLVE, ctor, ber, cmp}
#define STRING_PROP(fid,a) {fid, a, PROP_TYPE_STRING}
#define WRITE_ONLY_STRING_PROP(fid,a) {fid, a, PROP_TYPE_STRING | PROP_WRITE_ONLY}
#define E_STRING_PROP(fid,a) {fid, a, PROP_TYPE_STRING | PROP_EVOLVE}


	/* name fields */
	STRING_PROP (E_CONTACT_FULL_NAME,   "cn" ),
	WRITE_ONLY_STRING_PROP (E_CONTACT_FAMILY_NAME, "sn" ),

	/* email addresses */
	COMPLEX_PROP   (E_CONTACT_EMAIL, "mail", email_populate, email_ber, email_compare),

	/* phone numbers */
	E_STRING_PROP (E_CONTACT_PHONE_PRIMARY,      "primaryPhone"),
	COMPLEX_PROP  (E_CONTACT_PHONE_BUSINESS,     "telephoneNumber", business_populate, business_ber, business_compare),
	COMPLEX_PROP  (E_CONTACT_PHONE_HOME,         "homePhone", homephone_populate, homephone_ber, homephone_compare),
	STRING_PROP   (E_CONTACT_PHONE_MOBILE,       "mobile"),
	E_STRING_PROP (E_CONTACT_PHONE_CAR,          "carPhone"),
	STRING_PROP   (E_CONTACT_PHONE_BUSINESS_FAX, "facsimileTelephoneNumber"), 
	E_STRING_PROP (E_CONTACT_PHONE_HOME_FAX,     "homeFacsimileTelephoneNumber"), 
	E_STRING_PROP (E_CONTACT_PHONE_OTHER,        "otherPhone"), 
	E_STRING_PROP (E_CONTACT_PHONE_OTHER_FAX,    "otherFacsimileTelephoneNumber"), 
	STRING_PROP   (E_CONTACT_PHONE_ISDN,         "internationaliSDNNumber"), 
	STRING_PROP   (E_CONTACT_PHONE_PAGER,        "pager"),
	E_STRING_PROP (E_CONTACT_PHONE_RADIO,        "radio"),
	E_STRING_PROP (E_CONTACT_PHONE_TELEX,        "telex"),
	E_STRING_PROP (E_CONTACT_PHONE_ASSISTANT,    "assistantPhone"),
	E_STRING_PROP (E_CONTACT_PHONE_COMPANY,      "companyPhone"),
	E_STRING_PROP (E_CONTACT_PHONE_CALLBACK,     "callbackPhone"),
	E_STRING_PROP (E_CONTACT_PHONE_TTYTDD,       "tty"),

	/* org information */
	STRING_PROP   (E_CONTACT_ORG,       "o"),
	STRING_PROP   (E_CONTACT_ORG_UNIT,  "ou"),
	STRING_PROP   (E_CONTACT_OFFICE,    "roomNumber"),
	STRING_PROP   (E_CONTACT_TITLE,     "title"),
	E_STRING_PROP (E_CONTACT_ROLE,      "businessRole"), 
	E_STRING_PROP (E_CONTACT_MANAGER,   "managerName"), 
	E_STRING_PROP (E_CONTACT_ASSISTANT, "assistantName"), 

	/* addresses */
	COMPLEX_PROP  (E_CONTACT_ADDRESS_LABEL_WORK, "postalAddress", work_address_populate, work_address_ber, work_address_compare),
	COMPLEX_PROP  (E_CONTACT_ADDRESS_LABEL_HOME,     "homePostalAddress", home_address_populate, home_address_ber, home_address_compare),
	E_COMPLEX_PROP(E_CONTACT_ADDRESS_LABEL_OTHER,    "otherPostalAddress", other_address_populate, other_address_ber, other_address_compare),

	/* photos */
	BINARY_PROP  (E_CONTACT_PHOTO,       "jpegPhoto", photo_populate, photo_ber, photo_compare),

	/* certificate foo. */
	BINARY_PROP  (E_CONTACT_X509_CERT,   "userCertificate", cert_populate, NULL/*XXX*/, NULL/*XXX*/),
#if 0
	/* hm, which do we use?  the inetOrgPerson schema says that
	   userSMIMECertificate should be used in favor of
	   userCertificate for S/MIME applications. */
	BINARY_PROP  (E_CONTACT_X509_CERT,   "userSMIMECertificate", cert_populate, NULL/*XXX*/, NULL/*XXX*/),
#endif

	/* misc fields */
	STRING_PROP    (E_CONTACT_HOMEPAGE_URL,  "labeledURI"),
	/* map nickname to displayName */
	STRING_PROP    (E_CONTACT_NICKNAME,    "displayName"),
	E_STRING_PROP  (E_CONTACT_SPOUSE,      "spouseName"), 
	E_STRING_PROP  (E_CONTACT_NOTE,        "note"), 
	E_COMPLEX_PROP (E_CONTACT_ANNIVERSARY, "anniversary", anniversary_populate, anniversary_ber, anniversary_compare), 
	E_COMPLEX_PROP (E_CONTACT_BIRTH_DATE,  "birthDate", birthday_populate, birthday_ber, birthday_compare), 
	E_STRING_PROP  (E_CONTACT_MAILER,      "mailer"), 

	E_STRING_PROP  (E_CONTACT_FILE_AS,     "fileAs"),

	E_COMPLEX_PROP (E_CONTACT_CATEGORY_LIST,  "category", category_populate, category_ber, category_compare),

	STRING_PROP (E_CONTACT_CALENDAR_URI,   "calCalURI"),
	STRING_PROP (E_CONTACT_FREEBUSY_URL,   "calFBURL"),
	STRING_PROP (E_CONTACT_ICS_CALENDAR,   "icsCalendar"),

#undef E_STRING_PROP
#undef STRING_PROP
#undef E_COMPLEX_PROP
#undef COMPLEX_PROP
};

static int num_prop_infos = sizeof(prop_info) / sizeof(prop_info[0]);

#if 0
static void
remove_view (int msgid, LDAPOp *op, EDataBookView *view)
{
	if (op->view == view)
		op->view = NULL;
}

static void
view_destroy(gpointer data, GObject *where_object_was)
{
	EDataBook           *book = (EDataBook *)data;
	EBookBackendLDAP    *bl;
	EIterator         *iter;

	d(printf ("view_destroy (%p)\n", where_object_was));

	bl = E_BOOK_BACKEND_LDAP(e_data_book_get_backend(book));

	iter = e_list_get_iterator (bl->priv->book_views);

	while (e_iterator_is_valid (iter)) {
		EBookBackendLDAPBookView *view = (EBookBackendLDAPBookView*)e_iterator_get (iter);

		if (view->book_view == (EDataBookView*)where_object_was) {
			GNOME_Evolution_Addressbook_Book    corba_book;
			CORBA_Environment ev;

			/* if we have an active search, interrupt it */
			if (view->search_op) {
				ldap_op_finished (view->search_op);
			}
			/* and remove us as the view for any other
                           operations that might be using us to spew
                           status messages to the gui */
			g_static_rec_mutex_lock (&bl->priv->op_hash_mutex);
			g_hash_table_foreach (bl->priv->id_to_op, (GHFunc)remove_view, view->book_view);
			g_static_rec_mutex_unlock (&bl->priv->op_hash_mutex);

			/* free up the view structure */
			g_free (view->search);
			g_free (view);

			/* and remove it from our list */
			e_iterator_delete (iter);

			/* unref the book now */
			corba_book = bonobo_object_corba_objref(BONOBO_OBJECT(book));

			CORBA_exception_init(&ev);

			GNOME_Evolution_Addressbook_Book_unref(corba_book, &ev);
	
			if (ev._major != CORBA_NO_EXCEPTION) {
				g_warning("view_destroy: Exception unreffing "
					  "corba book.\n");
			}

			CORBA_exception_free(&ev);
			break;
		}

		e_iterator_next (iter);
	}

	g_object_unref (iter);

}
#endif

static void
book_view_notify_status (EDataBookView *view, const char *status)
{
	if (!view)
		return;
	e_data_book_view_notify_status_message (view, status);
}

static EDataBookView*
find_book_view (EBookBackendLDAP *bl)
{
	EList *views = e_book_backend_get_book_views (E_BOOK_BACKEND (bl));
	EIterator *iter = e_list_get_iterator (views);
	EDataBookView *rv = NULL;

	if (e_iterator_is_valid (iter)) {
		/* just always use the first book view */
		EDataBookView *v = (EDataBookView*)e_iterator_get(iter);
		if (v)
			rv = v;
	}

	g_object_unref (iter);
	g_object_unref (views);

	return rv;
}

static void
add_to_supported_fields (EBookBackendLDAP *bl, char **attrs, GHashTable *attr_hash)
{
	int i;
	for (i = 0; attrs[i]; i ++) {
		char *query_prop = g_hash_table_lookup (attr_hash, attrs[i]);

		if (query_prop) {
			bl->priv->supported_fields = g_list_append (bl->priv->supported_fields, g_strdup (query_prop));

			/* handle the list attributes here */
			if (!strcmp (query_prop, e_contact_field_name (E_CONTACT_EMAIL))) {
				bl->priv->supported_fields = g_list_append (bl->priv->supported_fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_1)));
				bl->priv->supported_fields = g_list_append (bl->priv->supported_fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_2)));
				bl->priv->supported_fields = g_list_append (bl->priv->supported_fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_3)));
				bl->priv->supported_fields = g_list_append (bl->priv->supported_fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_4)));
			}
			else if (!strcmp (query_prop, e_contact_field_name (E_CONTACT_PHONE_BUSINESS))) {
				bl->priv->supported_fields = g_list_append (bl->priv->supported_fields, g_strdup (e_contact_field_name (E_CONTACT_PHONE_BUSINESS_2)));
			}
			else if (!strcmp (query_prop, e_contact_field_name (E_CONTACT_PHONE_HOME))) {
				bl->priv->supported_fields = g_list_append (bl->priv->supported_fields, g_strdup (e_contact_field_name(E_CONTACT_PHONE_HOME_2)));
			}
			else if (!strcmp (query_prop, e_contact_field_name (E_CONTACT_CATEGORY_LIST) )) {
				bl->priv->supported_fields = g_list_append (bl->priv->supported_fields, g_strdup (e_contact_field_name (E_CONTACT_CATEGORIES)));
			}
			
		}
	}
}

static void
add_oc_attributes_to_supported_fields (EBookBackendLDAP *bl, LDAPObjectClass *oc)
{
	int i;
	GHashTable *attr_hash = g_hash_table_new (g_str_hash, g_str_equal);

	for (i = 0; i < num_prop_infos; i ++)
		g_hash_table_insert (attr_hash, prop_info[i].ldap_attr, (char*)e_contact_field_name (prop_info[i].field_id));

	if (oc->oc_at_oids_must)
		add_to_supported_fields (bl, oc->oc_at_oids_must, attr_hash);

	if (oc->oc_at_oids_may)
		add_to_supported_fields (bl, oc->oc_at_oids_may, attr_hash);

	g_hash_table_destroy (attr_hash);
}

static void
check_schema_support (EBookBackendLDAP *bl)
{
	char *attrs[2];
	LDAPMessage *resp;
	LDAP *ldap = bl->priv->ldap;
	struct timeval timeout;

	if (!ldap)
		return;

	if (!bl->priv->schema_dn)
		return;

	bl->priv->evolutionPersonChecked = TRUE;

	attrs[0] = "objectClasses";
	attrs[1] = NULL;

	timeout.tv_sec = 30;
	timeout.tv_usec = 0;

	if (ldap_search_ext_s (ldap, bl->priv->schema_dn, LDAP_SCOPE_BASE,
			       "(objectClass=subschema)", attrs, 0,
			       NULL, NULL, &timeout, LDAP_NO_LIMIT, &resp) == LDAP_SUCCESS) {
		char **values;

		values = ldap_get_values (ldap, resp, "objectClasses");

		if (values) {
			int i;
			for (i = 0; values[i]; i ++) {
				int j;
				int code;
				const char *err;
				LDAPObjectClass *oc = ldap_str2objectclass (values[i], &code, &err, 0);

				if (!oc)
					continue;

				for (j = 0; oc->oc_names[j]; j++)
					if (!g_ascii_strcasecmp (oc->oc_names[j], EVOLUTIONPERSON)) {
						g_print ("support found on ldap server for objectclass evolutionPerson\n");
						bl->priv->evolutionPersonSupported = TRUE;

						add_oc_attributes_to_supported_fields (bl, oc);
					}
					else if (!g_ascii_strcasecmp (oc->oc_names[j], CALENTRY)) {
						g_print ("support found on ldap server for objectclass calEntry\n");
						bl->priv->calEntrySupported = TRUE;
						add_oc_attributes_to_supported_fields (bl, oc);
					}
					else if (!g_ascii_strcasecmp (oc->oc_names[j], INETORGPERSON)
						 || !g_ascii_strcasecmp (oc->oc_names[j], ORGANIZATIONALPERSON)
						 || !g_ascii_strcasecmp (oc->oc_names[j], PERSON)) {
						add_oc_attributes_to_supported_fields (bl, oc);
					}

				ldap_objectclass_free (oc);
			}

			ldap_value_free (values);
		}
		else {
			/* the reason for this is so that if the user
			   ends up authenticating to the ldap server,
			   we will requery for the subschema values.
			   This makes it a bit more robust in the face
			   of draconian acl's that keep subschema
			   reads from working until the user is
			   authed. */
			if (!e_book_backend_is_writable (E_BOOK_BACKEND (bl))) {
				g_warning ("subschema read returned nothing before successful auth");
				bl->priv->evolutionPersonChecked = FALSE;
			}
			else {
				g_warning ("subschema read returned nothing after successful auth");
			}
		}

		ldap_msgfree (resp);
	}
}

static void
get_ldap_library_info ()
{
	LDAPAPIInfo info;
	LDAP *ldap;

	if (LDAP_SUCCESS != ldap_create (&ldap)) {
		g_warning ("couldn't create LDAP* for getting at the client lib api info");
		return;
	}

	info.ldapai_info_version = LDAP_API_INFO_VERSION;

	if (LDAP_OPT_SUCCESS != ldap_get_option (ldap, LDAP_OPT_API_INFO, &info)) {
		g_warning ("couldn't get ldap api info");
	}
	else {
		int i;
		g_message ("libldap vendor/version: %s %2d.%02d.%02d",
			   info.ldapai_vendor_name,
			   info.ldapai_vendor_version / 10000,
			   (info.ldapai_vendor_version % 10000) / 1000,
			   info.ldapai_vendor_version % 1000);

		g_message ("library extensions present:");
		/* yuck.  we have to free these? */
		for (i = 0; info.ldapai_extensions[i]; i++) {
			char *extension = info.ldapai_extensions[i];
			g_message (extension);
			ldap_memfree (extension);
		}
		ldap_memfree (info.ldapai_extensions);
		ldap_memfree (info.ldapai_vendor_name);
	}

	ldap_unbind_ext_s (ldap, NULL, NULL);
}

static int
query_ldap_root_dse (EBookBackendLDAP *bl)
{
#define MAX_DSE_ATTRS 20
	LDAP *ldap = bl->priv->ldap;
	LDAPMessage *resp;
	int ldap_error = LDAP_OTHER;
	char *attrs[MAX_DSE_ATTRS], **values;
	int i = 0;
	struct timeval timeout;

	if (!ldap)
		return ldap_error;

	attrs[i++] = "supportedControl";
	attrs[i++] = "supportedExtension";
	attrs[i++] = "supportedFeatures";
	attrs[i++] = "supportedSASLMechanisms";
	attrs[i++] = "supportedLDAPVersion";
	attrs[i++] = "subschemaSubentry"; /* OpenLDAP's dn for schema information */
	attrs[i++] = "schemaNamingContext"; /* Active directory's dn for schema information */
	attrs[i] = NULL;

	timeout.tv_sec = 30;
	timeout.tv_usec = 0;

	ldap_error = ldap_search_ext_s (ldap,
					LDAP_ROOT_DSE, LDAP_SCOPE_BASE,
					"(objectclass=*)",
					attrs, 0, NULL, NULL, &timeout, LDAP_NO_LIMIT, &resp);
	if (ldap_error != LDAP_SUCCESS) {
		g_warning ("could not perform query on Root DSE (ldap_error 0x%02x)", ldap_error);
		return ldap_error;
	}

	values = ldap_get_values (ldap, resp, "supportedControl");
	if (values) {
		for (i = 0; values[i]; i++)
			g_message ("supported server control: %s", values[i]);
		ldap_value_free (values);
	}

	values = ldap_get_values (ldap, resp, "supportedExtension");
	if (values) {
		for (i = 0; values[i]; i++) {
			g_message ("supported server extension: %s", values[i]);
			if (!strcmp (values[i], LDAP_EXOP_START_TLS)) {
				g_message ("server reports LDAP_EXOP_START_TLS");
			}
		}
		ldap_value_free (values);
	}

	values = ldap_get_values (ldap, resp, "supportedSASLMechanisms");
	if (values) {
		char *auth_method;
		if (bl->priv->supported_auth_methods) {
			g_list_foreach (bl->priv->supported_auth_methods, (GFunc)g_free, NULL);
			g_list_free (bl->priv->supported_auth_methods);
		}
		bl->priv->supported_auth_methods = NULL;

		auth_method = g_strdup_printf ("ldap/simple-binddn|%s", _("Using Distinguished Name (DN)"));
		bl->priv->supported_auth_methods = g_list_append (bl->priv->supported_auth_methods, auth_method);

		auth_method = g_strdup_printf ("ldap/simple-email|%s", _("Using Email Address"));
		bl->priv->supported_auth_methods = g_list_append (bl->priv->supported_auth_methods, auth_method);

		for (i = 0; values[i]; i++) {
			auth_method = g_strdup_printf ("sasl/%s|%s", values[i], values[i]);
			bl->priv->supported_auth_methods = g_list_append (bl->priv->supported_auth_methods, auth_method);
			g_message ("supported SASL mechanism: %s", values[i]);
		}
		ldap_value_free (values);
	}


	values = ldap_get_values (ldap, resp, "subschemaSubentry");
	if (!values || !values[0]) {
		if (values) ldap_value_free (values);
		values = ldap_get_values (ldap, resp, "schemaNamingContext");
	}
	if (values && values[0]) {
		g_free (bl->priv->schema_dn);
		bl->priv->schema_dn = g_strdup (values[0]);
	}
	else {
		g_warning ("could not determine location of schema information on LDAP server");
	}
	if (values)
		ldap_value_free (values);

	ldap_msgfree (resp);

	return LDAP_SUCCESS;
}

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_ldap_connect (EBookBackendLDAP *bl)
{
	EBookBackendLDAPPrivate *blpriv = bl->priv;
	int protocol_version = LDAP_VERSION3;

	/* close connection first if it's open first */
	if (blpriv->ldap)
		ldap_unbind_ext_s (blpriv->ldap, NULL, NULL);

	blpriv->ldap = ldap_init (blpriv->ldap_host, blpriv->ldap_port);
#if defined (DEBUG) && defined (LDAP_OPT_DEBUG_LEVEL)
	{
		int debug_level = 4;
		ldap_set_option (blpriv->ldap, LDAP_OPT_DEBUG_LEVEL, &debug_level);
	}
#endif

	if (NULL != blpriv->ldap) {
		int ldap_error;

		ldap_error = ldap_set_option (blpriv->ldap, LDAP_OPT_PROTOCOL_VERSION, &protocol_version);
		if (LDAP_OPT_SUCCESS != ldap_error) {
			g_warning ("failed to set protocol version to LDAPv3");
			bl->priv->ldap_v3 = FALSE;
		}
		else
			bl->priv->ldap_v3 = TRUE;

		if (bl->priv->use_tls != E_BOOK_BACKEND_LDAP_TLS_NO) {
			int tls_level;

			if (!bl->priv->ldap_v3 && bl->priv->use_tls == E_BOOK_BACKEND_LDAP_TLS_ALWAYS) {
				g_message ("TLS not available (fatal version), v3 protocol could not be established (ldap_error 0x%02x)", ldap_error);
				ldap_unbind_ext_s (blpriv->ldap, NULL, NULL);
				blpriv->ldap = NULL;
				return GNOME_Evolution_Addressbook_TLSNotAvailable;
			}

			if (bl->priv->ldap_port == LDAPS_PORT && bl->priv->use_tls == E_BOOK_BACKEND_LDAP_TLS_ALWAYS) {
				tls_level = LDAP_OPT_X_TLS_HARD;
				ldap_set_option (blpriv->ldap, LDAP_OPT_X_TLS, &tls_level);
			}
			else if (bl->priv->use_tls) {
				ldap_error = ldap_start_tls_s (blpriv->ldap, NULL, NULL);
				if (LDAP_SUCCESS != ldap_error) {
					if (bl->priv->use_tls == E_BOOK_BACKEND_LDAP_TLS_ALWAYS) {
						g_message ("TLS not available (fatal version), (ldap_error 0x%02x)", ldap_error);
						ldap_unbind_ext_s (blpriv->ldap, NULL, NULL);
						blpriv->ldap = NULL;
						return GNOME_Evolution_Addressbook_TLSNotAvailable;
					}
					else {
						g_message ("TLS not available (ldap_error 0x%02x)", ldap_error);
					}
				}
				else
					g_message ("TLS active");
			}
		}

		/* bind anonymously initially, we'll actually
		   authenticate the user properly later (in
		   authenticate_user) if they've selected
		   authentication */
		ldap_error = ldap_simple_bind_s (blpriv->ldap, NULL, NULL);
		if (ldap_error == LDAP_PROTOCOL_ERROR) {
			g_warning ("failed to bind using v3.  trying v2.");
			/* server doesn't support v3 binds, so let's
			   drop it down to v2 and try again. */
			bl->priv->ldap_v3 = FALSE;
			
			protocol_version = LDAP_VERSION2;
			ldap_set_option (blpriv->ldap, LDAP_OPT_PROTOCOL_VERSION, &protocol_version);

			ldap_error = ldap_simple_bind_s (blpriv->ldap, NULL, NULL);
		}

		if (ldap_error == LDAP_PROTOCOL_ERROR) {
			g_warning ("failed to bind using either v3 or v2 binds.");
			return GNOME_Evolution_Addressbook_OtherError;
		}
		else if (ldap_error == LDAP_SERVER_DOWN) {
			/* we only want this to be fatal if the server is down. */
			g_warning ("failed to bind anonymously while connecting (ldap_error 0x%02x)", ldap_error);
			return GNOME_Evolution_Addressbook_RepositoryOffline;
		}

		ldap_error = query_ldap_root_dse (bl);
		/* query_ldap_root_dse will cause the actual
		   connect(), so any tcpip problems will show up
		   here */

		/* we can't just check for LDAP_SUCCESS here since in
		   older servers (namely openldap1.x servers), there's
		   not a root DSE at all, so the query will fail with
		   LDAP_NO_SUCH_OBJECT, and GWIA's LDAP server (which
		   is v2 based and doesn't have a root dse) seems to
		   fail with LDAP_PARTIAL_RESULTS. */
		if (ldap_error == LDAP_SUCCESS 
		    || ldap_error == LDAP_PARTIAL_RESULTS
		    || LDAP_NAME_ERROR (ldap_error)) {
			blpriv->connected = TRUE;

			/* check to see if evolutionPerson is supported, if we can (me
			   might not be able to if we can't authenticate.  if we
			   can't, try again in auth_user.) */
			if (!bl->priv->evolutionPersonChecked)
				check_schema_support (bl);

			e_book_backend_set_is_loaded (E_BOOK_BACKEND (bl), TRUE);
			return GNOME_Evolution_Addressbook_Success;
		}
		else
			g_warning ("Failed to perform root dse query anonymously, (ldap_error 0x%02x)", ldap_error);
	}

	g_warning ("e_book_backend_ldap_connect failed for "
		   "'ldap://%s:%d/%s'\n",
		   blpriv->ldap_host,
		   blpriv->ldap_port,
		   blpriv->ldap_rootdn ? blpriv->ldap_rootdn : "");
	blpriv->connected = FALSE;
	return GNOME_Evolution_Addressbook_RepositoryOffline;
}

static gboolean
e_book_backend_ldap_reconnect (EBookBackendLDAP *bl, EDataBookView *book_view, int ldap_status)
{
	if (!bl->priv->ldap)
		return FALSE;

	/* we need to reconnect if we were previously connected */
	if (bl->priv->connected && ldap_status == LDAP_SERVER_DOWN) {
		GNOME_Evolution_Addressbook_CallStatus status;
		int ldap_error = LDAP_SUCCESS;

		if (book_view)
			book_view_notify_status (book_view, _("Reconnecting to LDAP server..."));

		status = e_book_backend_ldap_connect (bl);

		if (status != GNOME_Evolution_Addressbook_Success) {
			if (book_view)
				book_view_notify_status (book_view, "");
			return FALSE;
		}

		if (bl->priv->auth_dn)
			ldap_error = ldap_simple_bind_s(bl->priv->ldap,
							bl->priv->auth_dn,
							bl->priv->auth_passwd);
		if (book_view)
			book_view_notify_status (book_view, "");

		return (ldap_error == LDAP_SUCCESS);
	}
	else {
		return FALSE;
	}
}

static void
ldap_op_add (LDAPOp *op, EBookBackend *backend,
	     EDataBook *book, EDataBookView *view,
	     int opid,
	     int msgid,
	     LDAPOpHandler handler, LDAPOpDtor dtor)
{
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);

	op->backend = backend;
	op->book = book;
	op->view = view;
	op->opid = opid;
	op->id = msgid;
	op->handler = handler;
	op->dtor = dtor;

	g_static_rec_mutex_lock (&bl->priv->op_hash_mutex);
	if (g_hash_table_lookup (bl->priv->id_to_op, &op->id)) {
		g_warning ("conflicting ldap msgid's");
	}

	g_hash_table_insert (bl->priv->id_to_op,
			     &op->id, op);

	bl->priv->active_ops ++;

	if (bl->priv->poll_timeout == -1)
		bl->priv->poll_timeout = g_timeout_add (LDAP_POLL_INTERVAL,
							(GSourceFunc) poll_ldap,
							bl);

	g_static_rec_mutex_unlock (&bl->priv->op_hash_mutex);
}

static void
ldap_op_finished (LDAPOp *op)
{
	EBookBackend *backend = op->backend;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);

	g_static_rec_mutex_lock (&bl->priv->op_hash_mutex);
	g_hash_table_remove (bl->priv->id_to_op, &op->id);

	/* should handle errors here */
	if (bl->priv->ldap)
		ldap_abandon (bl->priv->ldap, op->id);

	op->dtor (op);

	bl->priv->active_ops--;

	if (bl->priv->active_ops == 0) {
		if (bl->priv->poll_timeout != -1)
			g_source_remove (bl->priv->poll_timeout);
		bl->priv->poll_timeout = -1;
	}
	g_static_rec_mutex_unlock (&bl->priv->op_hash_mutex);
}

static void
ldap_op_change_id (LDAPOp *op, int msg_id)
{
	EBookBackend *backend = op->backend;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);

	g_static_rec_mutex_lock (&bl->priv->op_hash_mutex);
	g_hash_table_remove (bl->priv->id_to_op, &op->id);

	op->id = msg_id;

	g_hash_table_insert (bl->priv->id_to_op,
			     &op->id, op);
	g_static_rec_mutex_unlock (&bl->priv->op_hash_mutex);
}

static int
ldap_error_to_response (int ldap_error)
{
	if (ldap_error == LDAP_SUCCESS)
		return GNOME_Evolution_Addressbook_Success;
	else if (ldap_error == LDAP_INVALID_DN_SYNTAX)
		return GNOME_Evolution_Addressbook_OtherError;
	else if (LDAP_NAME_ERROR (ldap_error))
		return GNOME_Evolution_Addressbook_ContactNotFound;
	else if (ldap_error == LDAP_INSUFFICIENT_ACCESS)
		return GNOME_Evolution_Addressbook_PermissionDenied;
	else if (ldap_error == LDAP_SERVER_DOWN)
		return GNOME_Evolution_Addressbook_RepositoryOffline;
	else if (ldap_error == LDAP_ALREADY_EXISTS)
		return GNOME_Evolution_Addressbook_ContactIdAlreadyExists;
	else
		return GNOME_Evolution_Addressbook_OtherError;
}


static char *
create_dn_from_contact (EContact *contact, const char *root_dn)
{
	char *cn, *cn_part = NULL;
	char *dn;

	cn = e_contact_get (contact, E_CONTACT_FULL_NAME);
	if (cn) {
		if (strchr (cn, ',')) {
			/* need to escape commas */
			char *new_cn = g_malloc0 (strlen (cn) * 3 + 1);
			int i, j;

			for (i = 0, j = 0; i < strlen (cn); i ++) {
				if (cn[i] == ',') {
					sprintf (new_cn + j, "%%%02X", cn[i]);
					j += 3;
				}
				else {
					new_cn[j++] = cn[i];
				}
			}
			cn_part = g_strdup_printf ("cn=%s", new_cn);
			g_free (new_cn);
		}
		else {
			cn_part = g_strdup_printf ("cn=%s", cn);
		}
	}
	else {
		cn_part = g_strdup ("");
	}

	dn = g_strdup_printf ("%s%s%s", cn_part,
			      (root_dn && strlen(root_dn)) ? "," : "",
			      (root_dn && strlen(root_dn)) ? root_dn: "");

	g_free (cn_part);

	g_print ("generated dn: %s\n", dn);

	return dn;
}

static void
free_mods (GPtrArray *mods)
{
	int i = 0;
	LDAPMod *mod;

	while ((mod = g_ptr_array_index (mods, i++))) {
		int j;
		g_free (mod->mod_type);

		if (mod->mod_op & LDAP_MOD_BVALUES && mod->mod_bvalues) {
			for (j = 0; mod->mod_bvalues[j]; j++) {
				g_free (mod->mod_bvalues[j]->bv_val);
				g_free (mod->mod_bvalues[j]);
			}
		}
		else if (mod->mod_values) {
			for (j = 0; mod->mod_values[j]; j++)
				g_free (mod->mod_values[j]);
		}
		g_free (mod);
	}

	g_ptr_array_free (mods, TRUE);
}

static GPtrArray*
build_mods_from_contacts (EBookBackendLDAP *bl, EContact *current, EContact *new, gboolean *new_dn_needed)
{
	gboolean adding = (current == NULL);
	GPtrArray *result = g_ptr_array_new();
	int i;

	if (new_dn_needed)
		*new_dn_needed = FALSE;

	/* we walk down the list of properties we can deal with (that
	 big table at the top of the file) */

	for (i = 0; i < num_prop_infos; i ++) {
		gboolean include;
		gboolean new_prop_present = FALSE;
		gboolean current_prop_present = FALSE;
		struct berval** new_prop_bers = NULL;
		char *new_prop = NULL;
		char *current_prop = NULL;

		/* XXX if it's an evolutionPerson prop and the ldap
                   server doesn't support that objectclass, skip it. */
		if (prop_info[i].prop_type & PROP_EVOLVE && !bl->priv->evolutionPersonSupported)
			continue;

		/* get the value for the new contact, and compare it to
                   the value in the current contact to see if we should
                   update it -- if adding is TRUE, short circuit the
                   check. */
		if (prop_info[i].prop_type & PROP_TYPE_STRING) {
			new_prop = e_contact_get (new, prop_info[i].field_id);
			new_prop_present = (new_prop != NULL);
		}
		else {
			new_prop_bers = prop_info[i].ber_func ? prop_info[i].ber_func (new) : NULL;
			new_prop_present = (new_prop_bers != NULL);
		}

		/* need to set INCLUDE to true if the field needs to
                   show up in the ldap modify request */
		if (adding) {
			/* if we're creating a new contact, include it if the
                           field is there at all */
			if (prop_info[i].prop_type & PROP_TYPE_STRING)
				include = (new_prop_present && *new_prop); /* empty strings cause problems */
			else
				include = new_prop_present;
		}
		else {
			/* if we're modifying an existing contact,
                           include it if the current field value is
                           different than the new one, if it didn't
                           exist previously, or if it's been
                           removed. */
			if (prop_info[i].prop_type & PROP_TYPE_STRING) {
				current_prop = e_contact_get (current, prop_info[i].field_id);
				current_prop_present = (current_prop != NULL);

				if (new_prop && current_prop)
					include = *new_prop && strcmp (new_prop, current_prop);
				else
					include = (new_prop != current_prop) && (!new_prop || *new_prop); /* empty strings cause problems */
			}
			else {
				int j;
				struct berval **current_prop_bers = prop_info[i].ber_func ? prop_info[i].ber_func (current) : NULL;

				current_prop_present = (current_prop_bers != NULL);

				/* free up the current_prop_bers */
				if (current_prop_bers) {
					for (j = 0; current_prop_bers[j]; j++) {
						g_free (current_prop_bers[j]->bv_val);
						g_free (current_prop_bers[j]);
					}
					g_free (current_prop_bers);
				}

				include = prop_info[i].compare_func ? !prop_info[i].compare_func (new, current) : FALSE;
			}
		}

		if (include) {
			LDAPMod *mod = g_new (LDAPMod, 1);

			/* the included attribute has changed - we
                           need to update the dn if it's one of the
                           attributes we compute the dn from. */
			if (new_dn_needed)
				*new_dn_needed |= prop_info[i].prop_type & PROP_DN;

			if (adding) {
				mod->mod_op = LDAP_MOD_ADD;
			}
			else {
				if (!new_prop_present)
					mod->mod_op = LDAP_MOD_DELETE;
				else if (!current_prop_present)
					mod->mod_op = LDAP_MOD_ADD;
				else
					mod->mod_op = LDAP_MOD_REPLACE;
			}
			
			mod->mod_type = g_strdup (prop_info[i].ldap_attr);

			if (prop_info[i].prop_type & PROP_TYPE_STRING) {
				mod->mod_values = g_new (char*, 2);
				mod->mod_values[0] = new_prop;
				mod->mod_values[1] = NULL;
			}
			else { /* PROP_TYPE_COMPLEX */
				mod->mod_op |= LDAP_MOD_BVALUES;
				mod->mod_bvalues = new_prop_bers;
			}

			g_ptr_array_add (result, mod);
		}
		
	}

	/* NULL terminate the list of modifications */
	g_ptr_array_add (result, NULL);

	return result;
}

static void
add_objectclass_mod (EBookBackendLDAP *bl, GPtrArray *mod_array, GList *existing_objectclasses)
{
#define FIND_INSERT(oc) \
	if (!g_list_find_custom (existing_objectclasses, (oc), (GCompareFunc)g_ascii_strcasecmp)) \
	         g_ptr_array_add (objectclasses, g_strdup ((oc)))
#define INSERT(oc) \
		 g_ptr_array_add (objectclasses, g_strdup ((oc)))

	LDAPMod *objectclass_mod;
	GPtrArray *objectclasses = g_ptr_array_new();

	if (existing_objectclasses) {
		objectclass_mod = g_new (LDAPMod, 1);
		objectclass_mod->mod_op = LDAP_MOD_ADD;
		objectclass_mod->mod_type = g_strdup ("objectClass");

		/* yes, this is a linear search for each of our
                   objectclasses, but really, how many objectclasses
                   are there going to be in any sane ldap entry? */
		FIND_INSERT (TOP);
		FIND_INSERT (PERSON);
		FIND_INSERT (ORGANIZATIONALPERSON);
		FIND_INSERT (INETORGPERSON);
		if (bl->priv->calEntrySupported)
			FIND_INSERT (CALENTRY);
		if (bl->priv->evolutionPersonSupported)
			FIND_INSERT (EVOLUTIONPERSON);

		if (objectclasses->len) {
			g_ptr_array_add (objectclasses, NULL);
			objectclass_mod->mod_values = (char**)objectclasses->pdata;
			g_ptr_array_add (mod_array, objectclass_mod);
			g_ptr_array_free (objectclasses, FALSE);
		}
		else {
			g_ptr_array_free (objectclasses, TRUE);
			g_free (objectclass_mod->mod_type);
			g_free (objectclass_mod);
		}

	}
	else {
		objectclass_mod = g_new (LDAPMod, 1);
		objectclass_mod->mod_op = LDAP_MOD_ADD;
		objectclass_mod->mod_type = g_strdup ("objectClass");

		INSERT(TOP);
		INSERT(PERSON);
		INSERT(ORGANIZATIONALPERSON);
		INSERT(INETORGPERSON);
		if (bl->priv->calEntrySupported)
			INSERT(CALENTRY);
		if (bl->priv->evolutionPersonSupported)
			INSERT(EVOLUTIONPERSON);
		g_ptr_array_add (objectclasses, NULL);
		objectclass_mod->mod_values = (char**)objectclasses->pdata;
		g_ptr_array_add (mod_array, objectclass_mod);
		g_ptr_array_free (objectclasses, FALSE);
	}
}

typedef struct {
	LDAPOp op;
	char *dn;
	EContact *new_contact;
} LDAPCreateOp;

static void
create_contact_handler (LDAPOp *op, LDAPMessage *res)
{
	LDAPCreateOp *create_op = (LDAPCreateOp*)op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (op->backend);
	LDAP *ldap = bl->priv->ldap;
	char *ldap_error_msg;
	int ldap_error;
	int response;

	if (!ldap) {
		e_data_book_respond_create (op->book,
					    op->opid,
					    GNOME_Evolution_Addressbook_OtherError,
					    NULL);
		ldap_op_finished (op);
	}

	if (LDAP_RES_ADD != ldap_msgtype (res)) {
		g_warning ("incorrect msg type %d passed to create_contact_handler", ldap_msgtype (res));
		e_data_book_respond_create (op->book,
					    op->opid,
					    GNOME_Evolution_Addressbook_OtherError,
					    NULL);
		ldap_op_finished (op);
		return;
	}

	ldap_parse_result (ldap, res, &ldap_error,
			   NULL, &ldap_error_msg, NULL, NULL, 0);
	if (ldap_error != LDAP_SUCCESS) {
		g_warning ("create_contact_handler: %02X (%s), additional info: %s",
			   ldap_error,
			   ldap_err2string (ldap_error), ldap_error_msg);
	} else {
		if (bl->priv->cache)
			e_book_backend_cache_add_contact (bl->priv->cache, create_op->new_contact);
	}
	ldap_memfree (ldap_error_msg);

	/* and lastly respond */
	response = ldap_error_to_response (ldap_error);
	e_data_book_respond_create (op->book,
				    op->opid,
				    response,
				    create_op->new_contact);

	ldap_op_finished (op);
}

static void
create_contact_dtor (LDAPOp *op)
{
	LDAPCreateOp *create_op = (LDAPCreateOp*)op;

	g_free (create_op->dn);
	g_object_unref (create_op->new_contact);
	g_free (create_op);
}

static void
e_book_backend_ldap_create_contact (EBookBackend *backend,
				    EDataBook    *book,
				    guint32       opid,
				    const char   *vcard)
{
	LDAPCreateOp *create_op = g_new (LDAPCreateOp, 1);
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);
	EDataBookView *book_view;
	int create_contact_msgid;
	int response;
	int err;
	GPtrArray *mod_array;
	LDAPMod **ldap_mods;
	LDAP *ldap;

	
	switch (bl->priv->mode) {

	case GNOME_Evolution_Addressbook_MODE_LOCAL :
		e_data_book_respond_create(book, opid, GNOME_Evolution_Addressbook_RepositoryOffline, NULL);
		return;
	case GNOME_Evolution_Addressbook_MODE_REMOTE : 

		if (!bl->priv->ldap) {
			e_data_book_respond_create (book, opid, GNOME_Evolution_Addressbook_OtherError, NULL);
			return;
		}

		book_view = find_book_view (bl);

		printf ("vcard = %s\n", vcard);
		
		create_op->new_contact = e_contact_new_from_vcard (vcard);
		
		create_op->dn = create_dn_from_contact (create_op->new_contact, bl->priv->ldap_rootdn);
		e_contact_set (create_op->new_contact, E_CONTACT_UID, create_op->dn);
		
		ldap = bl->priv->ldap;
		
		/* build our mods */
		mod_array = build_mods_from_contacts (bl, NULL, create_op->new_contact, NULL);
		
#if 0
		if (!mod_array) {
			/* there's an illegal field in there.  report
			   UnsupportedAttribute back */
			e_data_book_respond_create (book,
						    GNOME_Evolution_Addressbook_BookListener_UnsupportedField,
						    NULL);
			
			g_free (create_op->dn);
			g_object_unref (create_op->new_contact);
			g_free (create_op);
			return;
		}
#endif
		
		/* remove the NULL at the end */
		g_ptr_array_remove (mod_array, NULL);
		
		/* add our objectclass(es) */
		add_objectclass_mod (bl, mod_array, NULL);
		
		/* then put the NULL back */
		g_ptr_array_add (mod_array, NULL);
		
#ifdef LDAP_DEBUG_ADD
		{
			int i;
			printf ("Sending the following to the server as ADD\n");
			
			for (i = 0; g_ptr_array_index(mod_array, i); i ++) {
				LDAPMod *mod = g_ptr_array_index(mod_array, i);
				if (mod->mod_op & LDAP_MOD_DELETE)
					printf ("del ");
				else if (mod->mod_op & LDAP_MOD_REPLACE)
					printf ("rep ");
				else
					printf ("add ");
				if (mod->mod_op & LDAP_MOD_BVALUES)
					printf ("ber ");
				else
					printf ("    ");
			
				printf (" %s:\n", mod->mod_type);
			
				if (mod->mod_op & LDAP_MOD_BVALUES) {
					int j;
					for (j = 0; mod->mod_bvalues[j] && mod->mod_bvalues[j]->bv_val; j++)
						printf ("\t\t'%s'\n", mod->mod_bvalues[j]->bv_val);
				}
				else {
					int j;
				
					for (j = 0; mod->mod_values[j]; j++)
						printf ("\t\t'%s'\n", mod->mod_values[j]);
				}
			}
		}
#endif
	
		ldap_mods = (LDAPMod**)mod_array->pdata;
	
		do {
			book_view_notify_status (book_view, _("Adding contact to LDAP server..."));
		
			err = ldap_add_ext (ldap, create_op->dn, ldap_mods,
					    NULL, NULL, &create_contact_msgid);
		
		} while (e_book_backend_ldap_reconnect (bl, book_view, err));
	
		/* and clean up */
		free_mods (mod_array);
	
		if (LDAP_SUCCESS != err) {
			response = ldap_error_to_response (err);
			e_data_book_respond_create (create_op->op.book,
						    opid,
						    response,
						    NULL);
			create_contact_dtor ((LDAPOp*)create_op);
			return;
		}
		else {
			g_print ("ldap_add_ext returned %d\n", err);
			ldap_op_add ((LDAPOp*)create_op, backend, book,
				     book_view, opid, create_contact_msgid,
				     create_contact_handler, create_contact_dtor);
		}
	}
}
	

typedef struct {
	LDAPOp op;
	char *id;
} LDAPRemoveOp;

static void
remove_contact_handler (LDAPOp *op, LDAPMessage *res)
{
	LDAPRemoveOp *remove_op = (LDAPRemoveOp*)op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (op->backend);
	char *ldap_error_msg;
	int ldap_error;
	GList *ids = NULL;

	if (!bl->priv->ldap) {
		e_data_book_respond_remove_contacts (op->book, op->opid, GNOME_Evolution_Addressbook_OtherError, NULL);
		ldap_op_finished (op);
		return;
	}

	if (LDAP_RES_DELETE != ldap_msgtype (res)) {
		g_warning ("incorrect msg type %d passed to remove_contact_handler", ldap_msgtype (res));
		e_data_book_respond_remove_contacts (op->book,
						     op->opid,
						     GNOME_Evolution_Addressbook_OtherError,
						     NULL);
		ldap_op_finished (op);
		return;
	}

	ldap_parse_result (bl->priv->ldap, res, &ldap_error,
			   NULL, &ldap_error_msg, NULL, NULL, 0);
	if (ldap_error != LDAP_SUCCESS) {
		g_warning ("remove_contact_handler: %02X (%s), additional info: %s",
			   ldap_error,
			   ldap_err2string (ldap_error), ldap_error_msg);
	} else {
		/* Remove from cache too */
		if (bl->priv->cache)
			e_book_backend_cache_remove_contact (bl->priv->cache, remove_op->id);
	}

	ldap_memfree (ldap_error_msg);

	ids = g_list_append (ids, remove_op->id);
	e_data_book_respond_remove_contacts (remove_op->op.book,
					     op->opid,
					     ldap_error_to_response (ldap_error),
					     ids);
	g_list_free (ids);
}

static void
remove_contact_dtor (LDAPOp *op)
{
	LDAPRemoveOp *remove_op = (LDAPRemoveOp*)op;

	g_free (remove_op->id);
	g_free (remove_op);
}

static void
e_book_backend_ldap_remove_contacts (EBookBackend *backend,
				     EDataBook    *book,
				     guint32       opid,
				     GList        *ids)
{
	LDAPRemoveOp *remove_op = g_new (LDAPRemoveOp, 1);
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);
	EDataBookView *book_view;
	int remove_msgid;
	int ldap_error;

	switch (bl->priv->mode) {

	case GNOME_Evolution_Addressbook_MODE_LOCAL :
		e_data_book_respond_remove_contacts (book, opid, GNOME_Evolution_Addressbook_RepositoryOffline, NULL);
		g_free (remove_op);
		return;
	case GNOME_Evolution_Addressbook_MODE_REMOTE : 
		if (!bl->priv->ldap) {
			e_data_book_respond_remove_contacts (book, opid, GNOME_Evolution_Addressbook_OtherError, NULL);
			g_free (remove_op);
			return;
		}

		book_view = find_book_view (bl);

		/*
		** since we didn't pass "bulk-removes" in our static
		** capabilities, we should only get 1 length lists here, so
		** the id we're deleting is the first and only id in the list.
		*/
		remove_op->id = g_strdup (ids->data);
		
		do {
			book_view_notify_status (book_view, _("Removing contact from LDAP server..."));
			
			ldap_error = ldap_delete_ext (bl->priv->ldap,
						      remove_op->id,
						      NULL, NULL, &remove_msgid);
		} while (e_book_backend_ldap_reconnect (bl, book_view, ldap_error));
		
		if (ldap_error != LDAP_SUCCESS) {
			e_data_book_respond_remove_contacts (remove_op->op.book,
							     opid,
							     ldap_error_to_response (ldap_error),
							     NULL);
			remove_contact_dtor ((LDAPOp*)remove_op);
			return;
		}
		else {
			g_print ("ldap_delete_ext returned %d\n", ldap_error);
			ldap_op_add ((LDAPOp*)remove_op, backend, book,
				     book_view, opid, remove_msgid,
				     remove_contact_handler, remove_contact_dtor);
		}
		break;
	}
	
}
	
/*
** MODIFY
**
** The modification request is actually composed of 2 separate
** requests.  Since we need to get a list of theexisting objectclasses
** used by the ldap server for the entry, and since the UI only sends
** us the current contact, we need to query the ldap server for the
** existing contact.
**
*/

typedef struct {
	LDAPOp op;
	const char *id; /* the id of the contact we're modifying */
	EContact *current_contact;
	EContact *contact;
	GList *existing_objectclasses;
} LDAPModifyOp;

static void
modify_contact_modify_handler (LDAPOp *op, LDAPMessage *res)
{
	LDAPModifyOp *modify_op = (LDAPModifyOp*)op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (op->backend);
	LDAP *ldap = bl->priv->ldap;
	char *ldap_error_msg;
	int ldap_error;

	if (!ldap) {
		e_data_book_respond_modify (op->book,
					    op->opid,
					    GNOME_Evolution_Addressbook_OtherError,
					    NULL);
		ldap_op_finished (op);
		return;
	}

	if (LDAP_RES_MODIFY != ldap_msgtype (res)) {
		g_warning ("incorrect msg type %d passed to modify_contact_handler", ldap_msgtype (res));
		e_data_book_respond_modify (op->book,
					    op->opid,
					    GNOME_Evolution_Addressbook_OtherError,
					    NULL);
		ldap_op_finished (op);
		return;
	}

	ldap_parse_result (ldap, res, &ldap_error,
			   NULL, &ldap_error_msg, NULL, NULL, 0);
	if (ldap_error != LDAP_SUCCESS) {
		g_warning ("modify_contact_handler: %02X (%s), additional info: %s",
			   ldap_error,
			   ldap_err2string (ldap_error), ldap_error_msg);
	} else {
		if (bl->priv->cache)
			e_book_backend_cache_add_contact (bl->priv->cache, modify_op->contact);
	}
	ldap_memfree (ldap_error_msg);

	/* and lastly respond */
	e_data_book_respond_modify (op->book,
				    op->opid,
				    ldap_error_to_response (ldap_error),
				    modify_op->contact);
	ldap_op_finished (op);
}

static void
modify_contact_search_handler (LDAPOp *op, LDAPMessage *res)
{
	LDAPModifyOp *modify_op = (LDAPModifyOp*)op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (op->backend);
	LDAP *ldap = bl->priv->ldap;
	int msg_type;

	if (!ldap) {
		e_data_book_respond_modify (op->book, op->opid,
					    GNOME_Evolution_Addressbook_OtherError, NULL);
		ldap_op_finished (op);
		return;
	}

	/* if it's successful, we should get called with a
	   RES_SEARCH_ENTRY and a RES_SEARCH_RESULT.  if it's
	   unsuccessful, we should only see a RES_SEARCH_RESULT */

	msg_type = ldap_msgtype (res);
	if (msg_type == LDAP_RES_SEARCH_ENTRY) {
		LDAPMessage *e = ldap_first_entry(ldap, res);

		if (!e) {
			g_warning ("uh, this shouldn't happen");
			e_data_book_respond_modify (op->book,
						    op->opid,
						    GNOME_Evolution_Addressbook_OtherError,
						    NULL);
			ldap_op_finished (op);
			return;
		}

		modify_op->current_contact = build_contact_from_entry (ldap, e,
								       &modify_op->existing_objectclasses);
	}
	else if (msg_type == LDAP_RES_SEARCH_RESULT) {
		char *ldap_error_msg;
		int ldap_error;
		LDAPMod **ldap_mods;
		GPtrArray *mod_array;
		gboolean differences;
		gboolean need_new_dn;
		int modify_contact_msgid;

		/* grab the result code, and set up the actual modify
                   if it was successful */
		ldap_parse_result (bl->priv->ldap, res, &ldap_error,
				   NULL, &ldap_error_msg, NULL, NULL, 0);
		if (ldap_error != LDAP_SUCCESS) {
			g_warning ("modify_contact_search_handler: %02X (%s), additional info: %s",
				   ldap_error,
				   ldap_err2string (ldap_error), ldap_error_msg);
		}
		ldap_memfree (ldap_error_msg);

		if (ldap_error != LDAP_SUCCESS) {
			/* more here i'm sure */
			e_data_book_respond_modify (op->book,
						    op->opid,
						    ldap_error_to_response (ldap_error),
						    NULL);
			ldap_op_finished (op);
			return;
		}

		/* build our mods */
		mod_array = build_mods_from_contacts (bl, modify_op->current_contact, modify_op->contact, &need_new_dn);
		differences = mod_array->len > 0;

		if (differences) {
			/* remove the NULL at the end */
			g_ptr_array_remove (mod_array, NULL);

			/* add our objectclass(es), making sure
			   evolutionPerson is there if it's supported */
			add_objectclass_mod (bl, mod_array, modify_op->existing_objectclasses);

			/* then put the NULL back */
			g_ptr_array_add (mod_array, NULL);

			ldap_mods = (LDAPMod**)mod_array->pdata;

			/* actually perform the ldap modify */
			ldap_error = ldap_modify_ext (ldap, modify_op->id, ldap_mods,
						      NULL, NULL, &modify_contact_msgid);

			if (ldap_error == LDAP_SUCCESS) {
				op->handler = modify_contact_modify_handler;
				ldap_op_change_id ((LDAPOp*)modify_op,
						   modify_contact_msgid);
			}
			else {
				g_warning ("ldap_modify_ext returned %d\n", ldap_error);
				e_data_book_respond_modify (op->book,
							    op->opid,
							    ldap_error_to_response (ldap_error),
							    NULL);
				ldap_op_finished (op);
				return;
			}
		}

		/* and clean up */
		free_mods (mod_array);
	}
	else {
		g_warning ("unhandled result type %d returned", msg_type);
		e_data_book_respond_modify (op->book,
					    op->opid,
					    GNOME_Evolution_Addressbook_OtherError,
					    NULL);
		ldap_op_finished (op);
	}
}

static void
modify_contact_dtor (LDAPOp *op)
{
	LDAPModifyOp *modify_op = (LDAPModifyOp*)op;

	g_list_foreach (modify_op->existing_objectclasses, (GFunc)g_free, NULL);
	g_list_free (modify_op->existing_objectclasses);
	if (modify_op->current_contact)
		g_object_unref (modify_op->current_contact);
	if (modify_op->contact)
		g_object_unref (modify_op->contact);
	g_free (modify_op);
}

static void
e_book_backend_ldap_modify_contact (EBookBackend *backend,
				    EDataBook    *book,
				    guint32       opid,
				    const char   *vcard)
{
	LDAPModifyOp *modify_op = g_new0 (LDAPModifyOp, 1);
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);
	int ldap_error;
	LDAP *ldap;
	int modify_contact_msgid;
	EDataBookView *book_view;


	switch (bl->priv->mode) {

	case GNOME_Evolution_Addressbook_MODE_LOCAL :
		e_data_book_respond_modify(book, opid, GNOME_Evolution_Addressbook_RepositoryOffline, NULL);
		return;
	case GNOME_Evolution_Addressbook_MODE_REMOTE :
		if (!bl->priv->ldap) {
			e_data_book_respond_modify (book, opid, GNOME_Evolution_Addressbook_OtherError, NULL);
			g_free (modify_op);
			return;
		}

		book_view = find_book_view (bl);

		modify_op->contact = e_contact_new_from_vcard (vcard);
		modify_op->id = e_contact_get_const (modify_op->contact, E_CONTACT_UID);

		ldap = bl->priv->ldap;

		do {
			book_view_notify_status (book_view, _("Modifying contact from LDAP server..."));

			ldap_error = ldap_search_ext (ldap, modify_op->id,
						      LDAP_SCOPE_BASE,
						      "(objectclass=*)",
						      NULL, 0, NULL, NULL,
						      NULL, /* XXX timeout */
						      1, &modify_contact_msgid);

		} while (e_book_backend_ldap_reconnect (bl, book_view, ldap_error));

		if (ldap_error == LDAP_SUCCESS) {
			ldap_op_add ((LDAPOp*)modify_op, backend, book,
				     book_view, opid, modify_contact_msgid,
				     modify_contact_search_handler, modify_contact_dtor);
		}
		else {
			g_warning ("ldap_search_ext returned %d\n", ldap_error);
			e_data_book_respond_modify (book,
						    opid,
						    GNOME_Evolution_Addressbook_OtherError,
						    NULL);
			modify_contact_dtor ((LDAPOp*)modify_op);
		}
	}
}


typedef struct {
	LDAPOp op;
} LDAPGetContactOp;

static void
get_contact_handler (LDAPOp *op, LDAPMessage *res)
{
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (op->backend);
	int msg_type;

	if (!bl->priv->ldap) {
		e_data_book_respond_get_contact (op->book, op->opid, GNOME_Evolution_Addressbook_OtherError, "");
		ldap_op_finished (op);
		return;
	}

	/* the msg_type will be either SEARCH_ENTRY (if we're
	   successful) or SEARCH_RESULT (if we're not), so we finish
	   the op after either */
	msg_type = ldap_msgtype (res);
	if (msg_type == LDAP_RES_SEARCH_ENTRY) {
		LDAPMessage *e = ldap_first_entry(bl->priv->ldap, res);
		EContact *contact;
		char *vcard;

		if (!e) {
			g_warning ("uh, this shouldn't happen");
			e_data_book_respond_get_contact (op->book,
							 op->opid,
							 GNOME_Evolution_Addressbook_OtherError,
							 "");
			ldap_op_finished (op);
			return;
		}

		contact = build_contact_from_entry (bl->priv->ldap, e, NULL);
		vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
		e_data_book_respond_get_contact (op->book,
						 op->opid,
						 GNOME_Evolution_Addressbook_Success,
						 vcard);
		g_free (vcard);
		g_object_unref (contact);
		ldap_op_finished (op);
	}
	else if (msg_type == LDAP_RES_SEARCH_RESULT) {
		char *ldap_error_msg;
		int ldap_error;
		ldap_parse_result (bl->priv->ldap, res, &ldap_error,
				   NULL, &ldap_error_msg, NULL, NULL, 0);
		if (ldap_error != LDAP_SUCCESS) {
			g_warning ("get_contact_handler: %02X (%s), additional info: %s",
				   ldap_error,
				   ldap_err2string (ldap_error), ldap_error_msg);
		}
		ldap_memfree (ldap_error_msg);

		e_data_book_respond_get_contact (op->book, 
						 op->opid,
						 ldap_error_to_response (ldap_error),
						 "");
		ldap_op_finished (op);
	}
	else {
		g_warning ("unhandled result type %d returned", msg_type);
		e_data_book_respond_get_contact (op->book,
						 op->opid,
						 GNOME_Evolution_Addressbook_OtherError,
						 "");
		ldap_op_finished (op);
	}

}

static void
get_contact_dtor (LDAPOp *op)
{
	LDAPGetContactOp *get_contact_op = (LDAPGetContactOp*)op;

	g_free (get_contact_op);
}

static void
e_book_backend_ldap_get_contact (EBookBackend *backend,
				 EDataBook    *book,
				 guint32       opid,
				 const char   *id)
{
	LDAPGetContactOp *get_contact_op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);
	LDAP *ldap = bl->priv->ldap;
	int get_contact_msgid;
	EDataBookView *book_view;
	int ldap_error;

	switch (bl->priv->mode) {

	case GNOME_Evolution_Addressbook_MODE_LOCAL :
		if (bl->priv->marked_for_offline && bl->priv->cache) {
			EContact *contact = e_book_backend_cache_get_contact (bl->priv->cache, id);
			gchar *vcard_str;

			if (!contact) {
				e_data_book_respond_get_contact (book, opid, GNOME_Evolution_Addressbook_OtherError, "");
				return;
			}

			vcard_str = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

			e_data_book_respond_get_contact (book,
							 opid,
							 GNOME_Evolution_Addressbook_Success,
							 vcard_str);
			g_free (vcard_str);
			g_object_unref (contact);
			return;
		}

		e_data_book_respond_get_contact(book, opid, GNOME_Evolution_Addressbook_RepositoryOffline, "");
		return;

	case GNOME_Evolution_Addressbook_MODE_REMOTE : 	
		if (!ldap) {
			e_data_book_respond_get_contact (book, opid, GNOME_Evolution_Addressbook_OtherError, "");
			return;
		}

		get_contact_op = g_new0 (LDAPGetContactOp, 1);
		book_view = find_book_view (bl);

		do {	
			ldap_error = ldap_search_ext (ldap, id,
						      LDAP_SCOPE_BASE,
						      "(objectclass=*)",
						      NULL, 0, NULL, NULL,
						      NULL, /* XXX timeout */
						      1, &get_contact_msgid);
		} while (e_book_backend_ldap_reconnect (bl, book_view, ldap_error));

		if (ldap_error == LDAP_SUCCESS) {
			ldap_op_add ((LDAPOp*)get_contact_op, backend, book,
				     book_view, opid, get_contact_msgid,
				     get_contact_handler, get_contact_dtor);
		}
		else {
			e_data_book_respond_get_contact (book,
							 opid,
							 ldap_error_to_response (ldap_error),
							 "");
			get_contact_dtor ((LDAPOp*)get_contact_op);
		}
	}
}


typedef struct {
	LDAPOp op;
	GList *contacts;
} LDAPGetContactListOp;

static void
contact_list_handler (LDAPOp *op, LDAPMessage *res)
{
	LDAPGetContactListOp *contact_list_op = (LDAPGetContactListOp*)op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (op->backend);
	LDAP *ldap = bl->priv->ldap;
	LDAPMessage *e;
	int msg_type;

	if (!ldap) {
		e_data_book_respond_get_contact_list (op->book, op->opid, GNOME_Evolution_Addressbook_OtherError, NULL);
		ldap_op_finished (op);
		return;
	}

	msg_type = ldap_msgtype (res);
	if (msg_type == LDAP_RES_SEARCH_ENTRY) {
		e = ldap_first_entry(ldap, res);

		while (NULL != e) {
			EContact *contact = build_contact_from_entry (ldap, e, NULL);
			char *vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

			printf ("vcard = %s\n", vcard);
 
			contact_list_op->contacts = g_list_append (contact_list_op->contacts,
								   vcard);

			g_object_unref (contact);
			e = ldap_next_entry(ldap, e);
		}
	}
	else if (msg_type == LDAP_RES_SEARCH_RESULT) {
		char *ldap_error_msg;
		int ldap_error;

		ldap_parse_result (ldap, res, &ldap_error,
				   NULL, &ldap_error_msg, NULL, NULL, 0);
		if (ldap_error != LDAP_SUCCESS) {
			g_warning ("contact_list_handler: %02X (%s), additional info: %s",
				   ldap_error,
				   ldap_err2string (ldap_error), ldap_error_msg);
		}
		ldap_memfree (ldap_error_msg);

		g_warning ("search returned %d\n", ldap_error);

		if (ldap_error == LDAP_TIMELIMIT_EXCEEDED)
			e_data_book_respond_get_contact_list (op->book,
							      op->opid,
							      GNOME_Evolution_Addressbook_SearchTimeLimitExceeded,
							      contact_list_op->contacts);
		else if (ldap_error == LDAP_SIZELIMIT_EXCEEDED)
			e_data_book_respond_get_contact_list (op->book,
							      op->opid,
							      GNOME_Evolution_Addressbook_SearchSizeLimitExceeded,
							      contact_list_op->contacts);
		else if (ldap_error == LDAP_SUCCESS)
			e_data_book_respond_get_contact_list (op->book,
							      op->opid,
							      GNOME_Evolution_Addressbook_Success,
							      contact_list_op->contacts);
		else
			e_data_book_respond_get_contact_list (op->book,
							      op->opid,
							      GNOME_Evolution_Addressbook_OtherError,
							      contact_list_op->contacts);

		ldap_op_finished (op);
	}
	else {
		g_warning ("unhandled search result type %d returned", msg_type);
		e_data_book_respond_get_contact_list (op->book,
						      op->opid,
						      GNOME_Evolution_Addressbook_OtherError,
						      NULL);
		ldap_op_finished (op);
	}

}

static void
contact_list_dtor (LDAPOp *op)
{
	LDAPGetContactListOp *contact_list_op = (LDAPGetContactListOp*)op;

	g_free (contact_list_op);
}


static void
e_book_backend_ldap_get_contact_list (EBookBackend *backend,
				      EDataBook    *book,
				      guint32       opid,
				      const char   *query)
{
	LDAPGetContactListOp *contact_list_op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);
	LDAP *ldap = bl->priv->ldap;
	int contact_list_msgid;
	EDataBookView *book_view;
	int ldap_error;
	char *ldap_query;

	switch (bl->priv->mode) {
		
	case GNOME_Evolution_Addressbook_MODE_LOCAL :
		if (bl->priv->marked_for_offline && bl->priv->cache) {
			GList *contacts;
			GList *vcard_strings = NULL;
			GList *l;

			contacts = e_book_backend_cache_get_contacts (bl->priv->cache, query);

			for (l = contacts; l; l = g_list_next (l)) {
				EContact *contact = l->data;
				vcard_strings = g_list_prepend (vcard_strings, e_vcard_to_string (E_VCARD (contact),
								EVC_FORMAT_VCARD_30));
				g_object_unref (contact);
			}

			g_list_free (contacts);
			e_data_book_respond_get_contact_list (book, opid, GNOME_Evolution_Addressbook_Success, vcard_strings);
			return;
		}
		
		e_data_book_respond_get_contact_list (book, opid, GNOME_Evolution_Addressbook_RepositoryOffline,
						      NULL);
		return;
		
	case GNOME_Evolution_Addressbook_MODE_REMOTE:
		if (!ldap) {
			e_data_book_respond_get_contact_list (book, opid, GNOME_Evolution_Addressbook_OtherError, NULL);
			return;
		}

		contact_list_op = g_new0 (LDAPGetContactListOp, 1);
		book_view = find_book_view (bl);

		ldap_query = e_book_backend_ldap_build_query (bl, query);

		printf ("getting contact list with filter: %s\n", ldap_query);

		do {	
			ldap_error = ldap_search_ext (ldap,
						      bl->priv->ldap_rootdn,
						      bl->priv->ldap_scope,
						      ldap_query,
						      NULL, 0, NULL, NULL,
						      NULL, /* XXX timeout */
						      LDAP_NO_LIMIT, &contact_list_msgid);
		} while (e_book_backend_ldap_reconnect (bl, book_view, ldap_error));

		g_free (ldap_query);

		if (ldap_error == LDAP_SUCCESS) {
			ldap_op_add ((LDAPOp*)contact_list_op, backend, book,
				     book_view, opid, contact_list_msgid,
				     contact_list_handler, contact_list_dtor);
		}
		else {
			e_data_book_respond_get_contact_list (book,
							      opid,
							      ldap_error_to_response (ldap_error),
							      NULL);
			contact_list_dtor ((LDAPOp*)contact_list_op);
		}
	}
}



static EContactField email_ids[4] = {
	E_CONTACT_EMAIL_1,
	E_CONTACT_EMAIL_2,
	E_CONTACT_EMAIL_3,
	E_CONTACT_EMAIL_4
};

/* List property functions */
static void
email_populate(EContact *contact, char **values)
{
	int i;
	for (i = 0; values[i] && i < 4; i ++)
		e_contact_set (contact, email_ids[i], values[i]);
}

static struct berval**
email_ber(EContact *contact)
{
	struct berval** result;
	const char *emails[4];
	int i, j, num = 0;

	for (i = 0; i < 4; i ++) {
		emails[i] = e_contact_get (contact, email_ids[i]);
		if (emails[i])
			num++;
	}

	if (num == 0)
		return NULL;

	result = g_new (struct berval*, num + 1);

	for (i = 0; i < num; i ++)
		result[i] = g_new (struct berval, 1);

	j = 0;
	for (i = 0; i < 4; i ++) {
		if (emails[i]) {
			result[j]->bv_val = g_strdup (emails[i]);
			result[j++]->bv_len = strlen (emails[i]);
		}
	}

	result[num] = NULL;

	return result;
}

static gboolean
email_compare (EContact *contact1, EContact *contact2)
{
	const char *email1, *email2;
	int i;

	for (i = 0; i < 4; i ++) {
		gboolean equal;
		email1 = e_contact_get_const (contact1, email_ids[i]);
		email2 = e_contact_get_const (contact2, email_ids[i]);

		if (email1 && email2)
			equal = !strcmp (email1, email2);
		else
			equal = (!!email1 == !!email2);

		if (!equal)
			return equal;
	}

	return TRUE;
}

static void
homephone_populate(EContact *contact, char **values)
{
	if (values[0]) {
		e_contact_set (contact, E_CONTACT_PHONE_HOME, values[0]);
		if (values[1])
			e_contact_set (contact, E_CONTACT_PHONE_HOME_2, values[1]);
	}
}

static struct berval**
homephone_ber(EContact *contact)
{
	struct berval** result;
	const char *homephones[3];
	int i, j, num;

	num = 0;
	if ((homephones[0] = e_contact_get (contact, E_CONTACT_PHONE_HOME)))
		num++;
	if ((homephones[1] = e_contact_get (contact, E_CONTACT_PHONE_HOME_2)))
		num++;

	if (num == 0)
		return NULL;

	result = g_new (struct berval*, num + 1);

	for (i = 0; i < num; i ++)
		result[i] = g_new (struct berval, 1);

	j = 0;
	for (i = 0; i < 2; i ++) {
		if (homephones[i]) {
			result[j]->bv_val = g_strdup (homephones[i]);
			result[j++]->bv_len = strlen (homephones[i]);
		}
	}

	result[num] = NULL;

	return result;
}

static gboolean
homephone_compare (EContact *contact1, EContact *contact2)
{
	int phone_ids[2] = { E_CONTACT_PHONE_HOME, E_CONTACT_PHONE_HOME_2 };
	const char *phone1, *phone2;
	int i;

	for (i = 0; i < 2; i ++) {
		gboolean equal;
		phone1 = e_contact_get (contact1, phone_ids[i]);
		phone2 = e_contact_get (contact2, phone_ids[i]);

		if (phone1 && phone2)
			equal = !strcmp (phone1, phone2);
		else
			equal = (!!phone1 == !!phone2);

		if (!equal)
			return equal;
	}

	return TRUE;
}

static void
business_populate(EContact *contact, char **values)
{
	if (values[0]) {
		e_contact_set (contact, E_CONTACT_PHONE_BUSINESS, values[0]);
		if (values[1])
			e_contact_set (contact, E_CONTACT_PHONE_BUSINESS_2, values[1]);
	}
}

static struct berval**
business_ber(EContact *contact)
{
	struct berval** result;
	const char *business_phones[3];
	int i, j, num;

	num = 0;
	if ((business_phones[0] = e_contact_get (contact, E_CONTACT_PHONE_BUSINESS)))
		num++;
	if ((business_phones[1] = e_contact_get (contact, E_CONTACT_PHONE_BUSINESS_2)))
		num++;

	if (num == 0)
		return NULL;

	result = g_new (struct berval*, num + 1);

	for (i = 0; i < num; i ++)
		result[i] = g_new (struct berval, 1);

	j = 0;
	for (i = 0; i < 2; i ++) {
		if (business_phones[i]) {
			result[j]->bv_val = g_strdup (business_phones[i]);
			result[j++]->bv_len = strlen (business_phones[i]);
		}
	}

	result[num] = NULL;

	return result;
}

static gboolean
business_compare (EContact *contact1, EContact *contact2)
{
	int phone_ids[2] = { E_CONTACT_PHONE_BUSINESS, E_CONTACT_PHONE_BUSINESS_2 };
	const char *phone1, *phone2;
	int i;

	for (i = 0; i < 2; i ++) {
		gboolean equal;
		phone1 = e_contact_get (contact1, phone_ids[i]);
		phone2 = e_contact_get (contact2, phone_ids[i]);

		if (phone1 && phone2)
			equal = !strcmp (phone1, phone2);
		else
			equal = (!!phone1 == !!phone2);

		if (!equal)
			return equal;
	}

	return TRUE;
}

static void
anniversary_populate (EContact *contact, char **values)
{
	if (values[0]) {
		EContactDate *dt = e_contact_date_from_string (values[0]);
		e_contact_set (contact, E_CONTACT_ANNIVERSARY, dt);
		e_contact_date_free (dt);
	}
}

static struct berval**
anniversary_ber (EContact *contact)
{
	EContactDate *dt;
	struct berval** result = NULL;

	dt = e_contact_get (contact, E_CONTACT_ANNIVERSARY);

	if (dt) {
		char *anniversary;

		anniversary = e_contact_date_to_string (dt);

		result = g_new (struct berval*, 2);
		result[0] = g_new (struct berval, 1);
		result[0]->bv_val = anniversary;
		result[0]->bv_len = strlen (anniversary);

		result[1] = NULL;

		e_contact_date_free (dt);
	}

	return result;
}

static gboolean
anniversary_compare (EContact *contact1, EContact *contact2)
{
	EContactDate *dt1, *dt2;
	gboolean equal;

	dt1 = e_contact_get (contact1, E_CONTACT_ANNIVERSARY);
	dt2 = e_contact_get (contact2, E_CONTACT_ANNIVERSARY);

	equal = e_contact_date_equal (dt1, dt2);

	e_contact_date_free (dt1);
	e_contact_date_free (dt2);

	return equal;
}

static void
birthday_populate (EContact *contact, char **values)
{
	if (values[0]) {
		EContactDate *dt = e_contact_date_from_string (values[0]);
		e_contact_set (contact, E_CONTACT_BIRTH_DATE, dt);
		e_contact_date_free (dt);
	}
}

static struct berval**
birthday_ber (EContact *contact)
{
	EContactDate *dt;
	struct berval** result = NULL;

	dt = e_contact_get (contact, E_CONTACT_BIRTH_DATE);
	if (dt) {
		char *birthday;

		birthday = e_contact_date_to_string (dt);

		result = g_new (struct berval*, 2);
		result[0] = g_new (struct berval, 1);
		result[0]->bv_val = birthday;
		result[0]->bv_len = strlen (birthday);

		result[1] = NULL;

		e_contact_date_free (dt);
	}

	return result;
}

static gboolean
birthday_compare (EContact *contact1, EContact *contact2)
{
	EContactDate *dt1, *dt2;
	gboolean equal;

	dt1 = e_contact_get (contact1, E_CONTACT_BIRTH_DATE);
	dt2 = e_contact_get (contact2, E_CONTACT_BIRTH_DATE);

	equal = e_contact_date_equal (dt1, dt2);

	e_contact_date_free (dt1);
	e_contact_date_free (dt2);

	return equal;
}

static void
category_populate (EContact *contact, char **values)
{
	int i;
	GList *categories = NULL;

	for (i = 0; values[i]; i++)
		categories = g_list_append (categories, g_strdup (values[i]));

	e_contact_set (contact, E_CONTACT_CATEGORY_LIST, categories);

	g_list_foreach (categories, (GFunc)g_free, NULL);
	g_list_free (categories);
}

static struct berval**
category_ber (EContact *contact)
{
	struct berval** result = NULL;
	GList *categories;
	const char *category_string;

	category_string = e_contact_get (contact, E_CONTACT_CATEGORIES);
	if (!category_string || !*category_string)
		return NULL;

	categories = e_contact_get (contact, E_CONTACT_CATEGORY_LIST);

	if (g_list_length (categories) != 0) {
		int i;
		GList *iter;
		result = g_new0 (struct berval*, g_list_length (categories) + 1);

		for (iter = categories, i = 0; iter; iter = iter->next) {
			char *category = iter->data;

			if (category && *category) {
				result[i] = g_new (struct berval, 1);
				result[i]->bv_val = g_strdup (category);
				result[i]->bv_len = strlen (category);
				i++;
			}
		}
	}

	g_list_foreach (categories, (GFunc)g_free, NULL);
	g_list_free (categories);
	return result;
}

static gboolean
category_compare (EContact *contact1, EContact *contact2)
{
	const char *categories1, *categories2;
	gboolean equal;

	categories1 = e_contact_get_const (contact1, E_CONTACT_CATEGORIES);
	categories2 = e_contact_get_const (contact2, E_CONTACT_CATEGORIES);

	if (categories1 && categories2)
		equal = !strcmp (categories1, categories2);
	else
		equal = (categories1 == categories2);

	return equal;
}

static void
address_populate(EContact * card, char **values, EContactField field)
{
	if (values[0]) {
		char *temp = g_strdup(values[0]);
		char *i;
		for (i = temp; *i != '\0'; i++) {
			if (*i == '$') {
				*i = '\n';
			}
		}
		e_contact_set(card, field, temp);
		g_free(temp);
	}
}

static void
home_address_populate(EContact * card, char **values)
{
	address_populate(card, values, E_CONTACT_ADDRESS_LABEL_HOME);
}

static void
work_address_populate(EContact * card, char **values)
{
	address_populate(card, values, E_CONTACT_ADDRESS_LABEL_WORK);
}

static void
other_address_populate(EContact * card, char **values)
{
	address_populate(card, values, E_CONTACT_ADDRESS_LABEL_OTHER);
}

static struct berval **
address_ber(EContact * card, EContactField field)
{
	struct berval **result = NULL;
	char *address, *i;

	address = e_contact_get(card, field);
	if (address) {
		for (i = address; *i != '\0'; i++) {
			if (*i == '\n') {
				*i = '$';
			}
		}

		result = g_new(struct berval *, 2);
		result[0] = g_new(struct berval, 1);
		result[0]->bv_val = address;
		result[0]->bv_len = strlen(address);

		result[1] = NULL;
	}
	return result;
}

static struct berval **
home_address_ber(EContact * card)
{
	return address_ber(card, E_CONTACT_ADDRESS_LABEL_HOME);
}

static struct berval **
work_address_ber(EContact * card)
{
	return address_ber(card, E_CONTACT_ADDRESS_LABEL_WORK);
}

static struct berval **
other_address_ber(EContact * card)
{
	return address_ber(card, E_CONTACT_ADDRESS_LABEL_OTHER);
}

static gboolean
address_compare(EContact * ecard1, EContact * ecard2, EContactField field)
{
	const char *address1, *address2;

	gboolean equal;
	address1 = e_contact_get_const(ecard1, field);
	address2 = e_contact_get_const(ecard2, field);

	if (address1 && address2)
		equal = !strcmp(address1, address2);
	else
		equal = (!!address1 == !!address2);

	return equal;
}

static gboolean
home_address_compare(EContact * ecard1, EContact * ecard2)
{
	return address_compare(ecard1, ecard2, E_CONTACT_ADDRESS_LABEL_HOME);
}

static gboolean
work_address_compare(EContact * ecard1, EContact * ecard2)
{
	return address_compare(ecard1, ecard2, E_CONTACT_ADDRESS_LABEL_WORK);
}

static gboolean
other_address_compare(EContact * ecard1, EContact * ecard2)
{
	return address_compare(ecard1, ecard2, E_CONTACT_ADDRESS_LABEL_OTHER);
}

static void
photo_populate (EContact *contact, struct berval **ber_values)
{
        if (ber_values && ber_values[0]) {
                EContactPhoto photo;
                photo.data = ber_values[0]->bv_val;
                photo.length = ber_values[0]->bv_len;

                e_contact_set (contact, E_CONTACT_PHOTO, &photo);
        }
}

static struct berval **
photo_ber (EContact *contact)
{
	struct berval **result = NULL;
	EContactPhoto *photo;

	photo = e_contact_get(contact, E_CONTACT_PHOTO);
	if (photo) {

		result = g_new(struct berval *, 2);
		result[0] = g_new(struct berval, 1);
		result[0]->bv_len = photo->length;
		result[0]->bv_val = g_malloc (photo->length);
		memcpy (result[0]->bv_val, photo->data, photo->length);
		e_contact_photo_free (photo);

		result[1] = NULL;
	}

	return result;
}

static gboolean
photo_compare(EContact * ecard1, EContact * ecard2)
{
	EContactPhoto *photo1, *photo2;
	gboolean equal;

	photo1 = e_contact_get(ecard1, E_CONTACT_PHOTO);
	photo2 = e_contact_get(ecard2, E_CONTACT_PHOTO);


	if (photo1 && photo2) {
		equal = ((photo1->length == photo2->length)
			 && !memcmp (photo1->data, photo2->data, photo1->length));
	}
	else {
		equal = (!!photo1 == !!photo2);
	}

	if (photo1)
		e_contact_photo_free (photo1);
	if (photo2)
		e_contact_photo_free (photo2);

	return equal;
}

static void
cert_populate (EContact *contact, struct berval **ber_values)
{
        if (ber_values && ber_values[0]) {
                EContactCert cert;
                cert.data = ber_values[0]->bv_val;
                cert.length = ber_values[0]->bv_len;

                e_contact_set (contact, E_CONTACT_X509_CERT, &cert);
        }
}

typedef struct {
	GList *list;
	EBookBackendLDAP *bl;
} EBookBackendLDAPSExpData;

#define IS_RFC2254_CHAR(c) ((c) == '*' || (c) =='\\' || (c) == '(' || (c) == ')' || (c) == '\0')
static char *
rfc2254_escape(char *str)
{
	int i;
	int len = strlen(str);
	int newlen = 0;

	for (i = 0; i < len; i ++) {
		if (IS_RFC2254_CHAR(str[i]))
			newlen += 3;
		else
			newlen ++;
	}

	if (len == newlen) {
		return g_strdup (str);
	}
	else {
		char *newstr = g_malloc0 (newlen + 1);
		int j = 0;
		for (i = 0; i < len; i ++) {
			if (IS_RFC2254_CHAR(str[i])) {
				sprintf (newstr + j, "\\%02x", str[i]);
				j+= 3;
			}
			else {
				newstr[j++] = str[i];
			}
		}
		return newstr;
	}
}

static ESExpResult *
func_and(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	EBookBackendLDAPSExpData *ldap_data = data;
	ESExpResult *r;
	char ** strings;

	if (argc > 0) {
		int i;

		strings = g_new0(char*, argc+3);
		strings[0] = g_strdup ("(&");
		strings[argc+3 - 2] = g_strdup (")");
		
		for (i = 0; i < argc; i ++) {
			GList *list_head = ldap_data->list;
			if (!list_head)
				break;
			strings[argc - i] = list_head->data;
			ldap_data->list = g_list_remove_link(list_head, list_head);
			g_list_free_1(list_head);
		}

		ldap_data->list = g_list_prepend(ldap_data->list, g_strjoinv(" ", strings));

		for (i = 0 ; i < argc + 2; i ++)
			g_free (strings[i]);

		g_free (strings);
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_or(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	EBookBackendLDAPSExpData *ldap_data = data;
	ESExpResult *r;
	char ** strings;

	if (argc > 0) {
		int i;

		strings = g_new0(char*, argc+3);
		strings[0] = g_strdup ("(|");
		strings[argc+3 - 2] = g_strdup (")");

		for (i = 0; i < argc; i ++) {
			GList *list_head = ldap_data->list;
			if (!list_head)
				break;
			strings[argc - i] = list_head->data;
			ldap_data->list = g_list_remove_link(list_head, list_head);
			g_list_free_1(list_head);
		}

		ldap_data->list = g_list_prepend(ldap_data->list, g_strjoinv(" ", strings));

		for (i = 0 ; i < argc + 2; i ++)
			g_free (strings[i]);

		g_free (strings);
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_not(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	EBookBackendLDAPSExpData *ldap_data = data;
	ESExpResult *r;

	/* just replace the head of the list with the NOT of it. */
	if (argc > 0) {
		char *term = ldap_data->list->data;
		ldap_data->list->data = g_strdup_printf("(!%s)", term);
		g_free (term);
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_contains(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	EBookBackendLDAPSExpData *ldap_data = data;
	ESExpResult *r;

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		char *propname = argv[0]->value.string;
		char *str = rfc2254_escape(argv[1]->value.string);
		gboolean one_star = FALSE;

		if (strlen(str) == 0)
			one_star = TRUE;

		if (!strcmp (propname, "x-evolution-any-field")) {
			int i;
			int query_length;
			char *big_query;
			char *match_str;
			if (one_star) {
				/* ignore NULL query */
				r = e_sexp_result_new (f, ESEXP_RES_BOOL);
				r->value.bool = FALSE;
				return r;
			}

			match_str = g_strdup_printf ("=*%s*)", str);

			query_length = 3; /* strlen ("(|") + strlen (")") */

			for (i = 0; i < num_prop_infos; i ++) {
				query_length += 1 /* strlen ("(") */ + strlen(prop_info[i].ldap_attr) + strlen (match_str);
			}

			big_query = g_malloc0(query_length + 1);
			strcat (big_query, "(|");
			for (i = 0; i < num_prop_infos; i ++) {
				strcat (big_query, "(");
				strcat (big_query, prop_info[i].ldap_attr);
				strcat (big_query, match_str);
			}
			strcat (big_query, ")");

			ldap_data->list = g_list_prepend(ldap_data->list, big_query);

			g_free (match_str);
		}
		else {
			char *ldap_attr = query_prop_to_ldap(propname);

			if (ldap_attr)
				ldap_data->list = g_list_prepend(ldap_data->list,
								 g_strdup_printf("(%s=*%s%s)",
										 ldap_attr,
										 str,
										 one_star ? "" : "*"));
		}

		g_free (str);
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_is(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	EBookBackendLDAPSExpData *ldap_data = data;
	ESExpResult *r;

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		char *propname = argv[0]->value.string;
		char *str = rfc2254_escape(argv[1]->value.string);
		char *ldap_attr = query_prop_to_ldap(propname);

		if (ldap_attr)
			ldap_data->list = g_list_prepend(ldap_data->list,
							 g_strdup_printf("(%s=%s)",
									 ldap_attr, str));
		else {
			g_warning ("unknown query property\n");
			/* we want something that'll always be false */
			ldap_data->list = g_list_prepend(ldap_data->list,
							 g_strdup("objectClass=MyBarnIsBiggerThanYourBarn"));
		}

		g_free (str);
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_beginswith(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	EBookBackendLDAPSExpData *ldap_data = data;
	ESExpResult *r;

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		char *propname = argv[0]->value.string;
		char *str = rfc2254_escape(argv[1]->value.string);
		char *ldap_attr = query_prop_to_ldap(propname);

		if (strlen (str) == 0) {
			r = e_sexp_result_new (f, ESEXP_RES_BOOL);
			r->value.bool = FALSE;
			return r;
		}

		/* insert hack for fileAs queries, since we need to do
		   the right thing if the server supports them or not,
		   and for entries that have no fileAs attribute. */
		if (ldap_attr) {
			if (!strcmp (propname, "full_name")) {
				ldap_data->list = g_list_prepend(ldap_data->list,
							       g_strdup_printf(
								       "(|(cn=%s*)(sn=%s*))",
								       str, str));
			}
			else if (!strcmp (ldap_attr, "fileAs")) {
				if (ldap_data->bl->priv->evolutionPersonSupported)
					ldap_data->list = g_list_prepend(ldap_data->list,
								 g_strdup_printf("(|(fileAs=%s*)(&(!(fileAs=*))(sn=%s*)))",
										 str, str));
				else 
					ldap_data->list = g_list_prepend(ldap_data->list,
									 g_strdup_printf("(sn=%s*)", str));
			}
			else {
				ldap_data->list = g_list_prepend(ldap_data->list,
								 g_strdup_printf("(%s=%s*)",
										 ldap_attr,
										 str));
			}
		}

		g_free (str);
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_endswith(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	EBookBackendLDAPSExpData *ldap_data = data;
	ESExpResult *r;

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		char *propname = argv[0]->value.string;
		char *str = rfc2254_escape(argv[1]->value.string);
		char *ldap_attr = query_prop_to_ldap(propname);

		if (ldap_attr)
			ldap_data->list = g_list_prepend(ldap_data->list,
							 g_strdup_printf("(%s=*%s)",
									 ldap_attr,
									 str));
		g_free (str);
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_exists(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	EBookBackendLDAPSExpData *ldap_data = data;
	ESExpResult *r;

	if (argc == 1
	    && argv[0]->type == ESEXP_RES_STRING) {
		char *propname = argv[0]->value.string;

		if (!strcmp (propname, "x-evolution-any-field")) {
			int i;
			int query_length;
			char *big_query;
			char *match_str;

			match_str = g_strdup("=*)");

			query_length = 3; /* strlen ("(|") + strlen (")") */

			for (i = 0; i < num_prop_infos; i ++) {
				query_length += 1 /* strlen ("(") */ + strlen(prop_info[i].ldap_attr) + strlen (match_str);
			}

			big_query = g_malloc0(query_length + 1);
			strcat (big_query, "(|");
			for (i = 0; i < num_prop_infos; i ++) {
				strcat (big_query, "(");
				strcat (big_query, prop_info[i].ldap_attr);
				strcat (big_query, match_str);
			}
			strcat (big_query, ")");

			ldap_data->list = g_list_prepend(ldap_data->list, big_query);

			g_free (match_str);
		}
		else {
			char *ldap_attr = query_prop_to_ldap(propname);

			if (ldap_attr)
				ldap_data->list = g_list_prepend(ldap_data->list,
								 g_strdup_printf("(%s=*)", ldap_attr));
		}
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

/* 'builtin' functions */
static struct {
	char *name;
	ESExpFunc *func;
	int type;		/* set to 1 if a function can perform shortcut evaluation, or
				   doesn't execute everything, 0 otherwise */
} symbols[] = {
	{ "and", func_and, 0 },
	{ "or", func_or, 0 },
	{ "not", func_not, 0 },
	{ "contains", func_contains, 0 },
	{ "is", func_is, 0 },
	{ "beginswith", func_beginswith, 0 },
	{ "endswith", func_endswith, 0 },
	{ "exists", func_exists, 0 },
};

static gchar *
e_book_backend_ldap_build_query (EBookBackendLDAP *bl, const char *query)
{
	ESExp *sexp;
	ESExpResult *r;
	gchar *retval;
	EBookBackendLDAPSExpData data;
	int i;
	char **strings;

	data.list = NULL;
	data.bl = bl;

	sexp = e_sexp_new();

	for(i=0;i<sizeof(symbols)/sizeof(symbols[0]);i++) {
		if (symbols[i].type == 1) {
			e_sexp_add_ifunction(sexp, 0, symbols[i].name,
					     (ESExpIFunc *)symbols[i].func, &data);
		} else {
			e_sexp_add_function(sexp, 0, symbols[i].name,
					    symbols[i].func, &data);
		}
	}

	e_sexp_input_text(sexp, query, strlen(query));
	e_sexp_parse(sexp);

	r = e_sexp_eval(sexp);

	e_sexp_result_free(sexp, r);
	e_sexp_unref (sexp);

	if (data.list) {
		if (data.list->next) {
			g_warning ("conversion to ldap query string failed");
			retval = NULL;
			g_list_foreach (data.list, (GFunc)g_free, NULL);
		}
		else {
			strings = g_new0(char*, 5);
			strings[0] = g_strdup ("(&");
			strings[1] = g_strdup ("(objectclass=person)");
			strings[2] = data.list->data;
			strings[3] = g_strdup (")");
			retval =  g_strjoinv (" ", strings);
			for (i = 0 ; i < 4; i ++)
				g_free (strings[i]);
			g_free (strings);
		}
	}
	else {
		g_warning ("conversion to ldap query string failed");
		retval = NULL;
	}

	g_list_free (data.list);
	return retval;
}

static gchar *
query_prop_to_ldap(gchar *query_prop)
{
	int i;

	for (i = 0; i < num_prop_infos; i ++)
		if (!strcmp (query_prop, e_contact_field_name (prop_info[i].field_id)))
			return prop_info[i].ldap_attr;

	return NULL;
}


typedef struct {
	LDAPOp op;
	EDataBookView *view;

	/* used to detect problems with start/stop_book_view racing */
	gboolean aborted;
	/* used by search_handler to only send the status messages once */
	gboolean notified_receiving_results;
} LDAPSearchOp;

static EContact *
build_contact_from_entry (LDAP *ldap, LDAPMessage *e, GList **existing_objectclasses)
{
	EContact *contact = e_contact_new ();
	char *dn;
	char *attr;
	BerElement *ber = NULL;

	dn = ldap_get_dn(ldap, e);
	e_contact_set (contact, E_CONTACT_UID, dn);
	ldap_memfree (dn);

	for (attr = ldap_first_attribute (ldap, e, &ber); attr;
	     attr = ldap_next_attribute (ldap, e, ber)) {
		int i;
		struct prop_info *info = NULL;
		char **values;

		if (existing_objectclasses && !g_ascii_strcasecmp (attr, "objectclass")) {
			values = ldap_get_values (ldap, e, attr);
			for (i = 0; values[i]; i ++)
				*existing_objectclasses = g_list_append (*existing_objectclasses, g_strdup (values[i]));

			ldap_value_free (values);
		}
		else {
			for (i = 0; i < num_prop_infos; i ++)
				if (!g_ascii_strcasecmp (attr, prop_info[i].ldap_attr)) {
					info = &prop_info[i];
					break;
				}

			printf ("attr = %s, ", attr);
			printf ("info = %p\n", info);

			if (info) {
				if (info->prop_type & PROP_WRITE_ONLY)
					continue;

				if (info->prop_type & PROP_TYPE_BINARY) {
					struct berval **ber_values = ldap_get_values_len (ldap, e, attr);

					if (ber_values) {
						info->binary_populate_contact_func (contact, ber_values);

						ldap_value_free_len (ber_values);
					}
				}
				else {
					values = ldap_get_values (ldap, e, attr);

					if (values) {
						if (info->prop_type & PROP_TYPE_STRING) {
							printf ("value = %s\n", values[0]);
							/* if it's a normal property just set the string */
							if (values[0])
								e_contact_set (contact, info->field_id, values[0]);
						}
						else if (info->prop_type & PROP_TYPE_COMPLEX) {
							/* if it's a list call the contact-populate function,
							   which calls g_object_set to set the property */
							info->populate_contact_func(contact,
										    values);
						}

						ldap_value_free (values);
					}
				}
			}
		}

		ldap_memfree (attr);
	}

	if (ber)
		ber_free (ber, 0);

	return contact;
}

static gboolean
poll_ldap (EBookBackendLDAP *bl)
{
	LDAP           *ldap = bl->priv->ldap;
	int            rc;
	LDAPMessage    *res;
	struct timeval timeout;

	if (!ldap) {
		bl->priv->poll_timeout = -1;
		return FALSE;
	}

	if (!bl->priv->active_ops) {
		g_warning ("poll_ldap being called for backend with no active operations");
		bl->priv->poll_timeout = -1;
		return FALSE;
	}

	timeout.tv_sec = 0;
	timeout.tv_usec = LDAP_RESULT_TIMEOUT_MILLIS * 1000;

	rc = ldap_result (ldap, LDAP_RES_ANY, 0, &timeout, &res);
	if (rc != 0) {/* rc == 0 means timeout exceeded */
		if (rc == -1) {
			EDataBookView *book_view = find_book_view (bl);
			g_warning ("ldap_result returned -1, restarting ops");

			e_book_backend_ldap_reconnect (bl, book_view, LDAP_SERVER_DOWN);
#if 0
			if (bl->priv->connected)
				restart_ops (bl);
#endif
		}
		else {
			int msgid = ldap_msgid (res);
			LDAPOp *op;

			g_static_rec_mutex_lock (&bl->priv->op_hash_mutex);
			op = g_hash_table_lookup (bl->priv->id_to_op, &msgid);

			d(printf ("looked up msgid %d, got op %p\n", msgid, op));

			if (op)
				op->handler (op, res);
			else
				g_warning ("unknown operation, msgid = %d", msgid);

			/* XXX should the call to op->handler be
			   protected by the lock? */
			g_static_rec_mutex_unlock (&bl->priv->op_hash_mutex);

			ldap_msgfree(res);
		}
	}

	return TRUE;
}

static void
ldap_search_handler (LDAPOp *op, LDAPMessage *res)
{
	LDAPSearchOp *search_op = (LDAPSearchOp*)op;
	EDataBookView *view = search_op->view;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (op->backend);
	LDAP *ldap = bl->priv->ldap;
	LDAPMessage *e;
	int msg_type;

	d(printf ("ldap_search_handler (%p)\n", view));

	if (!ldap) {
		e_data_book_view_notify_complete (view, GNOME_Evolution_Addressbook_OtherError);
		ldap_op_finished (op);
		return;
	}

	if (!search_op->notified_receiving_results) {
		search_op->notified_receiving_results = TRUE;
		book_view_notify_status (op->view, _("Receiving LDAP search results..."));
	}

	msg_type = ldap_msgtype (res);
	if (msg_type == LDAP_RES_SEARCH_ENTRY) {
		e = ldap_first_entry(ldap, res);

		while (NULL != e) {
			EContact *contact = build_contact_from_entry (ldap, e, NULL);

			e_data_book_view_notify_update (view, contact);
			g_object_unref (contact);

			e = ldap_next_entry(ldap, e);
		}
	}
	else if (msg_type == LDAP_RES_SEARCH_RESULT) {
		char *ldap_error_msg;
		int ldap_error;

		ldap_parse_result (ldap, res, &ldap_error,
				   NULL, &ldap_error_msg, NULL, NULL, 0);
		if (ldap_error != LDAP_SUCCESS) {
			g_warning ("ldap_search_handler: %02X (%s), additional info: %s",
				   ldap_error,
				   ldap_err2string (ldap_error), ldap_error_msg);
		}
		ldap_memfree (ldap_error_msg);

		if (ldap_error == LDAP_TIMELIMIT_EXCEEDED)
			e_data_book_view_notify_complete (view, GNOME_Evolution_Addressbook_SearchTimeLimitExceeded);
		else if (ldap_error == LDAP_SIZELIMIT_EXCEEDED)
			e_data_book_view_notify_complete (view, GNOME_Evolution_Addressbook_SearchSizeLimitExceeded);
		else if (ldap_error == LDAP_SUCCESS)
			e_data_book_view_notify_complete (view, GNOME_Evolution_Addressbook_Success);
		else
			e_data_book_view_notify_complete (view, GNOME_Evolution_Addressbook_OtherError);

		ldap_op_finished (op);
	}
	else {
		g_warning ("unhandled search result type %d returned", msg_type);
		//e_data_book_view_notify_complete (view, GNOME_Evolution_Addressbook_OtherError);
		e_data_book_view_notify_complete (view, GNOME_Evolution_Addressbook_InvalidQuery);
		ldap_op_finished (op);
	}
}

static void
ldap_search_dtor (LDAPOp *op)
{
	LDAPSearchOp *search_op = (LDAPSearchOp*) op;

	d(printf ("ldap_search_dtor (%p)\n", search_op->view));

	/* unhook us from our EDataBookView */
	g_object_set_data (G_OBJECT (search_op->view), "EBookBackendLDAP.BookView::search_op", NULL);

	bonobo_object_unref (search_op->view);

	g_free (search_op);
}

static void
e_book_backend_ldap_search (EBookBackendLDAP *bl,
			    EDataBook        *book,
			    EDataBookView    *view)
{
	char *ldap_query;
	GList *contacts;
	GList *l;

	switch (bl->priv->mode) {
	case GNOME_Evolution_Addressbook_MODE_LOCAL :
		if (!(bl->priv->marked_for_offline && bl->priv->cache)) {
			e_data_book_view_notify_complete (view, GNOME_Evolution_Addressbook_RepositoryOffline);
			return;
		}

		contacts = e_book_backend_cache_get_contacts (bl->priv->cache,
							      e_data_book_view_get_card_query (view));

		for (l = contacts; l; l = g_list_next (l)) {
			EContact *contact = l->data;
			e_data_book_view_notify_update (view, contact);
			g_object_unref (contact);
		}

		g_list_free (contacts);

		e_data_book_view_notify_complete (view, GNOME_Evolution_Addressbook_Success);
		return;
		
	case GNOME_Evolution_Addressbook_MODE_REMOTE :
		ldap_query = e_book_backend_ldap_build_query (bl, e_data_book_view_get_card_query (view));

		if (ldap_query != NULL && bl->priv->ldap) {
			LDAP *ldap = bl->priv->ldap;
			int ldap_err;
			int search_msgid;
			int view_limit;

			view_limit = e_data_book_view_get_max_results (view);
			if (view_limit == -1 || view_limit > bl->priv->ldap_limit)
				view_limit = bl->priv->ldap_limit;

			printf ("searching server using filter: %s (expecting max %d results)\n", ldap_query,
				view_limit);

			do {
				book_view_notify_status (view, _("Searching..."));

				ldap_err = ldap_search_ext (ldap, bl->priv->ldap_rootdn,
							    bl->priv->ldap_scope,
							    ldap_query,
							    NULL, 0,
							    NULL, /* XXX */
							    NULL, /* XXX */
							    NULL, /* XXX timeout */
							    view_limit, &search_msgid);
			} while (e_book_backend_ldap_reconnect (bl, view, ldap_err));

			g_free (ldap_query);

			if (ldap_err != LDAP_SUCCESS) {
				book_view_notify_status (view, ldap_err2string(ldap_err));
				return;
			}
			else if (search_msgid == -1) {
				book_view_notify_status (view,
							 _("Error performing search"));
				return;
			}
			else {
				LDAPSearchOp *op = g_new0 (LDAPSearchOp, 1);

				d(printf ("adding search_op (%p, %d)\n", view, search_msgid));

				op->view = view;

				bonobo_object_ref (view);

				ldap_op_add ((LDAPOp*)op, E_BOOK_BACKEND(bl), book, view,
					     0, search_msgid,
					     ldap_search_handler, ldap_search_dtor);

				g_object_set_data (G_OBJECT (view), "EBookBackendLDAP.BookView::search_op", op);
			}
			return;
		}
		else {
			/*
			e_data_book_view_notify_complete (view,
							  GNOME_Evolution_Addressbook_InvalidQuery);
			*/
			/* Ignore NULL query */
			e_data_book_view_notify_complete (view,
							  GNOME_Evolution_Addressbook_Success);
			return;
		}
	}
}

static void
e_book_backend_ldap_start_book_view (EBookBackend  *backend,
				     EDataBookView *view)
{
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);

	d(printf ("start_book_view (%p)\n", view));

	/* we start at 1 so the user sees stuff as it appears and we
	   aren't left waiting for more cards to show up, if the
	   connection is slow. */
	e_data_book_view_set_thresholds (view, 1, 3000);

	e_book_backend_ldap_search (bl, NULL /* XXX ugh */, view);
}

static void
e_book_backend_ldap_stop_book_view (EBookBackend  *backend,
				    EDataBookView *view)
{
	LDAPSearchOp *op;

	d(printf ("stop_book_view (%p)\n", view));

	op = g_object_get_data (G_OBJECT (view), "EBookBackendLDAP.BookView::search_op");
	if (op)
		ldap_op_finished ((LDAPOp*)op);
}

static void
e_book_backend_ldap_get_changes (EBookBackend *backend,
				 EDataBook    *book,
				 guint32       opid,
				 const char   *change_id)
{
	/* FIXME: implement */
}

#define LDAP_SIMPLE_PREFIX "ldap/simple-"
#define SASL_PREFIX "sasl/"

static void
generate_cache_handler (LDAPOp *op, LDAPMessage *res)
{
	LDAPGetContactListOp *contact_list_op = (LDAPGetContactListOp *) op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (op->backend);
	LDAP *ldap = bl->priv->ldap;
	LDAPMessage *e;
	gint msg_type;
	EDataBookView *book_view;

	if (!ldap) {
		ldap_op_finished (op);
		return;
	}

	book_view = find_book_view (bl);

	msg_type = ldap_msgtype (res);
	if (msg_type == LDAP_RES_SEARCH_ENTRY) {
		e = ldap_first_entry(ldap, res);

		while (e != NULL) {
			EContact *contact = build_contact_from_entry (ldap, e, NULL);
			contact_list_op->contacts = g_list_prepend (contact_list_op->contacts, contact);

			e = ldap_next_entry(ldap, e);
		}
	} else {
		GList *l;
		int contact_num = 0;
		char *status_msg;
		
		e_file_cache_clean (E_FILE_CACHE (bl->priv->cache));

		e_file_cache_freeze_changes (E_FILE_CACHE (bl->priv->cache));
		for (l = contact_list_op->contacts; l; l = g_list_next (l)) {
			EContact *contact = l->data;

			contact_num++;
			if (book_view) {
				status_msg = g_strdup_printf (_("Downloading contacts (%d)... "),
								 contact_num);
				e_data_book_view_notify_status_message (book_view, status_msg);
				g_free (status_msg); 
			}
			e_book_backend_cache_add_contact (bl->priv->cache, contact);
		}
		e_book_backend_cache_set_populated (bl->priv->cache);
		e_file_cache_thaw_changes (E_FILE_CACHE (bl->priv->cache));
		if (book_view)
			e_data_book_view_notify_complete (book_view,
							  GNOME_Evolution_Addressbook_Success);
		ldap_op_finished (op);
	}
}

static void
generate_cache_dtor (LDAPOp *op)
{
	LDAPGetContactListOp *contact_list_op = (LDAPGetContactListOp *) op;
	GList *l;

	for (l = contact_list_op->contacts; l; l = g_list_next (l)) {
		g_object_unref (l->data);
	}

	g_list_free (contact_list_op->contacts);
	g_free (contact_list_op);
}

static void
generate_cache (EBookBackendLDAP *book_backend_ldap)
{
	LDAPGetContactListOp *contact_list_op = g_new0 (LDAPGetContactListOp, 1);
	EBookBackendLDAPPrivate *priv;
	gchar *ldap_query;
	gint contact_list_msgid;
	gint ldap_error;

	priv = book_backend_ldap->priv;

	if (!priv->ldap) {
		g_free (contact_list_op);
		return;
	}

	ldap_query = e_book_backend_ldap_build_query (book_backend_ldap, 
						      "(beginswith \"file_as\" \"\")");

	do {	
		ldap_error = ldap_search_ext (priv->ldap,
					      priv->ldap_rootdn,
					      priv->ldap_scope,
					      ldap_query,
					      NULL, 0, NULL, NULL,
					      NULL, /* XXX timeout */
					      LDAP_NO_LIMIT, &contact_list_msgid);
	} while (e_book_backend_ldap_reconnect (book_backend_ldap, NULL, ldap_error));

	g_free (ldap_query);

	if (ldap_error == LDAP_SUCCESS) {
		ldap_op_add ((LDAPOp*) contact_list_op, (EBookBackend *) book_backend_ldap, NULL /* book */,
			     NULL /* book_view */, 0 /* opid */, contact_list_msgid,
			     generate_cache_handler, generate_cache_dtor);
	} else {
		generate_cache_dtor ((LDAPOp *) contact_list_op);
	}
}

static void
e_book_backend_ldap_authenticate_user (EBookBackend *backend,
				       EDataBook    *book,
				       guint32       opid,
				       const char   *user,
				       const char   *passwd,
				       const char   *auth_method)
{
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);
	int ldap_error;
	int status;
	char *dn = NULL;

	if (bl->priv->mode == GNOME_Evolution_Addressbook_MODE_LOCAL) {
		e_book_backend_notify_writable (backend, FALSE);
		e_book_backend_notify_connection_status (backend, FALSE);
		e_data_book_respond_authenticate_user (book,
						       opid,
						       GNOME_Evolution_Addressbook_Success);
		return;
	}
	if (!bl->priv->connected || !bl->priv->ldap) {
		status = e_book_backend_ldap_connect (bl);
		if (status != GNOME_Evolution_Addressbook_Success) {
			e_data_book_respond_authenticate_user (book,
							       opid, status);
			return ;
		}
						       
	}
	if (!strncasecmp (auth_method, LDAP_SIMPLE_PREFIX, strlen (LDAP_SIMPLE_PREFIX))) {

		if (!strcmp (auth_method, "ldap/simple-email")) {
			LDAPMessage    *res, *e;
			char *query = g_strdup_printf ("(mail=%s)", user);

			ldap_error = ldap_search_s (bl->priv->ldap,
						    bl->priv->ldap_rootdn,
						    bl->priv->ldap_scope,
						    query,
						    NULL, 0, &res);
			g_free (query);

			if (ldap_error == LDAP_SUCCESS) {
				char *entry_dn;

				e = ldap_first_entry (bl->priv->ldap, res);
				if (!e) {
					g_warning ("Failed to get the DN for %s", user);
					ldap_msgfree (res);
					e_data_book_respond_authenticate_user (book,
									       opid,
									       GNOME_Evolution_Addressbook_AuthenticationFailed);
					return;
				}

				entry_dn = ldap_get_dn (bl->priv->ldap, e);
				dn = g_strdup(entry_dn);

				ldap_memfree (entry_dn);
				ldap_msgfree (res);
			}
			else {
				e_data_book_respond_authenticate_user (book,
								       opid,
								       GNOME_Evolution_Addressbook_PermissionDenied);
				return;
			}
		}
		else if (!strcmp (auth_method, "ldap/simple-binddn")) {
			dn = g_strdup (user);
		}

		/* now authenticate against the DN we were either supplied or queried for */
		printf ("simple auth as %s\n", dn);
		ldap_error = ldap_simple_bind_s(bl->priv->ldap,
						dn,
						passwd);
		/* Some ldap servers are returning (ex active directory ones) LDAP_SERVER_DOWN
		 * when we try to do an ldap operation  after being  idle 
		 * for some time. This error is handled by poll_ldap in case of search operations
		 * We need to handle it explicitly for this bind call. We call reconnect so that
		 * we get a fresh ldap handle Fixes #67541 */

		if (ldap_error == LDAP_SERVER_DOWN) {
			EDataBookView *view = find_book_view (bl);

			if (e_book_backend_ldap_reconnect (bl, view, ldap_error)) {
				ldap_error = LDAP_SUCCESS;
			}

		}
				
		e_data_book_respond_authenticate_user (book,
						       opid,
						       ldap_error_to_response (ldap_error));
	}
#ifdef ENABLE_SASL_BINDS
	else if (!strncasecmp (auth_method, SASL_PREFIX, strlen (SASL_PREFIX))) {
		g_print ("sasl bind (mech = %s) as %s", auth_method + strlen (SASL_PREFIX), user);
		ldap_error = ldap_sasl_bind_s (bl->priv->ldap,
					       NULL,
					       auth_method + strlen (SASL_PREFIX),
					       passwd,
					       NULL,
					       NULL,
					       NULL);

		if (ldap_error == LDAP_NOT_SUPPORTED)
			e_data_book_respond_authenticate_user (book,
							       opid,
							       GNOME_Evolution_Addressbook_UnsupportedAuthenticationMethod);
		else
			e_data_book_respond_authenticate_user (book,
							       opid,
							       ldap_error_to_response (ldap_error));
	}
#endif
	else {
		e_data_book_respond_authenticate_user (book,
						       opid,
						       GNOME_Evolution_Addressbook_UnsupportedAuthenticationMethod);
		return;
	}

	if (ldap_error == LDAP_SUCCESS) {
		bl->priv->auth_dn = dn;
		bl->priv->auth_passwd = g_strdup (passwd);

		e_book_backend_set_is_writable (backend, TRUE);

		/* force a requery on the root dse since some ldap
		   servers are set up such that they don't report
		   anything (including the schema DN) until the user
		   is authenticated */
		if (!bl->priv->evolutionPersonChecked) {
			ldap_error = query_ldap_root_dse (bl);

			if (LDAP_SUCCESS == ldap_error) {
				if (!bl->priv->evolutionPersonChecked)
					check_schema_support (bl);
			}
			else
				g_warning ("Failed to perform root dse query after authenticating, (ldap_error 0x%02x)", ldap_error);
		}

		e_data_book_report_writable (book, TRUE);

		if (bl->priv->marked_for_offline && bl->priv->cache)
			generate_cache (bl);
	}
}

static void
e_book_backend_ldap_get_required_fields (EBookBackend *backend,
					  EDataBook    *book,
					  guint32       opid)

{
	GList *fields = NULL;
	
	
	/*FIMEME we should look at mandatory attributs in the schema
	  and return all those fields here */
	fields = g_list_append (fields, (char *)e_contact_field_name (E_CONTACT_FILE_AS));
	fields = g_list_append (fields, (char *)e_contact_field_name (E_CONTACT_FULL_NAME));
	fields = g_list_append (fields, (char *)e_contact_field_name (E_CONTACT_FAMILY_NAME));
	
	
	e_data_book_respond_get_required_fields (book,
						  opid,
						  GNOME_Evolution_Addressbook_Success,
						  fields);
	g_list_free (fields);
}


static void
e_book_backend_ldap_get_supported_fields (EBookBackend *backend,
					  EDataBook    *book,
					  guint32       opid)

{
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);

	e_data_book_respond_get_supported_fields (book,
						  opid,
						  GNOME_Evolution_Addressbook_Success,
						  bl->priv->supported_fields);
}

static void
e_book_backend_ldap_get_supported_auth_methods (EBookBackend *backend,
						EDataBook    *book,
						guint32       opid)

{
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);

	e_data_book_respond_get_supported_auth_methods (book,
							opid,
							GNOME_Evolution_Addressbook_Success,
							bl->priv->supported_auth_methods);
}

static void
ldap_cancel_op(void *key, void *value, void *data)
{
	EBookBackendLDAP *bl = data;
	LDAPOp *op = value;

	/* ignore errors, its only best effort? */
	if (bl->priv->ldap)
		ldap_abandon_ext (bl->priv->ldap, op->id, NULL, NULL);
}

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_ldap_cancel_operation (EBookBackend *backend, EDataBook *book)
{
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);

	g_static_rec_mutex_lock (&bl->priv->op_hash_mutex);
	g_hash_table_foreach (bl->priv->id_to_op, ldap_cancel_op, bl);
	g_static_rec_mutex_unlock (&bl->priv->op_hash_mutex);

	return GNOME_Evolution_Addressbook_Success;
}

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_ldap_load_source (EBookBackend             *backend,
				 ESource                  *source,
				 gboolean                  only_if_exists)
{
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);
	LDAPURLDesc    *lud;
	int ldap_error;
	int limit = 100;
	int timeout = 60; /* 1 minute */
	gchar *uri;
	const char *str;
	const char *offline;
	gint result;

	g_assert (bl->priv->connected == FALSE);

	uri = e_source_get_uri (source);

	offline = e_source_get_property (source, "offline_sync");
	if (offline  &&   g_str_equal (offline, "1"))
		bl->priv->marked_for_offline = TRUE;
	str = e_source_get_property (source, "limit");
	if (str)
		limit = atoi (str);

	str = e_source_get_property (source, "ssl");
	if (str) {
		if (!strcmp (str, "always"))
			bl->priv->use_tls = E_BOOK_BACKEND_LDAP_TLS_ALWAYS;
		else if (!strcmp (str, "whenever_possible"))
			bl->priv->use_tls = E_BOOK_BACKEND_LDAP_TLS_WHEN_POSSIBLE;
		else if (strcmp (str, "never"))
			g_warning ("Unhandled value for 'ssl', not using it.");
	}
	else
		bl->priv->use_tls = E_BOOK_BACKEND_LDAP_TLS_NO;

	str = e_source_get_property (source, "timeout");
	if (str)
		timeout = atoi (str);

	ldap_error = ldap_url_parse ((char*) uri, &lud);

	if (ldap_error == LDAP_SUCCESS) {
		bl->priv->ldap_host = g_strdup(lud->lud_host);
		bl->priv->ldap_port = lud->lud_port;
		/* if a port wasn't specified, default to LDAP_PORT */
		if (bl->priv->ldap_port == 0)
			bl->priv->ldap_port = LDAP_PORT;
		bl->priv->ldap_rootdn = g_strdup(lud->lud_dn);
		bl->priv->ldap_limit = limit;
		bl->priv->ldap_timeout = timeout;
		bl->priv->ldap_scope = lud->lud_scope;

		ldap_free_urldesc(lud);
	} else {
		g_free (uri);
		return GNOME_Evolution_Addressbook_OtherError;
	}

	if (bl->priv->cache) {
		g_object_unref (bl->priv->cache);
		bl->priv->cache = NULL;
	}

	bl->priv->cache = e_book_backend_cache_new (uri);
	g_free (uri);

	if (bl->priv->mode == GNOME_Evolution_Addressbook_MODE_LOCAL) {
		/* Offline */

		e_book_backend_set_is_loaded (backend, TRUE);
		e_book_backend_set_is_writable (backend, FALSE);
		e_book_backend_notify_writable (backend, FALSE);
		e_book_backend_notify_connection_status (backend, FALSE);

		if (!bl->priv->marked_for_offline)
			return GNOME_Evolution_Addressbook_OfflineUnavailable;

#if 0
		if (!e_book_backend_cache_is_populated (bl->priv->cache))
			return GNOME_Evolution_Addressbook_OfflineUnavailable;
#endif

		return GNOME_Evolution_Addressbook_Success;
	}
	else 
		e_book_backend_notify_connection_status (backend, TRUE);

	/* Online */

	result = e_book_backend_ldap_connect (bl);
	if (result != GNOME_Evolution_Addressbook_Success)
		return result;

	if (bl->priv->marked_for_offline)
		generate_cache (bl);

	return result;
}

static void
e_book_backend_ldap_remove (EBookBackend *backend, EDataBook *book, guint32 opid)
{
	/* if we ever add caching, we'll remove it here, but for now,
	   just report back Success */

	e_data_book_respond_remove (book, opid, GNOME_Evolution_Addressbook_Success);
}

static char*
e_book_backend_ldap_get_static_capabilities (EBookBackend *backend)
{
	return g_strdup("net,anon-access,do-initial-query");
}

static void
stop_views (EBookBackend *backend)
{
	EList     *book_views;
	EIterator *iter;

	book_views = e_book_backend_get_book_views (backend);
	iter = e_list_get_iterator (book_views);

	while (e_iterator_is_valid (iter)) {
		EDataBookView *data_book_view = (EDataBookView *) e_iterator_get (iter);
		e_book_backend_ldap_stop_book_view (backend, data_book_view);
		e_iterator_next (iter);
	}

	g_object_unref (iter);
	g_object_unref (book_views);
}

static void
start_views (EBookBackend *backend)
{
	EList     *book_views;
	EIterator *iter;

	book_views = e_book_backend_get_book_views (backend);
	iter = e_list_get_iterator (book_views);

	while (e_iterator_is_valid (iter)) {
		EDataBookView *data_book_view = (EDataBookView *) e_iterator_get (iter);
		e_book_backend_ldap_start_book_view (backend, data_book_view);
		e_iterator_next (iter);
	}

	g_object_unref (iter);
	g_object_unref (book_views);
}

static void 
e_book_backend_ldap_set_mode (EBookBackend *backend, int mode)
{
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);

	if (bl->priv->mode == mode)
		return;

	bl->priv->mode = mode;
#if 0
	stop_views (backend);
#endif

	/* Cancel all running operations */
	e_book_backend_ldap_cancel_operation (backend, NULL);

	if (mode == GNOME_Evolution_Addressbook_MODE_LOCAL) {
		/* Go offline */

		e_book_backend_set_is_writable (backend, FALSE);
		e_book_backend_notify_writable (backend, FALSE);
		e_book_backend_notify_connection_status (backend, FALSE);

#if 0
		if (bl->priv->ldap) {
			ldap_unbind_ext_s (bl->priv->ldap, NULL, NULL);
			bl->priv->ldap = NULL;
		}
#endif

		bl->priv->connected = FALSE;

#if 0
		if (e_book_backend_is_loaded (backend))
			start_views (backend);
#endif
	}
	else if (mode == GNOME_Evolution_Addressbook_MODE_REMOTE) {
		/* Go online */

		e_book_backend_set_is_writable (backend, TRUE);
		e_book_backend_notify_writable (backend, TRUE);
		e_book_backend_notify_connection_status (backend, TRUE);

		if (e_book_backend_is_loaded (backend)) {
			e_book_backend_ldap_connect (bl);
			e_book_backend_notify_auth_required (backend);

#if 0
			start_views (backend);
#endif

			if (bl->priv->marked_for_offline && bl->priv->cache)
				generate_cache (bl);
		}
	}
}

static gboolean
e_book_backend_ldap_construct (EBookBackendLDAP *backend)
{
	g_assert (backend != NULL);
	g_assert (E_IS_BOOK_BACKEND_LDAP (backend));

	if (! e_book_backend_construct (E_BOOK_BACKEND (backend)))
		return FALSE;

	return TRUE;
}

/**
 * e_book_backend_ldap_new:
 */
EBookBackend *
e_book_backend_ldap_new (void)
{
	EBookBackendLDAP *backend;

	backend = g_object_new (E_TYPE_BOOK_BACKEND_LDAP, NULL);

	if (! e_book_backend_ldap_construct (backend)) {
		g_object_unref (backend);
		return NULL;
	}

	return E_BOOK_BACKEND (backend);
}

static gboolean
call_dtor (int msgid, LDAPOp *op, gpointer data)
{
	EBookBackendLDAP *bl;

	bl = E_BOOK_BACKEND_LDAP (op->backend);

	if (bl->priv->ldap)
		ldap_abandon (bl->priv->ldap, op->id);

	op->dtor (op);

	return TRUE;
}

static void
e_book_backend_ldap_dispose (GObject *object)
{
	EBookBackendLDAP *bl;

	bl = E_BOOK_BACKEND_LDAP (object);

	if (bl->priv) {
		g_static_rec_mutex_lock (&bl->priv->op_hash_mutex);
		g_hash_table_foreach_remove (bl->priv->id_to_op, (GHRFunc)call_dtor, NULL);
		g_hash_table_destroy (bl->priv->id_to_op);
		g_static_rec_mutex_unlock (&bl->priv->op_hash_mutex);
		g_static_rec_mutex_free (&bl->priv->op_hash_mutex);

		if (bl->priv->ldap)
			ldap_unbind_ext_s (bl->priv->ldap, NULL, NULL);

		if (bl->priv->poll_timeout != -1) {
			printf ("removing timeout\n");
			g_source_remove (bl->priv->poll_timeout);
		}

		if (bl->priv->supported_fields) {
			g_list_foreach (bl->priv->supported_fields, (GFunc)g_free, NULL);
			g_list_free (bl->priv->supported_fields);
		}

		if (bl->priv->supported_auth_methods) {
			g_list_foreach (bl->priv->supported_auth_methods, (GFunc)g_free, NULL);
			g_list_free (bl->priv->supported_auth_methods);
		}
		if (bl->priv->summary_file_name) {
			g_free (bl->priv->summary_file_name);
			bl->priv->summary_file_name = NULL;
		}
		if (bl->priv->summary) {
			e_book_backend_summary_save (bl->priv->summary);
			g_object_unref (bl->priv->summary);
			bl->priv->summary = NULL;
		}

		g_free (bl->priv->ldap_host);
		g_free (bl->priv->ldap_rootdn);
		g_free (bl->priv->schema_dn);
		g_free (bl->priv);
		bl->priv = NULL;
	}

	if (G_OBJECT_CLASS (e_book_backend_ldap_parent_class)->dispose)
		G_OBJECT_CLASS (e_book_backend_ldap_parent_class)->dispose (object);
}

static void
e_book_backend_ldap_class_init (EBookBackendLDAPClass *klass)
{
	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	EBookBackendClass *parent_class;

	/* get client side information (extensions present in the library) */
	get_ldap_library_info ();

	e_book_backend_ldap_parent_class = g_type_class_peek_parent (klass);

	parent_class = E_BOOK_BACKEND_CLASS (klass);

	/* Set the virtual methods. */
	parent_class->load_source             = e_book_backend_ldap_load_source;
	parent_class->remove                  = e_book_backend_ldap_remove;
	parent_class->get_static_capabilities = e_book_backend_ldap_get_static_capabilities;

	parent_class->create_contact          = e_book_backend_ldap_create_contact;
	parent_class->remove_contacts         = e_book_backend_ldap_remove_contacts;
	parent_class->modify_contact          = e_book_backend_ldap_modify_contact;
	parent_class->get_contact             = e_book_backend_ldap_get_contact;
	parent_class->get_contact_list        = e_book_backend_ldap_get_contact_list;
	parent_class->start_book_view         = e_book_backend_ldap_start_book_view;
	parent_class->stop_book_view          = e_book_backend_ldap_stop_book_view;
	parent_class->get_changes             = e_book_backend_ldap_get_changes;
	parent_class->authenticate_user       = e_book_backend_ldap_authenticate_user;
	parent_class->get_required_fields    = e_book_backend_ldap_get_required_fields;
	parent_class->get_supported_fields    = e_book_backend_ldap_get_supported_fields;
	parent_class->get_supported_auth_methods = e_book_backend_ldap_get_supported_auth_methods;
	parent_class->cancel_operation	      = e_book_backend_ldap_cancel_operation;
	parent_class->set_mode                = e_book_backend_ldap_set_mode;
	
	object_class->dispose = e_book_backend_ldap_dispose;
}

static void
e_book_backend_ldap_init (EBookBackendLDAP *backend)
{
	EBookBackendLDAPPrivate *priv;

	priv                   = g_new0 (EBookBackendLDAPPrivate, 1);

	priv->supported_fields       = NULL;
	priv->supported_auth_methods = NULL;
	priv->ldap_limit       	     = 100;
	priv->id_to_op         	     = g_hash_table_new (g_int_hash, g_int_equal);
	priv->poll_timeout     	     = -1;
	priv->marked_for_offline     = FALSE;
	priv->mode                   = GNOME_Evolution_Addressbook_MODE_REMOTE;
	priv->is_summary_ready 	     = FALSE;
	priv->reserved1 	     = NULL;
	priv->reserved2 	     = NULL;
	priv->reserved3 	     = NULL;
	priv->reserved4 	     = NULL;
	g_static_rec_mutex_init (&priv->op_hash_mutex);

	backend->priv = priv;
}

/**
 * e_book_backend_ldap_get_type:
 */
GType
e_book_backend_ldap_get_type (void)
{
	static GType type = 0;

	if (! type) {
		GTypeInfo info = {
			sizeof (EBookBackendLDAPClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  e_book_backend_ldap_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (EBookBackendLDAP),
			0,    /* n_preallocs */
			(GInstanceInitFunc) e_book_backend_ldap_init
		};

		type = g_type_register_static (E_TYPE_BOOK_BACKEND, "EBookBackendLDAP", &info, 0);
	}

	return type;
}
