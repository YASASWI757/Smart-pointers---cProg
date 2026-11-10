/* test_threads.c - shared ownership and plain malloc from 8 threads at once. */
#include <pthread.h>

#include "smartptr.h"
#include "check.h"

typedef struct {
    int value;
} Shared;

static int dtor_calls;
static void shared_dtor(void *object) { (void)object; dtor_calls++; }

static sp_weak g_weak;

static void *worker(void *arg)
{
    Shared *mine = sp_share(arg);
    for (int i = 0; i < 5000; ++i) {
        Shared *a = sp_share(mine);
        Shared *b = sp_weak_lock(&g_weak);
        sp_drop(&a);
        if (b) sp_drop(&b);
        char *scratch = malloc(24);
        free(scratch);
    }
    sp_drop(&mine);
    return NULL;
}

static void test_concurrent_sharing(void)
{
    dtor_calls = 0;
    size_t issues = sp_guard_stats().issues;
    Shared *s = sp_make_shared_d(Shared, shared_dtor);
    g_weak = sp_weak_from(s);
    pthread_t t[8];
    for (int i = 0; i < 8; ++i) CHECK(pthread_create(&t[i], NULL, worker, s) == 0);
    for (int i = 0; i < 8; ++i) pthread_join(t[i], NULL);
    CHECK(sp_use_count(s) == 1);
    sp_drop(&s);
    CHECK(dtor_calls == 1 && sp_weak_expired(&g_weak));
    sp_weak_drop(&g_weak);
    CHECK(sp_guard_stats().issues == issues);
}

int main(void)
{
    sp_guard_set_mode(SP_MODE_SILENT);
    printf("test_threads\n");
    RUN(test_concurrent_sharing);
    printf("  %d passed\n", check_passed);
    return 0;
}
