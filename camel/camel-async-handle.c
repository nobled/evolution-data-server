/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-async.c: Utils for asynchronous operation */

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

#include <config.h>
#include "camel-async-handle.h"

typedef void (*GtkSignal_NONE__FLOAT)( GtkObject *, gfloat, gpointer );

static CamelObjectClass *parent_class = NULL;

#define MYCLASS(ah) (CAMEL_ASYNC_HANDLE_CLASS( (GTK_OBJECT( ah ))->klass ))

enum SIGNALS {
	HUP,
	PROGRESS_PCT,
	PROGRESS_STEP,
	EXCEPTION,
	STARTED,
	EVENT,
	FINISHED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void marshal_NONE__FLOAT( GtkObject *object, GtkSignalFunc func, gpointer func_data, GtkArg *args );
static void delayed_emission( CamelAsyncHandle *ao, guint sig, gfloat pct, gint step, gpointer data );

static void hup( CamelAsyncHandle *ao );
static void progress_pct( CamelAsyncHandle *ao, gfloat percentage );
static void progress_step( CamelAsyncHandle *ao, gint step );
static void exception( CamelAsyncHandle *ao );
static void started( CamelAsyncHandle *ao );
static void event( CamelAsyncHandle *ao, gpointer eventdata );
static void finished( CamelAsyncHandle *ao );
static void finalize( GtkObject *obj );

static void
camel_async_handle_init( CamelAsyncHandle *ao )
{
	ao->state = CAMEL_ASYNC_STATE_PENDING;
	ao->success = CAMEL_ASYNC_SUCCESS_INDETERMINATE;
	ao->ex = camel_exception_new();
	ao->priv = NULL;
}

static void
camel_async_handle_class_init( CamelAsyncHandleClass *myclass )
{
	GtkObjectClass *gtk_object_class =
		GTK_OBJECT_CLASS( myclass );

	parent_class = gtk_type_class( camel_object_get_type() );

	myclass->hup = hup;
	myclass->progress_pct = progress_pct;
	myclass->progress_step = progress_step;
	myclass->started = started;
	myclass->exception = exception;
	myclass->event = event;
	myclass->finished = finished;

	gtk_object_class->finalize = finalize;

	signals[HUP] = gtk_signal_new( "hup",
				       GTK_RUN_FIRST,
				       gtk_object_class->type,
				       GTK_SIGNAL_OFFSET( CamelAsyncHandleClass,
							  hup ),
				       gtk_marshal_NONE__NONE,
				       GTK_TYPE_NONE, 0 );

	signals[PROGRESS_PCT] = gtk_signal_new( "progress_pct",
				       GTK_RUN_FIRST,
				       gtk_object_class->type,
				       GTK_SIGNAL_OFFSET( CamelAsyncHandleClass,
							  progress_pct ),
				       marshal_NONE__FLOAT,
				       GTK_TYPE_NONE, 1, GTK_TYPE_FLOAT );

	signals[PROGRESS_STEP] = gtk_signal_new( "progress_step",
				       GTK_RUN_FIRST,
				       gtk_object_class->type,
				       GTK_SIGNAL_OFFSET( CamelAsyncHandleClass,
							  progress_step ),
				       gtk_marshal_NONE__INT,
				       GTK_TYPE_NONE, 1, GTK_TYPE_INT );

	signals[EXCEPTION] = gtk_signal_new( "exception",
				       GTK_RUN_FIRST,
				       gtk_object_class->type,
				       GTK_SIGNAL_OFFSET( CamelAsyncHandleClass,
							  exception ),
				       gtk_marshal_NONE__NONE,
				       GTK_TYPE_NONE, 0 );

	signals[STARTED] = gtk_signal_new( "started",
				       GTK_RUN_FIRST,
				       gtk_object_class->type,
				       GTK_SIGNAL_OFFSET( CamelAsyncHandleClass,
							  started ),
				       gtk_marshal_NONE__NONE,
				       GTK_TYPE_NONE, 0 );

	signals[EVENT] = gtk_signal_new( "event",
				       GTK_RUN_FIRST,
				       gtk_object_class->type,
				       GTK_SIGNAL_OFFSET( CamelAsyncHandleClass,
							  event ),
				       gtk_marshal_NONE__POINTER,
				       GTK_TYPE_NONE, 1, GTK_TYPE_POINTER );

	signals[FINISHED] = gtk_signal_new( "finished",
				       GTK_RUN_FIRST,
				       gtk_object_class->type,
				       GTK_SIGNAL_OFFSET( CamelAsyncHandleClass,
							  finished ),
				       gtk_marshal_NONE__NONE,
				       GTK_TYPE_NONE, 0 );

	gtk_object_class_add_signals( gtk_object_class, signals, LAST_SIGNAL );
}

GtkType
camel_async_handle_get_type (void)
{
        static GtkType camel_async_handle_type = 0;

        if (!camel_async_handle_type) {
                GtkTypeInfo camel_async_handle_info =
                {
                        "CamelAsyncHandle",
                        sizeof (CamelAsyncHandle),
                        sizeof (CamelAsyncHandleClass),
                        (GtkClassInitFunc) camel_async_handle_class_init,
                        (GtkObjectInitFunc) camel_async_handle_init,
                                /* reserved_1 */ NULL,
                                /* reserved_2 */ NULL,
                        (GtkClassInitFunc) NULL,
                };

                camel_async_handle_type = gtk_type_unique (camel_object_get_type (),
                                                     &camel_async_handle_info);
        }

        return camel_async_handle_type;
}

CamelAsyncHandle *camel_async_handle_new( CamelAsyncHandleProgressStyle progress_style,
					  gpointer user_data )
{
	CamelAsyncHandle *ao;

	ao = gtk_type_new( CAMEL_ASYNC_HANDLE_TYPE );
	ao->progress_style = progress_style;
	ao->userdata = user_data;

	return ao;
}

void camel_async_handle_hup( CamelAsyncHandle *ao, gboolean now )
{
	g_return_if_fail( ao );
	g_return_if_fail( CAMEL_IS_ASYNC_HANDLE( ao ) );

	if( now )
		gtk_signal_emit( GTK_OBJECT( ao ), signals[HUP] );
	else
		delayed_emission( ao, signals[HUP], 0.0, 0, NULL );
}

void camel_async_handle_started( CamelAsyncHandle *ao, gboolean now )
{
	g_return_if_fail( ao );
	g_return_if_fail( CAMEL_IS_ASYNC_HANDLE( ao ) );
	g_return_if_fail( ao->state == CAMEL_ASYNC_STATE_PENDING );

	if( now )
		gtk_signal_emit( GTK_OBJECT( ao ), signals[STARTED] );
	else
		delayed_emission( ao, signals[STARTED], 0.0, 0, NULL );
}

void camel_async_handle_finished( CamelAsyncHandle *ao, gboolean now )
{
	g_return_if_fail( ao );
	g_return_if_fail( CAMEL_IS_ASYNC_HANDLE( ao ) );
	g_return_if_fail( ao->state != CAMEL_ASYNC_STATE_IN_PROGRESS );

	if( now )
		gtk_signal_emit( GTK_OBJECT( ao ), signals[FINISHED] );
	else
		delayed_emission( ao, signals[FINISHED], 0.0, 0, NULL );
}

void camel_async_handle_event( CamelAsyncHandle *ao, gpointer eventdata, gboolean now )
{
	g_return_if_fail( ao );
	g_return_if_fail( CAMEL_IS_ASYNC_HANDLE( ao ) );
	g_return_if_fail( ao->state == CAMEL_ASYNC_STATE_IN_PROGRESS );
	g_return_if_fail( ao->success == CAMEL_ASYNC_SUCCESS_INDETERMINATE );

	if( now )
		gtk_signal_emit( GTK_OBJECT( ao ), signals[EVENT], eventdata );
	else
		delayed_emission( ao, signals[EVENT], 0.0, 0, eventdata );
}

void camel_async_handle_exception( CamelAsyncHandle *ao, gboolean now )
{
	g_return_if_fail( ao );
	g_return_if_fail( CAMEL_IS_ASYNC_HANDLE( ao ) );
	g_return_if_fail( ao->state == CAMEL_ASYNC_STATE_IN_PROGRESS );

	if( now )
		gtk_signal_emit( GTK_OBJECT( ao ), signals[EXCEPTION] );
	else
		delayed_emission( ao, signals[EXCEPTION], 0.0, 0, NULL );
}

void camel_async_handle_progress_pct( CamelAsyncHandle *ao, gfloat percentage, gboolean now )
{
	g_return_if_fail( ao );
	g_return_if_fail( CAMEL_IS_ASYNC_HANDLE( ao ) );
	g_assert( ao->progress_style == CAMEL_ASYNC_PROGRESS_PERCENTAGE );
	g_return_if_fail( ao->state == CAMEL_ASYNC_STATE_IN_PROGRESS );
	g_return_if_fail( ao->state == CAMEL_ASYNC_SUCCESS_INDETERMINATE );

	if( now )
		gtk_signal_emit( GTK_OBJECT( ao ), signals[PROGRESS_PCT], percentage );
	else
		delayed_emission( ao, signals[PROGRESS_PCT], percentage, 0, NULL );
}

void camel_async_handle_progress_step( CamelAsyncHandle *ao, gint step, gboolean now )
{
	g_return_if_fail( ao );
	g_return_if_fail( CAMEL_IS_ASYNC_HANDLE( ao ) );
	g_assert( ao->progress_style == CAMEL_ASYNC_PROGRESS_STEPS );
	g_return_if_fail( ao->state == CAMEL_ASYNC_STATE_IN_PROGRESS );
	g_return_if_fail( ao->state == CAMEL_ASYNC_SUCCESS_INDETERMINATE );

	if( now )
		gtk_signal_emit( GTK_OBJECT( ao ), signals[PROGRESS_STEP], step );
	else
		delayed_emission( ao, signals[PROGRESS_STEP], 0.0, step, NULL );
}

/* util funcs */

static void chain_finished( CamelAsyncHandle *ao, gpointer userdata );
static void chain_finished( CamelAsyncHandle *ao, gpointer userdata )
{
	camel_async_handle_finished( CAMEL_ASYNC_HANDLE( userdata ), TRUE );
}

void camel_async_handle_util_chain_finished( CamelAsyncHandle *ao, CamelAsyncHandle *parent )
{
	g_return_if_fail( ao );
	g_return_if_fail( CAMEL_IS_ASYNC_HANDLE( ao ) );
	g_return_if_fail( parent );
	g_return_if_fail( CAMEL_IS_ASYNC_HANDLE( parent ) );
	
	gtk_signal_connect( GTK_OBJECT( ao ), "finished", chain_finished, parent );
}

static void chain_exception( CamelAsyncHandle *ao, gpointer userdata );
static void chain_exception( CamelAsyncHandle *ao, gpointer userdata )
{
	camel_exception_xfer( (CAMEL_ASYNC_HANDLE( userdata ))->ex, ao->ex );
	camel_async_handle_exception( CAMEL_ASYNC_HANDLE( userdata ), TRUE );
}

void camel_async_handle_util_chain_exception( CamelAsyncHandle *ao, CamelAsyncHandle *parent )
{
	g_return_if_fail( ao );
	g_return_if_fail( CAMEL_IS_ASYNC_HANDLE( ao ) );
	g_return_if_fail( parent );
	g_return_if_fail( CAMEL_IS_ASYNC_HANDLE( parent ) );
	
	gtk_signal_connect( GTK_OBJECT( ao ), "exception", chain_exception, parent );
}

void camel_async_handle_util_self_destruct( CamelAsyncHandle *ao )
{
	g_return_if_fail( ao );
	g_return_if_fail( CAMEL_IS_ASYNC_HANDLE( ao ) );

	gtk_signal_connect( GTK_OBJECT( ao ), "finished", gtk_object_destroy, NULL );
}

void camel_async_handle_instant_exception( CamelAsyncHandle **handle, gpointer async_data, gchar *str )
{
	g_warning( "Async exception triggered: %s", str );

	CAMEL_ASYNC_MAYBE_NEW( handle, CAMEL_ASYNC_PROGRESS_ATOMIC, async_data );
	CAMEL_ASYNC_MAYBE_START( handle, FALSE );
	camel_exception_set( (*handle)->ex, CAMEL_EXCEPTION_SYSTEM, str );
	camel_async_handle_exception( (*handle), FALSE );
}

/* static funcs */

static void hup( CamelAsyncHandle *ao )
{
}

static void progress_pct( CamelAsyncHandle *ao, gfloat percentage )
{
}

static void progress_step( CamelAsyncHandle *ao, gint step )
{
}

static void exception( CamelAsyncHandle *ao )
{
	ao->success = CAMEL_ASYNC_SUCCESS_EXCEPTION;

	camel_async_handle_finished( ao, FALSE );
}

static void started( CamelAsyncHandle *ao )
{
	ao->state = CAMEL_ASYNC_STATE_IN_PROGRESS;
	ao->success = CAMEL_ASYNC_SUCCESS_INDETERMINATE;
}

static void event( CamelAsyncHandle *ao, gpointer eventdata )
{
}

static void finished( CamelAsyncHandle *ao )
{
	ao->state = CAMEL_ASYNC_STATE_FINISHED;

	if( ao->success == CAMEL_ASYNC_SUCCESS_INDETERMINATE )
		ao->success = CAMEL_ASYNC_SUCCESS_SUCCEEDED;
}

static void finalize( GtkObject *obj )
{
	CamelAsyncHandle *ao = CAMEL_ASYNC_HANDLE( obj );

	if( ao->state != CAMEL_ASYNC_STATE_FINISHED )
		g_warning( "Async handle being destroyed before it is finished." );

	if( ao->success == CAMEL_ASYNC_SUCCESS_EXCEPTION )
		g_warning( "Async handle got an exception: %s",
			   camel_exception_get_description( ao->ex ) );

	if( ao->ex ) { 
		camel_exception_free( ao->ex );
		ao->ex = NULL;
	}

	ao->priv = NULL;
}

static void marshal_NONE__FLOAT( GtkObject *object, GtkSignalFunc func, gpointer func_data, GtkArg *args )
{
	GtkSignal_NONE__FLOAT rfunc;

	rfunc = (GtkSignal_NONE__FLOAT) func;

	(*rfunc)( object, GTK_VALUE_FLOAT( args[0] ), func_data );
}

/* Delayed signal emission */

struct delayed_info_s {
	CamelAsyncHandle *ao;
	guint sig;
	gfloat pct; 
	gint step; 
	gpointer data;
};

static gboolean delayed_emit( gpointer data );

static void delayed_emission( CamelAsyncHandle *ao, guint sig, gfloat pct, gint step, gpointer data )
{
	struct delayed_info_s *di;

	di = g_new( struct delayed_info_s, 1 );
	di->ao = ao;
	di->sig = sig;
	di->pct = pct;
	di->step = step;
	di->data = data;

	g_idle_add( delayed_emit, di );
}

static gboolean delayed_emit( gpointer data )
{
	struct delayed_info_s *di = (struct delayed_info_s *) data;

	/* We're an idle func. Here we check to make sure that
	 * we're in a valid state to emit some of the signals,
	 * as there will be chunks of code that go:
	 *    camel_async_handle_started( h, FALSE );
	 *    camel_async_handle_event( h, pointer, FALSE );
	 *    camel_async_handle_finished( h, FALSE );
	 * We need to make sure they're emitted in the right order.
	 * Depending on our state, we postpone the action or cancel
	 * it altogether. We also need to make sure that we don't hang
	 * around after di is destroyed, et al.
	 */

	if( GTK_OBJECT_DESTROYED( GTK_OBJECT(di->ao) ) ||
	    di->ao->state == CAMEL_ASYNC_STATE_FINISHED ) {
		g_free( di );
		return FALSE;
	}

	if( di->sig == signals[EXCEPTION] ) {
		if( di->ao->state != CAMEL_ASYNC_STATE_IN_PROGRESS )
			return TRUE;
	} else if( di->sig == signals[FINISHED] ) {
		/* We only want to cancel if an exception was emitted
		 * and then a finish -- not when the exception emits
		 * a finish. 
		 */
		if( di->ao->success != CAMEL_ASYNC_SUCCESS_EXCEPTION &&
		    camel_exception_is_set( di->ao->ex ) ) {
			g_free( di );
			return FALSE;
		}
		if( di->ao->state != CAMEL_ASYNC_STATE_IN_PROGRESS )
			return TRUE;
	} else if( di->sig == signals[EVENT] ) {
		if( di->ao->success != CAMEL_ASYNC_SUCCESS_INDETERMINATE )
			return TRUE;
		if( di->ao->state != CAMEL_ASYNC_STATE_IN_PROGRESS )
			return TRUE;
	}

	if( di->sig == signals[PROGRESS_PCT] )
		gtk_signal_emit( GTK_OBJECT( di->ao ), di->sig, di->pct );
	else if( di->sig == signals[PROGRESS_STEP] )
		gtk_signal_emit( GTK_OBJECT( di->ao ), di->sig, di->step );
	else if( di->sig == signals[EVENT] )
		gtk_signal_emit( GTK_OBJECT( di->ao ), di->sig, di->data );
	else
		gtk_signal_emit( GTK_OBJECT( di->ao ), di->sig );

	g_free( di );
	return FALSE;
}
