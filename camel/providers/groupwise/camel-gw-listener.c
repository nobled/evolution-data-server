/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  
 *  Sivaiah Nallagatla <snallagatla@novell.com>
 *
 * Copyright 2003, Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "camel-gw-listener.h"
#include <string.h>
#include  "camel-i18n.h"
#include <e-gw-connection.h>
#include <e-passwords.h>
#include "widgets/misc/e-error.h"

/*stores some info about all currently existing groupwise accounts 
  list of GwAccountInfo structures */

static 	GList *groupwise_accounts = NULL;

struct _CamelGwListenerPrivate {
	GConfClient *gconf_client;
	/* we get notification about mail account changes form this object */
	EAccountList *account_list;                  
};

struct _GwAccountInfo {
	char *uid;
	char *name;
	char *source_url;
};

typedef struct _GwAccountInfo GwAccountInfo;

#define GROUPWISE_URI_PREFIX   "groupwise://" 
#define GROUPWISE_PREFIX_LENGTH 12

#define PARENT_TYPE G_TYPE_OBJECT

static GObjectClass *parent_class = NULL;

static void dispose (GObject *object);
static void finalize (GObject *object);


static void 
camel_gw_listener_class_init (CamelGwListenerClass *class)
{
	GObjectClass *object_class;
	
	parent_class =  g_type_class_ref (PARENT_TYPE);
	object_class = G_OBJECT_CLASS (class);
	
	/* virtual method override */
	object_class->dispose = dispose;
	object_class->finalize = finalize;
}

static void 
camel_gw_listener_init (CamelGwListener *config_listener,  CamelGwListenerClass *class)
{
	printf("||| Camel GW Listener Init |||\n") ;
	config_listener->priv = g_new0 (CamelGwListenerPrivate, 1);	
}

static void 
dispose (GObject *object)
{
	CamelGwListener *config_listener = CAMEL_GW_LISTENER (object);
	
	g_object_unref (config_listener->priv->gconf_client);
	g_object_unref (config_listener->priv->account_list);

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void 
finalize (GObject *object)
{
	CamelGwListener *config_listener = CAMEL_GW_LISTENER (object);
	GList *list;
	GwAccountInfo *info;

	if (config_listener->priv) {
		g_free (config_listener->priv);
	}

	for ( list = g_list_first (groupwise_accounts); list ; list = g_list_next (list) ) {
	       
		info = (GwAccountInfo *) (list->data);

		if (info) {
			
			g_free (info->uid);
			g_free (info->name);
			g_free (info->source_url);
			g_free (info);
		}
	}
	
	g_list_free (groupwise_accounts);
}

/*determines whehter the passed in account is groupwise or not by looking at source url */

static gboolean
is_groupwise_account (EAccount *account)
{
	if (account->source->url != NULL) {
		return (strncmp (account->source->url,  GROUPWISE_URI_PREFIX, GROUPWISE_PREFIX_LENGTH ) == 0);
	} else {
		return FALSE;
	}
}

/* looks up for an existing groupwise account info in the groupwise_accounts list based on uid */

static GwAccountInfo* 
lookup_account_info (const char *key)
{
	GList *list;
        GwAccountInfo *info ;
	int found = 0;
                                                                      
        if (!key)
                return NULL;

	info = NULL;

        for (list = g_list_first (groupwise_accounts);  list;  list = g_list_next (list)) {
                info = (GwAccountInfo *) (list->data);
                found = (strcmp (info->uid, key) == 0);
		if (found)
			break;
	}
	if (found)
		return info;
	return NULL;
}

#define CALENDAR_SOURCES "/apps/evolution/calendar/sources"
#define TASKS_SOURCES "/apps/evolution/tasks/sources"
#define SELECTED_CALENDARS "/apps/evolution/calendar/display/selected_calendars"
#define SELECTED_TASKS   "/apps/evolution/calendar/tasks/selected_tasks"

static void
add_esource (const char *conf_key, const char *group_name,  const char* source_name, const char *username, const char* relative_uri, const char *soap_port, const char *use_ssl)
{
	ESourceList *source_list;
	ESourceGroup *group;
	ESource *source;
        GConfClient* client;
	GSList *ids, *temp ;
	char *source_selection_key;

	client = gconf_client_get_default();	
	source_list = e_source_list_new_for_gconf (client, conf_key);

	group = e_source_group_new (group_name,  GROUPWISE_URI_PREFIX);
	if ( !e_source_list_add_group (source_list, group, -1))
		return;

	source = e_source_new (source_name, relative_uri);
	e_source_set_property (source, "auth", "1");
	e_source_set_property (source, "username", username);
	e_source_set_property (source, "port", soap_port);
	e_source_set_property (source, "auth-domain", "Groupwise");
	e_source_set_property (source, "use_ssl", use_ssl);
	e_source_group_add_source (group, source, -1);
	e_source_list_sync (source_list, NULL);

	if (!strcmp (conf_key, CALENDAR_SOURCES)) 
		source_selection_key = SELECTED_CALENDARS;
	else if (!strcmp (conf_key, TASKS_SOURCES))
		source_selection_key = SELECTED_TASKS;
	else source_selection_key = NULL;
	if (source_selection_key) {
		ids = gconf_client_get_list (client, source_selection_key , GCONF_VALUE_STRING, NULL);
		ids = g_slist_append (ids, g_strdup (e_source_peek_uid (source)));
		gconf_client_set_list (client,  source_selection_key, GCONF_VALUE_STRING, ids, NULL);
		temp  = ids;
		for (; temp != NULL; temp = g_slist_next (temp))
			g_free (temp->data);
		g_slist_free (ids);
	}
	
	g_object_unref (source);
	g_object_unref (group);
	g_object_unref (source_list);
	g_object_unref (client);
}


static void 
remove_esource (const char *conf_key, const char *group_name, char* source_name, const char* relative_uri)
{
	ESourceList *list;
        ESourceGroup *group;
        ESource *source;
        GSList *groups;
        GSList *sources;
	gboolean found_group;
	GConfClient* client;
	GSList *ids;
	GSList *node_tobe_deleted;
	char *source_selection_key;
                                                                                                                             
        client = gconf_client_get_default();
        list = e_source_list_new_for_gconf (client, conf_key);
	groups = e_source_list_peek_groups (list); 
	
	found_group = FALSE;
	
	for ( ; groups != NULL && !found_group; groups = g_slist_next (groups)) {

		group = E_SOURCE_GROUP (groups->data);
		
		if (strcmp (e_source_group_peek_name (group), group_name) == 0 && 
		   strcmp (e_source_group_peek_base_uri (group), GROUPWISE_URI_PREFIX ) == 0) {

			sources = e_source_group_peek_sources (group);
			
			for( ; sources != NULL; sources = g_slist_next (sources)) {
				
				source = E_SOURCE (sources->data);
				
				if (strcmp (e_source_peek_relative_uri (source), relative_uri) == 0) {
				
					if (!strcmp (conf_key, CALENDAR_SOURCES)) 
						source_selection_key = SELECTED_CALENDARS;
					else if (!strcmp (conf_key, TASKS_SOURCES))
						source_selection_key = SELECTED_TASKS;
					else source_selection_key = NULL;
					if (source_selection_key) {
						ids = gconf_client_get_list (client, source_selection_key , 
									     GCONF_VALUE_STRING, NULL);
						node_tobe_deleted = g_slist_find_custom (ids, e_source_peek_uid (source), (GCompareFunc) strcmp);
						if (node_tobe_deleted) {
							g_free (node_tobe_deleted->data);
							ids = g_slist_delete_link (ids, node_tobe_deleted);
						}
						gconf_client_set_list (client,  source_selection_key, 
								       GCONF_VALUE_STRING, ids, NULL);

					}
					e_source_list_remove_group (list, group);
					e_source_list_sync (list, NULL);	
					found_group = TRUE;
					break;
					
				}
			}

		}
		
	      
	}

	g_object_unref (list);
	g_object_unref (client);		
	
}

/* looks up for e-source with having same info as old_account_info and changes its values passed in new values */

static void 
modify_esource (const char* conf_key, GwAccountInfo *old_account_info, const char* new_group_name, const char *username, const char* new_relative_uri, const char *soap_port, const char *use_ssl)
{
	ESourceList *list;
        ESourceGroup *group;
        ESource *source;
        GSList *groups;
	GSList *sources;
	char *old_relative_uri;
	CamelURL *url;
	gboolean found_group;
      	GConfClient* client;
	const char *poa_address;

	url = camel_url_new (old_account_info->source_url, NULL);
	poa_address = camel_url_get_param (url, "poa");
	if (!poa_address || strlen (poa_address) ==0)
		return;
	old_relative_uri =  g_strdup_printf ("%s@%s/", url->user, poa_address);
	client = gconf_client_get_default ();
        list = e_source_list_new_for_gconf (client, conf_key);
	groups = e_source_list_peek_groups (list); 

	found_group = FALSE;

	for ( ; groups != NULL &&  !found_group; groups = g_slist_next (groups)) {

		group = E_SOURCE_GROUP (groups->data);
		
		if (strcmp (e_source_group_peek_name (group), old_account_info->name) == 0 && 
		    strcmp (e_source_group_peek_base_uri (group), GROUPWISE_URI_PREFIX) == 0) {

			sources = e_source_group_peek_sources (group);
			
			for ( ; sources != NULL; sources = g_slist_next (sources)) {
				
				source = E_SOURCE (sources->data);
				
				if (strcmp (e_source_peek_relative_uri (source), old_relative_uri) == 0) {
					
					e_source_group_set_name (group, new_group_name);
					e_source_set_relative_uri (source, new_relative_uri);
					e_source_set_property (source, "username", username);
					e_source_set_property (source, "port", soap_port);
					e_source_set_property (source, "use_ssl", use_ssl);		
					e_source_list_sync (list, NULL);
					found_group = TRUE;
					break;
				}
			}
		}
	}

	g_object_unref (list);
	g_object_unref (client);
	camel_url_free (url);
	g_free (old_relative_uri);
	
}
/* add sources for calendar and tasks if the account added is groupwise account
   adds the new account info to  groupwise_accounts list */

static void 
add_calendar_tasks_sources (GwAccountInfo *info)
{
	CamelURL *url;
	char *relative_uri;
	const char *soap_port;
	const char * use_ssl;
	const char *poa_address;
	
	url = camel_url_new (info->source_url, NULL);
	poa_address = camel_url_get_param (url, "poa");
	if (!poa_address || strlen (poa_address) ==0)
		return;
	soap_port = camel_url_get_param (url, "soap_port");

 	if (!soap_port || strlen (soap_port) == 0)
		soap_port = "7181";

	use_ssl = camel_url_get_param (url, "soap_ssl");
	if (use_ssl)
		use_ssl = "always";
	else 
		use_ssl = NULL;

	relative_uri =  g_strdup_printf ("%s@%s/", url->user, poa_address);
	add_esource ("/apps/evolution/calendar/sources", info->name, _("Calendar"), url->user, relative_uri, soap_port, use_ssl);
	add_esource ("/apps/evolution/tasks/sources", info->name, _("Tasks"), url->user,  relative_uri, soap_port, use_ssl);
	
	camel_url_free (url);
	g_free (relative_uri);

}

/* removes calendar and tasks sources if the account removed is groupwise account 
   removes the the account info from groupwise_account list */

static void 
remove_calendar_tasks_sources (GwAccountInfo *info)
{
	CamelURL *url;
	char *relative_uri;
        const char *soap_port;
	const char *poa_address;

	url = camel_url_new (info->source_url, NULL);

	poa_address = camel_url_get_param (url, "poa");
	if (!poa_address || strlen (poa_address) ==0)
		return;

	soap_port = camel_url_get_param (url, "soap_port");
	if (!soap_port || strlen (soap_port) == 0)
		soap_port = "7181";

	relative_uri =  g_strdup_printf ("%s@%s/", url->user, poa_address);
	remove_esource ("/apps/evolution/calendar/sources", info->name, _("Calendar"), relative_uri);
	remove_esource ("/apps/evolution/tasks/sources", info->name,  _("Checklist"), relative_uri);
	camel_url_free (url);
	g_free (relative_uri);

}

static GList*
get_addressbook_names_from_server (char *source_url)
{
  	char *key;
        EGwConnection *cnc;	
	char *password;
	GList *book_list;
	int status;
	const char *soap_port;
	CamelURL *url;
	gboolean remember;
	char *failed_auth; 
	char *prompt;
	char *uri;
	const char *use_ssl;
	const char *poa_address;
	guint32 flags = E_PASSWORDS_REMEMBER_FOREVER;

	url = camel_url_new (source_url, NULL);
        if (url == NULL) {
                return NULL;
        }
	poa_address = camel_url_get_param (url, "poa");
	if (!poa_address || strlen (poa_address) ==0)
		return NULL;
	
        soap_port = camel_url_get_param (url, "soap_port");
        if (!soap_port || strlen (soap_port) == 0)
                soap_port = "7181";
	use_ssl = camel_url_get_param (url, "soap_ssl");
	if(use_ssl)
		use_ssl = "always";
	key =  g_strdup_printf ("groupwise://%s@%s/", url->user, poa_address); 
	if (use_ssl)
		uri = g_strdup_printf ("https://%s:%s/soap", poa_address, soap_port);
	else 
		uri = g_strdup_printf ("http://%s:%s/soap", poa_address, soap_port);
	
	failed_auth = "";
	cnc = NULL;
        do {
		prompt = g_strdup_printf (_("%sEnter password for %s (user %s)"),
                                          failed_auth, poa_address, url->user);
		
		password = e_passwords_ask_password (prompt, "Groupwise", key, prompt,
                                                     E_PASSWORDS_REMEMBER_FOREVER|E_PASSWORDS_SECRET, &remember,
						     NULL);
		g_free (prompt);
		
		if (!password) 
			break;
		cnc = e_gw_connection_new (uri, url->user, password);
		failed_auth = _("Failed to authenticate.\n");
		flags |= E_PASSWORDS_REPROMPT;
	} while (cnc == NULL);
	
	if (E_IS_GW_CONNECTION(cnc))  {
		book_list = NULL;	
		status = e_gw_connection_get_address_book_list (cnc, &book_list);
		if (status == E_GW_CONNECTION_STATUS_OK)
			return book_list;
	    
		       
	}
	e_error_run (NULL, "mail:gw-accountsetup-error", poa_address, NULL);
	return NULL;
}
                                                                                                         

static gboolean
add_addressbook_sources (EAccount *account)
{
	CamelURL *url;
	ESourceList *list;
        ESourceGroup *group;
        ESource *source;
       	char *base_uri;
	const char *soap_port;
	GList *books_list, *temp_list;
	GConfClient* client;
	const char* use_ssl;
	const char *poa_address;


        url = camel_url_new (account->source->url, NULL);
	if (url == NULL) {
		return FALSE;
	}

	poa_address = camel_url_get_param (url, "poa");
	if (!poa_address || strlen (poa_address) ==0)
		return FALSE;

	soap_port = camel_url_get_param (url, "soap_port");
	if (!soap_port || strlen (soap_port) == 0)
		soap_port = "7181";
	use_ssl = camel_url_get_param (url, "soap_ssl");
	base_uri =  g_strdup_printf ("groupwise://%s@%s", url->user, poa_address);
	client = gconf_client_get_default ();
	list = e_source_list_new_for_gconf (client, "/apps/evolution/addressbook/sources" );
	group = e_source_group_new (account->name, base_uri);
	books_list = get_addressbook_names_from_server (account->source->url);
	temp_list = books_list;
	if (!temp_list)
		return FALSE;
	for (; temp_list != NULL; temp_list = g_list_next (temp_list)) {
		const char *book_name =  e_gw_container_get_name (E_GW_CONTAINER(temp_list->data));
		source = e_source_new (book_name, g_strconcat (";",book_name, NULL));
		e_source_set_property (source, "auth", "plain/password");
		e_source_set_property (source, "auth-domain", "Groupwise");
		e_source_set_property (source, "port", soap_port);
		e_source_set_property(source, "user", url->user);
		
		if (!e_gw_container_get_is_writable (E_GW_CONTAINER(temp_list->data)))
			e_source_set_property (source, "completion", "true");
		if (e_gw_container_get_is_frequent_contacts (E_GW_CONTAINER(temp_list->data)))
			e_source_set_property (source, "completion", "true");
		e_source_set_property (source, "use_ssl", use_ssl);
		e_source_group_add_source (group, source, -1);
		g_object_unref (source);
		g_object_unref (E_GW_CONTAINER(temp_list->data));
		
	}
		
	g_list_free (books_list);			

      
	e_source_list_add_group (list, group, -1);
      	e_source_list_sync (list, NULL);	
	g_object_unref (group);
	g_object_unref (list);
	g_object_unref (client);
	g_free (base_uri);
	
	return TRUE;
}

static void 
modify_addressbook_sources ( EAccount *account, GwAccountInfo *existing_account_info )
{
	CamelURL *url;
	ESourceList *list;
        ESourceGroup *group;
	GSList *groups;
       	gboolean found_group;
	gboolean delete_group;
	char *old_base_uri;
	char *new_base_uri;
	const char *soap_port;
	const char *use_ssl;
	GSList *sources;
	ESource *source;
	GConfClient *client;
	const char *poa_address;

	url = camel_url_new (existing_account_info->source_url, NULL);
	if (url == NULL) {
		return;
	}

	poa_address = camel_url_get_param (url, "poa");
	if (!poa_address || strlen (poa_address) ==0)
		return;

	old_base_uri =  g_strdup_printf ("groupwise://%s@%s", url->user, poa_address);
	camel_url_free (url);
	
	url = camel_url_new (account->source->url, NULL);
	if (url == NULL)
		return ;
	poa_address = camel_url_get_param (url, "poa");
	if (!poa_address || strlen (poa_address) ==0)
		return;
	new_base_uri = g_strdup_printf ("groupwise://%s@%s", url->user, poa_address);
	soap_port = camel_url_get_param (url, "soap_port");
	if (!soap_port || strlen (soap_port) == 0)
		soap_port = "7181";
	use_ssl = camel_url_get_param (url, "soap_ssl");

	client = gconf_client_get_default ();
	list = e_source_list_new_for_gconf (client, "/apps/evolution/addressbook/sources" );
	groups = e_source_list_peek_groups (list); 
	delete_group = FALSE;
	if (strcmp (old_base_uri, new_base_uri) != 0)
		delete_group = TRUE;
	group = NULL;
	found_group = FALSE;
	for ( ; groups != NULL &&  !found_group; groups = g_slist_next (groups)) {

		group = E_SOURCE_GROUP (groups->data);
		if ( strcmp ( e_source_group_peek_base_uri(group), old_base_uri) == 0 && strcmp (e_source_group_peek_name (group), existing_account_info->name) == 0) {
			found_group = TRUE;
			if (!delete_group) {
				e_source_group_set_name (group, account->name);
				sources = e_source_group_peek_sources (group);
				for (; sources != NULL; sources = g_slist_next (sources)) {
					source = E_SOURCE (sources->data);
					e_source_set_property (source, "port", soap_port);
					e_source_set_property (source, "use_ssl", use_ssl);
				}
					
				e_source_list_sync (list, NULL);
			}
		
		}
	}
	if (found_group && delete_group) {
		e_source_list_remove_group (list, group);
		e_source_list_sync (list, NULL);
		g_object_unref (list);
		list = NULL;
		add_addressbook_sources (account);
	}
	g_free (old_base_uri);
	if (list)
		g_object_unref (list);
	camel_url_free (url);
	g_object_unref (client);


}

static void 
remove_addressbook_sources (GwAccountInfo *existing_account_info)
{
	ESourceList *list;
        ESourceGroup *group;
	GSList *groups;
       	gboolean found_group;
	CamelURL *url;
	char *base_uri;
	const char *soap_port;
	GConfClient *client;
	const char *poa_address;

	url = camel_url_new (existing_account_info->source_url, NULL);
	if (url == NULL) {
		return;
	}

	poa_address = camel_url_get_param (url, "poa");
	if (!poa_address || strlen (poa_address) ==0)
		return;

	soap_port = camel_url_get_param (url, "soap_port");
	if (!soap_port || strlen (soap_port) == 0)
		soap_port = "7181";
	base_uri =  g_strdup_printf ("groupwise://%s@%s", url->user,  poa_address);
	client = gconf_client_get_default ();
	list = e_source_list_new_for_gconf (client, "/apps/evolution/addressbook/sources" );
	groups = e_source_list_peek_groups (list); 

	found_group = FALSE;

	for ( ; groups != NULL &&  !found_group; groups = g_slist_next (groups)) {

		group = E_SOURCE_GROUP (groups->data);
		if ( strcmp ( e_source_group_peek_base_uri (group), base_uri) == 0 && strcmp (e_source_group_peek_name (group), existing_account_info->name) == 0) {

			e_source_list_remove_group (list, group);
			e_source_list_sync (list, NULL);
			found_group = TRUE;
						
		}
	}
	g_object_unref (list);
	g_object_unref (client);
	g_free (base_uri);
	camel_url_free (url);
	

}



static void 
account_added (EAccountList *account_listener, EAccount *account)
{

	GwAccountInfo *info;
	gboolean status;

	if (!is_groupwise_account (account))
		return;
	
	info = g_new0 (GwAccountInfo, 1);
	info->uid = g_strdup (account->uid);
	info->name = g_strdup (account->name);
	info->source_url = g_strdup (account->source->url);
	status = add_addressbook_sources (account);
	if (status) 
		add_calendar_tasks_sources (info);
	groupwise_accounts = g_list_append (groupwise_accounts, info);

}

static void 
account_removed (EAccountList *account_listener, EAccount *account)
{
       	GwAccountInfo *info;
	
	if (!is_groupwise_account (account))
		return;
	
	info = lookup_account_info (account->uid);
	if (info == NULL) {
		return;
	}

	remove_calendar_tasks_sources (info);
	remove_addressbook_sources (info);
	groupwise_accounts = g_list_remove (groupwise_accounts, info);
	g_free (info->uid);
	g_free (info->name);
	g_free (info->source_url);
        g_free (info);


}


static void
account_changed (EAccountList *account_listener, EAccount *account)
{
	gboolean is_gw_account;
	CamelURL *old_url, *new_url;
	char *relative_uri;
	const char *old_soap_port, *new_soap_port;
	GwAccountInfo *existing_account_info;
	const char *old_use_ssl, *new_use_ssl;
	gboolean old_ssl, new_ssl;
	const char *old_poa_address, *new_poa_address;
	
	is_gw_account = is_groupwise_account (account);
	
	existing_account_info = lookup_account_info (account->uid);
       
	if (existing_account_info == NULL && is_gw_account) {

		if (!account->enabled)
			return;

		/* some account of other type is changed to Groupwise */
		account_added (account_listener, account);

	} else if ( existing_account_info != NULL && !is_gw_account) {

		/*Groupwise account is changed to some other type */
		remove_calendar_tasks_sources (existing_account_info);
		remove_addressbook_sources (existing_account_info);
		groupwise_accounts = g_list_remove (groupwise_accounts, existing_account_info);
		g_free (existing_account_info->uid);
		g_free (existing_account_info->name);
		g_free (existing_account_info->source_url);
		g_free (existing_account_info);
		
	} else if ( existing_account_info != NULL && is_gw_account ) {
		
		if (!account->enabled) {
			account_removed (account_listener, account);
			return;
		}
		old_ssl = new_ssl = FALSE;
		/* some info of groupwise account is changed . update the sources with new info if required */
		old_url = camel_url_new (existing_account_info->source_url, NULL);
		old_poa_address = camel_url_get_param (old_url, "poa");
		old_soap_port = camel_url_get_param (old_url, "soap_port");
		old_use_ssl = camel_url_get_param (old_url, "soap_ssl");
		if (old_use_ssl)
			old_ssl = TRUE;
		new_url = camel_url_new (account->source->url, NULL);

		new_poa_address = camel_url_get_param (new_url, "poa");
		if (!new_poa_address || strlen (new_poa_address) ==0)
			return;
		new_soap_port = camel_url_get_param (new_url, "soap_port");
		if (!new_soap_port || strlen (new_soap_port) == 0)
			new_soap_port = "7181";

		new_use_ssl = camel_url_get_param (new_url, "soap_ssl");
		if (new_use_ssl){
			new_use_ssl = "always";
			new_ssl = TRUE;
		}
			
		if ((old_poa_address && strcmp (old_poa_address, new_poa_address))
		   ||  (old_soap_port && strcmp (old_soap_port, new_soap_port)) 
		   ||  strcmp (old_url->user, new_url->user) 
		   || ( old_ssl ^ new_ssl)) {
			
			account_removed (account_listener, account);
			account_added (account_listener, account);
		} else if (strcmp (existing_account_info->name, account->name)) {
			
			relative_uri =  g_strdup_printf ("%s@%s/", new_url->user, new_poa_address); 
			modify_esource ("/apps/evolution/calendar/sources", existing_account_info, account->name, new_url->user, relative_uri, new_soap_port, new_use_ssl);
			modify_esource ("/apps/evolution/tasks/sources", existing_account_info, account->name, new_url->user, relative_uri, new_soap_port, new_use_ssl);
			modify_addressbook_sources (account, existing_account_info);
			g_free (relative_uri);
			
		}
		
		g_free (existing_account_info->name);
		g_free (existing_account_info->source_url);
		existing_account_info->name = g_strdup (account->name);
		existing_account_info->source_url = g_strdup (account->source->url);
		camel_url_free (old_url);
		camel_url_free (new_url);
	}
		
	
} 



static void
camel_gw_listener_construct (CamelGwListener *config_listener)
{
	EIterator *iter;
	EAccount *account;
	GwAccountInfo *info ;
	
       	config_listener->priv->account_list = e_account_list_new (config_listener->priv->gconf_client);

	for ( iter = e_list_get_iterator (E_LIST ( config_listener->priv->account_list) ) ; e_iterator_is_valid (iter); e_iterator_next (iter) ) {
		
		account = E_ACCOUNT (e_iterator_get (iter));
		if ( is_groupwise_account (account) && account->enabled) {
			
		        info = g_new0 (GwAccountInfo, 1);
			info->uid = g_strdup (account->uid);
			info->name = g_strdup (account->name);
			info->source_url = g_strdup (account->source->url);
			groupwise_accounts = g_list_append (groupwise_accounts, info);
			
		}
			
	}
	g_signal_connect (config_listener->priv->account_list, "account_added", G_CALLBACK (account_added), NULL);
	g_signal_connect (config_listener->priv->account_list, "account_changed", G_CALLBACK (account_changed), NULL);
	g_signal_connect (config_listener->priv->account_list, "account_removed", G_CALLBACK (account_removed), NULL);
	
       
}

GType
camel_gw_listener_get_type (void)
{
	static GType camel_gw_listener_type  = 0;

	if (!camel_gw_listener_type) {
		static GTypeInfo info = {
                        sizeof (CamelGwListenerClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) camel_gw_listener_class_init,
                        NULL, NULL,
                        sizeof (CamelGwListener),
                        0,
                        (GInstanceInitFunc) camel_gw_listener_init
                };
		camel_gw_listener_type = g_type_register_static (PARENT_TYPE, "CamelGwListener", &info, 0);
	}

	return camel_gw_listener_type;
}

CamelGwListener*
camel_gw_listener_new ()
{
	CamelGwListener *config_listener;
       
	config_listener = g_object_new (CAMEL_TYPE_GW_LISTENER, NULL);
	config_listener->priv->gconf_client = gconf_client_get_default();
	
	camel_gw_listener_construct (config_listener);

	return config_listener;

}


