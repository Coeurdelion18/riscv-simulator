#ifndef CONTROL_H
#define CONTROL_H

/*
 * Control signals for one instruction, as the ID stage would produce them
 * in hardware.
 *
 * DESIGN NOTE (see CLAUDE.md section 6, "option 3"): the instruction
 * handlers in isa/ are monolithic single-cycle functions, and we do NOT
 * change them. Instead, everything the pipeline needs to know about an
 * instruction's *timing* -- which registers it reads and writes, whether it
 * touches memory, whether it can redirect the PC -- is derived here from
 * the InstrFormat the instruction was registered with. That is why an
 * extension like M (Lab 4) needs no pipeline changes at all: every M
 * instruction is FMT_R, so it automatically reads rs1/rs2 and writes rd.
 */

#include "isa_registry.h"

#define REG_NONE (-1)   /* "no register": unused operand, or x0 (never a real dependency) */

/* Lab 5: ONE register-number space for both register files, so the hazard
 * unit and forwarding work unchanged across them:
 *     1..31   x1..x31          (x0 is REG_NONE)
 *     32..63  f0..f31          (f0 is a real register)
 * An integer and an FP register with the same index never alias. */
#define REG_FP_BASE 32
#define REG_IS_FP(r) ((r) >= REG_FP_BASE)
#define REG_INDEX(r) ((r) & 0x1F)
#define NUM_UNIFIED_REGS 64

typedef enum {
    MEMOP_NONE = 0,
    MEMOP_LOAD,
    MEMOP_STORE
} MemOp;

typedef struct {
    int   src1;          /* register read as the 1st operand, or REG_NONE */
    int   src2;          /* register read as the 2nd operand, or REG_NONE */
    int   src3;          /* 3rd operand (fused multiply-add rs3), or REG_NONE */
    int   dst;           /* register written in WB, or REG_NONE */

    MemOp mem_op;        /* loads/stores are performed by the MEM stage itself */
    int   mem_size;      /* 1, 2 or 4 bytes */
    int   mem_unsigned;  /* loads only: zero-extend (lbu/lhu) instead of sign-extend */

    int   is_branch;     /* conditional branch: may redirect the PC in EX */
    int   is_jump;       /* jal / jalr: always redirects the PC in EX */
    int   is_ecall;      /* system call: reads a7/a0 in EX */
} Control;

/* Fills *out from the handler's format, its IF_*_FP flags and the decoded
 * fields. */
void control_from_instr(const InstructionHandler *h, const DecodedInstr *d, Control *out);

/* Read/write a register by unified number (never REG_NONE). */
uint32_t unified_read(const CPUState *cpu, int r);
void     unified_write(CPUState *cpu, int r, uint32_t v);

/* "x5", "f12": for messages/traces. */
const char *unified_name(int r);

#endif /* CONTROL_H */
