# SmartPtr-C 2.0 — design

## 1. Goals

1. **Zero API for existing code.** Including one header must be enough to protect plain
   `malloc`/`free` programs.
2. **Diagnose, don't crash.** Every rule violation produces a report that names the line,
   the block, its owner, its history and a fix, and the program continues safely.
3. **Real ownership.** Unique, shared and weak ownership with every conversion, scope
   cleanup and borrowing, while a smart pointer stays an ordinary `T *`.

## 2. Architecture

```
 user code ──#include "smartptr.h"──▶ malloc/free/... macros ─┐
                                    sp_make_*/sp_move/...  ───┤
                                                              ▼
   sp_ptr.c   ownership rules (kind, strong, weak, borrows, destructor)
       │
   sp_guard.c block table · sentinels · quarantine · malloc family · exit analysis
       │                                    │
   sp_report.c  diagnosis + JSON log        └──▶ real malloc / free (libc)
```

The macros in `smartptr.h` replace `malloc(n)` with `sp__malloc(n, SP_HERE)`, where
`SP_HERE` is `{__FILE__, __LINE__, __func__}`. `free(p)` also passes `#p`, the source
text of its argument, so reports can say `free(alias)` rather than only an address.
Library sources never include `smartptr.h`, so inside `src/` the names refer to the
real C library functions.

## 3. Data structures

### 3.1 The block record (`sp_record`, `src/sp_internal.h`)

Every allocation has one record, which acts as the control block for all three
pointer kinds:

| Field | Purpose |
|---|---|
| `ptr`, `size`, `id`, `type`, `label` | identity; `id` grows monotonically and is never reused |
| `kind` | `SP_RAW`, `SP_UNIQUE` or `SP_SHARED` |
| `state` | `LIVE` → `DESTROYING` → `FREED` (quarantine) → removed, or `EXPIRED` |
| `strong`, `weak`, `borrows` | owners, weak observers, active borrows |
| `dtor`, `pending_destroy` | destructor and deferred destruction |
| `alloc_at`, `free_at`, `borrow_at` | sites |
| `history[12]`, `nevents` | ring buffer of events (kind, site, expression, owner count) |

The record lives **beside** the memory, not in front of it, so the pointer the user
receives is exactly the pointer `malloc` returned. The same pointer works as a raw,
unique or shared pointer, and conversions only change the record.

### 3.2 Block table

A power-of-two hash table keyed by address. Collisions are chained, the hash is the
64-bit mixer from MurmurHash3's finaliser, and the table doubles when the load
factor exceeds 1. Lookup is O(1) on average. A doubly linked list of live
blocks supports leak reports and audits, and finding an interior pointer, which only
happens on the error path, is O(n).

### 3.3 Sentinel bytes

Each allocation is `size + 16` bytes. The 16 bytes after the user area hold a
position-dependent pattern (`0xA5 ^ 29·i`). On release, on `realloc`, on
`sp_guard_check()` and at exit, the pattern is compared. The index of the farthest
damaged byte shows how far the overflow reached, and the fix suggests the size that
would have been needed.

### 3.4 Quarantine and poison

A freed block is not returned to libc straight away. Its user bytes are overwritten
with `0xDD` and the record joins a FIFO quarantine (by default 4096 blocks or 16 MB).
While a block is quarantined:

* its record is still in the table, so a second `free` is recognised as a **double free**
  and the report shows the first free;
* any write through a stale pointer changes the poison. When the block leaves quarantine
  (or at `sp_guard_check()` or exit), the first changed byte is reported as a
  **use-after-free** write.

`realloc` always moves the block and quarantines the old one, so a pointer kept from
before a `realloc` is caught the same way. This is a classic bug that C++ cannot detect.

## 4. Ownership rules

| Operation | Precondition | Effect |
|---|---|---|
| `sp_make_*` | size > 0, no overflow | new zeroed block, `strong = 1` |
| `sp_move(&p)` | `p` live | `*p = NULL`, event logged, the same address returned |
| `sp_share(p)` | shared | `strong++` |
| `sp_drop(&p)` | — | `*p = NULL` always; shared with `strong > 1`: `strong--`; otherwise destroy |
| `free(p)` | — | raw or unique: destroy; shared with others: `strong--` and a warning |
| `sp_to_shared` | unique | kind becomes shared |
| `sp_to_unique` | shared with `strong == 1` | kind becomes unique |
| `sp_release` | unique, or shared with one owner | kind becomes raw, destructor cleared |
| `sp_adopt[_shared]` | raw | kind becomes unique or shared |
| `sp_weak_from` | unique or shared | `weak++` |
| `sp_weak_lock` | shared, live | `strong++` |
| `sp_borrow` / `sp_unborrow` | live | `borrows±1`; at 0 with `pending_destroy`, destroy |

If a precondition fails, nothing changes, an issue is reported, and `NULL` is
returned where a pointer was expected.

### 4.1 Destruction protocol

`sp__destroy` is entered with the global lock held.

1. If `borrows > 0`: set `pending_destroy`, report a borrow conflict and return. The
   object stays valid for the borrower, and the final `sp_unborrow` destroys it.
2. Set state `DESTROYING`, then **release the lock** and call the destructor. The
   destructor may call `free()` (which takes the lock), and that must not deadlock.
3. Retake the lock, check the sentinel, poison the memory and quarantine it.

A `free` of the same block during step 2 sees `DESTROYING` and is reported as a double
free from inside the destructor.

### 4.2 Weak handles and record lifetime

`sp_weak` stores the record pointer and the block `id`. Records whose `weak > 0` are
never freed. When the memory leaves quarantine, the record moves to an `expired` list
with `ptr = NULL`, and the last `sp_weak_drop` frees it. A weak handle can therefore
never dangle, and `sp_weak_lock` simply checks `state == LIVE && kind == SHARED`.

## 5. Exit analysis

When the program exits, an `atexit` handler:

1. empties the quarantine (catching late use-after-free writes) and checks every live
   sentinel;
2. builds a graph of the leaked blocks. Every pointer-aligned word inside a leaked block
   is looked up in the table, and a hit means an edge. This is the same conservative
   scan that the Boehm garbage collector and LeakSanitizer use;
3. runs **Tarjan's strongly connected components** algorithm. A component with more than
   one block, or with a self-edge, is a **reference cycle**, printed as a path
   (`#3 Node -> #4 Node -> #3 Node`);
4. sorts the remaining blocks by allocation site and reports one issue per site. A block
   that some other leaked block points to is an **indirect** leak; the others are
   **direct**. Fixing the direct leaks also fixes the indirect ones, and the fix text
   says so;
5. writes the summary (and the JSON summary line), then frees all remaining memory
   unless `SENTINEL_RECLAIM=0`.

The fix text is chosen from what was observed. For example: the same line leaked many
times (a missing `free` in a loop), a unique owner was never dropped (use `sp_auto`),
or a shared owner was never dropped.

## 6. Concurrency

The block table, the lists and the counters are protected by a single `pthread_mutex`.
Every public operation holds it only briefly, and it is released around destructor
calls. ThreadSanitizer reports no races in `test_threads` (8 threads × 5000
share/lock/drop/malloc/free) or in example 06. A single global lock was chosen for
correctness and simplicity. Per-bucket locks with atomic reference counts are the
natural next step if throughput matters.

## 7. Complexity

| Operation | Cost |
|---|---|
| malloc / free / share / drop / lock | O(1) average (hash lookup plus constant work) |
| free cost from poisoning | O(size) |
| quarantine eviction check | O(size) |
| `sp_guard_check()` | O(total live and quarantined bytes) |
| exit analysis | O(leaked bytes + L log L) for L leaked blocks |

## 8. Testing strategy

* **Unit suites** for each module (`tests/`, 58 tests). Every rule violation is
  triggered on purpose, and the matching counter from `sp_guard_issues()` is checked.
* **Exit analysis** is tested end to end: `test_exit_report` re-runs itself as a child
  with `SENTINEL_LOG` set and checks the JSON lines (3 leak groups, 1 cycle, and the
  correct direct and indirect split).
* **Fault injection** (`sp_guard_fail_after`) covers every out-of-memory path.
* **External tools**: Valgrind (0 errors, 0 leaks for every binary), ASan + UBSan, TSan.

## 9. Limitations and future work

* Only files that include `smartptr.h` are intercepted. Linking-level interposition
  (overriding `malloc` symbols, or `LD_PRELOAD`) would cover third-party code too.
* Reads after free and underflows before a block are not detected. Guard pages
  (`mprotect`) would catch both, at a higher memory cost.
* After quarantine eviction an address may be reused, so very late double frees are
  not distinguishable. A larger quarantine trades memory for detection.
* The conservative scan can treat an integer as a pointer (false negatives in leak
  reports, never false positives in memory safety).
* `free` passed as a function pointer is the real `free`.
