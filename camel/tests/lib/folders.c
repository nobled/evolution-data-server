
#include "camel-test.h"
#include "folders.h"
#include "messages.h"

#include "camel/camel-exception.h"

/* check the total/unread is what we think it should be */
void
test_folder_counts(CamelFolder *folder, int total, int unread)
{
	GPtrArray *s;
	int i, myunread;
	CamelMessageInfo *info;

	push("test folder counts %d total %d unread", total, unread);

	/* first, use the standard functions */
	check(camel_folder_get_message_count(folder) == total);
	check(camel_folder_get_unread_message_count(folder) == total);

	/* next, use the summary */
	s = camel_folder_get_summary(folder);
	check(s != NULL);
	check(s->len == total);
	myunread = s->len;
	for (i=0;i<s->len;i++) {
		info = s->pdata[i];
		if (info->flags & CAMEL_MESSAGE_SEEN)
			myunread--;
	}
	check(unread == myunread);
	camel_folder_free_summary(folder, s);

	/* last, use the uid list */
	s = camel_folder_get_uids(folder);
	check(s != NULL);
	check(s->len == total);
	myunread = s->len;
	for (i=0;i<s->len;i++) {
		info = camel_folder_get_message_info(folder, s->pdata[i]);
		if (info->flags & CAMEL_MESSAGE_SEEN)
			myunread--;
		camel_folder_free_message_info(folder, info);
	}
	check(unread == myunread);
	camel_folder_free_uids(folder, s);

	pull();
}

static int
safe_strcmp(const char *a, const char *b)
{
	if (a == NULL && b == NULL)
		return 0;
	if (a == NULL)
		return 1;
	if (b == NULL)
		return -1;
	return strcmp(a, b);
}

void
test_message_info(CamelMimeMessage *msg, const CamelMessageInfo *info)
{
	check_msg(safe_strcmp(camel_message_info_subject(info), camel_mime_message_get_subject(msg)) == 0,
		  "info->subject = '%s', get_subject() = '%s'", camel_message_info_subject(info), camel_mime_message_get_subject(msg));

	/* FIXME: testing from/cc/to, etc is more tricky */

	check(info->date_sent == camel_mime_message_get_date(msg, NULL));

	/* date received isn't set for messages that haven't been sent anywhere ... */
	/*check(info->date_received == camel_mime_message_get_date_received(msg, NULL));*/

	/* so is messageid/references, etc */
}

/* check a message is present */
void
test_folder_message(CamelFolder *folder, const char *uid)
{
	CamelMimeMessage *msg;
	CamelMessageInfo *info;
	GPtrArray *s;
	int i;
	CamelException *ex = camel_exception_new();
	int found;

	push("uid %s is in folder", uid);

	/* first try getting info */
	info = camel_folder_get_message_info(folder, uid);
	check(info != NULL);
	check(strcmp(camel_message_info_uid(info), uid) == 0);
	camel_folder_free_message_info(folder, info);

	/* then, getting message */
	msg = camel_folder_get_message(folder, uid, ex);
	check_msg(!camel_exception_is_set(ex), "%s", camel_exception_get_description(ex));
	check(msg != NULL);

	/* cross check with info */
	test_message_info(msg, info);

	camel_object_unref((CamelObject *)msg);

	/* see if it is in the summary (only once) */
	s = camel_folder_get_summary(folder);
	check(s != NULL);
	found = 0;
	for (i=0;i<s->len;i++) {
		info = s->pdata[i];
		if (strcmp(camel_message_info_uid(info), uid) == 0)
			found++;
	}
	check(found == 1);
	camel_folder_free_summary(folder, s);

	/* check it is in the uid list */
	s = camel_folder_get_uids(folder);
	check(s != NULL);
	found = 0;
	for (i=0;i<s->len;i++) {
		if (strcmp(s->pdata[i], uid) == 0)
			found++;
	}
	check(found == 1);
	camel_folder_free_uids(folder, s);

	camel_exception_free(ex);

	pull();
}

/* check message not present */
void
test_folder_not_message(CamelFolder *folder, const char *uid)
{
	CamelMimeMessage *msg;
	CamelMessageInfo *info;
	GPtrArray *s;
	int i;
	CamelException *ex = camel_exception_new();
	int found;

	push("uid %s is not in folder", uid);

	/* first try getting info */
	info = camel_folder_get_message_info(folder, uid);
	check(info == NULL);

	/* then, getting message */
	msg = camel_folder_get_message(folder, uid, ex);
	check(camel_exception_is_set(ex));
	check(msg == NULL);
	camel_exception_clear(ex);

	/* see if it is not in the summary (only once) */
	s = camel_folder_get_summary(folder);
	check(s != NULL);
	found = 0;
	for (i=0;i<s->len;i++) {
		info = s->pdata[i];
		if (strcmp(camel_message_info_uid(info), uid) == 0)
			found++;
	}
	check(found == 0);
	camel_folder_free_summary(folder, s);

	/* check it is not in the uid list */
	s = camel_folder_get_uids(folder);
	check(s != NULL);
	found = 0;
	for (i=0;i<s->len;i++) {
		if (strcmp(s->pdata[i], uid) == 0)
			found++;
	}
	check(found == 0);
	camel_folder_free_uids(folder, s);

	camel_exception_free(ex);

	pull();
}

/* test basic store operations on folders */
/* TODO: Add subscription stuff */
void
test_folder_basic(CamelSession *session, const char *storename, int local)
{
	CamelStore *store;
	CamelException *ex = camel_exception_new();
	CamelFolder *folder, *root;
	char *what = g_strdup_printf("testing store: %s", storename);

	camel_test_start(what);
	test_free(what);

	push("getting store");
	store = camel_session_get_store(session, storename, ex);
	check_msg(!camel_exception_is_set(ex), "getting store: %s", camel_exception_get_description(ex));
	check(store != NULL);
	pull();

	/* local providers == no root folder */
	push("getting root folder");
	root = camel_store_get_root_folder(store, ex);
	if (local) {
		check(camel_exception_is_set(ex));
		check(root == NULL);
		camel_exception_clear(ex);
	} else {
		check_msg(!camel_exception_is_set(ex), "%s", camel_exception_get_description(ex));
		check(root != NULL);
		check_unref(root, 1);
	}
	pull();

	/* same for default folder */
	push("getting default folder");
	root = camel_store_get_root_folder(store, ex);
	if (local) {
		check(camel_exception_is_set(ex));
		check(root == NULL);
		camel_exception_clear(ex);
	} else {
		check_msg(!camel_exception_is_set(ex), "%s", camel_exception_get_description(ex));
		check(root != NULL);
		check_unref(root, 1);
	}
	pull();

	push("getting a non-existant folder, no create");
	folder = camel_store_get_folder(store, "unknown", 0, ex);
	check(camel_exception_is_set(ex));
	check(folder == NULL);
	camel_exception_clear(ex);
	pull();

	push("getting a non-existant folder, with create");
	folder = camel_store_get_folder(store, "testbox", CAMEL_STORE_FOLDER_CREATE, ex);
	check_msg(!camel_exception_is_set(ex), "%s", camel_exception_get_description(ex));
	check(folder != NULL);
	check_unref(folder, 1);
	pull();

	push("getting an existing folder");
	folder = camel_store_get_folder(store, "testbox", 0, ex);
	check_msg(!camel_exception_is_set(ex), "%s", camel_exception_get_description(ex));
	check(folder != NULL);
	check_unref(folder, 1);
	pull();

	push("renaming a non-existant folder");
	camel_store_rename_folder(store, "unknown1", "unknown2", ex);
	check(camel_exception_is_set(ex));
	camel_exception_clear(ex);
	pull();

	push("renaming an existing folder");
	camel_store_rename_folder(store, "testbox", "testbox2", ex);
	check_msg(!camel_exception_is_set(ex), "%s", camel_exception_get_description(ex));
	pull();

	push("opening the old name of a renamed folder");
	folder = camel_store_get_folder(store, "testbox", 0, ex);
	check(camel_exception_is_set(ex));
	check(folder == NULL);
	camel_exception_clear(ex);
	pull();

	push("opening the new name of a renamed folder");
	folder = camel_store_get_folder(store, "testbox2", 0, ex);
	check_msg(!camel_exception_is_set(ex), "%s", camel_exception_get_description(ex));
	check(folder != NULL);
	check_unref(folder, 1);
	pull();

	push("deleting a non-existant folder");
	camel_store_delete_folder(store, "unknown", ex);
	check(camel_exception_is_set(ex));
	camel_exception_clear(ex);
	pull();

	push("deleting an existing folder");
	camel_store_delete_folder(store, "testbox2", ex);
	check_msg(!camel_exception_is_set(ex), "%s", camel_exception_get_description(ex));
	pull();

	push("opening a folder that has been deleted");
	folder = camel_store_get_folder(store, "testbox2", 0, ex);
	check(camel_exception_is_set(ex));
	check(folder == NULL);
	camel_exception_clear(ex);
	pull();

	check_unref(store, 1);

	camel_test_end();

	camel_exception_free(ex);
}


/* todo: cross-check everything with folder_info checks as well */
/* this should probably take a folder instead of a session ... */
void
test_folder_message_ops(CamelSession *session, const char *name, int local)
{
	CamelStore *store;
	CamelException *ex = camel_exception_new();
	CamelFolder *folder;
	CamelMimeMessage *msg;
	int j;
	int indexed, max;
	GPtrArray *uids;
	CamelMessageInfo *info;

	max=local?2:1;

	for (indexed = 0;indexed<max;indexed++) {
		char *what = g_strdup_printf("folder ops: %s %s", name, local?(indexed?"indexed":"non-indexed"):"");
		int flags;

		camel_test_start(what);
		test_free(what);

		push("getting store");
		store = camel_session_get_store(session, name, ex);
		check_msg(!camel_exception_is_set(ex), "getting store: %s", camel_exception_get_description(ex));
		check(store != NULL);
		pull();

		push("creating %sindexed folder", indexed?"":"non-");
		if (indexed)
			flags = CAMEL_STORE_FOLDER_CREATE|CAMEL_STORE_FOLDER_BODY_INDEX;
		else
			flags = CAMEL_STORE_FOLDER_CREATE;
		folder = camel_store_get_folder(store, "testbox", flags, ex);
		check_msg(!camel_exception_is_set(ex), "%s", camel_exception_get_description(ex));
		check(folder != NULL);

		/* verify empty/can't get nonexistant stuff */
		test_folder_counts(folder, 0, 0);
		test_folder_not_message(folder, "0");
		test_folder_not_message(folder, "");

		for (j=0;j<10;j++) {
			char *content, *subject;

			push("creating test message");
			msg = test_message_create_simple();
			content = g_strdup_printf("Test message %d contents\n\n", j);
			test_message_set_content_simple((CamelMimePart *)msg, 0, "text/plain",
							content, strlen(content));
			test_free(content);
			subject = g_strdup_printf("Test message %d", j);
			camel_mime_message_set_subject(msg, subject);
			pull();

			push("appending simple message %d", j);
			camel_folder_append_message(folder, msg, NULL, ex);
			check_msg(!camel_exception_is_set(ex), "%s", camel_exception_get_description(ex));

			/* sigh, this shouldn't be required, but the imap code is too dumb to do it itself */
			if (!local) {
				push("forcing a refresh of folder updates");
				camel_folder_refresh_info(folder, ex);
				check_msg(!camel_exception_is_set(ex), "%s", camel_exception_get_description(ex));
				pull();
			}

			if (!local)
				camel_test_nonfatal("unread counts dont seem right for imap");

			test_folder_counts(folder, j+1, j+1);

			if (!local)
				camel_test_fatal();

			push("checking it is in the right uid slot & exists");
			uids = camel_folder_get_uids(folder);
			check(uids != NULL);
			check(uids->len == j+1);
			test_folder_message(folder, uids->pdata[j]);
			pull();

			push("checking it is the right message (subject): %s", subject);
			info = camel_folder_get_message_info(folder, uids->pdata[j]);
			check_msg(strcmp(camel_message_info_subject(info), subject)==0,
				  "info->subject %s", camel_message_info_subject(info));
			camel_folder_free_uids(folder, uids);
			camel_folder_free_message_info(folder, info);
			pull();

			test_free(subject);

			check_unref(msg, 1);
			pull();
		}

		check_unref(folder, 1);
		pull();

		push("deleting test folder, with messages in it");
		camel_store_delete_folder(store, "testbox", ex);
		check(camel_exception_is_set(ex));
		camel_exception_clear(ex);
		pull();

		push("re-opening folder");
		folder = camel_store_get_folder(store, "testbox", flags, ex);
		check_msg(!camel_exception_is_set(ex), "%s", camel_exception_get_description(ex));
		check(folder != NULL);

			/* verify counts */
		test_folder_counts(folder, 10, 10);

		/* re-check uid's, after a reload */
		uids = camel_folder_get_uids(folder);
		check(uids != NULL);
		check(uids->len == 10);
		for (j=0;j<10;j++) {
			char *subject = g_strdup_printf("Test message %d", j);

			push("verify reload of %s", subject);
			test_folder_message(folder, uids->pdata[j]);

			info = camel_folder_get_message_info(folder, uids->pdata[j]);
			check_msg(strcmp(camel_message_info_subject(info), subject)==0,
				  "info->subject %s", camel_message_info_subject(info));
			test_free(subject);
			camel_folder_free_message_info(folder, info);
			pull();
		}

		push("deleting first message & expunging");
		camel_folder_delete_message(folder, uids->pdata[0]);
		test_folder_counts(folder, 10, 10);
		camel_folder_expunge(folder, ex);
		check_msg(!camel_exception_is_set(ex), "%s", camel_exception_get_description(ex));
		test_folder_not_message(folder, uids->pdata[0]);
		test_folder_counts(folder, 9, 9);

		camel_folder_free_uids(folder, uids);

		uids = camel_folder_get_uids(folder);
		check(uids != NULL);
		check(uids->len == 9);
		for (j=0;j<9;j++) {
			char *subject = g_strdup_printf("Test message %d", j+1);

			push("verify after expunge of %s", subject);
			test_folder_message(folder, uids->pdata[j]);

			info = camel_folder_get_message_info(folder, uids->pdata[j]);
			check_msg(strcmp(camel_message_info_subject(info), subject)==0,
				  "info->subject %s", camel_message_info_subject(info));
			test_free(subject);
			camel_folder_free_message_info(folder, info);
			pull();
		}
		pull();

		push("deleting last message & expunging");
		camel_folder_delete_message(folder, uids->pdata[8]);
		/* sync? */
		test_folder_counts(folder, 9, 9);
		camel_folder_expunge(folder, ex);
		check_msg(!camel_exception_is_set(ex), "%s", camel_exception_get_description(ex));
		test_folder_not_message(folder, uids->pdata[8]);
		test_folder_counts(folder, 8, 8);

		camel_folder_free_uids(folder, uids);

		uids = camel_folder_get_uids(folder);
		check(uids != NULL);
		check(uids->len == 8);
		for (j=0;j<8;j++) {
			char *subject = g_strdup_printf("Test message %d", j+1);

			push("verify after expunge of %s", subject);
			test_folder_message(folder, uids->pdata[j]);

			info = camel_folder_get_message_info(folder, uids->pdata[j]);
			check_msg(strcmp(camel_message_info_subject(info), subject)==0,
				  "info->subject %s", camel_message_info_subject(info));
			test_free(subject);
			camel_folder_free_message_info(folder, info);
			pull();
		}
		pull();

		push("deleting all messages & expunging");
		for (j=0;j<8;j++) {
			camel_folder_delete_message(folder, uids->pdata[j]);
		}
		/* sync? */
		test_folder_counts(folder, 8, 8);
		camel_folder_expunge(folder, ex);
		check_msg(!camel_exception_is_set(ex), "%s", camel_exception_get_description(ex));
		for (j=0;j<8;j++) {
			test_folder_not_message(folder, uids->pdata[j]);
		}
		test_folder_counts(folder, 0, 0);

		camel_folder_free_uids(folder, uids);
		pull();

		check_unref(folder, 1);
		pull(); /* re-opening folder */

		push("deleting test folder, with no messages in it");
		camel_store_delete_folder(store, "testbox", ex);
		check_msg(!camel_exception_is_set(ex), "%s", camel_exception_get_description(ex));
		pull();

		check_unref(store, 1);
		camel_test_end();
	}

	camel_exception_free(ex);
}
