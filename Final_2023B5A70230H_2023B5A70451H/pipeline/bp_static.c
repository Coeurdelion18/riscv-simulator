#include "branch_predictor.h"
#include <string.h>

/* Static predictors keep no state; reset/update do nothing. */

static void reset_none(BPState *s) {
    memset(s, 0, sizeof(*s));
}

static void update_none(BPState *s, uint32_t pc, uint32_t target, int taken) {
    (void)s; (void)pc; (void)target; (void)taken;
}

/* ---------------- always not taken ---------------- */

static int predict_not_taken(const BPState *s, uint32_t pc, uint32_t target) {
    (void)s; (void)pc; (void)target;
    return 0;
}

const BranchPredictor bp_not_taken = {
    "not-taken",
    "static: always predict not taken",
    reset_none,
    predict_not_taken,
    update_none,
};

/* ---------------- backward taken, forward not taken ---------------- */

/* Loops branch backwards (taken every iteration but the last); forward
 * branches usually skip rarely-executed code. The target is known in IF
 * from the pre-decoded immediate, so this costs no state at all. */
static int predict_btfn(const BPState *s, uint32_t pc, uint32_t target) {
    (void)s;
    return target < pc;
}

const BranchPredictor bp_btfn = {
    "btfn",
    "static: backward taken, forward not taken",
    reset_none,
    predict_btfn,
    update_none,
};
