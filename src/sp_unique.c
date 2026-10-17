
#include "sp_unique.h"

#include <stdlib.h>

int sp_unique_make(
    sp_unique *owner,
    size_t size,
    sp_destructor_fn destroy
) {
    if (owner == NULL || size == 0) {
        return SP_UNIQUE_ERROR;
    }

    /* Do not overwrite an existing owner. */
    if (owner->ptr != NULL || owner->destroy != NULL) {
        return SP_UNIQUE_ERROR;
    }

    void *memory = malloc(size);

    if (memory == NULL) {
        return SP_UNIQUE_ERROR;
    }

    owner->ptr = memory;
    owner->destroy = destroy;

    return SP_UNIQUE_SUCCESS;
}

void *sp_unique_get(const sp_unique *owner) {
    if (owner == NULL) {
        return NULL;
    }

    return owner->ptr;
}

int sp_unique_move(sp_unique *dst, sp_unique *src) {
    if (dst == NULL || src == NULL) {
        return SP_UNIQUE_ERROR;
    }

    /* Moving a handle to itself changes nothing. */
    if (dst == src) {
        return SP_UNIQUE_SUCCESS;
    }

    /* A move must not overwrite an existing owner. */
    if (dst->ptr != NULL || dst->destroy != NULL) {
        return SP_UNIQUE_ERROR;
    }

    *dst = *src;
    *src = SP_UNIQUE_EMPTY;

    return SP_UNIQUE_SUCCESS;
}

void *sp_unique_release(sp_unique *owner) {
    if (owner == NULL) {
        return NULL;
    }

    void *memory = owner->ptr;
    *owner = SP_UNIQUE_EMPTY;

    return memory;
}

void sp_unique_free(sp_unique *owner) {
    if (owner == NULL) {
        return;
    }

    if (owner->ptr != NULL) {
        if (owner->destroy != NULL) {
            owner->destroy(owner->ptr);
        }

        free(owner->ptr);
    }

    *owner = SP_UNIQUE_EMPTY;
}
