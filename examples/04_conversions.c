/*
 * 04_conversions.c - every ownership conversion, in one journey.
 *
 *   raw --adopt--> unique --to_shared--> shared --weak_from--> weak
 *        weak --lock--> shared --to_unique--> unique --release--> raw --free
 */
#include <stdio.h>

#include "smartptr.h"

typedef struct {
    int id;
    double marks;
} Record;

static void show(const char *step, const void *p)
{
    printf("  %-34s kind=%-11s owners=%zu\n", step, sp_kind_name(sp_kind_of(p)), sp_use_count(p));
}

int main(void)
{
    printf("04: conversions\n");

    Record *raw = malloc(sizeof *raw);               /* ordinary malloc */
    raw->id = 7;
    raw->marks = 91.5;
    show("malloc", raw);

    Record *u = sp_adopt(raw, NULL);                 /* raw -> unique */
    show("sp_adopt          (raw -> unique)", u);

    Record *s = sp_to_shared(&u);                    /* unique -> shared, u = NULL */
    show("sp_to_shared   (unique -> shared)", s);

    sp_weak w = sp_weak_from(s);                     /* shared -> weak */
    printf("  %-34s observers=%zu\n", "sp_weak_from     (shared -> weak)", sp_weak_count(s));

    Record *s2 = sp_weak_lock(&w);                   /* weak -> shared */
    show("sp_weak_lock     (weak -> shared)", s2);

    Record *blocked = sp_to_unique(&s);              /* 2 owners: refused, explained */
    printf("  sp_to_unique with 2 owners returned %s\n", blocked ? "a pointer" : "NULL");

    sp_drop(&s2);
    Record *u2 = sp_to_unique(&s);                   /* shared (1 owner) -> unique */
    show("sp_to_unique   (shared -> unique)", u2);

    Record *back = sp_release(&u2);                  /* unique -> raw */
    show("sp_release        (unique -> raw)", back);
    printf("  data survived the whole journey: id=%d marks=%.1f\n", back->id, back->marks);

    free(back);                                      /* raw memory: normal free */
    printf("  weak observer after free: %s\n", sp_weak_expired(&w) ? "expired" : "alive");
    sp_weak_drop(&w);
    return 0;
}
