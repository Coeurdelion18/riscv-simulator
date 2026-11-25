#include "branch_predictor.h"
#include <string.h>

static const BranchPredictor *const g_predictors[] = {
    &bp_not_taken,
    &bp_btfn,
    &bp_1bit,
    &bp_2bit,
};

#define NUM_PREDICTORS ((int)(sizeof(g_predictors) / sizeof(g_predictors[0])))

const BranchPredictor *bp_find(const char *name) {
    for (int i = 0; i < NUM_PREDICTORS; i++) {
        if (strcmp(g_predictors[i]->name, name) == 0) return g_predictors[i];
    }
    return NULL;
}

const char *bp_names(void) {
    return "not-taken, btfn, 1bit, 2bit";
}
