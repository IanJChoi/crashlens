#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/personality.h>
#include <sys/user.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <cstdint>
#include <csignal>

#include "crlns.h"

static const char* signal_name(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGFPE:  return "SIGFPE";
    case SIGILL:  return "SIGILL";
    case SIGBUS:  return "SIGBUS";
    default:      return "UNKNOWN";
    }
}

static bool is_crash_signal(int sig) {
    return sig == SIGSEGV || sig == SIGABRT || sig == SIGFPE
        || sig == SIGILL  || sig == SIGBUS;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: ./crlns <program> [args...]\n");
        return 1;
    }

    std::vector<char*> target_argv(argv + 1, argv + argc);
    target_argv.push_back(nullptr);

    pid_t child = fork();

    if (child < 0) {
        perror("fork");
        return 1;
    }

    if (child == 0) {
        ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
        personality(ADDR_NO_RANDOMIZE);

        execvp(target_argv[0], target_argv.data());

        perror("execvp");
        _exit(1);
    }

    int status;

    waitpid(child, &status, 0);
    ptrace(PTRACE_CONT, child, nullptr, nullptr);

    while (1) {
        waitpid(child, &status, 0);

        if (WIFEXITED(status)) {
            printf("message from crashlens:\n");
            printf("Program exited normally with code %d.\n", WEXITSTATUS(status));
            return 0;
        }

        if (WIFSTOPPED(status)) {
            int sig = WSTOPSIG(status);

            if (is_crash_signal(sig)) {
                printf("message from crashlens:\n");
                printf("Crash detected! Signal: %s (%d)\n", signal_name(sig), sig);

                user_regs_struct regs;

                if (read_regs(child, &regs)) {
                    read_bytes(child, regs.rip);

                    symbol_info sym_info;

                    if (crlns_get_symbol_info(argv[1], &sym_info) == 0) {
                        crlns_backtrace(child, &sym_info, &regs);
                    }
                }

                kill(child, SIGKILL);
                waitpid(child, nullptr, 0);
                return 1;
            }

            ptrace(PTRACE_CONT, child, nullptr, (void*)(intptr_t)sig);
        }

        if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);

            printf("message from crashlens:\n");
            printf("Program killed by signal %s (%d).\n", signal_name(sig), sig);
            return 1;
        }
    }
}