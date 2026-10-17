#ifndef SP_UNIQUE_H
#define SP_UNIQUE_H

#include <stddef.h>
#include "sp_common.h"

/*
 * Cleans nested resources only.
 * The library deallocates the outer object afterward.
 * Callbacks must not modify the handle being cleaned up.
 */
typedef void (*sp_destructor_fn)(void *object);

#define SP_MAGIC_UNIQUE 0x5350554Eu

typedef struct {
    void *ptr;
    sp_destructor_fn destroy;
    unsigned magic;
} sp_unique;

#define SP_UNIQUE_EMPTY \
    ((sp_unique){ NULL, NULL, SP_MAGIC_UNIQUE })

/*
 * All handles must be initialized before use.
 * Do not copy owning handles with ordinary assignment.
 */

/* Allocate an object into an empty handle. Size must be positive. */
sp_status sp_unique_make(
    sp_unique *owner,
    size_t size,
    sp_destructor_fn destroy
);

/*
 * Adopt into an empty handle.
 * raw must be compatible with the current allocator pair.
 * On failure, the caller retains ownership.
 * NULL produces an empty handle.
 */
sp_status sp_unique_adopt(
    sp_unique *owner,
    void *raw,
    sp_destructor_fn destroy
);

/* Borrow access. Returns NULL for an empty or invalid handle. */
void *sp_unique_get(const sp_unique *owner);

/* Move into an empty destination. Self-move succeeds unchanged. */
sp_status sp_unique_move(sp_unique *dst, sp_unique *src);

/*
 * Transfer ownership to the caller without destroying the object.
 * raw_out must point to a separate void * variable, not a handle field.
 * On error, raw_out is unchanged.
 * Empty release succeeds and writes NULL.
 * Caller must run the required destructor and matching deallocator.
 */
sp_status sp_unique_release(sp_unique *owner, void **raw_out);

/*
 * Replace the object with a compatible allocation or NULL.
 * On failure, owner is unchanged and caller retains new_raw.
 * Same-pointer reset is a no-op preserving the old destructor.
 */
sp_status sp_unique_reset(
    sp_unique *owner,
    void *new_raw,
    sp_destructor_fn destroy
);

/* Destroy the object and empty the handle. Empty cleanup succeeds. */
sp_status sp_unique_free(sp_unique *owner);

#endif