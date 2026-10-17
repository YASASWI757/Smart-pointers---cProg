
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -g -Iinclude

.PHONY: all demo test clean

all: demo test

demo: examples/unique_demo.c src/sp_unique.c include/sp_unique.h
	$(CC) $(CFLAGS) src/sp_unique.c examples/unique_demo.c -o unique_demo.exe

test: tests/test_unique.c src/sp_unique.c include/sp_unique.h
	$(CC) $(CFLAGS) src/sp_unique.c tests/test_unique.c -o test_unique.exe
	./test_unique.exe

clean:
	rm -f unique_demo.exe test_unique.exe
