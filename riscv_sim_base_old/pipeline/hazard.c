#include "hazard.h"

int produces(int valid, const ControlSignals *ctrl, uint32_t dst, uint32_t source) {
    return valid && ctrl->reg_write && dst != 0 && dst == source;
}

static int depends(const ControlSignals *consumer, int valid, int fault, const ControlSignals *producer) {
    return fault == PF_NONE &&
        (produces(valid, producer, producer->dst, consumer->src1) ||
         produces(valid, producer, producer->dst, consumer->src2) ||
         produces(valid, producer, producer->dst, consumer->src3));
}

HazardVerdict hazard_check(const Pipeline *p, const DecodedInstr *d, const ControlSignals *ctrl) {
    (void)d;
    HazardVerdict verdict = {0, 0};
    if (p->hazard_mode == HAZARD_NONE) return verdict;
    if (p->hazard_mode == HAZARD_STALL) {
        /* WB runs before ID reads the register files in the same cycle. */
        verdict.stall = depends(ctrl, p->id_ex.valid, p->id_ex.fault, &p->id_ex.ctrl) ||
                        depends(ctrl, p->ex_mem.valid, p->ex_mem.fault, &p->ex_mem.ctrl);
    } else if (p->id_ex.ctrl.mem_read &&
               depends(ctrl, p->id_ex.valid, p->id_ex.fault, &p->id_ex.ctrl)) {
        verdict.stall = verdict.load_use = 1;
    }
    return verdict;
}

static uint32_t forwarded(const Pipeline *p, uint32_t source, uint32_t value, Operands *out) {
    if (source == 0) return value;
    if (p->ex_mem.fault == PF_NONE &&
        produces(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.ctrl.dst, source)) {
        /* A load's address is never a forwardable result. */
        if (!p->ex_mem.ctrl.mem_read) {
            out->fwd_ex++;
            return p->ex_mem.alu_result;
        }
        return value;
    }
    if (p->mem_wb.fault == PF_NONE &&
        produces(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.ctrl.dst, source)) {
        out->fwd_mem++;
        return p->mem_wb.wb_val;
    }
    return value;
}

Operands forward_operands(const Pipeline *p, const IDEX *d) {
    Operands out = {d->rs1_val, d->rs2_val, d->rs3_val, 0, 0};
    if (p->hazard_mode == HAZARD_FORWARD && d->valid && d->fault == PF_NONE) {
        out.a = forwarded(p, d->ctrl.src1, out.a, &out);
        out.b = forwarded(p, d->ctrl.src2, out.b, &out);
        out.c = forwarded(p, d->ctrl.src3, out.c, &out);
    }
    return out;
}
