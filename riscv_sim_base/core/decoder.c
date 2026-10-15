#include "decoder.h"
#include <stddef.h>

DecodeStatus decode_instruction(uint32_t raw, DecodedInstr *out, const InstructionHandler **handler_out) {
    /* These bit positions are fixed across R/I/S/B formats by RISC-V's
     * design (that's deliberate in the ISA, to simplify hardware decode). */
    uint32_t opcode = raw & 0x7F;
    uint32_t rd     = (raw >> 7)  & 0x1F;
    uint32_t funct3 = (raw >> 12) & 0x7;
    uint32_t rs1    = (raw >> 15) & 0x1F;
    uint32_t rs2    = (raw >> 20) & 0x1F;
    uint32_t funct7 = (raw >> 25) & 0x7F;

    const InstructionHandler *h = isa_find_by_opcode(opcode, funct3, funct7);
    if (!h) {
        *handler_out = NULL;
        return DECODE_UNKNOWN_INSTRUCTION;
    }

    int32_t imm = 0;
    switch (h->format) {
        case FMT_R:
        case FMT_I_SYS:
            imm = 0;
            break;

        case FMT_I:
        case FMT_I_LOAD:
        case FMT_I_JALR:
            /* imm[11:0] = raw[31:20], sign-extended via arithmetic shift */
            imm = ((int32_t)raw) >> 20;
            break;

        case FMT_I_SHAMT:
            imm = (int32_t)((raw >> 20) & 0x1F);
            break;

        case FMT_S: {
            int32_t imm11_5 = (int32_t)((raw >> 25) & 0x7F);
            int32_t imm4_0  = (int32_t)((raw >> 7)  & 0x1F);
            int32_t v = (imm11_5 << 5) | imm4_0;
            if (v & 0x800) v |= 0xFFFFF000; /* sign-extend 12-bit */
            imm = v;
            break;
        }

        case FMT_B: {
            int32_t b12   = (int32_t)((raw >> 31) & 0x1);
            int32_t b11   = (int32_t)((raw >> 7)  & 0x1);
            int32_t b10_5 = (int32_t)((raw >> 25) & 0x3F);
            int32_t b4_1  = (int32_t)((raw >> 8)  & 0xF);
            int32_t v = (b12 << 12) | (b11 << 11) | (b10_5 << 5) | (b4_1 << 1);
            if (v & 0x1000) v |= 0xFFFFE000; /* sign-extend 13-bit */
            imm = v;
            break;
        }

        case FMT_U:
            imm = (int32_t)(raw & 0xFFFFF000);
            break;

        case FMT_J: {
            int32_t b20    = (int32_t)((raw >> 31) & 0x1);
            int32_t b19_12 = (int32_t)((raw >> 12) & 0xFF);
            int32_t b11    = (int32_t)((raw >> 20) & 0x1);
            int32_t b10_1  = (int32_t)((raw >> 21) & 0x3FF);
            int32_t v = (b20 << 20) | (b19_12 << 12) | (b11 << 11) | (b10_1 << 1);
            if (v & 0x100000) v |= 0xFFE00000; /* sign-extend 21-bit */
            imm = v;
            break;
        }
    }

    out->opcode = opcode;
    out->funct3 = funct3;
    out->funct7 = funct7;
    out->rd     = rd;
    out->rs1    = rs1;
    out->rs2    = rs2;
    out->imm    = imm;
    out->raw    = raw;

    *handler_out = h;
    return DECODE_OK;
}
