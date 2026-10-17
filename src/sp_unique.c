
#include "sp_unique.h"

#include "sp_internal.h"

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

    void *memory = sp__alloc(size);

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

    if (memory != NULL) {
        sp__untrack();
    }

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

        sp__free(owner->ptr);;
    }

    *owner = SP_UNIQUE_EMPTY;
}

int sp_unique_adopt(
    sp_unique *owner,
    void *raw,
    sp_destructor_fn destroy
) {
    if (owner == NULL) {
        return SP_UNIQUE_ERROR;
    }

    if (owner->ptr != NULL || owner->destroy != NULL) {
        return SP_UNIQUE_ERROR;
    }

    if (raw != NULL) {
        if (sp__track_external() != SP_OK) {
            return SP_UNIQUE_ERROR;
        }
    }

    owner->ptr = raw;
    owner->destroy = raw != NULL ? destroy : NULL;

    return SP_UNIQUE_SUCCESS;
}

int sp_unique_reset(
    sp_unique *owner,
    void *new_raw,
    sp_destructor_fn destroy
) {
    if (owner == NULL) {
        return SP_UNIQUE_ERROR;
    }

    if (owner->ptr == new_raw) {
        return SP_UNIQUE_SUCCESS;
    }

    /*
     * Register the replacement before changing the handle.
     * Failure leaves both ownerships unchanged.
     */
    if (new_raw != NULL) {
        if (sp__track_external() != SP_OK) {
            return SP_UNIQUE_ERROR;
        }
    }

    void *old_ptr = owner->ptr;
    sp_destructor_fn old_destroy = owner->destroy;

    owner->ptr = new_raw;
    owner->destroy = new_raw != NULL ? destroy : NULL;

    if (old_ptr != NULL) {
        if (old_destroy != NULL) {
            old_destroy(old_ptr);
        }

        sp__free(old_ptr);
    }

    return SP_UNIQUE_SUCCESS;
}