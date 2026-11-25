#include "hazard.h"

/* No hazard unit at all: exactly the Lab 1 behaviour. Kept so the
 * difference forwarding makes can be shown on the same binary. */
static void decide_none(const HazardInput *in, HazardDecision *out) {
    (void)in;
    (void)out;
}

const HazardDetector hazard_none = {
    "none",
    "no stalls, no forwarding (Lab 1 behaviour: RAW hazards give stale values)",
    decide_none,
};
