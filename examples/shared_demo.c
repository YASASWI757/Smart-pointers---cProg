/*
 * shared_demo.c - one Course shared by several Students.
 * Shows the reference count going up and down, and the course being
 * destroyed only when the last student lets go.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "smartptr.h"

typedef struct {
    char *title;   /* separate heap allocation */
} Course;

static void destroy_course(void *object) {
    Course *course = object;
    printf("  [destructor] course \"%s\" destroyed\n", course->title);
    free(course->title);
    course->title = NULL;
}

int main(void) {
    printf("\x1b[1;34m=== Shared pointer demo ===\x1b[0m\n");

    sp_shared course = SP_SHARED_EMPTY;
    if (sp_shared_make(&course, sizeof(Course), destroy_course) != SP_OK) {
        return 1;
    }

    Course *c = sp_shared_get(&course);
    c->title = malloc(16);
    if (c->title == NULL) {
        sp_shared_free(&course);
        return 1;
    }
    strcpy(c->title, "Data Structures");

    printf("  course created            owners = %zu\n", sp_shared_use_count(&course));

    const char *names[] = { "Ananya", "Bharat", "Chitra" };
    sp_shared students[3];

    for (int i = 0; i < 3; ++i) {
        students[i] = SP_SHARED_EMPTY;
        sp_shared_copy(&students[i], &course);
        printf("  %-7s enrolled           owners = %zu\n",
               names[i], sp_shared_use_count(&course));
    }

    sp_shared_free(&course);
    printf("  catalogue lets go         owners = %zu  (course still alive)\n",
           sp_shared_use_count(&students[0]));

    for (int i = 0; i < 3; ++i) {
        size_t left = sp_shared_use_count(&students[i]) - 1;
        printf("  %-7s drops out          owners = %zu\n", names[i], left);
        sp_shared_free(&students[i]);
    }

    size_t live = sp_live_allocations();
    printf("\n  live allocations at exit: %zu  %s\n", live,
           live == 0 ? "\x1b[32m-> NO LEAKS\x1b[0m" : "\x1b[31m-> LEAK\x1b[0m");
    return live == 0 ? 0 : 1;
}
