#include "control.h"

ControlSignals control_decode(const DecodedInstr* i, const InstructionHandler* h) {

    ControlSignals sig = {0};

    switch (h->format) {
        case FMT_CSR:
            sig.reg_write = 1;
            sig.wb_src = WB_ALU;
            sig.uses_rs1 = i->funct3 < 5;
            break;
        case FMT_R2:
            sig.reg_write = 1;
            sig.wb_src = WB_ALU;
            sig.uses_rs1 = 1;
            break;
        case FMT_R4:
            sig.uses_rs3 = 1;
            /* fall through */
        case FMT_R:
            sig.reg_write = 1;
            sig.wb_src = WB_ALU;
            sig.mem_read = 0;
            sig.mem_write = 0;
            sig.is_branch = 0;
            sig.is_jump = 0;
            sig.uses_rs1 = 1;
            sig.uses_rs2 = 1;
            break;

        case FMT_I:
        case FMT_I_SHAMT:
            sig.reg_write = 1;
            sig.wb_src = WB_ALU;
            sig.mem_read = 0;
            sig.mem_write = 0;
            sig.is_branch = 0;
            sig.is_jump = 0;
            sig.uses_rs1 = 1;
            sig.uses_rs2 = 0;
            break;

        case FMT_U:
            sig.reg_write = 1;
            sig.wb_src = WB_ALU;
            sig.mem_read = 0;
            sig.mem_write = 0;
            sig.is_branch = 0;
            sig.is_jump = 0;
            sig.uses_rs1 = 0;
            sig.uses_rs2 = 0;
            break;

        case FMT_I_LOAD:
            sig.reg_write = 1;
            sig.wb_src = WB_MEM;
            sig.mem_read = 1;
            sig.mem_write = 0;
            sig.is_branch = 0;
            sig.is_jump = 0;
            sig.uses_rs1 = 1;
            sig.uses_rs2 = 0;
            break;

        case FMT_S:
            sig.reg_write = 0;
            sig.wb_src = WB_NONE;
            sig.mem_read = 0;
            sig.mem_write = 1;
            sig.is_branch = 0;
            sig.is_jump = 0;
            sig.uses_rs1 = 1;
            sig.uses_rs2 = 1;
            break;

        case FMT_B:
            sig.reg_write = 0;
            sig.wb_src = WB_NONE;
            sig.mem_read = 0;
            sig.mem_write = 0;
            sig.is_branch = 1;
            sig.is_jump = 0;
            sig.uses_rs1 = 1;
            sig.uses_rs2 = 1;
            break;

        case FMT_J:
            sig.reg_write = 1;
            sig.wb_src = WB_PC4;
            sig.mem_read = 0;
            sig.mem_write = 0;
            sig.is_branch = 0;
            sig.is_jump = 1;
            sig.uses_rs1 = 0;
            sig.uses_rs2 = 0;
            break;

        case FMT_I_JALR:
            sig.reg_write = 1;
            sig.wb_src = WB_PC4;
            sig.mem_read = 0;
            sig.mem_write = 0;
            sig.is_branch = 0;
            sig.is_jump = 1;
            sig.uses_rs1 = 1;
            sig.uses_rs2 = 0;
            break;

        case FMT_I_SYS:
            sig.reg_write = 0;
            sig.wb_src = WB_NONE;
            sig.mem_read = 0;
            sig.mem_write = 0;
            sig.is_branch = 0;
            sig.is_jump = 0;
            sig.is_system = 1;
            sig.uses_rs1 = 0;
            sig.uses_rs2 = 0;
            break;
    }

    uint32_t flags = instruction_flags(h);
    sig.src1 = sig.uses_rs1 ? i->rs1 + ((flags & IF_RS1_FP) ? 32 : 0) : 0;
    sig.src2 = sig.uses_rs2 ? i->rs2 + ((flags & IF_RS2_FP) ? 32 : 0) : 0;
    sig.src3 = sig.uses_rs3 ? i->rs3 + ((flags & IF_RS3_FP) ? 32 : 0) : 0;
    sig.dst = sig.reg_write ? i->rd + ((flags & IF_RD_FP) ? 32 : 0) : 0;
    return sig;
}