# CrashLens

Welcome to CrashLe

Here, we are going to learn debugging one step at a time. When a program crashes, our goal is not just to say that an error happened. Our goal is to understand how that error can be captured, inspected, and explained.

We will build a small debugger-like tool from scratch and use it to study real program failures. We will learn how to find the exact address where a crash occurred, how to inspect registers, how to trace which functions led to the crash, and how to reason about why the crash happened. CrashLens is designed as an educational project: a place where students can see how debugging works underneath the surface by directly implementing the pieces themselves.

## Part 1: Understanding `ptrace()`
See: src/crlns.cpp
CrashLens is used like this:
````bash
./crashlens <target_program> <args for target_program>
````
The goal of CrashLens is to run a target program and observe what happens when that program stops(by normal exit, crash, signal, etc).
To do this, CrashLens uses a parent-child process structure. First, CrashLens calls `fork()`. After `fork()`, there are now two processes:  
````text
parent process: crashlens itself  
child process:  the process that will become the target program
````
The child process will eventually run the target program. The parent process will monitor that child process. 

In the child process, it calls:
```cpp
ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
```
This means: "Allow my parent process to trace me."

After that, the child calls:
```cpp
execvp(program, args);
```
This replaces the child process with the target program.

At this point, something important happens. The child does not immediately start running the target program normally. Because the child called `PTRACE_TRACEME` before `execvp()`, the operating system stops the child with `SIGTRAP` right after the successful `exec()`. Meanwhile, the parent process was waiting with `waitpid()` until the child sends this first `SIGTRAP`. So the flow looks like this:
````text
parent: waitpid(child, &status, 0)  
child:  ptrace(PTRACE_TRACEME, ...)  
child:  execvp(target_program, args)  
exec() succeeds  
→ child process image is replaced by the target program  
→ target program is ready to run  
→ before it begins normal execution, the child stops with SIGTRAP  
→ parent's waitpid() returns
````
This first stop is important because it gives the parent process control before the target program actually runs. Once the parent sees that the child has stopped, it can allow the target program to continue:  
```cpp
ptrace(PTRACE_CONT, child, nullptr, nullptr);
```
This means: "Continue running the traced child process until it stops again."

After this, the target program can do one of several things:  
````text
1. It can exit normally.  
2. It can crash, for example with SIGSEGV.  
3. It can stop because of another signal.
````
CrashLens mainly focuses on the second case: when the target program crashes. For example, if the target program dereferences a null pointer, the operating system sends it `SIGSEGV`. Because the program is being traced, the parent process gets a chance to observe that signal before the program fully terminates.

## Part 2: Reading Registers at the Moment of Crash
Now let’s look at a simple crashing program.

See: `sample_tests/segv.cpp`

```cpp
#include <iostream>

int main() {
    int* p = nullptr;
    *p = 10;
    return 0;
}
````

Here, `p` is a pointer whose value is `nullptr`. In other words, it points to address `0x0`.

The line:

```cpp
*p = 10;
```

means: “write the value `10` to the memory address stored in `p`.”

But since `p` is `nullptr`, this tries to write to address `0x0`.

Address `0x0` is not a valid user-space memory address that this program is allowed to write to. The operating system protects this address. So when the target program tries to write there, the CPU traps into the operating system, and the operating system sends the program `SIGSEGV`(Signal Segmentation Violation).

Because the target program is being traced by CrashLens, the child process stops before it fully dies. This gives the parent process a chance to inspect what happened.

In `src/crlns.cpp`, CrashLens handles the crash like this:

```cpp
printf("message from crashlens:\n");
printf("Crash detected! Signal: %s (%d)\n", signal_name(sig), sig);
read_regs(child);

kill(child, SIGKILL);
waitpid(child, nullptr, 0);
return 1;
```

First, CrashLens prints the signal that caused the stop. In this example, the signal is `SIGSEGV`.

Then it calls:

```cpp
read_regs(child);
```

This function is implemented in `src/crlns-info.cpp`.

```cpp
bool read_regs(pid_t child) {
#if defined(__x86_64__)
    struct user_regs_struct regs;

    if (ptrace(PTRACE_GETREGS, child, nullptr, &regs) == -1) {
        perror("ptrace PTRACE_GETREGS");
        return false;
    }

    printf("Register state at crash:\n");
    printf("  RIP = 0x%llx\n", regs.rip);
    printf("  RSP = 0x%llx\n", regs.rsp);
    printf("  RBP = 0x%llx\n", regs.rbp);
    printf("  RAX = 0x%llx\n", regs.rax);
    printf("  RBX = 0x%llx\n", regs.rbx);
    printf("  RCX = 0x%llx\n", regs.rcx);
    printf("  RDX = 0x%llx\n", regs.rdx);
    printf("  RSI = 0x%llx\n", regs.rsi);
    printf("  RDI = 0x%llx\n", regs.rdi);

    return true;
#else
    fprintf(stderr, "read_regs() is only implemented for x86-64 Linux.\n");
    return false;
#endif
}
```

The line:

```cpp
struct user_regs_struct regs;
```

creates a structure that can store the CPU register values of the child process.

Then this line:

```cpp
ptrace(PTRACE_GETREGS, child, nullptr, &regs)
```

asks the operating system to copy the child process’s current register state into `regs`.

This is important because the child process is stopped exactly at the crash. So the register values show the CPU state at the moment the crash happened.

For example:

```cpp
printf("  RIP = 0x%llx\n", regs.rip);
```

prints the value of `RIP`.

`RIP` stands for **Instruction Pointer Register**. On x86-64, it stores the address of the instruction that the CPU was executing when the crash happened.

So if the program crashes while executing the instruction for:

```cpp
*p = 10;
```

then `RIP` can help us find the exact machine instruction that caused the crash.

The other registers also give useful context:

```text
RSP: stack pointer
RBP: base pointer
RAX, RBX, RCX, RDX: general-purpose registers
RSI, RDI: registers often used for function arguments
```

For this simple null pointer crash, one especially interesting register is often `RAX`, because the compiler may load the value of `p` into `RAX` before writing through it.

Now let’s compare CrashLens’s register output with the `objdump` output.
`objdump -d segv` disassembles the `segv` executable and shows the machine instructions that the CPU will run.

If we disassemble the crashing program:

```bash
objdump -d segv
````

we can find the assembly instructions for `main`:

```asm
0000000000401106 <main>:
  401106:       f3 0f 1e fa             endbr64
  40110a:       55                      push   %rbp
  40110b:       48 89 e5                mov    %rsp,%rbp
  40110e:       48 c7 45 f8 00 00 00    movq   $0x0,-0x8(%rbp)
  401115:       00
  401116:       48 8b 45 f8             mov    -0x8(%rbp),%rax
  40111a:       c7 00 0a 00 00 00       movl   $0xa,(%rax)
  401120:       b8 00 00 00 00          mov    $0x0,%eax
  401125:       5d                      pop    %rbp
  401126:       c3                      ret
```

When we run the program through CrashLens:

```bash
./crlns ../sample_tests/segv
```

we get output like this:

```text
message from crashlens:
Crash detected! Signal: SIGSEGV (11)
Register state at crash:
  RIP = 0x40111a
  RSP = 0x7fffffffe200
  RBP = 0x7fffffffe200
  RAX = 0x0
  RBX = 0x7fffffffe328
  RCX = 0x403e30
  RDX = 0x7fffffffe338
  RSI = 0x7fffffffe328
  RDI = 0x1
```

The most important registers here are `RIP` and `RAX`.

`RIP` stands for **Instruction Pointer Register**. On x86-64, it stores the address of the instruction where the CPU stopped.

CrashLens shows:

```text
RIP = 0x40111a
```

Now compare that with the `objdump` output:

```asm
40111a:       c7 00 0a 00 00 00       movl   $0xa,(%rax)
```

The address matches exactly. This tells us that the program crashed while executing this instruction:

```asm
movl   $0xa,(%rax)
```

This instruction means:

```text
write the value 0xa to the memory address stored in RAX
```

`0xa` is hexadecimal for `10`, so this instruction is trying to write the value `10` into memory.

Now look at the value of `RAX`:

```text
RAX = 0x0
```

That means the instruction is really trying to do this:

```text
write 10 to memory address 0x0
```

But address `0x0` is not a valid address for this program to write to. The operating system protects that address. Therefore, the program receives `SIGSEGV`.

This connects directly back to the C++ source code:

```cpp
int* p = nullptr;
*p = 10;
```

At the assembly level, the pointer `p` is first stored on the stack:

```asm
40110e:       48 c7 45 f8 00 00 00    movq   $0x0,-0x8(%rbp)
```

This stores `0x0` at `-0x8(%rbp)`, which is where the local variable `p` lives.

Then the program loads the value of `p` into `RAX`:

```asm
401116:       48 8b 45 f8             mov    -0x8(%rbp),%rax
```

Since `p` is `nullptr`, `RAX` becomes `0x0`.

Finally, the program tries to write `10` to the address stored in `RAX`:

```asm
40111a:       c7 00 0a 00 00 00       movl   $0xa,(%rax)
```

Because `RAX` is `0x0`, this becomes an invalid memory write to address `0x0`.

That is the exact cause of the crash.

We can summarize the crash like this:

```text
C++ source:
*p = 10;

Assembly instruction:
movl $0xa, (%rax)

Register state:
RIP = 0x40111a
RAX = 0x0

Meaning:
The CPU tried to write 10 to address 0x0.

Result:
SIGSEGV
```

After printing the crash information and register state, CrashLens cleans up the child process:

```cpp
kill(child, SIGKILL);
waitpid(child, nullptr, 0);
```

`kill(child, SIGKILL)` forcefully terminates the stopped child process.

Then:

```cpp
waitpid(child, nullptr, 0);
```

waits until the child process has actually terminated, so the parent process can clean it up properly.

At this point, CrashLens has done three important things:

```text
1. It detected that the target program crashed.
2. It identified the signal that caused the crash.
3. It inspected the CPU register state at the moment of the crash.
```

This is the first step toward building a real crash analyzer. Instead of only saying “the program crashed,” CrashLens begins to explain where and how the crash happened.

## Part 3: Reading ELF Symbols and Building a Backtrace
Now CrashLens can detect a crash and read the register state at the moment of failure. In Part 2, we used `RIP` to find the exact assembly instruction where the crash happened.

But there is still one problem.

An address like this:

```text
RIP = 0x401199
```

is useful, but it is not very readable by itself. It tells us where the CPU stopped, but it does not immediately tell us which function the program was executing.

This is where ELF becomes useful.

ELF stands for **Executable and Linkable Format**. On Linux, executable programs are commonly stored as ELF files. An ELF file contains the machine code of the program, but it can also contain useful information about the program, such as section headers, symbol tables, string tables, function names, and function address ranges.

CrashLens reads the target program's ELF file so that it can translate a raw runtime address into a human-readable function name.

So instead of only saying:

```text
The program crashed at 0x401199.
```

CrashLens can say:

```text
The program crashed at 0x401199 inside crash_here().
```

To test this, I created a target program with several function calls before the crash.

See: `sample_tests/deep_crash.cpp`

```cpp
#include <iostream>

extern "C" void crash_here() {
    std::cout << "inside crash_here()" << std::endl;

    int* p = nullptr;
    *p = 123;
}

extern "C" void level_three() {
    std::cout << "inside level_three()" << std::endl;
    crash_here();
}

extern "C" void level_two() {
    std::cout << "inside level_two()" << std::endl;
    level_three();
}

extern "C" void level_one() {
    std::cout << "inside level_one()" << std::endl;
    level_two();
}

int main() {
    std::cout << "inside main()" << std::endl;
    level_one();
    return 0;
}
```

The call chain looks like this:

```text
main()
→ level_one()
→ level_two()
→ level_three()
→ crash_here()
→ null pointer dereference
```

The actual crash happens here:

```cpp
int* p = nullptr;
*p = 123;
```

Here, `p` points to address `0x0`. The line `*p = 123;` means: “write the value `123` to the memory address stored in `p`.” Since `p` is `nullptr`, this tries to write to address `0x0`, which causes `SIGSEGV`.

For this part, the target program is compiled with these options:

```bash
g++ -std=c++17 -Wall -Wextra -O0 -fno-omit-frame-pointer -no-pie -o deep_crash deep_crash.cpp
```

`-O0` disables optimization, which makes the generated assembly easier to understand.

`-fno-omit-frame-pointer` keeps the `RBP` frame pointer. CrashLens uses `RBP` to walk backward through the stack.

`-no-pie` disables PIE, which stands for **Position Independent Executable**. This keeps code addresses stable, so the addresses in the ELF file match the runtime addresses more directly.

When I run CrashLens on this target program:

```bash
./crlns ../sample_tests/deep_crash
```

I get this backtrace:

```text
Backtrace:
  #0  0x401199 in crash_here(0x401156)
  #1  0x4011da in level_three(0x4011a2)
  #2  0x401215 in level_two(0x4011dd)
  #3  0x401250 in level_one(0x401218)
  #4  0x40128b in main(0x401253)
```

This output means that the crash happened at address `0x401199`, inside the function `crash_here`.

Then CrashLens walked backward through the stack and found that `crash_here()` was called by `level_three()`, which was called by `level_two()`, which was called by `level_one()`, which was called by `main()`.

Now let’s compare this with the actual assembly output.

If we run:

```bash
objdump -d deep_crash
```

we can find the assembly for `crash_here`:

```asm
0000000000401156 <crash_here>:
  401156:	f3 0f 1e fa          	endbr64
  40115a:	55                   	push   %rbp
  40115b:	48 89 e5             	mov    %rsp,%rbp
  40115e:	48 83 ec 10          	sub    $0x10,%rsp
  ...
  40118d:	48 c7 45 f8 00 00 00 	movq   $0x0,-0x8(%rbp)
  401194:	00 
  401195:	48 8b 45 f8          	mov    -0x8(%rbp),%rax
  401199:	c7 00 7b 00 00 00    	movl   $0x7b,(%rax)
  40119f:	90                   	nop
  4011a0:	c9                   	leave
  4011a1:	c3                   	ret
```

CrashLens reported:

```text
#0  0x401199 in crash_here(0x401156)
```

This matches the `objdump` output.

The function `crash_here` starts at:

```text
0x401156
```

And the crashing instruction is:

```asm
401199:	c7 00 7b 00 00 00    	movl   $0x7b,(%rax)
```

This instruction means:

```text
write the value 0x7b to the memory address stored in RAX
```

`0x7b` is hexadecimal for `123`.

This connects directly back to the C++ source code:

```cpp
*p = 123;
```

The program first stores `nullptr` into the local variable `p`:

```asm
40118d:	48 c7 45 f8 00 00 00 	movq   $0x0,-0x8(%rbp)
```

Then it loads the value of `p` into `RAX`:

```asm
401195:	48 8b 45 f8          	mov    -0x8(%rbp),%rax
```

Since `p` is `nullptr`, `RAX` becomes `0x0`.

Finally, the program tries to write `123` to the address stored in `RAX`:

```asm
401199:	c7 00 7b 00 00 00    	movl   $0x7b,(%rax)
```

Because `RAX` is `0x0`, this becomes an invalid memory write to address `0x0`. That is the exact cause of the crash.

The important point is that CrashLens did not guess the function name. It found it by reading the target program's ELF file.

In `src/crlns-elf.cpp`, CrashLens opens the target executable and maps it into memory using `mmap()`.

Then it checks the ELF magic number to make sure the file is actually an ELF file:

```cpp
uint8_t elf_magic[4] = { ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3 };

if (memcmp(elf_header->e_ident, elf_magic, sizeof(elf_magic)) != 0) {
    fprintf(stderr, "bad ELF magic number\n");
    munmap(elf_file, filestat.st_size);
    return -1;
}
```

After that, CrashLens finds the section header table and looks for two sections:

```text
.symtab
.strtab
```

`.symtab` is the symbol table. It contains information about symbols in the program, including functions.

`.strtab` is the string table. It stores the actual names of the symbols.

CrashLens finds these sections with this code:

```cpp
if (strcmp(section_name, ".symtab") == 0) {
    symtab_shdr = &shdr_table[i];
}

if (strcmp(section_name, ".strtab") == 0) {
    strtab_shdr = &shdr_table[i];
}
```

Once CrashLens has `.symtab` and `.strtab`, it can search through the program's function symbols.

The key function is:

```cpp
Elf64_Sym* crlns_find_function(symbol_info* sym_info, uint64_t addr)
```

This function loops through the symbol table and only looks at function symbols:

```cpp
if (ELF64_ST_TYPE(sym->st_info) != STT_FUNC) {
    continue;
}
```

Then it checks whether the runtime address is inside that function's address range:

```cpp
uint64_t start = sym->st_value;
uint64_t end = sym->st_value + sym->st_size;

if (start <= addr && addr < end) {
    return sym;
}
```

So if the crash address is `0x401199`, and the function `crash_here` starts at `0x401156`, CrashLens can determine that `0x401199` belongs to `crash_here`.

This is why CrashLens prints:

```text
#0  0x401199 in crash_here(0x401156)
```

The first address, `0x401199`, is the current instruction address.

The address inside parentheses, `0x401156`, is the function's starting address from the ELF symbol table.

CrashLens does the same thing for the caller functions.

From `objdump`, we can see these function start addresses:

```text
0000000000401156 <crash_here>:
00000000004011a2 <level_three>:
00000000004011dd <level_two>:
0000000000401218 <level_one>:
0000000000401253 <main>:
```

CrashLens prints the same function start addresses in the backtrace:

```text
#0  0x401199 in crash_here(0x401156)
#1  0x4011da in level_three(0x4011a2)
#2  0x401215 in level_two(0x4011dd)
#3  0x401250 in level_one(0x401218)
#4  0x40128b in main(0x401253)
```

To move from one function to its caller, CrashLens uses the frame pointer register `RBP`.

With frame pointers enabled, each stack frame stores the caller's frame pointer and return address:

```text
[RBP]     = saved RBP of the caller
[RBP + 8] = return address back into the caller
```

CrashLens reads these values from the stopped child process:

```cpp
crlns_read_memory(pid, rbp, &saved_rbp);
crlns_read_memory(pid, rbp + 8, &return_addr);
```

The return address tells CrashLens where execution would return after the current function finishes. That return address usually points back into the caller function.

So CrashLens can start from the crashing `RIP`, find the current function, read the return address, find the caller function, and repeat this process until it reaches `main`.

We can summarize Part 3 like this:

```text
Runtime crash:
RIP = 0x401199

ELF symbol lookup:
0x401199 is inside crash_here()

Stack walking:
crash_here()
← level_three()
← level_two()
← level_one()
← main()

Result:
CrashLens prints a debugger-style backtrace.
```

At this point, CrashLens is doing more than detecting that a program crashed.

It can now explain where the crash happened and how the program reached that point.
