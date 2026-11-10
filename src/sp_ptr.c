/*
 * sp_ptr.c - ownership on top of Sentinel's block records.
 *
 * A record's kind says who owns the block (raw / unique / shared), strong
 * counts owners, weak counts observers.  Each operation validates the block,
 * enforces the ownership rule, logs an event, and on a violation explains
 * what went wrong instead of crashing.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>

#include "sp_internal.h"

#define CALL(buf, op, expr) snprintf((buf), sizeof (buf), "%s(%s)", (op), (expr) ? (expr) : "")

const char *sp_kind_name(sp_kind kind)
{
    switch (kind) {
    case SP_RAW:    return "raw pointer";
    case SP_UNIQUE: return "unique";
    case SP_SHARED: return "shared";
    default:        return "none";
    }
}

/* Return the live record for p, or report why it cannot be used. (lock held) */
static sp_record *live_block(const void *p, sp_site at, const char *call)
{
    sp_record *r = sp__find(p);
    if (r == NULL) {
        size_t off = 0;
        sp_record *inside = sp__find_containing(p, &off);
        char detail[160];
        if (inside) {
            snprintf(detail, sizeof detail, "the pointer is %zu byte(s) inside block #%llu", off,
                     (unsigned long long)inside->id);
        } else {
            snprintf(detail, sizeof detail, "address %p is not a block SmartPtr-C knows about", p);
        }
        sp__report(SP_ISSUE_MISUSE, at, call, inside, "not a tracked heap block", detail,
                   "pass the pointer returned by malloc / sp_make_unique / sp_make_shared",
                   "blocked: nothing changed");
        return NULL;
    }
    if (r->state != ST_LIVE) {
        char detail[200], b[96];
        snprintf(detail, sizeof detail, "this object was already released at %s",
                 sp__site_str(r->free_at, b, sizeof b));
        sp__event(r, EV_BLOCKED, at, NULL, 0);
        sp__report(SP_ISSUE_USE_AFTER_FREE, at, call, r, "the object is already destroyed", detail,
                   "the variable still holds an old address. sp_drop() and sp_move() set it to NULL; "
                   "a plain '=' copy is not tracked. Use sp_weak to observe objects that may die",
                   "blocked: nothing changed");
        return NULL;
    }
    return r;
}

static void *slot_value(void **slot, sp_site at, const char *call)
{
    if (slot == NULL) {
        sp__report(SP_ISSUE_MISUSE, at, call, NULL, "NULL was passed instead of &variable", NULL,
                   "pass the address of your pointer variable, e.g. sp_drop(&p)", "blocked");
        return NULL;
    }
    return *slot;
}

/* ------------------------------------------------------------------ create */
void *sp__make(size_t size, size_t count, sp_kind kind, sp_dtor dtor, const char *type, sp_site at)
{
    sp__lock();
    sp__ensure_init();
    void *result = NULL;
    if (size == 0 || count == 0) {
        sp__report(SP_ISSUE_MISUSE, at, "sp_make", NULL, "cannot make an object of size 0",
                   "the element count or size is zero", "check the count passed to *_array", "returned NULL");
    } else if (count > SIZE_MAX / size) {
        sp__report(SP_ISSUE_ALLOC_FAIL, at, "sp_make", NULL, "allocation failed: size overflow",
                   "count x sizeof(type) does not fit in size_t", "check the element count", "returned NULL");
    } else {
        sp_record *r = sp__new_block(size * count, true, type, at);
        if (r) {
            r->kind = kind;
            r->strong = 1;
            r->dtor = dtor;
            sp__event(r, EV_MAKE, at, type, 1);
            result = r->ptr;
        }
    }
    sp__unlock();
    return result;
}

/* ------------------------------------------------------------------ ownership */
void *sp__move(void **slot, sp_site at, const char *expr)
{
    char call[128];
    CALL(call, "sp_move", expr);
    sp__lock();
    sp__ensure_init();
    void *p = slot_value(slot, at, call);
    void *result = NULL;
    if (p) {
        sp_record *r = live_block(p, at, call);
        if (r) {
            sp__event(r, EV_MOVE, at, expr, r->strong);
            *slot = NULL;
            result = p;
        }
    }
    sp__unlock();
    return result;
}

void *sp__share(void *p, sp_site at, const char *expr)
{
    if (p == NULL) return NULL;
    char call[128];
    CALL(call, "sp_share", expr);
    sp__lock();
    sp__ensure_init();
    void *result = NULL;
    sp_record *r = live_block(p, at, call);
    if (r && r->kind == SP_SHARED) {
        if (r->strong == SIZE_MAX) {
            sp__report(SP_ISSUE_MISUSE, at, call, r, "owner count would overflow", NULL,
                       "something is sharing in an endless loop", "blocked");
        } else {
            r->strong++;
            sp__event(r, EV_SHARE, at, expr, r->strong);
            result = p;
        }
    } else if (r) {
        const bool unique = r->kind == SP_UNIQUE;
        sp__report(SP_ISSUE_MISUSE, at, call, r,
                   unique ? "a unique object cannot have a second owner"
                          : "plain malloc memory has no owner count to share",
                   NULL,
                   unique ? "convert it first: p = sp_to_shared(&p); then sp_share(p) works"
                          : "give it an owner first: p = sp_adopt_shared(p, NULL);",
                   "blocked: returned NULL");
    }
    sp__unlock();
    return result;
}

void sp__drop(void **slot, sp_site at, const char *expr)
{
    char call[128];
    CALL(call, "sp_drop", expr);
    sp__lock();
    sp__ensure_init();
    void *p = slot_value(slot, at, call);
    if (p) {
        *slot = NULL;           /* the variable is cleared no matter what */
        sp_record *r = sp__find(p);
        if (r == NULL) {
            live_block(p, at, call);          /* reports */
        } else if (r->state != ST_LIVE) {
            char detail[200], b[96];
            snprintf(detail, sizeof detail, "it was already released at %s", sp__site_str(r->free_at, b, sizeof b));
            sp__event(r, EV_BLOCKED, at, expr, 0);
            sp__report(SP_ISSUE_DOUBLE_FREE, at, call, r, "this object was released twice", detail,
                       "a plain '=' copy does not add an owner: use q = sp_share(p) for a second shared "
                       "owner, or q = sp_move(&p) to hand ownership over",
                       "second release blocked; variable set to NULL");
        } else if (r->kind == SP_SHARED && r->strong > 1) {
            r->strong--;
            sp__event(r, EV_DROP, at, expr, r->strong);
        } else {
            sp__event(r, EV_DROP, at, expr, 0);
            sp__destroy(r, at, "sp_drop", expr);
        }
    }
    sp__unlock();
}

/* ------------------------------------------------------------------ conversions */
void *sp__to_shared(void **slot, sp_site at, const char *expr)
{
    char call[128];
    CALL(call, "sp_to_shared", expr);
    sp__lock();
    sp__ensure_init();
    void *result = NULL;
    void *p = slot_value(slot, at, call);
    sp_record *r = p ? live_block(p, at, call) : NULL;
    if (r) {
        if (r->kind == SP_RAW) {
            sp__report(SP_ISSUE_MISUSE, at, call, r, "plain malloc memory is not owned yet", NULL,
                       "use p = sp_adopt_shared(p, destructor) to give raw memory a shared owner",
                       "blocked: returned NULL");
        } else {
            r->kind = SP_SHARED;
            sp__event(r, EV_TO_SHARED, at, expr, r->strong);
            *slot = NULL;
            result = p;
        }
    }
    sp__unlock();
    return result;
}

void *sp__to_unique(void **slot, sp_site at, const char *expr)
{
    char call[128];
    CALL(call, "sp_to_unique", expr);
    sp__lock();
    sp__ensure_init();
    void *result = NULL;
    void *p = slot_value(slot, at, call);
    sp_record *r = p ? live_block(p, at, call) : NULL;
    if (r) {
        if (r->kind == SP_SHARED && r->strong > 1) {
            char detail[96];
            snprintf(detail, sizeof detail, "%zu owners share it right now", r->strong);
            sp__report(SP_ISSUE_MISUSE, at, call, r, "a shared object with several owners cannot become unique",
                       detail, "drop the other owners first (sp_drop), then convert; sp_use_count(p) tells you how many remain",
                       "blocked: returned NULL, your variable is unchanged");
        } else if (r->kind == SP_RAW) {
            sp__report(SP_ISSUE_MISUSE, at, call, r, "plain malloc memory is not owned yet", NULL,
                       "use p = sp_adopt(p, destructor)", "blocked: returned NULL");
        } else {
            r->kind = SP_UNIQUE;
            r->strong = 1;
            sp__event(r, EV_TO_UNIQUE, at, expr, 1);
            *slot = NULL;
            result = p;
        }
    }
    sp__unlock();
    return result;
}

void *sp__release(void **slot, sp_site at, const char *expr)
{
    char call[128];
    CALL(call, "sp_release", expr);
    sp__lock();
    sp__ensure_init();
    void *result = NULL;
    void *p = slot_value(slot, at, call);
    sp_record *r = p ? live_block(p, at, call) : NULL;
    if (r) {
        if (r->kind == SP_SHARED && r->strong > 1) {
            sp__report(SP_ISSUE_MISUSE, at, call, r, "cannot release an object other owners still use", NULL,
                       "only a sole owner can give the memory away", "blocked: returned NULL");
        } else {
            r->kind = SP_RAW;
            r->strong = 1;
            r->dtor = NULL;
            sp__event(r, EV_RELEASE, at, expr, 0);
            *slot = NULL;
            result = p;
        }
    }
    sp__unlock();
    return result;
}

void *sp__adopt(void *p, sp_kind kind, sp_dtor dtor, sp_site at, const char *expr)
{
    if (p == NULL) return NULL;
    char call[128];
    CALL(call, kind == SP_SHARED ? "sp_adopt_shared" : "sp_adopt", expr);
    sp__lock();
    sp__ensure_init();
    void *result = NULL;
    sp_record *r = sp__find(p);
    if (r == NULL && sp__find_containing(p, NULL) == NULL) {
        r = sp__register_foreign(p, at);   /* malloc'd in a file without smartptr.h */
        sp__event(r, EV_ALLOC, at, "untracked memory", 0);
    }
    if (r) r = live_block(p, at, call);
    else live_block(p, at, call);
    if (r) {
        if (r->kind != SP_RAW) {
            sp__report(SP_ISSUE_MISUSE, at, call, r, "this object already has an owner", NULL,
                       "adopt only plain malloc memory. To change the kind of an owned object use "
                       "sp_to_shared / sp_to_unique",
                       "blocked: returned NULL");
        } else {
            r->kind = kind;
            r->strong = 1;
            r->dtor = dtor;
            sp__event(r, EV_ADOPT, at, expr, 1);
            result = p;
        }
    }
    sp__unlock();
    return result;
}

/* ------------------------------------------------------------------ weak */
sp_weak sp__weak(void *p, sp_site at, const char *expr)
{
    sp_weak w = SP_WEAK_EMPTY;
    if (p == NULL) return w;
    char call[128];
    CALL(call, "sp_weak_from", expr);
    sp__lock();
    sp__ensure_init();
    sp_record *r = live_block(p, at, call);
    if (r && r->kind == SP_RAW) {
        sp__report(SP_ISSUE_MISUSE, at, call, r, "weak observers need an owned object", NULL,
                   "give it an owner first with sp_adopt or sp_adopt_shared", "returned an empty sp_weak");
    } else if (r) {
        r->weak++;
        sp__event(r, EV_WEAK, at, expr, r->weak);
        w.rec_ = r;
        w.id_ = r->id;
    }
    sp__unlock();
    return w;
}

sp_weak sp__weak_copy(const sp_weak *w, sp_site at)
{
    (void)at;
    sp_weak copy = SP_WEAK_EMPTY;
    if (w == NULL || w->rec_ == NULL) return copy;
    sp__lock();
    sp_record *r = w->rec_;
    r->weak++;
    copy = *w;
    sp__unlock();
    return copy;
}

static bool weak_alive(const sp_weak *w, sp_record **out)
{
    if (w == NULL || w->rec_ == NULL) return false;
    sp_record *r = w->rec_;
    if (r->id != w->id_ || r->state != ST_LIVE || r->pending_destroy) return false;
    *out = r;
    return true;
}

void *sp__weak_lock(const sp_weak *w, sp_site at, const char *expr)
{
    char call[128];
    CALL(call, "sp_weak_lock", expr);
    sp__lock();
    sp__ensure_init();
    void *result = NULL;
    sp_record *r;
    if (weak_alive(w, &r)) {
        if (r->kind == SP_SHARED) {
            r->strong++;
            sp__event(r, EV_LOCK, at, expr, r->strong);
            result = r->ptr;
        } else {
            sp__report(SP_ISSUE_MISUSE, at, call, r, "cannot lock a weak observer of a unique object",
                       "locking would create a second owner of a unique object",
                       "use sp_weak_peek(&w) to look at it without owning it, or make the owner shared",
                       "returned NULL");
        }
    }
    sp__unlock();
    return result;
}

void *sp_weak_peek(const sp_weak *w)
{
    sp__lock();
    sp_record *r;
    void *p = weak_alive(w, &r) ? r->ptr : NULL;
    sp__unlock();
    return p;
}

bool sp_weak_expired(const sp_weak *w)
{
    sp__lock();
    sp_record *r;
    bool expired = !weak_alive(w, &r);
    sp__unlock();
    return expired;
}

void sp__weak_drop(sp_weak *w, sp_site at)
{
    (void)at;
    if (w == NULL || w->rec_ == NULL) return;
    sp__lock();
    sp__record_unref_weak(w->rec_);
    sp__unlock();
    *w = SP_WEAK_EMPTY;
}

/* ------------------------------------------------------------------ borrowing */
void *sp__borrow(void *p, sp_site at, const char *expr)
{
    if (p == NULL) return NULL;
    char call[128];
    CALL(call, "sp_borrow", expr);
    sp__lock();
    sp__ensure_init();
    sp_record *r = live_block(p, at, call);
    void *result = NULL;
    if (r) {
        r->borrows++;
        r->borrow_at = at;
        sp__event(r, EV_BORROW, at, expr, r->borrows);
        result = p;
    }
    sp__unlock();
    return result;
}

void sp__unborrow(void *p, sp_site at, const char *expr)
{
    if (p == NULL) return;
    char call[128];
    CALL(call, "sp_unborrow", expr);
    sp__lock();
    sp__ensure_init();
    sp_record *r = sp__find(p);
    if (r == NULL || r->state == ST_FREED || r->state == ST_EXPIRED) {
        live_block(p, at, call);
    } else if (r->borrows == 0) {
        sp__report(SP_ISSUE_MISUSE, at, call, r, "sp_unborrow() without a matching sp_borrow()", NULL,
                   "call sp_unborrow once for every sp_borrow", "ignored");
    } else {
        r->borrows--;
        sp__event(r, EV_UNBORROW, at, expr, r->borrows);
        if (r->borrows == 0 && r->pending_destroy) {
            r->pending_destroy = false;
            sp__destroy(r, at, "sp_unborrow", expr);       /* the deferred destruction happens now */
        }
    }
    sp__unlock();
}

/* ------------------------------------------------------------------ scope cleanup */
void sp__auto_cleanup(void *slot)
{
    void **pp = slot;
    void *p = *pp;
    if (p == NULL) return;
    sp_site at = { "end of scope", 0, "sp_auto" };
    sp__lock();
    sp__ensure_init();
    sp_record *r = sp__find(p);
    if (r && r->state == ST_LIVE) {
        sp__event(r, EV_AUTO, at, "sp_auto", 0);
        if (r->kind == SP_SHARED && r->strong > 1) {
            r->strong--;
        } else {
            sp__destroy(r, at, "sp_auto", "variable");
        }
    }
    /* already freed by hand, or not ours: nothing to do */
    *pp = NULL;
    sp__unlock();
}

void sp__auto_weak_cleanup(void *slot)
{
    sp__weak_drop(slot, (sp_site){ "end of scope", 0, "sp_auto_weak" });
}

/* ------------------------------------------------------------------ queries */
void *sp__name(void *p, const char *label)
{
    if (p == NULL) return NULL;
    sp__lock();
    sp__ensure_init();
    sp_record *r = sp__find(p);
    if (r && label) snprintf(r->label, sizeof r->label, "%s", label);
    sp__unlock();
    return p;
}

size_t sp_use_count(const void *p)
{
    sp__lock();
    sp_record *r = sp__find(p);
    size_t n = (r && r->state == ST_LIVE) ? r->strong : 0;
    sp__unlock();
    return n;
}

size_t sp_weak_count(const void *p)
{
    sp__lock();
    sp_record *r = sp__find(p);
    size_t n = r ? r->weak : 0;
    sp__unlock();
    return n;
}

sp_kind sp_kind_of(const void *p)
{
    sp__lock();
    sp_record *r = sp__find(p);
    sp_kind k = (r && r->state == ST_LIVE) ? r->kind : SP_NONE;
    sp__unlock();
    return k;
}

uint64_t sp_id(const void *p)
{
    sp__lock();
    sp_record *r = sp__find(p);
    uint64_t id = r ? r->id : 0;
    sp__unlock();
    return id;
}
