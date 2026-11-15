#include "branch_predictor.h"
#include <string.h>

static void reset_1bit(BPState *state) {
    memset(state->table, 0, sizeof(state->table));
}

static int predict_1bit(const BPState *state, uint32_t pc, uint32_t target) {
    (void)target;
    return state->table[bp_index(pc)] != 0;
}

static void update_1bit(BPState *state, uint32_t pc, uint32_t target, int taken) {
    (void)target;
    state->table[bp_index(pc)] = taken != 0;
}

static void reset_2bit(BPState *state) {
    memset(state->table, 1, sizeof(state->table)); /* Weakly not taken. */
}

static int predict_2bit(const BPState *state, uint32_t pc, uint32_t target) {
    (void)target;
    return state->table[bp_index(pc)] >= 2;
}

static void update_2bit(BPState *state, uint32_t pc, uint32_t target, int taken) {
    (void)target;
    uint8_t *counter = &state->table[bp_index(pc)];
    if (taken) {
        if (*counter < 3) ++*counter;
    } else if (*counter > 0) {
        --*counter;
    }
}

const BranchPredictor bp_1bit = {
    "1bit", "last outcome",
    reset_1bit, predict_1bit, update_1bit
};

const BranchPredictor bp_2bit = {
    "2bit", "saturating counter, initially weakly not taken",
    reset_2bit, predict_2bit, update_2bit
};
