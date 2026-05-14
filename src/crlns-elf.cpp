#include "crlns.h"

#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <cstdint>

int crlns_get_symbol_info(const char* exec_path, symbol_info* oinfo) {
    struct stat filestat;

    int fd = open(exec_path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    if (fstat(fd, &filestat) != 0) {
        perror("fstat");
        close(fd);
        return -1;
    }

    char* elf_file = (char*)mmap(
        nullptr,
        filestat.st_size,
        PROT_READ,
        MAP_PRIVATE,
        fd,
        0
    );

    close(fd);

    if (elf_file == MAP_FAILED) {
        perror("mmap");
        return -1;
    }

    Elf64_Ehdr* elf_header = (Elf64_Ehdr*)elf_file;

    uint8_t elf_magic[4] = { ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3 };

    if (memcmp(elf_header->e_ident, elf_magic, sizeof(elf_magic)) != 0) {
        fprintf(stderr, "bad ELF magic number\n");
        munmap(elf_file, filestat.st_size);
        return -1;
    }

    if (elf_header->e_shoff == 0) {
        fprintf(stderr, "no section header table\n");
        munmap(elf_file, filestat.st_size);
        return -1;
    }

    Elf64_Shdr* shdr_table = (Elf64_Shdr*)(elf_file + elf_header->e_shoff);

    if (elf_header->e_shstrndx == SHN_UNDEF ||
        elf_header->e_shstrndx == SHN_XINDEX) {
        fprintf(stderr, "unsupported section string table index\n");
        munmap(elf_file, filestat.st_size);
        return -1;
    }

    Elf64_Shdr* sec_str_shdr = &shdr_table[elf_header->e_shstrndx];
    char* section_names = elf_file + sec_str_shdr->sh_offset;

    Elf64_Shdr* symtab_shdr = nullptr;
    Elf64_Shdr* strtab_shdr = nullptr;

    for (int i = 0; i < elf_header->e_shnum; i++) {
        const char* section_name = &section_names[shdr_table[i].sh_name];

        if (strcmp(section_name, ".symtab") == 0) {
            symtab_shdr = &shdr_table[i];
        }

        if (strcmp(section_name, ".strtab") == 0) {
            strtab_shdr = &shdr_table[i];
        }
    }

    if (symtab_shdr == nullptr) {
        fprintf(stderr, ".symtab not found\n");
        munmap(elf_file, filestat.st_size);
        return -1;
    }

    if (strtab_shdr == nullptr) {
        fprintf(stderr, ".strtab not found\n");
        munmap(elf_file, filestat.st_size);
        return -1;
    }

    oinfo->elf_file = elf_file;
    oinfo->elf_file_size = filestat.st_size;
    oinfo->symtab = (Elf64_Sym*)(elf_file + symtab_shdr->sh_offset);
    oinfo->num_syms = symtab_shdr->sh_size / sizeof(Elf64_Sym);
    oinfo->strtab = elf_file + strtab_shdr->sh_offset;

    return 0;
}

Elf64_Sym* crlns_find_function(symbol_info* sym_info, uint64_t addr) {
    for (size_t i = 0; i < sym_info->num_syms; i++) {
        Elf64_Sym* sym = &sym_info->symtab[i];

        if (ELF64_ST_TYPE(sym->st_info) != STT_FUNC) {
            continue;
        }

        if (sym->st_size == 0) {
            continue;
        }

        uint64_t start = sym->st_value;
        uint64_t end = sym->st_value + sym->st_size;

        if (start <= addr && addr < end) {
            return sym;
        }
    }

    return nullptr;
}

int crlns_backtrace(pid_t pid, symbol_info* sym_info, user_regs_struct* regs) {
    uint64_t addr = regs->rip;
    uint64_t rbp = regs->rbp;

    printf("\nBacktrace:\n");

    int frame = 0;

    while (true) {
        Elf64_Sym* func = crlns_find_function(sym_info, addr);

        if (func == nullptr) {
            printf("  #%d  0x%lx in <unknown>\n", frame, addr);
            return -1;
        }

        const char* func_name = &sym_info->strtab[func->st_name];

        printf("  #%d  0x%lx in %s\n", frame, addr, func_name);

        if (strcmp(func_name, "main") == 0) {
            return 0;
        }

        uint64_t saved_rbp = 0;
        uint64_t return_addr = 0;

        if (crlns_read_memory(pid, rbp, &saved_rbp) == -1) {
            perror("crlns_read_memory saved rbp");
            return -1;
        }

        if (crlns_read_memory(pid, rbp + 8, &return_addr) == -1) {
            perror("crlns_read_memory return address");
            return -1;
        }

        rbp = saved_rbp;
        addr = return_addr;

        frame++;

        if (rbp == 0 || addr == 0) {
            break;
        }
    }

    return 0;
}