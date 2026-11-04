#include "pipeline_state.h"

typedef struct{
    int stall; /* NEW: flag to toggle forwarding mode */
    int load_use; /* the producer is a load still in EX (Task 3) */
}HazardVerdict;

typedef struct{
    uint32_t a,b; /*the operands that EX should use*/
    int fwd_ex, fwd_mem; /*how many operands came from each path. Can be 0,1 or 2 */
}Operands;

Operands forward_operands(const Pipeline *p, const IDEX *d); // This is the mux. Deciding where the value comes from.

HazardVerdict hazard_check(const Pipeline *p, const DecodedInstr *d, const ControlSignals *c);

/* valid latch + really writes a register + not x0 + same register number */
int produces(int valid, const ControlSignals *lc, uint32_t rd, uint32_t rs);
