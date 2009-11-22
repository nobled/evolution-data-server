/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-object.h: Base class for Camel */
/*
 * Authors:
 *  Dan Winship <danw@ximian.com>
 *  Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_OBJECT_H
#define CAMEL_OBJECT_H

#include <glib.h>
#include <glib-object.h>
#include <stdio.h>		/* FILE */
#include <stdlib.h>		/* gsize */
#include <stdarg.h>
#include <pthread.h>

#include <camel/camel-arg.h>

/* The CamelObjectBag API was originally defined in this header,
 * so include it here for backward-compatibility. */
#include <camel/camel-object-bag.h>

/* Standard GObject macros */
#define CAMEL_TYPE_OBJECT \
	(camel_object_get_type ())
#define CAMEL_OBJECT(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_OBJECT, CamelObject))
#define CAMEL_OBJECT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_OBJECT, CamelObjectClass))
#define CAMEL_IS_OBJECT(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_OBJECT))
#define CAMEL_IS_OBJECT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_OBJECT))
#define CAMEL_OBJECT_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_OBJECT, CamelObjectClass))

#define CAMEL_ERROR \
	(camel_error_quark ())

G_BEGIN_DECLS

typedef struct _CamelObjectClass CamelObjectClass;
typedef struct _CamelObject CamelObject;
typedef guint CamelObjectHookID;
typedef struct _CamelObjectMeta CamelObjectMeta;

typedef gboolean (*CamelObjectEventPrepFunc) (CamelObject *, gpointer);
typedef void (*CamelObjectEventHookFunc) (CamelObject *, gpointer, gpointer);

typedef enum {
	CAMEL_ERROR_SYSTEM,
	CAMEL_ERROR_USER_CANCEL
} CamelError;

/* camel object args. */
enum {
	/* Get a description of the object. */
	CAMEL_OBJECT_ARG_DESCRIPTION = CAMEL_ARG_FIRST,
	/* Get a copy of the meta-data list (should be freed) */
	CAMEL_OBJECT_ARG_METADATA,
	CAMEL_OBJECT_ARG_STATE_FILE,
	CAMEL_OBJECT_ARG_PERSISTENT_PROPERTIES
};

enum {
	CAMEL_OBJECT_DESCRIPTION = CAMEL_OBJECT_ARG_DESCRIPTION | CAMEL_ARG_STR,
	/* Returns a CamelObjectMeta list */
	CAMEL_OBJECT_METADATA = CAMEL_OBJECT_ARG_METADATA | CAMEL_ARG_PTR,
	/* sets where the persistent data should reside, otherwise it isn't persistent */
	CAMEL_OBJECT_STATE_FILE = CAMEL_OBJECT_ARG_STATE_FILE | CAMEL_ARG_STR,
	/* returns a GSList CamelProperties of persistent properties */
	CAMEL_OBJECT_PERSISTENT_PROPERTIES = CAMEL_OBJECT_ARG_PERSISTENT_PROPERTIES | CAMEL_ARG_PTR
};

/* returned by get::CAMEL_OBJECT_METADATA */
struct _CamelObjectMeta {
	CamelObjectMeta *next;

	gchar *value;
	gchar name[1];		/* allocated as part of structure */
};

struct _CamelObject {
	GObject parent;

	/* current hooks on this object */
	struct _CamelHookList *hooks;
};

struct _CamelObjectClass {
	GObjectClass parent_class;

	/* available hooks for this class */
	struct _CamelHookPair *hooks;

	/* root-class fields follow, type system above */

	/* get/set interface */
	gint (*setv)(CamelObject *, GError **error, CamelArgV *args);
	gint (*getv)(CamelObject *, GError **error, CamelArgGetV *args);
	/* we only free 1 at a time, and only pointer types, obviously */
	void (*free)(CamelObject *, guint32 tag, gpointer ptr);

	/* get/set meta-data interface */
	gchar *(*meta_get)(CamelObject *, const gchar * name);
	gboolean (*meta_set)(CamelObject *, const gchar * name, const gchar *value);

	/* persistence stuff */
	gint (*state_read)(CamelObject *, FILE *fp);
	gint (*state_write)(CamelObject *, FILE *fp);
};

/* object class methods (types == classes now) */
void camel_object_class_add_event (CamelObjectClass *klass, const gchar *name, CamelObjectEventPrepFunc prep);

GType camel_object_get_type (void);
GQuark camel_error_quark (void) G_GNUC_CONST;

/* hooks */
CamelObjectHookID camel_object_hook_event(gpointer obj, const gchar *name, CamelObjectEventHookFunc hook, gpointer data);
void camel_object_remove_event(gpointer obj, CamelObjectHookID id);
void camel_object_unhook_event(gpointer obj, const gchar *name, CamelObjectEventHookFunc hook, gpointer data);
void camel_object_trigger_event(gpointer obj, const gchar *name, gpointer event_data);

/* get/set methods */
gint camel_object_set(gpointer obj, GError **error, ...);
gint camel_object_setv(gpointer obj, GError **error, CamelArgV *);
gint camel_object_get(gpointer obj, GError **error, ...);
gint camel_object_getv(gpointer obj, GError **error, CamelArgGetV *);

/* not very efficient one-time calls */
gpointer camel_object_get_ptr(gpointer vo, GError **error, gint tag);
gint camel_object_get_int(gpointer vo, GError **error, gint tag);

/* meta-data for user-specific data */
gchar *camel_object_meta_get(gpointer vo, const gchar * name);
gboolean camel_object_meta_set(gpointer vo, const gchar * name, const gchar *value);

/* reads/writes the state from/to the CAMEL_OBJECT_STATE_FILE */
gint camel_object_state_read(gpointer vo);
gint camel_object_state_write(gpointer vo);

/* free a retrieved object.  May be a noop for static data. */
void camel_object_free(gpointer vo, guint32 tag, gpointer value);

G_END_DECLS

#endif /* CAMEL_OBJECT_H */
