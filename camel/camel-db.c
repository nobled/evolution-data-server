/* Srinivasa Ragavan - <sragavan@novell.com> - GPL v2 or later */

#include "camel-db.h"

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#define d(x) x

static int 
cdb_sql_exec (sqlite3 *db, const char* stmt, CamelException *ex) 
{
  	char *errmsg;
  	int   ret;

  	ret = sqlite3_exec(db, stmt, 0, 0, &errmsg);

  	if (ret != SQLITE_OK) {
    		d(g_warning ("Error in statement: %s [%s].\n", stmt, errmsg));
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
	d(g_print ("\n\aDatabase succesfully opened  \n\a"));
	return cdb;
}

void
camel_db_close (CamelDB *cdb)
{
	if (cdb) {
		sqlite3_close (cdb->db);
		g_mutex_free (cdb->lock);
		g_free (cdb);
		d(g_print ("\n\aDatabase succesfully closed \n\a"));
	}
}

gboolean
camel_db_command (CamelDB *cdb, const char *stmt, CamelException *ex)
{
	gboolean ret;
	
	if (!cdb)
		return TRUE;
	g_mutex_lock (cdb->lock);
	d(g_print("Executing: %s\n", stmt));
	ret = cdb_sql_exec (cdb->db, stmt, ex);
	g_mutex_unlock (cdb->lock);
	return ret;
}

/* We enforce it to be count and not COUNT just to speed up */
static int 
count_cb (void *data, int argc, char **argv, char **azColName)
{
  	int i;

  	for(i=0; i<argc; i++) {
		if (strstr(azColName[i], "count")) {
			*(int **)data = atol(argv[i]);
		}
  	}

  	return 0;
}

guint32
camel_db_count (CamelDB *cdb, const char *stmt)
{
	int count=0;
	char *errmsg;
	int ret;

	if (!cdb)
		return 0;
	g_mutex_lock (cdb->lock);
	ret = sqlite3_exec(cdb->db, stmt, count_cb, &count, &errmsg);
  	if(ret != SQLITE_OK) {
    		d(g_warning ("Error in select statement %s [%s].\n", stmt, errmsg));
		sqlite3_free (errmsg);
  	}
	g_mutex_unlock (cdb->lock);
	d(g_print("count of '%s' is %d\n", stmt, count));
	return count;
}


int
camel_db_select (CamelDB *cdb, const char* stmt, CamelDBSelectCB callback, gpointer data) 
{
  	char *errmsg;
  	//int nrecs = 0;
	int ret;

	if (!cdb)
		return TRUE;
	g_mutex_lock (cdb->lock);	
  	ret = sqlite3_exec(cdb->db, stmt, callback, data, &errmsg);

  	if(ret != SQLITE_OK) {
    		d(g_warning ("Error in select statement '%s' [%s].\n", stmt, errmsg));
		sqlite3_free (errmsg);
  	}
	g_mutex_unlock (cdb->lock);

	return ret;
}


gboolean
camel_db_delete_folder (CamelDB *cdb, char *folder, CamelException *ex)
{
	char *tab = g_strdup_printf ("delete from folders where folder_name ='%s'", folder);
	gboolean ret;

	ret = camel_db_command (cdb, tab, ex);
	g_free (tab);

	return ret;
}

int
camel_db_create_folders_table (CamelDB *cdb, CamelException *ex)
{
	char *query = "CREATE TABLE IF NOT EXISTS folders ( folder_name TEXT PRIMARY KEY, version REAL, flags INTEGER, nextuid INTEGER, time NUMERIC, saved_count INTEGER, unread_count INTEGER, deleted_count INTEGER, junk_count INTEGER, bdata TEXT )";

	return ((camel_db_command (cdb, query, ex)));
}

int
camel_db_write_folder_info_record (CamelDB *cdb, CamelFIRecord *record, CamelException *ex)
{

	char *upd_query;
	char *del_query;
	char *ins_query;

	ins_query = g_strdup_printf ("INSERT INTO folders VALUES ( \"%s\", %d, %d, %d, 143, %d, %d, %d, %d, \"%s\" ) ", record->folder_name, record->version, record->flags , record->nextuid , record->saved_count , record->unread_count , record->deleted_count , record->junk_count , record->bdata); 

	del_query = g_strdup_printf ("DELETE FROM folders WHERE folder_name = \"%s\"", record->folder_name);

	upd_query = g_strdup_printf ("UPDATE folders SET version = %d, flags = %d, nextuid = %d, time = 143, saved_count = %d, unread_count = %d, deleted_count = %d, junk_count = %d, bdata = %s, WHERE folder_name = \"%s\"", record->version, record->flags, record->nextuid, record->saved_count, record->unread_count, record->deleted_count, record->junk_count, "PROVIDER SPECIFIC DATA", record->folder_name );

#if 0
	camel_db_command (cdb, upd_query, ex);
#else
	camel_db_command (cdb, "BEGIN", ex);
	camel_db_command (cdb, del_query, ex);
	camel_db_command (cdb, ins_query, ex);
	camel_db_command (cdb, "COMMIT", ex);
#endif

	g_free (upd_query);
	g_free (del_query);
	g_free (ins_query);

	return 0;
}

gboolean
camel_db_delete_uid (CamelDB *cdb, char *folder, char *uid, CamelException *ex)
{
	char *tab = g_strdup_printf ("delete from %s where uid='%s'", folder, uid);
	gboolean ret;

	ret = camel_db_command (cdb, tab, ex);
	g_free (tab);

	return ret;
}
