/*
 * sp_guard.h - Sentinel: the always-on memory guard behind SmartPtr-C.
 *
 * You normally never include this file directly: include "smartptr.h" and
 * your ordinary malloc / calloc / realloc / strdup / free calls are routed
 * here automatically, together with the file, line and function they came
 * from.  Everything below that starts with "sp_guard_" is optional tooling.
 */
#ifndef SP_GUARD_H
#define SP_GUARD_H

#include <stddef.h>
#include <stdint.h>

/* A source location, captured automatically by the macros in smartptr.h. */
typedef struct {
    const char *file;
    int line;
    const char *func;
} sp_site;

#define SP_HERE ((sp_site){ __FILE__, __LINE__, __func__ })

/* Every kind of problem Sentinel can diagnose. */
typedef enum {
    SP_ISSUE_DOUBLE_FREE,     /* the same block released twice              */
    SP_ISSUE_INVALID_FREE,    /* free() of an address malloc never returned  */
    SP_ISSUE_INTERIOR_FREE,   /* free() of a pointer into the middle of a block */
    SP_ISSUE_OVERFLOW,        /* bytes written past the end of a block       */
    SP_ISSUE_USE_AFTER_FREE,  /* a freed block was written to or used again  */
    SP_ISSUE_LEAK,            /* a block was never released                  */
    SP_ISSUE_CYCLE,           /* leaked blocks that point at each other      */
    SP_ISSUE_OWNERSHIP,       /* raw free() fighting a smart owner           */
    SP_ISSUE_MISUSE,          /* a smart-pointer rule was broken             */
    SP_ISSUE_BORROW,          /* destroy / realloc while a borrow is active  */
    SP_ISSUE_ALLOC_FAIL,      /* allocation failed (real or injected)        */
    SP_ISSUE_COUNT_
} sp_issue;

/* How loudly Sentinel speaks.  Also settable with SENTINEL=report|summary|silent|abort */
typedef enum {
    SP_MODE_REPORT,   /* full diagnostic for every issue + summary at exit (default) */
    SP_MODE_SUMMARY,  /* only the summary at exit                                    */
    SP_MODE_SILENT,   /* print nothing; issues are still counted                     */
    SP_MODE_ABORT     /* print the first error, then abort() (for CI / debuggers)    */
} sp_mode;

typedef struct {
    size_t allocations;        /* blocks handed out                    */
    size_t frees;              /* blocks released                      */
    size_t live_blocks;        /* currently allocated                  */
    size_t live_bytes;
    size_t peak_bytes;         /* high-water mark of live_bytes        */
    size_t total_bytes;        /* sum of every allocation ever made    */
    size_t quarantined_blocks; /* freed blocks held back for checking  */
    size_t issues;             /* errors + warnings reported so far    */
} sp_stats;

/* ---- entry points used by the interception macros (do not call directly) ---- */
void *sp__malloc(size_t size, sp_site at);
void *sp__calloc(size_t count, size_t size, sp_site at);
void *sp__realloc(void *ptr, size_t size, sp_site at, const char *expr);
void  sp__free(void *ptr, sp_site at, const char *expr);
char *sp__strdup(const char *text, sp_site at);
char *sp__strndup(const char *text, size_t max, sp_site at);

/* ---- optional tooling ---- */
void        sp_guard_set_mode(sp_mode mode);
sp_mode     sp_guard_mode(void);
sp_stats    sp_guard_stats(void);
size_t      sp_guard_issues(sp_issue kind);   /* how many of this kind so far   */
size_t      sp_guard_check(void);             /* audit every block right now    */
void        sp_guard_dump(const void *ptr);   /* print the life story of a block */
void        sp_guard_fail_after(long n);      /* n more allocations succeed, then one fails; -1 = off */
void        sp_guard_summary(void);           /* print the summary now          */
const char *sp_issue_name(sp_issue kind);

#endif
