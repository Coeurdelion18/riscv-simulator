#ifndef HAZARD_H
#define HAZARD_H

/*
 * Lab 2: the hazard unit, as a plug-in.
 *
 * Once per cycle, before any stage runs, the pipeline describes what is in
 * each stage (only the control signals -- the hazard unit never sees data
 * values) and asks the selected HazardDetector two questions:
 *
 *   1. stall?   Must the instruction in ID wait? If so, IF and ID hold
 *               their contents and a bubble is sent into EX.
 *   2. forward? For each source operand of the instruction in EX, should
 *               the value read from the register file in ID be replaced by
 *               a newer one from the EX/MEM or MEM/WB latch?
 *
 * The pipeline applies the decision; the detector only decides. Three
 * detectors are provided (select with --hazard):
 *
 *   none     never stalls or forwards: reproduces Lab 1 (wrong results)
 *   stall    stalls ID until every producer has written back; no forwarding
 *   forward  EX->EX and MEM->EX forwarding; stalls only on load-use
 *
 * Writing another one means filling in a HazardDetector and adding it to
 * the table in hazard.c. Nothing else in the pipeline changes.
 *
 * Lab 5: register numbers are "unified" (control.h): x1..x31 = 1..31,
 * f0..f31 = 32..63, so the same comparisons cover both register files.
 * Instructions can now have a third source (src3).
 */

#include "control.h"

/* What one stage holds this cycle, as far as the hazard unit can see. */
typedef struct {
    int     valid;   /* 0 = bubble (ctrl is then meaningless) */
    Control ctrl;
} HazardSlot;

typedef struct {
    HazardSlot id;   /* in ID this cycle: will read the register file    */
    HazardSlot ex;   /* in EX this cycle (the ID/EX latch)                */
    HazardSlot mem;  /* in MEM this cycle (EX/MEM): result = alu_result   */
    HazardSlot wb;   /* in WB this cycle (MEM/WB): result = loaded/ALU value */
} HazardInput;

typedef enum {
    FWD_NONE = 0,    /* use the value read from the register file in ID */
    FWD_EX_MEM,      /* use the EX/MEM latch's ALU result (instruction now in MEM) */
    FWD_MEM_WB       /* use the MEM/WB latch's write-back value (instruction now in WB) */
} ForwardSel;

typedef struct {
    int        stall;   /* hold IF and ID this cycle, bubble into EX */
    ForwardSel fwd1;    /* for the EX instruction's src1 */
    ForwardSel fwd2;    /* for the EX instruction's src2 */
    ForwardSel fwd3;    /* for the EX instruction's src3 (Lab 5: fused multiply-add) */
} HazardDecision;

typedef struct {
    const char *name;
    const char *description;
    /* *out is zero-initialised (no stall, no forwarding) before the call. */
    void (*decide)(const HazardInput *in, HazardDecision *out);
} HazardDetector;

extern const HazardDetector hazard_none;
extern const HazardDetector hazard_stall;
extern const HazardDetector hazard_forward;

/* Looks a detector up by name ("none", "stall", "forward"); NULL if unknown. */
const HazardDetector *hazard_find(const char *name);

/* Comma-separated list of the available names, for usage messages. */
const char *hazard_names(void);

#endif /* HAZARD_H */
