#ifndef CRLNS_H
#define CRLNS_H

#include <sys/types.h>
#include <sys/user.h>
#include <elf.h>
#include <cstdint>
#include <cstddef>

bool read_regs(pid_t child, user_regs_struct* regs_out);
bool read_bytes(pid_t child, unsigned long long addr);

struct symbol_info {
    char* elf_file;
    size_t elf_file_size;

    Elf64_Sym* symtab;
    size_t num_syms;

    char* strtab;
};

int crlns_get_symbol_info(const char* exec_path, symbol_info* oinfo);
Elf64_Sym* crlns_find_function(symbol_info* sym_info, uint64_t addr);
int crlns_backtrace(pid_t pid, symbol_info* sym_info, user_regs_struct* regs);
int crlns_read_memory(pid_t pid, uint64_t addr, uint64_t* out);

#endif