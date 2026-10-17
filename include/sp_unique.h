
#ifndef SP_UNIQUE_H
#define SP_UNIQUE_H

#include <stddef.h>

/*
 * Cleans up resources owned by the object.
 * Do not free the outer object in this callback;
 * the library frees it after calling the callback.
 */
typedef void (*sp_destructor_fn)(void *object);

#define SP_MAGIC_UNIQUE 0x5350554Eu

typedef struct {
    void *ptr;
    sp_destructor_fn destroy;
    unsigned magic;
} sp_unique;

/* Every handle must be initialized before use. */
#define SP_UNIQUE_EMPTY \
    ((sp_unique){ NULL, NULL, SP_MAGIC_UNIQUE })

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

/*
 * Takes ownership of a malloc-compatible allocation.
 * Destination must be empty.
 * On failure, ownership remains with the caller.
 * raw == NULL creates an empty handle.
 */
int sp_unique_adopt(
    sp_unique *owner,
    void *raw,
    sp_destructor_fn destroy
);

/*
 * Destroys the old object and takes ownership of new_raw.
 * new_raw must be malloc-compatible or NULL.
 * Resetting to the currently owned pointer is a no-op;
 * its existing destructor is preserved.
 */
int sp_unique_reset(
    sp_unique *owner,
    void *new_raw,
    sp_destructor_fn destroy
);


#endif
