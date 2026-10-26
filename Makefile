.RECIPEPREFIX := >

CC = gcc
AR = ar

CPPFLAGS = -Iinclude -Isrc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -g
LDFLAGS =
LDLIBS =

BUILD = build

LIB = $(BUILD)/libsmartptr.a
OBJECTS = $(BUILD)/sp_common.o $(BUILD)/sp_unique.o $(BUILD)/sp_shared.o
TESTS = $(BUILD)/test_common $(BUILD)/test_allocator $(BUILD)/test_unique $(BUILD)/test_shared
DEMOS = $(BUILD)/unique_demo $(BUILD)/shared_demo $(BUILD)/midterm_demo

VALGRIND = valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=1

.PHONY: all lib test demo asan valgrind bug clean

all: lib $(TESTS) $(DEMOS)

lib: $(LIB)

$(BUILD):
>mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c | $(BUILD)
>$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(LIB): $(OBJECTS)
>$(AR) rcs $@ $^

$(BUILD)/test_%: tests/test_%.c $(LIB) $(wildcard include/*.h src/*.h)
>$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(BUILD)/%_demo: examples/%_demo.c $(LIB) $(wildcard include/*.h)
>$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< $(LIB) $(LDLIBS) -o $@

test: $(TESTS)
>$(BUILD)/test_common
>$(BUILD)/test_allocator
>$(BUILD)/test_unique
>$(BUILD)/test_shared

demo: $(DEMOS)
>$(BUILD)/midterm_demo
>$(BUILD)/shared_demo

asan:
>$(MAKE) BUILD=build/asan CFLAGS="$(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer" LDFLAGS="$(LDFLAGS) -fsanitize=address,undefined" test demo

valgrind: $(TESTS) $(DEMOS)
>$(VALGRIND) $(BUILD)/test_common
>$(VALGRIND) $(BUILD)/test_allocator
>$(VALGRIND) $(BUILD)/test_unique
>$(VALGRIND) $(BUILD)/test_shared
>$(VALGRIND) $(BUILD)/midterm_demo
>$(VALGRIND) $(BUILD)/shared_demo

# The problem without the library: plain double free (expected to crash).
bug: | $(BUILD)
>$(CC) -g examples/raw_bug.c -o $(BUILD)/raw_bug
>-$(BUILD)/raw_bug

clean:
>rm -rf build
>rm -f unique_demo.exe test_unique.exe

-include $(OBJECTS:.o=.d)
