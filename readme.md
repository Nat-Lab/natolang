natolang: A cursed programing language
---
I created a 32-bits addressable virtual machine that has some simple instructions long ago to practice writing in assembly. At that time I don't have much clue on how you can make a compiler - I can write in assembly, I can write a parser, but I just didn't have any clue on how to make a compiler.

Years have gone by. Now I actually feel like I can make a compiler myself. So I did. And this was the result. It's cursed, but it is fun.

### The Language

Before we get into the fun part, let's take a look at the basics of the language. 

#### Datatype

The only datatype in `natolang` is 32-bit int. (Since the target VM is 32-bits addressable.)

```
123;   # => 123
1.2;   # bad token '.', won't compile.
'a';   # => 97
'aa';  # => 1067 (97 * 10 + 97)
```

#### Math

Maths. Like any other programing language, we need basic math.

```c
10 + 10; # => 10    (ADD)
10 - 5;  # => 5     (SUB)
5 * 2;   # => 10    (MUL)
5 / 2;   # => 2     (DIV)
10 % 4;  # => 2     (MOD)
!0;      # => 1     (NOT)
!1;      # => 0     (NOT)
```

#### Logics

We got logic too. Pretty much the same as any other programing language except we use `&` and `|` for logic AND and OR.

```c
1 & 1;   # => 1     (AND) (not bitwise)
0 & 1;   # => 0     (AND) (not bitwise)
1 | 0;   # => 1     (OR) (not bitwise)
1 == 1;  # => 1     (EQ)
2 > 1;   # => 1     (GT)
2 < 1;   # => 0     (LT)
2 >= 1;  # => 1     (GE)
2 <= 1;  # => 0     (LE)
2 != 1;  # => 1     (NE)
```
#### Variables

The syntax is similar to C, but the way they work is fundamentally different. We will talk about that later.

```c
var a = 0;
var b[10] = "nawoji\n";
var c[10] = { 'n', 'a', 't', 'o', '\n', 0 };
```
Note that we don't have n-D arrays, only 1-D arrays. Also, the initializer list and literal and not a pointer type. There's no such thing as a pointer in `natolang`. More on that later.

You can use assignment operators on variables:
```c
var a = 10;
a += 10;
a -= 2;
a *= 4;
a /= 2;
a %= 3;
```

And also increment/decrement:
```c
var a = 10;
a++; ++a;
a--; --a;
```
Note that the good old "pre-increment (`++i`) is faster than post-increment (`i++`)" is true in `natolang` - since the compiler won't optimize post-increment to pre-increment when the value is not used.

#### Address Operators

You may get the address of a variable or use a variable as address with with `*` and `&` operator:

```c
var a = 0;
var b = &a;
*b = 1;
printi(a); # prints "1"
```

Note that `b` is not a pointer type - `*` just allows you to use the value of a variable as address, `&` operator simply returns the address of that variable, and "arrays" are not pointers. More specifically:

```c
var a[10] = {100, 200, 300};
printi(a); # prints "100"
*(&a+1) = 10;
printi(a[1]); # prints "10"
```

#### Flow Controls

We got conditionals and loops. They should work as you imagined.
```c
var a;
for (a = 0; a < 5; ++a) {
    var b = 0;
    printi(a);
    if (a == 4) break;
    else if (a == 3) continue;
    while (b < 5) {
        ++b;
        printi(b);
        break;
    }
}
```

And we got goto:
```c
lbl1:
prints("Hello\n");
goto lbl1;
```

#### Functions

Function declaration looks like bash, and you access function parameters like bash:
```c
fun fib {
    # use $1, $2, ... to refer to function arguments.
    if ($1 <= 1) return $1; 

    # the last statement/expression in a function automatically becomes return 
    # value.
    fib($1-1) + fib($1-2); 
}

fib(10); # invoke function.
```

Arguments count can be accessed with `$$`, and function arguments can be accessed with `$(expression)`, like this:

```c
fun printvars {
    var i;
    prints("number of args: ");
    printi($$);
    printc('\n');
    for (i = 1; i <= $$; ++i) printi($(i));
    printc('\n');
}

printvars(1, 1, 4, 5, 1, 4); # prints "114514"
```

#### Built-ins
We got some built-in functions (actually, they are handled as operator):
```c
# takes one parameter, print the result as an integer.
printi(n); 
printi(1+1);

# takes one parameter, print the result as an integer.
printc(n); 
printc('a' + 1); 

# prints takes one parameter and print it as string, the parameter must be a 
# variable or a string literal.
prints(n); 
prints("Hello, world!\n");

# getchar. takes no parameter, get and return one char from stdin.
c = getc();

# returns the number of 32-bits size memory blocks allocated for this variable.
sz = sizeof(c);

# exit.
exit();
```

#### Others

Variables in `{ }` are simply scoped variables, not local variables. Think of them as the C static variable. For example, for the following code: 
```c
var a;
for (a = 0; a < 5; ++a) {
    var b;
    ++b;
    printi(b);
    printc(' ');
}
```
The output will be `1 2 3 4 5 `. Note that the `=` in the variable declaration is not initialization. It is an assignment. So if you do:
```c
var a;
for (a = 0; a < 5; ++a) {
    var b = 0;
    ++b;
    printi(b);
    printc(' ');
}
```
You will get `1 1 1 1 1 `.


Functions can be nested:
```c
fun fa {
    fun fb {
        $1 + 1;
    }
    fb($1) + 2;
}
```
Nested functions are basically scoped variables. They can't be accessed from the outside. (so you can't invoke `fb` outside `fa`)

And we use `#` for comment if you haven't already noticed.

### The Funs (and curses)

The first thing you need to know is that the idea of the pointer/reference does not exist in `natolang`, in the sense that there's no actual pointer type. (I'm lazy)

So, when you do this:
```c
var a = 0;
var c[10] = { 'n', 'a', 't', 'o', '\n', 0 };
```
The compiler actually puts `nato\n\0` at the variable's location.

And arrays are just a normal variable with some extra memory spaces append to them (size of the space specified with `[]`). So in the data region, the variable `a` and `c` shown above looks like this:

```
Address     0   1   2   3   4   5   6   7   8   9   10
Variable    a   c   c   c   c   c   c   c   c   c   c   
Value       \0  n   a   t   o   \n  \0  \0  \0  \0  \0 
```

And this is why we don't have n-D arrays. Since what looks like an array are just a big variable.  

Also, you can do this:

```c
var v1 = { 1, 2, 3 };
var v2;
var v3;
prints("v1: "); printi(v1); prints(", ");
prints("v2: "); printi(v2); printc(", ");
prints("v3: "); printi(v3); printc(", ");
prints("v2[1]: "); printi(v2[1]); printc('\n');
```

Run it yields the following output:
```
v1: 1, v2: 2, v3: 3, v2[1]: 3
```
How could this be fun? Let's take a look at how functions in this cursed language work before we get into that.

If you define a variable with ASM in it:
```c
var asm[20] = {
    40,         # SRS       SubRoutine Start
    2, 'H',     # IMM 'H'   IMMediate
    44,         # PAC       Print Accumulator Char
    2, 'e',     # IMM 'e'   IMMediate
    44,         # PAC       Print Accumulator Char
    2, 'l',     # IMM 'l'   IMMediate
    44,         # PAC       Print Accumulator Char
    2, 'l',     # IMM 'l'   IMMediate
    44,         # PAC       Print Accumulator Char
    2, 'o',     # IMM 'o'   IMMediate
    44,         # PAC       Print Accumulator Char
    2, '\n',    # IMM '\n'  IMMediate
    44,         # PAC       Print Accumulator Char
    39,         # SRE       SubRoutine End
};
```

Guess what? You can invoke it.
```c
asm(); # => Hello
```

Yes, function a just like any other variables. That means you can do this:
```c
fun self_mod {
    printi(1);
    self_mod[2] = self_mod[2] + 1;
}
```

Every time you call `self_mod`, it's output will be increased by one. The `self_mod` function compiles to:
```
00   SRS
01   IMM
02   1
03   PAI
..   ...
XX   SRE
```
By changing `self_mod[2]`, we changed what will be `IMM` to the register next time. This is a function that modifies itself, quite fun, isn't it?

A more involved example will be to search for a certain instruction in function's code and change it:

```c
fun do_op {
    $1 + 10;
}

fun find_op {
    var i;
    for (i = 0; i < $2; i++) {
        # looks for "IMM 10; ADD" i.e. "+ 10";
        if (*($1 + i) == 2 & *($1 + i + 1) == 10 & *($1 + i + 2) == 20 & *($1 + i + 3) == 39) {
            prints("'+' is at: ");
            printi(i+2);
            printc('\n');
            break;
        }
    }
    i + 2;
}

printi(do_op(20)); # prints "30" (20 + 10)
printc('\n');
do_op[find_op(&do_op, sizeof(do_op))] = 22; # opcode for MUL (*)
printi(do_op(5)); # prints "50" (5 * 10)
```

### The VM

The virtual machine has the following instructions:
```c
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
```

### Usage

Clone the repo and run make. Then compile and run your program with `./compiler -s code.n -o code.bin && ./vm code.bin`:

```
$ git clone https://github.com/nat-lab/natolang
$ cd natolang
$ make
$ ./compiler -s examples/bubble.n -o bubble.bin
$ ./vm bubble.bin
$ ./vm -v bubble.bin  # verbose vm
```

### License

UNLICENSE