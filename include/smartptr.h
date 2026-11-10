/*
 * smartptr.h - the only header you need.
 *
 *     #include "smartptr.h"
 *
 * From this line on, malloc / calloc / realloc / strdup / strndup / free in
 * this file are watched by Sentinel: double frees are blocked, overflows,
 * use-after-free writes, invalid frees and leaks are reported with the exact
 * line that caused them, and anything still leaked is reclaimed at exit.
 *
 * Define SP_NO_INTERCEPT before including to turn the interception off for a
 * file (smart pointers keep working).
 */
#ifndef SMARTPTR_H
#define SMARTPTR_H

/* Pull in the real declarations first, so the macros below never clash. */
#include <stdlib.h>
#include <string.h>

#include "sp_guard.h"
#include "sp_ptr.h"

#ifndef SP_NO_INTERCEPT
#  undef malloc
#  undef calloc
#  undef realloc
#  undef free
#  undef strdup
#  undef strndup
#  define malloc(size)        sp__malloc((size), SP_HERE)
#  define calloc(count, size) sp__calloc((count), (size), SP_HERE)
#  define realloc(ptr, size)  sp__realloc((ptr), (size), SP_HERE, #ptr)
#  define free(ptr)           sp__free((ptr), SP_HERE, #ptr)
#  define strdup(text)        sp__strdup((text), SP_HERE)
#  define strndup(text, max)  sp__strndup((text), (max), SP_HERE)
#endif

#endif
