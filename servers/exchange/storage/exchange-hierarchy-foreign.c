/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2002-2004 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* ExchangeHierarchyForeign: class for a hierarchy consisting of a
 * selected subset of folders from another user's mailbox.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-hierarchy-foreign.h"
#include "exchange-account.h"
#include "e-folder-exchange.h"
#include "e2k-propnames.h"
#include "e2k-uri.h"
#include "e2k-utils.h"
#include "exchange-config-listener.h"

#include <libedataserver/e-xml-hash-utils.h>
#include <libedataserver/e-source-list.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct _ExchangeHierarchyForeignPrivate {
	GMutex *hide_private_lock;
	gboolean checked_hide_private;
};

extern const char *exchange_localfreebusy_path;

#define PARENT_TYPE EXCHANGE_TYPE_HIERARCHY_SOMEDAV
static ExchangeHierarchySomeDAVClass *parent_class = NULL;

static GPtrArray *get_hrefs (ExchangeHierarchySomeDAV *hsd);
static ExchangeAccountFolderResult create_folder (ExchangeHierarchy *hier,
						  EFolder *parent,
						  const char *name,
						  const char *type);
static ExchangeAccountFolderResult remove_folder (ExchangeHierarchy *hier,
						  EFolder *folder);
static ExchangeAccountFolderResult scan_subtree (ExchangeHierarchy *hier,
						 EFolder *folder,
						 gboolean offline);
static void finalize (GObject *object);

static void
class_init (GObjectClass *object_class)
{
	ExchangeHierarchyClass *hierarchy_class =
		EXCHANGE_HIERARCHY_CLASS (object_class);
	ExchangeHierarchySomeDAVClass *somedav_class =
		EXCHANGE_HIERARCHY_SOMEDAV_CLASS (object_class);

	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->finalize = finalize;

	hierarchy_class->create_folder  = create_folder;
	hierarchy_class->remove_folder  = remove_folder;
	hierarchy_class->scan_subtree   = scan_subtree;

	somedav_class->get_hrefs        = get_hrefs;
}

static void
init (GObject *object)
{
	ExchangeHierarchyForeign *hfor = EXCHANGE_HIERARCHY_FOREIGN (object);
	ExchangeHierarchy *hier = EXCHANGE_HIERARCHY (object);

	hfor->priv = g_new0 (ExchangeHierarchyForeignPrivate, 1);
	hfor->priv->hide_private_lock = g_mutex_new ();
	hier->hide_private_items = TRUE;
}

static void
finalize (GObject *object)
{
	ExchangeHierarchyForeign *hfor = EXCHANGE_HIERARCHY_FOREIGN (object);

	g_mutex_free (hfor->priv->hide_private_lock);
	g_free (hfor->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

E2K_MAKE_TYPE (exchange_hierarchy_foreign, ExchangeHierarchyForeign, class_init, init, PARENT_TYPE)

static const char *privacy_props[] = {
	PR_DELEGATES_ENTRYIDS,
	PR_DELEGATES_SEE_PRIVATE,
};
static const int n_privacy_props = sizeof (privacy_props) / sizeof (privacy_props[0]);

static void
check_hide_private (ExchangeHierarchy *hier)
{
	ExchangeHierarchyForeign *hfor = EXCHANGE_HIERARCHY_FOREIGN (hier);
	E2kContext *ctx;
	E2kHTTPStatus status;
	E2kResult *results;
	int nresults, i;
	GPtrArray *entryids, *privflags;
	GByteArray *entryid;
	const char *my_dn, *delegate_dn;
	char *uri;

	g_mutex_lock (hfor->priv->hide_private_lock);

	if (hfor->priv->checked_hide_private) {
		g_mutex_unlock (hfor->priv->hide_private_lock);
		return;
	}

	uri = e2k_uri_concat (hier->account->home_uri,
			      "NON_IPM_SUBTREE/Freebusy%20Data/LocalFreebusy.EML");
	ctx = exchange_account_get_context (hier->account);

	status = e2k_context_propfind (ctx, NULL, uri,
				       privacy_props, n_privacy_props,
				       &results, &nresults);
	g_free (uri);

	hfor->priv->checked_hide_private = TRUE;

	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status) || nresults == 0) {
		g_mutex_unlock (hfor->priv->hide_private_lock);
		return;
	}
	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (results[0].status) ||
	    !results[0].props || nresults > 1) {
		e2k_results_free (results, nresults);
		g_mutex_unlock (hfor->priv->hide_private_lock);
		return;
	}

	entryids  = e2k_properties_get_prop (results[0].props,
					     PR_DELEGATES_ENTRYIDS);
	privflags = e2k_properties_get_prop (results[0].props,
					     PR_DELEGATES_SEE_PRIVATE);
	if (entryids && privflags) {
		my_dn = hier->account->legacy_exchange_dn;
		for (i = 0; i < entryids->len && i < privflags->len; i++) {
			entryid = entryids->pdata[i];
			delegate_dn = e2k_entryid_to_dn (entryid);

			if (delegate_dn &&
			    !g_ascii_strcasecmp (delegate_dn, my_dn) &&
			    privflags->pdata[i] &&
			    atoi (privflags->pdata[i]))
				hier->hide_private_items = FALSE;
			break;
		}
	}

	e2k_results_free (results, nresults);
	g_mutex_unlock (hfor->priv->hide_private_lock);
}

static void
remove_all_cb (ExchangeHierarchy *hier, EFolder *folder, gpointer user_data)
{
	exchange_hierarchy_removed_folder (hier, folder);
}

static void
hierarchy_foreign_cleanup (ExchangeHierarchy *hier)
{
	char *mf_path;

	exchange_hierarchy_webdav_offline_scan_subtree (hier, remove_all_cb,
							NULL);

	mf_path = e_folder_exchange_get_storage_file (hier->toplevel, "hierarchy.xml");
	unlink (mf_path);
	g_free (mf_path);

	exchange_hierarchy_removed_folder (hier, hier->toplevel);
}

static const char *folder_props[] = {
	E2K_PR_EXCHANGE_FOLDER_CLASS,
	E2K_PR_HTTPMAIL_UNREAD_COUNT,
	E2K_PR_DAV_DISPLAY_NAME,
	PR_ACCESS
};
static const int n_folder_props = sizeof (folder_props) / sizeof (folder_props[0]);

static ExchangeAccountFolderResult
find_folder (ExchangeHierarchy *hier, const char *uri, EFolder **folder_out)
{
	ExchangeHierarchyWebDAV *hwd = EXCHANGE_HIERARCHY_WEBDAV (hier);
	E2kContext *ctx = exchange_account_get_context (hier->account);
	E2kHTTPStatus status;
	E2kResult *results;
	int nresults;
	EFolder *folder;
	const char *access;

	status = e2k_context_propfind (ctx, NULL, uri,
				       folder_props, n_folder_props,
				       &results, &nresults);
	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) 
		return exchange_hierarchy_webdav_status_to_folder_result (status);
	if (nresults == 0)
		return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;

	access = e2k_properties_get_prop (results[0].props, PR_ACCESS);
	if (!access || !atoi (access))
		return EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED;

	folder = exchange_hierarchy_webdav_parse_folder (hwd, hier->toplevel,
							 &results[0]);
	e2k_results_free (results, nresults);

	if (!folder)
		return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;

	exchange_hierarchy_new_folder (hier, folder);
	if (folder_out)
		*folder_out = folder;
	else
		g_object_unref (folder);
	return EXCHANGE_ACCOUNT_FOLDER_OK;
}

static struct {
	const char *name, *prop;
} std_folders[] = {
	{ N_("Calendar"),	E2K_PR_STD_FOLDER_CALENDAR },
	{ N_("Contacts"),	E2K_PR_STD_FOLDER_CONTACTS },
	{ N_("Deleted Items"),	E2K_PR_STD_FOLDER_DELETED_ITEMS },
	{ N_("Drafts"),		E2K_PR_STD_FOLDER_DRAFTS },
	{ N_("Inbox"),		E2K_PR_STD_FOLDER_INBOX },
	{ N_("Journal"),	E2K_PR_STD_FOLDER_JOURNAL },
	{ N_("Notes"),		E2K_PR_STD_FOLDER_NOTES },
	{ N_("Outbox"),		E2K_PR_STD_FOLDER_OUTBOX },
	{ N_("Sent Items"),	E2K_PR_STD_FOLDER_SENT_ITEMS },
	{ N_("Tasks"),		E2K_PR_STD_FOLDER_TASKS }
};
const static int n_std_folders = sizeof (std_folders) / sizeof (std_folders[0]);
static ExchangeAccountFolderResult
create_internal (ExchangeHierarchy *hier, EFolder *parent,
		 const char *name, const char *type, EFolder **folder_out)
{
	ExchangeAccountFolderResult result;
	char *literal_uri = NULL, *standard_uri = NULL;
	const char *home_uri;
	int i;

	/* For now, no nesting */
	if (parent != hier->toplevel || strchr (name + 1, '/'))
		return EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR;

	check_hide_private (hier);

	home_uri = e_folder_exchange_get_internal_uri (hier->toplevel);
	literal_uri = e2k_uri_concat (home_uri, name);
	if (exchange_account_get_folder (hier->account, literal_uri)) {
		g_free (literal_uri);
		if (exchange_hierarchy_is_empty (hier))
			hierarchy_foreign_cleanup (hier);
		return EXCHANGE_ACCOUNT_FOLDER_ALREADY_EXISTS;
	}

	for (i = 0; i < n_std_folders; i++) {
		if (g_ascii_strcasecmp (std_folders[i].name, name) != 0 &&
		    g_utf8_collate (_(std_folders[i].name), name) != 0)
			continue;

		standard_uri = exchange_account_get_standard_uri_for (
			hier->account, home_uri, std_folders[i].prop);
		if (!standard_uri)
			break;
		if (!strcmp (literal_uri, standard_uri)) {
			g_free (standard_uri);
			standard_uri = NULL;
			break;
		}

		if (exchange_account_get_folder (hier->account, standard_uri)) {
			g_free (standard_uri);
			g_free (literal_uri);
			if (exchange_hierarchy_is_empty (hier))
				hierarchy_foreign_cleanup (hier);
			return EXCHANGE_ACCOUNT_FOLDER_ALREADY_EXISTS;
		}

		break;
	}

	result = find_folder (hier, literal_uri, folder_out);
	if (result == EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST && standard_uri)
		result = find_folder (hier, standard_uri, folder_out);

	g_free (literal_uri);
	g_free (standard_uri);

	/* If the hierarchy is now empty, then we must have just been
	 * created but then the add failed. So remove it again.
	 */
	if (exchange_hierarchy_is_empty (hier))
		hierarchy_foreign_cleanup (hier);

	return result;
}


static ExchangeAccountFolderResult
create_folder (ExchangeHierarchy *hier, EFolder *parent,
	       const char *name, const char *type)
{
	return create_internal (hier, parent, name, type, NULL);
}

static ExchangeAccountFolderResult
remove_folder (ExchangeHierarchy *hier, EFolder *folder)
{
	ESourceList *cal_source_list, *task_source_list, *cont_source_list;
	const char *folder_type, *physical_uri;

	/* Temp Fix for remove fav folders. see #59168 */
        /* remove ESources */
        folder_type = e_folder_get_type_string (folder);
        physical_uri = e_folder_get_physical_uri (folder);

        if (strcmp (folder_type, "calendar") == 0) {
                cal_source_list = e_source_list_new_for_gconf (
                                        gconf_client_get_default (),
                                        CONF_KEY_CAL);
                remove_esource (hier->account, EXCHANGE_CALENDAR_FOLDER, 
				physical_uri, &cal_source_list, FALSE);
                e_source_list_sync (cal_source_list, NULL);
                g_object_unref (cal_source_list);
        }
        else if (strcmp (folder_type, "tasks") == 0) {
                task_source_list = e_source_list_new_for_gconf (
                                        gconf_client_get_default (),
                                        CONF_KEY_TASKS);
                remove_esource (hier->account, EXCHANGE_TASKS_FOLDER, 
				physical_uri, &task_source_list, FALSE);
                e_source_list_sync (task_source_list, NULL);
                g_object_unref (task_source_list);
        }
        else if (strcmp (folder_type, "contacts") == 0) {
                cont_source_list = e_source_list_new_for_gconf (
                                        gconf_client_get_default (),
                                        CONF_KEY_CONTACTS);
                remove_esource (hier->account, EXCHANGE_CONTACTS_FOLDER, 
				physical_uri, &cont_source_list, FALSE);
                e_source_list_sync (cont_source_list, NULL);
                g_object_unref (cont_source_list);
	}

	if (folder != hier->toplevel)
		exchange_hierarchy_removed_folder (hier, folder);

	if (folder == hier->toplevel || exchange_hierarchy_is_empty (hier))
		hierarchy_foreign_cleanup (hier);

	return EXCHANGE_ACCOUNT_FOLDER_OK;
}

static ExchangeAccountFolderResult
scan_subtree (ExchangeHierarchy *hier, EFolder *folder, gboolean offline)
{
	ExchangeAccountFolderResult folder_result;

	check_hide_private (hier);

	folder_result = EXCHANGE_HIERARCHY_CLASS (parent_class)->scan_subtree (hier, folder, offline);

	if (exchange_hierarchy_is_empty (hier))
		hierarchy_foreign_cleanup (hier);

	return folder_result;
}

static void
add_href (ExchangeHierarchy *hier, EFolder *folder, gpointer hrefs)
{
	char *uri = g_strdup (e_folder_exchange_get_internal_uri (folder));

	g_ptr_array_add (hrefs, (gpointer) uri);
}

static GPtrArray *
get_hrefs (ExchangeHierarchySomeDAV *hsd)
{
	GPtrArray *hrefs;

	hrefs = g_ptr_array_new ();
	exchange_hierarchy_webdav_offline_scan_subtree (EXCHANGE_HIERARCHY (hsd), add_href, hrefs);
	return hrefs;
}

/**
 * exchange_hierarchy_foreign_add_folder:
 * @hier: the hierarchy
 * @folder_name: the name of the folder to add
 * @folder: on successful return, the created folder
 *
 * Adds a new folder to @hier.
 *
 * Return value: the folder result.
 **/
ExchangeAccountFolderResult
exchange_hierarchy_foreign_add_folder (ExchangeHierarchy *hier,
				       const char *folder_name,
				       EFolder **folder)
{
	ExchangeAccountFolderResult result;
	const char *folder_type = NULL;
	const char *physical_uri = NULL;
	char *new_folder_name;
	ESourceList *cal_source_list, *task_source_list, *cont_source_list;

	result =  create_internal (hier, hier->toplevel, folder_name, NULL, folder);

	if (result == EXCHANGE_ACCOUNT_FOLDER_OK) {
		// Add the esources
		folder_type = e_folder_get_type_string (*folder);
		physical_uri = e_folder_get_physical_uri (*folder);
		new_folder_name = g_strdup_printf("%s's %s", 
					hier->owner_name, folder_name);

		if (!(strcmp (folder_type, "calendar")) ||
		!(strcmp (folder_type, "calendar/public"))) {
			cal_source_list = e_source_list_new_for_gconf (
						gconf_client_get_default (),
						CONF_KEY_CAL);
			add_esource (hier->account,
				     EXCHANGE_CALENDAR_FOLDER,
				     new_folder_name,
				     physical_uri,
				     &cal_source_list);
			e_source_list_sync (cal_source_list, NULL);
			g_object_unref (cal_source_list);

		}
		else if (!(strcmp (folder_type, "tasks")) ||
			 !(strcmp (folder_type, "tasks/public"))) {
				task_source_list = e_source_list_new_for_gconf (
						gconf_client_get_default (),
						CONF_KEY_TASKS);
				add_esource (hier->account,
				     EXCHANGE_TASKS_FOLDER,
				     new_folder_name,
				     physical_uri,
				     &task_source_list);
				e_source_list_sync (task_source_list, NULL);
				g_object_unref (task_source_list);

		}
		else if (!(strcmp (folder_type, "contacts")) ||
			 !(strcmp (folder_type, "contacts/public")) ||
			 !(strcmp (folder_type, "contacts/ldap"))) {
				cont_source_list = e_source_list_new_for_gconf (
						gconf_client_get_default (),
						CONF_KEY_CONTACTS);

				add_esource (hier->account,
				     EXCHANGE_CONTACTS_FOLDER,
				     new_folder_name,
				     physical_uri,
				     &cont_source_list);
				e_source_list_sync (cont_source_list, NULL);
				g_object_unref (cont_source_list);
		}
		g_free (new_folder_name);
	}

	return result;
}

static ExchangeHierarchy *
hierarchy_foreign_new (ExchangeAccount *account,
		       const char *hierarchy_name,
		       const char *physical_uri_prefix,
		       const char *internal_uri_prefix,
		       const char *owner_name,
		       const char *owner_email,
		       const char *source_uri)
{
	ExchangeHierarchyForeign *hfor;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);

	hfor = g_object_new (EXCHANGE_TYPE_HIERARCHY_FOREIGN, NULL);

	exchange_hierarchy_webdav_construct (EXCHANGE_HIERARCHY_WEBDAV (hfor),
					     account,
					     EXCHANGE_HIERARCHY_FOREIGN,
					     hierarchy_name,
					     physical_uri_prefix,
					     internal_uri_prefix,
					     owner_name, owner_email,
					     source_uri,
					     FALSE);

	return EXCHANGE_HIERARCHY (hfor);
}

/**
 * exchange_hierarchy_foreign_new:
 * @account: an #ExchangeAccount
 * @hierarchy_name: the name of the hierarchy
 * @physical_uri_prefix: the prefix of physical URIs in this hierarchy
 * @internal_uri_prefix: the prefix of internal (http) URIs in this hierarchy
 * @owner_name: display name of the owner of the hierarchy
 * @owner_email: email address of the owner of the hierarchy
 * @source_uri: evolution-mail source uri for the hierarchy
 *
 * Creates a new (initially empty) hierarchy for another user's
 * folders.
 *
 * Return value: the new hierarchy.
 **/
ExchangeHierarchy *
exchange_hierarchy_foreign_new (ExchangeAccount *account,
				const char *hierarchy_name,
				const char *physical_uri_prefix,
				const char *internal_uri_prefix,
				const char *owner_name,
				const char *owner_email,
				const char *source_uri)
{
	ExchangeHierarchy *hier;
	char *mf_path;
	GHashTable *props;
	xmlDoc *doc;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);
	
	hier = hierarchy_foreign_new (account, hierarchy_name,
				      physical_uri_prefix,
				      internal_uri_prefix,
				      owner_name, owner_email,
				      source_uri);

	props = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (props, "name", (char *)hierarchy_name);
	g_hash_table_insert (props, "physical_uri_prefix",
			     (char *)physical_uri_prefix);
	g_hash_table_insert (props, "internal_uri_prefix",
			     (char *)internal_uri_prefix);
	g_hash_table_insert (props, "owner_name", (char *)owner_name);
	g_hash_table_insert (props, "owner_email", (char *)owner_email);
	g_hash_table_insert (props, "source_uri", (char *)source_uri);

	mf_path = e_folder_exchange_get_storage_file (hier->toplevel, "hierarchy.xml");
	doc = e_xml_from_hash (props, E_XML_HASH_TYPE_PROPERTY,
			       "foreign-hierarchy");
	xmlSaveFile (mf_path, doc);
	g_hash_table_destroy (props);
	g_free (mf_path);
	xmlFreeDoc (doc);

	return hier;
}

/**
 * exchange_hierarchy_foreign_new_from_dir:
 * @account: an #ExchangeAccount
 * @folder_path: pathname to a directory containing a hierarchy.xml file
 *
 * Recreates a new hierarchy from saved values.
 *
 * Return value: the new hierarchy.
 **/
ExchangeHierarchy *
exchange_hierarchy_foreign_new_from_dir (ExchangeAccount *account,
					 const char *folder_path)
{
	ExchangeHierarchy *hier;
	char *mf_path;
	GHashTable *props;
	xmlDoc *doc;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);
	g_return_val_if_fail (folder_path != NULL, NULL);

	mf_path = g_build_filename (folder_path, "hierarchy.xml", NULL);
	doc = xmlParseFile (mf_path);
	g_free (mf_path);
	if (!doc)
		return NULL;

	props = e_xml_to_hash (doc, E_XML_HASH_TYPE_PROPERTY);
	xmlFreeDoc (doc);

	hier = hierarchy_foreign_new (account,
				      g_hash_table_lookup (props, "name"),
				      g_hash_table_lookup (props, "physical_uri_prefix"),
				      g_hash_table_lookup (props, "internal_uri_prefix"),
				      g_hash_table_lookup (props, "owner_name"),
				      g_hash_table_lookup (props, "owner_email"),
				      g_hash_table_lookup (props, "source_uri"));

	e_xml_destroy_hash (props);
	return hier;
}
