# Unique ownership and common layer

Author: N Saketh

## Scope

This module implements unique ownership, common status messages,
allocator hooks, allocation tracking and build targets.

The unique-to-shared conversion requires integration with the
shared-pointer control block and is not implemented in this module yet.

## Public interface

Applications include:

```c
#include "smartptr.h"
```

Every unique handle must be initialized:

```c
sp_unique owner = SP_UNIQUE_EMPTY;
```

A valid handle is either empty or the sole owner of an allocation.
An empty handle has a NULL pointer, a NULL destructor and a valid marker.

Never duplicate an owning handle using ordinary structure assignment.
Use sp_unique_move to transfer ownership.

## Operations

| Function | Behavior |
|---|---|
| sp_unique_make | Allocate into an empty handle; size must be positive |
| sp_unique_adopt | Take ownership of an existing compatible allocation |
| sp_unique_get | Borrow access without changing ownership |
| sp_unique_move | Transfer ownership into an empty destination |
| sp_unique_release | Return ownership through a void ** output parameter |
| sp_unique_reset | Destroy the old object and adopt a replacement |
| sp_unique_free | Destroy the object and empty the handle |

All operations return sp_status except sp_unique_get, which returns
a borrowed pointer or NULL.

Allocation contents are uninitialized. Initialize fields before reading
them or invoking a destructor that reads them.

Move into an occupied destination returns SP_ERR_NOT_EMPTY and leaves
both handles unchanged. Self-move succeeds without changes.

Reset to the currently owned pointer is a no-op that preserves its
existing destructor. Reset to NULL empties the handle.

Freeing an empty valid handle succeeds. Repeated cleanup of that same
handle is harmless.

Adopt and reset accept NULL as an empty allocation. On failure, the
caller retains ownership of the supplied replacement.

## Ownership and destruction

The destructor callback cleans nested resources only.
The library then deallocates the outer object.

For a structure containing a separately allocated name, the callback
frees the name; it must not free the structure itself.

Callbacks must not modify the handle being cleaned up.

Borrowed pointers remain usable only while the object remains alive.
Moving ownership does not relocate the object. Free or replacement
invalidates existing borrows to the destroyed object.

Cleanup is explicit. Standard C does not automatically call these
functions when a variable leaves scope.

## Release

```c
void *raw = NULL;
sp_status status = sp_unique_release(&owner, &raw);
```

On success, owner becomes empty and the caller owns raw.
Releasing an empty handle succeeds and writes NULL.

On error, the output variable remains unchanged.

Use a separate void * output variable. Do not cast int ** to void **,
and do not use a handle field as the output variable.

Release does not call the destructor. The caller must perform any
required nested cleanup and use the matching deallocator.
Save the cleanup policy before release if needed.

## Allocator compatibility

The default allocation pair is malloc/free.

sp_set_allocator installs a compatible allocation/deallocation pair.
Passing NULL, NULL restores the defaults. Passing only one NULL
argument returns SP_ERR_NULL_ARG.

Changing hooks while tracked allocations exist returns SP_ERR_NOT_EMPTY.

Adopted and replacement pointers must be compatible with the currently
configured deallocator. They must not already be owned by another handle.

Do not adopt stack objects, string literals or FILE pointers.
A fclose callback followed by ordinary free is not a valid FILE cleanup
policy for this API.

After release, use the allocation's original matching deallocator,
even if the library hooks later change.

## Allocation tracking

sp_live_allocations counts allocations currently managed by the library.

| Action | Counter effect |
|---|---|
| Successful make | +1 |
| Adopt non-NULL allocation | +1 |
| Move | No change |
| Release nonempty handle | -1 |
| Free nonempty handle | -1 |
| Replace non-NULL object with another | No net change |

Failed allocations and NULL cleanup do not change the count.

Released allocations and nested resources allocated directly by the
application are not counted. A zero count alone does not prove that an
application has no leaks; memory tools provide additional checks.

Internal modules use sp__alloc and sp__free for library allocations.
External ownership registration uses sp__track_external, and release
uses sp__untrack. Do not register the same allocation twice.

## Errors and validation

Common results include SP_OK, SP_ERR_NULL_ARG, SP_ERR_INVALID_SIZE,
SP_ERR_NOT_EMPTY, SP_ERR_NO_MEMORY and SP_ERR_INVALID_HANDLE.

Use sp_strerror to obtain a readable message. Its returned strings must
not be freed.

The handle marker detects deliberately initialized invalid handles.
It does not make uninitialized variables, dangling handle pointers or
arbitrary addresses safe to inspect.

The marker cannot detect ordinary copies of valid owning handles.

## Build and verification

Run from the repository root:

```bash
make all
make test
make demo
make asan
make valgrind
```

The static library is build/libsmartptr.a.

The current Makefile builds the common and unique modules.
Shared and weak modules require later integration.

Tests cover:
- make, borrow and free
- move, self-move and occupied destination rejection
- adopt, failed adoption and reset
- release and output argument validation
- empty and invalid handles
- repeated cleanup and exactly-once destruction
- nested-resource cleanup
- allocation failure injection
- allocation counter transitions
- allocator hook restrictions
- 100,000 make/free cycles

On the development machine, these tests and the example passed
AddressSanitizer/UndefinedBehaviorSanitizer and Valgrind.
Valgrind reported zero errors and no remaining allocations for the
tested programs. These results apply to the exercised paths.

## Limitations

- Single-threaded; counters and hooks are not synchronized.
- Raw structure copies can violate ownership rules.
- Borrowed raw pointers are not tracked.
- Allocator compatibility is the caller's responsibility.
- No automatic scope-exit cleanup.
- Unique-to-shared conversion remains pending integration.

## Design references

Ownership operations are inspired by C++ unique_ptr concepts.
The implementation uses explicit C functions rather than C++ lifetime
mechanisms.