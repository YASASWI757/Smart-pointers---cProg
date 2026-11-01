/*
 * sp_ptr.h - unique, shared and weak ownership for plain C pointers.
 *
 * A smart pointer here is just an ordinary pointer (Student *, char *, ...).
 * Ownership lives in Sentinel's side table, not in a wrapper struct, so
 * you dereference it with -> and [] as usual and pass it to any function.
 * The macros below capture the call site and the variable name for reports.
 */
#ifndef SP_PTR_H
#define SP_PTR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sp_guard.h"

typedef void (*sp_dtor)(void *object);

typedef enum {
    SP_NONE = 0,  /* not a live tracked block (NULL, freed, or foreign) */
    SP_RAW,       /* plain malloc'd memory: you free() it               */
    SP_UNIQUE,    /* exactly one owner                                  */
    SP_SHARED     /* reference counted                                  */
} sp_kind;

/* A weak observer.  Copy it with sp_weak_copy(), not with '='. */
typedef struct {
    void *rec_;
    uint64_t id_;
} sp_weak;

#define SP_WEAK_EMPTY ((sp_weak){ NULL, 0 })

/* ---------------------------------------------------------------- create */
#define sp_make_unique(T)              ((T *)sp__make(sizeof(T), 1, SP_UNIQUE, NULL, #T, SP_HERE))
#define sp_make_unique_d(T, dtor)      ((T *)sp__make(sizeof(T), 1, SP_UNIQUE, (dtor), #T, SP_HERE))
#define sp_make_unique_array(T, n)     ((T *)sp__make(sizeof(T), (n), SP_UNIQUE, NULL, #T "[]", SP_HERE))
#define sp_make_shared(T)              ((T *)sp__make(sizeof(T), 1, SP_SHARED, NULL, #T, SP_HERE))
#define sp_make_shared_d(T, dtor)      ((T *)sp__make(sizeof(T), 1, SP_SHARED, (dtor), #T, SP_HERE))
#define sp_make_shared_array(T, n)     ((T *)sp__make(sizeof(T), (n), SP_SHARED, NULL, #T "[]", SP_HERE))

/* ---------------------------------------------------------------- ownership */
#define sp_move(pp)            sp__move((void **)(pp), SP_HERE, #pp)       /* take it; *pp becomes NULL      */
#define sp_share(p)            sp__share((p), SP_HERE, #p)                 /* one more shared owner          */
#define sp_drop(pp)            sp__drop((void **)(pp), SP_HERE, #pp)       /* give up ownership; *pp = NULL  */

/* ---------------------------------------------------------------- conversions */
#define sp_to_shared(pp)       sp__to_shared((void **)(pp), SP_HERE, #pp)  /* unique -> shared               */
#define sp_to_unique(pp)       sp__to_unique((void **)(pp), SP_HERE, #pp)  /* shared (1 owner) -> unique     */
#define sp_release(pp)         sp__release((void **)(pp), SP_HERE, #pp)    /* unique -> raw (you free it)    */
#define sp_adopt(p, dtor)      sp__adopt((p), SP_UNIQUE, (dtor), SP_HERE, #p)  /* raw -> unique              */
#define sp_adopt_shared(p, dtor) sp__adopt((p), SP_SHARED, (dtor), SP_HERE, #p) /* raw -> shared             */
#define sp_weak_from(p)        sp__weak((p), SP_HERE, #p)                  /* unique/shared -> weak          */
#define sp_weak_lock(w)        sp__weak_lock((w), SP_HERE, #w)             /* weak -> new shared owner/NULL  */
#define sp_weak_copy(w)        sp__weak_copy((w), SP_HERE)
#define sp_weak_drop(w)        sp__weak_drop((w), SP_HERE)

/* ---------------------------------------------------------------- borrowing */
#define sp_borrow(p)           sp__borrow((p), SP_HERE, #p)    /* "I am using this, do not destroy it" */
#define sp_unborrow(p)         sp__unborrow((p), SP_HERE, #p)

/* ---------------------------------------------------------------- scope cleanup (RAII) */
#define sp_auto       __attribute__((cleanup(sp__auto_cleanup)))
#define sp_auto_weak  __attribute__((cleanup(sp__auto_weak_cleanup)))

/* ---------------------------------------------------------------- queries */
#define sp_name(p, label)      sp__name((p), (label))  /* human name shown in reports */
void       *sp_weak_peek(const sp_weak *w);            /* pointer if alive, no ownership */
bool        sp_weak_expired(const sp_weak *w);
size_t      sp_use_count(const void *p);
size_t      sp_weak_count(const void *p);
sp_kind     sp_kind_of(const void *p);
uint64_t    sp_id(const void *p);
const char *sp_kind_name(sp_kind kind);

/* ---------------------------------------------------------------- implementation entry points */
void   *sp__make(size_t size, size_t count, sp_kind kind, sp_dtor dtor, const char *type, sp_site at);
void   *sp__move(void **slot, sp_site at, const char *expr);
void   *sp__share(void *p, sp_site at, const char *expr);
void    sp__drop(void **slot, sp_site at, const char *expr);
void   *sp__to_shared(void **slot, sp_site at, const char *expr);
void   *sp__to_unique(void **slot, sp_site at, const char *expr);
void   *sp__release(void **slot, sp_site at, const char *expr);
void   *sp__adopt(void *p, sp_kind kind, sp_dtor dtor, sp_site at, const char *expr);
sp_weak sp__weak(void *p, sp_site at, const char *expr);
sp_weak sp__weak_copy(const sp_weak *w, sp_site at);
void   *sp__weak_lock(const sp_weak *w, sp_site at, const char *expr);
void    sp__weak_drop(sp_weak *w, sp_site at);
void   *sp__borrow(void *p, sp_site at, const char *expr);
void    sp__unborrow(void *p, sp_site at, const char *expr);
void   *sp__name(void *p, const char *label);
void    sp__auto_cleanup(void *slot);
void    sp__auto_weak_cleanup(void *slot);

#endif
