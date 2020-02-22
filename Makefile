CFLAGS=-O3 -Wall -Wextra
.PHONY: clean all

all: compiler vm

compiler:
	$(CC) $(CFLAGS) compiler.c -o compiler

vm:
	$(CC) $(CFLAGS) vm.c -o vm

clean:
	rm -f compiler vm
