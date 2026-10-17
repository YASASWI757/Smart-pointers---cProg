    #include "sp_unique.h"
#include "sp_internal.h"

/*
 * Requires NULL or a pointer to an initialized handle.
 * Cannot validate arbitrary memory or detect copied owners.
 */
static sp_status validate(const sp_unique *owner) {
    if (owner == NULL) {
        return SP_ERR_NULL_ARG;
    }

    if (owner->magic != SP_MAGIC_UNIQUE) {
        return SP_ERR_INVALID_HANDLE;
    }

    if (owner->ptr == NULL && owner->destroy != NULL) {
        return SP_ERR_INVALID_HANDLE;
    }

    return SP_OK;
}

/* The handle is already empty/replaced before this is called. */
static void destroy_object(
    void *object,
    sp_destructor_fn destroy
) {
    if (object != NULL) {
        if (destroy != NULL) {
            destroy(object);
        }

        sp__free(object);
    }
}

sp_status sp_unique_make(
    sp_unique *owner,
    size_t size,
    sp_destructor_fn destroy
) {
    sp_status status = validate(owner);

    if (status != SP_OK) {
        return status;
    }

    if (size == 0) {
        return SP_ERR_INVALID_SIZE;
    }

    if (owner->ptr != NULL) {
        return SP_ERR_NOT_EMPTY;
    }

    void *memory = sp__alloc(size);

    if (memory == NULL) {
        return SP_ERR_NO_MEMORY;
    }

    owner->ptr = memory;
    owner->destroy = destroy;

    return SP_OK;
}

sp_status sp_unique_adopt(
    sp_unique *owner,
    void *raw,
    sp_destructor_fn destroy
) {
    sp_status status = validate(owner);

    if (status != SP_OK) {
        return status;
    }

    if (owner->ptr != NULL) {
        return SP_ERR_NOT_EMPTY;
    }

    if (raw != NULL) {
        status = sp__track_external();

        if (status != SP_OK) {
            return status;
        }
    }

    owner->ptr = raw;
    owner->destroy = raw != NULL ? destroy : NULL;

    return SP_OK;
}

void *sp_unique_get(const sp_unique *owner) {
    return validate(owner) == SP_OK ? owner->ptr : NULL;
}

sp_status sp_unique_move(sp_unique *dst, sp_unique *src) {
    sp_status status = validate(dst);

    if (status != SP_OK) {
        return status;
    }

    status = validate(src);

    if (status != SP_OK) {
        return status;
    }

    if (dst == src) {
        return SP_OK;
    }

    if (dst->ptr != NULL) {
        return SP_ERR_NOT_EMPTY;
    }

    *dst = *src;
    *src = SP_UNIQUE_EMPTY;

    return SP_OK;
}

sp_status sp_unique_release(sp_unique *owner, void **raw_out) {
    if (raw_out == NULL) {
        return SP_ERR_NULL_ARG;
    }

    sp_status status = validate(owner);

    if (status != SP_OK) {
        return status;
    }

    /* Writing through this alias would refill the emptied handle. */
    if (raw_out == &owner->ptr) {
        return SP_ERR_INVALID_HANDLE;
    }

    void *memory = owner->ptr;
    *owner = SP_UNIQUE_EMPTY;

    if (memory != NULL) {
        sp__untrack();
    }

    *raw_out = memory;
    return SP_OK;
}

sp_status sp_unique_reset(
    sp_unique *owner,
    void *new_raw,
    sp_destructor_fn destroy
) {
    sp_status status = validate(owner);

    if (status != SP_OK) {
        return status;
    }

    if (owner->ptr == new_raw) {
        return SP_OK;
    }

    if (new_raw != NULL) {
        status = sp__track_external();

        if (status != SP_OK) {
            return status;
        }
    }

    void *old_ptr = owner->ptr;
    sp_destructor_fn old_destroy = owner->destroy;

    owner->ptr = new_raw;
    owner->destroy = new_raw != NULL ? destroy : NULL;

    destroy_object(old_ptr, old_destroy);

    return SP_OK;
}

sp_status sp_unique_free(sp_unique *owner) {
    sp_status status = validate(owner);

    if (status != SP_OK) {
        return status;
    }

    void *old_ptr = owner->ptr;
    sp_destructor_fn old_destroy = owner->destroy;

    *owner = SP_UNIQUE_EMPTY;
    destroy_object(old_ptr, old_destroy);

    return SP_OK;
}