/* check.h - a tiny test harness: CHECK(cond) and RUN(test). */
#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>
#include <stdlib.h>

static int check_passed;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                             \
        }                                                                        \
    } while (0)

#define RUN(test)                    \
    do {                             \
        test();                      \
        check_passed++;              \
        printf("  ok  %s\n", #test); \
    } while (0)

/* issues of one kind reported since the snapshot */
#define ISSUES(kind) (sp_guard_issues(kind))

#endif
