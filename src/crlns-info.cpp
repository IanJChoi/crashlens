#include <sys/ptrace.h>  // ptrace, PTRACE_GETREGS
#include <sys/user.h>    // user_regs_struct
#include <cstdio>        // printf, perror

#include "crlns.h"

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