/*
 *  Copyright (C) 2000 Helix Code Inc.
 *
 *  Authors: Michael Zucchi <notzed@helixcode.com>
 *
 *  This program is free software; you can redistribute it and/or 
 *  modify it under the terms of the GNU General Public License as 
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 *  USA
 */

#ifndef _CAMEL_MIME_FILTER_FROM_H
#define _CAMEL_MIME_FILTER_FROM_H

#include <camel/camel-mime-filter.h>

#define CAMEL_MIME_FILTER_FROM(obj)         CAMEL_CHECK_CAST (obj, camel_mime_filter_from_get_type (), CamelMimeFilterFrom)
#define CAMEL_MIME_FILTER_FROM_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_mime_filter_from_get_type (), CamelMimeFilterFromClass)
#define IS_CAMEL_MIME_FILTER_FROM(obj)      CAMEL_CHECK_TYPE (obj, camel_mime_filter_from_get_type ())

typedef struct _CamelMimeFilterFrom      CamelMimeFilterFrom;
typedef struct _CamelMimeFilterFromClass CamelMimeFilterFromClass;

struct _CamelMimeFilterFrom {
	CamelMimeFilter parent;

	struct _CamelMimeFilterFromPrivate *priv;

	int midline;		/* are we between lines? */
};

struct _CamelMimeFilterFromClass {
	CamelMimeFilterClass parent_class;
};

guint		camel_mime_filter_from_get_type	(void);
CamelMimeFilterFrom      *camel_mime_filter_from_new	(void);

#endif /* ! _CAMEL_MIME_FILTER_FROM_H */
