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
    FMT_J         /* rd, label                       e.g. jal  ra, foo        */
} InstrFormat;

typedef struct {
    uint32_t opcode;
    uint32_t funct3;
    uint32_t funct7;
    uint32_t rd, rs1, rs2;
    int32_t  imm;
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

const InstructionHandler *isa_find_by_opcode(uint32_t opcode, uint32_t funct3, uint32_t funct7);
const InstructionHandler *isa_find_by_mnemonic(const char *mnemonic);

int isa_registry_count(void);
const InstructionHandler *isa_registry_get(int index);

#endif /* ISA_REGISTRY_H */
