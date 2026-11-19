#include "branch_predictor.h"
#include <string.h>

const BranchPredictor *bp_find(const char *name) {
    static const BranchPredictor *const policies[] = {
        &bp_not_taken, &bp_btfn, &bp_1bit, &bp_2bit
    };

    if (name != NULL)
        for (unsigned i = 0; i < sizeof(policies) / sizeof(policies[0]); ++i)
            if (strcmp(name, policies[i]->name) == 0)
                return policies[i];
    return NULL;
}
