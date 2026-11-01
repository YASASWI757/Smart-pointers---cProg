# SmartPtr-C 2.0 — smart pointers and a memory sentinel for C

**Team Error-404 · Project 12 · Saketh N · S Mithil Reddy · Sai Yasaswi**

SmartPtr-C works in two layers.

1. **Sentinel (zero API).** Add `#include "smartptr.h"` and keep writing ordinary
   `malloc` / `calloc` / `realloc` / `strdup` / `free`. Every allocation is watched.
   Double frees are blocked, overflows, use-after-free writes, invalid frees and leaks
   are diagnosed with the exact line, the block's history and a suggested fix, and
   whatever is still leaked at exit is reclaimed.
2. **Ownership.** Unique, shared and weak pointers, with every conversion between them,
   scope cleanup (`sp_auto`) and borrowing. A smart pointer is just an ordinary
   `Student *`, so `->`, `[]` and passing it to functions all work as usual.

```c
#include "smartptr.h"          /* the only change to your program */

char *greeting = malloc(5);
strcpy(greeting, "Hello");     /* 6 bytes into 5 */
free(greeting);
```

```
┌─ SENTINEL ─ BUFFER OVERFLOW (error #2)
│ what      buffer overflow: memory after the block was overwritten
│ where     01_zero_api.c:47 in main()  ->  free(greeting)
│ block     #3, 5 bytes
│ owner     raw pointer
│ created   01_zero_api.c:45 in main()
│ timeline   1. malloc    01_zero_api.c:45 main()
│            2. free      01_zero_api.c:47 main() `greeting`
│ detail    at least 1 byte(s) were written past the end of this 5-byte block
│           (found when the block was released)
│ fix       check the code that writes into this block: strcpy/sprintf need
│           strlen+1 bytes for the '\0', and loops over N items must use i < N,
│           not i <= N. Allocate 6 bytes or more, or write less.
└─ reported; the block is still released normally
```

---

## Quick start (Ubuntu / WSL)

```bash
sudo apt install build-essential valgrind
cd smartptr
make            # library, 8 test suites, 6 examples (warnings are errors)
make test       # 58 tests
make demo       # every example with Sentinel's reports
make valgrind   # every test and example: 0 errors, 0 leaks
make asan       # tests under AddressSanitizer + UndefinedBehaviorSanitizer
make tsan       # thread tests under ThreadSanitizer
make bench      # overhead numbers
make bug        # the same bug in plain C, for comparison: it crashes
```

To use it in your own program:

```bash
gcc -Iinclude myprog.c build/libsmartptr.a -pthread -o myprog
```

---

## Layer 1 — Sentinel: what it catches with no code changes

| Problem | When it is detected | What happens |
|---|---|---|
| Double free | at the second `free` | blocked; report shows where it was first freed |
| Invalid free (stack, global, literal) | at the `free` | blocked |
| Free of an interior pointer (`free(p + 4)`) | at the `free` | blocked; report shows the real start address |
| Buffer overflow past the end | at `free`, `realloc`, `sp_guard_check()` or exit | reported with how many bytes overflowed and the size needed |
| Use after free (write) | when the block leaves quarantine, at `sp_guard_check()` or exit | reported with the byte offset that changed |
| Stale pointer after `realloc` moved the block | same as use after free | reported |
| Memory leak | at exit | grouped by the line that allocated, split into direct and indirect leaks |
| Reference cycle | at exit | the loop is printed, e.g. `#3 Node -> #4 Node -> #3 Node` |
| `free()` on a shared object | at the `free` | only that one reference is dropped; the others keep working |
| Allocation failure | at the call | `NULL` returned, noted (real, or injected for testing) |

Every report contains the same parts:

| Field | Meaning |
|---|---|
| `what` | the problem, in one line |
| `where` | file, line, function **and the source text of the call**, e.g. `free(alias)` |
| `block` | a unique id, the size and, for smart pointers, the type name |
| `owner` | raw / unique / shared (number of owners), weak observers, borrows, name |
| `created` / `released` | where the block was allocated and freed |
| `timeline` | the block's recent history: every malloc, move, share, drop, lock, borrow |
| `detail` | the evidence (bytes overflowed, offset written, owner count...) |
| `fix` | a concrete suggestion, chosen from what Sentinel observed |
| last line | what Sentinel did about it (blocked, deferred, reclaimed...) |

At exit a summary is printed:

```
== SENTINEL SUMMARY ==========================================
  8 allocations, 5 frees, 153 bytes allocated in total, peak 96 bytes live
  5 issue(s): 1 double free, 1 interior free, 1 buffer overflow, 1 use after free, 1 memory leak
===============================================================
```

### Settings (environment variables)

| Variable | Values | Effect |
|---|---|---|
| `SENTINEL` | `report` (default), `summary`, `silent`, `abort` | how much is printed; `abort` stops at the first error (useful in a debugger or CI) |
| `SENTINEL_LOG` | a file path | also writes one JSON line per issue plus a summary line, for tools and CI |
| `SENTINEL_RECLAIM` | `0` | do not free leaked blocks at exit |
| `SENTINEL_FAIL_AT` | `n` | make the n-th allocation fail, to test out-of-memory paths |
| `SENTINEL_QUARANTINE` | `n` | how many freed blocks are held back for checking (default 4096) |
| `NO_COLOR` | any | plain text reports |

The same settings are available from code: `sp_guard_set_mode`, `sp_guard_fail_after`,
`sp_guard_check`, `sp_guard_dump(p)` (prints one block's life story), `sp_guard_stats`,
`sp_guard_issues(kind)`, `sp_guard_summary`.

---

## Layer 2 — ownership

### Create

```c
Student *s = sp_make_unique(Student);                 /* zeroed, one owner      */
Student *s = sp_make_unique_d(Student, student_dtor); /* with a destructor      */
int     *v = sp_make_unique_array(int, 100);
Course  *c = sp_make_shared(Course);                  /* reference counted      */
Course  *c = sp_make_shared_d(Course, course_dtor);
double  *d = sp_make_shared_array(double, 16);
```

The destructor frees what is *inside* the object (for example a `name` string); the
library then frees the object itself.

### Own, share, release

| Call | Meaning |
|---|---|
| `q = sp_move(&p)` | hand ownership to `q`; `p` becomes `NULL` |
| `q = sp_share(p)` | `q` becomes another owner of a shared object (count + 1) |
| `sp_drop(&p)` | give up your ownership and set `p = NULL`; the last owner destroys the object |
| `free(p)` | also works: on a shared object it drops only your reference |

### Every conversion

```
            sp_adopt                  sp_to_shared               sp_weak_from
   raw  ───────────────▶  unique  ───────────────▶  shared  ───────────────▶  weak
   ▲   ◀───────────────          ◀───────────────          ◀───────────────
   │      sp_release              sp_to_unique              sp_weak_lock
   │                           (only with 1 owner)        (NULL if expired)
   └──── sp_adopt_shared: raw ─▶ shared directly
```

| Call | From → to | Rule |
|---|---|---|
| `sp_adopt(p, dtor)` | raw → unique | `p` came from `malloc` |
| `sp_adopt_shared(p, dtor)` | raw → shared | `p` came from `malloc` |
| `sp_to_shared(&u)` | unique → shared | always allowed; `u` becomes `NULL` |
| `sp_to_unique(&s)` | shared → unique | only when it is the **only** owner (C++ cannot do this) |
| `sp_release(&u)` | unique → raw | you `free()` it yourself afterwards |
| `sp_weak_from(p)` | unique or shared → weak | observes without owning |
| `sp_weak_lock(&w)` | weak → shared | a new owner, or `NULL` if the object is gone |
| `sp_weak_peek(&w)` | weak → plain pointer | look without owning; works for unique objects too |

Weak handles are copied with `sp_weak_copy(&w)` and released with `sp_weak_drop(&w)`.

### Scope cleanup: `sp_auto`

```c
int parse(const char *input) {
    sp_auto char    *copy = strdup(input);                  /* freed on every return */
    sp_auto Buffer  *work = sp_make_unique_d(Buffer, dtor); /* dropped on every return */
    if (copy[0] == '\0') return -1;                         /* nothing leaks here */
    ...
}
```

`sp_auto` works on plain `malloc` memory and on smart pointers. Use `sp_auto_weak` for
weak handles. (It uses the `cleanup` attribute supported by GCC and Clang.)

### Borrowing and deferred destruction

```c
Buffer *view = sp_borrow(owner);   /* "I am using this" */
sp_drop(&owner);                   /* owner lets go during the borrow */
puts(view->text);                  /* still valid: destruction was deferred */
sp_unborrow(view);                 /* the object is destroyed now */
```

### Queries

`sp_use_count(p)`, `sp_weak_count(p)`, `sp_kind_of(p)`, `sp_id(p)`, `sp_kind_name(kind)`,
`sp_name(p, "label")` (the name appears in reports).

---

## Beyond C++ smart pointers

| | C++ `unique_ptr` / `shared_ptr` / `weak_ptr` | SmartPtr-C 2.0 |
|---|---|---|
| Works on existing `malloc`/`free` code unchanged | no | **yes** (Sentinel) |
| Double free | undefined behaviour | **blocked and explained** |
| Using a moved-from or dropped object | undefined behaviour | **blocked and explained** |
| Error output | crash, or nothing | **diagnosis: where, which block, owner, history, fix** |
| Buffer overflow / use-after-free write detection | no | **yes** (sentinel bytes, poisoned quarantine) |
| Leak report | no | **grouped by line, direct vs indirect** |
| Reference cycle | silent leak | **found at exit and printed as a loop** |
| shared → unique conversion | impossible | **allowed when there is exactly one owner** |
| Observe a unique object safely | no (`weak_ptr` needs `shared_ptr`) | **`sp_weak_peek` on unique objects** |
| Owner releases while someone is using it | dangling pointer | **destruction deferred until `sp_unborrow`** |
| Wrong `free()` on a shared object | undefined behaviour | **only that reference is dropped** |
| Out-of-memory testing | external tools | **built in: `SENTINEL_FAIL_AT`** |
| Machine-readable diagnostics | no | **JSON lines (`SENTINEL_LOG`)** |
| Object = ordinary pointer | no (wrapper type) | **yes: `Student *`** |

What C++ does better, honestly: its smart pointers cost nothing extra and are checked
by the compiler. SmartPtr-C checks at run time and costs more (see *Performance*), so
it is built for development, teaching and testing, where finding the bug matters most.

---

## Project layout

```
include/smartptr.h     the only header users include (turns interception on)
include/sp_guard.h     Sentinel API: modes, stats, checks, issue kinds
include/sp_ptr.h       unique / shared / weak / conversions / borrow / sp_auto
src/sp_guard.c         block table, sentinels, quarantine, malloc family, leak + cycle analysis
src/sp_report.c        report formatting, timelines, JSON log
src/sp_ptr.c           ownership rules on top of the block table
src/sp_internal.h      private record structure (the "control block")
tests/                 8 suites, 58 tests (+ check.h, a tiny harness)
examples/01..06        zero-API bugs, unique, shared+weak+cycle, conversions, sp_auto+borrow, threads
examples/raw_bug.c     the plain C double free, without the library
bench/bench.c          overhead measurement
docs/DESIGN.md         how it works inside
```

## Verification

| Check | Result |
|---|---|
| `make` with `-std=c11 -Wall -Wextra -Wpedantic -Werror` | clean |
| `make test` | 58 / 58 tests pass |
| `make valgrind` (every test and example) | 0 errors, 0 leaks |
| `make asan` (AddressSanitizer + UBSan) | clean |
| `make tsan` (8 threads sharing objects) | no data races |

## Performance

From `make bench` (1,000,000 operations, `-O2`):

| Operation | Time per pair |
|---|---|
| plain `malloc`/`free` | ~9 ns |
| guarded `malloc`/`free` | ~270 ns |
| `sp_share`/`sp_drop` | ~115 ns |

That is the same order of cost as other memory checkers. Define `SP_NO_INTERCEPT`
before including `smartptr.h` to switch the guard off for a file.

## Limitations

- Interception works in files that include `smartptr.h`. Memory freed by a library that
  was compiled without it bypasses Sentinel.
- `free` used as a value (for example passed as a function pointer) is the real `free`,
  not the macro, so pass guarded memory only to code that frees through the macro.
- A struct member named `free`, `malloc`, etc. clashes with the macros; define
  `SP_NO_INTERCEPT` in that file.
- Reads after free and writes *before* the start of a block are not detected. Overflow
  detection covers the 16 bytes after each block; a write that jumps further can land in
  other memory unnoticed.
- A double free is recognised while the block is in quarantine (the last 4096 frees by
  default). After that its address may be reused.
- Leak and cycle analysis scans memory conservatively, so an integer that happens to
  equal a block address can count as a pointer.
