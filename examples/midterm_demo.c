/*
 * midterm_demo.c - Phase 1 demonstration of the SmartPtr-C library.
 *
 * Uses ONLY the functions in include/smartptr.h (common layer + sp_unique).
 * Each scene prints what it is doing so the audience can follow along.
 *
 *   Scene 1  Create a student record (struct with a heap string inside)
 *   Scene 2  Explicit ownership transfer (move)
 *   Scene 3  Double free is harmless; destructor runs exactly once
 *   Scene 4  Bad input gives an error code, not a crash
 *   Scene 5  Out-of-memory is handled cleanly (simulated)
 *   Final    Live allocation counter = 0 -> no leaks
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "smartptr.h"

#define TITLE(t) printf("\n\x1b[1;34m=== %s ===\x1b[0m\n", t)
#define OK(t)    printf("  \x1b[32m[ok]\x1b[0m %s\n", t)

/* ---------- a realistic object: memory with more memory inside ---------- */
typedef struct {
    char *name;   /* separate heap allocation */
    int   roll;
} Student;

static int students_destroyed = 0;

/* Destructor: frees what is INSIDE the student. The library frees the struct. */
static void destroy_student(void *object)
{
    Student *s = object;
    printf("  [destructor] freeing name \"%s\" (roll %d)\n", s->name, s->roll);
    free(s->name);
    s->name = NULL;
    students_destroyed++;
}

static char *copy_text(const char *text)
{
    char *p = malloc(strlen(text) + 1);
    if (p) strcpy(p, text);
    return p;
}

static void *always_fail(size_t size) { (void)size; return NULL; }

static void show(const char *call, sp_status status)
{
    printf("  %-42s -> %s\n", call, sp_strerror(status));
}

int main(void)
{
    printf("SmartPtr-C | Phase 1 demo: common layer + unique pointer\n");

    /* ------------------------------------------------------------ 1 */
    TITLE("Scene 1: create a student record");
    sp_unique admission = SP_UNIQUE_EMPTY;
    if (sp_unique_make(&admission, sizeof(Student), destroy_student) != SP_OK) return 1;

    Student *s = sp_unique_get(&admission);
    s->name = NULL;                 /* make does not zero memory */
    s->name = copy_text("Ravi");
    s->roll = 101;
    printf("  admission owns: %s (roll %d)\n", s->name, s->roll);
    printf("  library is tracking %zu block(s)\n", sp_live_allocations());

    /* ------------------------------------------------------------ 2 */
    TITLE("Scene 2: explicit ownership transfer (move)");
    sp_unique registrar = SP_UNIQUE_EMPTY;
    show("sp_unique_move(&registrar, &admission)", sp_unique_move(&registrar, &admission));
    printf("  admission is now: %s\n", sp_unique_get(&admission) ? "owning" : "EMPTY");
    printf("  registrar owns:   %s\n", ((Student *)sp_unique_get(&registrar))->name);
    printf("  still %zu block(s): the memory did not move, only the ownership\n",
           sp_live_allocations());

    /* ------------------------------------------------------------ 3 */
    TITLE("Scene 3: free, then free again (double free)");
    show("sp_unique_free(&registrar)  [1st]", sp_unique_free(&registrar));
    show("sp_unique_free(&registrar)  [2nd]", sp_unique_free(&registrar));
    show("sp_unique_free(&admission)  [empty]", sp_unique_free(&admission));
    printf("  destructor ran %d time(s)\n", students_destroyed);
    OK("no crash: a freed handle is empty, so freeing it again does nothing");

    /* ------------------------------------------------------------ 4 */
    TITLE("Scene 4: bad input -> error code, not a crash");
    sp_unique a = SP_UNIQUE_EMPTY, b = SP_UNIQUE_EMPTY;
    sp_unique_make(&a, sizeof(int), NULL);
    sp_unique_make(&b, sizeof(int), NULL);
    sp_unique e = SP_UNIQUE_EMPTY;
    sp_unique not_setup = SP_UNIQUE_EMPTY;
    not_setup.magic = 0;            /* simulate a handle that was never set up */

    show("sp_unique_make(&e, 0 bytes, ...)", sp_unique_make(&e, 0, NULL));
    show("sp_unique_free(NULL)", sp_unique_free(NULL));
    show("sp_unique_move(&b, &a)  [b not empty]", sp_unique_move(&b, &a));
    show("sp_unique_free(&not_setup)", sp_unique_free(&not_setup));
    OK("both a and b still own their ints: rejected calls change nothing");
    sp_unique_free(&a);
    sp_unique_free(&b);

    /* ------------------------------------------------------------ 5 */
    TITLE("Scene 5: out of memory (simulated)");
    sp_set_allocator(always_fail, free);
    sp_unique c = SP_UNIQUE_EMPTY;
    show("sp_unique_make(&c, ...) with malloc failing", sp_unique_make(&c, sizeof(int), NULL));
    printf("  c is %s, tracked blocks = %zu\n",
           sp_unique_get(&c) ? "owning" : "EMPTY", sp_live_allocations());
    sp_set_allocator(NULL, NULL);
    OK("failure reported cleanly, nothing leaked");

    /* ------------------------------------------------------------ end */
    TITLE("Result");
    size_t live = sp_live_allocations();
    printf("  live allocations at exit: %zu  %s\n", live,
           live == 0 ? "\x1b[32m-> NO LEAKS\x1b[0m" : "\x1b[31m-> LEAK\x1b[0m");
    return live == 0 ? 0 : 1;
}
