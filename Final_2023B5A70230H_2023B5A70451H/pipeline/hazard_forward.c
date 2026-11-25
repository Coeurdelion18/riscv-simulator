#include "hazard.h"

/*
 * Full forwarding unit:
 *   EX/MEM -> EX   producer one instruction ahead (ALU result)
 *   MEM/WB -> EX   producer two ahead (ALU result or loaded value)
 *   (three ahead needs nothing: WB writes before ID reads)
 * plus a 1-cycle load-use stall. FP registers (unified numbers 32..63) are
 * forwarded exactly like integer ones.
 */
static ForwardSel select_source(int source, const HazardInput *in) {
    if (source == REG_NONE) return FWD_NONE;
    if (in->mem.valid && in->mem.ctrl.dst == source)
        return in->mem.ctrl.mem_op == MEMOP_LOAD ? FWD_NONE : FWD_EX_MEM;
    if (in->wb.valid && in->wb.ctrl.dst == source) return FWD_MEM_WB;
    return FWD_NONE;
}

static void decide_forward(const HazardInput *in, HazardDecision *out) {
    if (in->ex.valid) {
        out->fwd1 = select_source(in->ex.ctrl.src1, in);
        out->fwd2 = select_source(in->ex.ctrl.src2, in);
        out->fwd3 = select_source(in->ex.ctrl.src3, in);
    }
    if (in->id.valid && in->ex.valid && in->ex.ctrl.mem_op == MEMOP_LOAD &&
        in->ex.ctrl.dst != REG_NONE) {
        int dst = in->ex.ctrl.dst;
        out->stall = dst == in->id.ctrl.src1 || dst == in->id.ctrl.src2 || dst == in->id.ctrl.src3;
    }
}

const HazardDetector hazard_forward = {
    "forward",
    "EX->EX and MEM->EX forwarding, 1-cycle stall on load-use",
    decide_forward,
};
