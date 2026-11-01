/*
 * bench.c - what does Sentinel cost?  Times 1,000,000 malloc/free pairs with
 * and without the guard, and 1,000,000 shared share/drop pairs.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <time.h>

#define SP_NO_INTERCEPT   /* call the guard explicitly so both versions sit in one file */
#include "smartptr.h"

enum { N = 1000000 };

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

int main(void)
{
    sp_guard_set_mode(SP_MODE_SUMMARY);
    double t0 = now();
    for (int i = 0; i < N; ++i) {
        void *p = malloc(32);
        *(volatile char *)p = 1;
        free(p);
    }
    double plain = now() - t0;

    t0 = now();
    for (int i = 0; i < N; ++i) {
        void *p = sp__malloc(32, SP_HERE);
        *(volatile char *)p = 1;
        sp__free(p, SP_HERE, "p");
    }
    double guarded = now() - t0;

    int *shared = sp_make_shared(int);
    t0 = now();
    for (int i = 0; i < N; ++i) {
        int *extra = sp_share(shared);
        sp_drop(&extra);
    }
    double sharing = now() - t0;
    sp_drop(&shared);

    printf("1,000,000 operations each:\n");
    printf("  plain malloc/free       %7.1f ns per pair\n", plain / N * 1e9);
    printf("  guarded malloc/free     %7.1f ns per pair  (%.1fx)\n", guarded / N * 1e9, guarded / plain);
    printf("  shared share/drop       %7.1f ns per pair\n", sharing / N * 1e9);
    return 0;
}
