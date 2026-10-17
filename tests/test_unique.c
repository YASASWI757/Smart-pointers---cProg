
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "sp_unique.h"

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

int main(void) {
    test_make_and_get();
    test_move();
    test_release();
    test_invalid_inputs();

    printf("All unique-pointer tests passed!\n");
    return 0;
}
