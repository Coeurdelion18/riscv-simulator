#ifndef CPU_STATE_H
#define CPU_STATE_H

#include <stdint.h>

#define NUM_REGS 32

/* Overall machine status. This is the C substitute for exceptions:
 * every layer of the simulator checks/sets this instead of throwing. */
typedef enum {
    CPU_RUNNING = 0,
    CPU_HALTED,                 /* clean shutdown requested (e.g. ecall exit) */
    CPU_ILLEGAL_INSTRUCTION,    /* decoder found no matching handler */
    CPU_MEM_FAULT,              /* out-of-bounds memory access */
    CPU_ECALL_EXIT              /* program executed the "exit" syscall */
} CPUStatus;

typedef struct {
    uint32_t regs[NUM_REGS];    /* x0..x31. x0 is always read as 0 (enforced in accessors) */
    uint32_t fregs[32];
    uint32_t fcsr;
    uint32_t pc;
    uint32_t next_pc;           /* instruction handlers write here; executor commits it to pc */
    CPUStatus status;
    uint64_t cycle_count;

    /* Diagnostics for the CLI when something goes wrong */
    uint32_t fault_addr;
    char fault_msg[128];
} CPUState;

void cpu_reset(CPUState *cpu, uint32_t start_pc);

/* Always go through these two functions to touch registers -- this is the
 * single place that enforces x0 == 0, and keeps that rule from being
 * reimplemented (and potentially forgotten) in every instruction handler. */
uint32_t cpu_read_reg(const CPUState *cpu, int idx);
void cpu_write_reg(CPUState *cpu, int idx, uint32_t value);

/* ABI register names (zero, ra, sp, ... ) used by the assembler and the CLI */
const char *cpu_reg_abi_name(int idx);

/* Returns -1 if not a recognized register name (neither "xN" nor an ABI name) */
int cpu_reg_index_from_name(const char *name);

const char *cpu_status_str(CPUStatus status);

#define FFLAG_NX 1u
#define FFLAG_UF 2u
#define FFLAG_OF 4u
#define FFLAG_DZ 8u
#define FFLAG_NV 16u
uint32_t cpu_read_freg(const CPUState *cpu, int index);
void cpu_write_freg(CPUState *cpu, int index, uint32_t value);
void cpu_raise_fflags(CPUState *cpu, uint32_t flags);
uint32_t cpu_frm(const CPUState *cpu);
int cpu_freg_index_from_name(const char *name);
const char *cpu_freg_abi_name(int index);
#endif
