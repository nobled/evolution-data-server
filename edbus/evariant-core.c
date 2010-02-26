/*
 * Copyright © 2007, 2008 Ryan Lortie
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION: gvariant
 * @title: EVariant
 * @short_description: a general purpose variant datatype
 * @see_also: EVariantType
 *
 * #EVariant is a variant datatype; it stores a value along with
 * information about the type of that value.  The range of possible
 * values is determined by the type.  The range of possible types is
 * exactly those types that may be sent over DBus.
 *
 * #EVariant instances always have a type and a value (which are given
 * at construction time).  The type and value of a #EVariant instance
 * can never change other than by the #EVariant itself being
 * destroyed.  A #EVariant can not contain a pointer.
 *
 * Facilities exist for serialising the value of a #EVariant into a
 * byte sequence.  A #EVariant can be sent over the bus or be saved to
 * disk.  Additionally, #EVariant is used as the basis of the
 * #GSettings persistent storage system.
 **/

/*
 * This file is organised into 6 sections
 *
 * SECTION 1: structure declaration, condition constants
 * SECTION 2: allocation/free functions
 * SECTION 3: condition enabling functions
 * SECTION 4: condition machinery
 * SECTION 5: other internal functions
 * SECTION 6: user-visible functions
 */

#include "evariant-private.h"
#include "evariant-serialiser.h"

#include <string.h>
#include <glib.h>
#include "ebitlock.h"

/* == SECTION 1: structure declaration, condition constants ============== */
/**
 * EVariant:
 *
 * #EVariant is an opaque data structure and can only be accessed
 * using the following functions.
 **/
struct _EVariant
{
  union
  {
    struct
    {
      EVariant *source;
      guint8 *data;
    } serialised;

    struct
    {
      EVariant **children;
      gsize n_children;
    } tree;

    struct
    {
      GDestroyNotify callback;
      gpointer user_data;
    } notify;
  } contents;

  gsize size;
  EVariantTypeInfo *type;
  gboolean floating;
  gint state;
  gint ref_count;
};

#define CONDITION_NONE                           0
#define CONDITION_SOURCE_NATIVE         0x00000001
#define CONDITION_BECAME_NATIVE         0x00000002
#define CONDITION_NATIVE                0x00000004

#define CONDITION_SOURCE_TRUSTED        0x00000010
#define CONDITION_BECAME_TRUSTED        0x00000020
#define CONDITION_TRUSTED               0x00000040

#define CONDITION_FIXED_SIZE            0x00000100
#define CONDITION_SIZE_KNOWN            0x00000200
#define CONDITION_SIZE_VALID            0x00000400

#define CONDITION_SERIALISED            0x00001000
#define CONDITION_INDEPENDENT           0x00002000
#define CONDITION_RECONSTRUCTED         0x00004000

#define CONDITION_NOTIFY                0x00010000
#define CONDITION_LOCKED                0x80000000

static const char * /* debugging only */
e_variant_state_to_string (guint state)
{
  GString *string;

  string = g_string_new (NULL);

#define add(cond,name) if (state & cond) g_string_append (string, name ", ");
  add (CONDITION_SOURCE_NATIVE,  "source native");
  add (CONDITION_BECAME_NATIVE,  "became native");
  add (CONDITION_NATIVE,         "native");
  add (CONDITION_SOURCE_TRUSTED, "source trusted");
  add (CONDITION_BECAME_TRUSTED, "became trusted");
  add (CONDITION_TRUSTED,        "trusted");
  add (CONDITION_FIXED_SIZE,     "fixed-size");
  add (CONDITION_SIZE_KNOWN,     "size known");
  add (CONDITION_SIZE_VALID,     "size valid");
  add (CONDITION_INDEPENDENT,    "independent");
  add (CONDITION_SERIALISED,     "serialised");
  add (CONDITION_RECONSTRUCTED,  "reconstructed");
  add (CONDITION_NOTIFY,         "notify");
#undef add

  g_string_truncate (string, string->len - 2);
  return g_string_free (string, FALSE);
}

/* == SECTION 2: allocation/free functions =============================== */
static EVariant *
e_variant_alloc (EVariantTypeInfo *type,
                 guint             initial_state)
{
  EVariant *new;

  new = g_slice_new (EVariant);
  new->ref_count = 1;
  new->type = type;
  new->floating = TRUE;
  new->state = initial_state & ~CONDITION_LOCKED;

  return new;
}

static void
e_variant_free (EVariant *value)
{
  /* free the type info */
  if (value->type)
    e_variant_type_info_unref (value->type);

  /* free the data */
  if (value->state & CONDITION_NOTIFY)
    value->contents.notify.callback (value->contents.notify.user_data);
  else if (value->state & CONDITION_SERIALISED)
    {
      if (value->state & CONDITION_INDEPENDENT)
        g_slice_free1 (value->size, value->contents.serialised.data);
      else
        e_variant_unref (value->contents.serialised.source);

      if (value->state & CONDITION_RECONSTRUCTED)
        e_variant_unref (value->contents.serialised.source);
    }
  else
    {
      EVariant **children;
      gsize n_children;
      gsize i;

      children = value->contents.tree.children;
      n_children = value->contents.tree.n_children;

      for (i = 0; i < n_children; i++)
        e_variant_unref (children[i]);

      g_slice_free1 (sizeof (EVariant *) * n_children, children);
    }

  /* free the structure itself */
  g_slice_free (EVariant, value);
}

static void
e_variant_lock (EVariant *value)
{
  e_bit_lock (&value->state, 31);
}

static void
e_variant_unlock (EVariant *value)
{
  e_bit_unlock (&value->state, 31);
}

/* == SECTION 3: condition enabling functions ============================ */
static void e_variant_fill_gvs (EVariantSerialised *, gpointer);

static gboolean
e_variant_enable_size_known (EVariant *value)
{
  EVariant **children;
  gsize n_children;

  children = value->contents.tree.children;
  n_children = value->contents.tree.n_children;
  value->size = e_variant_serialiser_needed_size (value->type,
                                                  &e_variant_fill_gvs,
                                                  (gpointer *) children,
                                                  n_children);

  return TRUE;
}

static gboolean
e_variant_enable_serialised (EVariant *value)
{
  EVariantSerialised gvs;
  EVariant **children;
  gsize n_children;
  gsize i;

  children = value->contents.tree.children;
  n_children = value->contents.tree.n_children;

  gvs.type_info = value->type;
  gvs.size = value->size;
  gvs.data = g_slice_alloc (gvs.size);

  e_variant_serialiser_serialise (gvs, &e_variant_fill_gvs,
                                  (gpointer *) children, n_children);

  value->contents.serialised.source = NULL;
  value->contents.serialised.data = gvs.data;

  for (i = 0; i < n_children; i++)
    e_variant_unref (children[i]);
  g_slice_free1 (sizeof (EVariant *) * n_children, children);

  return TRUE;
}

static gboolean
e_variant_enable_source_native (EVariant *value)
{
  return (value->contents.serialised.source->state & CONDITION_NATIVE) != 0;
}

static gboolean
e_variant_enable_became_native (EVariant *value)
{
  EVariantSerialised gvs;

  gvs.type_info = value->type;
  gvs.size = value->size;
  gvs.data = value->contents.serialised.data;

  e_variant_serialised_byteswap (gvs);

  return TRUE;
}

static gboolean
e_variant_enable_source_trusted (EVariant *value)
{
  return (value->contents.serialised.source->state & CONDITION_TRUSTED) != 0;
}

static gboolean
e_variant_enable_became_trusted (EVariant *value)
{
  EVariantSerialised gvs;

  gvs.type_info = value->type;
  gvs.data = value->contents.serialised.data;
  gvs.size = value->size;

  return e_variant_serialised_is_normal (gvs);
}

static gboolean
e_variant_enable_reconstructed (EVariant *value)
{
  EVariant *old, *new;

  old = e_variant_alloc (e_variant_type_info_ref (value->type),
                         value->state & ~CONDITION_LOCKED);
  value->contents.serialised.source = e_variant_ref_sink (old);
  old->contents.serialised.source = NULL;
  old->contents.serialised.data = value->contents.serialised.data;
  old->size = value->size;

  new = e_variant_deep_copy (old);
  e_variant_flatten (new);

  /* steal the data from new.  this is very evil. */
  new->state &= ~CONDITION_INDEPENDENT;
  new->contents.serialised.source = value;
  value->contents.serialised.data = new->contents.serialised.data;
  value->size = new->size;

  e_variant_unref (new);

  return TRUE;
}

static gboolean
e_variant_enable_independent (EVariant *value)
{
  EVariant *source;
  gpointer  new;

  source = value->contents.serialised.source;
  g_assert (source->state & CONDITION_INDEPENDENT);

  new = g_slice_alloc (value->size);
  memcpy (new, value->contents.serialised.data, value->size);

  /* barrier to ensure byteswap is not in progress */
  e_variant_lock (source);
  e_variant_unlock (source);

  /* rare: check if the source became native while we were copying */
  if (source->state & CONDITION_NATIVE)
    {
      /* our data is probably half-and-half */
      g_slice_free1 (value->size, new);

      return FALSE;
    }

  value->contents.serialised.source = NULL;
  value->contents.serialised.data = new;
  e_variant_unref (source);

  return TRUE;
}

static gboolean
e_variant_enable_fixed_size (EVariant *value)
{
  gsize fixed_size;

  e_variant_type_info_query (value->type, NULL, &fixed_size);

  return fixed_size != 0;
}

/* == SECTION 4: condition machinery ===================================== */
struct precondition_clause
{
  guint required;
  guint forbidden;
};

struct condition
{
  guint    condition;
  guint    implies;
  guint    forbids;
  guint    absence_implies;
  void     (*assert_invariant) (EVariant *);
  gboolean (*enable) (EVariant *);
  struct precondition_clause precondition[4];
};

struct condition condition_table[] =
{
  { CONDITION_SOURCE_NATIVE,
    CONDITION_NATIVE | CONDITION_SERIALISED,
    CONDITION_INDEPENDENT | CONDITION_BECAME_NATIVE |
    CONDITION_RECONSTRUCTED,
    CONDITION_NONE,
    NULL, e_variant_enable_source_native,
    { { CONDITION_NONE, CONDITION_NATIVE | CONDITION_INDEPENDENT    } } },

  { CONDITION_BECAME_NATIVE,
    CONDITION_NATIVE | CONDITION_INDEPENDENT | CONDITION_SERIALISED,
    CONDITION_SOURCE_NATIVE | CONDITION_RECONSTRUCTED,
    CONDITION_NONE,
    NULL, e_variant_enable_became_native,
    { { CONDITION_INDEPENDENT | CONDITION_SIZE_VALID,
        CONDITION_NATIVE                                            } } },

  { CONDITION_NATIVE,
    CONDITION_NONE,
    CONDITION_NONE,
    CONDITION_SERIALISED,
    NULL, NULL,
    { { CONDITION_SOURCE_NATIVE                                     },
      { CONDITION_BECAME_NATIVE                                     },
      { CONDITION_RECONSTRUCTED                                     } } },

  { CONDITION_SOURCE_TRUSTED,
    CONDITION_TRUSTED | CONDITION_SERIALISED,
    CONDITION_RECONSTRUCTED,
    CONDITION_NONE,
    NULL, e_variant_enable_source_trusted,
    { { CONDITION_NONE, CONDITION_TRUSTED | CONDITION_INDEPENDENT   } } },

  { CONDITION_BECAME_TRUSTED,
    CONDITION_TRUSTED | CONDITION_SERIALISED,
    CONDITION_SOURCE_TRUSTED | CONDITION_RECONSTRUCTED,
    CONDITION_NONE,
    NULL, e_variant_enable_became_trusted,
    { { CONDITION_SERIALISED, CONDITION_TRUSTED                     } } },

  { CONDITION_TRUSTED,
    CONDITION_NONE,
    CONDITION_NONE,
    CONDITION_NONE,
    NULL, NULL,
    { { CONDITION_SOURCE_TRUSTED                                    },
      { CONDITION_BECAME_TRUSTED                                    },
      { CONDITION_RECONSTRUCTED                                     } } },

  { CONDITION_SERIALISED,
    CONDITION_SIZE_KNOWN,
    CONDITION_NONE,
    CONDITION_NATIVE | CONDITION_INDEPENDENT,
    NULL, e_variant_enable_serialised,
    { { CONDITION_SIZE_KNOWN                                        } } },

  { CONDITION_SIZE_KNOWN,
    CONDITION_NONE,
    CONDITION_NOTIFY,
    CONDITION_NONE,
    NULL, e_variant_enable_size_known,
    { { CONDITION_NONE, CONDITION_SIZE_KNOWN                        } } },

  { CONDITION_SIZE_VALID,
    CONDITION_SIZE_KNOWN,
    CONDITION_NONE,
    CONDITION_NONE,
    NULL, NULL,
    { { CONDITION_SIZE_KNOWN | CONDITION_FIXED_SIZE                 },
      { CONDITION_SIZE_KNOWN | CONDITION_TRUSTED                    },
      { CONDITION_SIZE_KNOWN | CONDITION_NATIVE                     } } },

  { CONDITION_INDEPENDENT,
    CONDITION_NONE,
    CONDITION_NONE,
    CONDITION_SERIALISED,
    NULL, e_variant_enable_independent,
    { { CONDITION_SERIALISED, CONDITION_NATIVE                      } } },

  { CONDITION_RECONSTRUCTED,
    CONDITION_TRUSTED | CONDITION_NATIVE | CONDITION_INDEPENDENT,
    CONDITION_BECAME_NATIVE | CONDITION_BECAME_TRUSTED,
    CONDITION_NONE,
    NULL, e_variant_enable_reconstructed,
    { { CONDITION_SERIALISED,
        CONDITION_NATIVE | CONDITION_TRUSTED | CONDITION_FIXED_SIZE } } },

  { CONDITION_FIXED_SIZE,
    CONDITION_NONE,
    CONDITION_NONE,
    CONDITION_NONE,
    NULL, e_variant_enable_fixed_size,
    { { CONDITION_NONE, CONDITION_FIXED_SIZE                        } } },

  { CONDITION_NOTIFY,
    CONDITION_NONE,
    CONDITION_NOTIFY - 1,
    CONDITION_NONE,
    NULL, NULL,
    { } },

  { }
};

static gboolean
e_variant_state_is_valid (guint state)
{
  struct condition *c;

  for (c = condition_table; c->condition; c++)
    {
      if (state & c->condition)
        {
          if (~state & c->implies)
            return FALSE;

          if (state & c->forbids)
            return FALSE;
        }
      else
        {
          if (~state & c->absence_implies)
            return FALSE;
        }
    }

  return TRUE;
}

/*
 * e_variant_assert_invariant:
 * @value: a #EVariant instance to check
 *
 * This function asserts the class invariant on a #EVariant instance.
 * Any detected problems will result in an assertion failure.
 *
 * This function is potentially very slow.
 *
 * This function never fails.
 */
void
e_variant_assert_invariant (EVariant *value)
{
  if (value->state & CONDITION_NOTIFY)
    return;

  e_variant_lock (value);

  g_assert_cmpint (value->ref_count, >, 0);

  if G_UNLIKELY (!e_variant_state_is_valid (value->state & ~CONDITION_LOCKED))
    g_critical ("instance %p in invalid state: %s",
                value, e_variant_state_to_string (value->state));

  if (value->state & CONDITION_SERIALISED)
    {
      if (value->state & CONDITION_RECONSTRUCTED)
        e_variant_assert_invariant (value->contents.serialised.source);

      if (!(value->state & CONDITION_INDEPENDENT))
        e_variant_assert_invariant (value->contents.serialised.source);
    }
  else
    {
      gsize i;

      for (i = 0; i < value->contents.tree.n_children; i++)
        {
          g_assert_cmpint (value->state & CONDITION_TRUSTED, <=,
                           value->contents.tree.children[i]->state &
                           CONDITION_TRUSTED);

          g_assert (!value->contents.tree.children[i]->floating);
          e_variant_assert_invariant (value->contents.tree.children[i]);
        }
    }

  e_variant_unlock (value);
}

/* how many bits are in 'reqd' but not 'have'?
 */
static int
bits_missing (guint have,
              guint reqd)
{
  guint count = 0;

  reqd &= ~have;
  while (reqd && ++count)
    reqd &= reqd - 1;

  return count;
}

static gboolean
e_variant_try_unlocked (EVariant *value,
                        guint     conditions)
{
  struct precondition_clause *p;
  struct condition *c;
  int max_missing;

  /* attempt to enable each missing condition */
  for (c = condition_table; c->condition; c++)
    if ((value->state & c->condition) < (conditions & c->condition))
      {
        /* prefer preconditon clauses with the fewest false terms */
        for (max_missing = 0; max_missing < 10; max_missing++)
          for (p = c->precondition; p->required || p->forbidden; p++)
            if (!(value->state & p->forbidden) &&
                bits_missing (value->state, p->required) < max_missing &&
                max_missing && e_variant_try_unlocked (value, p->required))
              goto attempt_enable;

        return FALSE;

       attempt_enable:
        if (c->enable && !c->enable (value))
          return FALSE;

        value->state |= c->condition;
      }

  if (~value->state & conditions)
    g_error ("was %x %x", value->state, conditions);

  g_assert (!(~value->state & conditions));

  return TRUE;
}

static gboolean
e_variant_try_enabling_conditions (EVariant *value,
                                   guint     conditions)
{
  gboolean success;

  e_variant_assert_invariant (value);

  if ((value->state & conditions) == conditions)
    return TRUE;

  e_variant_lock (value);
  success = e_variant_try_unlocked (value, conditions);
  e_variant_unlock (value);
  e_variant_assert_invariant (value);

  return success;
}

static void
e_variant_require_conditions (EVariant *value,
                              guint     condition_set)
{
  if G_UNLIKELY (!e_variant_try_enabling_conditions (value, condition_set))
    g_error ("instance %p unable to enable '%s' from '%s'\n",
             value, e_variant_state_to_string (condition_set),
             e_variant_state_to_string (value->state));
}

static gboolean
e_variant_forbid_conditions (EVariant *value,
                             guint     condition_set)
{
  e_variant_assert_invariant (value);

  if (value->state & condition_set)
    return FALSE;

  e_variant_lock (value);

  if (value->state & condition_set)
    {
      e_variant_unlock (value);
      return FALSE;
    }

  return TRUE;
}

EVariant *
e_variant_load_fixed (const EVariantType *type,
                      gconstpointer       data,
                      gsize               n_items)
{
  EVariantTypeInfo *info;
  gsize fixed_size;
  EVariant *new;

  info = e_variant_type_info_get (type);
  if (e_variant_type_is_array (type))
    {
      e_variant_type_info_query_element (info, NULL, &fixed_size);
      fixed_size *= n_items;
    }
  else
    e_variant_type_info_query (info, NULL, &fixed_size);
  g_assert (fixed_size);

  /* TODO: can't be trusted yet since there may be non-zero
   *       padding between the elements.  can fix this with
   *       some sort of intelligent zero-inserting memdup.
   */
  new = e_variant_alloc (info,
                         CONDITION_INDEPENDENT | CONDITION_NATIVE |
                         CONDITION_SERIALISED | CONDITION_SIZE_KNOWN);
  new->contents.serialised.source = NULL;
  new->contents.serialised.data = g_slice_alloc (fixed_size);
  memcpy (new->contents.serialised.data, data, fixed_size);
  new->size = fixed_size;

  e_variant_assert_invariant (new);

  return new;
}

/* == SECTION 5: other internal functions ================================ */
static EVariantSerialised
e_variant_get_gvs (EVariant  *value,
                   EVariant **source)
{
  EVariantSerialised gvs = { value->type };

  g_assert (value->state & CONDITION_SERIALISED);
  e_variant_require_conditions (value, CONDITION_SIZE_VALID);

  if (e_variant_forbid_conditions (value, CONDITION_INDEPENDENT))
    {
      /* dependent */
      gvs.data = value->contents.serialised.data;

      if (source)
        *source = e_variant_ref (value->contents.serialised.source);

      e_variant_unlock (value);
    }
  else
    {
      /* independent */
      gvs.data = value->contents.serialised.data;

      if (source)
        *source = e_variant_ref (value);

      e_variant_unlock (value);
    }

  gvs.size = value->size;

  return gvs;
}

/*
 * e_variant_fill_gvs:
 * @serialised: the #EVariantSerialised to fill
 * @data: our #EVariant instance
 *
 * Utility function used as a callback from the serialiser to get
 * information about a given #EVariant instance (in @data).
 */
static void
e_variant_fill_gvs (EVariantSerialised *serialised,
                    gpointer            data)
{
  EVariant *value = data;

  e_variant_assert_invariant (value);

  e_variant_require_conditions (value, CONDITION_SIZE_VALID);

  if (serialised->type_info == NULL)
    serialised->type_info = value->type;

  if (serialised->size == 0)
    serialised->size = value->size;

  g_assert (serialised->type_info == value->type);
  g_assert (serialised->size == value->size);

  if (serialised->data && serialised->size)
    e_variant_store (value, serialised->data);
}

/*
 * e_variant_get_zeros:
 * @size: a size, in bytes
 * @returns: a new reference to a #EVariant
 *
 * Creates a #EVariant with no type that contains at least @size bytes
 * worth of zeros.  The instance will live forever.
 *
 * This is required for the case where deserialisation of a fixed-size
 * value from a non-fixed-sized container fails.  The fixed-sized
 * value needs to have zero-filled data (in case this data is
 * requested).  This data must also outlive the child since the parent
 * from which that child was taken may have been flattened (in which
 * case the user expects the child's data to live as long as the
 * parent).
 *
 * There are less-permanent ways of doing this (ie: somehow
 * associating the zeros with the parent) but this way is easy and it
 * works fine for now.
 */
static EVariant *
e_variant_get_zeros (gsize size)
{
  static GStaticMutex lock = G_STATIC_MUTEX_INIT;
  static GSList *zeros;
  EVariant *value;

  /* there's actually no sense in storing the entire linked list since
   * we will always use the first item in the list, but it prevents
   * valgrind from complaining.
   */

  if (size < 4096)
    size = 4096;

  g_static_mutex_lock (&lock);
  if (zeros)
    {
      value = zeros->data;

      if (value->size < size)
        value = NULL;
    }
  else
    value = NULL;

  if (value == NULL)
    {
      size--;
      size |= size >> 1; size |= size >> 2; size |= size >> 4;
      size |= size >> 8; size |= size >> 16;
      size |= size >> 16; size |= size >> 16;
      size++;

      value = e_variant_alloc (NULL,
                               CONDITION_SERIALISED | CONDITION_SIZE_KNOWN |
                               CONDITION_NATIVE | CONDITION_TRUSTED |
                               CONDITION_INDEPENDENT | CONDITION_SIZE_VALID);
      value->contents.serialised.source = NULL;
      value->contents.serialised.data = g_malloc0 (size);
      value->size = size;

      zeros = g_slist_prepend (zeros, e_variant_ref_sink (value));
    }
  g_static_mutex_unlock (&lock);

  return e_variant_ref (value);
}

/*
 * e_variant_apply_flags:
 * @value: a fresh #EVariant instance
 * @flags: various load flags
 *
 * This function is the common code used to apply flags (normalise,
 * byteswap, etc) to fresh #EVariant instances created using one of
 * the load functions.
 */
static EVariant *
e_variant_apply_flags (EVariant      *value,
                       EVariantFlags  flags)
{
  guint16 byte_order = flags;

  if (byte_order == 0)
    byte_order = G_BYTE_ORDER;

  g_assert (byte_order == G_LITTLE_ENDIAN ||
            byte_order == G_BIG_ENDIAN);

  if (byte_order == G_BYTE_ORDER)
    value->state |= CONDITION_NATIVE;

  else if (~flags & E_VARIANT_LAZY_BYTESWAP)
    e_variant_require_conditions (value, CONDITION_NATIVE);

  e_variant_assert_invariant (value);

  return value;
}

gboolean
e_variant_is_trusted (EVariant *value)
{
  return !!(value->state & CONDITION_TRUSTED);
}

gboolean
e_variant_is_normal_ (EVariant *value)
{
  return e_variant_serialised_is_normal (e_variant_get_gvs (value, NULL));
}

/* == SECTION 6: user-visibile functions ================================= */
/**
 * e_variant_get_type:
 * @value: a #EVariant
 * @returns: a #EVariantType
 *
 * Determines the type of @value.
 *
 * The return value is valid for the lifetime of @value and must not
 * be freed.
 */
const EVariantType *
e_variant_get_type (EVariant *value)
{
  g_return_val_if_fail (value != NULL, NULL);
  e_variant_assert_invariant (value);

  return E_VARIANT_TYPE (e_variant_type_info_get_type_string (value->type));
}

/**
 * e_variant_n_children:
 * @value: a container #EVariant
 * @returns: the number of children in the container
 *
 * Determines the number of children in a container #EVariant
 * instance.  This includes variants, maybes, arrays, tuples and
 * dictionary entries.  It is an error to call this function on any
 * other type of #EVariant.
 *
 * For variants, the return value is always 1.  For maybes, it is
 * always zero or one.  For arrays, it is the length of the array.
 * For tuples it is the number of tuple items (which depends
 * only on the type).  For dictionary entries, it is always 2.
 *
 * This function never fails.
 * TS
 **/
gsize
e_variant_n_children (EVariant *value)
{
  gsize n_children;

  g_return_val_if_fail (value != NULL, 0);
  e_variant_assert_invariant (value);

  if (e_variant_forbid_conditions (value, CONDITION_SERIALISED))
    {
      n_children = value->contents.tree.n_children;
      e_variant_unlock (value);
    }
  else
    {
      EVariantSerialised gvs;
      EVariant *source;

      gvs = e_variant_get_gvs (value, &source);
      n_children = e_variant_serialised_n_children (gvs);
      e_variant_unref (source);
    }

  return n_children;
}

/**
 * e_variant_get_child_value:
 * @value: a container #EVariant
 * @index: the index of the child to fetch
 * @returns: the child at the specified index
 *
 * Reads a child item out of a container #EVariant instance.  This
 * includes variants, maybes, arrays, tuple and dictionary
 * entries.  It is an error to call this function on any other type of
 * #EVariant.
 *
 * It is an error if @index is greater than the number of child items
 * in the container.  See e_variant_n_children().
 *
 * This function never fails.
 **/
EVariant *
e_variant_get_child_value (EVariant *value,
                           gsize     index)
{
  EVariant *child;

  g_return_val_if_fail (value != NULL, NULL);
  e_variant_assert_invariant (value);

  if (e_variant_forbid_conditions (value, CONDITION_SERIALISED))
    {
      if G_UNLIKELY (index >= value->contents.tree.n_children)
        g_error ("Attempt to access item %" G_GSIZE_FORMAT
                 " in a container with only %" G_GSIZE_FORMAT
                 " items", index, value->contents.tree.n_children);

      child = e_variant_ref (value->contents.tree.children[index]);
      e_variant_unlock (value);
    }
  else
    {
      EVariantSerialised gvs;
      EVariant *source;

      gvs = e_variant_get_gvs (value, &source);
      gvs = e_variant_serialised_get_child (gvs, index);

      child = e_variant_alloc (gvs.type_info,
                               CONDITION_SERIALISED | CONDITION_SIZE_KNOWN);
      child->type = gvs.type_info;
      child->contents.serialised.source = source;
      child->contents.serialised.data = gvs.data;
      child->size = gvs.size;
      child->floating = FALSE;

      if (gvs.data == NULL)
        {
          /* not actually using source data -- release it */
          e_variant_unref (child->contents.serialised.source);
          child->contents.serialised.source = NULL;

          if (gvs.size)
            {
              EVariant *zeros;
              gpointer data;

              g_assert (!((source->state | value->state)
                          & CONDITION_TRUSTED));

              zeros = e_variant_get_zeros (gvs.size);
              data = zeros->contents.serialised.data;

              child->contents.serialised.source = zeros;
              child->contents.serialised.data = data;

              child->state |= CONDITION_FIXED_SIZE | CONDITION_TRUSTED;
            }
          else
            child->state |= CONDITION_INDEPENDENT;

          child->state |= CONDITION_NATIVE | CONDITION_SIZE_VALID;
        }

      /* inherit 'native' and 'trusted' attributes */
      child->state |= (source->state | value->state) &
                      (CONDITION_NATIVE | CONDITION_TRUSTED);
    }

  e_variant_assert_invariant (child);

  return child;
}

/**
 * e_variant_get_size:
 * @value: a #EVariant instance
 * @returns: the serialised size of @value
 *
 * Determines the number of bytes that would be required to store
 * @value with e_variant_store().
 *
 * In the case that @value is already in serialised form or the size
 * has already been calculated (ie: this function has been called
 * before) then this function is O(1).  Otherwise, the size is
 * calculated, an operation which is approximately O(n) in the number
 * of values involved.
 *
 * This function never fails.
 **/
gsize
e_variant_get_size (EVariant *value)
{
  g_return_val_if_fail (value != NULL, 0);

  e_variant_require_conditions (value, CONDITION_SIZE_VALID);

  return value->size;
}

/**
 * e_variant_get_data:
 * @value: a #EVariant instance
 * @returns: the serialised form of @value
 *
 * Returns a pointer to the serialised form of a #EVariant instance.
 * The returned data is in machine native byte order but may not be in
 * fully-normalised form if read from an untrusted source.  The
 * returned data must not be freed; it remains valid for as long as
 * @value exists.
 *
 * In the case that @value is already in serialised form, this
 * function is O(1).  If the value is not already in serialised form,
 * serialisation occurs implicitly and is approximately O(n) in the
 * size of the result.
 *
 * This function never fails.
 **/
gconstpointer
e_variant_get_data (EVariant *value)
{
  g_return_val_if_fail (value != NULL, NULL);

  e_variant_require_conditions (value, CONDITION_NATIVE | CONDITION_SERIALISED);

  return value->contents.serialised.data;
}

/**
 * e_variant_store:
 * @value: the #EVariant to store
 * @data: the location to store the serialised data at
 *
 * Stores the serialised form of @variant at @data.  @data should be
 * serialised enough.  See e_variant_get_size().
 *
 * The stored data is in machine native byte order but may not be in
 * fully-normalised form if read from an untrusted source.  See
 * e_variant_normalise() for a solution.
 *
 * This function is approximately O(n) in the size of @data.
 *
 * This function never fails.
 **/
void
e_variant_store (EVariant *value,
                 gpointer  data)
{
  g_return_if_fail (value != NULL);

  e_variant_assert_invariant (value);

  e_variant_require_conditions (value, CONDITION_SIZE_VALID | CONDITION_NATIVE);

  if (e_variant_forbid_conditions (value, CONDITION_SERIALISED))
    {
      EVariantSerialised gvs;
      EVariant **children;
      gsize n_children;

      gvs.type_info = value->type;
      gvs.data = data;
      gvs.size = value->size;

      children = value->contents.tree.children;
      n_children = value->contents.tree.n_children;

      /* XXX we hold the lock for an awful long time here... */
      e_variant_serialiser_serialise (gvs,
                                      &e_variant_fill_gvs,
                                      (gpointer *) children,
                                      n_children);
      e_variant_unlock (value);
    }
  else
    {
      EVariantSerialised gvs;
      EVariant *source;

      gvs = e_variant_get_gvs (value, &source);
      memcpy (data, gvs.data, gvs.size);
      e_variant_unref (source);
  }
}

/**
 * e_variant_get_fixed:
 * @value: a #EVariant
 * @size: the size of @value
 * @returns: a pointer to the fixed-sized data
 *
 * Gets a pointer to the data of a fixed sized #EVariant instance.
 * This pointer can be treated as a pointer to the equivalent C
 * stucture type and accessed directly.  The data is in machine byte
 * order.
 *
 * @size must be equal to the fixed size of the type of @value.  It
 * isn't used for anything, but serves as a sanity check to ensure the
 * user of this function will be able to make sense of the data they
 * receive a pointer to.
 *
 * This function may return %NULL if @size is zero.
 **/
gconstpointer
e_variant_get_fixed (EVariant *value,
                     gsize     size)
{
  gsize fixed_size;

  g_return_val_if_fail (value != NULL, NULL);

  e_variant_assert_invariant (value);

  e_variant_type_info_query (value->type, NULL, &fixed_size);
  g_assert (fixed_size);

  g_assert_cmpint (size, ==, fixed_size);

  return e_variant_get_data (value);
}

/**
 * e_variant_get_fixed_array:
 * @value: an array #EVariant
 * @elem_size: the size of one array element
 * @length: a pointer to the length of the array, or %NULL
 * @returns: a pointer to the array data
 *
 * Gets a pointer to the data of an array of fixed sized #EVariant
 * instances.  This pointer can be treated as a pointer to an array of
 * the equivalent C type and accessed directly.  The data is
 * in machine byte order.
 *
 * @elem_size must be equal to the fixed size of the element type of
 * @value.  It isn't used for anything, but serves as a sanity check
 * to ensure the user of this function will be able to make sense of
 * the data they receive a pointer to.
 *
 * @length is set equal to the number of items in the array, so that
 * the size of the memory region returned is @elem_size times @length.
 *
 * This function may return %NULL if either @elem_size or @length is
 * zero.
 */
gconstpointer
e_variant_get_fixed_array (EVariant *value,
                           gsize     elem_size,
                           gsize    *length)
{
  gsize fixed_elem_size;

  g_return_val_if_fail (value != NULL, NULL);

  e_variant_assert_invariant (value);

  /* unsupported: maybes are treated as arrays of size zero or one */
  e_variant_type_info_query_element (value->type, NULL, &fixed_elem_size);
  g_assert (fixed_elem_size);

  g_assert_cmpint (elem_size, ==, fixed_elem_size);

  if (length != NULL)
    *length = e_variant_n_children (value);

  return e_variant_get_data (value);
}

/**
 * e_variant_unref:
 * @value: a #EVariant
 *
 * Decreases the reference count of @variant.  When its reference
 * count drops to 0, the memory used by the variant is freed.
 **/
void
e_variant_unref (EVariant *value)
{
  g_return_if_fail (value != NULL);

  e_variant_assert_invariant (value);

  if (g_atomic_int_dec_and_test (&value->ref_count))
    e_variant_free (value);
}

/**
 * e_variant_ref:
 * @value: a #EVariant
 * @returns: the same @variant
 *
 * Increases the reference count of @variant.
 **/
EVariant *
e_variant_ref (EVariant *value)
{
  g_return_val_if_fail (value != NULL, NULL);

  e_variant_assert_invariant (value);

  g_atomic_int_inc (&value->ref_count);

  return value;
}

/**
 * e_variant_ref_sink:
 * @value: a #EVariant
 * @returns: the same @variant
 *
 * If @value is floating, mark it as no longer floating.  If it is not
 * floating, increase its reference count.
 **/
EVariant *
e_variant_ref_sink (EVariant *value)
{
  g_return_val_if_fail (value != NULL, NULL);

  e_variant_assert_invariant (value);

  e_variant_ref (value);
  if (g_atomic_int_compare_and_exchange (&value->floating, 1, 0))
    e_variant_unref (value);

  return value;
}

/* private */
EVariant *
e_variant_new_tree (const EVariantType  *type,
                    EVariant           **children,
                    gsize                n_children,
                    gboolean             trusted)
{
  EVariant *new;

  new = e_variant_alloc (e_variant_type_info_get (type),
                         CONDITION_INDEPENDENT | CONDITION_NATIVE);
  new->contents.tree.children = children;
  new->contents.tree.n_children = n_children;
  new->size = 0;

  if (trusted)
    new->state |= CONDITION_TRUSTED;

  e_variant_assert_invariant (new);

  return new;
}

/**
 * e_variant_from_slice:
 * @type: the #EVariantType of the new variant
 * @slice: a pointer to a GSlice-allocated region
 * @size: the size of @slice
 * @flags: zero or more #EVariantFlags
 * @returns: a new #EVariant instance
 *
 * Creates a #EVariant instance from a memory slice.  Ownership of the
 * memory slice is assumed.  This function allows efficiently creating
 * #EVariant instances with data that is, for example, read over a
 * socket.
 *
 * If @type is %NULL then @data is assumed to have the type
 * %E_VARIANT_TYPE_VARIANT and the return value is the value extracted
 * from that variant.
 *
 * This function never fails.
 **/
EVariant *
e_variant_from_slice (const EVariantType *type,
                      gpointer            slice,
                      gsize               size,
                      EVariantFlags       flags)
{
  EVariant *new;

  g_return_val_if_fail (slice != NULL || size == 0, NULL);

  if (type == NULL)
    {
      EVariant *variant;

      variant = e_variant_from_slice (E_VARIANT_TYPE_VARIANT,
                                      slice, size, flags);
      new = e_variant_get_variant (variant);
      e_variant_unref (variant);

      return new;
    }
  else
    {
      new = e_variant_alloc (e_variant_type_info_get (type),
                             CONDITION_SERIALISED | CONDITION_INDEPENDENT |
                             CONDITION_SIZE_KNOWN);

      new->contents.serialised.source = NULL;
      new->contents.serialised.data = slice;
      new->floating = FALSE;
      new->size = size;

      return e_variant_apply_flags (new, flags);
    }
}

/**
 * e_variant_from_data:
 * @type: the #EVariantType of the new variant
 * @data: a pointer to the serialised data
 * @size: the size of @data
 * @flags: zero or more #EVariantFlags
 * @notify: a function to call when @data is no longer needed
 * @user_data: a #gpointer to pass to @notify
 * @returns: a new #EVariant instance
 *
 * Creates a #EVariant instance from serialised data.  The data is not
 * copied.  When the data is no longer required (which may be before
 * or after the return value is freed) @notify is called.  @notify may
 * even be called before this function returns.
 *
 * If @type is %NULL then @data is assumed to have the type
 * %E_VARIANT_TYPE_VARIANT and the return value is the value extracted
 * from that variant.
 *
 * This function never fails.
 **/
EVariant *
e_variant_from_data (const EVariantType *type,
                          gconstpointer       data,
                          gsize               size,
                          EVariantFlags       flags,
                          GDestroyNotify      notify,
                          gpointer            user_data)
{
  EVariant *new;

  g_return_val_if_fail (data != NULL || size == 0, NULL);

  if (type == NULL)
    {
      EVariant *variant;

      variant = e_variant_from_data (E_VARIANT_TYPE_VARIANT,
                                     data, size, flags, notify, user_data);
      new = e_variant_get_variant (variant);
      e_variant_unref (variant);

      return new;
    }
  else
    {
      EVariant *marker;

      marker = e_variant_alloc (NULL, CONDITION_NOTIFY);
      marker->contents.notify.callback = notify;
      marker->contents.notify.user_data = user_data;

      new = e_variant_alloc (e_variant_type_info_get (type),
                             CONDITION_SERIALISED | CONDITION_SIZE_KNOWN);
      new->contents.serialised.source = marker;
      new->contents.serialised.data = (gpointer) data;
      new->floating = FALSE;
      new->size = size;

      return e_variant_apply_flags (new, flags);
    }
}

/**
 * e_variant_load:
 * @type: the #EVariantType of the new variant, or %NULL
 * @data: the serialised #EVariant data to load
 * @size: the size of @data
 * @flags: zero or more #EVariantFlags
 * @returns: a new #EVariant instance
 *
 * Creates a new #EVariant instance.  @data is copied.  For a more
 * efficient way to create #EVariant instances, see
 * e_variant_from_slice() or e_variant_from_data().
 *
 * If @type is non-%NULL then it specifies the type of the
 * #EVariant contained in the serialise data.  If @type is
 * %NULL then the serialised data is assumed to have type
 * "v" and instead of returning the variant itself, the
 * contents of the variant is returned.  This provides a
 * simple way to store data along with its type.
 *
 * This function is O(n) in the size of @data.
 *
 * This function never fails.
 **/
EVariant *
e_variant_load (const EVariantType *type,
                gconstpointer       data,
                gsize               size,
                EVariantFlags       flags)
{
  EVariant *new;

  g_return_val_if_fail (data != NULL || size == 0, NULL);

  if (type == NULL)
    {
      EVariant *variant;

      variant = e_variant_load (E_VARIANT_TYPE_VARIANT, data, size, flags);
      new = e_variant_get_variant (variant);
      e_variant_unref (variant);

      return new;
    }
  else
    {
      gpointer slice;

      slice = g_slice_alloc (size);
      memcpy (slice, data, size);

      return e_variant_from_slice (type, slice, size, flags);
    }
}

/**
 * e_variant_classify:
 * @value: a #EVariant
 * @returns: the #EVariantClass of @value
 *
 * Returns the type class of @value.  This function is equivalent to
 * calling e_variant_get_type() followed by
 * e_variant_type_get_class().
 **/
EVariantClass
e_variant_classify (EVariant *value)
{
  g_return_val_if_fail (value != NULL, 0);
  return e_variant_type_info_get_type_char (value->type);
}

#include <glib-object.h>

GType
e_variant_get_gtype (void)
{
  static GType type_id = 0;

  if (!type_id)
    type_id = g_boxed_type_register_static (g_intern_static_string ("EVariant"),
                                            (GBoxedCopyFunc) e_variant_ref,
                                            (GBoxedFreeFunc) e_variant_unref);

  return type_id;
}
