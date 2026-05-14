#include <sys/ptrace.h>  // ptrace, PTRACE_GETREGS, PTRACE_PEEKDATA
#include <sys/user.h>    // user_regs_struct
#include <sys/types.h>   // pid_t
#include <cstdio>        // printf, perror, fprintf
#include <cerrno>        // errno

#include "crlns.h"

bool read_regs(pid_t child, unsigned long long *rip_out) {
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

    if(rip_out != nullptr) {
        *rip_out = regs.rip;
    }
    return true;
#else
    fprintf(stderr, "read_regs() is only implemented for x86-64 Linux.\n");
    return false;
#endif
}

bool read_bytes(pid_t child, unsigned long long addr) {
#if defined(__x86_64__)
    errno = 0;

    long data = ptrace(PTRACE_PEEKDATA, child, (void*)addr, nullptr);

    if(data == -1 && errno != 0) {
        perror("ptrace PTRACE_PEEKDATA");
        return false;
    }

    printf("Memory at address:\n");
    printf("  [0x%llx] = 0x%016lx\n", addr, data);

    unsigned char* bytes = reinterpret_cast<unsigned char*>(&data);

    printf("  bytes:");
    for(int i = 0; i < 8; i++) {
        printf(" %02x", bytes[i]);
    }
    printf("\n");

    return true;
#else
    fprintf(stderr, "read_bytes() is only implemented for x86-64 Linux.\n");
    return false;
#endif
}