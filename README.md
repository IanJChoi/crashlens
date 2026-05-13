# crashlens

Welcome to crashlens.

Here, we are going to learn debugging one step at a time. When a program crashes, our goal is not just to say that an error happened. Our goal is to understand how that error can be captured, inspected, and explained.

We will build a small debugger-like tool from scratch and use it to study real program failures. We will learn how to find the exact address where a crash occurred, how to inspect registers, how to trace which functions led to the crash, and how to reason about why the crash happened. crashlens is designed as an educational project: a place where students can see how debugging works underneath the surface by directly implementing the pieces themselves.

## Lecture 1: Understanding `ptrace()`
See: src/crlns.cpp
crashlens is used like this:
````bash
./crashlens <target_program> <args for target_program>
````
The goal of crashlens is to run a target program and observe what happens when that program stops(by normal exit, crash, signal, etc).
To do this, crashlens uses a parent-child process structure. First, crashlens calls `fork()`. After `fork()`, there are now two processes:  
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

## Lecture 2: Reading Registers at the Moment of Crash
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
