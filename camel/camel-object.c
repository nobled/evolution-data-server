/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *
 *
 * Author:
 *  Dan Winship <danw@ximian.com>
 *  Michael Zucchi <notzed@ximian.com>
 *
 * Copyright 2000 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include "camel-object.h"

#include <e-util/e-memory.h>

#ifdef ENABLE_THREADS
#include <pthread.h>
#include <e-util/e-msgport.h>
#endif

/* I just mashed the keyboard for these... */
#define CAMEL_OBJECT_MAGIC           	 0x77A344ED
#define CAMEL_OBJECT_CLASS_MAGIC     	 0xEE26A997
#define CAMEL_OBJECT_FINALISED_MAGIC       0x84AC365F
#define CAMEL_OBJECT_CLASS_FINALISED_MAGIC 0x7621ABCD

/* ** Quickie type system ************************************************* */

/* A 'locked' hooklist, that is only allocated on demand */
typedef struct _CamelHookList {
	EMutex *lock;

	unsigned int depth:30;	/* recursive event depth */
	unsigned int flags:2;	/* flags, see below */

	unsigned int list_length;
	struct _CamelHookPair *list;
} CamelHookList;

#define CAMEL_HOOK_PAIR_REMOVED (1<<0)

/* a 'hook pair', actually a hook tuple, we just store all hooked events in the same list,
   and just comapre as we go, rather than storing separate lists for each hook type

   the name field just points directly to the key field in the class's preplist hashtable.
   This way we can just use a direct pointer compare when scanning it, and also saves
   copying the string */
typedef struct _CamelHookPair
{
	struct _CamelHookPair *next; /* next MUST be the first member */

	unsigned int id:30;
	unsigned int flags:2;	/* removed, etc */

	const char *name;	/* points to the key field in the classes preplist, static memory */
	union {
		CamelObjectEventHookFunc event;
		CamelObjectEventPrepFunc prep;
	} func;
	void *data;
} CamelHookPair;

/* ********************************************************************** */

static void camel_object_free_hooks(CamelObject *o);

/* ********************************************************************** */

static pthread_mutex_t chunks_lock = PTHREAD_MUTEX_INITIALIZER;

static EMemChunk *pair_chunks;
static EMemChunk *hook_chunks;
static unsigned int pair_id = 1;

static EMutex *type_lock;

static GHashTable *type_table;
static EMemChunk *type_chunks;

CamelType camel_object_type;

#ifdef ENABLE_THREADS
#define P_LOCK(l) (pthread_mutex_lock(&l))
#define P_UNLOCK(l) (pthread_mutex_unlock(&l))
#define E_LOCK(l) (e_mutex_lock(l))
#define E_UNLOCK(l) (e_mutex_unlock(l))
#define CLASS_LOCK(k) (g_mutex_lock((((CamelObjectClass *)k)->lock)))
#define CLASS_UNLOCK(k) (g_mutex_unlock((((CamelObjectClass *)k)->lock)))
#else
#define P_LOCK(l)
#define P_UNLOCK(l)
#define E_LOCK(l)
#define E_UNLOCK(l)
#define CLASS_LOCK(k)
#define CLASS_UNLOCK(k)
#endif

static struct _CamelHookPair *
pair_alloc(void)
{
	CamelHookPair *pair;

	P_LOCK(chunks_lock);
	pair = e_memchunk_alloc(pair_chunks);
	pair->id = pair_id++;
	if (pair_id == 0)
		pair_id = 1;
	P_UNLOCK(chunks_lock);

	return pair;
}

static void
pair_free(CamelHookPair *pair)
{
	g_assert(pair_chunks != NULL);

	P_LOCK(chunks_lock);
	e_memchunk_free(pair_chunks, pair);
	P_UNLOCK(chunks_lock);
}

static struct _CamelHookList *
hooks_alloc(void)
{
	CamelHookList *hooks;

	P_LOCK(chunks_lock);
	hooks = e_memchunk_alloc(hook_chunks);
	P_UNLOCK(chunks_lock);

	return hooks;
}

static void
hooks_free(CamelHookList *hooks)
{
	g_assert(hook_chunks != NULL);

	P_LOCK(chunks_lock);
	e_memchunk_free(hook_chunks, hooks);
	P_UNLOCK(chunks_lock);
}

/* not checked locked, who cares, only required for people that want to redefine root objects */
void
camel_type_init(void)
{
	static int init = FALSE;

	if (init)
		return;

	init = TRUE;
	pair_chunks = e_memchunk_new(16, sizeof(CamelHookPair));
	hook_chunks = e_memchunk_new(16, sizeof(CamelHookList));
	type_lock = e_mutex_new(E_MUTEX_REC);
	type_chunks = e_memchunk_new(32, sizeof(CamelType));
	type_table = g_hash_table_new(NULL, NULL);
}

/* ************************************************************************ */

/* Should this return the object to the caller? */
static void
cobject_init (CamelObject *o, CamelObjectClass *klass)
{
	o->klass = klass;
	o->magic = CAMEL_OBJECT_MAGIC;
	o->ref_count = 1;
	o->flags = 0;
}

static void
cobject_finalise(CamelObject *o)
{
	g_assert(o->ref_count == 0);

	camel_object_free_hooks(o);

	o->magic = CAMEL_OBJECT_FINALISED_MAGIC;
	o->klass = NULL;
}

static int
cobject_getv(CamelObject *o, CamelException *ex, CamelArgGetV *args)
{
	/* could have flags or stuff here? */
	return 0;
}

static int
cobject_setv(CamelObject *o, CamelException *ex, CamelArgV *args)
{
	/* could have flags or stuff here? */
	return 0;
}

static void
cobject_class_init(CamelObjectClass *klass)
{
	klass->magic = CAMEL_OBJECT_CLASS_MAGIC;

	klass->getv = cobject_getv;
	klass->setv = cobject_setv;

	camel_object_class_add_event(klass, "finalize", NULL);
}

static void
cobject_class_finalise(CamelObjectClass * klass)
{
	klass->magic = CAMEL_OBJECT_CLASS_FINALISED_MAGIC;

	g_free(klass);
}

CamelType
camel_object_get_type (void)
{
	if (camel_object_type == CAMEL_INVALID_TYPE) {
		camel_type_init();

		camel_object_type = camel_type_register(NULL, "CamelObject", /*, 0, 0*/
							sizeof(CamelObject), sizeof(CamelObjectClass),
							cobject_class_init, cobject_class_finalise,
							cobject_init, cobject_finalise);
	}

	return camel_object_type;
}

static void
camel_type_class_init(CamelObjectClass *klass, CamelObjectClass *type)
{
	if (type->parent)
		camel_type_class_init(klass, type->parent);

	if (type->klass_init)
		type->klass_init(klass);
}

CamelType
camel_type_register (CamelType parent, const char * name,
		     /*unsigned int ver, unsigned int rev,*/
		     size_t object_size, size_t klass_size,
		     CamelObjectClassInitFunc class_init,
		     CamelObjectClassFinalizeFunc class_finalise,
		     CamelObjectInitFunc object_init,
		     CamelObjectFinalizeFunc object_finalise)
{
	CamelObjectClass *klass;
	/*int offset;
	  size_t size;*/

	if (parent != NULL && parent->magic != CAMEL_OBJECT_CLASS_MAGIC) {
		g_warning("camel_type_register: invalid junk parent class for '%s'", name);
		return NULL;
	}

	E_LOCK(type_lock);

	/* Have to check creation, it might've happened in another thread before we got here */
	klass = g_hash_table_lookup(type_table, name);
	if (klass != NULL) {
		if (klass->klass_size != klass_size || klass->object_size != object_size
		    || klass->klass_init != class_init || klass->klass_finalise != class_finalise
		    || klass->init != object_init || klass->finalise != object_finalise) {
			g_warning("camel_type_register: Trying to re-register class '%s'", name);
			klass = NULL;
		}
		E_UNLOCK(type_lock);
		return klass;
	}

	/* this is for objects with no parent as part of their struct ('interfaces'?) */
	/*offset = parent?parent->klass_size:0;
	offset = (offset + 3) & (~3);

	size = offset + klass_size;

	klass = g_malloc0(size);

	klass->klass_size = size;
	klass->klass_data = offset;

	offset = parent?parent->object_size:0;
	offset = (offset + 3) & (~3);

	klass->object_size = offset + object_size;
	klass->object_data = offset;*/

	if (parent
	    && klass_size < parent->klass_size) {
		g_warning("camel_type_register: '%s' has smaller class size than parent '%s'", name, parent->name);
		E_UNLOCK(type_lock);
		return NULL;
	}

	klass = g_malloc0(klass_size);
	klass->klass_size = klass_size;
	klass->object_size = object_size;	
#ifdef ENABLE_THREADS
	klass->lock = g_mutex_new();
#endif
	klass->instance_chunks = e_memchunk_new(8, object_size);

	klass->parent = parent;
	if (parent) {
		klass->next = parent->child;
		parent->child = klass;
	}
	klass->name = name;

	/*klass->version = ver;
	  klass->revision = rev;*/

	klass->klass_init = class_init;
	klass->klass_finalise = class_finalise;

	klass->init = object_init;
	klass->finalise = object_finalise;

	/* setup before class init, incase class init func uses the type or looks it up ? */
	g_hash_table_insert(type_table, (void *)name, klass);

	camel_type_class_init(klass, klass);

	E_UNLOCK(type_lock);

	return klass;
}

static void
camel_object_init(CamelObject *o, CamelObjectClass *klass, CamelType type)
{
	if (type->parent)
		camel_object_init(o, klass, type->parent);

	if (type->init)
		type->init(o, klass);
}

CamelObject *
camel_object_new(CamelType type)
{
	CamelObject *o;

	if (type == NULL)
		return NULL;

	if (type->magic != CAMEL_OBJECT_CLASS_MAGIC)
		return NULL;

	CLASS_LOCK(type);
	o = e_memchunk_alloc0(type->instance_chunks);

#ifdef CAMEL_OBJECT_TRACK_INSTANCES
	if (type->instances)
		type->instances->prev = o;
	o->next = type->instances;
	o->prev = NULL;
	type->instances = o;
#endif

	CLASS_UNLOCK(type);
	camel_object_init(o, type, type);

	return o;
}

void
camel_object_ref(CamelObject *o)
{
	CLASS_LOCK(o->klass);
	o->ref_count++;
	CLASS_UNLOCK(o->klass);
}

void
camel_object_unref(CamelObject *o)
{
	register CamelObjectClass *klass = o->klass, *k;
	
	CLASS_LOCK(klass);
	o->ref_count--;
	if (o->ref_count > 0
	    || (o->flags & CAMEL_OBJECT_DESTROY)) {
		CLASS_UNLOCK(klass);
		return;
	}

	o->flags |= CAMEL_OBJECT_DESTROY;

	CLASS_UNLOCK(klass);

	camel_object_trigger_event(o, "finalize", NULL);

	k = klass;
	while (k) {
		if (k->finalise)
			k->finalise(o);
		k = k->parent;
	}

	o->magic = CAMEL_OBJECT_FINALISED_MAGIC;

	CLASS_LOCK(klass);
#ifdef CAMEL_OBJECT_TRACK_INSTANCES
	if (o->prev)
		o->prev->next = o->next;
	else
		klass->instances = o->next;
	if (o->next)
		o->next->prev = o->prev;
#endif
	e_memchunk_free(klass->instance_chunks, o);
	CLASS_UNLOCK(klass);
}

const char *
camel_type_to_name (CamelType type)
{
	if (type == NULL)
		return "(NULL class)";

	if (type->magic == CAMEL_OBJECT_CLASS_MAGIC)
		return type->name;

	return "(Junk class)";
}

CamelType camel_name_to_type(const char *name)
{
	/* TODO: Load a class off disk (!) */

	return g_hash_table_lookup(type_table, name);
}

static char *
desc_data(CamelObject *o, int ok)
{
	char *what;

	if (o == NULL)
		what = g_strdup("NULL OBJECT");
	else if (o->magic == ok)
		what = NULL;
	else if (o->magic == CAMEL_OBJECT_MAGIC)
		what = g_strdup_printf("CLASS '%s'", ((CamelObjectClass *)o)->name);
	else if (o->magic == CAMEL_OBJECT_CLASS_MAGIC)
		what = g_strdup_printf("CLASS '%s'", ((CamelObjectClass *)o)->name);
	else if (o->magic == CAMEL_OBJECT_FINALISED_MAGIC)
		what = g_strdup_printf("finalised OBJECT");
	else if (o->magic == CAMEL_OBJECT_CLASS_FINALISED_MAGIC)
		what = g_strdup_printf("finalised CLASS");
	else 
		what = g_strdup_printf("junk data");

	return what;
}

static gboolean
check_magic(void *o, CamelType ctype, int isob)
{
	char *what, *to;

	what = desc_data(o, isob?CAMEL_OBJECT_MAGIC:CAMEL_OBJECT_CLASS_MAGIC);
	to = desc_data((CamelObject *)ctype, CAMEL_OBJECT_CLASS_MAGIC);

	if (what || to) {
		if (what == NULL) {
			if (isob)
				what = g_strdup_printf("OBJECT '%s'", ((CamelObject *)o)->klass->name);
			else
				what = g_strdup_printf("OBJECT '%s'", ((CamelObjectClass *)o)->name);
		}		
		if (to == NULL)
			to = g_strdup_printf("OBJECT '%s'", ctype->name);
		g_warning("Trying to check %s is %s", what, to);
		g_free(what);
		g_free(to);

		return FALSE;
	}

	return TRUE;
}

gboolean
camel_object_is (CamelObject *o, CamelType ctype)
{
	CamelObjectClass *k;

	g_return_val_if_fail(check_magic(o, ctype, TRUE), FALSE);

	k = o->klass;
	while (k) {
		if (k == ctype)
			return TRUE;
		k = k->parent;
	}

	return FALSE;
}

gboolean
camel_object_class_is (CamelObjectClass *k, CamelType ctype)
{
	g_return_val_if_fail(check_magic(k, ctype, FALSE), FALSE);

	while (k) {
		if (k == ctype)
			return TRUE;
		k = k->parent;
	}

	return FALSE;
}

CamelObject *
camel_object_cast(CamelObject *o, CamelType ctype)
{
	CamelObjectClass *k;

	g_return_val_if_fail(check_magic(o, ctype, TRUE), NULL);

	k = o->klass;
	while (k) {
		if (k == ctype)
			return o;
		k = k->parent;
	}

	g_warning("Object %p (class '%s') doesn't have '%s' in its heirachy", o, o->klass->name, ctype->name);

	return NULL;
}

CamelObjectClass *
camel_object_class_cast(CamelObjectClass *k, CamelType ctype)
{
	CamelObjectClass *r = k;

	g_return_val_if_fail(check_magic(k, ctype, FALSE), NULL);

	while (k) {
		if (k == ctype)
			return r;
		k = k->parent;
	}

	g_warning("Class '%s' doesn't have '%s' in its heirarchy", k->name, ctype->name);

	return NULL;
}

void
camel_object_class_add_event(CamelObjectClass *klass, const char *name, CamelObjectEventPrepFunc prep)
{
	CamelHookPair *pair;

	g_return_if_fail (CAMEL_IS_OBJECT_CLASS (klass));
	g_return_if_fail (name);

	pair = klass->hooks;
	while (pair) {
		if (strcmp(pair->name, name) == 0) {
			g_warning("camel_object_class_add_event: `%s' is already declared for '%s'\n",
				  name, klass->name);
			return;
		}
		pair = pair->next;
	}

	pair = pair_alloc();
	pair->name = name;
	pair->func.prep = prep;
	pair->flags = 0;

	pair->next = klass->hooks;
	klass->hooks = pair;
}

/* free hook data */
static void camel_object_free_hooks(CamelObject *o)
{
	CamelHookPair *pair, *next;

	if (o->hooks) {
		g_assert(o->hooks->depth == 0);
		g_assert((o->hooks->flags & CAMEL_HOOK_PAIR_REMOVED) == 0);

		pair = o->hooks->list;
		while (pair) {
			next = pair->next;
			pair_free(pair);
			pair = next;
		}
		e_mutex_destroy(o->hooks->lock);
		hooks_free(o->hooks);
		o->hooks = NULL;
	}
}

/* return (allocate if required) the object's hook list, locking at the same time */
static CamelHookList *camel_object_get_hooks(CamelObject *o)
{
#ifdef ENABLE_THREADS
	static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
#endif
	CamelHookList *hooks;

	/* if we have it, we dont have to do any other locking,
	   otherwise use a global lock to setup the object's hook data */
#ifdef ENABLE_THREADS
	if (o->hooks == NULL) {
		pthread_mutex_lock(&lock);
#endif
		if (o->hooks == NULL) {
			hooks = hooks_alloc();
#ifdef ENABLE_THREADS
			hooks->lock = e_mutex_new(E_MUTEX_REC);
#endif
			hooks->flags = 0;
			hooks->depth = 0;
			hooks->list_length = 0;
			hooks->list = NULL;
			o->hooks = hooks;
		}
#ifdef ENABLE_THREADS
		pthread_mutex_unlock(&lock);
	}
#endif

#ifdef ENABLE_THREADS
	e_mutex_lock(o->hooks->lock);
#endif
	return o->hooks;	
}

/* unlock object hooks' list */
#ifdef ENABLE_THREADS
#define camel_object_unget_hooks(o) (e_mutex_unlock((CAMEL_OBJECT(o)->hooks->lock)))
#else
#define camel_object_unget_hooks(o)
#endif

unsigned int
camel_object_hook_event(CamelObject * obj, const char * name, CamelObjectEventHookFunc func, void *data)
{
	CamelHookPair *pair, *hook;
	CamelHookList *hooks;
	int id;

	g_return_val_if_fail (CAMEL_IS_OBJECT (obj), 0);
	g_return_val_if_fail (name != NULL, 0);
	g_return_val_if_fail (func != NULL, 0);

	hook = obj->klass->hooks;
	while (hook) {
		if (strcmp(hook->name, name) == 0)
			goto setup;
		hook = hook->next;
	}

	g_warning("camel_object_hook_event: trying to hook event `%s' in class `%s' with no defined events.",
		  name, obj->klass->name);

	return 0;

setup:
	/* setup hook pair */
	pair = pair_alloc();
	pair->name = hook->name;	/* effectively static! */
	pair->func.event = func;
	pair->data = data;
	pair->flags = 0;
	id = pair->id;

	/* get the hook list object, locked, link in new event hook, unlock */
	hooks = camel_object_get_hooks(obj);
	pair->next = hooks->list;
	hooks->list = pair;
	hooks->list_length++;
	camel_object_unget_hooks(obj);

	return id;
}

void
camel_object_remove_event(CamelObject * obj, unsigned int id)
{
	CamelHookList *hooks;
	CamelHookPair *pair, *parent;

	g_return_if_fail (CAMEL_IS_OBJECT (obj));
	g_return_if_fail (id != 0);

	if (obj->hooks == NULL) {
		g_warning("camel_object_unhook_event: trying to unhook `%d` from an instance of `%s' with no hooks",
			  id, obj->klass->name);
		return;
	}

	/* scan hooks for this event, remove it, or flag it if we're busy */
	hooks = camel_object_get_hooks(obj);
	parent = (CamelHookPair *)&hooks->list;
	pair = parent->next;
	while (pair) {
		if (pair->id == id
		    && (pair->flags & CAMEL_HOOK_PAIR_REMOVED) == 0) {
			if (hooks->depth > 0) {
				pair->flags |= CAMEL_HOOK_PAIR_REMOVED;
				hooks->flags |= CAMEL_HOOK_PAIR_REMOVED;
			} else {
				parent->next = pair->next;
				pair_free(pair);
				hooks->list_length--;
			}
			camel_object_unget_hooks(obj);
			return;
		}
		parent = pair;
		pair = pair->next;
	}
	camel_object_unget_hooks(obj);

	g_warning("camel_object_unhook_event: cannot find hook id %d in instance of `%s'",
		  id, obj->klass->name);
}

void
camel_object_unhook_event(CamelObject * obj, const char * name, CamelObjectEventHookFunc func, void *data)
{
	CamelHookList *hooks;
	CamelHookPair *pair, *parent;

	g_return_if_fail (CAMEL_IS_OBJECT (obj));
	g_return_if_fail (name != NULL);
	g_return_if_fail (func != NULL);

	if (obj->hooks == NULL) {
		g_warning("camel_object_unhook_event: trying to unhook `%s` from an instance of `%s' with no hooks",
			  name, obj->klass->name);
		return;
	}

	/* scan hooks for this event, remove it, or flag it if we're busy */
	hooks = camel_object_get_hooks(obj);
	parent = (CamelHookPair *)&hooks->list;
	pair = parent->next;
	while (pair) {
		if (pair->func.event == func
		    && pair->data == data
		    && strcmp(pair->name, name) == 0
		    && (pair->flags & CAMEL_HOOK_PAIR_REMOVED) == 0) {
			if (hooks->depth > 0) {
				pair->flags |= CAMEL_HOOK_PAIR_REMOVED;
				hooks->flags |= CAMEL_HOOK_PAIR_REMOVED;
			} else {
				parent->next = pair->next;
				pair_free(pair);
				hooks->list_length--;
			}
			camel_object_unget_hooks(obj);
			return;
		}
		parent = pair;
		pair = pair->next;
	}
	camel_object_unget_hooks(obj);

	g_warning("camel_object_unhook_event: cannot find hook/data pair %p/%p in an instance of `%s' attached to `%s'",
		  func, data, obj->klass->name, name);
}

void
camel_object_trigger_event (CamelObject * obj, const char * name, void *event_data)
{
	CamelHookList *hooks;
	CamelHookPair *pair, **pairs, *parent, *hook;
	int i, size;
	const char *prepname;

	g_return_if_fail (CAMEL_IS_OBJECT (obj));
	g_return_if_fail (name);

	hook = obj->klass->hooks;
	while (hook) {
		if (strcmp(hook->name, name) == 0)
			goto trigger;
		hook = hook->next;
	}

	g_warning("camel_object_trigger_event: trying to trigger unknown event `%s' in class `%s'",
		  name, obj->klass->name);

	return;

trigger:
	/* try prep function, if false, then quit */
	if (hook->func.prep != NULL && !hook->func.prep(obj, event_data))
		return;

	/* also, no hooks, dont bother going further */
	if (obj->hooks == NULL)
		return;

	/* lock the object for hook emission */
	camel_object_ref(obj);
	hooks = camel_object_get_hooks(obj);
	
	if (hooks->list) {
		/* first, copy the items in the list, and say we're in an event */
		hooks->depth++;
		pair = hooks->list;
		size = 0;
		pairs = alloca(sizeof(pairs[0]) * hooks->list_length);
		prepname = hook->name;
		while (pair) {
			if (pair->name == prepname)
				pairs[size++] = pair;
			pair = pair->next;
		}

		/* now execute the events we have, if they haven't been removed during our calls */
		for (i=0;i<size;i++) {
			pair = pairs[i];
			if ((pair->flags & CAMEL_HOOK_PAIR_REMOVED) == 0)
				(pair->func.event) (obj, event_data, pair->data);
		}
		hooks->depth--;

		/* and if we're out of any events, then clean up any pending removes */
		if (hooks->depth == 0 && (hooks->flags & CAMEL_HOOK_PAIR_REMOVED)) {
			parent = (CamelHookPair *)&hooks->list;
			pair = parent->next;
			while (pair) {
				if (pair->flags & CAMEL_HOOK_PAIR_REMOVED) {
					parent->next = pair->next;
					pair_free(pair);
					hooks->list_length--;
				} else {
					parent = pair;
				}
				pair = parent->next;
			}
			hooks->flags &= ~CAMEL_HOOK_PAIR_REMOVED;
		}
	}

	camel_object_unget_hooks(obj);
	camel_object_unref(obj);
}

/* get/set arg methods */
int camel_object_set(CamelObject *o, CamelException *ex, ...)
{
	CamelArgV args;
	CamelObjectClass *klass = o->klass;
	int ret = 0;

	g_return_val_if_fail(CAMEL_IS_OBJECT(o), -1);

	camel_argv_start(&args, ex);

	while (camel_argv_build(&args) && ret == 0)
		ret = klass->setv(o, ex, &args);
	if (ret == 0)
		ret = klass->setv(o, ex, &args);

	camel_argv_end(&args);

	return ret;
}

int camel_object_setv(CamelObject *o, CamelException *ex, CamelArgV *args)
{
	g_return_val_if_fail(CAMEL_IS_OBJECT(o), -1);

	return o->klass->setv(o, ex, args);
}

int camel_object_get(CamelObject *o, CamelException *ex, ...)
{
	CamelArgGetV args;
	CamelObjectClass *klass = o->klass;
	int ret = 0;

	g_return_val_if_fail(CAMEL_IS_OBJECT(o), -1);

	camel_argv_start(&args, ex);

	while (camel_arggetv_build(&args) && ret == 0)
		ret = klass->getv(o, ex, &args);
	if (ret == 0)
		ret = klass->getv(o, ex, &args);

	camel_argv_end(&args);

	return ret;
}

int camel_object_getv(CamelObject *o, CamelException *ex, CamelArgGetV *args)
{
	CamelObjectClass *klass = o->klass;

	g_return_val_if_fail(CAMEL_IS_OBJECT(o), -1);

	return klass->getv(o, ex, args);
}

static void
object_class_dump_tree_rec(CamelType root, int depth)
{
	char *p;
	struct _CamelObject *o;

	p = alloca(depth*2+1);
	memset(p, ' ', depth*2);
	p[depth*2] = 0;

	while (root) {
		CLASS_LOCK(root);
		printf("%sClass: %s\n", p, root->name);
		/*printf("%sVersion: %u.%u\n", p, root->version, root->revision);*/
		if (root->hooks) {
			CamelHookPair *pair = root->hooks;

			while (pair) {
				printf("%s  event '%s' prep %p\n", p, pair->name, pair->func.prep);
				pair = pair->next;
			}
		}
#ifdef CAMEL_OBJECT_TRACK_INSTANCES
		o = root->instances;
		while (o) {
			printf("%s instance %p [%d]\n", p, o, o->ref_count);
			/* todo: should lock hooks while it scans them */
			if (o->hooks) {
				CamelHookPair *pair = o->hooks->list;

				while (pair) {
					printf("%s  hook '%s' func %p data %p\n", p, pair->name, pair->func.event, pair->data);
					pair = pair->next;
				}
			}
			o = o->next;
		}
#endif
		CLASS_UNLOCK(root);

		if (root->child)
			object_class_dump_tree_rec(root->child, depth+1);

		root = root->next;
	}
}

void
camel_object_class_dump_tree(CamelType root)
{
	object_class_dump_tree_rec(root, 0);
}
