#include "cpu_state.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char *abi_names[NUM_REGS] = {
    "zero","ra","sp","gp","tp","t0","t1","t2",
    "s0","s1","a0","a1","a2","a3","a4","a5",
    "a6","a7","s2","s3","s4","s5","s6","s7",
    "s8","s9","s10","s11","t3","t4","t5","t6"
};

void cpu_reset(CPUState *cpu, uint32_t start_pc) {
    memset(cpu->regs, 0, sizeof(cpu->regs));
    cpu->pc = start_pc;
    cpu->next_pc = start_pc;
    cpu->status = CPU_RUNNING;
    cpu->cycle_count = 0;
    cpu->fault_addr = 0;
    cpu->fault_msg[0] = '\0';
}

uint32_t cpu_read_reg(const CPUState *cpu, int idx) {
    if (idx == 0) return 0;
    return cpu->regs[idx & 0x1F];
}

void cpu_write_reg(CPUState *cpu, int idx, uint32_t value) {
    if (idx == 0) return; /* x0 hardwired to zero, writes silently discarded */
    cpu->regs[idx & 0x1F] = value;
}

const char *cpu_reg_abi_name(int idx) {
    return abi_names[idx & 0x1F];
}

int cpu_reg_index_from_name(const char *name) {
    if (!name || !*name) return -1;

    if ((name[0] == 'x' || name[0] == 'X') && isdigit((unsigned char)name[1])) {
        char *end;
        long v = strtol(name + 1, &end, 10);
        if (*end == '\0' && v >= 0 && v < NUM_REGS) return (int)v;
        return -1;
    }

    for (int i = 0; i < NUM_REGS; i++) {
        if (strcmp(name, abi_names[i]) == 0) return i;
    }
    /* fp is a common alias for s0 */
    if (strcmp(name, "fp") == 0) return 8;
    return -1;
}

const char *cpu_status_str(CPUStatus status) {
    switch (status) {
        case CPU_RUNNING:              return "RUNNING";
        case CPU_HALTED:                return "HALTED";
        case CPU_ILLEGAL_INSTRUCTION:   return "ILLEGAL_INSTRUCTION";
        case CPU_MEM_FAULT:             return "MEM_FAULT";
        case CPU_ECALL_EXIT:            return "ECALL_EXIT";
        default:                        return "UNKNOWN";
    }
}
