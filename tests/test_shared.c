/* test_shared.c - reference counting. */
#include "smartptr.h"
#include "check.h"

typedef struct {
    int value;
} Course;

static int dtor_calls;
static void course_dtor(void *object) { (void)object; dtor_calls++; }

static void test_share_and_drop_count(void)
{
    dtor_calls = 0;
    Course *c = sp_make_shared_d(Course, course_dtor);
    CHECK(sp_kind_of(c) == SP_SHARED && sp_use_count(c) == 1);
    Course *a = sp_share(c);
    Course *b = sp_share(c);
    CHECK(a == c && b == c && sp_use_count(c) == 3);
    sp_drop(&c);
    CHECK(c == NULL && sp_use_count(a) == 2 && dtor_calls == 0);
    sp_drop(&a);
    sp_drop(&b);
    CHECK(dtor_calls == 1);
}

static void test_move_keeps_count(void)
{
    Course *c = sp_make_shared(Course);
    Course *d = sp_move(&c);
    CHECK(c == NULL && sp_use_count(d) == 1);
    sp_drop(&d);
}

static void test_free_drops_only_one_reference(void)
{
    dtor_calls = 0;
    size_t before = ISSUES(SP_ISSUE_OWNERSHIP);
    Course *c = sp_make_shared_d(Course, course_dtor);
    Course *other = sp_share(c);
    free(c);
    CHECK(ISSUES(SP_ISSUE_OWNERSHIP) == before + 1);
    CHECK(sp_use_count(other) == 1 && dtor_calls == 0);
    other->value = 5;                        /* still valid */
    sp_drop(&other);
    CHECK(dtor_calls == 1);
}

static void test_plain_copy_double_drop(void)
{
    size_t before = ISSUES(SP_ISSUE_DOUBLE_FREE);
    Course *c = sp_make_shared(Course);
    Course *copy = c;                        /* not sp_share */
    sp_drop(&c);
    sp_drop(&copy);
    CHECK(ISSUES(SP_ISSUE_DOUBLE_FREE) == before + 1);
}

static void test_thousand_owners(void)
{
    enum { N = 1000 };
    static Course *owners[N];
    dtor_calls = 0;
    Course *c = sp_make_shared_d(Course, course_dtor);
    for (int i = 0; i < N; ++i) owners[i] = sp_share(c);
    CHECK(sp_use_count(c) == N + 1);
    sp_drop(&c);
    for (int i = 0; i < N; ++i) sp_drop(&owners[i]);
    CHECK(dtor_calls == 1);
}

static void test_realloc_shared_blocked(void)
{
    size_t before = ISSUES(SP_ISSUE_MISUSE);
    Course *c = sp_make_shared(Course);
    CHECK(realloc(c, 64) == NULL);
    CHECK(ISSUES(SP_ISSUE_MISUSE) == before + 1);
    CHECK(sp_use_count(c) == 1);
    sp_drop(&c);
}

static void test_shared_arrays(void)
{
    double *v = sp_make_shared_array(double, 16);
    double *w = sp_share(v);
    w[15] = 2.5;
    CHECK(v[15] == 2.5);
    sp_drop(&v);
    sp_drop(&w);
}

int main(void)
{
    sp_guard_set_mode(SP_MODE_SILENT);
    printf("test_shared\n");
    RUN(test_share_and_drop_count);
    RUN(test_move_keeps_count);
    RUN(test_free_drops_only_one_reference);
    RUN(test_plain_copy_double_drop);
    RUN(test_thousand_owners);
    RUN(test_realloc_shared_blocked);
    RUN(test_shared_arrays);
    printf("  %d passed\n", check_passed);
    return 0;
}
