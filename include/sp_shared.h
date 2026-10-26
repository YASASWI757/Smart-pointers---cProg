#ifndef SP_SHARED_H
#define SP_SHARED_H

#include <stddef.h>
#include "sp_common.h"
#include "sp_unique.h"   /* for sp_destructor_fn */

/*
 * Shared ownership with reference counting.
 *
 * Several sp_shared handles may own the same object. All of them point to
 * one control block that stores the object and a count of owners. The
 * object is destroyed when the last owner is freed.
 *
 * Same handle rules as sp_unique:
 *   - initialize every handle with SP_SHARED_EMPTY,
 *   - never copy a handle with '=', use sp_shared_copy.
 */

#define SP_MAGIC_SHARED 0x53505348u   /* "SPSH" */

typedef struct sp_control_block sp_control_block;   /* private */

typedef struct {
    sp_control_block *control;
    unsigned magic;
} sp_shared;

#define SP_SHARED_EMPTY \
    ((sp_shared){ NULL, SP_MAGIC_SHARED })

/* Allocate an object into an empty handle. Count starts at 1. */
sp_status sp_shared_make(
    sp_shared *owner,
    size_t size,
    sp_destructor_fn destroy
);

/*
 * Make dst another owner of src's object (count + 1).
 * dst must be empty. Copying an empty handle leaves dst empty.
 */
sp_status sp_shared_copy(sp_shared *dst, const sp_shared *src);

/* Transfer one ownership into an empty dst. Count unchanged. */
sp_status sp_shared_move(sp_shared *dst, sp_shared *src);

/* Borrow access. NULL for an empty or invalid handle. */
void *sp_shared_get(const sp_shared *owner);

/* Number of owners. 0 for an empty or invalid handle. */
size_t sp_shared_use_count(const sp_shared *owner);

/*
 * Give up this ownership (count - 1) and empty the handle.
 * The object is destroyed when the count reaches 0.
 * Freeing an empty handle succeeds.
 */
sp_status sp_shared_free(sp_shared *owner);

#endif
