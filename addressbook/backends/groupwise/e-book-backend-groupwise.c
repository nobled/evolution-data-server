/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   Sivaiah Nallagatla (snallagatla@novell.com)
 *
 * Copyright 2004, Novell, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif



#include <libebook/e-contact.h>
                                                                                                                             
#include <libedata-book/e-book-backend-sexp.h>
#include <libedata-book/e-book-backend-summary.h>
#include <libedata-book/e-data-book.h>
#include <libedata-book/e-data-book-view.h>
#include "e-book-backend-groupwise.h"
#include  <e-gw-connection.h>
#include <e-gw-item.h>
static EBookBackendClass *e_book_backend_groupwise_parent_class;
                                                                                                                             
struct _EBookBackendGroupwisePrivate {
  EGwConnection *cnc; 
  char *uri;
  char *container_id;
  char *book_name;
  gboolean only_if_exists;

};

#define ELEMENT_TYPE_SIMPLE 0x01
#define ELEMENT_TYPE_COMPLEX 0x02
#define XML_ELEMENT_TYPE_WITH_ATTRIBUTE 0x04


static void populate_emails (EContact *contact, gpointer data);
static void populate_full_name (EContact *contact, gpointer data);
static void populate_contact_members (EContact *contact, gpointer data);
static void set_full_name_in_gw_item (EGwItem *item, gpointer data);
static void  set_emails_in_gw_item (EGwItem *item, gpointer data);
static void set_members_in_gw_item (EGwItem *item, gpointer data);
static void populate_birth_date (EContact *contact, gpointer data);
static void set_birth_date_in_gw_item (EGwItem *item, gpointer data);
static void populate_address (EContact *contact, gpointer data);
static void set_address_in_gw_item (EGwItem *item, gpointer data);
static void populate_ims (EContact *contact, gpointer data);
static void set_ims_in_gw_item (EGwItem *item, gpointer data);

struct field_element_mapping {
  EContactField field_id;
  
  int element_type;
  char *element_name;
   
  void (*populate_contact_func)(EContact *contact,    gpointer data);
  void (*set_value_in_gw_item) (EGwItem *item, gpointer data);
  
 
} mappings [] = { 
  
  { E_CONTACT_UID, ELEMENT_TYPE_SIMPLE, "id"},
  { E_CONTACT_FILE_AS, ELEMENT_TYPE_SIMPLE, "name" },
  { E_CONTACT_FULL_NAME, ELEMENT_TYPE_COMPLEX, "full_name", populate_full_name, set_full_name_in_gw_item},
  { E_CONTACT_BIRTH_DATE, ELEMENT_TYPE_COMPLEX, "birthday", populate_birth_date, set_birth_date_in_gw_item},
  { E_CONTACT_HOMEPAGE_URL, ELEMENT_TYPE_SIMPLE, "website"},
  { E_CONTACT_NOTE, ELEMENT_TYPE_SIMPLE, "comment"},
  { E_CONTACT_PHONE_PRIMARY, ELEMENT_TYPE_SIMPLE , "default_phone"},
  { E_CONTACT_EMAIL_1, ELEMENT_TYPE_COMPLEX, "email", populate_emails, set_emails_in_gw_item },
  { E_CONTACT_PHONE_BUSINESS, ELEMENT_TYPE_SIMPLE, "phone_Office"},
  { E_CONTACT_PHONE_HOME, ELEMENT_TYPE_SIMPLE, "phone_Home"},
  { E_CONTACT_PHONE_MOBILE, ELEMENT_TYPE_SIMPLE, "phone_Mobile"},
  { E_CONTACT_PHONE_BUSINESS_FAX, ELEMENT_TYPE_SIMPLE, "phone_Fax" },
  { E_CONTACT_PHONE_PAGER, ELEMENT_TYPE_SIMPLE, "phone_Pager"},
  { E_CONTACT_ORG, ELEMENT_TYPE_SIMPLE, "organization"},
  { E_CONTACT_ORG_UNIT, ELEMENT_TYPE_SIMPLE, "department"},
  { E_CONTACT_TITLE, ELEMENT_TYPE_SIMPLE, "title"},
  { E_CONTACT_EMAIL, ELEMENT_TYPE_COMPLEX, "members", populate_contact_members, set_members_in_gw_item},
  // { E_CONTACT_ADDRESS_HOME, ELEMENT_TYPE_COMPLEX, "Home", populate_address, set_address_in_gw_item },
  //{ E_CONTACT_ADDRESS_WORK, ELEMENT_TYPE_COMPLEX, "Office", populate_address, set_address_in_gw_item},
  { E_CONTACT_IM_AIM, ELEMENT_TYPE_COMPLEX, "service", populate_ims, set_ims_in_gw_item }

}; 


static int num_mappings = sizeof(mappings) / sizeof(mappings [0]);

static int email_fields [3] = {
  E_CONTACT_EMAIL_1,
  E_CONTACT_EMAIL_2,
  E_CONTACT_EMAIL_3

};

static void 
populate_ims (EContact *contact, gpointer data)
{
  GList *im_list;
  GList *aim_list = NULL;
  GList *icq_list = NULL;
  GList *yahoo_list = NULL;
  GList *msn_list = NULL;
  GList *jabber_list = NULL;
  IMAddress *address;

  EGwItem *item;
  
  item = E_GW_ITEM (data);
  
  im_list = e_gw_item_get_im_list (item);
  for (; im_list != NULL; im_list = g_list_next (im_list)) {

    address = (IMAddress *) (im_list->data);
    if (address->service == NULL) {
      continue;
    }
    if (g_str_equal (address->service, "icq"))
      icq_list = g_list_append (icq_list, address->address);
    else if (g_str_equal (address->service, "aim"))
      aim_list = g_list_append (aim_list, address->address);
    else if ( g_str_equal (address->service, "msn"))
      msn_list = g_list_append (msn_list, address->address);
    else if (g_str_equal (address->service, "yahoo"))
      yahoo_list = g_list_append (yahoo_list, address->address);
    else if (g_str_equal (address->service, "jabber"))
      jabber_list = g_list_append (jabber_list, address->address);
  }
     
  e_contact_set (contact, E_CONTACT_IM_AIM, aim_list);
  e_contact_set (contact, E_CONTACT_IM_JABBER, jabber_list);
  e_contact_set (contact, E_CONTACT_IM_ICQ, icq_list);
  e_contact_set (contact, E_CONTACT_IM_YAHOO, yahoo_list);
  e_contact_set (contact, E_CONTACT_IM_MSN, msn_list);
}



static void 
set_ims_in_gw_item (EGwItem *item, gpointer data)
{
  EContact *contact;
  GList *im_list = NULL;
  IMAddress *address;

  contact = E_CONTACT (data);
  

  GList *list;
  list = e_contact_get (contact, E_CONTACT_IM_AIM);
  printf ("setting ims \n");
  for (; list != NULL; list =  g_list_next (list))
    {
      printf ("setting aim list\n");
      address = g_new (IMAddress , 1);
      address->service = g_strdup ("aim");
      address->address = list->data;
      im_list = g_list_append (im_list, address);
    }
  
  list = e_contact_get (contact, E_CONTACT_IM_YAHOO);
  for (; list != NULL; list =  g_list_next (list))
    {
      address = g_new (IMAddress , 1);
      address->service = g_strdup ("yahoo");
      address->address = list->data;
      im_list = g_list_append (im_list, address);
    }

  list = e_contact_get (contact, E_CONTACT_IM_ICQ);
  for (; list != NULL; list =  g_list_next (list))
    {
      address = g_new (IMAddress , 1);
      address->service = g_strdup ("icq");
      address->address = list->data;
      im_list = g_list_append (im_list, address);
    }

  list = e_contact_get (contact, E_CONTACT_IM_JABBER);
  for (; list != NULL; list =  g_list_next (list))
    {
      address = g_new (IMAddress , 1);
      address->service = g_strdup ("jabber");
      address->address = list->data;
      im_list = g_list_append (im_list, address);
    }
   
  list = e_contact_get (contact, E_CONTACT_IM_MSN);
  for (; list != NULL; list =  g_list_next (list))
    {
      address = g_new (IMAddress , 1);
      address->service = g_strdup ("aim");
      address->address = list->data;
      im_list = g_list_append (im_list, address);
    }
  e_gw_item_set_im_list (item, im_list);
}
static void 
populate_address (EContact *contact, gpointer data)
{
  PostalAddress *address;
  EGwItem *item;
  EContactAddress *contact_addr;
  item = E_GW_ITEM (data);
   
  address = e_gw_item_get_address (item, "Home");
  contact_addr = NULL;

  if (address) {
    contact_addr = g_new0(EContactAddress, 1);
    contact_addr->address_format = "\0";
    contact_addr->po = "\0";
    contact_addr->street = "\0";
    contact_addr->locality = "\0";
    contact_addr->region = address->state;
    contact_addr->code = address->postal_code;
    contact_addr->country = address->country;
  }

  e_contact_set (contact, E_CONTACT_ADDRESS_HOME, contact_addr);
  address = e_gw_item_get_address (item, "Office");
  
  if (address) {
    
    contact_addr = g_new0(EContactAddress, 1);
    contact_addr->address_format = "";
    contact_addr->po = "";
    contact_addr->street = address->street_address;
    contact_addr->locality = address->location;
    contact_addr->region = address->state;
    contact_addr->code = address->postal_code;
    contact_addr->country = address->country;

  }
  if (contact_addr)
    e_contact_set (contact, E_CONTACT_ADDRESS_WORK, contact_addr);
}

static void 
set_address_in_gw_item (EGwItem *item, gpointer data)
{

}
static void 
populate_birth_date (EContact *contact, gpointer data)
{
  EGwItem *item;
  
  item = E_GW_ITEM (data);
  char *value ;
  EContactDate *date;
  value = e_gw_item_get_field_value (item, "birthday");
 
  
  if (value) {
    date =  e_contact_date_from_string (value);
    e_contact_set (contact, E_CONTACT_BIRTH_DATE, date);
    e_contact_date_free (date);
  }
}

static void 
set_birth_date_in_gw_item (EGwItem *item, gpointer data)
{
  EContact *contact;
  EContactDate *date;

  contact = E_CONTACT (data);
  date = e_contact_get (contact, E_CONTACT_BIRTH_DATE);
  if (date)
    e_gw_item_set_field_value (item, "birthday", e_contact_date_to_string (date));


}

static void 
populate_emails (EContact *contact, gpointer data)
{
  GList *email_list;
  EGwItem *item;
  int i;

  item = E_GW_ITEM (data);
  email_list = e_gw_item_get_email_list(item);

  for (i =0 ; i < 3; i++, email_list = g_list_next (email_list)) {
    if (email_list && email_list->data)
      e_contact_set (contact, email_fields[i], email_list->data);

  }
  
} 

static void 
set_emails_in_gw_item (EGwItem *item, gpointer data)
{
  GList *email_list;
  EContact *contact;
  char *email;
  contact = E_CONTACT (data);
  int i;

  email_list = NULL;
  for (i =0 ; i < 3; i++) {
    email = g_strdup (e_contact_get (contact, email_fields[i]));
    if(email)
      email_list = g_list_append (email_list, email);
  }
  e_gw_item_set_email_list (item, email_list);

}  

static void 
populate_full_name (EContact *contact, gpointer data)
{
  EGwItem *item;
  item = E_GW_ITEM(data);
  char *value ;
  value = e_gw_item_get_full_name (item);
  if (value) {
    
    e_contact_set (contact, E_CONTACT_FULL_NAME, value);
  }

}

static void 
set_full_name_in_gw_item (EGwItem *item, gpointer data)
{
  EContact *contact;
  char   *name;
  contact = E_CONTACT (data);
  
  name = e_contact_get (contact, E_CONTACT_FULL_NAME);
  if(name)
    e_gw_item_set_full_name (item, name);

}

static void 
populate_contact_members (EContact *contact, gpointer data)
{
  EGwItem *item;
  item = E_GW_ITEM(data);
  e_contact_set (contact, E_CONTACT_EMAIL, e_gw_item_get_member_list(item));

}
static void
set_members_in_gw_item (EGwItem  *item, gpointer data)
{
  
  EContact *contact;
  contact = E_CONTACT (data);
  GList*  members ;
  members = e_contact_get (contact, E_CONTACT_EMAIL);
  e_gw_item_set_member_list (item, members);
  
}

static void 
fill_contact_from_gw_item (EContact *contact, EGwItem *item)
{

  char* value;
  int element_type;
  int i;

  e_contact_set (contact, E_CONTACT_IS_LIST, e_gw_item_get_item_type (item) == E_GW_ITEM_TYPE_GROUP ? TRUE: FALSE);
  for ( i = 0; i < num_mappings; i++) {
    element_type = mappings[i].element_type;
    if(element_type == ELEMENT_TYPE_SIMPLE)
      {
	value = e_gw_item_get_field_value (item, mappings[i].element_name);
	if(value != NULL)
	  e_contact_set (contact, mappings[i].field_id, value);
	
      } else if (element_type == ELEMENT_TYPE_COMPLEX) {

	mappings[i].populate_contact_func(contact, item);
      }
    
  }
}

static void
e_book_backend_groupwise_create_contact (EBookBackend *backend,
					 EDataBook *book,
					 const char *vcard )
{
  EContact *contact;
  EBookBackendGroupwise *egwb;
  char *id;
  int status;
  EGwItem *item;
  int element_type;
  char* value;
  int i;

  egwb = E_BOOK_BACKEND_GROUPWISE (backend);
  contact = e_contact_new_from_vcard(vcard);
  item = e_gw_item_new_empty ();
  e_gw_item_set_item_type (item, e_contact_get (contact, E_CONTACT_IS_LIST) ? E_GW_ITEM_TYPE_GROUP :E_GW_ITEM_TYPE_CONTACT);
  e_gw_item_set_container_id (item, g_strdup(egwb->priv->container_id));

  for (i = 0; i < num_mappings; i++) {
    element_type = mappings[i].element_type;
    if (element_type == ELEMENT_TYPE_SIMPLE)  {
      value =  e_contact_get(contact, mappings[i].field_id);
      if (value != NULL)
	e_gw_item_set_field_value (item, mappings[i].element_name, value);
    } else if (element_type == ELEMENT_TYPE_COMPLEX) {
      mappings[i].set_value_in_gw_item (item, contact);
    }
     
    
  }
  status = e_gw_connection_create_item (egwb->priv->cnc, item, &id);  
  if (status == E_GW_CONNECTION_STATUS_OK) {
    e_contact_set (contact, E_CONTACT_UID, id);
    e_data_book_respond_create(book,  GNOME_Evolution_Addressbook_Success, contact);
    g_free(id);
  }
  else {
    e_data_book_respond_create(book, GNOME_Evolution_Addressbook_OtherError, NULL);
  }
  
}

static void
e_book_backend_groupwise_remove_contacts (EBookBackend *backend,
					  EDataBook    *book,
					  GList *id_list    )
{
  
  char *id;
 
  EBookBackendGroupwise *ebgw;
  GList *deleted_ids = NULL;

  ebgw = E_BOOK_BACKEND_GROUPWISE (backend);
  for ( ; id_list != NULL; id_list = g_list_next (id_list)) {
    id = (char*) id_list->data;
    if (e_gw_connection_remove_item (ebgw->priv->cnc, ebgw->priv->container_id, id) == E_GW_CONNECTION_STATUS_OK) 
      deleted_ids =  g_list_append (deleted_ids, id);
  }
  e_data_book_respond_remove_contacts (book,
				       GNOME_Evolution_Addressbook_Success,  deleted_ids);
}


static void
e_book_backend_groupwise_modify_contact (EBookBackend *backend,
					 EDataBook    *book,
					 const char *vcard    )
{	
  g_warning ("here\n");
 
	
}
static void
e_book_backend_groupwise_get_contact (EBookBackend *backend,
				      EDataBook    *book,
				      const char *id   )
{
  EBookBackendGroupwise *gwb ;
  int status ;
  EGwItem *item;
  EContact *contact;
  char *vcard;

  gwb =  E_BOOK_BACKEND_GROUPWISE (backend);
  
  status = e_gw_connection_get_item (gwb->priv->cnc, gwb->priv->container_id, id,  &item);
  if (status == E_GW_CONNECTION_STATUS_OK) {
    if (item) {
      contact = e_contact_new ();
      fill_contact_from_gw_item (contact, item);
      vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
      e_data_book_respond_get_contact (book, GNOME_Evolution_Addressbook_Success, vcard);
      g_free (vcard);
      g_object_unref (contact);
      return;
    }
    
  }
  e_data_book_respond_get_contact (book, GNOME_Evolution_Addressbook_OtherError, "");  
	
}




static void
e_book_backend_groupwise_get_contact_list (EBookBackend *backend,
					   EDataBook    *book,
					   const char *query )
{
  
  GList *vcard_list;
  int status;
  GList *gw_items;
  EContact *contact;
  EBookBackendGroupwise *gwb = E_BOOK_BACKEND_GROUPWISE (backend);
  vcard_list = NULL;
  gw_items = NULL;
  printf ("inside get contact list %s\n ", query);
  status = e_gw_connection_get_items (gwb->priv->cnc, gwb->priv->container_id, NULL, &gw_items);
  if (status != E_GW_CONNECTION_STATUS_OK) {
    e_data_book_respond_get_contact_list (book, GNOME_Evolution_Addressbook_OtherError,
					  NULL);
    return;
  }
  for (; gw_items != NULL; gw_items = g_list_next(gw_items)) { 
    contact = e_contact_new ();
    fill_contact_from_gw_item (contact, E_GW_ITEM (gw_items->data));
    vcard_list = g_list_append (vcard_list, e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30));
    g_object_unref (contact);
    
  }
  
  e_data_book_respond_get_contact_list (book, GNOME_Evolution_Addressbook_Success,
					vcard_list);
  
}

 
static void
e_book_backend_groupwise_start_book_view (EBookBackend  *backend,
					  EDataBookView *book_view)
{

  GList *contact_list;
  int status;
  GList *gw_items;
  EContact *contact;
  EBookBackendGroupwise *gwb = E_BOOK_BACKEND_GROUPWISE (backend);
  contact_list = NULL;
  gw_items = NULL;
    
  status = e_gw_connection_get_items (gwb->priv->cnc, gwb->priv->container_id, NULL, &gw_items);
    
  if (status != E_GW_CONNECTION_STATUS_OK) {
    e_data_book_view_notify_complete (book_view, GNOME_Evolution_Addressbook_OtherError);
    return;
  }
  for (; gw_items != NULL; gw_items = g_list_next(gw_items)) { 
    contact = e_contact_new ();
    fill_contact_from_gw_item (contact, E_GW_ITEM (gw_items->data));
    contact_list = g_list_append (contact_list, contact);
    e_data_book_view_notify_update (book_view, contact);
    g_object_unref(contact);
      
  }
    
  e_data_book_view_notify_complete (book_view, GNOME_Evolution_Addressbook_Success);
    
}     
  
static void
e_book_backend_groupwise_stop_book_view (EBookBackend  *backend,
					 EDataBookView *book_view)
{
  /* FIXME : provide implmentation */
}

static void
e_book_backend_groupwise_get_changes (EBookBackend *backend,
				      EDataBook    *book,
				      const char *change_id  )
{

  /* FIXME : provide implmentation */

       
}

static void
e_book_backend_groupwise_authenticate_user (EBookBackend *backend,
					    EDataBook    *book,
					    const char *user,
					    const char *passwd,
					    const char *auth_method)
{
  EBookBackendGroupwise *ebgw;
  EBookBackendGroupwisePrivate *priv;
  char *id;
  int status;
  gboolean is_writable;

  ebgw = E_BOOK_BACKEND_GROUPWISE (backend);
  priv = ebgw->priv;
  
  priv->cnc = e_gw_connection_new ("http://192.108.102.237:7181/soap", user, passwd);
  if (priv->cnc == NULL) {
    e_data_book_respond_authenticate_user (book,  GNOME_Evolution_Addressbook_OtherError);
    return;
  }

  id = NULL;
  is_writable = FALSE;
  status = e_gw_connection_get_address_book_id (priv->cnc,  priv->book_name, &id, &is_writable); 
  if (status == E_GW_CONNECTION_STATUS_OK) {
    if ( (id == NULL) && !priv->only_if_exists ) {
      
      status = e_gw_connection_create_book (priv->cnc, priv->book_name,  &id);
      if (status != E_GW_CONNECTION_STATUS_OK ) {
	e_data_book_respond_authenticate_user (book,  GNOME_Evolution_Addressbook_OtherError);
	return;
      }
     
    }

  }
  if (id != NULL) {
    priv->container_id = g_strdup (id);
    g_free(id);
    e_book_backend_set_is_writable (backend, is_writable);
    e_data_book_report_writable (book, TRUE);
    e_data_book_respond_authenticate_user (book,  GNOME_Evolution_Addressbook_Success); 
   
  } else 
    e_data_book_respond_authenticate_user (book,  GNOME_Evolution_Addressbook_OtherError);
  
}

static void
e_book_backend_groupwise_get_supported_fields (EBookBackend *backend,
					       EDataBook    *book )
{
  GList *fields = NULL;
  int i;
  
  for (i = 0; i < num_mappings ; i ++)
    fields = g_list_append (fields, g_strdup (e_contact_field_name (mappings[i].field_id)));
  fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_2)));
  fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_3)));
  fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_ICQ)));
  fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_YAHOO)));
  fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_MSN)));
  fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_JABBER)));
  e_data_book_respond_get_supported_fields (book,
					    GNOME_Evolution_Addressbook_Success,
					    fields);
 
}
  

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_groupwise_load_source (EBookBackend           *backend,
				      ESource                *source,
				      gboolean                only_if_exists)
{
  EBookBackendGroupwise *ebgw;
  EBookBackendGroupwisePrivate *priv;
  const char *book_name;
  
  ebgw = E_BOOK_BACKEND_GROUPWISE (backend);
  priv = ebgw->priv;
  priv->uri = e_source_get_uri (source);
  if(priv->uri == NULL)
    return  GNOME_Evolution_Addressbook_OtherError;
  priv->only_if_exists = only_if_exists;
  book_name = e_source_peek_name(source);
  if(book_name == NULL)
    return  GNOME_Evolution_Addressbook_OtherError;
  priv->book_name = g_strdup (book_name);
  
  e_book_backend_set_is_loaded (E_BOOK_BACKEND (backend), TRUE);
  e_book_backend_set_is_writable (E_BOOK_BACKEND(backend), FALSE);  
  return GNOME_Evolution_Addressbook_Success;
}

static void
e_book_backend_groupwise_remove (EBookBackend *backend,
				 EDataBook        *book)
{
  EBookBackendGroupwise *ebgw;
  int status;
  
  ebgw = E_BOOK_BACKEND_GROUPWISE (backend);
  status = e_gw_connection_remove_item (ebgw->priv->cnc, NULL, ebgw->priv->container_id);
  if (status == E_GW_CONNECTION_STATUS_OK) 
    e_data_book_respond_remove (book,  GNOME_Evolution_Addressbook_Success);
  else
    e_data_book_respond_remove (book,  GNOME_Evolution_Addressbook_OtherError);
    
}


static char *
e_book_backend_groupwise_get_static_capabilities (EBookBackend *backend)
{
  return g_strdup("net,bulk-removes");
}
static void 
e_book_backend_groupwise_get_supported_auth_methods (EBookBackend *backend, EDataBook *book)
{
  char *auth_method;
  GList *supported_auth_methods = NULL;
  auth_method = g_strdup_printf ("ldap/simple-binddn|%s", "Using Distinguished Name (DN)");
  supported_auth_methods = g_list_append (supported_auth_methods, auth_method);
  e_data_book_respond_get_supported_auth_methods (book,
						  GNOME_Evolution_Addressbook_Success,
						  supported_auth_methods);
  
}


/**
 * e_book_backend_groupwise_new:
 */
EBookBackend *
e_book_backend_groupwise_new (void)
{
  EBookBackendGroupwise *backend;
                                                                                                                             
  backend = g_object_new (E_TYPE_BOOK_BACKEND_GROUPWISE, NULL);
                                                                                                       
  return E_BOOK_BACKEND (backend);
}

static void
e_book_backend_groupwise_dispose (GObject *object)
{
  EBookBackendGroupwise *bgw;
                                                                                                                             
  bgw = E_BOOK_BACKEND_GROUPWISE (object);
                                                                                                                             
  if (bgw->priv) {
    if (bgw->priv->uri) {
      g_free (bgw->priv->uri);
      bgw->priv->uri = NULL;
    }
    if (bgw->priv->cnc) {
      g_object_unref (bgw->priv->cnc);
      bgw->priv->cnc = NULL;
    }
    if (bgw->priv->container_id) {
      g_free (bgw->priv->container_id);
      bgw->priv->container_id = NULL;
    }
    if (bgw->priv->book_name) {
      g_free (bgw->priv->book_name);
      bgw->priv->book_name = NULL;
    }
    g_free (bgw->priv);
    bgw->priv = NULL;
  }
                                                                                                                             
  G_OBJECT_CLASS (e_book_backend_groupwise_parent_class)->dispose (object);
}
                                                                                                                            
static void
e_book_backend_groupwise_class_init (EBookBackendGroupwiseClass *klass)
{
  

  GObjectClass  *object_class = G_OBJECT_CLASS (klass);
  EBookBackendClass *parent_class;


  e_book_backend_groupwise_parent_class = g_type_class_peek_parent (klass);

  parent_class = E_BOOK_BACKEND_CLASS (klass);

  /* Set the virtual methods. */
  parent_class->load_source             = e_book_backend_groupwise_load_source;
  parent_class->get_static_capabilities = e_book_backend_groupwise_get_static_capabilities;

  parent_class->create_contact          = e_book_backend_groupwise_create_contact;
  parent_class->remove_contacts         = e_book_backend_groupwise_remove_contacts;
  parent_class->modify_contact          = e_book_backend_groupwise_modify_contact;
  parent_class->get_contact             = e_book_backend_groupwise_get_contact;
  parent_class->get_contact_list        = e_book_backend_groupwise_get_contact_list;
  parent_class->start_book_view         = e_book_backend_groupwise_start_book_view;
  parent_class->stop_book_view          = e_book_backend_groupwise_stop_book_view;
  parent_class->get_changes             = e_book_backend_groupwise_get_changes;
  parent_class->authenticate_user       = e_book_backend_groupwise_authenticate_user;
  parent_class->get_supported_fields    = e_book_backend_groupwise_get_supported_fields;
  parent_class->get_supported_auth_methods = e_book_backend_groupwise_get_supported_auth_methods;
  parent_class->remove                  = e_book_backend_groupwise_remove;
  object_class->dispose                 = e_book_backend_groupwise_dispose;
}

static void
e_book_backend_groupwise_init (EBookBackendGroupwise *backend)
{
  EBookBackendGroupwisePrivate *priv;
                                                                                                                             
  priv= g_new0 (EBookBackendGroupwisePrivate, 1);
                                                                                                                             
  backend->priv = priv;
}


/**
 * e_book_backend_groupwise_get_type:
 */
GType
e_book_backend_groupwise_get_type (void)
{
  static GType type = 0;
                                                                                                                             
  if (! type) {
    GTypeInfo info = {
      sizeof (EBookBackendGroupwiseClass),
      NULL, /* base_class_init */
      NULL, /* base_class_finalize */
      (GClassInitFunc)  e_book_backend_groupwise_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (EBookBackendGroupwise),
      0,    /* n_preallocs */
      (GInstanceInitFunc) e_book_backend_groupwise_init
    };
                                                                                                                             
    type = g_type_register_static (E_TYPE_BOOK_BACKEND, "EBookBackendGroupwise", &info, 0);
  }
                                                                                                                             
  return type;
}

