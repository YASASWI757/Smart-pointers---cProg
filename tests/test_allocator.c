#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "sp_internal.h"

static size_t allocate_calls = 0;
static size_t deallocate_calls = 0;

static void *counting_allocate(size_t size) {
    ++allocate_calls;
    return malloc(size);
}

static void counting_deallocate(void *ptr) {
    ++deallocate_calls;
    free(ptr);
}

static void *failing_allocate(size_t size) {
    (void)size;
    return NULL;
}

int main(void) {
    assert(sp_live_allocations() == 0);

    /* Reject incomplete allocator pairs. */
    assert(sp_set_allocator(counting_allocate, NULL)
           == SP_ERR_NULL_ARG);

    assert(sp_set_allocator(NULL, counting_deallocate)
           == SP_ERR_NULL_ARG);

    /* Successful allocations are counted. */
    assert(sp_set_allocator(
        counting_allocate, counting_deallocate
    ) == SP_OK);

    void *first = sp__alloc(sizeof(int));
    void *second = sp__alloc(64);

    assert(first != NULL);
    assert(second != NULL);
    assert(allocate_calls == 2);
    assert(sp_live_allocations() == 2);

    /* Allocator changes must not invalidate live allocations. */
    assert(sp_set_allocator(NULL, NULL)
           == SP_ERR_NOT_EMPTY);

    sp__free(NULL);
    assert(sp_live_allocations() == 2);
    assert(deallocate_calls == 0);

    sp__free(first);
    assert(sp_live_allocations() == 1);

    sp__free(second);
    assert(sp_live_allocations() == 0);
    assert(deallocate_calls == 2);

    /* Allocation failure must not increase the count. */
    assert(sp_set_allocator(
        failing_allocate, counting_deallocate
    ) == SP_OK);

    assert(sp__alloc(32) == NULL);
    assert(sp_live_allocations() == 0);

    /* Restore the default allocator. */
    assert(sp_set_allocator(NULL, NULL) == SP_OK);

    assert(sp__alloc(0) == NULL);
    assert(sp_live_allocations() == 0);

    void *last = sp__alloc(16);
    assert(last != NULL);
    assert(sp_live_allocations() == 1);

    sp__free(last);
    assert(sp_live_allocations() == 0);

    puts("All allocator tests passed!");
    return 0;
}