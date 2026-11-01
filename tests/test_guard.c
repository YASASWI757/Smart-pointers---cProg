/* test_guard.c - Sentinel on plain malloc/free code. */
#include "smartptr.h"
#include "check.h"

static void test_malloc_free_counts(void)
{
    sp_stats before = sp_guard_stats();
    char *p = malloc(10);
    CHECK(p != NULL);
    CHECK(sp_kind_of(p) == SP_RAW);
    CHECK(sp_guard_stats().live_blocks == before.live_blocks + 1);
    free(p);
    CHECK(sp_guard_stats().live_blocks == before.live_blocks);
    CHECK(sp_guard_stats().frees == before.frees + 1);
}

static void test_free_null_is_fine(void)
{
    size_t issues = sp_guard_stats().issues;
    free(NULL);
    CHECK(sp_guard_stats().issues == issues);
}

static void test_double_free_blocked(void)
{
    size_t before = ISSUES(SP_ISSUE_DOUBLE_FREE);
    int *p = malloc(sizeof *p);
    int *alias = p;
    free(p);
    free(alias);
    free(alias);
    CHECK(ISSUES(SP_ISSUE_DOUBLE_FREE) == before + 2);
}

static void test_invalid_free_of_stack(void)
{
    size_t before = ISSUES(SP_ISSUE_INVALID_FREE);
    int on_stack = 5;
    free(&on_stack);
    CHECK(ISSUES(SP_ISSUE_INVALID_FREE) == before + 1);
}

static void test_interior_free_blocked(void)
{
    size_t before = ISSUES(SP_ISSUE_INTERIOR_FREE);
    char *p = malloc(20);
    free(p + 4);
    CHECK(ISSUES(SP_ISSUE_INTERIOR_FREE) == before + 1);
    CHECK(sp_kind_of(p) == SP_RAW);          /* still alive */
    free(p);
}

static void test_overflow_detected_at_free(void)
{
    size_t before = ISSUES(SP_ISSUE_OVERFLOW);
    char *p = malloc(5);
    memcpy(p, "Hello", 6);                   /* one byte too many */
    free(p);
    CHECK(ISSUES(SP_ISSUE_OVERFLOW) == before + 1);
}

static void test_overflow_detected_by_check(void)
{
    size_t before = ISSUES(SP_ISSUE_OVERFLOW);
    int *a = malloc(4 * sizeof *a);
    for (int i = 0; i <= 4; ++i) a[i] = i;   /* off-by-one loop */
    CHECK(sp_guard_check() >= 1);
    CHECK(ISSUES(SP_ISSUE_OVERFLOW) == before + 1);
    free(a);                                 /* reported once only */
    CHECK(ISSUES(SP_ISSUE_OVERFLOW) == before + 1);
}

static void test_use_after_free_write(void)
{
    size_t before = ISSUES(SP_ISSUE_USE_AFTER_FREE);
    int *p = calloc(4, sizeof *p);
    int *stale = p;
    free(p);
    stale[1] = 7;
    sp_guard_check();
    CHECK(ISSUES(SP_ISSUE_USE_AFTER_FREE) == before + 1);
}

static void test_calloc_zeroes_and_overflow(void)
{
    int *z = calloc(8, sizeof *z);
    for (int i = 0; i < 8; ++i) CHECK(z[i] == 0);
    free(z);
    size_t before = ISSUES(SP_ISSUE_ALLOC_FAIL);
    CHECK(calloc(SIZE_MAX, 2) == NULL);
    CHECK(ISSUES(SP_ISSUE_ALLOC_FAIL) == before + 1);
}

static void test_realloc_keeps_data_and_guards_old_pointer(void)
{
    char *p = malloc(4);
    memcpy(p, "abc", 4);
    char *old = p;
    p = realloc(p, 100);
    CHECK(p != NULL && strcmp(p, "abc") == 0);
    CHECK(sp_kind_of(old) == SP_NONE || old == p);
    size_t before = ISSUES(SP_ISSUE_USE_AFTER_FREE);
    if (old != p) {
        old[0] = 'X';                        /* stale pointer after realloc moved the block */
        sp_guard_check();
        CHECK(ISSUES(SP_ISSUE_USE_AFTER_FREE) == before + 1);
    }
    free(p);
}

static void test_realloc_edge_cases(void)
{
    char *p = realloc(NULL, 8);              /* acts like malloc */
    CHECK(p != NULL);
    CHECK(realloc(p, 0) == NULL);            /* acts like free */
    size_t before = ISSUES(SP_ISSUE_USE_AFTER_FREE);
    CHECK(realloc(p, 16) == NULL);           /* already freed: blocked */
    CHECK(ISSUES(SP_ISSUE_USE_AFTER_FREE) == before + 1);
}

static void test_strdup_strndup(void)
{
    char *a = strdup("sentinel");
    char *b = strndup("sentinel", 4);
    CHECK(strcmp(a, "sentinel") == 0);
    CHECK(strcmp(b, "sent") == 0);
    free(a);
    free(b);
    size_t before = ISSUES(SP_ISSUE_MISUSE);
    CHECK(strdup(NULL) == NULL);
    CHECK(ISSUES(SP_ISSUE_MISUSE) == before + 1);
}

static void test_fault_injection(void)
{
    sp_guard_fail_after(1);                  /* one succeeds, the next fails */
    void *ok = malloc(8);
    void *fails = malloc(8);
    void *ok_again = malloc(8);
    CHECK(ok != NULL && fails == NULL && ok_again != NULL);
    free(ok);
    free(ok_again);
}

static void test_many_blocks(void)
{
    enum { N = 20000 };
    static void *blocks[N];
    for (int i = 0; i < N; ++i) blocks[i] = malloc((size_t)(i % 64) + 1);
    for (int i = 0; i < N; ++i) CHECK(sp_kind_of(blocks[i]) == SP_RAW);
    for (int i = N - 1; i >= 0; --i) free(blocks[i]);
    size_t before = sp_guard_stats().issues;
    for (int i = 0; i < 100; ++i) {
        void *p = malloc(32);
        free(p);
    }
    CHECK(sp_guard_stats().issues == before);
}

int main(void)
{
    sp_guard_set_mode(SP_MODE_SILENT);
    printf("test_guard\n");
    RUN(test_malloc_free_counts);
    RUN(test_free_null_is_fine);
    RUN(test_double_free_blocked);
    RUN(test_invalid_free_of_stack);
    RUN(test_interior_free_blocked);
    RUN(test_overflow_detected_at_free);
    RUN(test_overflow_detected_by_check);
    RUN(test_use_after_free_write);
    RUN(test_calloc_zeroes_and_overflow);
    RUN(test_realloc_keeps_data_and_guards_old_pointer);
    RUN(test_realloc_edge_cases);
    RUN(test_strdup_strndup);
    RUN(test_fault_injection);
    RUN(test_many_blocks);
    printf("  %d passed\n", check_passed);
    return 0;
}
