/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Various utilities for the mbox provider */

/* 
 * Authors : 
 *   Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright (C) 1999 Helix Code.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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


/* "xev" stands for x-evolution, which is the name of the
 * evolution specific header where are stored informations
 * like : 
 *   - mail status 
 *   - mail uid 
 *  ...
 *
 *
 * The evolution line has the following format :
 *
 *   X-Evolution:XXXXX-X
 *               \___/ \/
 *          UID ---'    `- Status 
 * 
 * the UID is internally used as a 32 bits long integer, but only the first 24 bits are 
 * used. The UID is coded as a string on 4 characters. Each character is a 6 bits 
 * integer coded using the b64 alphabet. 
 *
 */


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>


#include <glib.h>
#include "camel-mbox-utils.h"
#include "camel-mbox-parser.h"
#include "camel-mbox-summary.h"
#include "camel-mime-message.h"
#include "camel/camel-mime-part.h"
#include "camel/camel-multipart.h"
#include "camel/camel-stream-fs.h"

static gchar b64_alphabet[64] = 
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";



static void 
uid_to_string (guint32 uid, gchar string[4])
{
	
	string [0] = b64_alphabet [(uid >> 18) & 0x3f];
	string [1] = b64_alphabet [(uid >> 12) & 0x3f];
	string [2] = b64_alphabet [(uid >>  6) & 0x3f];
	string [3] = b64_alphabet [(uid      ) & 0x3f];
}


static guint32
string_to_uid (gchar *string)
{
	guint32 i;
	
	i = 
		(((string [0] >= 97) ? ( string [0] - 71 ) :
		 ((string [0] >= 65) ? ( string [0] - 65 ) :
		  ((string [0] >= 48) ? ( string [0] + 4 ) :
		   ((string [0] == 43) ? 62 : 63 )))) << 18)		
		
		+ (((string [1] >= 97) ? ( string [1] - 71 ) :
		   ((string [1] >= 65) ? ( string [1] - 65 ) :
		    ((string [1] >= 48) ? ( string [1] + 4 ) :
		     ((string [1] == 43) ? 62 : 63 )))) << 12)
		
		
		+ ((((string [2] >= 97) ? ( string [2] - 71 ) :
		   ((string [2] >= 65) ? ( string [2] - 65 ) :
		    ((string [2] >= 48) ? ( string [2] + 4 ) :
		     ((string [2] == 43) ? 62 : 63 ))))) << 6)
		
		
		+ (((string [3] >= 97) ? ( string [3] - 71 ) :
		   ((string [3] >= 65) ? ( string [3] - 65 ) :
		    ((string [3] >= 48) ? ( string [3] + 4 ) :
		     ((string [3] == 43) ? 62 : 63 )))));
	
	return i;
	
}


static gchar
flag_to_string (guchar status)
{
	return b64_alphabet [status & 0x3f];
}


static guchar
string_to_flag (gchar string)
{	
	return (string >= 97) ? ( string - 71 ) :
		((string >= 65) ? ( string - 65 ) :
		 ((string >= 48) ? ( string + 4 ) :
		  ((string == 43) ? 62 : 63 )));
}





void 
camel_mbox_xev_parse_header_content (gchar header_content[6], 
				     guint32 *uid, 
				     guchar *status)
{
	
	/* we assume that the first 4 characters of the header content 
	   are actually the uid stuff. If somebody messed with it ...
	   toooo bad. 
	*/
	*uid = string_to_uid (header_content);
	*status = string_to_flag (header_content[5]);
}

void 
camel_mbox_xev_write_header_content (gchar header_content[6], 
				     guint32 uid, 
				     guchar status)
{
	uid_to_string (uid, header_content);
	header_content[5] = flag_to_string (status);
	header_content[4] = '-';
}






void 
camel_mbox_copy_file_chunk (gint fd_src,
			    gint fd_dest, 
			    glong nb_bytes, 
			    CamelException *ex)
{
	gchar buffer [1000];
	glong nb_to_read;
	glong nb_read=1, v;
	
	nb_to_read = nb_bytes;
	while (nb_to_read > 0 && nb_read>0) {
		
		do {
			nb_read = read (fd_src, buffer, MIN (1000, nb_to_read));
		} while (nb_read == -1 && errno == EINTR);


		if (nb_read == -1) {
			camel_exception_setv (ex, 
					      CAMEL_EXCEPTION_FOLDER_INSUFFICIENT_PERMISSION,
					      "could not read from the mbox file\n"
					      "Full error is : %s\n",
					      strerror (errno));
			return;
		}


		nb_to_read -= nb_read;

		do {
			v = write (fd_dest, buffer, nb_read);
		} while (v == -1 && errno == EINTR);

		if (v == -1) {
			camel_exception_setv (ex, 
					     CAMEL_EXCEPTION_FOLDER_INSUFFICIENT_PERMISSION,
					     "could not write to the mbox copy file\n"
					      "Full error is : %s\n",
					      strerror (errno));
			return;
		}

		
	}
	
	
	
}

typedef void (*index_data_callback)(ibex *index, char *text, int len, int *left);

/*
  needs to handle encoding?
*/
static void
index_text(ibex *index, char *text, int len, int *left)
{
	/*printf("indexing %.*s\n", len, text);*/

	ibex_index_buffer(index, "message", text, len, left);
/*
	if (left) {
		printf("%d bytes left from indexing\n", *left);
	}
*/
}

/*
  index html data, ignore tags for now.
    could also index attribute values?
    should also handle encoding types ...
    should convert everything to utf8
*/
static void
index_html(ibex *index, char *text, int len, int *left)
{
	static int state = 0;
	char indexbuf[128];
	char *out = indexbuf, *outend = indexbuf+128;
	char *in, *inend;
	int c;

	in = text;
	inend = text+len;

	/*printf("indexing html: %d %d %.*s\n", state, len, len, text);*/

	while (in<inend) {
		c = *in++;
		switch (state) {
		case 0:			/* no tag */
			if (c=='<')
				state = 1;
			else {
				*out++ = c;
				if (out==outend) {
					index_text(index, indexbuf, out-indexbuf, left);
					memcpy(indexbuf, indexbuf+(out-indexbuf)-*left, *left);
					out = indexbuf+*left;
					printf("** %d bytes left\n", *left);
				}
			}
			break;
		case 1:
			if (c=='>')
				state = 0;
#if 0
			else if (c=='"')
				state = 2;
			break;
		case 2:
			if (c=='"') {
				state = 1;
			}
#endif
			break;
		}
	}
	index_text(index, indexbuf, out-indexbuf, left);
}

static void
index_message_content(ibex *index, CamelDataWrapper *object)
{
	CamelDataWrapper *containee;
	CamelStream *stream;
	int parts, i;
	int len;
	int left;
	char buffer[128];

	containee = camel_medium_get_content_object(CAMEL_MEDIUM(object));

	if (containee) {
		char *type = gmime_content_field_get_mime_type(containee->mime_type);
		index_data_callback callback = NULL;

		/*printf("type = %s\n", type);*/

		if (!strcasecmp(type, "text/plain")) {
			callback = index_text;
		} else if (!strcasecmp(type, "text/html")) {
			callback = index_html;
		} else if (!strncasecmp(type, "multipart/", 10)) {
			parts = camel_multipart_get_number (CAMEL_MULTIPART(containee));
			/*printf("multipart message, scanning contents  %d parts ...\n", parts);*/
			for (i=0;i<parts;i++) {
				index_message_content(index, CAMEL_DATA_WRAPPER (camel_multipart_get_part(CAMEL_MULTIPART(containee), i)));
			}
		} else {
			/*printf("\nunknwon format, ignored\n");*/
		}

		if (callback) {
			int total=0;

			/*printf("reading containee\n");

			  printf("containee = %p\n", containee);*/

			stream = camel_data_wrapper_get_output_stream(containee);
			left = 0;

			if (stream) {
				/*printf("stream = %p\n", stream);*/
				while ( (len = camel_stream_read(stream, buffer+left, sizeof(buffer)-left)) > 0) {
					total = len+left;
					callback(index, buffer, total, &left);
					if (left>0) {
						memcpy(buffer, buffer+total-left, left);
					}
				}
				callback(index, buffer+total-left, left, NULL);
				
				/*camel_stream_close(stream);*/
				/*printf("\n");*/
			} else {
				g_warning("cannot get stream for message?");
			}
		}

		g_free(type);
	} else {
		printf("no containee?\n");
	}
}


static void
index_message(ibex *index, int fd, CamelMboxParserMessageInfo *mi)
{
	off_t pos;
	CamelStream *stream;
	CamelMimeMessage *message;
	int newfd;

	if (index != NULL) {
		/*printf("indexing message\n %s\n %d for %d bytes\n", mi->from, mi->message_position, mi->size);*/
		pos = lseek(fd, 0, SEEK_CUR);
		
		/* the stream will close the fd we have */
		newfd = dup(fd);
		stream = camel_stream_fs_new_with_fd_and_bounds(newfd, mi->message_position, mi->message_position + mi->size);
		message = camel_mime_message_new_with_session( (CamelSession *)NULL);
		
		camel_data_wrapper_set_input_stream (
			CAMEL_DATA_WRAPPER (message), stream);
		
		index_message_content(index, CAMEL_DATA_WRAPPER (message));
		
		/*	printf("messageid = '%s'\n", message->message_uid);*/
		
		gtk_object_unref (GTK_OBJECT (message));
		gtk_object_unref (GTK_OBJECT (stream));
		
		lseek(fd, pos, SEEK_SET);
	}
}

guint32
camel_mbox_write_xev (CamelMboxFolder *folder,
		      gchar *mbox_file_name,
		      GArray *summary_information, 
		      guint32 *file_size,
		      guint32  next_uid, 
		      CamelException *ex)
{
	gint cur_msg;
	CamelMboxParserMessageInfo *cur_msg_info;
	gint fd1, fd2;
	guint bytes_to_copy = 0;
	glong cur_pos = 0;
	glong cur_offset = 0;
	glong end_of_last_message = 0;
	glong next_free_uid;
	gchar xev_header[20] = "X-Evolution:XXXX-X\n";
	gchar *tmp_file_name;
	gchar *tmp_file_name_secure;
	gint rename_result;
	gint unlink_result;
	int changed = FALSE;

	tmp_file_name = g_strdup_printf ("%s__.ev_tmp", mbox_file_name);
	tmp_file_name_secure = g_strdup_printf ("%s__.ev_tmp_secure", mbox_file_name);

	fd1 = open (mbox_file_name, O_RDONLY);
	fd2 = open (tmp_file_name, 
		    O_WRONLY | O_CREAT | O_TRUNC ,
		    0600);

	if (fd2 == -1) {
			camel_exception_setv (ex, 
					     CAMEL_EXCEPTION_FOLDER_INSUFFICIENT_PERMISSION,
					     "could not create the temporary mbox copy file\n"
					      "\t%s\n"
					      "Full error is : %s\n",
					      tmp_file_name,
					      strerror (errno));
			return next_uid;
		}
	
	next_free_uid = next_uid;
	for (cur_msg = 0; cur_msg < summary_information->len; cur_msg++) {
		
		cur_msg_info = (CamelMboxParserMessageInfo *)(summary_information->data) + cur_msg;
		end_of_last_message = cur_msg_info->message_position + cur_msg_info->size;

		if (cur_msg_info->uid == 0) {
			
			bytes_to_copy = cur_msg_info->message_position 
				+ cur_msg_info->end_of_headers_offset
				- cur_pos;

			cur_pos = cur_msg_info->message_position 
				+ cur_msg_info->end_of_headers_offset;

			cur_msg_info->uid = next_free_uid;			
			index_message(folder->index, fd1, cur_msg_info);
			changed = TRUE;

			camel_mbox_copy_file_chunk (fd1, fd2, bytes_to_copy, ex);
			if (camel_exception_get_id (ex)) {
				close (fd1);
				close (fd2);
				goto end;
			}
			
			cur_msg_info->status = 0;

			camel_mbox_xev_write_header_content (xev_header + 12, next_free_uid, 0);
			next_free_uid++;
			write (fd2, xev_header, 19);
			cur_offset += 19;
			cur_msg_info->size += 19;
			cur_msg_info->x_evolution_offset = cur_msg_info->end_of_headers_offset;
			cur_msg_info->x_evolution = g_strdup_printf ("%.6s", xev_header + 12);
			cur_msg_info->end_of_headers_offset += 19;
			*file_size += 19;
			cur_msg_info->message_position += cur_offset;
		} else {
			cur_msg_info->message_position += cur_offset;
		}
	}

	/* make sure the index is in sync */
	if (changed) {
		ibex_write(folder->index);
	}
	
	bytes_to_copy = end_of_last_message - cur_pos;
		camel_mbox_copy_file_chunk (fd1, fd2, bytes_to_copy, ex);


	/* close the original file as well as the 
	   newly created one */
	close (fd1);
	close (fd2);
	


	/* replace the mbox file with the temporary
	   file we just created */ 

	/* first rename the old mbox file to a temporary file */
	rename_result = rename (mbox_file_name, tmp_file_name_secure);
	if (rename_result == -1) {
		camel_exception_set (ex, 
				     CAMEL_EXCEPTION_FOLDER_INSUFFICIENT_PERMISSION,
				     "could not rename the mbox file to a temporary file");
		goto end;
	}
	
	/* then rename the newly created mbox file to the name 
	   of the original one */
	rename_result = rename (tmp_file_name, mbox_file_name);
	if (rename_result == -1) {
		camel_exception_set (ex, 
				     CAMEL_EXCEPTION_FOLDER_INSUFFICIENT_PERMISSION,
				     "could not rename the X-Evolution fed file to the mbox file");
		goto end;
	}

	/* finally, remove the old renamed mbox file */
	unlink_result = unlink (tmp_file_name_secure);
	if (unlink_result == -1) {
		camel_exception_set (ex, 
				     CAMEL_EXCEPTION_FOLDER_INSUFFICIENT_PERMISSION,
				     "could not remove the saved original mbox file");
		goto end;
	}


 end: /* free everything and return */
	
	g_free (tmp_file_name);
	g_free (tmp_file_name_secure);
	return next_free_uid;
}
