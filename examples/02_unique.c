/*
 * 02_unique.c - unique ownership: one owner, explicit hand-over.
 *
 * Smart pointers are ordinary pointers here (Student *), so -> works as
 * usual.  sp_move hands ownership over and clears the old variable; sp_drop
 * releases it and clears the variable; the destructor frees what is inside.
 */
#include <stdio.h>

#include "smartptr.h"

typedef struct {
    char *name;
    int roll;
} Student;

static int destroyed = 0;

static void student_dtor(void *object)
{
    Student *s = object;
    printf("  [destructor] %s (roll %d)\n", s->name, s->roll);
    free(s->name);
    destroyed++;
}

static Student *enroll(const char *name, int roll)
{
    Student *s = sp_make_unique_d(Student, student_dtor);   /* zeroed, owned */
    s->name = strdup(name);
    s->roll = roll;
    return s;
}

int main(void)
{
    printf("02: unique pointer\n");

    Student *admission = enroll("Ravi", 101);
    sp_name(admission, "Ravi's record");

    /* hand over: admission becomes NULL, registrar owns the object */
    Student *registrar = sp_move(&admission);
    printf("  after move: admission=%s, registrar=%s, owners=%zu\n",
           admission ? "owns" : "NULL", registrar->name, sp_use_count(registrar));

    /* a unique object cannot be shared: explained, not crashed */
    Student *copy = sp_share(registrar);
    printf("  sp_share on a unique object returned %s\n", copy ? "a pointer" : "NULL");

    sp_drop(&registrar);                 /* destructor runs, registrar = NULL */
    sp_drop(&registrar);                 /* harmless: already NULL */
    printf("  destructor ran %d time(s)\n", destroyed);

    /* Using an old copy after the owner dropped it is caught. */
    Student *old = enroll("Asha", 102);
    Student *stale = old;                /* plain '=' copy, not an owner */
    sp_drop(&old);
    sp_drop(&stale);                     /* object already destroyed */

    /* Simulated out-of-memory: the 1st next allocation fails. */
    sp_guard_fail_after(0);
    Student *none = sp_make_unique(Student);
    printf("  allocation under simulated OOM returned %s\n", none ? "memory" : "NULL");
    return 0;
}
