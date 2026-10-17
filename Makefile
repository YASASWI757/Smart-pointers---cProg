.RECIPEPREFIX := >

CC = gcc
AR = ar

CPPFLAGS = -Iinclude -Isrc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -g
LDFLAGS =
LDLIBS =

BUILD = build

LIB = $(BUILD)/libsmartptr.a
OBJECTS = $(BUILD)/sp_common.o $(BUILD)/sp_unique.o
TESTS = $(BUILD)/test_common $(BUILD)/test_allocator $(BUILD)/test_unique
DEMO = $(BUILD)/unique_demo

.PHONY: all lib test demo asan valgrind clean

all: lib $(TESTS) $(DEMO)

lib: $(LIB)

$(BUILD):
>mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c | $(BUILD)
>$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(LIB): $(OBJECTS)
>$(AR) rcs $@ $^

$(BUILD)/test_%: tests/test_%.c $(LIB) $(wildcard include/*.h src/*.h)
>$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(DEMO): examples/unique_demo.c $(LIB) $(wildcard include/*.h)
>$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< $(LIB) $(LDLIBS) -o $@

test: $(TESTS)
>$(BUILD)/test_common
>$(BUILD)/test_allocator
>$(BUILD)/test_unique

demo: $(DEMO)
>$(DEMO)

asan:
>$(MAKE) BUILD=build/asan CFLAGS="$(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer" LDFLAGS="$(LDFLAGS) -fsanitize=address,undefined" test demo

valgrind: $(TESTS) $(DEMO)
>valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=1 $(BUILD)/test_common
>valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=1 $(BUILD)/test_allocator
>valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=1 $(BUILD)/test_unique
>valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=1 $(DEMO)

clean:
>rm -rf build
>rm -f unique_demo.exe test_unique.exe

-include $(OBJECTS:.o=.d)
