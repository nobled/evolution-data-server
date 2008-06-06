/* Srinivasa Ragavan - <sragavan@novell.com> - GPL v2 or later */

#include "camel-db.h"

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#define d(x) 

static int 
cdb_sql_exec (sqlite3 *db, const char* stmt, CamelException *ex) 
{
  	char *errmsg;
  	int   ret;

  	ret = sqlite3_exec(db, stmt, 0, 0, &errmsg);
	d(g_print("%s\n", stmt));
  	if (ret != SQLITE_OK) {
    		d(g_print ("Error in SQL EXEC statement: %s [%s].\n", stmt, errmsg));
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _(errmsg));
		sqlite3_free (errmsg);
		return -1;
 	}
	return 0;
}

CamelDB *
camel_db_open (const char *path, CamelException *ex)
{
	CamelDB *cdb;
	sqlite3 *db;
	int ret;

	ret = sqlite3_open(path, &db);
	if (ret) {

		if (!db) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Insufficient memory"));
		} else {
			const char *error;
			error = sqlite3_errmsg (db);
			d(g_print("Can't open database %s: %s\n", path, error));
			camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _(error));
			sqlite3_close(db);
		}
		return NULL;
	}

	cdb = g_new (CamelDB, 1);
	cdb->db = db;
	cdb->lock = g_mutex_new ();
	d(g_print ("\nDatabase succesfully opened  \n"));
	return cdb;
}

void
camel_db_close (CamelDB *cdb)
{
	if (cdb) {
		sqlite3_close (cdb->db);
		g_mutex_free (cdb->lock);
		g_free (cdb);
		d(g_print ("\nDatabase succesfully closed \n"));
	}
}

/* Should this be really exposed ? */
int
camel_db_command (CamelDB *cdb, const char *stmt, CamelException *ex)
{
	int ret;
	
	if (!cdb)
		return TRUE;
	g_mutex_lock (cdb->lock);
	d(g_print("Executing: %s\n", stmt));
	ret = cdb_sql_exec (cdb->db, stmt, ex);
	g_mutex_unlock (cdb->lock);
	return ret;
}


int 
camel_db_begin_transaction (CamelDB *cdb, CamelException *ex)
{
	if (!cdb)
		return -1;

	d(g_print ("\n\aBEGIN TRANSACTION \n\a"));
	g_mutex_lock (cdb->lock);
	return (cdb_sql_exec (cdb->db, "BEGIN", ex));
}

int 
camel_db_end_transaction (CamelDB *cdb, CamelException *ex)
{
	int ret;
	if (!cdb)
		return -1;

	d(g_print ("\nCOMMIT TRANSACTION \n"));
	ret = cdb_sql_exec (cdb->db, "COMMIT", ex);
	g_mutex_unlock (cdb->lock);

	return ret;
}

int
camel_db_abort_transaction (CamelDB *cdb, CamelException *ex)
{
	int ret;
	
	d(g_print ("\nABORT TRANSACTION \n"));
	ret = cdb_sql_exec (cdb->db, "ROLLBACK", ex);
	g_mutex_unlock (cdb->lock);

	return ret;
}

int
camel_db_add_to_transaction (CamelDB *cdb, const char *stmt, CamelException *ex)
{
	if (!cdb)
		return -1;

	d(g_print("Adding the following query to transaction: %s\n", stmt));

	return (cdb_sql_exec (cdb->db, stmt, ex));
}

int 
camel_db_transaction_command (CamelDB *cdb, GSList *qry_list, CamelException *ex)
{
	int ret;
	const char *query;

	if (!cdb)
		return -1;

	g_mutex_lock (cdb->lock);

	ret = cdb_sql_exec (cdb->db, "BEGIN", ex);
	if (ret)
		goto end;

	d(g_print ("\nBEGIN Transaction\n"));

	while (qry_list) {
		query = qry_list->data;
		d(g_print ("\nInside Transaction: [%s] \n", query));
		ret = cdb_sql_exec (cdb->db, query, ex);
		if (ret)
			goto end;
		qry_list = g_slist_next (qry_list);
	}

	ret = cdb_sql_exec (cdb->db, "COMMIT", ex);

end:
	g_mutex_unlock (cdb->lock);
	d(g_print ("\nTransaction Result: [%d] \n", ret));
	return ret;
}

static int 
count_cb (void *data, int argc, char **argv, char **azColName)
{
  	int i;

  	for(i=0; i<argc; i++) {
		if (strstr(azColName[i], "COUNT")) {
			*(guint32 *)data = argv [i] ? strtoul (argv [i], NULL, 10) : 0;
		}
  	}

  	return 0;
}

static int
camel_db_count_message_info (CamelDB *cdb, const char *query, guint32 *count, CamelException *ex)
{
	int ret;
	char *errmsg;

	ret = sqlite3_exec (cdb->db, query, count_cb, count, &errmsg);
	if (ret != SQLITE_OK) {
    		g_print ("Error in SQL SELECT statement: %s [%s]\n", query, errmsg);
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _(errmsg));
		sqlite3_free (errmsg);
 	}
	return ret;
}

int
camel_db_count_junk_message_info (CamelDB *cdb, const char *table_name, guint32 *count, CamelException *ex)
{
	int ret;

	if (!cdb)
		return -1;

	char *query;
	query = sqlite3_mprintf ("SELECT COUNT (junk) FROM %Q WHERE junk = 1", table_name);

	ret = camel_db_count_message_info (cdb, query, count, ex);
	sqlite3_free (query);

	return ret;
}

int
camel_db_count_unread_message_info (CamelDB *cdb, const char *table_name, guint32 *count, CamelException *ex)
{
	int ret;

	if (!cdb)
		return -1;

	char *query;
	query = sqlite3_mprintf ("SELECT COUNT (read) FROM %Q WHERE read = 0", table_name);

	ret = camel_db_count_message_info (cdb, query, count, ex);
	sqlite3_free (query);

	return ret;
}


int
camel_db_count_deleted_message_info (CamelDB *cdb, const char *table_name, guint32 *count, CamelException *ex)
{
	int ret;

	if (!cdb)
		return -1;

	char *query ;
	query = sqlite3_mprintf ("SELECT COUNT (deleted) FROM %Q WHERE deleted = 1", table_name);

	ret = camel_db_count_message_info (cdb, query, count, ex);
	sqlite3_free (query);

	return ret;
}


int
camel_db_count_total_message_info (CamelDB *cdb, const char *table_name, guint32 *count, CamelException *ex)
{

	int ret;
	char *query;

	if (!cdb)
		return -1;
	
	query = sqlite3_mprintf ("SELECT COUNT (uid) FROM %Q", table_name);

	ret = camel_db_count_message_info (cdb, query, count, ex);
	sqlite3_free (query);

	return ret;
}

int
camel_db_select (CamelDB *cdb, const char* stmt, CamelDBSelectCB callback, gpointer data, CamelException *ex) 
{
  	char *errmsg;
  	//int nrecs = 0;
	int ret;

	if (!cdb)
		return TRUE;
  	ret = sqlite3_exec(cdb->db, stmt, callback, data, &errmsg);

  	if (ret != SQLITE_OK) {
    		d(g_warning ("Error in select statement '%s' [%s].\n", stmt, errmsg));
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, errmsg);
		sqlite3_free (errmsg);
  	}

	return ret;
}

int
camel_db_delete_folder (CamelDB *cdb, const char *folder, CamelException *ex)
{
	char *tab = sqlite3_mprintf ("DELETE FROM folders WHERE folder_name =%Q", folder);
	int ret;

	ret = camel_db_command (cdb, tab, ex);
	sqlite3_free (tab);

	return ret;
}

int
camel_db_create_folders_table (CamelDB *cdb, CamelException *ex)
{
	char *query = "CREATE TABLE IF NOT EXISTS folders ( folder_name TEXT PRIMARY KEY, version REAL, flags INTEGER, nextuid INTEGER, time NUMERIC, saved_count INTEGER, unread_count INTEGER, deleted_count INTEGER, junk_count INTEGER, bdata TEXT )";

	return ((camel_db_command (cdb, query, ex)));
}

int 
camel_db_prepare_message_info_table (CamelDB *cdb, const char *folder_name, CamelException *ex)
{
	int ret;
	char *table_creation_query;
	
	table_creation_query = sqlite3_mprintf ("CREATE TABLE IF NOT EXISTS %Q (  uid TEXT PRIMARY KEY , flags INTEGER , read INTEGER , deleted INTEGER , replied INTEGER , important INTEGER , junk INTEGER , attachment INTEGER , size INTEGER , dsent NUMERIC , dreceived NUMERIC , subject TEXT , mail_from TEXT , mail_to TEXT , mail_cc TEXT , mlist TEXT , followup_flag TEXT , followup_completed_on TEXT , followup_due_by TEXT , part TEXT , labels TEXT , usertags TEXT , cinfo TEXT , bdata TEXT )", folder_name);

	ret = camel_db_add_to_transaction (cdb, table_creation_query, ex);

	sqlite3_free (table_creation_query);
	return ret;
}

int
camel_db_write_message_info_record (CamelDB *cdb, const char *folder_name, CamelMIRecord *record, CamelException *ex)
{
	int ret;
	char *del_query;
	char *ins_query;

	ins_query = sqlite3_mprintf ("INSERT INTO %Q VALUES (%Q, %d, %d, %d, %d, %d, %d, %d, %d, %ld, %ld, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q )", 
			folder_name, record->uid, record->flags,
			record->read, record->deleted, record->replied,
			record->important, record->junk, record->attachment,
			record->size, record->dsent, record->dreceived,
			record->subject, record->from, record->to,
			record->cc, record->mlist, record->followup_flag,
			record->followup_completed_on, record->followup_due_by, 
			record->part, record->labels, record->usertags,
			record->cinfo, record->bdata);

	del_query = sqlite3_mprintf ("DELETE FROM %Q WHERE uid = %Q", folder_name, record->uid);

#if 0
	char *upd_query;

	upd_query = g_strdup_printf ("IMPLEMENT AND THEN TRY");
	camel_db_command (cdb, upd_query, ex);
	g_free (upd_query);
#else

	ret = camel_db_add_to_transaction (cdb, del_query, ex);
	ret = camel_db_add_to_transaction (cdb, ins_query, ex);

#endif

	sqlite3_free (del_query);
	sqlite3_free (ins_query);

	return ret;
}

int
camel_db_write_folder_info_record (CamelDB *cdb, CamelFIRecord *record, CamelException *ex)
{
	int ret;

	char *del_query;
	char *ins_query;

	ins_query = sqlite3_mprintf ("INSERT INTO folders VALUES ( %Q, %d, %d, %d, %d, %d, %d, %d, %d, %Q ) ", 
			record->folder_name, record->version,
								 record->flags, record->nextuid, record->time,
			record->saved_count, record->unread_count,
			record->deleted_count, record->junk_count, record->bdata); 

	del_query = sqlite3_mprintf ("DELETE FROM folders WHERE folder_name = %Q", record->folder_name);


#if 0
	char *upd_query;
	
	upd_query = g_strdup_printf ("UPDATE folders SET version = %d, flags = %d, nextuid = %d, time = 143, saved_count = %d, unread_count = %d, deleted_count = %d, junk_count = %d, bdata = %s, WHERE folder_name = %Q", record->version, record->flags, record->nextuid, record->saved_count, record->unread_count, record->deleted_count, record->junk_count, "PROVIDER SPECIFIC DATA", record->folder_name );
	camel_db_command (cdb, upd_query, ex);
	g_free (upd_query);
#else

	ret = camel_db_add_to_transaction (cdb, del_query, ex);
	ret = camel_db_add_to_transaction (cdb, ins_query, ex);

#endif

	sqlite3_free (del_query);
	sqlite3_free (ins_query);

	return ret;
}

static int 
read_fir_callback (void * ref, int ncol, char ** cols, char ** name)
{
	CamelFIRecord *record = *(CamelFIRecord **) ref;

	d(g_print ("\nread_fir_callback called \n"));
#if 0
	record->folder_name = cols [0];
	record->version = cols [1];
	/* Just a sequential mapping of struct members to columns is enough I guess. 
	Needs some checking */
#else
	int i;
	
	for (i = 0; i < ncol; ++i) {
		if (!strcmp (name [i], "folder_name"))
			record->folder_name = g_strdup(cols [i]);

		else if (!strcmp (name [i], "version"))
			record->version = cols [i] ? strtoul (cols [i], NULL, 10) : 0;

		else if (!strcmp (name [i], "flags"))
			record->flags = cols [i] ? strtoul (cols [i], NULL, 10) : 0;

		else if (!strcmp (name [i], "nextuid"))
			record->nextuid = cols [i] ? strtoul (cols [i], NULL, 10) : 0;

		else if (!strcmp (name [i], "time"))
			record->time = cols [i] ? strtoul (cols [i], NULL, 10) : 0;

		else if (!strcmp (name [i], "saved_count"))
			record->saved_count = cols [i] ? strtoul (cols [i], NULL, 10) : 0;

		else if (!strcmp (name [i], "unread_count"))
			record->unread_count = cols [i] ? strtoul (cols [i], NULL, 10) : 0;

		else if (!strcmp (name [i], "deleted_count"))
			record->deleted_count = cols [i] ? strtoul (cols [i], NULL, 10) : 0;

		else if (!strcmp (name [i], "junk_count"))
			record->junk_count = cols [i] ? strtoul (cols [i], NULL, 10) : 0;

		else if (!strcmp (name [i], "bdata"))
			record->bdata = g_strdup (cols [i]);
	
	}
#endif 
	return 0;
}

int
camel_db_read_folder_info_record (CamelDB *cdb, const char *folder_name, CamelFIRecord **record, CamelException *ex)
{
	char *query;
	int ret;

	d(g_print ("\ncamel_db_read_folder_info_record called \n"));

	query = sqlite3_mprintf ("SELECT * FROM folders WHERE folder_name = %Q", folder_name);
	ret = camel_db_select (cdb, query, read_fir_callback, record, ex);

	sqlite3_free (query);
	return (ret);
}

int
camel_db_read_message_info_record_with_uid (CamelDB *cdb, const char *folder_name, const char *uid, gpointer **p, CamelDBSelectCB read_mir_callback, CamelException *ex)
{
	char *query;
	int ret;

	query = sqlite3_mprintf ("SELECT * FROM %Q WHERE uid = %Q", folder_name, uid);
	ret = camel_db_select (cdb, query, read_mir_callback, p, ex);
	sqlite3_free (query);

	return (ret);
}

int
camel_db_read_message_info_records (CamelDB *cdb, const char *folder_name, gpointer **p, CamelDBSelectCB read_mir_callback, CamelException *ex)
{
	char *query;
	int ret;

	query = sqlite3_mprintf ("SELECT * FROM %Q ", folder_name);
	ret = camel_db_select (cdb, query, read_mir_callback, p, ex);
	sqlite3_free (query);

	return (ret);
}

int
camel_db_delete_uid (CamelDB *cdb, const char *folder, const char *uid, CamelException *ex)
{
	char *tab = sqlite3_mprintf ("DELETE FROM %Q WHERE uid = %Q", folder, uid);
	int ret;

	ret = camel_db_command (cdb, tab, ex);
	sqlite3_free (tab);

	return ret;
}

int
camel_db_delete_uids (CamelDB *cdb, const char * folder_name, GSList *uids, CamelException *ex)
{
	char *tmp;
	int ret;
	gboolean first = TRUE;
	GString *str = g_string_new ("DELETE FROM ");
	GSList *iterator;

	tmp = sqlite3_mprintf ("%Q WHERE uid IN (", folder_name); 
	g_string_append_printf (str, "%s ", tmp);
	sqlite3_free (tmp);

	iterator = uids;

	while (iterator) {
		tmp = sqlite3_mprintf ("%Q", (char *) iterator->data);
		iterator = iterator->next;

		if (first == TRUE) {
			g_string_append_printf (str, " %s ", tmp);
			first = FALSE;
		} else
			g_string_append_printf (str, ", %s ", tmp);

		sqlite3_free (tmp);
	}

	g_string_append (str, ")");

	ret = camel_db_command (cdb, str->str, ex);

	g_string_free (str, TRUE);

	return ret;
}

int
camel_db_clear_folder_summary (CamelDB *cdb, char *folder, CamelException *ex)
{
	int ret;

	char *folders_del;
	char *msginfo_del;

	folders_del = sqlite3_mprintf ("DELETE FROM folders WHERE folder_name = %Q", folder);
	msginfo_del = sqlite3_mprintf ("DELETE FROM %Q ", folder);

	camel_db_begin_transaction (cdb, ex);
	camel_db_add_to_transaction (cdb, msginfo_del, ex);
	camel_db_add_to_transaction (cdb, folders_del, ex);
	ret = camel_db_end_transaction (cdb, ex);

	sqlite3_free (folders_del);
	sqlite3_free (msginfo_del);

	return ret;
}



void
camel_db_camel_mir_free (CamelMIRecord *record)
{
	if (record) {
		g_free (record->uid);
		g_free (record->subject);
		g_free (record->from);
		g_free (record->to);
		g_free (record->cc);
		g_free (record->mlist);
		g_free (record->followup_flag);
		g_free (record->followup_completed_on);
		g_free (record->followup_due_by);
		g_free (record->part);
		g_free (record->labels);
		g_free (record->usertags);
		g_free (record->cinfo);
		g_free (record->bdata);

		g_free (record);
	}
}
