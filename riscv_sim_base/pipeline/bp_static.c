#include "branch_predictor.h"
#include <string.h>

static void reset_static(BPState *state) {
    memset(state, 0, sizeof(*state));
}

static void update_static(BPState *state, uint32_t pc, uint32_t target, int taken) {
    (void)state;
    (void)pc;
    (void)target;
    (void)taken;
}

static int predict_not_taken(const BPState *state, uint32_t pc, uint32_t target) {
    (void)state;
    (void)pc;
    (void)target;
    return 0;
}

static int predict_btfn(const BPState *state, uint32_t pc, uint32_t target) {
    (void)state;
    return target < pc;
}

const BranchPredictor bp_not_taken = {
    "not-taken", "always predict not taken",
    reset_static, predict_not_taken, update_static
};

const BranchPredictor bp_btfn = {
    "btfn", "backward taken, forward not taken",
    reset_static, predict_btfn, update_static
};
