/*
 *  Copyright (C) 2000 Helix Code Inc.
 *
 *  Authors: Michael Zucchi <notzed@helixcode.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public License
 *  as published by the Free Software Foundation; either version 2 of
 *  the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include <iconv.h>

#include <glib.h>
#include <time.h>

#include <ctype.h>

#include "camel-mime-utils.h"

#define d(x)
#define d2(x)

static char *base64_alphabet =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static unsigned char tohex[16] = {
	'0', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};

static unsigned char camel_mime_special_table[256] = {
	  5,  5,  5,  5,  5,  5,  5,  5,  5,167,  7,  5,  5, 39,  5,  5,
	  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
	178,128,140,128,128,128,128,128,140,140,128,128,140,128,136,132,
	128,128,128,128,128,128,128,128,128,128,204,140,140,  4,140,132,
	140,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
	128,128,128,128,128,128,128,128,128,128,128,172,172,172,128,128,
	128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
	128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,  5,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};

static unsigned char camel_mime_base64_rank[256] = {
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255, 62,255,255,255, 63,
	 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,255,255,255,  0,255,255,
	255,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
	 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,255,255,255,255,255,
	255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
};

/*
  if any of these change, then the tables above should be regenerated
  by compiling this with -DBUILD_TABLE, and running.

  gcc -o buildtable `glib-config --cflags --libs` -DBUILD_TABLE camel-mime-utils.c
  ./buildtable

*/
enum {
	IS_CTRL		= 1<<0,
	IS_LWSP		= 1<<1,
	IS_TSPECIAL	= 1<<2,
	IS_SPECIAL	= 1<<3,
	IS_SPACE	= 1<<4,
	IS_DSPECIAL	= 1<<5,
	IS_COLON	= 1<<6,	/* rather wasteful of space ... */
	IS_QPSAFE	= 1<<7
};

#define is_ctrl(x) ((camel_mime_special_table[(unsigned char)(x)] & IS_CTRL) != 0)
#define is_lwsp(x) ((camel_mime_special_table[(unsigned char)(x)] & IS_LWSP) != 0)
#define is_tspecial(x) ((camel_mime_special_table[(unsigned char)(x)] & IS_TSPECIAL) != 0)
#define is_type(x, t) ((camel_mime_special_table[(unsigned char)(x)] & (t)) != 0)
#define is_ttoken(x) ((camel_mime_special_table[(unsigned char)(x)] & (IS_TSPECIAL|IS_LWSP|IS_CTRL)) == 0)
#define is_atom(x) ((camel_mime_special_table[(unsigned char)(x)] & (IS_SPECIAL|IS_SPACE|IS_CTRL)) == 0)
#define is_dtext(x) ((camel_mime_special_table[(unsigned char)(x)] & IS_DSPECIAL) == 0)
#define is_fieldname(x) ((camel_mime_special_table[(unsigned char)(x)] & (IS_CTRL|IS_SPACE|IS_COLON)) == 0)
#define is_qpsafe(x) ((camel_mime_special_table[(unsigned char)(x)] & IS_QPSAFE) != 0)

/* only needs to be run to rebuild the tables above */
#ifdef BUILD_TABLE

#define CHARS_LWSP " \t\n\r"
#define CHARS_TSPECIAL "()<>@,;:\\\"/[]?="
#define CHARS_SPECIAL "()<>@,;:\\\".[]"
#define CHARS_CSPECIAL "()\\\r"	/* not in comments */
#define CHARS_DSPECIAL "[]\\\r \t"	/* not in domains */

static void
header_init_bits(unsigned char bit, unsigned char bitcopy, int remove, unsigned char *vals, int len)
{
	int i;

	if (!remove) {
		for (i=0;i<len;i++) {
			camel_mime_special_table[vals[i]] |= bit;
		}
		if (bitcopy) {
			for (i=0;i<256;i++) {
				if (camel_mime_special_table[i] & bitcopy)
					camel_mime_special_table[i] |= bit;
			}
		}
	} else {
		for (i=0;i<256;i++)
			camel_mime_special_table[i] |= bit;
		for (i=0;i<len;i++) {
			camel_mime_special_table[vals[i]] &= ~bit;
		}
		if (bitcopy) {
			for (i=0;i<256;i++) {
				if (camel_mime_special_table[i] & bitcopy)
					camel_mime_special_table[i] &= ~bit;
			}
		}
	}
}

static void
header_decode_init(void)
{
	int i;

	for (i=0;i<256;i++) camel_mime_special_table[i] = 0;
	for (i=0;i<32;i++) camel_mime_special_table[i] |= IS_CTRL;
	camel_mime_special_table[127] = IS_CTRL;
	camel_mime_special_table[' '] = IS_SPACE;
	camel_mime_special_table[':'] = IS_COLON;
	header_init_bits(IS_LWSP, 0, 0, CHARS_LWSP, sizeof(CHARS_LWSP)-1);
	header_init_bits(IS_TSPECIAL, IS_CTRL, 0, CHARS_TSPECIAL, sizeof(CHARS_TSPECIAL)-1);
	header_init_bits(IS_SPECIAL, 0, 0, CHARS_SPECIAL, sizeof(CHARS_SPECIAL)-1);
	header_init_bits(IS_DSPECIAL, 0, FALSE, CHARS_DSPECIAL, sizeof(CHARS_DSPECIAL)-1);
	for (i=0;i<256;i++) if ((i>=33 && i<=60) || (i>=62 && i<=126) || i==32 || i==9) camel_mime_special_table[i] |= IS_QPSAFE;
}

void
base64_init(void)
{
	int i;

	memset(camel_mime_base64_rank, 0xff, sizeof(camel_mime_base64_rank));
	for (i=0;i<64;i++) {
		camel_mime_base64_rank[(unsigned int)base64_alphabet[i]] = i;
	}
	camel_mime_base64_rank['='] = 0;
}

int main(int argc, char **argv)
{
	int i;
	void run_test(void);

	header_decode_init();
	base64_init();

	printf("static unsigned char camel_mime_special_table[256] = {\n\t");
	for (i=0;i<256;i++) {
		printf("%3d,", camel_mime_special_table[i]);
		if ((i&15) == 15) {
			printf("\n");
			if (i!=255) {
				printf("\t");
			}
		}
	}
	printf("};\n");

	printf("static unsigned char camel_mime_base64_rank[256] = {\n\t");
	for (i=0;i<256;i++) {
		printf("%3d,", camel_mime_base64_rank[i]);
		if ((i&15) == 15) {
			printf("\n");
			if (i!=255) {
				printf("\t");
			}
		}
	}
	printf("};\n");

	run_test();

	return 0;
}

#endif


/* call this when finished encoding everything, to
   flush off the last little bit */
int
base64_encode_close(unsigned char *in, int inlen, unsigned char *out, int *state, int *save)
{
	int c1, c2;
	unsigned char *outptr = out;

	if (inlen>0)
		outptr += base64_encode_step(in, inlen, outptr, state, save);

	c1 = ((char *)save)[1];
	c2 = ((char *)save)[2];

	switch (((char *)save)[0]) {
	case 2:
		outptr[2] = base64_alphabet [ ( (c2 &0x0f) << 2 ) ];
		goto skip;
	case 1:
		outptr[2] = '=';
	skip:
		outptr[0] = base64_alphabet [ c1 >> 2 ];
		outptr[1] = base64_alphabet [ c2 >> 4 | ( (c1&0x3) << 4 )];
		outptr[3] = '=';
		outptr += 4;
		break;
	}
	*outptr++ = '\n';

	*save = 0;
	*state = 0;

	return outptr-out;
}

/*
  performs an 'encode step', only encodes blocks of 3 characters to the
  output at a time, saves left-over state in state and save (initialise to
  0 on first invocation).
*/
int
base64_encode_step(unsigned char *in, int len, unsigned char *out, int *state, int *save)
{
	register unsigned char *inptr, *outptr;

	if (len<=0)
		return 0;

	inptr = in;
	outptr = out;

	d(printf("we have %d chars, and %d saved chars\n", len, ((char *)save)[0]));

	if (len + ((char *)save)[0] > 2) {
		unsigned char *inend = in+len-2;
		register int c1, c2, c3;
		register int already;

		already = *state;

		switch (((char *)save)[0]) {
		case 1:	c1 = ((char *)save)[1];	goto skip1;
		case 2:	c1 = ((char *)save)[1];
			c2 = ((char *)save)[2];	goto skip2;
		}
		
		/* yes, we jump into the loop, no i'm not going to change it, its beautiful! */
		while (inptr < inend) {
			c1 = *inptr++;
		skip1:
			c2 = *inptr++;
		skip2:
			c3 = *inptr++;
			*outptr++ = base64_alphabet [ c1 >> 2 ];
			*outptr++ = base64_alphabet [ c2 >> 4 | ( (c1&0x3) << 4 ) ];
			*outptr++ = base64_alphabet [ ( (c2 &0x0f) << 2 ) | (c3 >> 6) ];
			*outptr++ = base64_alphabet [ c3 & 0x3f ];
			/* this is a bit ugly ... */
			if ((++already)>=19) {
				*outptr++='\n';
				already = 0;
			}
		}

		((char *)save)[0] = 0;
		len = 2-(inptr-inend);
		*state = already;
	}

	d(printf("state = %d, len = %d\n",
		 (int)((char *)save)[0],
		 len));

	if (len>0) {
		register char *saveout;

		/* points to the slot for the next char to save */
		saveout = & (((char *)save)[1]) + ((char *)save)[0];

		/* len can only be 0 1 or 2 */
		switch(len) {
		case 2:	*saveout++ = *inptr++;
		case 1:	*saveout++ = *inptr++;
		}
		((char *)save)[0]+=len;
	}

	d(printf("mode = %d\nc1 = %c\nc2 = %c\n",
		 (int)((char *)save)[0],
		 (int)((char *)save)[1],
		 (int)((char *)save)[2]));

	return outptr-out;
}

int
base64_decode_step(unsigned char *in, int len, unsigned char *out, int *state, unsigned int *save)
{
	register unsigned char *inptr, *outptr;
	unsigned char *inend, c;
	register unsigned int v;
	int i;

	inend = in+len;
	outptr = out;

	/* convert 4 base64 bytes to 3 normal bytes */
	v=*save;
	i=*state;
	inptr = in;
	while (inptr<inend) {
		c = camel_mime_base64_rank[*inptr++];
		if (c != 0xff) {
			v = (v<<6) | c;
			i++;
			if (i==4) {
				*outptr++ = v>>16;
				*outptr++ = v>>8;
				*outptr++ = v;
				i=0;
			}
		}
	}

	*save = v;
	*state = i;

	/* quick scan back for '=' on the end somewhere */
	/* fortunately we can drop 1 output char for each trailing = (upto 2) */
	i=2;
	while (inptr>in && i) {
		inptr--;
		if (camel_mime_base64_rank[*inptr] != 0xff) {
			if (*inptr == '=')
				outptr--;
			i--;
		}
	}

	/* if i!= 0 then there is a truncation error! */
	return outptr-out;
}

int
quoted_encode_close(unsigned char *in, int len, unsigned char *out, int *state, int *save)
{
	register unsigned char *outptr = out;

	if (len>0)
		outptr += quoted_encode_step(in, len, outptr, state, save);

	/* hmm, not sure if this should really be added here, we dont want
	   to add it to the content, afterall ...? */
	*outptr++ = '\n';

	*save = 0;
	*state = 0;

	return outptr-out;
}

/*
  FIXME: does not handle trailing spaces/tabs before end of line
*/
int
quoted_encode_step(unsigned char *in, int len, unsigned char *out, int *state, int *save)
{
	register unsigned char *inptr, *outptr, *inend;
	unsigned char c;
	register int sofar = *state;

	inptr = in;
	inend = in+len;
	outptr = out;
	while (inptr<inend) {
		c = *inptr++;
		if (is_qpsafe(c)) {
				/* check for soft line-break */
			if ((++sofar)>74) {
				*outptr++='=';
				*outptr++='\n';
				sofar = 1;
			}
			*outptr++=c;
		} else {
			if ((++sofar)>72) {
				*outptr++='=';
				*outptr++='\n';
				sofar = 3;
			}
			*outptr++ = '=';
			*outptr++ = tohex[(c>>4) & 0xf];
			*outptr++ = tohex[c & 0xf];
		}
	}
	*state = sofar;
	return outptr-out;
}

/*
  FIXME: this does not strip trailing spaces from lines (as it should, rfc 2045, section 6.7)
  Should it also canonicalise the end of line to CR LF??

  Note: Trailing rubbish (at the end of input), like = or =x or =\r will be lost.
*/ 

int
quoted_decode_step(unsigned char *in, int len, unsigned char *out, int *savestate, int *saveme)
{
	register unsigned char *inptr, *outptr;
	unsigned char *inend, c;
	int state, save;

	inend = in+len;
	outptr = out;

	printf("decoding text '%.*s'\n", len, in);

	state = *savestate;
	save = *saveme;
	inptr = in;
	while (inptr<inend) {
		switch (state) {
		case 0:
			while (inptr<inend) {
				c = *inptr++;
				/* FIXME: use a specials table to avoid 3 comparisons for the common case */
				if (c=='=') { 
					state = 1;
					break;
				}
#ifdef CANONICALISE_EOL
				/*else if (c=='\r') {
					state = 3;
				} else if (c=='\n') {
					*outptr++ = '\r';
					*outptr++ = c;
					} */
#endif
				else {
					*outptr++ = c;
				}
			}
			break;
		case 1:
			c = *inptr++;
			if (c=='\n') {
				/* soft break ... unix end of line */
				state = 0;
			} else {
				save = c;
				state = 2;
			}
			break;
		case 2:
			c = *inptr++;
			if (isxdigit(c) && isxdigit(save)) {
				c = toupper(c);
				save = toupper(save);
				*outptr++ = (((save>='A'?save-'A'+10:save-'0')&0x0f) << 4)
					| ((c>='A'?c-'A'+10:c-'0')&0x0f);
			} else if (c=='\n' && save == '\r') {
				/* soft break ... canonical end of line */
			} else {
				/* just output the data */
				*outptr++ = '=';
				*outptr++ = save;
				*outptr++ = c;
			}
			state = 0;
			break;
#ifdef CANONICALISE_EOL
		case 3:
			/* convert \r -> to \r\n, leaves \r\n alone */
			c = *inptr++;
			if (c=='\n') {
				*outptr++ = '\r';
				*outptr++ = c;
			} else {
				*outptr++ = '\r';
				*outptr++ = '\n';
				*outptr++ = c;
			}
			state = 0;
			break;
#endif
		}
	}

	*savestate = state;
	*saveme = save;

	return outptr-out;
}

/*
  this is for the "Q" encoding of international words,
  which is slightly different than plain quoted-printable
*/
int
quoted_decode(unsigned char *in, int len, unsigned char *out)
{
	register unsigned char *inptr, *outptr;
	unsigned char *inend, c, c1;
	int ret = 0;

	inend = in+len;
	outptr = out;

	printf("decoding text '%.*s'\n", len, in);

	inptr = in;
	while (inptr<inend) {
		c = *inptr++;
		if (c=='=') {
			/* silently ignore truncated data? */
			if (inend-in>=2) {
				c = toupper(*inptr++);
				c1 = toupper(*inptr++);
				*outptr++ = (((c>='A'?c-'A'+10:c-'0')&0x0f) << 4)
					| ((c1>='A'?c1-'A'+10:c1-'0')&0x0f);
			} else {
				ret = -1;
				break;
			}
		} else if (c=='_') {
			*outptr++ = 0x20;
		} else if (c==' ' || c==0x09) {
			/* FIXME: this is an error! ignore for now ... */
			ret = -1;
			break;
		} else {
			*outptr++ = c;
		}
	}
	if (ret==0) {
		return outptr-out;
	}
	return -1;
}


static void
header_decode_lwsp(const char **in)
{
	const char *inptr = *in;
	char c;

	d2(printf("is ws: '%s'\n", *in));

	while (is_lwsp(*inptr) || *inptr =='(') {
		while (is_lwsp(*inptr)) {
			d2(printf("(%c)", *inptr));
			inptr++;
		}
		d2(printf("\n"));

		/* check for comments */
		if (*inptr == '(') {
			int depth = 1;
			inptr++;
			while (depth && (c=*inptr)) {
				if (c=='\\' && inptr[1]) {
					inptr++;
				} else if (c=='(') {
					depth++;
				} else if (c==')') {
					depth--;
				}
				inptr++;
			}
		}
	}
	*in = inptr;
}

char *
g_strdup_len(const char *start, int len)
{
	char *d = g_malloc(len+1);
	memcpy(d, start, len);
	d[len] = 0;
	return d;
}


/* decode rfc 2047 encoded string segment */
char *
rfc2047_decode_word(char *in, int len)
{
	char *inptr = in+2;
	char *inend = in+len-2;
	char *encname;
	int tmplen;
	int ret;
	char *decword = NULL;
	char *decoded = NULL;
	char *outbase = NULL;
	char *inbuf, *outbuf;
	int inlen, outlen;
	iconv_t ic;

	printf("decoding '%.*s'\n", len, in);

	/* just make sure we're not passed shit */
	if (len<7
	    || !(in[0]=='=' && in[1]=='?' && in[len-1]=='=' && in[len-2]=='?')) {
		printf("invalid\n");
		return NULL;
	}

	inptr = memchr(inptr, '?', inend-inptr);
	if (inptr!=NULL
	    && inptr<inend+2
	    && inptr[2]=='?') {
		inptr++;
		tmplen = inend-inptr-2;
		decword = g_malloc(tmplen); /* this will always be more-than-enough room */
		switch(toupper(inptr[0])) {
		case 'Q':
			inlen = quoted_decode(inptr+2, tmplen, decword);
			break;
		case 'B': {
			int state=0;
			unsigned int save=0;
			inlen = base64_decode_step(inptr+2, tmplen, decword, &state, &save);
			/* if state != 0 then error? */
			break;
		}
		}
		if (inlen>0) {
			/* yuck, all this snot is to setup iconv! */
			tmplen = inptr-in-3;
			encname = alloca(tmplen+1);
			encname[tmplen]=0;
			memcpy(encname, in+2, tmplen);

			inbuf = decword;

			outlen = inlen*6;
			outbase = g_malloc(outlen);
			outbuf = outbase;

			ic = iconv_open("utf-8", encname);
			ret = iconv(ic, (const char **)&inbuf, &inlen, &outbuf, &outlen);
			iconv_close(ic);
			if (ret>=0) {
				*outbuf = 0;
				decoded = outbase;
				outbase = NULL;
			}
		}
	}
	free(outbase);
	free(decword);

	printf("decoded '%s'\n", decoded);

	return decoded;
}

char *
decode_coded_string(char *in)
{
	return rfc2047_decode_word(in, strlen(in));
}

/* grrr, glib should have this ! */
GString *
g_string_append_len(GString *st, char *s, int l)
{
	char *tmp;

	tmp = alloca(l+1);
	tmp[l]=0;
	memcpy(tmp, s, l);
	return g_string_append(st, tmp);
}

/* decodes a simple text, rfc822 */
char *
header_decode_text(char *in, int inlen)
{
	GString *out;
	char *inptr = in;
	char *inend = in+inlen;
	char *encstart, *encend;
	char *decword;

	printf("encoded is '%s'\n", in);

	out = g_string_new("");
	while ( (encstart = strstr(inptr, "=?"))
		&& (encend = strstr(encstart+2, "?=")) ) {

		decword = rfc2047_decode_word(encstart, encend-encstart+2);
		if (decword) {
			g_string_append_len(out, inptr, encstart-inptr);
			g_string_append_len(out, decword, strlen(decword));
			free(decword);
		} else {
			g_string_append_len(out, inptr, encend-inptr+2);
		}
		inptr = encend+2;
	}
	g_string_append_len(out, inptr, inend-inptr);

	inptr = out->str;
	g_string_free(out, FALSE);
	printf("decoded is: '%s'\n", inptr);
	return inptr;
}

char *
header_decode_string(char *in)
{
	return header_decode_text(in, strlen(in));
}


/* these are all internal parser functions */

static char *
header_decode_token(const char **in)
{
	const char *inptr = *in;
	const char *start;

	header_decode_lwsp(&inptr);
	start = inptr;
	while (is_ttoken(*inptr))
		inptr++;
	if (inptr>start) {
		*in = inptr;
		return g_strdup_len(start, inptr-start);
	} else {
		return NULL;
	}
}

/*
   <"> * ( <any char except <"> \, cr  /  \ <any char> ) <">
*/
static char *
header_decode_quoted_string(const char **in)
{
	const char *inptr = *in;
	char *out = NULL, *outptr;
	int outlen;
	int c;

	header_decode_lwsp(&inptr);
	if (*inptr == '"') {
		const char *intmp;
		int skip = 0;

		/* first, calc length */
		inptr++;
		intmp = inptr;
		while ( (c = *intmp++) && c!= '"' ) {
			if (c=='\\' && *intmp) {
				intmp++;
				skip++;
			}
		}
		outlen = intmp-inptr-skip;
		out = outptr = g_malloc(outlen+1);
		while ( (c = *inptr++) && c!= '"' ) {
			if (c=='\\' && *inptr) {
				c = *inptr++;
			}
			*outptr++ = c;
		}
		*outptr = 0;
	}
	*in = inptr;
	return out;
}

static char *
header_decode_atom(const char **in)
{
	const char *inptr = *in, *start;

	header_decode_lwsp(&inptr);
	start = inptr;
	while (is_atom(*inptr))
		inptr++;
	*in = inptr;
	if (inptr > start)
		return g_strdup_len(start, inptr-start);
	else
		return NULL;
}

static char *
header_decode_word(const char **in)
{
	const char *inptr = *in;

	header_decode_lwsp(&inptr);
	if (*inptr == '"') {
		*in = inptr;
		return header_decode_quoted_string(in);
	} else {
		*in = inptr;
		return header_decode_atom(in);
	}
}

static char *
header_decode_value(const char **in)
{
	const char *inptr = *in;

	header_decode_lwsp(&inptr);
	if (*inptr == '"') {
		printf("decoding quoted string\n");
		return header_decode_quoted_string(in);
	} else if (is_ttoken(*inptr)) {
		printf("decoding token\n");
		/* this may not have the right specials for all params? */
		return header_decode_token(in);
	}
	return NULL;
}

/* shoudl this return -1 for no int? */
static int
header_decode_int(const char **in)
{
	const char *inptr = *in;
	int c, v=0;

	header_decode_lwsp(&inptr);
	while ( (c=*inptr++ & 0xff)
		&& isdigit(c) ) {
		v = v*10+(c-'0');
	}
	*in = inptr-1;
	return v;
}

static int
header_decode_param(const char **in, char **paramp, char **valuep)
{
	const char *inptr = *in;
	char *param, *value=NULL;

	param = header_decode_token(&inptr);
	header_decode_lwsp(&inptr);
	if (*inptr == '=') {
		inptr++;
		value = header_decode_value(&inptr);
	}

	if (param && value) {
		*paramp = param;
		*valuep = value;
		*in = inptr;
		return 0;
	} else {
		g_free(param);
		g_free(value);
		return 1;
	}
}

char *
header_param(struct _header_param *p, char *name)
{
	while (p && strcasecmp(p->name, name) != 0)
		p = p->next;
	if (p)
		return p->value;
	return NULL;
}

char *
header_content_type_param(struct _header_content_type *t, char *name)
{
	return header_param(t->params, name);
}

void
header_content_type_free(struct _header_content_type *ct)
{
	struct _header_param *p, *n;

	if (ct) {
		p = ct->params;
		while (p) {
			n = p->next;
			g_free(p->name);
			g_free(p->value);
			g_free(p);
			p = n;
		}
		g_free(ct->type);
		g_free(ct->subtype);
		g_free(ct);
	}
}

/* for decoding email addresses, canonically */
static char *
header_decode_domain(const char **in)
{
	const char *inptr = *in, *start;
	int go = TRUE;
	GString *domain = g_string_new("");

				/* domain ref | domain literal */
	header_decode_lwsp(&inptr);
	while (go) {
		if (*inptr == '[') { /* domain literal */
			g_string_append(domain, "[ ");
			inptr++;
			header_decode_lwsp(&inptr);
			start = inptr;
			while (is_dtext(*inptr)) {
				g_string_append_c(domain, *inptr);
				inptr++;
			}
			if (*inptr == ']') {
				g_string_append(domain, " ]");
				inptr++;
			} else {
				g_warning("closing ']' not found in domain: %s", *in);
			}
		} else {
			char *a = header_decode_atom(&inptr);
			if (a) {
				g_string_append(domain, a);
			} else {
				g_warning("missing atom from domain-ref");
				break;
			}
		}
		header_decode_lwsp(&inptr);
		if (*inptr == '.') { /* next sub-domain? */
			g_string_append(domain, " . ");
			inptr++;
			header_decode_lwsp(&inptr);
		} else
			go = FALSE;
	}

	*in = inptr;

	/* FIXME:L free string header */
	return domain->str;
}

static char *
header_decode_addrspec(const char **in)
{
	const char *inptr = *in;
	char *word;
	GString *addr = g_string_new("");

	header_decode_lwsp(&inptr);

	/* addr-spec */
	word = header_decode_word(&inptr);
	if (word) {
		g_string_append(addr, word);
		header_decode_lwsp(&inptr);
		while (*inptr == '.' && word) {
			inptr++;
			g_string_append_c(addr, '.');
			word = header_decode_word(&inptr);
			if (word) {
				g_string_append(addr, word);
				header_decode_lwsp(&inptr);
			} else {
				g_warning("Invalid address spec: %s", *in);
			}
		}
		if (*inptr == '@') {
			inptr++;
			g_string_append_c(addr, '@');
			word = header_decode_domain(&inptr);
			if (word) {
				g_string_append(addr, word);
			} else {
				g_warning("Invalid address, missing domain: %s", *in);
			}
		} else {
			g_warning("Invalid addr-spec, missing @: %s", *in);
		}
	} else {
		g_warning("invalid addr-spec, no local part");
	}

	/* FIXME: return null on error? */

	*in = inptr;
	word = addr->str;
	g_string_free(addr, FALSE);
	return word;
}

/*
  address:
   word *('.' word) @ domain |
   *(word) '<' [ *('@' domain ) ':' ] word *( '.' word) @ domain |

   1*word ':' [ word ... etc (mailbox, as above) ] ';'
 */

/* mailbox:
   word *( '.' word ) '@' domain
   *(word) '<' [ *('@' domain ) ':' ] word *( '.' word) @ domain
   */

/* FIXME: what does this return? */

static void
header_decode_mailbox(const char **in)
{
	const char *inptr = *in;
	char *pre;
	int closeme = FALSE;
	GString *addr;

	addr = g_string_new("");

	/* for each address */
	pre = header_decode_word(&inptr);
	header_decode_lwsp(&inptr);
	if (!(*inptr == '.' || *inptr == '@' || *inptr==',' || *inptr=='\0')) {	/* ',' and '\0' required incase it is a simple address, no @ domain part (buggy writer) */
		/* FIXME: rfc 2047 decode each word */
		while (pre) {
			/* rfc_decode(pre) */
			pre = header_decode_word(&inptr);
		}
		header_decode_lwsp(&inptr);
		if (*inptr == '<') {
			closeme = TRUE;
			inptr++;
			header_decode_lwsp(&inptr);
			if (*inptr == '@') {
				while (*inptr == '@') {
					inptr++;
					header_decode_domain(&inptr);
					header_decode_lwsp(&inptr);
					if (*inptr == ',') {
						inptr++;
						header_decode_lwsp(&inptr);
					}
				}
				if (*inptr == ':') {
					inptr++;
				} else {
					g_warning("broken route-address, missing ':': %s", *in);
				}
			}
			pre = header_decode_word(&inptr);
			header_decode_lwsp(&inptr);
		} else {
			g_warning("broken address? %s", *in);
		}
	}

	if (pre) {
		g_string_append(addr, pre);
	} else {
		g_warning("No local-part for email address: %s", *in);
	}

	/* should be at word '.' localpart */
	while (*inptr == '.' && pre) {
		inptr++;
		pre = header_decode_word(&inptr);
		g_string_append_c(addr, '.');
		g_string_append(addr, pre);
		header_decode_lwsp(&inptr);
	}

	/* now at '@' domain part */
	if (*inptr == '@') {
		char *dom;

		inptr++;
		g_string_append_c(addr, '@');
		dom = header_decode_domain(&inptr);
		g_string_append(addr, dom);
	} else {
		g_warning("invalid address, no '@' domain part at %c: %s", *inptr, *in);
	}

	if (closeme) {
		header_decode_lwsp(&inptr);
		if (*inptr == '>') {
			inptr++;
		} else {
			g_warning("invalid route address, no closing '>': %s", *in);
		} 
	}

	*in = inptr;

	printf("got mailbox: %s\n", addr->str);
}

/* FIXME: what does this return? */
static void
header_decode_address(const char **in)
{
	const char *inptr = *in;
	char *pre;
	GString *group = g_string_new("");

	/* pre-scan, trying to work out format, discard results */
	header_decode_lwsp(&inptr);
	while ( (pre = header_decode_word(&inptr)) ) {
		g_string_append(group, pre);
		g_string_append(group, " ");
		g_free(pre);
	}
	header_decode_lwsp(&inptr);
	if (*inptr == ':') {
		printf("group detected: %s\n", group->str);
		/* that was a group spec, scan mailbox's */
		inptr++;
		/* FIXME: check rfc 2047 encodings of words, here or above in the loop */
		header_decode_lwsp(&inptr);
		if (*inptr != ';') {
			int go = TRUE;
			do {
				header_decode_mailbox(&inptr);
				header_decode_lwsp(&inptr);
				if (*inptr == ',')
					inptr++;
				else
					go = FALSE;
			} while (go);
			if (*inptr == ';') {
				inptr++;
			} else {
				g_warning("Invalid group spec, missing closing ';': %s", *in);
			}
		} else {
			inptr++;
		}
		*in = inptr;
/*	} else if (*inptr == '.' || *inptr == '<' || *inptr == '@') {*/
	} else {
		/* back-track, and rescan.  not worth the code duplication to do this faster */
		/* this will detect invalid input */
		header_decode_mailbox(in);
	}/* else {
		g_warning("Cannot scan address at '%c': %s", *inptr, *in);
		}*/

	/* FIXME: store gropu somewhere */
	g_string_free(group, TRUE);
}

char *
header_msgid_decode(const char *in)
{
	const char *inptr = in;
	char *msgid = NULL;

	printf("decoding Message-ID: '%s'\n", in);

	if (in == NULL)
		return NULL;

	header_decode_lwsp(&inptr);
	if (*inptr == '<') {
		inptr++;
		header_decode_lwsp(&inptr);
		msgid = header_decode_addrspec(&inptr);
		if (msgid) {
			header_decode_lwsp(&inptr);
			if (*inptr == '>') {
				inptr++;
			} else {
				g_warning("Missing closing '>' on message id: %s", in);
			}
		} else {
			g_warning("Cannot find message id in: %s", in);
		}
	} else {
		g_warning("missing opening '<' on message id: %s", in);
	}

	if (msgid) {
		printf("Got message id: %s\n", msgid);
	}
	return msgid;
}

void
header_to_decode(const char *in)
{
	const char *inptr = in, *last;

	printf("decoding To: '%s'\n", in);

	if (in == NULL)
		return NULL;

	do {
		last = inptr;
		header_decode_address(&inptr);
		header_decode_lwsp(&inptr);
		if (*inptr == ',')
			inptr++;
		else
			break;
	} while (inptr != last);

	if (*inptr) {
		g_warning("Invalid input detected at %c (%d): %s\n or at: %s", *inptr, inptr-in, in, inptr);
	}

	if (inptr == last) {
		g_warning("detected invalid input loop at : %s", last);
	}
}

void
header_mime_decode(const char *in)
{
	const char *inptr = in;
	int major=-1, minor=-1;

	printf("decoding MIME-Version: '%s'\n", in);

	if (in == NULL)
		return NULL;

	header_decode_lwsp(&inptr);
	if (isdigit(*inptr)) {
		major = header_decode_int(&inptr);
		header_decode_lwsp(&inptr);
		if (*inptr == '.') {
			inptr++;
			header_decode_lwsp(&inptr);
			if (isdigit(*inptr))
				minor = header_decode_int(&inptr);
		}
	}

	printf("major = %d, minor = %d\n", major, minor);
}


struct _header_content_type *
header_content_type_decode(const char *in)
{
	const char *inptr = in;
	char *type, *subtype = NULL;
	struct _header_content_type *t = NULL;

	if (in==NULL)
		return NULL;

	type = header_decode_token(&inptr);
	header_decode_lwsp(&inptr);
	if (type) {
		if  (*inptr == '/') {
			inptr++;
			subtype = header_decode_token(&inptr);
		}
		if (subtype == NULL && (!strcasecmp(type, "text"))) {
			g_warning("text type with no subtype, resorting to text/plain: %s", in);
			subtype = g_strdup("plain");
		}
		if (subtype) {
			t = g_malloc(sizeof(*t));
			t->type = type;
			t->subtype = subtype;
			t->params = NULL;
			printf("content-type is %s / %s\n", type, subtype);
			header_decode_lwsp(&inptr);
			while (*inptr == ';') {
				char *param, *value;
				struct _header_param *p;

				inptr++;
				/* invalid format? */
				if (header_decode_param(&inptr, &param, &value) != 0)
					break;

				p = g_malloc(sizeof(*p));
				p->name = param;
				p->value = value;
				p->next = t->params;
				t->params = p;
			}
		} else {
			g_free(type);
			printf("cannot find MIME subtype in header (1) '%s'", in);
		}
	} else {
		g_free(type);
		printf("cannot find MIME type in header (2) '%s'", in);
	}
	return t;
}

void
header_content_type_dump(struct _header_content_type *ct)
{
	struct _header_param *p;

	printf("Content-Type: ");
	if (ct==NULL) {
		printf("<NULL>\n");
		return;
	}
	printf("%s / %s", ct->type, ct->subtype);
	p = ct->params;
	if (p) {
		while (p) {
			printf(";\n\t%s=\"%s\"", p->name, p->value);
			p = p->next;
		}
	}
	printf("\n");
}

char *
header_content_encoding_decode(const char *in)
{
	return header_decode_token(&in);
}

/* hrm, is there a library for this shit? */
static struct {
	char *name;
	int offset;
} tz_offsets [] = {
	{ "UT", 0 },
	{ "GMT", 0 },
	{ "EST", -500 },	/* these are all US timezones.  bloody yanks */
	{ "EDT", -400 },
	{ "CST", -600 },
	{ "CDT", -500 },
	{ "MST", -700 },
	{ "MDT", -600 },
	{ "PST", -800 },
	{ "PDT", -700 },
	{ "Z", 0 },
	{ "A", -100 },
	{ "M", -1200 },
	{ "N", 100 },
	{ "Y", 1200 },
};

static char *tz_months [] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nove", "Dec"
};

char *
header_format_date(time_t time, int offset)
{
	struct tm tm;

	printf("offset = %d\n", offset);

	printf("converting date %s", ctime(&time));

	time += ((offset / 100) * (60*60)) + (offset % 100)*60;

	printf("converting date %s", ctime(&time));

	memcpy(&tm, gmtime(&time), sizeof(tm));

	return g_strdup_printf("%02d %s %04d %02d:%02d:%02d %c%04d",
			       tm.tm_mday, tz_months[tm.tm_mon],
			       tm.tm_year + 1900,
			       tm.tm_hour, tm.tm_min, tm.tm_sec,
			       offset>=0?'+':'-',
			       offset);
}

/* convert a date to time_t representation */
/* this is an awful mess oh well */
time_t
header_decode_date(const char *in, int *saveoffset)
{
	const char *inptr = in;
	char *monthname;
	int year, offset = 0;
	struct tm tm;
	int i;
	time_t t;

	printf("\ndecoding date '%s'\n", inptr);

	memset(&tm, 0, sizeof(tm));

	header_decode_lwsp(&inptr);
	if (!isdigit(*inptr)) {
		char *day = header_decode_token(&inptr);
		/* we dont really care about the day, its only for display */
		if (day) {
			printf("got day: %s\n", day);
			g_free(day);
			header_decode_lwsp(&inptr);
			if (*inptr == ',')
				inptr++;
			else
				printf("day not followed by ',', what gives?\n");
		}
	}
	tm.tm_mday = header_decode_int(&inptr);
	monthname = header_decode_token(&inptr);
	if (monthname) {
		for (i=0;i<sizeof(tz_months)/sizeof(tz_months[0]);i++) {
			if (!strcasecmp(tz_months[i], monthname)) {
				tm.tm_mon = i;
				break;
			}
		}
		g_free(monthname);
	}
	year = header_decode_int(&inptr);
	if (year<100) {
		tm.tm_year = year;
	} else {
		tm.tm_year = year-1900;
	}
	/* get the time ... yurck */
	tm.tm_hour = header_decode_int(&inptr);
	header_decode_lwsp(&inptr);
	if (*inptr == ':')
		inptr++;
	tm.tm_min = header_decode_int(&inptr);
	header_decode_lwsp(&inptr);
	if (*inptr == ':')
		inptr++;
	tm.tm_sec = header_decode_int(&inptr);
	header_decode_lwsp(&inptr);
	if (*inptr == '+'
	    || *inptr == '-') {
		offset = (*inptr++)=='-'?-1:1;
		offset = offset * header_decode_int(&inptr);
		printf("abs signed offset = %d\n", offset);
	} else if (isdigit(*inptr)) {
		offset = header_decode_int(&inptr);
		printf("abs offset = %d\n", offset);
	} else {
		char *tz = header_decode_token(&inptr);

		if (tz) {
			for (i=0;i<sizeof(tz_offsets)/sizeof(tz_offsets[0]);i++) {
				if (!strcasecmp(tz_offsets[i].name, tz)) {
					offset = tz_offsets[i].offset;
					break;
				}
			}
			g_free(tz);
		}
		/* some broken mailers seem to put in things like GMT+1030 instead of just +1030 */
		header_decode_lwsp(&inptr);
		if (*inptr == '+' || *inptr == '-') {
			int sign = (*inptr++)=='-'?-1:1;
			offset = offset + (header_decode_int(&inptr)*sign);
		}
		printf("named offset = %d\n", offset);
	}

	/*	t -= ( (offset/100) * 60*60) + (offset % 100)*60 + timezone;*/

	t = mktime(&tm) - timezone;

	/* t is now GMT of the time we want, but not offset by the timezone ... */

	printf(" gmt normalized? = %s\n", ctime(&t));

	/* this should convert the time to the GMT equiv time */
	t -= ( (offset/100) * 60*60) + (offset % 100)*60;

	printf(" gmt normalized for timezone? = %s\n", ctime(&t));

	printf(" encoded again: %s\n", header_format_date(t, offset));

	if (saveoffset)
		*saveoffset = offset;

	return t;
}

/* extra rfc checks */
#define CHECKS

#ifdef CHECKS
static void
check_header(struct _header_raw *h)
{
	unsigned char *p;

	p = h->value;
	while (*p) {
		if (!isascii(*p)) {
			g_warning("Appending header violates rfc: %s: %s", h->name, h->value);
			return;
		}
		p++;
	}
}
#endif

void
header_raw_append_parse(struct _header_raw **list, const char *header)
{
	register const char *in;
	int fieldlen;
	char *name;

	in = header;
	while (is_fieldname(*in))
		in++;
	fieldlen = in-header;
	while (is_lwsp(*in))
		in++;
	if (fieldlen == 0 || *in != ':') {
		printf("Invalid header line: '%s'\n", header);
		return;
	}
	in++;
	name = alloca(fieldlen+1);
	memcpy(name, header, fieldlen);
	name[fieldlen] = 0;
	header_raw_append(list, name, in);
}

void
header_raw_append(struct _header_raw **list, const char *name, const char *value)
{
	struct _header_raw *l, *n;

	printf("Header: %s: %s\n", name, value);

	n = g_malloc(sizeof(*n));
	n->next = NULL;
	n->name = g_strdup(name);
	n->value = g_strdup(value);
#ifdef CHECKS
	check_header(n);
#endif
	l = (struct _header_raw *)list;
	while (l->next) {
		l = l->next;
	}
	l->next = n;

	/* debug */
	if (!strcasecmp(name, "To")) {
		printf("- Decoding To\n");
		header_to_decode(value);
	} else if (!strcasecmp(name, "Content-type")) {
		printf("- Decoding content-type\n");
		header_content_type_dump(header_content_type_decode(value));		
	} else if (!strcasecmp(name, "MIME-Version")) {
		printf("- Decoding mime version\n");
		header_mime_decode(value);
	}
}

struct _header_raw *
header_raw_find_node(struct _header_raw **list, const char *name)
{
	struct _header_raw *l;

	l = *list;
	while (l) {
		if (!strcasecmp(l->name, name))
			break;
		l = l->next;
	}
	return l;
}

const char *
header_raw_find(struct _header_raw **list, const char *name)
{
	struct _header_raw *l;

	l = header_raw_find_node(list, name);
	if (l)
		return l->value;
	else
		return NULL;
}

static void
header_raw_free(struct _header_raw *l)
{
	g_free(l->name);
	g_free(l->value);
	g_free(l);
}

void
header_raw_remove(struct _header_raw **list, const char *name)
{
	struct _header_raw *l, *p;

	/* the next pointer is at the head of the structure, so this is safe */
	p = (struct _header_raw *)list;
	l = *list;
	while (l) {
		if (!strcasecmp(l->name, name)) {
			p->next = l->next;
			header_raw_free(l);
			l = p->next;
		} else {
			p = l;
			l = l->next;
		}
	}
}

void
header_raw_replace(struct _header_raw **list, const char *name, const char *value)
{
	header_raw_remove(list, name);
	header_raw_append(list, name, value);
}

void
header_raw_clear(struct _header_raw **list)
{
	struct _header_raw *l, *n;
	l = *list;
	while (l) {
		n = l->next;
		header_raw_free(l);
		l = n;
	}
	*list = NULL;
}



#ifdef BUILD_TABLE

/* for debugging tests */
/* should also have some regression tests somewhere */

void run_test(void)
{
	char *to = "gnome hacker dudes: license-discuss@opensource.org,
        \"Richard M. Stallman\" <rms@gnu.org>,
        Barry Chester <barry_che@antdiv.gov.au>,
        Michael Zucchi <zucchi.michael(this (is a nested) comment)@zedzone.mmc.com.au>,
        Miguel de Icaza <miguel@gnome.org>;,
	zucchi@zedzone.mmc.com.au, \"Foo bar\" <zed@zedzone>,
	<frob@frobzone>";

	header_to_decode(to);

	header_mime_decode("1.0");
	header_mime_decode("1.3 (produced by metasend V1.0)");
	header_mime_decode("(produced by metasend V1.0) 5.2");
	header_mime_decode("7(produced by metasend 1.0) . (produced by helix/send/1.0) 9 . 5");
	header_mime_decode("3.");
	header_mime_decode(".");
	header_mime_decode(".5");
	header_mime_decode("c.d");
	header_mime_decode("");

	header_msgid_decode(" <\"L3x2i1.0.Nm5.Xd-Wu\"@lists.redhat.com>");
	header_msgid_decode("<200001180446.PAA02065@beaker.htb.com.au>");

}

#endif /* BUILD_TABLE */
