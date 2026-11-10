/* test_unique.c - unique ownership. */
#include "smartptr.h"
#include "check.h"

typedef struct {
    char *name;
    int roll;
} Student;

static int dtor_calls;

static void student_dtor(void *object)
{
    Student *s = object;
    free(s->name);              /* goes through Sentinel while it is unlocked */
    dtor_calls++;
}

static void test_make_is_zeroed_and_unique(void)
{
    Student *s = sp_make_unique(Student);
    CHECK(s != NULL && s->name == NULL && s->roll == 0);
    CHECK(sp_kind_of(s) == SP_UNIQUE && sp_use_count(s) == 1);
    sp_drop(&s);
    CHECK(s == NULL);
}

static void test_move_clears_source(void)
{
    Student *a = sp_make_unique(Student);
    Student *b = sp_move(&a);
    CHECK(a == NULL && b != NULL && sp_use_count(b) == 1);
    Student *none = NULL;
    CHECK(sp_move(&none) == NULL);          /* moving nothing gives nothing */
    sp_drop(&b);
}

static void test_destructor_runs_once(void)
{
    dtor_calls = 0;
    Student *s = sp_make_unique_d(Student, student_dtor);
    s->name = strdup("Ravi");
    sp_drop(&s);
    sp_drop(&s);                             /* NULL now: nothing happens */
    CHECK(dtor_calls == 1);
}

static void test_free_on_unique_destroys(void)
{
    dtor_calls = 0;
    size_t issues = sp_guard_stats().issues;
    Student *s = sp_make_unique_d(Student, student_dtor);
    s->name = strdup("Asha");
    free(s);                                 /* the one owner releasing: allowed */
    CHECK(dtor_calls == 1);
    CHECK(sp_guard_stats().issues == issues);
}

static void test_stale_copy_drop_is_double_free(void)
{
    size_t before = ISSUES(SP_ISSUE_DOUBLE_FREE);
    Student *s = sp_make_unique(Student);
    Student *copy = s;
    sp_drop(&s);
    sp_drop(&copy);
    CHECK(copy == NULL);
    CHECK(ISSUES(SP_ISSUE_DOUBLE_FREE) == before + 1);
}

static void test_cannot_share_unique(void)
{
    size_t before = ISSUES(SP_ISSUE_MISUSE);
    Student *s = sp_make_unique(Student);
    CHECK(sp_share(s) == NULL);
    CHECK(ISSUES(SP_ISSUE_MISUSE) == before + 1);
    CHECK(sp_use_count(s) == 1);
    sp_drop(&s);
}

static void test_move_after_drop_is_reported(void)
{
    size_t before = ISSUES(SP_ISSUE_USE_AFTER_FREE);
    Student *s = sp_make_unique(Student);
    Student *stale = s;
    sp_drop(&s);
    CHECK(sp_move(&stale) == NULL);
    CHECK(ISSUES(SP_ISSUE_USE_AFTER_FREE) == before + 1);
}

static void test_arrays(void)
{
    int *a = sp_make_unique_array(int, 100);
    for (int i = 0; i < 100; ++i) CHECK(a[i] == 0);
    a[99] = 1;
    CHECK(sp_guard_check() == 0);
    sp_drop(&a);
    size_t before = ISSUES(SP_ISSUE_MISUSE);
    CHECK(sp_make_unique_array(int, 0) == NULL);
    CHECK(ISSUES(SP_ISSUE_MISUSE) == before + 1);
}

static void test_allocation_failure(void)
{
    sp_stats before = sp_guard_stats();
    sp_guard_fail_after(0);
    Student *s = sp_make_unique(Student);
    CHECK(s == NULL);
    CHECK(sp_guard_stats().live_blocks == before.live_blocks);
}

static void test_names_and_ids(void)
{
    Student *a = sp_make_unique(Student);
    Student *b = sp_make_unique(Student);
    CHECK(sp_name(a, "admission") == a);
    CHECK(sp_id(a) != 0 && sp_id(b) == sp_id(a) + 1);
    sp_drop(&a);
    sp_drop(&b);
}

static void test_repeated_cycles(void)
{
    sp_stats before = sp_guard_stats();
    for (int i = 0; i < 50000; ++i) {
        Student *s = sp_make_unique(Student);
        sp_drop(&s);
    }
    CHECK(sp_guard_stats().live_blocks == before.live_blocks);
    CHECK(sp_guard_stats().issues == before.issues);
}

int main(void)
{
    sp_guard_set_mode(SP_MODE_SILENT);
    printf("test_unique\n");
    RUN(test_make_is_zeroed_and_unique);
    RUN(test_move_clears_source);
    RUN(test_destructor_runs_once);
    RUN(test_free_on_unique_destroys);
    RUN(test_stale_copy_drop_is_double_free);
    RUN(test_cannot_share_unique);
    RUN(test_move_after_drop_is_reported);
    RUN(test_arrays);
    RUN(test_allocation_failure);
    RUN(test_names_and_ids);
    RUN(test_repeated_cycles);
    printf("  %d passed\n", check_passed);
    return 0;
}
