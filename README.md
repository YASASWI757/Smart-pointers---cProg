
# Smart Pointer Library for C

A reusable C library inspired by smart pointers in C++.
It provides abstractions for managing dynamically allocated
memory using explicit ownership rules.

## Features

- Unique ownership
- Borrowed pointer access
- Explicit ownership transfer
- Explicit release of ownership
- Automatic cleanup through a destructor callback
- Unit tests for ownership and memory-management behavior

Shared ownership, reference counting, and weak references
will be integrated as those modules are implemented.

## Project Structure

- `include/` - Public library headers
- `src/` - Library implementations
- `examples/` - Demonstration programs
- `tests/` - Automated tests

## Build the Unique Pointer Demo

From the project root, run:

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -Iinclude \
    src/sp_unique.c examples/unique_demo.c -o unique_demo
```

Run the generated program:

```bash
./unique_demo
```

## Run the Tests

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -Iinclude \
    src/sp_unique.c tests/test_unique.c -o test_unique
```

Run the tests:

```bash
./test_unique
```

## Ownership Rules

- A unique handle is the sole owner of its allocation.
- A pointer returned by `sp_unique_get()` is borrowed.
- `sp_unique_move()` transfers ownership to an empty handle.
- `sp_unique_release()` transfers the raw allocation to the caller.
- `sp_unique_free()` destroys an owned allocation.
- A borrowed pointer must not be used after its object is destroyed.

## Team Contributions

- Unique-pointer module and integration: [add contributor]
- Shared-pointer module and reference counting: [add contributor]

## Project Status

Unique-pointer implementation: in progress.
Shared-pointer implementation: planned.
Weak-reference extension: planned.
