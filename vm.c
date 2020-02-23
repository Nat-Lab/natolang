#include <memory.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "vm.h"
#include "log.h"

void _vm_ps(int32_t *ptr) {
    for (; *ptr != 0; ptr++) printf("%c", (char) *ptr);
}

int vm (int32_t *mem,int32_t sp, int debug) {
    int32_t cycles = 0;
    int32_t acc = 0;
    int32_t pc = 0, fp = 0;
    int32_t init_sp = sp;

    while (1) {
        cycles++;
        int32_t i = mem[pc++];
        if (debug) {
            fprintf(stderr, "%09d: pc: %6d, sp: %6d, fp: %6d, a: %6d, i: %3d | %-3.3s", cycles, pc - 1, sp, fp, acc, i,
                &"ARGADJIMMJ  JS JZ JNZADISBIMUIDIIMDIEQINEIGTILTIGEILEIANIORIADDSUBMULDIVMODEQ NE GT LT GE LE ANDOR NOTLD SV LA PSHPOPSRESRSPSIPSCPAIPACGC EXTNOP"[i*3]
            );
            if (i < ADD) fprintf(stderr, " %d", mem[pc]);
            fprintf(stderr, "\n");
        }
        
        /**/ if (i == ARG) acc = mem[fp + mem[pc++]];
        else if (i == ADJ) sp += mem[pc++];
        else if (i == IMM) acc = mem[pc++];
        else if (i == J  ) pc = mem[pc];
        else if (i == JS ) { mem[--sp] = pc + 1; pc = mem[pc]; }
        else if (i == JZ ) pc = (acc == 0) ? mem[pc] : pc + 1;
        else if (i == JNZ) pc = (acc != 0) ? mem[pc] : pc + 1;
        else if (i == ADI) acc += mem[pc++];
        else if (i == SBI) acc -= mem[pc++];
        else if (i == MUI) acc *= mem[pc++];
        else if (i == DII) acc /= mem[pc++];
        else if (i == MDI) acc %= mem[pc++];
        else if (i == EQI) acc = acc == mem[pc++];
        else if (i == NEI) acc = acc != mem[pc++];
        else if (i == GTI) acc = acc > mem[pc++];
        else if (i == LTI) acc = acc < mem[pc++];
        else if (i == GEI) acc = acc >= mem[pc++];
        else if (i == LEI) acc = acc <= mem[pc++];
        else if (i == ANI) acc = acc && mem[pc++];
        else if (i == ORI) acc = acc || mem[pc++];
        else if (i == ADD) acc += mem[sp++];
        else if (i == SUB) acc = mem[sp++] - acc;
        else if (i == MUL) acc *= mem[sp++];
        else if (i == DIV) acc = mem[sp++] / acc;
        else if (i == MOD) acc = mem[sp++] % acc;
        else if (i == EQ ) acc = acc == mem[sp++];
        else if (i == NE ) acc = acc != mem[sp++];
        else if (i == GT ) acc = acc < mem[sp++];
        else if (i == LT ) acc = acc > mem[sp++];
        else if (i == GE ) acc = acc <= mem[sp++];
        else if (i == LE ) acc = acc >= mem[sp++];
        else if (i == LE ) acc = acc >= mem[sp++];
        else if (i == AND) acc = mem[sp++] && acc;
        else if (i == OR ) acc = mem[sp++] || acc;
        else if (i == NOT) acc = !acc;
        else if (i == LD ) acc = mem[acc];
        else if (i == SV ) mem[mem[sp++]] = acc;
        else if (i == LA ) acc = mem[fp + acc];
        else if (i == PSH) mem[--sp] = acc;
        else if (i == POP) acc = mem[sp++];
        else if (i == SRS) { mem[--sp] = fp; fp = sp; }
        else if (i == SRE) { sp = fp; fp = mem[sp++]; pc = mem[sp++]; }
        else if (i == PSI) printf("%d", mem[sp++]);
        else if (i == PSC) printf("%c", (char) mem[sp++]);
        else if (i == PAI) printf("%d", acc);
        else if (i == PAC) printf("%c", (char) acc);
        else if (i == GC ) acc = getchar();
        else if (i == EXT) break;
        else if (i == NOP) continue;
        else {
            fprintf(stderr, "halted: bad instruction %d at cycle %d.\n", i, cycles);
            break;
        }
    }

    if (debug) {
        if (sp > init_sp) {
            fprintf(stderr, "stack underflow on return.\n");
        }

        if (init_sp > sp) {
            fprintf(stderr, "non empty stack on return:\n");
            for (int32_t i = sp; i < init_sp; i++) {
                fprintf(stderr, "%06d: %d\n", i, mem[i]);
            }
        }
    }

    return acc;
}

void help (char *this) {
    fprintf(stderr, "usage: %s [-v] <program>\n", this);
    exit(1);
}

int main (int argc, char **argv) {
    int bin[65536];
    int verbose = 0;
    if (argc <= 1) help(argv[0]);
    if (argc == 3) {
        verbose = 1;
        argv++;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        log_fatal("can't open program: %s.\n", strerror(errno));
        return 1;
    }

    ssize_t l = read(fd, bin, 65536 * sizeof(int));
    if (l < 0) {
        log_fatal("can't read program: %s.\n", strerror(errno));
        return 1;
    }

    return vm(bin, 65536, verbose);
}