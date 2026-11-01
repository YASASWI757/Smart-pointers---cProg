/* test_convert.c - every conversion and every refusal. */
#include "smartptr.h"
#include "check.h"

typedef struct {
    int id;
} Item;

static int dtor_calls;
static void item_dtor(void *object) { (void)object; dtor_calls++; }

static void test_adopt_raw_to_unique(void)
{
    dtor_calls = 0;
    Item *raw = malloc(sizeof *raw);
    Item *u = sp_adopt(raw, item_dtor);
    CHECK(u == raw && sp_kind_of(u) == SP_UNIQUE);
    sp_drop(&u);
    CHECK(dtor_calls == 1);
}

static void test_adopt_raw_to_shared(void)
{
    Item *s = sp_adopt_shared(malloc(sizeof(Item)), NULL);
    CHECK(sp_kind_of(s) == SP_SHARED && sp_use_count(s) == 1);
    Item *t = sp_share(s);
    sp_drop(&s);
    sp_drop(&t);
}

static void test_adopt_owned_refused(void)
{
    size_t before = ISSUES(SP_ISSUE_MISUSE);
    Item *u = sp_make_unique(Item);
    CHECK(sp_adopt(u, NULL) == NULL);
    CHECK(ISSUES(SP_ISSUE_MISUSE) == before + 1);
    sp_drop(&u);
}

static void test_unique_to_shared(void)
{
    Item *u = sp_make_unique(Item);
    Item *s = sp_to_shared(&u);
    CHECK(u == NULL && sp_kind_of(s) == SP_SHARED && sp_use_count(s) == 1);
    Item *t = sp_share(s);
    CHECK(sp_use_count(s) == 2);
    sp_drop(&t);
    sp_drop(&s);
}

static void test_shared_to_unique_only_when_alone(void)
{
    size_t before = ISSUES(SP_ISSUE_MISUSE);
    Item *s = sp_make_shared(Item);
    Item *t = sp_share(s);
    CHECK(sp_to_unique(&s) == NULL && s != NULL);    /* refused, variable unchanged */
    CHECK(ISSUES(SP_ISSUE_MISUSE) == before + 1);
    sp_drop(&t);
    Item *u = sp_to_unique(&s);
    CHECK(u != NULL && s == NULL && sp_kind_of(u) == SP_UNIQUE);
    sp_drop(&u);
}

static void test_release_unique_to_raw(void)
{
    dtor_calls = 0;
    Item *u = sp_make_unique_d(Item, item_dtor);
    u->id = 9;
    Item *raw = sp_release(&u);
    CHECK(u == NULL && sp_kind_of(raw) == SP_RAW && raw->id == 9);
    free(raw);
    CHECK(dtor_calls == 0);                 /* released memory has no destructor */
}

static void test_release_shared_with_owners_refused(void)
{
    size_t before = ISSUES(SP_ISSUE_MISUSE);
    Item *s = sp_make_shared(Item);
    Item *t = sp_share(s);
    CHECK(sp_release(&s) == NULL && s != NULL);
    CHECK(ISSUES(SP_ISSUE_MISUSE) == before + 1);
    sp_drop(&s);
    sp_drop(&t);
}

static void test_raw_conversions_refused(void)
{
    size_t before = ISSUES(SP_ISSUE_MISUSE);
    Item *raw = malloc(sizeof *raw);
    Item *p = raw;
    CHECK(sp_to_shared(&p) == NULL);
    CHECK(sp_to_unique(&p) == NULL);
    CHECK(sp_share(raw) == NULL);
    CHECK(ISSUES(SP_ISSUE_MISUSE) == before + 3);
    free(raw);
}

static void test_full_round_trip(void)
{
    Item *raw = malloc(sizeof *raw);
    raw->id = 77;
    Item *u = sp_adopt(raw, NULL);
    Item *s = sp_to_shared(&u);
    sp_weak w = sp_weak_from(s);
    Item *s2 = sp_weak_lock(&w);
    sp_drop(&s2);
    Item *u2 = sp_to_unique(&s);
    Item *back = sp_release(&u2);
    CHECK(back->id == 77 && sp_kind_of(back) == SP_RAW);
    free(back);
    CHECK(sp_weak_expired(&w));
    sp_weak_drop(&w);
}

int main(void)
{
    sp_guard_set_mode(SP_MODE_SILENT);
    printf("test_convert\n");
    RUN(test_adopt_raw_to_unique);
    RUN(test_adopt_raw_to_shared);
    RUN(test_adopt_owned_refused);
    RUN(test_unique_to_shared);
    RUN(test_shared_to_unique_only_when_alone);
    RUN(test_release_unique_to_raw);
    RUN(test_release_shared_with_owners_refused);
    RUN(test_raw_conversions_refused);
    RUN(test_full_round_trip);
    printf("  %d passed\n", check_passed);
    return 0;
}
