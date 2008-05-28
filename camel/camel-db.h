/* Srinivasa Ragavan - <sragavan@novell.com> - GPL v2 or later */

#ifndef __CAMEL_DB_H
#define __CAMEL_DB_H
#include <sqlite3.h>
#include <glib.h>
#define CAMEL_DB_FILE "folders.db"

#include "camel-exception.h"

struct _CamelDB {
	sqlite3 *db;
	GMutex *lock;
	GMutex *read_lock;
};


/* The extensive DB format, supporting basic searching and sorting
  uid, - Message UID
  flags, - Camel Message info flags
  unread/read, - boolean read/unread status
  deleted, - boolean deleted status
  replied, - boolean replied status
  imp, - boolean important status
  junk, - boolean junk status
  size, - size of the mail
  attachment, boolean attachment status
  dsent, - sent date
  dreceived, - received date
  subject, - subject of the mail
  from, - sender
  to, - recipient
  cc, - CC members
  mlist, - message list headers
  follow-up-flag, - followup flag / also can be queried to see for followup or not
  completed-on-set, - completed date, can be used to see if completed
  due-by,  - to see the due by date
  Location - This can be derived from the database location/name. No need to store.
  label, - labels of mails also called as userflags
  usertags, composite string of user tags
  cinfo, content info string - composite string
  bdata, provider specific data
  part, part/references/thread id
*/

typedef struct _CamelMIRecord {
	char *uid;
	guint32 flags;
	gboolean read;
	gboolean deleted;
	gboolean replied;
	gboolean important;
	gboolean junk;
	gboolean attachment;
	guint32 size;
	time_t dsent;
	time_t dreceived;
	char *subject;
	char *from;
	char *to;
	char *cc;
	char *mlist;
	char *followup_flag;
	char *followup_completed_on;
	char *followup_due_by;
	char *part;
	char *labels;
	char *usertags;
	char *cinfo;
	char *bdata;
} CamelMIRecord;

typedef struct _CamelFIRecord {
	char *folder_name;
	guint32 version;
	guint32 flags;
	guint32 nextuid;
	time_t time;
	guint32 saved_count;
	/* Are these three really required? Can we just query it*/
	guint32 unread_count;
	guint32 deleted_count;
	guint32 junk_count;
	char *bdata;
} CamelFIRecord;




typedef struct _CamelDB CamelDB;
typedef int (*CamelDBSelectCB) (void *data, int ncol, char **colvalues, char **colnames);


CamelDB * camel_db_open (const char *path, CamelException *ex);
void camel_db_close (CamelDB *cdb);
int camel_db_command (CamelDB *cdb, const char *stmt, CamelException *ex);

int camel_db_transaction_command (CamelDB *cdb, GSList *qry_list, CamelException *ex);

int camel_db_begin_transaction (CamelDB *cdb, CamelException *ex);
int camel_db_add_to_transaction (CamelDB *cdb, const char *query, CamelException *ex);
int camel_db_end_transaction (CamelDB *cdb, CamelException *ex);
int camel_db_abort_transaction (CamelDB *cdb, CamelException *ex);

gboolean camel_db_delete_folder (CamelDB *cdb, char *folder, CamelException *ex);
gboolean camel_db_delete_uid (CamelDB *cdb, char *folder, char *uid, CamelException *ex);

int camel_db_create_folders_table (CamelDB *cdb, CamelException *ex);
int camel_db_select (CamelDB *cdb, const char* stmt, CamelDBSelectCB callback, gpointer data, CamelException *ex);

int camel_db_write_folder_info_record (CamelDB *cdb, CamelFIRecord *record, CamelException *ex);
int camel_db_read_folder_info_record (CamelDB *cdb, char *folder_name, CamelFIRecord **record, CamelException *ex);

int camel_db_write_message_info_record (CamelDB *cdb, const char *folder_name, CamelMIRecord *record, CamelException *ex);
guint32 camel_db_count (CamelDB *cdb, const char *stmt);
#endif

