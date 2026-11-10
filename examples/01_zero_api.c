/*
 * 01_zero_api.c - ordinary C, no new function names.
 *
 * This is plain malloc / strdup / free code with five classic bugs in it.
 * The only change from normal C is the #include on line 13.  Without it,
 * this program crashes or silently corrupts memory; with it, every bug is
 * explained with the line that caused it and the program finishes cleanly.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "smartptr.h"

typedef struct {
    char *name;
    int roll;
} Student;

static Student *student_new(const char *name, int roll)
{
    Student *s = malloc(sizeof *s);
    s->name = strdup(name);
    s->roll = roll;
    return s;
}

static void student_free(Student *s)
{
    free(s->name);
    free(s);
}

int main(void)
{
    printf("01: plain C with smartptr.h included\n");

    /* Bug 1: double free. Two variables hold one student; both free it. */
    Student *ravi = student_new("Ravi", 101);
    Student *alias = ravi;
    student_free(ravi);
    free(alias);                         /* second free of the same block */

    /* Bug 2: buffer overflow. 5 bytes for "Hello" forgets the '\0'. */
    char *greeting = malloc(5);
    strcpy(greeting, "Hello");           /* writes 6 bytes into 5 */
    free(greeting);

    /* Bug 3: freeing a pointer that was moved forward. */
    char *text = strdup("smart pointers");
    char *word = text + 6;
    free(word);                          /* not the start of the block */
    free(text);

    /* Bug 4: use after free, a write through a stale pointer. */
    int *scores = calloc(4, sizeof *scores);
    int *stale = scores;
    free(scores);
    stale[2] = 99;                       /* the memory is no longer ours */

    /* Bug 5: a leak inside a loop. */
    for (int i = 0; i < 3; ++i) {
        char *line = malloc(32);
        snprintf(line, 32, "line %d", i);
    }                                    /* never freed */

    sp_guard_check();                    /* optional: audit everything now */
    printf("01: program reached the end without crashing\n");
    return 0;
}
