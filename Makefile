CFLAGS=-O3 -Wall -Wextra
.PHONY: clean all

all: compiler vm

compiler: compiler.c
	$(CC) $(CFLAGS) compiler.c -o compiler

vm: vm.c
	$(CC) $(CFLAGS) vm.c -o vm

clean:
	rm -f compiler vm
