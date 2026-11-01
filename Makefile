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


.PHONY: all lib test examples demo bug clean

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

# The bug without the library: plain C double free.
bug: | $(BUILD)
>$(CC) -g examples/raw_bug.c -o $(BUILD)/raw_bug
>-./$(BUILD)/raw_bug

clean:
>rm -rf build

-include $(OBJECTS:.o=.d)
