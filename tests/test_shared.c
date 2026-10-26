#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "smartptr.h"

/*
 * Status is stored first and asserted after, so the calls still run
 * when assert is compiled out.
 */

static int destructor_calls = 0;

static void count_destructor(void *object) {
    (void)object;
    ++destructor_calls;
}

static void test_make_get_free(void) {
    sp_shared owner = SP_SHARED_EMPTY;

    sp_status status = sp_shared_make(&owner, sizeof(int), NULL);
    assert(status == SP_OK);
    assert(sp_shared_use_count(&owner) == 1);

    int *value = sp_shared_get(&owner);
    assert(value != NULL);
    *value = 7;
    assert(*(int *)sp_shared_get(&owner) == 7);

    status = sp_shared_free(&owner);
    assert(status == SP_OK);
    assert(sp_shared_get(&owner) == NULL);
    assert(sp_shared_use_count(&owner) == 0);
}

static void test_copy_shares_object(void) {
    sp_shared a = SP_SHARED_EMPTY;
    sp_shared b = SP_SHARED_EMPTY;
    sp_shared c = SP_SHARED_EMPTY;

    sp_status status = sp_shared_make(&a, sizeof(int), NULL);
    assert(status == SP_OK);

    status = sp_shared_copy(&b, &a);
    assert(status == SP_OK);
    status = sp_shared_copy(&c, &b);
    assert(status == SP_OK);

    assert(sp_shared_use_count(&a) == 3);
    assert(sp_shared_get(&a) == sp_shared_get(&b));
    assert(sp_shared_get(&b) == sp_shared_get(&c));

    /* A copy does not allocate a new object. */
    assert(sp_live_allocations() == 2);   /* control block + object */

    sp_shared_free(&a);
    sp_shared_free(&b);
    sp_shared_free(&c);
}

static void test_destroyed_only_by_last_owner(void) {
    sp_shared a = SP_SHARED_EMPTY;
    sp_shared b = SP_SHARED_EMPTY;
    destructor_calls = 0;

    sp_status status = sp_shared_make(&a, sizeof(int), count_destructor);
    assert(status == SP_OK);
    status = sp_shared_copy(&b, &a);
    assert(status == SP_OK);

    sp_shared_free(&a);
    assert(destructor_calls == 0);        /* b still owns it */
    assert(sp_shared_use_count(&b) == 1);

    sp_shared_free(&b);
    assert(destructor_calls == 1);        /* last owner gone */
    assert(sp_live_allocations() == 0);
}

static void test_double_free_is_harmless(void) {
    sp_shared owner = SP_SHARED_EMPTY;
    destructor_calls = 0;

    sp_status status = sp_shared_make(&owner, sizeof(int), count_destructor);
    assert(status == SP_OK);

    status = sp_shared_free(&owner);
    assert(status == SP_OK);
    status = sp_shared_free(&owner);
    assert(status == SP_OK);

    assert(destructor_calls == 1);
    assert(sp_live_allocations() == 0);
}

static void test_move(void) {
    sp_shared a = SP_SHARED_EMPTY;
    sp_shared b = SP_SHARED_EMPTY;

    sp_status status = sp_shared_make(&a, sizeof(int), NULL);
    assert(status == SP_OK);
    void *object = sp_shared_get(&a);

    status = sp_shared_move(&b, &a);
    assert(status == SP_OK);
    assert(sp_shared_get(&a) == NULL);
    assert(sp_shared_get(&b) == object);
    assert(sp_shared_use_count(&b) == 1); /* move does not change the count */

    sp_shared_free(&b);
}

static void test_invalid_inputs(void) {
    sp_shared a = SP_SHARED_EMPTY;
    sp_shared b = SP_SHARED_EMPTY;
    sp_shared bad = SP_SHARED_EMPTY;
    bad.magic = 0;

    assert(sp_shared_make(NULL, sizeof(int), NULL) == SP_ERR_NULL_ARG);
    assert(sp_shared_make(&a, 0, NULL) == SP_ERR_INVALID_SIZE);
    assert(sp_shared_copy(&a, NULL) == SP_ERR_NULL_ARG);
    assert(sp_shared_free(NULL) == SP_ERR_NULL_ARG);
    assert(sp_shared_free(&bad) == SP_ERR_INVALID_HANDLE);
    assert(sp_shared_get(&bad) == NULL);

    /* Copy into an occupied handle is refused and changes nothing. */
    sp_status status = sp_shared_make(&a, sizeof(int), NULL);
    assert(status == SP_OK);
    status = sp_shared_make(&b, sizeof(int), NULL);
    assert(status == SP_OK);
    assert(sp_shared_copy(&b, &a) == SP_ERR_NOT_EMPTY);
    assert(sp_shared_use_count(&a) == 1);
    assert(sp_shared_use_count(&b) == 1);

    sp_shared_free(&a);
    sp_shared_free(&b);
}

static void *fail_on_second_call(size_t size) {
    static int calls = 0;
    ++calls;
    return calls == 2 ? NULL : malloc(size);
}

static void test_allocation_failure_no_leak(void) {
    sp_shared owner = SP_SHARED_EMPTY;

    assert(sp_set_allocator(fail_on_second_call, free) == SP_OK);

    /* Control block succeeds, object fails: control block must be undone. */
    sp_status status = sp_shared_make(&owner, sizeof(int), NULL);
    assert(status == SP_ERR_NO_MEMORY);
    assert(sp_shared_get(&owner) == NULL);
    assert(sp_live_allocations() == 0);

    assert(sp_set_allocator(NULL, NULL) == SP_OK);
}

static void test_many_owners(void) {
    enum { N = 1000 };
    static sp_shared owners[N];
    destructor_calls = 0;

    for (int i = 0; i < N; ++i) {
        owners[i] = SP_SHARED_EMPTY;
    }

    sp_status status = sp_shared_make(&owners[0], 64, count_destructor);
    assert(status == SP_OK);

    for (int i = 1; i < N; ++i) {
        status = sp_shared_copy(&owners[i], &owners[i - 1]);
        assert(status == SP_OK);
    }
    assert(sp_shared_use_count(&owners[0]) == N);

    for (int i = 0; i < N; ++i) {
        sp_shared_free(&owners[i]);
    }

    assert(destructor_calls == 1);
    assert(sp_live_allocations() == 0);
}

int main(void) {
    test_make_get_free();
    test_copy_shares_object();
    test_destroyed_only_by_last_owner();
    test_double_free_is_harmless();
    test_move();
    test_invalid_inputs();
    test_allocation_failure_no_leak();
    test_many_owners();
    assert(sp_live_allocations() == 0);

    printf("All shared-pointer tests passed! (8 tests)\n");
    return 0;
}
