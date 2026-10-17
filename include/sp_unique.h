
#ifndef SP_UNIQUE_H
#define SP_UNIQUE_H

#include <stddef.h>

/*
 * Cleans up resources owned by the object.
 * Do not free the outer object in this callback;
 * the library frees it after calling the callback.
 */
typedef void (*sp_destructor_fn)(void *object);

typedef struct {
    void *ptr;
    sp_destructor_fn destroy;
} sp_unique;

#define SP_UNIQUE_EMPTY ((sp_unique){ NULL, NULL })

enum {
    SP_UNIQUE_SUCCESS = 0,
    SP_UNIQUE_ERROR = -1
};

/*
 * The owner must be initialized as SP_UNIQUE_EMPTY.
 * Returns SP_UNIQUE_SUCCESS on success, otherwise
 * SP_UNIQUE_ERROR.
 */
int sp_unique_make(
    sp_unique *owner,
    size_t size,
    sp_destructor_fn destroy
);

/* Returns a borrowed pointer. Ownership does not change. */
void *sp_unique_get(const sp_unique *owner);

/* Transfers ownership into an empty destination. */
int sp_unique_move(sp_unique *dst, sp_unique *src);

/*
 * Transfers the allocation to the caller.
 * The caller becomes responsible for all cleanup.
 */
void *sp_unique_release(sp_unique *owner);

/* Destroys the owned object and empties the handle. */
void sp_unique_free(sp_unique *owner);

#endif
