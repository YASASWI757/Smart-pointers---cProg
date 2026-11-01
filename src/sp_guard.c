/*
 * sp_guard.c - Sentinel core.
 *
 * Every allocation gets a side-table record (hash table keyed by address),
 * a 16-byte sentinel after the user bytes, and an event history.  Freed
 * blocks are poisoned and parked in a quarantine queue, so a second free is
 * recognised and a late write is caught when the block leaves quarantine.
 * At exit the remaining blocks are analysed: leaks grouped by the line that
 * created them, split into direct and indirect leaks, and reference cycles
 * found by scanning leaked memory for pointers to other leaked blocks.
 */
#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "sp_internal.h"

/* The leak scanner reads leaked memory word by word, like a conservative
 * garbage collector, including bytes the program never initialised.  Tell
 * Valgrind that this is deliberate when its header is available. */
#if defined(__has_include)
#  if __has_include(<valgrind/memcheck.h>)
#    include <valgrind/memcheck.h>
#    define SP_SCAN_DEFINED(p, n) VALGRIND_MAKE_MEM_DEFINED((p), (n))
#  endif
#endif
#ifndef SP_SCAN_DEFINED
#  define SP_SCAN_DEFINED(p, n) ((void)0)
#endif

/* ------------------------------------------------------------------ state */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static bool     g_init;
static sp_mode  g_mode = SP_MODE_REPORT;
static bool     g_reclaim = true;
static long     g_fail_countdown = -1;
static FILE    *g_log;
static uint64_t g_next_id = 1;
static sp_stats g_stats;
static size_t   g_issue_counts[SP_ISSUE_COUNT_];

static sp_record **g_buckets;
static size_t      g_nbuckets;
static size_t      g_nrecords;

typedef struct { sp_record *head, *tail; } sp_list;
static sp_list g_live, g_quarantine, g_expired;
static size_t  g_quarantine_bytes;
static size_t  g_quarantine_max_blocks = 4096;
static size_t  g_quarantine_max_bytes = (size_t)16 << 20;

void sp__lock(void) { pthread_mutex_lock(&g_lock); }
void sp__unlock(void) { pthread_mutex_unlock(&g_lock); }
sp_mode sp__mode(void) { return g_mode; }
FILE *sp__log_file(void) { return g_log; }

size_t sp__count_issue(sp_issue kind)
{
    g_issue_counts[kind]++;
    if (kind != SP_ISSUE_ALLOC_FAIL) {
        g_stats.issues++;
    }
    return g_stats.issues;
}

/* ------------------------------------------------------------------ lists */
static void list_push(sp_list *l, sp_record *r)
{
    r->prev = l->tail;
    r->next = NULL;
    if (l->tail) l->tail->next = r; else l->head = r;
    l->tail = r;
}

static void list_remove(sp_list *l, sp_record *r)
{
    if (r->prev) r->prev->next = r->next; else l->head = r->next;
    if (r->next) r->next->prev = r->prev; else l->tail = r->prev;
    r->prev = r->next = NULL;
}

/* ------------------------------------------------------------------ hash table */
static size_t hash_ptr(const void *p, size_t nbuckets)
{
    uint64_t x = (uint64_t)(uintptr_t)p;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return (size_t)x & (nbuckets - 1);
}

static void fatal(const char *msg)
{
    fprintf(stderr, "sentinel: fatal: %s\n", msg);
    abort();
}

static void hash_grow(void)
{
    size_t n = g_nbuckets ? g_nbuckets * 2 : 1024;
    sp_record **b = calloc(n, sizeof *b);
    if (b == NULL) fatal("out of memory for the block table");
    for (size_t i = 0; i < g_nbuckets; ++i) {
        sp_record *r = g_buckets[i];
        while (r) {
            sp_record *next = r->hnext;
            size_t h = hash_ptr(r->ptr, n);
            r->hnext = b[h];
            b[h] = r;
            r = next;
        }
    }
    free(g_buckets);
    g_buckets = b;
    g_nbuckets = n;
}

static void hash_insert(sp_record *r)
{
    if (g_nrecords + 1 > g_nbuckets) hash_grow();
    size_t h = hash_ptr(r->ptr, g_nbuckets);
    r->hnext = g_buckets[h];
    g_buckets[h] = r;
    g_nrecords++;
}

static void hash_remove(sp_record *r)
{
    sp_record **pp = &g_buckets[hash_ptr(r->ptr, g_nbuckets)];
    while (*pp && *pp != r) pp = &(*pp)->hnext;
    if (*pp) {
        *pp = r->hnext;
        g_nrecords--;
    }
    r->hnext = NULL;
}

sp_record *sp__find(const void *p)
{
    if (p == NULL || g_nbuckets == 0) return NULL;
    sp_record *r = g_buckets[hash_ptr(p, g_nbuckets)];
    while (r && r->ptr != p) r = r->hnext;
    return r;
}

sp_record *sp__find_containing(const void *p, size_t *offset)
{
    uintptr_t a = (uintptr_t)p;
    const sp_list *lists[2] = { &g_live, &g_quarantine };
    for (int k = 0; k < 2; ++k) {
        for (sp_record *r = lists[k]->head; r; r = r->next) {
            uintptr_t s = (uintptr_t)r->ptr;
            if (a > s && a < s + r->size) {
                if (offset) *offset = (size_t)(a - s);
                return r;
            }
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ init */
static void at_exit_handler(void);

void sp__ensure_init(void)
{
    if (g_init) return;
    g_init = true;

    const char *mode = getenv("SENTINEL");
    if (mode) {
        if (strcmp(mode, "summary") == 0) g_mode = SP_MODE_SUMMARY;
        else if (strcmp(mode, "silent") == 0) g_mode = SP_MODE_SILENT;
        else if (strcmp(mode, "abort") == 0) g_mode = SP_MODE_ABORT;
    }
    const char *reclaim = getenv("SENTINEL_RECLAIM");
    if (reclaim && strcmp(reclaim, "0") == 0) g_reclaim = false;

    const char *fail = getenv("SENTINEL_FAIL_AT");
    if (fail) {
        long n = strtol(fail, NULL, 10);
        if (n > 0) g_fail_countdown = n - 1;
    }
    const char *quarantine = getenv("SENTINEL_QUARANTINE");
    if (quarantine) {
        long n = strtol(quarantine, NULL, 10);
        if (n >= 0) g_quarantine_max_blocks = (size_t)n;
    }
    const char *log = getenv("SENTINEL_LOG");
    if (log && *log) g_log = fopen(log, "w");

    hash_grow();
    atexit(at_exit_handler);
}

/* ------------------------------------------------------------------ sentinels and poison */
static unsigned char sentinel_byte(size_t i) { return (unsigned char)(0xA5 ^ (i * 29u)); }

static void write_sentinel(sp_record *r)
{
    unsigned char *end = (unsigned char *)r->ptr + r->size;
    for (size_t i = 0; i < SP_SENTINEL_BYTES; ++i) end[i] = sentinel_byte(i);
}

/* Number of bytes past the end that were overwritten (0 = intact). */
static size_t sentinel_damage(const sp_record *r)
{
    if (r->foreign || r->ptr == NULL) return 0;
    const unsigned char *end = (const unsigned char *)r->ptr + r->size;
    size_t reach = 0;
    for (size_t i = 0; i < SP_SENTINEL_BYTES; ++i) {
        if (end[i] != sentinel_byte(i)) reach = i + 1;
    }
    return reach;
}

static bool poison_damaged(const sp_record *r, size_t *first)
{
    if (r->foreign || r->ptr == NULL) return false;
    const unsigned char *p = r->ptr;
    for (size_t i = 0; i < r->size; ++i) {
        if (p[i] != SP_POISON) {
            *first = i;
            return true;
        }
    }
    return false;
}

static void report_overflow(sp_record *r, sp_site at, const char *call, size_t reach, const char *when)
{
    if (r->overflow_reported) return;
    r->overflow_reported = true;
    char detail[200], fix[400];
    snprintf(detail, sizeof detail,
             "at least %zu byte(s) were written past the end of this %zu-byte block "
             "(found %s)", reach, r->size, when);
    snprintf(fix, sizeof fix,
             "check the code that writes into this block: strcpy/sprintf need strlen+1 "
             "bytes for the '\\0', and loops over N items must use i < N, not i <= N. "
             "Allocate %zu bytes or more, or write less.", r->size + reach);
    sp__report(SP_ISSUE_OVERFLOW, at, call, r, "buffer overflow: memory after the block was overwritten",
               detail, fix, "reported; the block is still released normally");
}

/* ------------------------------------------------------------------ blocks */
sp_record *sp__new_block(size_t size, bool zero, const char *type, sp_site at)
{
    if (g_fail_countdown == 0) {
        g_fail_countdown = -1;
        char detail[96];
        snprintf(detail, sizeof detail, "requested %zu bytes; failure injected on purpose", size);
        sp__report(SP_ISSUE_ALLOC_FAIL, at, NULL, NULL, "allocation failed (fault injection)", detail,
                   "make sure this code path handles NULL from malloc", "returned NULL");
        return NULL;
    }
    if (g_fail_countdown > 0) g_fail_countdown--;

    if (size > SIZE_MAX - SP_SENTINEL_BYTES) {
        sp__report(SP_ISSUE_ALLOC_FAIL, at, NULL, NULL, "allocation failed: size is too large",
                   NULL, "check the size calculation for an overflow or a negative value", "returned NULL");
        return NULL;
    }

    void *mem = malloc(size + SP_SENTINEL_BYTES);
    sp_record *r = mem ? calloc(1, sizeof *r) : NULL;
    if (r == NULL) {
        free(mem);
        char detail[64];
        snprintf(detail, sizeof detail, "requested %zu bytes", size);
        sp__report(SP_ISSUE_ALLOC_FAIL, at, NULL, NULL, "allocation failed: out of memory", detail,
                   "handle a NULL return value", "returned NULL");
        return NULL;
    }
    if (zero) memset(mem, 0, size);

    r->ptr = mem;
    r->size = size;
    r->id = g_next_id++;
    r->type = type;
    r->kind = SP_RAW;
    r->state = ST_LIVE;
    r->strong = 1;
    r->alloc_at = at;
    write_sentinel(r);

    hash_insert(r);
    list_push(&g_live, r);
    g_stats.allocations++;
    g_stats.live_blocks++;
    g_stats.live_bytes += size;
    g_stats.total_bytes += size;
    if (g_stats.live_bytes > g_stats.peak_bytes) g_stats.peak_bytes = g_stats.live_bytes;
    return r;
}

sp_record *sp__register_foreign(void *p, sp_site at)
{
    sp_record *r = calloc(1, sizeof *r);
    if (r == NULL) fatal("out of memory for a block record");
    r->ptr = p;
    r->id = g_next_id++;
    r->type = "foreign";
    r->kind = SP_RAW;
    r->state = ST_LIVE;
    r->strong = 1;
    r->foreign = true;
    r->alloc_at = at;
    hash_insert(r);
    list_push(&g_live, r);
    g_stats.allocations++;
    g_stats.live_blocks++;
    return r;
}

void sp__event(sp_record *r, sp_event_kind kind, sp_site at, const char *expr, size_t aux)
{
    sp_event *e = &r->history[r->nevents % SP_HISTORY];
    e->kind = kind;
    e->at = at;
    e->expr = expr;
    e->aux = aux;
    r->nevents++;
}

static void free_record(sp_record *r)
{
    free(r);
}

void sp__record_unref_weak(sp_record *r)
{
    if (r->weak > 0) r->weak--;
    if (r->weak == 0 && r->state == ST_EXPIRED) {
        list_remove(&g_expired, r);
        free_record(r);
    }
}

/* Remove the oldest quarantined block for good, checking it was not touched. */
static void evict(sp_record *r)
{
    list_remove(&g_quarantine, r);
    g_quarantine_bytes -= r->size;
    g_stats.quarantined_blocks--;

    size_t first;
    if (!r->uaf_reported && poison_damaged(r, &first)) {
        r->uaf_reported = true;
        char detail[200], fix[300];
        snprintf(detail, sizeof detail,
                 "byte %zu of this %zu-byte block changed after it was freed "
                 "(detected when the quarantine released it)", first, r->size);
        snprintf(fix, sizeof fix,
                 "some pointer to this block was still used after the free shown above. "
                 "Set every copy to NULL after freeing, or observe it with an sp_weak and "
                 "check sp_weak_peek() before use.");
        sp__report(SP_ISSUE_USE_AFTER_FREE, r->free_at, NULL, r,
                   "use after free: a freed block was written to", detail, fix,
                   "reported; memory returned to the system");
    }
    if (!r->foreign) free(r->ptr);
    hash_remove(r);
    r->ptr = NULL;
    if (r->weak > 0) {
        r->state = ST_EXPIRED;
        list_push(&g_expired, r);
    } else {
        free_record(r);
    }
}

/* Release a block's memory into quarantine (lock held). */
static void retire(sp_record *r, sp_site at, const char *call)
{
    size_t reach = sentinel_damage(r);
    if (reach) report_overflow(r, at, call, reach, "when the block was released");

    r->state = ST_FREED;
    r->free_at = at;
    list_remove(&g_live, r);
    g_stats.frees++;
    g_stats.live_blocks--;
    g_stats.live_bytes -= r->size;

    if (r->foreign) {          /* unknown size: nothing to poison or check */
        list_push(&g_quarantine, r);
        g_stats.quarantined_blocks++;
        evict(r);
        return;
    }
    memset(r->ptr, SP_POISON, r->size);
    list_push(&g_quarantine, r);
    g_quarantine_bytes += r->size;
    g_stats.quarantined_blocks++;
    while (g_quarantine.head &&
           (g_stats.quarantined_blocks > g_quarantine_max_blocks ||
            g_quarantine_bytes > g_quarantine_max_bytes)) {
        evict(g_quarantine.head);
    }
}

/* Destroy a block whose last owner let go (lock held on entry and exit). */
void sp__destroy(sp_record *r, sp_site at, const char *op, const char *expr)
{
    if (r->borrows > 0) {
        if (!r->pending_destroy) {
            r->pending_destroy = true;
            sp__event(r, EV_DEFERRED, at, expr, r->borrows);
            char detail[200], b[96];
            snprintf(detail, sizeof detail, "%zu borrow(s) still active; last borrowed at %s",
                     r->borrows, sp__site_str(r->borrow_at, b, sizeof b));
            sp__report(SP_ISSUE_BORROW, at, NULL, r,
                       "released while someone is still borrowing it", detail,
                       "release the object after the borrower calls sp_unborrow(), or give the "
                       "borrower its own owner with sp_share()",
                       "destruction deferred until the last sp_unborrow(): no dangling pointer");
        }
        return;
    }
    r->state = ST_DESTROYING;
    r->strong = 0;
    if (r->dtor) {
        sp_dtor dtor = r->dtor;
        void *obj = r->ptr;
        sp__unlock();               /* the destructor may call free() itself */
        dtor(obj);
        sp__lock();
    }
    char call[128];
    snprintf(call, sizeof call, "%s(%s)", op, expr ? expr : "");
    sp__event(r, EV_FREE, at, expr, 0);
    retire(r, at, call);
}

/* ------------------------------------------------------------------ malloc family */
static void *alloc_common(size_t size, bool zero, sp_site at)
{
    sp__lock();
    sp__ensure_init();
    sp_record *r = sp__new_block(size, zero, NULL, at);
    if (r) sp__event(r, EV_ALLOC, at, NULL, size);
    void *p = r ? r->ptr : NULL;
    sp__unlock();
    return p;
}

void *sp__malloc(size_t size, sp_site at) { return alloc_common(size, false, at); }

void *sp__calloc(size_t count, size_t size, sp_site at)
{
    if (size != 0 && count > SIZE_MAX / size) {
        sp__lock();
        sp__ensure_init();
        char detail[96];
        snprintf(detail, sizeof detail, "%zu x %zu bytes does not fit in size_t", count, size);
        sp__report(SP_ISSUE_ALLOC_FAIL, at, "calloc", NULL, "allocation failed: size overflow", detail,
                   "check the element count", "returned NULL");
        sp__unlock();
        return NULL;
    }
    return alloc_common(count * size, true, at);
}

char *sp__strdup(const char *text, sp_site at)
{
    if (text == NULL) {
        sp__lock();
        sp__ensure_init();
        sp__report(SP_ISSUE_MISUSE, at, "strdup(NULL)", NULL, "strdup() was given a NULL string", NULL,
                   "check the string before copying it", "returned NULL instead of crashing");
        sp__unlock();
        return NULL;
    }
    size_t n = strlen(text) + 1;
    char *p = alloc_common(n, false, at);
    if (p) memcpy(p, text, n);
    return p;
}

char *sp__strndup(const char *text, size_t max, sp_site at)
{
    if (text == NULL) return sp__strdup(NULL, at);
    size_t n = 0;
    while (n < max && text[n]) n++;
    char *p = alloc_common(n + 1, false, at);
    if (p) {
        memcpy(p, text, n);
        p[n] = '\0';
    }
    return p;
}

static void report_unknown(const void *p, sp_site at, const char *call, const char *op)
{
    size_t offset = 0;
    sp_record *inside = sp__find_containing(p, &offset);
    if (inside) {
        char detail[160], fix[200];
        snprintf(detail, sizeof detail, "the pointer is %zu byte(s) inside block #%llu, not at its start",
                 offset, (unsigned long long)inside->id);
        snprintf(fix, sizeof fix, "%s the exact pointer that malloc returned (block #%llu starts at %p); "
                 "keep the original pointer in a separate variable if you advance a copy",
                 op, (unsigned long long)inside->id, inside->ptr);
        sp__report(SP_ISSUE_INTERIOR_FREE, at, call, inside, "pointer into the middle of a block",
                   detail, fix, "blocked: nothing was freed");
    } else {
        char detail[96];
        snprintf(detail, sizeof detail, "address %p was never returned by malloc", p);
        sp__report(SP_ISSUE_INVALID_FREE, at, call, NULL, "this address does not belong to the heap",
                   detail,
                   "only free memory from malloc/calloc/realloc/strdup. Stack variables, globals and "
                   "string literals are released automatically. If a library allocated it, make sure "
                   "that file also includes smartptr.h",
                   "blocked: nothing was freed");
    }
}

static void report_double(sp_record *r, sp_site at, const char *call, const char *expr)
{
    char detail[200], b[96];
    if (r->state == ST_DESTROYING) {
        snprintf(detail, sizeof detail, "the block is being destroyed right now (its destructor freed it)");
    } else {
        snprintf(detail, sizeof detail, "it was already released at %s",
                 sp__site_str(r->free_at, b, sizeof b));
    }
    const char *fix = r->kind == SP_RAW
        ? "set the pointer to NULL right after free(). If two variables hold this address, "
          "decide which one owns it, or use sp_make_unique / sp_make_shared"
        : "a plain '=' copy does not add an owner. Use sp_share() to add a shared owner, or "
          "sp_move() to hand ownership over; sp_drop() sets your variable to NULL for you";
    sp__event(r, EV_BLOCKED, at, expr, 0);
    sp__report(SP_ISSUE_DOUBLE_FREE, at, call, r, "this block was released twice", detail, fix,
               "second release blocked; program continues safely");
}

void sp__free(void *ptr, sp_site at, const char *expr)
{
    if (ptr == NULL) return;
    char call[128];
    snprintf(call, sizeof call, "free(%s)", expr ? expr : "");

    sp__lock();
    sp__ensure_init();
    sp_record *r = sp__find(ptr);
    if (r == NULL) {
        report_unknown(ptr, at, call, "free()");
    } else if (r->state != ST_LIVE) {
        report_double(r, at, call, expr);
    } else if (r->kind == SP_SHARED && r->strong > 1) {
        r->strong--;
        sp__event(r, EV_DROP, at, expr, r->strong);
        char detail[160];
        snprintf(detail, sizeof detail, "the object has %zu other owner(s) still using it", r->strong);
        sp__report(SP_ISSUE_OWNERSHIP, at, call, r, "free() on an object that is shared", detail,
                   "use sp_drop(&p) for shared objects; it releases only your reference and clears p",
                   "only this reference was dropped; the object stays alive for the others");
    } else {
        sp__destroy(r, at, "free", expr);
    }
    sp__unlock();
}

void *sp__realloc(void *ptr, size_t size, sp_site at, const char *expr)
{
    if (ptr == NULL) return sp__malloc(size, at);
    if (size == 0) {
        sp__free(ptr, at, expr);
        return NULL;
    }
    char call[128];
    snprintf(call, sizeof call, "realloc(%s)", expr ? expr : "");

    sp__lock();
    sp__ensure_init();
    sp_record *r = sp__find(ptr);
    void *result = NULL;
    if (r == NULL) {
        report_unknown(ptr, at, call, "realloc()");
    } else if (r->state != ST_LIVE) {
        char detail[160], b[96];
        snprintf(detail, sizeof detail, "it was released at %s", sp__site_str(r->free_at, b, sizeof b));
        sp__report(SP_ISSUE_USE_AFTER_FREE, at, call, r, "realloc() of a block that was already freed",
                   detail, "set the pointer to NULL after freeing it; realloc(NULL, n) acts like malloc",
                   "blocked: returned NULL");
    } else if (r->kind == SP_SHARED) {
        sp__report(SP_ISSUE_MISUSE, at, call, r, "realloc() of a shared object",
                   "other owners would keep pointing at the old address",
                   "allocate a new shared object and copy the data, or convert with sp_to_unique() first",
                   "blocked: returned NULL, original untouched");
    } else if (r->borrows > 0) {
        sp__report(SP_ISSUE_BORROW, at, call, r, "realloc() while the block is borrowed",
                   "a borrower holds the current address", "finish the borrow (sp_unborrow) first",
                   "blocked: returned NULL, original untouched");
    } else {
        size_t reach = sentinel_damage(r);
        if (reach) report_overflow(r, at, call, reach, "during realloc");
        sp_record *n = sp__new_block(size, false, r->type, at);
        if (n) {
            memcpy(n->ptr, r->ptr, size < r->size ? size : r->size);
            n->kind = r->kind;
            n->dtor = r->dtor;
            n->alloc_at = r->alloc_at;
            memcpy(n->label, r->label, sizeof n->label);
            sp__event(n, EV_REALLOC, at, expr, (size_t)r->id);
            r->kind = SP_RAW;          /* the old memory is just bytes now */
            r->dtor = NULL;
            sp__event(r, EV_FREE, at, expr, 0);
            retire(r, at, call);
            result = n->ptr;
        }
    }
    sp__unlock();
    return result;
}

/* ------------------------------------------------------------------ tooling */
void sp_guard_set_mode(sp_mode mode)
{
    sp__lock();
    sp__ensure_init();
    g_mode = mode;
    sp__unlock();
}

sp_mode sp_guard_mode(void)
{
    sp__lock();
    sp__ensure_init();
    sp_mode m = g_mode;
    sp__unlock();
    return m;
}

sp_stats sp_guard_stats(void)
{
    sp__lock();
    sp_stats s = g_stats;
    sp__unlock();
    return s;
}

size_t sp_guard_issues(sp_issue kind)
{
    if ((unsigned)kind >= SP_ISSUE_COUNT_) return 0;
    sp__lock();
    size_t n = g_issue_counts[kind];
    sp__unlock();
    return n;
}

void sp_guard_fail_after(long n)
{
    sp__lock();
    sp__ensure_init();
    g_fail_countdown = n < 0 ? -1 : n;
    sp__unlock();
}

size_t sp_guard_check(void)
{
    sp__lock();
    sp__ensure_init();
    size_t before = g_stats.issues;
    sp_site here = { "sp_guard_check", 0, "audit" };
    for (sp_record *r = g_live.head; r; r = r->next) {
        size_t reach = sentinel_damage(r);
        if (reach) report_overflow(r, here, NULL, reach, "by sp_guard_check()");
    }
    for (sp_record *r = g_quarantine.head; r; r = r->next) {
        size_t first;
        if (!r->uaf_reported && poison_damaged(r, &first)) {
            r->uaf_reported = true;
            char detail[160];
            snprintf(detail, sizeof detail, "byte %zu of this %zu-byte block changed after it was freed",
                     first, r->size);
            sp__report(SP_ISSUE_USE_AFTER_FREE, r->free_at, NULL, r,
                       "use after free: a freed block was written to", detail,
                       "a pointer to this block was used after the free shown above; set copies to NULL",
                       "reported by sp_guard_check()");
        }
    }
    size_t found = g_stats.issues - before;
    sp__unlock();
    return found;
}

void sp_guard_dump(const void *ptr)
{
    sp__lock();
    sp__ensure_init();
    size_t off = 0;
    sp_record *r = sp__find(ptr);
    if (r == NULL) r = sp__find_containing(ptr, &off);
    if (r == NULL) {
        fprintf(stderr, "sentinel: %p is not a tracked block\n", ptr);
    } else {
        sp__print_block(stderr, r);
    }
    sp__unlock();
}

const char *sp_issue_name(sp_issue kind)
{
    static const char *names[SP_ISSUE_COUNT_] = {
        "double free", "invalid free", "interior free", "buffer overflow", "use after free",
        "memory leak", "reference cycle", "ownership conflict", "API misuse", "borrow conflict",
        "allocation failure"
    };
    return (unsigned)kind < SP_ISSUE_COUNT_ ? names[kind] : "unknown";
}

/* ------------------------------------------------------------------ exit analysis */
typedef struct {
    sp_record *r;
    size_t *edges;
    size_t nedges, cap;
    bool indirect;   /* pointed to by another leaked block */
    bool in_cycle;
    size_t index, low, scc;
    bool on_stack, visited;
} node;

static void add_edge(node *n, size_t to)
{
    for (size_t i = 0; i < n->nedges; ++i) if (n->edges[i] == to) return;
    if (n->nedges == n->cap) {
        size_t c = n->cap ? n->cap * 2 : 4;
        size_t *e = realloc(n->edges, c * sizeof *e);
        if (e == NULL) return;
        n->edges = e;
        n->cap = c;
    }
    n->edges[n->nedges++] = to;
}

/* Tarjan's strongly connected components, iterative-safe for small graphs. */
static size_t g_tarjan_index;
static size_t *g_stack;
static size_t g_sp;

static void strongconnect(node *nodes, size_t v)
{
    nodes[v].index = nodes[v].low = g_tarjan_index++;
    nodes[v].visited = true;
    g_stack[g_sp++] = v;
    nodes[v].on_stack = true;
    for (size_t i = 0; i < nodes[v].nedges; ++i) {
        size_t w = nodes[v].edges[i];
        if (!nodes[w].visited) {
            strongconnect(nodes, w);
            if (nodes[w].low < nodes[v].low) nodes[v].low = nodes[w].low;
        } else if (nodes[w].on_stack && nodes[w].index < nodes[v].low) {
            nodes[v].low = nodes[w].index;
        }
    }
    if (nodes[v].low == nodes[v].index) {
        size_t start = g_sp;
        do { start--; } while (g_stack[start] != v);
        size_t count = g_sp - start;
        bool self_loop = false;
        for (size_t i = 0; i < nodes[v].nedges; ++i) if (nodes[v].edges[i] == v) self_loop = true;
        for (size_t k = start; k < g_sp; ++k) {
            nodes[g_stack[k]].on_stack = false;
            nodes[g_stack[k]].scc = v;
            if (count > 1 || self_loop) nodes[g_stack[k]].in_cycle = true;
        }
        g_sp = start;
    }
}

static int cmp_site(const void *a, const void *b)
{
    const node *x = a, *y = b;
    int c = strcmp(x->r->alloc_at.file ? x->r->alloc_at.file : "",
                   y->r->alloc_at.file ? y->r->alloc_at.file : "");
    if (c) return c;
    if (x->r->alloc_at.line != y->r->alloc_at.line) return x->r->alloc_at.line < y->r->alloc_at.line ? -1 : 1;
    return x->r->id < y->r->id ? -1 : 1;
}

static void report_cycles(node *nodes, size_t n)
{
    bool *printed = calloc(n, sizeof *printed);
    size_t *path = calloc(n, sizeof *path);
    if (printed == NULL || path == NULL) {
        free(printed);
        free(path);
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        if (!nodes[i].in_cycle || printed[nodes[i].scc]) continue;
        printed[nodes[i].scc] = true;

        /* follow edges that stay inside this cycle until we return to the start */
        size_t len = 0, cur = i;
        path[len++] = cur;
        for (;;) {
            size_t next = SIZE_MAX;
            for (size_t e = 0; e < nodes[cur].nedges && next == SIZE_MAX; ++e) {
                size_t w = nodes[cur].edges[e];
                if (nodes[w].scc != nodes[i].scc) continue;
                bool seen = false;
                for (size_t k = 0; k < len; ++k) if (path[k] == w) seen = true;
                if (!seen || w == i) next = w;
            }
            if (next == SIZE_MAX || next == i || len == n) break;
            path[len++] = next;
            cur = next;
        }
        char text[480];
        size_t used = 0;
        for (size_t k = 0; k <= len && used < sizeof text - 48; ++k) {
            sp_record *r = nodes[path[k % len]].r;
            used += (size_t)snprintf(text + used, sizeof text - used, "%s#%llu %s",
                                     k ? " -> " : "", (unsigned long long)r->id,
                                     r->type ? r->type : "block");
        }
        char detail[600];
        snprintf(detail, sizeof detail, "%zu block(s) point at each other: %s", len, text);
        sp__report(SP_ISSUE_CYCLE, nodes[i].r->alloc_at, NULL, nodes[i].r,
                   "reference cycle: these blocks keep each other alive", detail,
                   "break the loop: make one direction non-owning (an sp_weak, or a plain pointer "
                   "that is never dropped), or clear one link before releasing the rest",
                   g_reclaim ? "reclaimed at exit" : "left allocated (SENTINEL_RECLAIM=0)");
    }
    free(printed);
    free(path);
}

static bool same_site(const node *a, const node *b)
{
    return a->r->alloc_at.line == b->r->alloc_at.line &&
           strcmp(a->r->alloc_at.file ? a->r->alloc_at.file : "",
                  b->r->alloc_at.file ? b->r->alloc_at.file : "") == 0;
}

static void report_leaks(node *nodes, size_t n)
{
    qsort(nodes, n, sizeof *nodes, cmp_site);   /* edges are no longer needed */
    size_t i = 0;
    while (i < n) {
        size_t j = i;
        size_t bytes = 0, direct = 0, indirect = 0, shared_owned = 0;
        sp_record *r = NULL;
        while (j < n && same_site(&nodes[i], &nodes[j])) {
            if (!nodes[j].in_cycle) {
                if (r == NULL) r = nodes[j].r;
                bytes += nodes[j].r->size;
                if (nodes[j].indirect) indirect++; else direct++;
                if (nodes[j].r->kind == SP_SHARED) shared_owned++;
            }
            j++;
        }
        size_t count = direct + indirect;
        if (count > 0) {
            char what[160], detail[320], fix[400], b[32];
            sp__fmt_bytes(bytes, b, sizeof b);
            snprintf(what, sizeof what, "%zu block(s), %s, were never released", count, b);
            snprintf(detail, sizeof detail,
                     "%zu direct (nothing else points to them) + %zu indirect (reachable only "
                     "through other leaked blocks)", direct, indirect);
            if (direct == 0) {
                snprintf(fix, sizeof fix, "these are lost only because the block holding them leaked: "
                         "fix that leak and these are fixed too");
            } else if (shared_owned) {
                snprintf(fix, sizeof fix, "a shared owner was never dropped. Every sp_make_shared, "
                         "sp_share and sp_weak_lock needs a matching sp_drop (or declare the owner sp_auto)");
            } else if (r->kind == SP_UNIQUE) {
                snprintf(fix, sizeof fix, "the unique owner went out of scope without sp_drop(). "
                         "Declare it with sp_auto so it is dropped automatically");
            } else if (count > 1) {
                snprintf(fix, sizeof fix, "allocated %zu times from this line and never freed: a free() "
                         "is missing inside a loop or on an early-return path (or declare the variable "
                         "sp_auto)", count);
            } else {
                snprintf(fix, sizeof fix, "add free() when you are done with it, or declare the variable "
                         "with sp_auto so it is freed when its scope ends");
            }
            sp__report(SP_ISSUE_LEAK, r->alloc_at, NULL, r, what, detail, fix,
                       g_reclaim ? "reclaimed at exit" : "left allocated (SENTINEL_RECLAIM=0)");
        }
        i = j;
    }
}

static void analyse_leaks(void)
{
    size_t n = 0;
    for (sp_record *r = g_live.head; r; r = r->next) n++;
    if (n == 0) return;
    node *nodes = calloc(n, sizeof *nodes);
    g_stack = calloc(n, sizeof *g_stack);
    if (nodes == NULL || g_stack == NULL) {
        free(nodes);
        free(g_stack);
        return;
    }
    size_t k = 0;
    for (sp_record *r = g_live.head; r; r = r->next) nodes[k++].r = r;

    /* Conservative scan: any aligned word equal to the start of another leaked block is an edge. */
    for (size_t a = 0; a < n; ++a) {
        sp_record *r = nodes[a].r;
        if (r->foreign) continue;
        const unsigned char *base = r->ptr;
        SP_SCAN_DEFINED(r->ptr, r->size);
        for (size_t off = 0; off + sizeof(void *) <= r->size; off += sizeof(void *)) {
            void *word;
            memcpy(&word, base + off, sizeof word);
            sp_record *t = sp__find(word);
            if (t == NULL || t->state != ST_LIVE) continue;
            for (size_t b = 0; b < n; ++b) {
                if (nodes[b].r == t) {
                    add_edge(&nodes[a], b);
                    if (b != a) nodes[b].indirect = true;
                    break;
                }
            }
        }
    }
    g_tarjan_index = 0;
    g_sp = 0;
    for (size_t v = 0; v < n; ++v) if (!nodes[v].visited) strongconnect(nodes, v);

    report_cycles(nodes, n);
    report_leaks(nodes, n);

    for (size_t v = 0; v < n; ++v) free(nodes[v].edges);
    free(nodes);
    free(g_stack);
    g_stack = NULL;
}

static void print_summary(FILE *out)
{
    char peak[32], total[32];
    sp__fmt_bytes(g_stats.peak_bytes, peak, sizeof peak);
    sp__fmt_bytes(g_stats.total_bytes, total, sizeof total);
    bool c = sp__color();
    const char *bold = c ? "\x1b[1m" : "", *dim = c ? "\x1b[2m" : "", *off = c ? "\x1b[0m" : "";
    const char *green = c ? "\x1b[32m" : "", *red = c ? "\x1b[31m" : "";

    fprintf(out, "\n%s== SENTINEL SUMMARY ==========================================%s\n", bold, off);
    fprintf(out, "  %zu allocations, %zu frees, %s allocated in total, peak %s live\n",
            g_stats.allocations, g_stats.frees, total, peak);
    if (g_stats.issues == 0) {
        fprintf(out, "  %sno memory errors, no leaks%s\n", green, off);
    } else {
        fprintf(out, "  %s%zu issue(s):%s", red, g_stats.issues, off);
        bool first = true;
        for (int k = 0; k < SP_ISSUE_COUNT_; ++k) {
            if (k == SP_ISSUE_ALLOC_FAIL || g_issue_counts[k] == 0) continue;
            fprintf(out, "%s %zu %s", first ? "" : ",", g_issue_counts[k], sp_issue_name((sp_issue)k));
            first = false;
        }
        fprintf(out, "\n");
    }
    if (g_issue_counts[SP_ISSUE_ALLOC_FAIL])
        fprintf(out, "  %s%zu allocation failure(s) handled%s\n", dim, g_issue_counts[SP_ISSUE_ALLOC_FAIL], off);
    fprintf(out, "%s===============================================================%s\n", bold, off);
}

void sp_guard_summary(void)
{
    sp__lock();
    sp__ensure_init();
    if (g_mode != SP_MODE_SILENT) print_summary(stderr);
    sp__unlock();
}

static void at_exit_handler(void)
{
    sp__lock();
    while (g_quarantine.head) evict(g_quarantine.head);   /* last chance to catch late writes */
    for (sp_record *r = g_live.head; r; r = r->next) {
        size_t reach = sentinel_damage(r);
        if (reach) report_overflow(r, r->alloc_at, NULL, reach, "at program exit");
    }
    analyse_leaks();

    if (g_mode != SP_MODE_SILENT) print_summary(stderr);
    if (g_log) {
        fprintf(g_log, "{\"summary\":{\"allocations\":%zu,\"frees\":%zu,\"peak_bytes\":%zu,\"issues\":%zu}}\n",
                g_stats.allocations, g_stats.frees, g_stats.peak_bytes, g_stats.issues);
        fclose(g_log);
        g_log = NULL;
    }

    if (g_reclaim) {
        while (g_live.head) {
            sp_record *r = g_live.head;
            list_remove(&g_live, r);
            if (!r->foreign) free(r->ptr);
            free_record(r);
        }
        while (g_expired.head) {
            sp_record *r = g_expired.head;
            list_remove(&g_expired, r);
            free_record(r);
        }
        free(g_buckets);
        g_buckets = NULL;
        g_nbuckets = 0;
        g_nrecords = 0;
    }
    sp__unlock();
}
