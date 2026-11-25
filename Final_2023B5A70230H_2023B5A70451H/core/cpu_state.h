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

/* fcsr layout (Lab 5, F extension):  bits 7:5 = frm (dynamic rounding
 * mode), bits 4:0 = fflags (accrued exception flags). */
#define FFLAG_NX 0x01u   /* inexact */
#define FFLAG_UF 0x02u   /* underflow */
#define FFLAG_OF 0x04u   /* overflow */
#define FFLAG_DZ 0x08u   /* divide by zero */
#define FFLAG_NV 0x10u   /* invalid operation */
#define FCSR_FFLAGS_MASK 0x1Fu
#define FCSR_FRM_SHIFT   5
#define FCSR_FRM_MASK    0x7u
#define FCSR_MASK        0xFFu

typedef struct {
    uint32_t regs[NUM_REGS];    /* x0..x31. x0 is always read as 0 (enforced in accessors) */
    uint32_t fregs[NUM_REGS];   /* f0..f31: raw IEEE 754 single-precision bit patterns (f0 is a real register) */
    uint32_t fcsr;              /* frm + fflags, see above */
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

/* Floating-point registers hold raw bit patterns: FP values are always
 * moved between registers and memory as uint32_t, never through a C
 * float, so NaN payloads and signalling NaNs survive unchanged. */
uint32_t cpu_read_freg(const CPUState *cpu, int idx);
void cpu_write_freg(CPUState *cpu, int idx, uint32_t bits);

/* FP ABI names (ft0..ft11, fs0..fs11, fa0..fa7) and "fN" parsing. */
const char *cpu_freg_abi_name(int idx);
int cpu_freg_index_from_name(const char *name);   /* -1 if not an FP register */

/* OR exception flags into fcsr.fflags (they are "accrued": only ever set
 * by instructions, cleared by software through the CSRs). */
void cpu_raise_fflags(CPUState *cpu, uint32_t flags);
uint32_t cpu_frm(const CPUState *cpu);

/* The CSRs this simulator implements (all views of fcsr). */
#define CSR_FFLAGS 0x001
#define CSR_FRM    0x002
#define CSR_FCSR   0x003

/* "fflags" / "frm" / "fcsr" -> CSR number; -1 if unknown. */
int cpu_csr_number_from_name(const char *name);

const char *cpu_status_str(CPUStatus status);

#endif /* CPU_STATE_H */
