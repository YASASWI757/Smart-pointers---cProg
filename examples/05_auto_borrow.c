/*
 * 05_auto_borrow.c - scope cleanup (RAII for C) and safe borrowing.
 *
 * sp_auto frees a variable when its scope ends, on every path out,
 * including early returns.  sp_borrow marks an object as "in use": if its
 * owner releases it meanwhile, destruction is postponed until the borrower
 * is done, so the borrower never holds a dangling pointer.
 */
#include <stdio.h>

#include "smartptr.h"

typedef struct {
    char text[64];
} Buffer;

static void buffer_dtor(void *object)
{
    printf("  [destructor] buffer \"%s\" released\n", ((Buffer *)object)->text);
}

static int parse(const char *input)
{
    sp_auto char *copy = strdup(input);                 /* freed on every return */
    sp_auto Buffer *work = sp_make_unique_d(Buffer, buffer_dtor);
    snprintf(work->text, sizeof work->text, "parsing %s", copy);

    if (copy[0] == '\0') {
        printf("  empty input: early return, nothing leaks\n");
        return -1;
    }
    printf("  %s\n", work->text);
    return (int)strlen(copy);
}

static Buffer *global_owner;

static void owner_gives_up(void)
{
    sp_drop(&global_owner);   /* the object is borrowed right now */
}

int main(void)
{
    printf("05: sp_auto and borrowing\n");
    parse("hello");
    parse("");

    global_owner = sp_make_unique_d(Buffer, buffer_dtor);
    snprintf(global_owner->text, sizeof global_owner->text, "report.txt");

    Buffer *view = sp_borrow(global_owner);
    owner_gives_up();                       /* owner drops it during the borrow */
    printf("  borrower still reads safely: \"%s\"\n", view->text);
    sp_unborrow(view);                      /* destruction happens here */
    printf("  done\n");
    return 0;
}
