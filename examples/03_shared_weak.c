/*
 * 03_shared_weak.c - shared ownership, weak observers, and cycle detection.
 */
#include <stdio.h>

#include "smartptr.h"

typedef struct {
    char title[32];
} Course;

static void course_dtor(void *object)
{
    printf("  [destructor] course \"%s\" destroyed\n", ((Course *)object)->title);
}

/* Two nodes that own each other: the classic shared-pointer leak. */
typedef struct Node {
    struct Node *other;
    int value;
} Node;

int main(void)
{
    printf("03: shared + weak\n");

    Course *course = sp_make_shared_d(Course, course_dtor);
    snprintf(course->title, sizeof course->title, "Data Structures");

    /* a weak observer: watches without keeping the course alive */
    sp_weak watcher = sp_weak_from(course);

    Course *ananya = sp_share(course);
    Course *bharat = sp_share(course);
    printf("  owners: %zu, weak observers: %zu\n", sp_use_count(course), sp_weak_count(course));

    sp_drop(&course);
    sp_drop(&ananya);
    printf("  after two drops: owners %zu, still alive: %s\n",
           sp_use_count(bharat), sp_weak_expired(&watcher) ? "no" : "yes");

    /* lock: turn the weak observer into a real owner for a moment */
    Course *temp = sp_weak_lock(&watcher);
    printf("  locked through weak: \"%s\", owners %zu\n", temp->title, sp_use_count(temp));
    sp_drop(&temp);

    sp_drop(&bharat);                    /* last owner: destructor runs */
    printf("  weak observer now sees: %s\n", sp_weak_expired(&watcher) ? "expired" : "alive");
    printf("  sp_weak_lock after expiry gives: %s\n", sp_weak_lock(&watcher) ? "pointer" : "NULL");
    sp_weak_drop(&watcher);

    /* free() on a shared object only drops that one reference */
    Course *shared = sp_make_shared(Course);
    Course *other = sp_share(shared);
    free(shared);                        /* reported, but safe */
    printf("  after free(): other owner still sees %zu owner(s)\n", sp_use_count(other));
    sp_drop(&other);

    /* a reference cycle: a owns b, b owns a; dropping both leaks them */
    Node *a = sp_make_shared(Node);
    Node *b = sp_make_shared(Node);
    a->other = sp_share(b);
    b->other = sp_share(a);
    sp_drop(&a);
    sp_drop(&b);                         /* each still has 1 owner: the other node */
    printf("  cycle created; Sentinel will find it at exit\n");
    return 0;
}
