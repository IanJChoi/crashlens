#include <sys/ptrace.h>  // ptrace, PTRACE_GETREGS, PTRACE_PEEKDATA
#include <sys/user.h>    // user_regs_struct
#include <sys/types.h>   // pid_t
#include <cstdio>        // printf, perror, fprintf
#include <cerrno>        // errno
#include <cstdint>       // uint64_t

#include "crlns.h"

bool read_regs(pid_t child, user_regs_struct* regs_out) {
#if defined(__x86_64__)
    if (ptrace(PTRACE_GETREGS, child, nullptr, regs_out) == -1) {
        perror("ptrace PTRACE_GETREGS");
        return false;
    }

    printf("\nRegister state at crash:\n");
    printf("  RIP = 0x%llx\n", regs_out->rip);
    printf("  RSP = 0x%llx\n", regs_out->rsp);
    printf("  RBP = 0x%llx\n", regs_out->rbp);
    printf("  RAX = 0x%llx\n", regs_out->rax);
    printf("  RBX = 0x%llx\n", regs_out->rbx);
    printf("  RCX = 0x%llx\n", regs_out->rcx);
    printf("  RDX = 0x%llx\n", regs_out->rdx);
    printf("  RSI = 0x%llx\n", regs_out->rsi);
    printf("  RDI = 0x%llx\n", regs_out->rdi);

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

    if (data == -1 && errno != 0) {
        perror("ptrace PTRACE_PEEKDATA");
        return false;
    }

    printf("\nMemory at address (8 bytes):\n");
    printf("  [0x%llx] = 0x%016lx\n", addr, data);

    unsigned char* bytes = reinterpret_cast<unsigned char*>(&data);

    printf("  bytes:");
    for (int i = 0; i < 8; i++) {
        printf(" %02x", bytes[i]);
    }
    printf("\n");

    return true;
#else
    fprintf(stderr, "read_bytes() is only implemented for x86-64 Linux.\n");
    return false;
#endif
}

int crlns_read_memory(pid_t pid, uint64_t addr, uint64_t* out) {
#if defined(__x86_64__)
    errno = 0;

    long data = ptrace(PTRACE_PEEKDATA, pid, (void*)addr, nullptr);

    if (data == -1 && errno != 0) {
        return -1;
    }

    *out = static_cast<uint64_t>(data);
    return 0;
#else
    fprintf(stderr, "crlns_read_memory() is only implemented for x86-64 Linux.\n");
    return -1;
#endif
}