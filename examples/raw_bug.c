/*
 * raw_bug.c - the problem WITHOUT the library: plain C double free.
 * Compare with 01_zero_api.c, which has the same bug plus four more.
 */
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    int *p = malloc(sizeof *p);
    int *q = p;          /* a second "owner" nobody tracks */
    *p = 42;
    printf("value = %d\n", *q);
    free(p);
    free(q);             /* double free: undefined behaviour */
    printf("if you see this, the bug went unnoticed\n");
    return 0;
}
