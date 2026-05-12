# CrashLens

Welcome to CrashLens.

Here, we are going to learn debugging one step at a time. When a program crashes, our goal is not just to say that an error happened. Our goal is to understand how that error can be captured, inspected, and explained.

We will build a small debugger-like tool from scratch and use it to study real program failures. We will learn how to find the exact address where a crash occurred, how to inspect registers, how to trace which functions led to the crash, and how to reason about why the crash happened. CrashLens is designed as an educational project: a place where students can see how debugging works underneath the surface by directly implementing the pieces themselves.

## Lecture 1: Understanding `ptrace()`

CrashLens is used like this:
````bash
./crashlens <target_program> <args for target_program>
````

The goal of CrashLens is to run a target program and observe what happens when that program stops(by normal exit, crash, signal, etc).
To do this, CrashLens uses a parent-child process structure.

First, CrashLens calls `fork()`. After `fork()`, there are now two processes:
parent process: CrashLens itself
child process:  the process that will become the target program

The child process will eventually run the target program.
The parent process will monitor that child process.

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

At this point, something important happens.

The child does not immediately start running the target program normally. Because the child called `PTRACE_TRACEME` before `execvp()`, the operating system stops the child with `SIGTRAP` right after the successful `exec()`.

Meanwhile, the parent process was waiting with `waitpid()` until the child sends this first `SIGTRAP`.

So the flow looks like this:
parent: waitpid(child, &status, 0)
child:  ptrace(PTRACE_TRACEME, ...)
child:  execvp(target_program, args)
exec() succeeds
→ child process image is replaced by the target program
→ target program is ready to run
→ before it begins normal execution, the child stops with SIGTRAP
→ parent's waitpid() returns
```

This first stop is important because it gives the parent process control before the target program actually runs.
Once the parent sees that the child has stopped, it can allow the target program to continue:
```cpp
ptrace(PTRACE_CONT, child, nullptr, nullptr);
```
This means: "Continue running the traced child process until it stops again."

After this, the target program can do one of several things:
1. It can exit normally.
2. It can crash, for example with SIGSEGV.
3. It can stop because of another signal.

CrashLens mainly focuses on the second case: when the target program crashes.
For example, if the target program dereferences a null pointer, the operating system sends it `SIGSEGV`. Because the program is being traced, the parent process gets a chance to observe that signal before the program fully terminates.

This is the moment where CrashLens can inspect the crash:
- What signal occurred?
- At what instruction address did the crash happen?
- What were the register values?
- Which function calls led to this point?
- Why did the crash happen?
