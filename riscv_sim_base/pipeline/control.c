#include "control.h"

ControlSignals control_decode(const DecodedInstr* i, const InstructionHandler* h) {
    // (void)i;
    ControlSignals sig = {0};
    uint32_t fl = isa_flags_of(h);

    switch (h->format) {
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

        case FMT_R2:            /* fsqrt, fcvt, fmv, fclass: rs2 is part of the opcode */
            sig.reg_write = 1;
            sig.wb_src = WB_ALU;
            sig.uses_rs1 = 1;
            sig.uses_rs2 = 0;
            break;

        case FMT_R4:            /* fused multiply-add: three sources */
            sig.reg_write = 1;
            sig.wb_src = WB_ALU;
            sig.uses_rs1 = 1;
            sig.uses_rs2 = 1;
            sig.uses_rs3 = 1;
            break;

        case FMT_CSR:           /* placeholder, refined when we do the CSR instructions */
            sig.reg_write = 1;
            sig.wb_src = WB_ALU;
            sig.uses_rs1 = 1;
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

    /* FP and CSR instructions compute via their registered handler in EX.
     * flw/fsw are excluded: they only need address generation, then use MEM. */
    if (!sig.mem_read && !sig.mem_write && ((fl & (IF_RD_FP | IF_RS1_FP | IF_RS2_FP | IF_RS3_FP | IF_HAS_RM)) || h->format == FMT_CSR)) sig.use_handler = 1;
    
    /* Unified register numbering: x1..x31 -> 1..31, f0..f31 -> 32..63.
     * x0 and unused operands -> REG_NONE. */
    #define UREG(idx, fp) ((fp) ? FREG_BASE + (int)(idx) : ((idx) == 0 ? REG_NONE : (int)(idx)))
    sig.src1 = sig.uses_rs1 ? UREG(i->rs1, fl & IF_RS1_FP) : REG_NONE;
    sig.src2 = sig.uses_rs2 ? UREG(i->rs2, fl & IF_RS2_FP) : REG_NONE;
    sig.src3 = sig.uses_rs3 ? UREG(i->rs3, fl & IF_RS3_FP) : REG_NONE;
    sig.dst  = sig.reg_write ? UREG(i->rd,  fl & IF_RD_FP)  : REG_NONE;
    #undef UREG

    return sig;
}