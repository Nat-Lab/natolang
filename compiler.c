#include "vm.h"
#include "log.h"
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#define SYMTAB_SZ 8192
#define LOOPTBL_SZ 8192
#define LBLTBL_SZ 8192
#define IS_NAME(x) (x >= 'a' && x <= 'z') || (x >= 'A' && x <= 'Z') || x == '_'
#define IS_NUM(x) (x >= '0' && x <= '9')
#define NEXT() if (_next(0) < 0) return -1
#define DNEXT() if (_next(1) < 0) return -1
#define EXPR(x) if (_expr(x) < 0) return -1
#define STMT() if (_stmt() < 0) return -1
#define CHKDATA() if (data > max_data_sz) { log_error("data region overflowed.\n"); return -1; }
#define WANT(c) if (cur != c) { \
        log_error("line %d: want '%c', saw %d", line, c, cur);\
        if (cur > T_NAME && cur <= O_STR) {\
            fprintf(stderr, " (%s).\n", t_names[cur - T_NAME]);\
        } else fprintf(stderr, " (%c).\n", (char) cur);\
        return -1; \
    } // TODO
#define SYM sym[symi]
#define LBL lbl[lbli]

const static char* t_names[] = {
    "identifier", "number", "label", "unresloved label", "function declaration",
    "variable declaration", "if", "else", "while", "for", "break",
    "continue", "goto", "return", "printi", "printc", "prints", "getc", "exit",
    "=", "+=", "-=", "*=", "/=", "%=", "{", "\"", 
    "!", "|", "&", "!=", "<", "<=", ">", ">=", "+", "-", "*", "/", "++", "--"
};

int _stmt();
int _next(int i);
int _expr(int l);

enum {
    T_NAME = 255, T_NUM, T_LBL, T_PLBL,
    D_FUNC, D_VAR,
    TKN_IF, TKN_ELSE, TKN_WHILE, TKN_FOR, TKN_BREAK, TKN_CONT, TKN_GOTO, TKN_RETURN,
    F_PRINTI, F_PRINTC, F_PRINTS, F_GETC, F_EXIT, // builtins
    O_ASSIG, O_ADDA, O_SUBA, O_MULA, O_DIVA, O_MODA, O_LIST, O_STR, 
    O_NOT, O_OR, O_AND, O_NE, O_EQ, O_LT, O_LE, O_GT, O_GE,
    O_ADD, O_SUB, O_MUL, O_DIV, O_MOD, O_INC, O_DEC
};

typedef struct lbltab lbltab_t;
struct lbltab {
    int jaddr;
    int next;
};


typedef struct symbol symbol_t;
struct symbol {
    int type;
    int val;
    int scope;
    int scope_level;
    size_t name_len;
    const char *name;
};

static const char *pos = NULL; // position in source 
static int cur = 0; // current token type (if > 255), or token itself
static int val = 0; // current value
static int *bin_start = NULL; // position of the start of asm.
static int *bin = NULL;  // position is nato-vm asm
static int data = 0; // current data location
static int max_data_sz = 0;
static int line = 1;  // line number
static symbol_t sym[SYMTAB_SZ]; // symbol table
static int symi = 0; // current symbol table element index
static int new_sym = 0; // =1 if last next() call reg new symbol
static int loop_addrs[LOOPTBL_SZ][3];
static int loopi = 0;
static int loopid = 0;
static int scope_level = 0;
static int scope_id = 0;
static int next_scope_id = 1;
static lbltab_t lbl[LBLTBL_SZ];
static int lbli = 0;

const char* symname(int i) {
    static char namebuf[64];
    size_t l = sym[i].name_len > 64 ? 64 : sym[i].name_len;
    memcpy(namebuf, sym[i].name, l);
    namebuf[sym[i].name_len] = '\0';

    return namebuf;
}

// set jump values for break/continue
void patch_loop(int loop_test_addr, int loop_end_addr) {
    for (int i = 0; i < loopi; i++) {
        int* loopaddr = loop_addrs[i];
        if (loopaddr[0] != loopid) continue;
        loopaddr[0] = -1;
        bin_start[loopaddr[2]] = (loopaddr[1] == TKN_BREAK) ? loop_end_addr : loop_test_addr;
    }

}

void symdump() {
    for (int i = 0; i < SYMTAB_SZ && sym[i].type != 0; i++) {
        fprintf(stderr, "%10s: type: %d, val: %d, scope_id: %d, scope_level: %d.\n", symname(i), sym[i].type, sym[i].val, sym[i].scope, sym[i].scope_level);
    }
}

int _next(int isdec) {
    while ((cur = *pos) != '\0') {
        pos++;

        if (cur == '\n') {
            line++;
            continue;
        }
        else if (cur == ' ') continue;
        else if (cur == '#') {
            // comment
            for (; *pos != 0 && *pos != '\n'; pos++);
            continue;
        } else if (IS_NAME(cur)) {
            // name
            const char *last_pos = pos - 1;
            for (; IS_NAME(*pos) || IS_NUM(*pos); pos++);
            size_t name_len = pos - last_pos;

            // lookup sym table
            int do_ret = 0;
            int i = 0;
            for (;sym[i].type != 0 && i < SYMTAB_SZ; i++) {
                if (sym[i].name != NULL && name_len == sym[i].name_len && (sym[i].scope == scope_id || (!isdec && sym[i].scope_level < scope_level)) && memcmp(sym[i].name, last_pos, name_len) == 0) {
                    // symbol found.
                    cur = sym[i].type;
                    symi = i;
                    new_sym = 0;
                    do_ret = 1;
                }
            }

            if (do_ret) {
                if (SYM.type == T_PLBL) { // was refered to by prev goto stmt.
                    lbltab_t *l = lbl + SYM.val;
                    do {
                        bin_start[l->jaddr] = bin - bin_start;
                    } while (l->next != 0);
                    SYM.type = T_LBL;
                    SYM.val = bin - bin_start;
                    if (*pos++ != ':') {
                        log_error("line %d: label must ends with ':'.\n", line);
                        return -1;
                    }
                    continue;
                }
                return 0;
            }
            if (i >= SYMTAB_SZ) {
                log_error("symbol table full.\n");
                return -1;
            }

            new_sym = 1;
            // not exist, create
            symi = i;
            SYM.type = T_NAME;
            SYM.name = last_pos;
            SYM.name_len = name_len;
            SYM.scope = scope_id;
            SYM.scope_level = scope_level;
            

            if (*pos == ':') { // the name is a label.
                pos++;
                SYM.type = T_LBL;
                SYM.val =  bin - bin_start;
                continue;
            }
            cur = SYM.type;

            return 0;
        } else if (IS_NUM(cur)) {
            // number
            val = cur - '0';
            while (*pos >= '0' && *pos <= '9') val = 10 * val + *pos++ - '0';
            
            cur = T_NUM;
            return 0;
        } else if (cur == '\'') {
            // char
            int tmp = *pos++;
            val = 0;

            while (tmp != '\'') {
                if (tmp == '\\') {
                    tmp = *pos++;

                    if (tmp == 'n') tmp = '\n';
                    if (tmp == '\\') tmp = '\\';
                    if (tmp == '\'') tmp = '\'';
                }

                val = val * 10 + tmp;
                tmp = *pos++;
            }

            cur = T_NUM;
            return 0;
        } else if (cur == '=') {
            if (*pos == '=') {
                pos++;
                cur = O_EQ;
                return 0;
            }

            cur = O_ASSIG;
            return 0;
        } else if (cur == '!') {
            if (*pos == '=') {
                pos++;
                cur = O_NE;
                return 0;
            }

            cur = O_NOT;
            return 0;
        } else if (cur == '>') {
            if (*pos == '=') {
                pos++;
                cur = O_GE;
                return 0;
            }

            cur = O_GT;
            return 0;
        } else if (cur == '<') {
            if (*pos == '=') {
                pos++;
                cur = O_LE;
                return 0;
            }

            cur = O_LT;
            return 0;
        }
        else if (cur == '+') { 
            cur = O_ADD; 
            if (*pos == '+') {
                pos++;
                cur = O_INC;
                return 0;
            }

            if (*pos == '=') {
                pos++;
                cur = O_ADDA;
                return 0;
            }
            
            return 0; 
        }
        else if (cur == '-') { 
            cur = O_SUB; 
            if (*pos == '-') {
                pos++;
                cur = O_DEC;
                return 0;
            }

            if (*pos == '=') {
                pos++;
                cur = O_SUBA;
                return 0;
            }
            
            return 0; 
        }
        else if (cur == '*') { 
            cur = O_MUL; 

            if (*pos == '=') {
                pos++;
                cur = O_MULA;
                return 0;
            }
            
            return 0; 
        }
        else if (cur == '/') { 
            cur = O_DIV; 

            if (*pos == '=') {
                pos++;
                cur = O_DIVA;
                return 0;
            }
            
            return 0; 
        }
        else if (cur == '%') { 
            cur = O_MOD; 

            if (*pos == '=') {
                pos++;
                cur = O_MODA;
                return 0;
            }
            
            return 0; 
        }
        else if (cur == '&') { cur = O_AND; return 0; }
        else if (cur == '|') { cur = O_OR; return 0; }
        else if (cur == '(' || cur == ')' || cur == '[' || cur == ']' ||
                 cur == '{' || cur == '}' || cur == ';' || cur == ',' || 
                 cur == '$' || cur == '"' || cur == ':') {
            return 0;
        }
        else {
            log_error("bad token: %c.\n", cur);
            return -1;
        }
    }

    return 0;
}

int _expr(int lvl) {
    if (cur == '\0') {
        log_error("unexpected eof.\n");
        return -1;
    }

    if (cur == T_NUM) {
        *bin++ = IMM;
        *bin++ = val;
        NEXT();
    } else if (cur == T_NAME) {
        symbol_t *s = &SYM;
        NEXT();

        if (new_sym) {
            log_error("line %d: undefined symbol %s (current scope: %d, level: %d).\n", line, symname(symi), scope_id, scope_level);
            symdump();
            return -1;
        }

        if (cur == '(') { // function call
            NEXT();
            int n_params = 0;
            while (cur != ')') {
                n_params++;
                EXPR(O_ASSIG);
                *bin++ = PSH;
                //NEXT();
                if (cur == ')') break;
                WANT(',');
                NEXT(); // eats ','
            }

            if (s->type == T_NAME) {
                *bin++ = JS;
                *bin++ = s->val;
            } else /* if (s->type == ...) */ {
                // TODO: other syscalls
                log_error("line %d: bad function call.\n", line);
                return -1;
            }

            if (n_params > 0) {
                *bin++ = ADJ;
                *bin++ = n_params;
            }

            NEXT(); // eat ')'
        } else if (cur == '[') { // address offset
            NEXT();
            EXPR(O_ASSIG);
            *bin++ = PSH;
            *bin++ = IMM;
            *bin++ = s->val;
            *bin++ = ADD;
            *bin++ = LD;
            WANT(']');
            NEXT(); // eat ']'
        } else { // normal var
            *bin++ = IMM;
            *bin++ = s->val;
            *bin++ = LD;
        } // end if (cur == '(') func call logic

    } else if (cur == '(') {
        NEXT();
        EXPR(O_ASSIG);
        WANT(')');
        NEXT();
    } else if (cur == '$') {
        NEXT();
        if (cur != T_NUM) {
            log_error("line %d: param accesser must be number.\n", line);
            return -1;
        }
        *bin++ = ARG;
        *bin++ = val;
        NEXT();
    } else if (cur == '!') {
        EXPR(O_ASSIG);
        *bin++ = NOT;
    } else if (cur == O_SUB) {
        NEXT();
        if (cur != T_NUM) {
            log_error("line %d: want number after '-'.\n", line);
            return -1;
        }
        *bin++ = IMM;
        *bin = -val;
    } else if (cur == O_LIST) {
        int addr = bin[-2];
        if (bin[-1] != PSH) {
            log_error("line %d: list_init must be rvalue.\n", line);
            return -1;
        }

        while (cur != '}') {
            NEXT();
            EXPR(O_ASSIG);
            *bin++ = SV;
            if (cur == '}') {
                bin--; // let O_ASSIG handle the last SV.
                break;
            }
            WANT(',');
            *bin++ = IMM;
            *bin++ = ++addr;
            *bin++ = PSH;
        }
        NEXT(); // eat '}'
    } else if (cur == O_STR) {
        int addr = bin[-2];
        if (bin[-1] != PSH) {
            log_error("line %d: list_init must be rvalue.\n", line);
            return -1;
        }
        
        cur = *pos++;
        while (cur != '"') {
            if (cur == '\\') {
                cur = *pos++;

                if (cur == 'n') cur = '\n';
                if (cur == '\\') cur = '\\';
                if (cur == '"') cur = '"';
            }
            // save value
            *bin++ = IMM;
            *bin++ = cur;
            *bin++ = SV;

            cur = *pos++;

            // push next address
            *bin++ = IMM;
            *bin++ = ++addr;
            *bin++ = PSH;
        }

        *bin++ = IMM;
        *bin++ = '\0'; // end the string.
        NEXT(); // eat '"'
    } else if (cur == F_PRINTI) {
        NEXT();
        EXPR(O_ASSIG);
        *bin++ = PAI;
    } else if (cur == F_PRINTC) {
        NEXT();
        EXPR(O_ASSIG);
        *bin++ = PAC;
    } else if (cur == F_PRINTS) {
        NEXT();
        int has_p = 0;
        if (cur == '(') {
            NEXT();
            has_p = 1;
        }
        if (cur == T_NAME) {
            int bin_offset = bin - bin_start;
            *bin++ = J;
            *bin++ = bin_offset + 3; // jump over fake "local variable"
            int lvar_pos = bin - bin_start; // addr of fake "local variable"
            *bin++ = 0; // init "local var" = var address
            *bin++ = IMM;
            *bin++ = lvar_pos;
            *bin++ = PSH;
            *bin++ = IMM;
            *bin++ = SYM.val;
            *bin++ = SV;
            int test_pos = bin - bin_start; // addr of test code
            *bin++ = IMM;
            *bin++ = lvar_pos;
            *bin++ = LD;
            *bin++ = LD;
            *bin++ = JZ; // test if cur == '\0'
            *bin++ = test_pos + 16; // jump to end of test
            *bin++ = PAC; // if not '\0', print
            *bin++ = IMM;
            *bin++ = lvar_pos;
            *bin++ = PSH;
            *bin++ = LD;
            *bin++ = ADI;
            *bin++ = 1;
            *bin++ = SV;
            *bin++ = J;
            *bin++ = test_pos; // jump to start of test
        } else if (cur == '"') {
            cur = *pos++;
            while (cur != '"') {
                if (cur == '\\') {
                    cur = *pos++;

                    if (cur == 'n') cur = '\n';
                    if (cur == '\\') cur = '\\';
                    if (cur == '"') cur = '"';
                }
                *bin++ = IMM;
                *bin++ = cur;
                *bin++ = PAC;
                cur = *pos++;
            }
        }
        if (has_p) {
            NEXT();
            WANT(')');
            NEXT(); // eats ')'
        }
        
    } else if (cur == F_GETC) {
        NEXT();
        WANT('(');
        NEXT();
        WANT(')');
        NEXT();
        *bin++ = GC;
    } else if (cur == F_EXIT) {
        NEXT();
        WANT('(');
        NEXT();
        WANT(')');
        NEXT();
        *bin++ = EXT;
    }

    while (cur >= lvl) {
        if (cur == O_ASSIG) {
            NEXT();
            if (bin[-1] != LD) { // not lvalue
                log_error("line %d: not lvalue on left of the '='.\n", line);
                return -1;
            }
            if (cur == '{') cur = O_LIST;

            bin[-1] = PSH;
            EXPR(O_ASSIG);
            *bin++ = SV;
        } else if (cur >= O_ADDA && cur <= O_MODA) { // +=, -=, /= ...
            int op = ADD + (cur - O_ADDA);
            NEXT();
            if (bin[-1] != LD) { // not lvalue
                log_error("line %d: not lvalue before op-assig.\n", line);
                return -1;
            }
            bin[-1] = PSH;
            *bin++ = LD;
            *bin++ = PSH;
            EXPR(O_ASSIG);
            *bin++ = op;
            *bin++ = SV;
        } else if (cur == O_INC || cur == O_DEC) { // ++, --
            int op = cur == O_INC ? ADI : SBI;
            int ret_old_value = 1; // if var++, return old value before add.
            NEXT();
            // current variable not lvalue. maybe next one? (i.e. ++var)
            if (bin[-1] != LD) {
                EXPR(O_ASSIG);
                ret_old_value = 0;
            }
            if (bin[-1] != LD) { // still not lvalue?
                log_error("line %d: not lvalue before/after ++/--.\n", line);
                return -1;
            }

            if (ret_old_value) {
                int var_addr = bin[-2];
                bin[-1] = LD;
                *bin++ = PSH;
                *bin++ = IMM;
                *bin++ = var_addr;
                *bin++ = PSH;
                *bin++ = LD;
            } else {
                bin[-1] = PSH;
                *bin++ = LD;
            }
            
            *bin++ = op;
            *bin++ = 1;
            *bin++ = SV;
            if (ret_old_value) *bin++ = POP;
        }
        else if (cur == O_NOT) { NEXT(); *bin++ = PSH; EXPR(O_OR ); *bin++ = NOT; } 
        else if (cur == O_OR ) { NEXT(); *bin++ = PSH; EXPR(O_AND); *bin++ = OR ; } 
        else if (cur == O_AND) { NEXT(); *bin++ = PSH; EXPR(O_NE ); *bin++ = AND; } 
        else if (cur == O_NE ) { NEXT(); *bin++ = PSH; EXPR(O_LT ); *bin++ = NE ; } 
        else if (cur == O_EQ ) { NEXT(); *bin++ = PSH; EXPR(O_LT ); *bin++ = EQ ; } 
        else if (cur == O_LT ) { NEXT(); *bin++ = PSH; EXPR(O_ADD); *bin++ = LT ; } 
        else if (cur == O_LE ) { NEXT(); *bin++ = PSH; EXPR(O_ADD); *bin++ = LE ; } 
        else if (cur == O_GT ) { NEXT(); *bin++ = PSH; EXPR(O_ADD); *bin++ = GT ; } 
        else if (cur == O_GE ) { NEXT(); *bin++ = PSH; EXPR(O_ADD); *bin++ = GE ; } 
        else if (cur == O_ADD) { NEXT(); *bin++ = PSH; EXPR(O_MUL); *bin++ = ADD; }
        else if (cur == O_SUB) { NEXT(); *bin++ = PSH; EXPR(O_MUL); *bin++ = SUB; }
        else if (cur == O_MUL) { NEXT(); *bin++ = PSH; EXPR(O_MOD); *bin++ = MUL; }
        else if (cur == O_DIV) { NEXT(); *bin++ = PSH; EXPR(O_MOD); *bin++ = DIV; }
        else if (cur == O_MOD) { NEXT(); *bin++ = PSH; EXPR(O_MOD); *bin++ = MOD; }
        else {
            log_error("line %d: bad token '%c' (%d).\n", line, (char) cur, cur);
            return -1;
        }
    }

    return 0;
}

int _stmt() {
    if (cur == D_FUNC) { // function
        DNEXT();
        if (!new_sym) {
            log_error("line %d: symbol '%s' already exist, current scope: %d, level: %d.\n", line, symname(symi), scope_id, scope_level);
            symdump();
            return -1;
        }
        SYM.type = T_NAME;
        SYM.val = bin - bin_start + 2;
        NEXT();

        // jump to end of func (func should not be run as code unless called.)
        *bin++ = J;
        int j_funcend_pos = bin++ - bin_start;
        *bin++ = SRS;
        int cur_scope = scope_id;
        scope_id = next_scope_id++;
        STMT();
        scope_id = cur_scope;
        *bin++ = SRE;
        bin_start[j_funcend_pos] = bin - bin_start;
    } else if (cur == D_VAR) { //variable
        DNEXT();

        if (!new_sym) {
            log_error("line %d: symbol '%s' already exist, current scope: %d, level: %d.\n", line, symname(symi), scope_id, scope_level);
            symdump();
            return -1;
        }

        NEXT();
        if (cur == '[') { // array
            NEXT();
            if (cur != T_NUM) {
                log_error("line %d: array length must be number.\n", line);
                return -1;
            }
            NEXT();
            WANT(']');
            int var_addr = data;
            SYM.val = var_addr;
            data += val;
            CHKDATA();
            SYM.type = T_NAME;
            NEXT();
            if (cur == O_ASSIG) {
                *bin++ = IMM;
                *bin++ = var_addr;
                *bin++ = PSH;
                NEXT();
                if (cur == '{') cur = O_LIST;
                if (cur == '"') cur = O_STR;
                EXPR(O_ASSIG);
                *bin++ = SV;
                WANT(';');
                NEXT();
            } else if (cur != ';') {
                log_error("line %d: want '=' or ';'.\n", line);
                return -1;
            }
        } else if (cur == ';') { // end of var def
            SYM.val = data++;
            CHKDATA();
            SYM.type = T_NAME;
        } else if (cur == O_ASSIG) { 
            SYM.val = data++;
            CHKDATA();
            *bin++ = IMM;
            *bin++ = SYM.val;
            *bin++ = PSH;
            NEXT();
            if (cur == '{') cur = O_LIST;
            if (cur == '"') cur = O_STR;
            EXPR(O_ASSIG);
            WANT(';');
            NEXT();
            *bin++ = SV;
        } else {
            log_error("line %d: want '=', '[' or ';'.\n", line);
            return -1;
        }
    } else if (cur == TKN_IF) {
        NEXT();
        EXPR(O_ASSIG);
        *bin++ = JZ;
        int j_addr_pos = bin++ - bin_start;

        int cur_scope = scope_id;
        scope_id = next_scope_id++;
        STMT();
        scope_id = cur_scope;

        if (cur == TKN_ELSE) {
            bin_start[j_addr_pos] = bin - bin_start + 2;
            *bin++ = J;
            j_addr_pos = bin++ - bin_start;

            NEXT();
            int cur_scope = scope_id;
            scope_id = next_scope_id++;
            STMT();
            scope_id = cur_scope;
        }

        bin_start[j_addr_pos] = bin - bin_start;
    } else if (cur == TKN_WHILE) {
        NEXT();
        int test_pos = bin - bin_start;
        EXPR(O_ASSIG);
        *bin++ = JZ;
        int j_addr_pos = bin++ - bin_start;
        loopid++;
        int cur_scope = scope_id;
        scope_id = next_scope_id++;
        STMT();
        scope_id = cur_scope;
        *bin++ = J;
        *bin++ = test_pos;
        bin_start[j_addr_pos] = bin - bin_start;
        patch_loop(test_pos, bin_start[j_addr_pos]);
        loopid--;
    } else if (cur == TKN_FOR) {
        NEXT();
        WANT('(');
        NEXT();

        EXPR(O_ASSIG);
        WANT(';');
        NEXT();

        int test_pos = bin - bin_start;
        EXPR(O_ASSIG);
        WANT(';');
        NEXT();

        *bin++ = JZ;
        int j_addr_pos = bin++ - bin_start;
        EXPR(O_ASSIG);

        WANT(')');
        NEXT();

        loopid++;
        int cur_scope = scope_id;
        scope_id = next_scope_id++;
        STMT();
        scope_id = cur_scope;
        *bin++ = J;
        *bin++ = test_pos;
        bin_start[j_addr_pos] = bin - bin_start;
        patch_loop(test_pos, bin_start[j_addr_pos]);
        loopid--;
    } else if (cur == TKN_RETURN) {
        NEXT();
        if (cur != ';') EXPR(O_ASSIG);
        *bin++ = SRE;
        WANT(';');
        NEXT();
    } else if (cur == TKN_BREAK) { 
        if (loopid == 0) {
            log_error("line %d: can't break: not in a loop.\n", line);
            return -1;
        }
        *bin++ = J;
        int *l = loop_addrs[loopi++];
        l[0] = loopid;
        l[1] = TKN_BREAK;
        l[2] = bin++ - bin_start;
        NEXT();
        WANT(';');
        NEXT();
    } else if (cur == TKN_CONT) { 
        if (loopid == 0) {
            log_error("line %d: can't continue: not in a loop.\n", line);
            return -1;
        }
        *bin++ = J;
        int *l = loop_addrs[loopi++];
        l[0] = loopid;
        l[1] = TKN_CONT;
        l[2] = bin++ - bin_start;
        NEXT();
        WANT(';');
        NEXT();
    } else if (cur == TKN_GOTO) { 
        NEXT();
        if (cur != T_LBL && !new_sym) {
            log_error("line %d: goto: '%s' is not a label.\n", line, symname(symi));
            return -1;
        } else if (cur == T_NAME && new_sym) {
            SYM.type = T_PLBL; // lbl not exist yet, make it a pending label
            *bin++ = J;
            LBL.jaddr = bin++ - bin_start;
            LBL.next = 0;
            SYM.val = lbli++;
        } else if (cur == T_PLBL) {
            // already pending non-resolved label
            *bin++ = J;
            LBL.jaddr = bin++ - bin_start;
            LBL.next = 0;
            lbltab_t *l = lbl + SYM.val;

            while (l->next != 0) { // find next empt
                l = lbl + l->next;
            }
            l->next = lbli++;
        } else if (cur == T_LBL) {
            *bin++ = J;
            *bin++ = SYM.val;
        } else {
            log_error("line %d: goto: bad label.\n", line);
            return -1;
        }
        
        NEXT();
        WANT(';');
        NEXT();
    } else if (cur == '{') {
        scope_level++;
        NEXT();
        int cur_scope = scope_id;
        scope_id = next_scope_id++;
        while (cur != '}') STMT();
        scope_id = cur_scope;

        NEXT();
        scope_level--;
    } else if (cur == ';') {
        NEXT();
    } else {
        EXPR(O_ASSIG);
        WANT(';');
        NEXT();
    }

    return 0;
}

ssize_t compile(const char *code, int *out, size_t data_sz) {
    bin_start = out;
    bin = bin_start + data_sz;
    max_data_sz = data_sz;

    memset(sym, 0, SYMTAB_SZ);

    pos = "fun var if else while for break continue goto return printi printc prints getc exit";
    for (int i = D_FUNC; i <= F_EXIT;  i++) {
        NEXT();
        SYM.type = i;
        SYM.scope_level = -1; // always avaliable.
    }  

    pos = code;

    // jump over data region
    bin_start[0] = J;
    bin_start[1] = data_sz;
    data += 2;
    CHKDATA();

    NEXT();
    while (cur != '\0') {
        if (cur == '\0') break;
        STMT();
    }

    *bin++ = EXT;
    return bin - out;
}

void help (char *this) {
    fprintf(stderr, "usage: %s -s SRC -o OUT [-d DATA_SZ] [-b BUF] [-v]\n", this);
    fprintf(stderr, "\n");
    fprintf(stderr, "required arguments:\n");
    fprintf(stderr, "    -s SRC      source code\n");
    fprintf(stderr, "    -o OUT      compiled output\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "optional arguments:\n");
    fprintf(stderr, "    -d DATA_SZ  data region size (default: 1024)\n");
    fprintf(stderr, "    -v          verbose switch\n");
    exit(1);
}

int main (int argc, char **argv) {
    char *src = NULL;
    char *dst = NULL;
    int verbose = 0;
    int buf_sz = 65536;
    int data_sz = 1024;
    char code[65536];
    int bin[65536];

    int opt;
    while ((opt = getopt(argc, argv, "s:o:d:b:v")) != -1) {
        switch (opt) {
            case 's': src = strdup(optarg); break;
            case 'o': dst = strdup(optarg); break;
            case 'd': data_sz = atoi(optarg); break;
            case 'b': buf_sz = atoi(optarg); break;
            case 'v': verbose = 1; break;
            default: help(argv[0]); break;
        }
    }

    if (src == NULL || dst == NULL) help(argv[0]);

    int srcfd = open(src, O_RDONLY);
    if (srcfd < 0) {
        log_fatal("can't open src file %s: %s\n", src, strerror(errno));
        goto fail;
    }

    int dstfd = open(dst, O_CREAT | O_WRONLY, 0644);
    if (dstfd < 0) {
        log_fatal("can't open/create dst file %s: %s.\n", dst, strerror(errno));
        goto fail;
    }

    ssize_t len = read(srcfd, code, 65536);
    if (len < 0) {
        log_fatal("can't read src file: %s.\n", strerror(errno));
        goto fail;
    }

    code[len] = '\0';
    ssize_t clen = compile(code, bin, data_sz);
    if (clen < 0) {
        log_fatal("failed to compile.\n");
        goto fail;
    }
    
    ssize_t wlen = write(dstfd, bin, clen * sizeof(int));
    if (wlen < 0) {
        log_fatal("failed to write: %s.\n", strerror(errno));
        goto fail;
    }

    if (verbose) symdump();

    free(src);
    free(dst);
    close(srcfd);
    close(dstfd);
    return 0;

fail:
    free(src);
    free(dst);
    return 1;

}