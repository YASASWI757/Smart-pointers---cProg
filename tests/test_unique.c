
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "smartptr.h"

static void test_make_and_get(void) {
    sp_unique owner = SP_UNIQUE_EMPTY;

    assert(sp_unique_make(&owner, sizeof(int), NULL)
           == SP_UNIQUE_SUCCESS);

    int *value = sp_unique_get(&owner);
    assert(value != NULL);

    *value = 123;
    assert(*(int *)sp_unique_get(&owner) == 123);

    sp_unique_free(&owner);
    assert(sp_unique_get(&owner) == NULL);
}

static void test_move(void) {
    sp_unique source = SP_UNIQUE_EMPTY;
    sp_unique destination = SP_UNIQUE_EMPTY;

    assert(sp_unique_make(&source, sizeof(int), NULL)
           == SP_UNIQUE_SUCCESS);

    *(int *)sp_unique_get(&source) = 42;

    void *original_address = sp_unique_get(&source);

    assert(sp_unique_move(&destination, &source)
           == SP_UNIQUE_SUCCESS);

    assert(sp_unique_get(&source) == NULL);
    assert(sp_unique_get(&destination) == original_address);
    assert(*(int *)sp_unique_get(&destination) == 42);

    sp_unique_free(&source);
    sp_unique_free(&destination);
    assert(sp_unique_get(&destination) == NULL);
}

static void test_release(void) {
    sp_unique owner = SP_UNIQUE_EMPTY;

    assert(sp_unique_make(&owner, sizeof(int), NULL)
           == SP_UNIQUE_SUCCESS);

    *(int *)sp_unique_get(&owner) = 99;

    int *raw = sp_unique_release(&owner);

    assert(raw != NULL);
    assert(*raw == 99);
    assert(sp_unique_get(&owner) == NULL);

    /* Ownership now belongs to this test. */
    free(raw);
}

static void test_invalid_inputs(void) {
    sp_unique owner = SP_UNIQUE_EMPTY;

    assert(sp_unique_make(&owner, 0, NULL)
           == SP_UNIQUE_ERROR);

    assert(sp_unique_make(NULL, sizeof(int), NULL)
           == SP_UNIQUE_ERROR);

    assert(sp_unique_move(NULL, &owner)
           == SP_UNIQUE_ERROR);
}
static int destructor_calls = 0;

static void count_destructor(void *object) {
    (void)object;
    ++destructor_calls;
}

static void test_adopt(void) {
    sp_unique owner = SP_UNIQUE_EMPTY;

    int *raw = malloc(sizeof *raw);
    assert(raw != NULL);
    *raw = 77;

    assert(sp_unique_adopt(&owner, raw, NULL)
           == SP_UNIQUE_SUCCESS);

    assert(sp_unique_get(&owner) == raw);
    assert(*(int *)sp_unique_get(&owner) == 77);

    /* The handle now owns raw. */
    sp_unique_free(&owner);
}

static void test_adopt_occupied(void) {
    sp_unique owner = SP_UNIQUE_EMPTY;

    assert(sp_unique_make(&owner, sizeof(int), NULL)
           == SP_UNIQUE_SUCCESS);

    void *original = sp_unique_get(&owner);

    int *raw = malloc(sizeof *raw);
    assert(raw != NULL);

    assert(sp_unique_adopt(&owner, raw, NULL)
           == SP_UNIQUE_ERROR);

    assert(sp_unique_get(&owner) == original);

    /* Failed adoption leaves raw owned by this test. */
    free(raw);
    sp_unique_free(&owner);
}

static void test_reset(void) {
    sp_unique owner = SP_UNIQUE_EMPTY;
    destructor_calls = 0;

    assert(sp_unique_make(
        &owner, sizeof(int), count_destructor
    ) == SP_UNIQUE_SUCCESS);

    int *replacement = malloc(sizeof *replacement);
    assert(replacement != NULL);
    *replacement = 88;

    assert(sp_unique_reset(
        &owner, replacement, count_destructor
    ) == SP_UNIQUE_SUCCESS);

    assert(destructor_calls == 1);
    assert(sp_unique_get(&owner) == replacement);
    assert(*(int *)sp_unique_get(&owner) == 88);

    /* Same-pointer reset preserves the original destructor. */
    assert(sp_unique_reset(&owner, replacement, NULL)
           == SP_UNIQUE_SUCCESS);
    assert(destructor_calls == 1);

    /* Reset to NULL destroys the replacement. */
    assert(sp_unique_reset(&owner, NULL, NULL)
           == SP_UNIQUE_SUCCESS);
    assert(destructor_calls == 2);
    assert(sp_unique_get(&owner) == NULL);

    sp_unique_free(&owner);
    sp_unique_free(&owner);
    assert(destructor_calls == 2);
}

static void test_adopt_reset_empty_and_null(void) {
    sp_unique owner = SP_UNIQUE_EMPTY;

    assert(sp_unique_adopt(NULL, NULL, NULL)
           == SP_UNIQUE_ERROR);
    assert(sp_unique_reset(NULL, NULL, NULL)
           == SP_UNIQUE_ERROR);

    assert(sp_unique_adopt(&owner, NULL, count_destructor)
           == SP_UNIQUE_SUCCESS);
    assert(owner.ptr == NULL);
    assert(owner.destroy == NULL);

    assert(sp_unique_reset(&owner, NULL, NULL)
           == SP_UNIQUE_SUCCESS);
}

static void *always_fail(size_t size) {
    (void)size;
    return NULL;
}

static void test_make_allocation_failure(void) {
    sp_unique owner = SP_UNIQUE_EMPTY;

    assert(sp_live_allocations() == 0);

    assert(sp_set_allocator(always_fail, free) == SP_OK);

    assert(sp_unique_make(&owner, sizeof(int), NULL)
           == SP_UNIQUE_ERROR);

    assert(sp_unique_get(&owner) == NULL);
    assert(sp_live_allocations() == 0);

    assert(sp_set_allocator(NULL, NULL) == SP_OK);
}

static void test_ownership_tracking(void) {
    sp_unique source = SP_UNIQUE_EMPTY;
    sp_unique destination = SP_UNIQUE_EMPTY;

    assert(sp_live_allocations() == 0);

    assert(sp_unique_make(&source, sizeof(int), NULL)
           == SP_UNIQUE_SUCCESS);
    assert(sp_live_allocations() == 1);

    assert(sp_set_allocator(NULL, NULL)
           == SP_ERR_NOT_EMPTY);

    assert(sp_unique_move(&destination, &source)
           == SP_UNIQUE_SUCCESS);
    assert(sp_live_allocations() == 1);

    int *raw = sp_unique_release(&destination);
    assert(raw != NULL);
    assert(sp_live_allocations() == 0);

    assert(sp_unique_adopt(&source, raw, NULL)
           == SP_UNIQUE_SUCCESS);
    assert(sp_live_allocations() == 1);

    int *replacement = malloc(sizeof *replacement);
    assert(replacement != NULL);

    assert(sp_unique_reset(&source, replacement, NULL)
           == SP_UNIQUE_SUCCESS);
    assert(sp_live_allocations() == 1);

    assert(sp_unique_reset(&source, NULL, NULL)
           == SP_UNIQUE_SUCCESS);
    assert(sp_live_allocations() == 0);

    sp_unique_free(&source);
    sp_unique_free(&destination);
    assert(sp_live_allocations() == 0);
}


int main(void) {
    test_make_and_get();
    test_move();
    test_release();
    test_invalid_inputs();
    test_adopt();
    test_adopt_occupied();
    test_reset();
    test_adopt_reset_empty_and_null();
    assert(sp_live_allocations() == 0);
    test_make_allocation_failure();
    test_ownership_tracking();
    assert(sp_live_allocations() == 0);

    printf("All unique-pointer tests passed!\n");
    return 0;
}
