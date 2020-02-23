#ifndef NATOLANG_VM_H
#define NATOLANG_VM_H
#include <stdint.h>

enum {
    /* opcodes w/ operand  */
    ARG, // load imm th function param to acc
    ADJ, // adjust sp
    IMM, // load immediate val to acc
    J,   // jump to immediate val
    JS,  // jump to immediate val and push pc to stack
    JZ,  // jump to immediate val if zero
    JNZ, // jump to immediate val if not zero
    ADI, SBI, MUI, DII, MDI, // math op w/ immediate val
    EQI, NEI, GTI, LTI, GEI, LEI, // comp
    ANI, ORI, // logic

    /* opcodes w/o operand */
    ADD, SUB, MUL, DIV, MOD, // math op
    EQ, NE, GT, LT, GE, LE, // comp
    AND, OR, NOT, // logic
    LD,  // load val at addr in acc to acc
    SV,  // save acc to stacktop addr
    LA,  // load acc th function param to acc
    PSH, // push acc to stack
    POP, // pop stack to acc
    SRE, // end subroutine: restore fp & sp
    SRS, // start subroutine: push fp on to stack, set fp to sp
    PSI, // print stacktop value as int
    PSC, // print stacktop value as char
    PAI, // print acc value as int
    PAC, // print acc value as char
    GC,  // get char to acc
    EXT, // exit
    NOP, // no-op
};

#endif // NATOLANG_VM_H