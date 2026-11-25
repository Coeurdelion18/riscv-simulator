#ifndef BRANCH_PREDICTOR_H
#define BRANCH_PREDICTOR_H

/*
 * Lab 3: branch direction prediction, as a plug-in.
 *
 * Where it sits in the pipeline:
 *
 *   IF   fetches a word and pre-decodes it (opcode + immediate only -- a
 *        few gates in hardware, standing in for a BTB that always hits):
 *          conditional branch -> ask predict(); if it says taken, the
 *                                next fetch is the branch target
 *          jal                -> always redirected to its target in IF
 *          anything else      -> next fetch is pc + 4
 *        The guess travels down the pipe with the instruction.
 *   EX   resolves the branch, calls update() with the real outcome, and if
 *        the guess was wrong flushes IF and ID and refetches from the right
 *        address (2-cycle penalty). jalr is never predicted: it always
 *        resolves in EX.
 *
 * A predictor only answers "taken or not" for conditional branches. Its
 * state is a small table (BPState) owned by the pipeline and indexed by
 * bp_index(pc); what each entry means is up to the predictor.
 *
 * Provided (select with --predictor):
 *   not-taken   static: always predict not taken
 *   btfn        static: backward taken, forward not taken
 *   1bit        dynamic: remember the last outcome of each branch
 *   2bit        dynamic: 2-bit saturating counter per branch (default)
 */

#include <stdint.h>

#define BP_TABLE_BITS 6
#define BP_TABLE_SIZE (1 << BP_TABLE_BITS)   /* 64 entries */

typedef struct {
    uint8_t table[BP_TABLE_SIZE];
} BPState;

/* Branch PCs are word-aligned, so the low 2 bits carry no information:
 * entry = pc[7:2]. Branches whose PCs differ by a multiple of 256 bytes
 * share (alias to) the same entry. */
static inline int bp_index(uint32_t pc) {
    return (int)((pc >> 2) & (BP_TABLE_SIZE - 1));
}

typedef struct {
    const char *name;
    const char *description;
    void (*reset)(BPState *s);
    /* 1 = predict taken. `target` is the branch's taken address. */
    int  (*predict)(const BPState *s, uint32_t pc, uint32_t target);
    /* Called in EX once the real outcome is known. */
    void (*update)(BPState *s, uint32_t pc, uint32_t target, int taken);
} BranchPredictor;

extern const BranchPredictor bp_not_taken;
extern const BranchPredictor bp_btfn;
extern const BranchPredictor bp_1bit;
extern const BranchPredictor bp_2bit;

/* Looks a predictor up by name; NULL if unknown. */
const BranchPredictor *bp_find(const char *name);

/* Comma-separated list of the available names, for usage messages. */
const char *bp_names(void);

#endif /* BRANCH_PREDICTOR_H */
