/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-async.h: Utils for asynchronous operation */

/* 
 * Author: 
 *  Peter Williams <peterw@helixcode.com>
 *
 * Copyright 1999, 2000 Helix Code, Inc. (http://www.helixcode.com)
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

/**
 * This design is (cough) "based" on that of Gnome VFS
 * which as I understand it is pretty elegant.
 *
 * We extend that async handle concept into a GTK+ object
 * that can deliver signals, instead of rely on passing
 * a whole bunch of FooCallback parameters.
 **/

#ifndef CAMEL_ASYNC_H
#define CAMEL_ASYNC_H 1

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include <camel/camel-object.h>
#include <camel/camel-exception.h>

#define CAMEL_ASYNC_HANDLE_TYPE     (camel_async_handle_get_type ())
#define CAMEL_ASYNC_HANDLE(obj)     (GTK_CHECK_CAST((obj), CAMEL_ASYNC_HANDLE_TYPE, CamelAsyncHandle))
#define CAMEL_ASYNC_HANDLE_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), CAMEL_ASYNC_HANDLE_TYPE, CamelAsyncHandleClass))
#define CAMEL_IS_ASYNC_HANDLE(o)    (GTK_CHECK_TYPE((o), CAMEL_ASYNC_HANDLE_TYPE))

typedef enum _CamelAsyncHandleState CamelAsyncHandleState;

enum _CamelAsyncHandleState {
	CAMEL_ASYNC_STATE_PENDING,
	CAMEL_ASYNC_STATE_IN_PROGRESS,
	CAMEL_ASYNC_STATE_FINISHED
};

typedef enum _CamelAsyncHandleProgressStyle CamelAsyncHandleProgressStyle;

enum _CamelAsyncHandleProgressStyle {
	CAMEL_ASYNC_PROGRESS_PERCENTAGE,
	CAMEL_ASYNC_PROGRESS_ATOMIC,
	CAMEL_ASYNC_PROGRESS_STEPS
};

typedef enum _CamelAsyncHandleSuccess CamelAsyncHandleSuccess;

enum _CamelAsyncHandleSuccess {
	CAMEL_ASYNC_SUCCESS_SUCCEEDED,
	CAMEL_ASYNC_SUCCESS_EXCEPTION,
	CAMEL_ASYNC_SUCCESS_CANCELLED,
	CAMEL_ASYNC_SUCCESS_INDETERMINATE
};

typedef struct _CamelAsyncHandle CamelAsyncHandle;

struct _CamelAsyncHandle {
	CamelObject parent;

	CamelAsyncHandleState state;
	CamelAsyncHandleProgressStyle progress_style;
	CamelAsyncHandleSuccess success;
	gpointer userdata;
	CamelException *ex;

	gpointer priv;
};

typedef struct _CamelAsyncHandleClass CamelAsyncHandleClass;

struct _CamelAsyncHandleClass {
	CamelObjectClass parent_class;

	/* signals */
	void (*hup)( CamelAsyncHandle *ao );
	void (*progress_pct)( CamelAsyncHandle *ao, gfloat percentage );
	void (*progress_step)( CamelAsyncHandle *ao, gint step );
	void (*exception)( CamelAsyncHandle *ao );
	void (*started)( CamelAsyncHandle *ao );
	void (*event)( CamelAsyncHandle *ao, gpointer eventdata );
	void (*finished)( CamelAsyncHandle *ao );
};

/* daduuum! */

GtkType camel_async_handle_get_type (void);

/* public methods */

CamelAsyncHandle *camel_async_handle_new( CamelAsyncHandleProgressStyle progress_style,
					  gpointer user_data );

void camel_async_handle_hup( CamelAsyncHandle *ao, gboolean now );
void camel_async_handle_started( CamelAsyncHandle *ao, gboolean now );
void camel_async_handle_finished( CamelAsyncHandle *ao, gboolean now );
void camel_async_handle_event( CamelAsyncHandle *ao, gpointer eventdata, gboolean now );
void camel_async_handle_exception( CamelAsyncHandle *ao, gboolean now );
void camel_async_handle_progress_pct( CamelAsyncHandle *ao, gfloat percentage, gboolean now );
void camel_async_handle_progress_step( CamelAsyncHandle *ao, gint step, gboolean now );

/* Utilities */

/* Have @ao pass its event on to @parent */
void camel_async_handle_util_chain_exception( CamelAsyncHandle *ao, CamelAsyncHandle *parent );
void camel_async_handle_util_chain_finished( CamelAsyncHandle *ao, CamelAsyncHandle *parent );

/* Call gtk_object_destroy on @ao when it is 'finished' */
void camel_async_handle_util_self_destruct( CamelAsyncHandle *ao );

/* Trigger an exception now on the handle pointer */
void camel_async_handle_instant_exception( CamelAsyncHandle **handle, gpointer async_data, gchar *str );

#define CAMEL_ASYNC_MAYBE_NEW( pp, s, ud )                         \
        G_STMT_START {                                             \
                if( (*(pp)) == NULL ) {                            \
                        (*(pp)) = camel_async_handle_new( s, ud ); \
                }                                                  \
        } G_STMT_END

#define CAMEL_ASYNC_MAYBE_START( pp, now )                          \
        G_STMT_START {                                              \
                if( (*(pp))->state == CAMEL_ASYNC_STATE_PENDING ) { \
                        camel_async_handle_started( (*(pp)), now ); \
                }                                                   \
        } G_STMT_END

#define EXCEPTION_IF_FAIL( expr, message )                                           \
        G_STMT_START { if( !( expr ) ) {                                             \
                g_log( G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,                           \
                       "file %s: line %d (%s): exception because `%s\' failed.",     \
                       __FILE__, __LINE__, __PRETTY_FUNCTION__,                      \
                       #expr );                                                      \
                camel_async_handle_instant_exception( handle, async_data, message ); \
                return;                                                              \
        } } G_STMT_END

#define EXCEPTION_IF_FAIL_FULL( handle, async_data, expr, message )                  \
        G_STMT_START { if( !( expr ) ) {                                             \
                g_log( G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,                           \
                       "file %s: line %d (%s): exception because `%s\' failed.",     \
                       __FILE__, __LINE__, __PRETTY_FUNCTION__,                      \
                       #expr );                                                      \
                camel_async_handle_instant_exception( handle, async_data, message ); \
                return;                                                              \
        } } G_STMT_END

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_ASYNC_H */

