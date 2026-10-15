#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#include <stdint.h>
#include "memory.h"

typedef enum {
    ASM_OK = 0,
    ASM_ERR_IO,
    ASM_ERR_SYNTAX,
    ASM_ERR_UNKNOWN_MNEMONIC,
    ASM_ERR_BAD_OPERAND,
    ASM_ERR_UNDEFINED_SYMBOL,
    ASM_ERR_DUPLICATE_SYMBOL,
    ASM_ERR_MEMORY,
    ASM_ERR_TOO_MANY_LINES
} AsmStatus;

typedef struct {
    char message[256];
    int line_number;   /* 1-based; 0 if not applicable */
} AsmError;

/* Two-pass assembler. Reads RISC-V assembly from `filename`, writes the
 * encoded program (and any .word/.half/.byte data) directly into `mem`
 * starting at address 0, and reports the address of the "main" label as
 * the entry point via *out_entry_point (0 if "main" is not defined).
 *
 * Uses the SAME instruction registry (core/isa_registry.h) that the
 * simulator's decoder uses, so any instruction a lab registers -- base or
 * extension -- is automatically assemblable without touching this file. */
AsmStatus assemble_file(const char *filename, Memory *mem, uint32_t *out_entry_point, AsmError *err);

#endif /* ASSEMBLER_H */
