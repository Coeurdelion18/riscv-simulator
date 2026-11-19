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
    memset(cpu->fregs, 0, sizeof(cpu->fregs));
    cpu->fcsr = 0;
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

static const char *fp_names[32] = {
    "ft0","ft1","ft2","ft3","ft4","ft5","ft6","ft7",
    "fs0","fs1","fa0","fa1","fa2","fa3","fa4","fa5","fa6","fa7",
    "fs2","fs3","fs4","fs5","fs6","fs7","fs8","fs9","fs10","fs11",
    "ft8","ft9","ft10","ft11"
};
uint32_t cpu_read_freg(const CPUState *cpu, int index) { return cpu->fregs[index & 31]; }
void cpu_write_freg(CPUState *cpu, int index, uint32_t value) { cpu->fregs[index & 31] = value; }
void cpu_raise_fflags(CPUState *cpu, uint32_t flags) { cpu->fcsr |= flags & 31; }
uint32_t cpu_frm(const CPUState *cpu) { return (cpu->fcsr >> 5) & 7; }
const char *cpu_freg_abi_name(int index) { return fp_names[index & 31]; }
int cpu_freg_index_from_name(const char *name) {
    if (name[0] == 'f' && isdigit((unsigned char)name[1])) {
        char *end;
        long index = strtol(name + 1, &end, 10);
        if (!*end && index >= 0 && index < 32) return (int)index;
    }
    for (int i = 0; i < 32; ++i)
        if (strcmp(name, fp_names[i]) == 0) return i;
    return -1;
}
