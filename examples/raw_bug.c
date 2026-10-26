/*
 * raw_bug.c - the problem WITHOUT our library.
 * Plain malloc/free: two names for one block, both freed.
 * Run under AddressSanitizer to watch it get caught.
 */
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    int *p = malloc(sizeof *p);
    int *q = p;          /* a second "owner" that nobody tracks */
    *p = 42;
    printf("value = %d\n", *q);
    free(p);
    free(q);             /* double free: undefined behaviour */
    printf("if you see this, the bug went unnoticed\n");
    return 0;
}
