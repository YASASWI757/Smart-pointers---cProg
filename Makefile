.RECIPEPREFIX := >

CC       ?= gcc
CPPFLAGS  = -Iinclude -Isrc
CFLAGS   ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -g -O1
LDLIBS    = -pthread
BUILD    ?= build

LIB      = $(BUILD)/libsmartptr.a
OBJECTS  = $(BUILD)/sp_guard.o $(BUILD)/sp_report.o $(BUILD)/sp_ptr.o
TESTS    = $(patsubst tests/%.c,$(BUILD)/%,$(wildcard tests/test_*.c))
EXAMPLES = $(patsubst examples/%.c,$(BUILD)/%,$(wildcard examples/[0-9]*.c))

VALGRIND = valgrind -q --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=1

.PHONY: all lib test examples demo valgrind asan tsan bench bug clean

all: lib $(TESTS) $(EXAMPLES)

lib: $(LIB)

$(BUILD):
>mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c src/sp_internal.h $(wildcard include/*.h) | $(BUILD)
>$(CC) $(CPPFLAGS) $(CFLAGS) -pthread -c $< -o $@

$(LIB): $(OBJECTS)
>ar rcs $@ $^

$(BUILD)/test_%: tests/test_%.c tests/check.h $(LIB)
>$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(BUILD)/%: examples/%.c $(LIB)
>$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

test: $(TESTS)
>@for t in $(TESTS); do ./$$t || exit 1; done
>@echo "ALL TEST SUITES PASSED"

examples: $(EXAMPLES)

# The showcase: every example, with Sentinel's reports.
demo: $(EXAMPLES)
>@for e in $(EXAMPLES); do echo; echo "################ $$e"; ./$$e; done

valgrind: $(TESTS) $(EXAMPLES)
>@for t in $(TESTS); do $(VALGRIND) ./$$t > /dev/null || exit 1; done
>@for e in $(EXAMPLES); do SENTINEL=silent $(VALGRIND) ./$$e > /dev/null || exit 1; done
>@echo "VALGRIND: 0 errors, 0 leaks in every test and example"

asan:
>$(MAKE) BUILD=build/asan CFLAGS="-std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer" LDLIBS="-pthread -fsanitize=address,undefined" test

tsan:
>$(MAKE) BUILD=build/tsan CFLAGS="-std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=thread" LDLIBS="-pthread -fsanitize=thread" build/tsan/test_threads build/tsan/06_threads
>./build/tsan/test_threads
>SENTINEL=summary ./build/tsan/06_threads

bench: $(LIB)
>$(CC) $(CPPFLAGS) -std=c11 -O2 bench/bench.c $(LIB) $(LDLIBS) -o $(BUILD)/bench
>./$(BUILD)/bench

# The bug without the library: plain C double free.
bug: | $(BUILD)
>$(CC) -g examples/raw_bug.c -o $(BUILD)/raw_bug
>-./$(BUILD)/raw_bug

clean:
>rm -rf build

-include $(OBJECTS:.o=.d)
