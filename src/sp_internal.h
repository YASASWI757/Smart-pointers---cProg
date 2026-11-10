/*
 * sp_internal.h - private to the library.  Never included by user code,
 * so malloc/free in src/ are the real C library functions.
 */
#ifndef SP_INTERNAL_H
#define SP_INTERNAL_H

#include <stdbool.h>
#include <stdio.h>

#include "sp_guard.h"
#include "sp_ptr.h"

#define SP_SENTINEL_BYTES 16    /* guard zone written after every block   */
#define SP_POISON         0xDD  /* pattern written over freed memory      */
#define SP_HISTORY        12    /* events remembered per block            */
#define SP_LABEL_MAX      32

typedef enum {
    EV_ALLOC, EV_MAKE, EV_FREE, EV_MOVE, EV_SHARE, EV_DROP,
    EV_TO_SHARED, EV_TO_UNIQUE, EV_RELEASE, EV_ADOPT,
    EV_WEAK, EV_LOCK, EV_BORROW, EV_UNBORROW, EV_REALLOC,
    EV_BLOCKED, EV_DEFERRED, EV_AUTO
} sp_event_kind;

typedef struct {
    sp_event_kind kind;
    sp_site at;
    const char *expr;   /* source text of the argument, e.g. "&admission" */
    size_t aux;         /* owner count after the event, or an old block id */
} sp_event;

typedef enum {
    ST_LIVE,        /* in use                                          */
    ST_DESTROYING,  /* destructor running                              */
    ST_FREED,       /* released, memory poisoned, held in quarantine   */
    ST_EXPIRED      /* memory gone, record kept only for weak handles  */
} sp_state;

/* One record per block: the "control block" of every pointer. */
typedef struct sp_record {
    void *ptr;
    size_t size;
    uint64_t id;
    const char *type;
    char label[SP_LABEL_MAX];

    sp_kind kind;
    sp_state state;
    size_t strong;      /* owners (unique: 1, shared: n)  */
    size_t weak;        /* sp_weak handles alive           */
    size_t borrows;     /* active sp_borrow() calls        */
    sp_dtor dtor;

    bool foreign;           /* adopted memory of unknown size  */
    bool pending_destroy;   /* destroy requested while borrowed */
    bool overflow_reported;
    bool uaf_reported;

    sp_site alloc_at;
    sp_site free_at;
    sp_site borrow_at;

    sp_event history[SP_HISTORY];
    size_t nevents;

    struct sp_record *hnext;            /* hash-table chain           */
    struct sp_record *prev, *next;      /* live / quarantine / expired list */
} sp_record;

/* ---- sp_guard.c ---- */
void       sp__lock(void);
void       sp__unlock(void);
void       sp__ensure_init(void);
sp_record *sp__find(const void *p);
sp_record *sp__find_containing(const void *p, size_t *offset);
sp_record *sp__new_block(size_t size, bool zero, const char *type, sp_site at);
sp_record *sp__register_foreign(void *p, sp_site at);
void       sp__event(sp_record *r, sp_event_kind kind, sp_site at, const char *expr, size_t aux);
void       sp__destroy(sp_record *r, sp_site at, const char *op, const char *expr);
void       sp__record_unref_weak(sp_record *r);
sp_mode    sp__mode(void);
size_t     sp__count_issue(sp_issue kind);
FILE      *sp__log_file(void);

/* ---- sp_report.c ---- */
void        sp__report(sp_issue kind, sp_site at, const char *call, const sp_record *r,
                       const char *what, const char *detail, const char *fix, const char *action);
void        sp__print_block(FILE *out, const sp_record *r);
const char *sp__event_name(sp_event_kind kind);
const char *sp__var_name(const char *expr, char *buf, size_t n);
void        sp__fmt_bytes(size_t n, char *buf, size_t len);
bool        sp__color(void);
const char *sp__site_str(sp_site at, char *buf, size_t n);

#endif
