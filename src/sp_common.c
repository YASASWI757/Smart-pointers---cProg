#include "sp_internal.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

const char *sp_strerror(sp_status status) {
    switch (status) {
        case SP_OK:
            return "Success";

        case SP_ERR_NULL_ARG:
            return "NULL argument";

        case SP_ERR_NO_MEMORY:
            return "Memory allocation failed";

        case SP_ERR_EMPTY:
            return "Handle is empty";

        case SP_ERR_INVALID_HANDLE:
            return "Invalid handle";

        case SP_ERR_EXPIRED:
            return "Object has expired";

        case SP_ERR_OVERFLOW:
            return "Reference count overflow";

        case SP_ERR_INVALID_SIZE:
            return "Invalid allocation size";

        case SP_ERR_NOT_EMPTY:
            return "Destination handle is not empty";

        default:
            return "Unknown error";
    }
}

static sp_malloc_fn current_allocate = malloc;
static sp_free_fn current_deallocate = free;
static size_t live_allocations = 0;

size_t sp_live_allocations(void) {
    return live_allocations;
}

sp_status sp_set_allocator(
    sp_malloc_fn allocate,
    sp_free_fn deallocate
) {
    if ((allocate == NULL) != (deallocate == NULL)) {
        return SP_ERR_NULL_ARG;
    }

    if (live_allocations != 0) {
        return SP_ERR_NOT_EMPTY;
    }

    if (allocate == NULL) {
        current_allocate = malloc;
        current_deallocate = free;
    } else {
        current_allocate = allocate;
        current_deallocate = deallocate;
    }

    return SP_OK;
}

void *sp__alloc(size_t size) {
    if (size == 0 || live_allocations == SIZE_MAX) {
        return NULL;
    }

    void *memory = current_allocate(size);

    if (memory != NULL) {
        ++live_allocations;
    }

    return memory;
}

void sp__free(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    /*
     * Internal precondition:
     * ptr must be an outstanding allocation from sp__alloc.
     */
    assert(live_allocations > 0);

    --live_allocations;
    current_deallocate(ptr);
}

sp_status sp__track_external(void) {
    if (live_allocations == SIZE_MAX) {
        return SP_ERR_OVERFLOW;
    }

    ++live_allocations;
    return SP_OK;
}

void sp__untrack(void) {
    assert(live_allocations > 0);
    --live_allocations;
}