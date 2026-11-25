#ifndef ISA_REGISTRY_H
#define ISA_REGISTRY_H

/*
 * This is the single most important file in the base system.
 *
 * The simulator does NOT hardcode a big switch/if-else ladder over opcodes.
 * Instead, every instruction (base RV32I, and later the M/F/custom
 * extensions students add in later labs) is a struct registered into a
 * shared table. The decoder looks instructions up in this table by their
 * bit pattern; the assembler looks them up by mnemonic. Both therefore
 * share ONE source of truth about what each instruction is.
 *
 * Extending the ISA in a later lab means writing new InstructionHandler
 * entries and calling register_instruction() -- nothing in core/ ever
 * needs to change.
 */

#include <stdint.h>
#include "cpu_state.h"
#include "memory.h"

typedef enum {
    FMT_R,        /* rd, rs1, rs2                  e.g. add  a0, a1, a2      */
    FMT_I,        /* rd, rs1, imm                   e.g. addi a0, a1, 4       */
    FMT_I_LOAD,   /* rd, imm(rs1)                   e.g. lw   a0, 0(sp)       */
    FMT_I_JALR,   /* rd, imm(rs1)                   e.g. jalr ra, 0(t0)       */
    FMT_I_SHAMT,  /* rd, rs1, shamt (uses funct7)    e.g. slli a0, a1, 3       */
    FMT_I_SYS,    /* no operands                     e.g. ecall                */
    FMT_S,        /* rs2, imm(rs1)                   e.g. sw   a0, 0(sp)       */
    FMT_B,        /* rs1, rs2, label                 e.g. beq  a0, a1, foo     */
    FMT_U,        /* rd, imm                         e.g. lui  a0, 0x10000     */
    FMT_J,        /* rd, label                       e.g. jal  ra, foo        */

    /* Added in Lab 5 (F extension): */
    FMT_R2,       /* rd, rs1 [, rm]  rs2 field fixed  e.g. fsqrt.s fa0, fa1    */
    FMT_R4,       /* rd, rs1, rs2, rs3 [, rm]         e.g. fmadd.s fa0, fa1, fa2, fa3 */
    FMT_CSR,      /* rd, csr, rs1                     e.g. csrrw t0, fcsr, t1  */
    FMT_CSRI      /* rd, csr, uimm5                   e.g. csrrwi t0, frm, 2   */
} InstrFormat;

/* Optional per-instruction flags (Lab 5), given to register_instruction_ex().
 * Plain register_instruction() means flags = 0: integer registers,
 * ordinary funct3. They are kept OUT of InstructionHandler so that every
 * existing 6-field registration compiles unchanged (and warning-free). */
#define IF_RD_FP     0x01u  /* rd  names an f register (else an x register) */
#define IF_RS1_FP    0x02u  /* rs1 names an f register */
#define IF_RS2_FP    0x04u  /* rs2 names an f register */
#define IF_RS3_FP    0x08u  /* rs3 names an f register (FMT_R4) */
#define IF_HAS_RM    0x10u  /* funct3 is a rounding mode: register with funct3 = -1; the
                               assembler accepts an optional ", rm" operand (default dyn) */
#define IF_RS2_FIXED 0x20u  /* the rs2 field is part of the opcode: must equal rs2_sel */

/* funct3 values of the rounding-mode field */
#define RM_RNE 0  /* round to nearest, ties to even */
#define RM_RTZ 1  /* toward zero */
#define RM_RDN 2  /* down (toward -inf) */
#define RM_RUP 3  /* up (toward +inf) */
#define RM_RMM 4  /* to nearest, ties to max magnitude */
#define RM_DYN 7  /* use fcsr.frm */

typedef struct {
    uint32_t opcode;
    uint32_t funct3;
    uint32_t funct7;
    uint32_t rd, rs1, rs2;
    uint32_t rs3;     /* bits 31:27, FMT_R4 only */
    int32_t  imm;     /* for FMT_CSR/FMT_CSRI: the 12-bit CSR number */
    uint32_t raw;
} DecodedInstr;

typedef void (*ExecuteFn)(CPUState *cpu, Memory *mem, DecodedInstr *instr);

typedef struct {
    const char *mnemonic;
    uint32_t    opcode;
    int32_t     funct3;   /* -1 means "don't care / not part of this encoding" */
    int32_t     funct7;   /* -1 means "don't care / not part of this encoding" */
    InstrFormat format;
    ExecuteFn   execute;
} InstructionHandler;

#define ISA_REGISTRY_MAX 256

void isa_registry_init(void);

/* Returns 0 on success, -1 if the table is full. Later registrations with
 * the same mnemonic are allowed (a lab overriding a base handler is a valid,
 * if unusual, use case) -- decode lookups return the FIRST opcode match and
 * mnemonic lookups return the LAST mnemonic match, so a later registration
 * for the same mnemonic "wins" for the assembler. */
int register_instruction(InstructionHandler handler);

/* Lab 5: same, with IF_* flags and (for IF_RS2_FIXED) the fixed rs2 value. */
int register_instruction_ex(InstructionHandler handler, uint32_t flags, uint32_t rs2_sel);

/* The flags / rs2 selector a registered handler was given (0 if none). */
uint32_t isa_flags(const InstructionHandler *h);
uint32_t isa_rs2_sel(const InstructionHandler *h);

/* rs2 is only compared for handlers flagged IF_RS2_FIXED (Lab 5). */
const InstructionHandler *isa_find_by_opcode(uint32_t opcode, uint32_t funct3, uint32_t funct7, uint32_t rs2);
const InstructionHandler *isa_find_by_mnemonic(const char *mnemonic);

int isa_registry_count(void);
const InstructionHandler *isa_registry_get(int index);

#endif /* ISA_REGISTRY_H */
