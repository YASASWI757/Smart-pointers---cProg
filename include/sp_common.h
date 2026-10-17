#ifndef SP_COMMON_H
#define SP_COMMON_H
#include <stddef.h>

typedef enum {
    SP_OK = 0,
    SP_ERR_NULL_ARG,
    SP_ERR_NO_MEMORY,
    SP_ERR_EMPTY,
    SP_ERR_INVALID_HANDLE,
    SP_ERR_EXPIRED,
    SP_ERR_OVERFLOW,
    SP_ERR_INVALID_SIZE,
    SP_ERR_NOT_EMPTY
} sp_status;

const char *sp_strerror(sp_status status);
typedef void *(*sp_malloc_fn)(size_t size);
typedef void (*sp_free_fn)(void *ptr);

/*
 * Number of allocations currently managed by the library,
 * including adopted allocations. Released allocations and
 * unregistered nested resources are not counted.
 * Not a system-wide leak detector.
 */
size_t sp_live_allocations(void);

/*
 * Set a compatible allocation/deallocation pair.
 * Passing NULL, NULL restores malloc/free.
 * One NULL argument is invalid.
 * Changes are rejected while tracked allocations exist.
 */
sp_status sp_set_allocator(
    sp_malloc_fn allocate,
    sp_free_fn deallocate
);


#endif