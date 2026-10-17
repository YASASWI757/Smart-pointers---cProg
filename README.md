# Smart Pointer Library for C

A C11 library for managing dynamically allocated objects through
explicit ownership rules.

## Current status

Implemented and tested:
- Common status codes and readable error messages
- Allocator hooks and managed-allocation tracking
- Unique ownership: make, adopt, get, move, release, reset and free
- Static library build
- Unit tests, sanitizer checks and Valgrind targets

Pending integration:
- Shared ownership and reference counting
- Weak references
- Unique-to-shared conversion

## Requirements

- GCC or Clang
- GNU Make
- Valgrind for memory checks
- Linux or WSL Ubuntu for the documented commands

## Build and run

Run from the repository root:

```bash
make all
make test
make demo
make asan
make valgrind
```

Build only the static library:

```bash
make lib
```

The library is generated at `build/libsmartptr.a`.

Remove generated files:

```bash
make clean
```

## Using the library

Include the public entry header:

```c
#include "smartptr.h"
```

Example:

```c
#include <stdio.h>
#include "smartptr.h"

int main(void) {
    sp_unique owner = SP_UNIQUE_EMPTY;

    sp_status status = sp_unique_make(&owner, sizeof(int), NULL);

    if (status != SP_OK) {
        fprintf(stderr, "%s\n", sp_strerror(status));
        return 1;
    }

    int *value = sp_unique_get(&owner);
    *value = 42;
    printf("%d\n", *value);

    return sp_unique_free(&owner) == SP_OK ? 0 : 1;
}
```

After building the library, compile an application saved as `app.c`:

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -Iinclude \
    app.c build/libsmartptr.a -o app
```

## Ownership rules

| Operation | Ownership effect |
|---|---|
| make | Allocates into an empty handle |
| adopt | Takes ownership of an existing compatible allocation |
| get | Borrows access; ownership stays with the handle |
| move | Transfers ownership into an empty destination |
| release | Transfers ownership to the caller |
| reset | Destroys the old object and takes a replacement |
| free | Destroys the object and empties the handle |

Initialize every handle with `SP_UNIQUE_EMPTY`.

Never copy owning handles with ordinary structure assignment.
Use `sp_unique_move`.

The destructor callback cleans nested resources. The library then
deallocates the outer object. Cleanup is explicitly triggered by API
calls; standard C does not automatically clean handles at scope exit.

Adopted and replacement allocations must be compatible with the
currently configured deallocator and must not have another owner.

After release, the caller must perform any required nested cleanup and
use the allocation's matching deallocator.

A borrowed pointer must not be used after its object is destroyed.

## Allocation tracking

`sp_live_allocations()` counts allocations currently managed by the
library, including adopted allocations.

Released allocations and application allocations not registered with
the library are excluded. The counter complements memory tools; it is
not a system-wide leak detector.

Allocator hook changes are rejected while tracked allocations exist.

## Project structure

- `include/`: Public headers
- `src/`: Implementations and private declarations
- `tests/`: Common, allocator and unique tests
- `examples/`: Demonstration programs
- `documentation/`: Module documentation
- `build/`: Generated files, ignored by Git

The current Makefile builds the common and unique modules.
Shared-related files are placeholders pending integration.

## Verification

The current tests and unique demo passed:
- GCC compilation with warnings treated as errors
- AddressSanitizer and UndefinedBehaviorSanitizer
- Valgrind with zero reported errors and no remaining allocations

Tests include allocation failure injection, ownership transitions,
nested-resource destruction and 100,000 make/free cycles.

These results cover the exercised test paths.

## Limitations

- Single-threaded implementation
- Raw handle copies can violate ownership
- Borrowed pointers are not tracked
- Validation markers do not make uninitialized handles safe
- Allocator compatibility is the caller's responsibility
- No automatic scope-exit cleanup

## Team responsibilities

- N Saketh: Common layer, unique ownership, Makefile, unique tests
  and unique documentation
- S Mithil Reddy: Shared ownership, control block and weak references
- Sai Yasaswi: Test framework, system-level memory tests, demo,
  CI and overall documentation
- Shared public API and integration: All members

## Documentation and credits

See [Unique module documentation](documentation/unique.md).

Ownership operations are inspired by C++ smart-pointer concepts.
Record external code, references and assistance according to the
course's attribution requirements.