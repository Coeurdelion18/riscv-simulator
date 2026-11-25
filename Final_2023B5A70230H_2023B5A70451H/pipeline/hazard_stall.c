#include "hazard.h"

/*
 * Naive interlock, no forwarding: the instruction in ID may only read the
 * register file once every older producer of its sources has written back.
 * (WB writes before ID reads in the same cycle.)
 */
static void decide_stall(const HazardInput *in, HazardDecision *out) {
    if (!in->id.valid) return;
    int sources[] = {in->id.ctrl.src1, in->id.ctrl.src2, in->id.ctrl.src3};
    for (int n = 0; n < 3; ++n) {
        int source = sources[n];
        if (source == REG_NONE) continue;
        if ((in->ex.valid && in->ex.ctrl.dst == source) ||
            (in->mem.valid && in->mem.ctrl.dst == source)) out->stall = 1;
    }
}

const HazardDetector hazard_stall = {
    "stall",
    "stall in ID until producers write back; no forwarding",
    decide_stall,
};
