/*
 * 06_threads.c - one shared object used by 8 threads at once.
 *
 * Every thread repeatedly takes its own owner (sp_share / sp_weak_lock) and
 * drops it.  Sentinel's table is protected by a lock, so the owner count
 * stays exact and the object is destroyed exactly once.
 */
#include <pthread.h>
#include <stdio.h>

#include "smartptr.h"

typedef struct {
    int value;
} Config;

static int destroyed;

static void config_dtor(void *object)
{
    (void)object;
    destroyed++;
}

static sp_weak g_watch;

static void *worker(void *arg)
{
    Config *mine = sp_share(arg);
    for (int i = 0; i < 20000; ++i) {
        Config *tmp = sp_weak_lock(&g_watch);
        if (tmp) sp_drop(&tmp);
        char *scratch = malloc(16);           /* plain malloc is tracked too */
        free(scratch);
    }
    sp_drop(&mine);
    return NULL;
}

int main(void)
{
    printf("06: 8 threads sharing one object\n");
    Config *config = sp_make_shared_d(Config, config_dtor);
    g_watch = sp_weak_from(config);

    pthread_t threads[8];
    for (int i = 0; i < 8; ++i) pthread_create(&threads[i], NULL, worker, config);
    for (int i = 0; i < 8; ++i) pthread_join(threads[i], NULL);

    printf("  owners after all threads finished: %zu (expected 1)\n", sp_use_count(config));
    sp_drop(&config);
    printf("  destructor ran %d time(s)\n", destroyed);
    sp_weak_drop(&g_watch);

    sp_stats st = sp_guard_stats();
    printf("  %zu allocations, %zu frees tracked across threads\n", st.allocations, st.frees);
    return 0;
}
