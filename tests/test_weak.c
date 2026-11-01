/* test_weak.c - weak observers. */
#include "smartptr.h"
#include "check.h"

typedef struct {
    int value;
} Node;

static void test_lock_while_alive(void)
{
    Node *n = sp_make_shared(Node);
    sp_weak w = sp_weak_from(n);
    CHECK(!sp_weak_expired(&w) && sp_weak_count(n) == 1);
    Node *again = sp_weak_lock(&w);
    CHECK(again == n && sp_use_count(n) == 2);
    sp_drop(&again);
    sp_drop(&n);
    CHECK(sp_weak_expired(&w));
    CHECK(sp_weak_lock(&w) == NULL);
    CHECK(sp_weak_peek(&w) == NULL);
    sp_weak_drop(&w);
}

static void test_weak_does_not_keep_alive(void)
{
    Node *n = sp_make_shared(Node);
    sp_weak w = sp_weak_from(n);
    CHECK(sp_use_count(n) == 1);
    sp_drop(&n);
    CHECK(sp_weak_expired(&w));
    sp_weak_drop(&w);
    CHECK(w.rec_ == NULL);
}

static void test_copies_are_counted(void)
{
    Node *n = sp_make_shared(Node);
    sp_weak a = sp_weak_from(n);
    sp_weak b = sp_weak_copy(&a);
    CHECK(sp_weak_count(n) == 2);
    sp_weak_drop(&a);
    CHECK(sp_weak_count(n) == 1);
    sp_drop(&n);
    CHECK(sp_weak_expired(&b));
    sp_weak_drop(&b);
}

static void test_weak_survives_quarantine_eviction(void)
{
    Node *n = sp_make_shared(Node);
    sp_weak w = sp_weak_from(n);
    sp_drop(&n);
    for (int i = 0; i < 5000; ++i) {         /* push the dead block out of quarantine */
        void *p = malloc(8);
        free(p);
    }
    CHECK(sp_weak_expired(&w));
    CHECK(sp_weak_lock(&w) == NULL);
    sp_weak_drop(&w);                        /* frees the detached record */
}

static void test_weak_to_unique_can_peek_not_lock(void)
{
    size_t before = ISSUES(SP_ISSUE_MISUSE);
    Node *u = sp_make_unique(Node);
    u->value = 42;
    sp_weak w = sp_weak_from(u);
    Node *seen = sp_weak_peek(&w);
    CHECK(seen == u && seen->value == 42);
    CHECK(sp_weak_lock(&w) == NULL);
    CHECK(ISSUES(SP_ISSUE_MISUSE) == before + 1);
    sp_drop(&u);
    CHECK(sp_weak_peek(&w) == NULL);
    sp_weak_drop(&w);
}

static void test_empty_and_raw(void)
{
    sp_weak empty = SP_WEAK_EMPTY;
    CHECK(sp_weak_expired(&empty) && sp_weak_lock(&empty) == NULL);
    sp_weak_drop(&empty);
    size_t before = ISSUES(SP_ISSUE_MISUSE);
    char *raw = malloc(4);
    sp_weak w = sp_weak_from(raw);
    CHECK(w.rec_ == NULL && ISSUES(SP_ISSUE_MISUSE) == before + 1);
    free(raw);
}

static void test_auto_weak(void)
{
    Node *n = sp_make_shared(Node);
    {
        sp_auto_weak sp_weak w = sp_weak_from(n);
        CHECK(sp_weak_count(n) == 1);
    }
    CHECK(sp_weak_count(n) == 0);
    sp_drop(&n);
}

int main(void)
{
    sp_guard_set_mode(SP_MODE_SILENT);
    printf("test_weak\n");
    RUN(test_lock_while_alive);
    RUN(test_weak_does_not_keep_alive);
    RUN(test_copies_are_counted);
    RUN(test_weak_survives_quarantine_eviction);
    RUN(test_weak_to_unique_can_peek_not_lock);
    RUN(test_empty_and_raw);
    RUN(test_auto_weak);
    printf("  %d passed\n", check_passed);
    return 0;
}
