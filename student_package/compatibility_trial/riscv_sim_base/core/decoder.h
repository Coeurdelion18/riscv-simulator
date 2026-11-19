#ifndef DECODER_H
#define DECODER_H

#include "isa_registry.h"

typedef enum {
    DECODE_OK = 0,
    DECODE_UNKNOWN_INSTRUCTION
} DecodeStatus;

/* Splits a raw 32-bit word into fields, looks the (opcode,funct3,funct7)
 * pattern up in the ISA registry, and -- using the format that handler
 * declares -- reconstructs the immediate. This is also where "instruction
 * not in library" is detected: if no handler matches, DECODE_UNKNOWN_INSTRUCTION
 * is returned and *handler_out is left NULL. */
DecodeStatus decode_instruction(uint32_t raw, DecodedInstr *out, const InstructionHandler **handler_out);

#endif /* DECODER_H */
