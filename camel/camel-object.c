/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-object.c: Base class for Camel */

/*
 * Author:
 *  Dan Winship <danw@helixcode.com>
 *
 * Copyright 2000 Helix Code, Inc. (http://www.helixcode.com)
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
#include "camel-object.h"

/* I just mashed the keyboard for these... */
#define CAMEL_OBJECT_MAGIC_VALUE           0x77A344EF
#define CAMEL_OBJECT_CLASS_MAGIC_VALUE     0xEE26A990
#define CAMEL_OBJECT_FINALIZED_VALUE       0x84AC3656
#define CAMEL_OBJECT_CLASS_FINALIZED_VALUE 0x7621ABCD

#define DEFAULT_PREALLOCS 8

#define BAST_CASTARD 1 /* Define to return NULL when casts fail */

/* ** Quickie type system ************************************************* */

typedef struct _CamelTypeInfo {
	CamelType self;
	CamelType parent;
	const gchar *name;

	size_t instance_size;
	GMemChunk *instance_chunk;
	CamelObjectInitFunc instance_init;
	CamelObjectFinalizeFunc instance_finalize;
	GList *free_instances;

	size_t classfuncs_size;
	GMemChunk *classfuncs_chunk;
	CamelObjectClassInitFunc class_init;
	CamelObjectClassFinalizeFunc class_finalize;
	GList *free_classfuncs;

	gpointer global_classfuncs;
} CamelTypeInfo;

/* ************************************************************************ */

static void obj_init (CamelObject *obj);
static void obj_finalize (CamelObject *obj);
static void obj_class_init( CamelObjectClass *class );
static void obj_class_finalize( CamelObjectClass *class );

static gboolean shared_is_of_type( CamelObjectShared *sh, CamelType ctype, gboolean is_obj );
static gpointer make_global_classfuncs( CamelTypeInfo *type_info );

/* ************************************************************************ */

G_LOCK_DEFINE_STATIC( type_system );

static gboolean type_system_initialized = FALSE;
static GHashTable *ctype_to_typeinfo = NULL;
static const CamelType camel_object_type = 1;
static CamelType cur_max_type = CAMEL_INVALID_TYPE;

void camel_type_init( void )
{
	CamelTypeInfo *obj_info;

	G_LOCK( type_system );

	if( type_system_initialized ) {
		g_warning( "camel_type_init: type system already initialized." );
		G_UNLOCK( type_system );
		return;
	}

	type_system_initialized = TRUE;
	ctype_to_typeinfo = g_hash_table_new( g_direct_hash, g_direct_equal );
	
	obj_info = g_new( CamelTypeInfo, 1 );
	obj_info->self = camel_object_type;
	obj_info->parent = CAMEL_INVALID_TYPE;
	obj_info->name = "CamelObject";

	obj_info->instance_size = sizeof( CamelObject );
	obj_info->instance_chunk = g_mem_chunk_create( CamelObject, DEFAULT_PREALLOCS, G_ALLOC_ONLY );
	obj_info->instance_init = obj_init;
	obj_info->instance_finalize = obj_finalize;
	obj_info->free_instances = NULL;

	obj_info->classfuncs_size = sizeof( CamelObject );
	obj_info->classfuncs_chunk = g_mem_chunk_create( CamelObjectClass, DEFAULT_PREALLOCS, G_ALLOC_ONLY );
	obj_info->class_init = obj_class_init;
	obj_info->class_finalize = obj_class_finalize;
	obj_info->free_classfuncs = NULL;

	g_hash_table_insert( ctype_to_typeinfo, GINT_TO_POINTER( CAMEL_INVALID_TYPE ), NULL );
	g_hash_table_insert( ctype_to_typeinfo, GINT_TO_POINTER( camel_object_type ), obj_info );

	/* Sigh. Ugly */
	obj_info->global_classfuncs = make_global_classfuncs( obj_info );

	cur_max_type = camel_object_type;

	G_UNLOCK( type_system );
}

CamelType camel_type_register( CamelType parent, const gchar *name,
			       size_t instance_size, size_t classfuncs_size,
			       CamelObjectClassInitFunc class_init,
			       CamelObjectClassFinalizeFunc class_finalize,
			       CamelObjectInitFunc instance_init,
			       CamelObjectFinalizeFunc instance_finalize )
{
	CamelTypeInfo *parent_info;
	CamelTypeInfo *obj_info;
	gchar *chunkname;

	g_return_val_if_fail( parent != CAMEL_INVALID_TYPE, CAMEL_INVALID_TYPE );
	g_return_val_if_fail( name, CAMEL_INVALID_TYPE );
	g_return_val_if_fail( instance_size, CAMEL_INVALID_TYPE );
	g_return_val_if_fail( classfuncs_size, CAMEL_INVALID_TYPE );

	G_LOCK( type_system );

	if( type_system_initialized == FALSE ) {
		G_UNLOCK( type_system );
		camel_type_init();
		G_LOCK( type_system );
	}

	parent_info = g_hash_table_lookup( ctype_to_typeinfo, GINT_TO_POINTER( parent ) );

	if( parent_info == NULL ) {
		g_warning( "camel_type_register: no such parent type %d of class `%s'",
			   parent, name );
		G_UNLOCK( type_system );
		return CAMEL_INVALID_TYPE;
	}

	if( parent_info->instance_size > instance_size ) {
		g_warning( "camel_type_register: instance of class `%s' would be smaller than parent `%s'",
			   name, parent_info->name );
		G_UNLOCK( type_system );
		return CAMEL_INVALID_TYPE;
	}

	if( parent_info->classfuncs_size > classfuncs_size ) {
		g_warning( "camel_type_register: classfuncs of class `%s' would be smaller than parent `%s'",
			   name, parent_info->name );
		G_UNLOCK( type_system );
		return CAMEL_INVALID_TYPE;
	}
		
	cur_max_type++;

	obj_info = g_new( CamelTypeInfo, 1 );
	obj_info->self = cur_max_type;
	obj_info->parent = parent;
	obj_info->name = name;

	obj_info->instance_size = instance_size;
	chunkname = g_strdup_printf( "chunk for instances of Camel type `%s'", name );
	obj_info->instance_chunk = g_mem_chunk_new( chunkname, instance_size, 
						    instance_size * DEFAULT_PREALLOCS,
						    G_ALLOC_ONLY );
	g_free( chunkname );
	obj_info->instance_init = instance_init;
	obj_info->instance_finalize = instance_finalize;
	obj_info->free_instances = NULL;

	obj_info->classfuncs_size = classfuncs_size;
	chunkname = g_strdup_printf( "chunk for classfuncs of Camel type `%s'", name );
	obj_info->classfuncs_chunk = g_mem_chunk_new( chunkname, classfuncs_size, 
						    classfuncs_size * DEFAULT_PREALLOCS,
						    G_ALLOC_ONLY );
	g_free( chunkname );
	obj_info->class_init = class_init;
	obj_info->class_finalize = class_finalize;
	obj_info->free_classfuncs = NULL;

	g_hash_table_insert( ctype_to_typeinfo, GINT_TO_POINTER( obj_info->self ), obj_info );

	/* Sigh. Ugly. */
	obj_info->global_classfuncs = make_global_classfuncs( obj_info );

	G_UNLOCK( type_system );
	return obj_info->self;
}

CamelObjectClass *camel_type_get_global_classfuncs( CamelType type )
{
	CamelTypeInfo *type_info;

	g_return_val_if_fail( type != CAMEL_INVALID_TYPE, NULL );

	G_LOCK( type_system );
	type_info = g_hash_table_lookup( ctype_to_typeinfo, GINT_TO_POINTER( type ) );
	G_UNLOCK( type_system );

	g_return_val_if_fail( type_info != NULL, NULL );

	return type_info->global_classfuncs;
}

const gchar *camel_type_to_name( CamelType type )
{
	CamelTypeInfo *type_info;

	g_return_val_if_fail( type != CAMEL_INVALID_TYPE, "(the invalid type)" );

	G_LOCK( type_system );
	type_info = g_hash_table_lookup( ctype_to_typeinfo, GINT_TO_POINTER( type ) );
	G_UNLOCK( type_system );

	g_return_val_if_fail( type_info != NULL, "(a bad type parameter was specified)" );

	return type_info->name;
}

/* ** The CamelObject ***************************************************** */

static void
obj_init (CamelObject *obj)
{
	obj->s.magic = CAMEL_OBJECT_MAGIC_VALUE;
	obj->ref_count = 1;
}

static void
obj_finalize (CamelObject *obj)
{
	g_return_if_fail( obj->s.magic == CAMEL_OBJECT_MAGIC_VALUE );
	g_return_if_fail( obj->ref_count == 0 );

	obj->s.magic = CAMEL_OBJECT_FINALIZED_VALUE;
}

static void 
obj_class_init( CamelObjectClass *class )
{
	class->s.magic = CAMEL_OBJECT_CLASS_MAGIC_VALUE;
}

static void obj_class_finalize( CamelObjectClass *class )
{
	g_return_if_fail( class->s.magic == CAMEL_OBJECT_CLASS_MAGIC_VALUE );
	
	class->s.magic = CAMEL_OBJECT_CLASS_FINALIZED_VALUE;
}

CamelType camel_object_get_type (void)
{
	if( type_system_initialized == FALSE )
		camel_type_init();

	return camel_object_type;
}

CamelObject *camel_object_new( CamelType type )
{
	CamelTypeInfo *type_info;
	GList *parents = NULL;
	CamelObject *instance;
	CamelObjectClass *classfuncs;

	g_return_val_if_fail( type != CAMEL_INVALID_TYPE, NULL );

	/* Look up the type */

	G_LOCK( type_system );

	type_info = g_hash_table_lookup( ctype_to_typeinfo, GINT_TO_POINTER( type ) );

	if( type_info == NULL ) {
		g_warning( "camel_object_new: trying to create object of invalid type %d", type );
		G_UNLOCK( type_system );
		return NULL;
	}

	/* Grab an instance out of the freed ones if possible, alloc otherwise */

	if( type_info->free_instances ) {
		GList *first;

		first = g_list_first( type_info->free_instances );
		instance = first->data;
		type_info->free_instances = g_list_remove_link( type_info->free_instances, first );
		g_list_free_1( first );
	} else {
		instance = g_mem_chunk_alloc( type_info->instance_chunk );
	}

	/* Same with the classfuncs */

	if( type_info->free_classfuncs ) {
		GList *first;

		first = g_list_first( type_info->free_classfuncs );
		classfuncs = first->data;
		type_info->free_classfuncs = g_list_remove_link( type_info->free_classfuncs, first );
		g_list_free_1( first );
	} else {
		classfuncs = g_mem_chunk_alloc( type_info->classfuncs_chunk );
	}

	/* Init the instance and classfuncs a bit */

	instance->s.type = type;
	instance->classfuncs = classfuncs;
	classfuncs->s.type = type;

	/* Loop through the parents in simplest -> most complex order, initing the class and instance.
	 *
	 * When parent = CAMEL_INVALID_TYPE and we're at the end of the line, _lookup returns NULL
	 * because we inserted it as corresponding to CAMEL_INVALID_TYPE. Clever, eh?
	 */

	while( type_info ) {
		parents = g_list_prepend( parents, type_info );
		type_info = g_hash_table_lookup( ctype_to_typeinfo, GINT_TO_POINTER( type_info->parent ) );
	}

	for( ; parents && parents->data; parents = parents->next ) {
		CamelTypeInfo *thisinfo;

		thisinfo = parents->data;
		if( thisinfo->class_init )
			(thisinfo->class_init)( classfuncs );
		if( thisinfo->instance_init )
			(thisinfo->instance_init)( instance );
	}

	G_UNLOCK( type_system );
	return instance;
}

void camel_object_ref( CamelObject *obj )
{
	g_return_if_fail( CAMEL_IS_OBJECT( obj ) );

	obj->ref_count++;
}

void camel_object_unref( CamelObject *obj )
{
	CamelTypeInfo *type_info;
	CamelTypeInfo *iter;
	GList *parents = NULL;
	CamelObjectClass *classfuncs;

	g_return_if_fail( CAMEL_IS_OBJECT( obj ) );

	obj->ref_count--;

	if( obj->ref_count != 0 )
		return;

	/* Destroy it! hahaha! */

	G_LOCK( type_system );

	type_info = g_hash_table_lookup( ctype_to_typeinfo, GINT_TO_POINTER( obj->s.type ) );

	if( type_info == NULL ) {
		g_warning( "camel_object_unref: seemingly valid object has a bad type %d",
			   obj->s.type );
		G_UNLOCK( type_system );
		return;
	}

	/* Save this important information */

	classfuncs = CAMEL_OBJECT_CLASS( obj->classfuncs );

	/* Loop through the parents in most complex -> simplest order, finalizing the class 
	 * and instance.
	 *
	 * When parent = CAMEL_INVALID_TYPE and we're at the end of the line, _lookup returns NULL
	 * because we inserted it as corresponding to CAMEL_INVALID_TYPE. Clever, eh?
	 *
	 * Use iter to preserve type_info for free_{instance,classfunc}s
	 */

	iter = type_info;

	while( iter ) {
		parents = g_list_prepend( parents, iter );
		iter = g_hash_table_lookup( ctype_to_typeinfo, GINT_TO_POINTER( iter->parent ) );
	}

	parents = g_list_reverse( parents );

	for( ; parents && parents->data; parents = parents->next ) {
		CamelTypeInfo *thisinfo;

		thisinfo = parents->data;
		if( thisinfo->instance_finalize )
			(thisinfo->instance_finalize)( obj );
		if( thisinfo->class_finalize )
			(thisinfo->class_finalize)( classfuncs );
	}

	/* A little bit of cleaning up.
	 *
	 * Don't erase the type, so we can peek at it if a finalized object
	 * is check_cast'ed somewhere.
	 */

	obj->classfuncs = NULL;

	/* Tuck away the pointers for use in a new object */

	type_info->free_instances = g_list_prepend( type_info->free_instances, obj );
	type_info->free_classfuncs = g_list_prepend( type_info->free_classfuncs, obj );

	G_UNLOCK( type_system );
}

gboolean camel_object_is_of_type( CamelObject *obj, CamelType ctype )
{
	return shared_is_of_type( (CamelObjectShared *) obj, ctype, TRUE );
}

gboolean camel_object_class_is_of_type( CamelObjectClass *class, CamelType ctype )
{
	return shared_is_of_type( (CamelObjectShared *) class, ctype, FALSE );
}

#ifdef BAST_CASTARD
#define ERRVAL NULL
#else
#define ERRVAL obj
#endif

CamelObject *camel_object_check_cast( CamelObject *obj, CamelType ctype )
{
	if( shared_is_of_type( (CamelObjectShared *) obj, ctype, TRUE ) )
		return obj;
	return ERRVAL;
}
		   
CamelObjectClass *camel_object_class_check_cast( CamelObjectClass *class, CamelType ctype )
{
	if( shared_is_of_type( (CamelObjectShared *) class, ctype, FALSE ) )
		return class;
	return ERRVAL;
}

#undef ERRVAL

gchar *camel_object_describe( CamelObject *obj )
{
	CamelTypeInfo *type_info;

	if( obj == NULL )
		return g_strdup( "a NULL pointer" );

	if( obj->s.magic == CAMEL_OBJECT_MAGIC_VALUE ) {
		return g_strdup_printf( "an instance of `%s' at %p", 
					camel_type_to_name( obj->s.type ),
					obj );
	} else if( obj->s.magic == CAMEL_OBJECT_FINALIZED_VALUE ) {
		return g_strdup_printf( "a finalized instance of `%s' at %p", 
					camel_type_to_name( obj->s.type ),
					obj );
	} else if( obj->s.magic == CAMEL_OBJECT_CLASS_MAGIC_VALUE ) {
		return g_strdup_printf( "the classfuncs of `%s' at %p", 
					camel_type_to_name( obj->s.type ),
					obj );
	} else if( obj->s.magic == CAMEL_OBJECT_CLASS_FINALIZED_VALUE ) {
		return g_strdup_printf( "the finalized classfuncs of `%s' at %p", 
					camel_type_to_name( obj->s.type ),
					obj );
	}

	return g_strdup( "not a CamelObject" );
}

/* ** Static helpers ****************************************************** */

static gboolean
shared_is_of_type( CamelObjectShared *sh, CamelType ctype, gboolean is_obj )
{
	CamelTypeInfo *type_info;
	gchar *targtype;

	if( is_obj )
		targtype = "instance";
	else
		targtype = "classdata";

	if( ctype == CAMEL_INVALID_TYPE ) {
		g_warning( "shared_is_of_type: trying to cast to CAMEL_INVALID_TYPE" );
		return FALSE;
	}

	if( sh == NULL ) {
		g_warning( "shared_is_of_type: trying to cast NULL to %s of `%s'",
			   targtype, camel_type_to_name( ctype ) );
		return FALSE;
	}

	if( sh->magic == CAMEL_OBJECT_FINALIZED_VALUE ) {
		g_warning( "shared_is_of_type: trying to cast finalized instance "
			   "of `%s' into %s of `%s'",
			   camel_type_to_name( sh->type ),
			   targtype,
			   camel_type_to_name( ctype ) );
		return FALSE;
	}

	if( sh->magic == CAMEL_OBJECT_CLASS_FINALIZED_VALUE ) {
		g_warning( "shared_is_of_type: trying to cast finalized classdata "
			   "of `%s' into %s of `%s'",
			   camel_type_to_name( sh->type ),
			   targtype,
			   camel_type_to_name( ctype ) );
		return FALSE;
	}

	if( is_obj ) {
		if( sh->magic == CAMEL_OBJECT_CLASS_MAGIC_VALUE ) {
			g_warning( "shared_is_of_type: trying to cast classdata "
				   "of `%s' into instance of `%s'",
				   camel_type_to_name( sh->type ),
				   camel_type_to_name( ctype ) );
			return FALSE;
		}

		if( sh->magic != CAMEL_OBJECT_MAGIC_VALUE ) {
			g_warning( "shared_is_of_type: trying to cast junk data "
				   "into instance of `%s'",
				   camel_type_to_name( ctype ) );
			return FALSE;
		}
	} else {
		if( sh->magic == CAMEL_OBJECT_MAGIC_VALUE ) {
			g_warning( "shared_is_of_type: trying to cast instance "
				   "of `%s' into classdata of `%s'",
				   camel_type_to_name( sh->type ),
			   camel_type_to_name( ctype ) );
			return FALSE;
		}

		if( sh->magic != CAMEL_OBJECT_CLASS_MAGIC_VALUE ) {
			g_warning( "shared_is_of_type: trying to cast junk data "
				   "into classdata of `%s'",
				   camel_type_to_name( ctype ) );
			return FALSE;
		}
	}

	G_LOCK( type_system );

	type_info = g_hash_table_lookup( ctype_to_typeinfo, GINT_TO_POINTER( sh->type ) );

	if( type_info == NULL ) {
		g_warning( "shared_is_of_type: seemingly valid %s has "
			   "bad type %d.",
			   targtype, sh->type );
		G_UNLOCK( type_system );
		return FALSE;
	}

	while( type_info ) {
		if( type_info->self == ctype ) {
			G_UNLOCK( type_system );
			return TRUE;
		}

		type_info = g_hash_table_lookup( ctype_to_typeinfo, GINT_TO_POINTER( type_info->parent ) );
	}

	g_warning( "shared_is_of_type: %s of `%s' (@%p) is not also %s of `%s'",
		   targtype,
		   camel_type_to_name( sh->type ),
		   sh,
		   targtype,
		   camel_type_to_name( ctype ) );

	G_UNLOCK( type_system );
	return FALSE;
}

static gpointer make_global_classfuncs( CamelTypeInfo *type_info )
{
	CamelObjectClass *funcs;
	GList *parents;

	g_assert( type_info );

	funcs = g_mem_chunk_alloc( type_info->classfuncs_chunk );
	funcs->s.type = type_info->self;

	parents = NULL;
	while( type_info ) {
		parents = g_list_prepend( parents, type_info );
		type_info = g_hash_table_lookup( ctype_to_typeinfo, GINT_TO_POINTER( type_info->parent ) );
	}

	for( ; parents && parents->data ; parents = parents->next ) {
		CamelTypeInfo *thisinfo;

		thisinfo = parents->data;
		if( thisinfo->class_init )
			(thisinfo->class_init)( funcs );
	}

	return funcs;
}
