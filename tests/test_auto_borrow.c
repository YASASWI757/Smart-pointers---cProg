/* test_auto_borrow.c - scope cleanup and deferred destruction. */
#include "smartptr.h"
#include "check.h"

typedef struct {
    int value;
} Box;

static int dtor_calls;
static void box_dtor(void *object) { (void)object; dtor_calls++; }

static size_t live(void) { return sp_guard_stats().live_blocks; }

static int early_return(int flag)
{
    sp_auto char *text = strdup("temporary");
    sp_auto Box *box = sp_make_unique_d(Box, box_dtor);
    if (flag) return 1;
    box->value = (int)strlen(text);
    return box->value;
}

static void test_auto_frees_raw_and_unique(void)
{
    size_t before = live();
    dtor_calls = 0;
    early_return(1);
    early_return(0);
    CHECK(live() == before && dtor_calls == 2);
}

static void test_auto_drops_one_shared_reference(void)
{
    Box *keeper = sp_make_shared(Box);
    {
        sp_auto Box *temp = sp_share(keeper);
        CHECK(sp_use_count(keeper) == 2);
        (void)temp;
    }
    CHECK(sp_use_count(keeper) == 1);
    sp_drop(&keeper);
}

static void test_auto_after_manual_free_is_quiet(void)
{
    size_t issues = sp_guard_stats().issues;
    {
        sp_auto char *p = malloc(8);
        free(p);
    }
    CHECK(sp_guard_stats().issues == issues);
}

static void test_auto_after_move_does_nothing(void)
{
    Box *outer;
    {
        sp_auto Box *inner = sp_make_unique(Box);
        outer = sp_move(&inner);             /* inner is NULL at scope end */
    }
    CHECK(sp_kind_of(outer) == SP_UNIQUE);
    sp_drop(&outer);
}

static void test_borrow_defers_destruction(void)
{
    dtor_calls = 0;
    size_t before = ISSUES(SP_ISSUE_BORROW);
    Box *owner = sp_make_unique_d(Box, box_dtor);
    owner->value = 3;
    Box *view = sp_borrow(owner);
    sp_drop(&owner);
    CHECK(ISSUES(SP_ISSUE_BORROW) == before + 1);
    CHECK(dtor_calls == 0 && view->value == 3);   /* still readable */
    sp_unborrow(view);
    CHECK(dtor_calls == 1);
}

static void test_nested_borrows(void)
{
    dtor_calls = 0;
    Box *owner = sp_make_shared_d(Box, box_dtor);
    Box *v1 = sp_borrow(owner);
    Box *v2 = sp_borrow(owner);
    free(owner);                             /* last owner via free(): deferred too */
    sp_unborrow(v1);
    CHECK(dtor_calls == 0);
    sp_unborrow(v2);
    CHECK(dtor_calls == 1);
}

static void test_unbalanced_unborrow(void)
{
    size_t before = ISSUES(SP_ISSUE_MISUSE);
    Box *b = sp_make_unique(Box);
    sp_unborrow(b);
    CHECK(ISSUES(SP_ISSUE_MISUSE) == before + 1);
    sp_drop(&b);
}

static void test_realloc_while_borrowed(void)
{
    size_t before = ISSUES(SP_ISSUE_BORROW);
    char *p = malloc(8);
    char *view = sp_borrow(p);
    CHECK(realloc(p, 64) == NULL);
    CHECK(ISSUES(SP_ISSUE_BORROW) == before + 1);
    sp_unborrow(view);
    free(p);
}

int main(void)
{
    sp_guard_set_mode(SP_MODE_SILENT);
    printf("test_auto_borrow\n");
    RUN(test_auto_frees_raw_and_unique);
    RUN(test_auto_drops_one_shared_reference);
    RUN(test_auto_after_manual_free_is_quiet);
    RUN(test_auto_after_move_does_nothing);
    RUN(test_borrow_defers_destruction);
    RUN(test_nested_borrows);
    RUN(test_unbalanced_unborrow);
    RUN(test_realloc_while_borrowed);
    printf("  %d passed\n", check_passed);
    return 0;
}
