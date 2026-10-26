#include <stdint.h>

#include "sp_shared.h"
#include "sp_internal.h"

/*
 * One control block per shared object.
 * Every sp_shared that owns the object points here.
 */
struct sp_control_block {
    void *object;
    sp_destructor_fn destroy;
    size_t strong;          /* number of owning handles */
};

static sp_status validate(const sp_shared *owner) {
    if (owner == NULL) {
        return SP_ERR_NULL_ARG;
    }

    if (owner->magic != SP_MAGIC_SHARED) {
        return SP_ERR_INVALID_HANDLE;
    }

    return SP_OK;
}

/* Runs only when the last owner has gone. */
static void destroy_control_block(sp_control_block *control) {
    if (control->destroy != NULL) {
        control->destroy(control->object);
    }

    sp__free(control->object);
    sp__free(control);
}

sp_status sp_shared_make(
    sp_shared *owner,
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

    if (owner->control != NULL) {
        return SP_ERR_NOT_EMPTY;
    }

    sp_control_block *control = sp__alloc(sizeof *control);

    if (control == NULL) {
        return SP_ERR_NO_MEMORY;
    }

    void *object = sp__alloc(size);

    if (object == NULL) {
        sp__free(control);          /* undo the first allocation: no leak */
        return SP_ERR_NO_MEMORY;
    }

    control->object = object;
    control->destroy = destroy;
    control->strong = 1;

    owner->control = control;

    return SP_OK;
}

sp_status sp_shared_copy(sp_shared *dst, const sp_shared *src) {
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

    if (dst->control != NULL) {
        return SP_ERR_NOT_EMPTY;
    }

    if (src->control == NULL) {
        return SP_OK;               /* copying "nothing" leaves dst empty */
    }

    if (src->control->strong == SIZE_MAX) {
        return SP_ERR_OVERFLOW;
    }

    ++src->control->strong;
    dst->control = src->control;

    return SP_OK;
}

sp_status sp_shared_move(sp_shared *dst, sp_shared *src) {
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

    if (dst->control != NULL) {
        return SP_ERR_NOT_EMPTY;
    }

    dst->control = src->control;
    *src = SP_SHARED_EMPTY;

    return SP_OK;
}

void *sp_shared_get(const sp_shared *owner) {
    if (validate(owner) != SP_OK || owner->control == NULL) {
        return NULL;
    }

    return owner->control->object;
}

size_t sp_shared_use_count(const sp_shared *owner) {
    if (validate(owner) != SP_OK || owner->control == NULL) {
        return 0;
    }

    return owner->control->strong;
}

sp_status sp_shared_free(sp_shared *owner) {
    sp_status status = validate(owner);

    if (status != SP_OK) {
        return status;
    }

    sp_control_block *control = owner->control;
    *owner = SP_SHARED_EMPTY;       /* empty the handle first */

    if (control == NULL) {
        return SP_OK;
    }

    --control->strong;

    if (control->strong == 0) {
        destroy_control_block(control);
    }

    return SP_OK;
}
