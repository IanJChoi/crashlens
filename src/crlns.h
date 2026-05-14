#ifndef CRLNS_H
#define CRLNS_H

#include <sys/types.h>  // pid_t

bool read_regs(pid_t child, unsigned long long* rip_out);
bool read_bytes(pid_t child, unsigned long long addr);

#endif