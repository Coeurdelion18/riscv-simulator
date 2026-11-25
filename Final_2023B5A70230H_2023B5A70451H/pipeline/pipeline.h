#ifndef PIPELINE_H
#define PIPELINE_H

/*
 * Lab 5: 5-stage in-order pipeline (IF, ID, EX, MEM, WB) with a pluggable
 * hazard unit (pipeline/hazard.h: stalls, EX->EX / MEM->EX forwarding), a
 * pluggable branch predictor (pipeline/branch_predictor.h), running
 * RV32IMF. Register numbers in Control are unified (x1..x31, f0..f31 ->
 * 1..63, see control.h), so the latches and hazard logic carry FP values
 * exactly like integer ones. fcsr (rounding mode + flags) is read and
 * written in EX, in program order; nothing younger than an instruction in
 * EX is ever kept after a fault, so no fcsr hazards can arise.
 *
 * Stage responsibilities:
 *   IF   fetch the word at fetch_pc into IF/ID; fetch_pc += 4
 *   ID   decode, derive control signals, read the register file into ID/EX
 *   EX   run the instruction's handler on a scratch CPUState holding the
 *        latched operands -> ALU result / branch outcome; compute the
 *        address of a load/store; resolve branches and jumps
 *   MEM  perform the load or store
 *   WB   write the destination register; retire the instruction
 *
 * Timing rules (these are what make data hazards observable):
 *   - WB writes the register file BEFORE ID reads it in the same cycle
 *     ("write in the first half, read in the second half"), so a producer
 *     three instructions ahead of a consumer is always seen.
 *   - Data hazards are handled by the selected HazardDetector (Lab 2).
 *     Once per cycle, before any stage runs, it sees the control signals
 *     of every stage and decides (a) whether ID must stall and (b) where
 *     each source operand of the EX instruction comes from: the value read
 *     in ID, the EX/MEM latch, or the MEM/WB latch. With --hazard none it
 *     reproduces Lab 1 (stale values).
 *   - A stall holds IF and ID and sends a bubble into EX.
 *   - IF predicts: a conditional branch asks the BranchPredictor, a jal is
 *     redirected to its target immediately (no penalty), everything else
 *     (including jalr) continues at pc + 4. The guess (pred_next) travels
 *     with the instruction.
 *   - Branches and jumps resolve in EX. If the real next PC differs from
 *     pred_next, the two younger instructions in IF and ID are squashed and
 *     fetch restarts at the right address (2-cycle penalty).
 *
 * Exceptions (illegal instruction, fetch/load/store out of bounds) are
 * *carried* down the latches and raised only when the instruction reaches
 * WB. A wrong-path instruction that gets squashed therefore never faults --
 * which matters, because IF happily fetches past the final ecall into .data.
 */

#include <stddef.h>
#include "cpu_state.h"
#include "memory.h"
#include "isa_registry.h"
#include "sim_engine.h"
#include "control.h"
#include "hazard.h"
#include "branch_predictor.h"

/* A fault that will be raised if (and only if) this instruction retires. */
typedef struct {
    CPUStatus status;      /* CPU_RUNNING = no fault */
    uint32_t  addr;
    char      msg[128];
} PipeFault;

typedef struct {
    int       valid;       /* 0 = bubble */
    uint32_t  pc;
    uint32_t  raw;
    PipeFault fault;
    uint32_t  pred_next;   /* the address IF fetched next (its prediction) */
    int       pred_taken;
} IF_ID_Latch;

typedef struct {
    int       valid;
    uint32_t  pc;
    uint32_t  raw;
    PipeFault fault;
    const InstructionHandler *handler;
    DecodedInstr instr;
    Control   ctrl;
    uint32_t  src1_val;    /* register-file values read in ID */
    uint32_t  src2_val;
    uint32_t  src3_val;    /* Lab 5: fused multiply-add rs3 */
    uint32_t  pred_next;   /* checked against the real next PC in EX */
    int       pred_taken;
} ID_EX_Latch;

typedef struct {
    int       valid;
    uint32_t  pc;
    uint32_t  raw;
    PipeFault fault;
    const InstructionHandler *handler;   /* for the mnemonic in fault messages */
    Control   ctrl;
    uint32_t  alu_result;  /* value for dst, or the effective address of a load/store */
    uint32_t  store_val;   /* stores only */
    int       exit_requested;  /* ecall exit: halt when this retires */
} EX_MEM_Latch;

typedef struct {
    int       valid;
    uint32_t  pc;
    uint32_t  raw;
    PipeFault fault;
    Control   ctrl;
    uint32_t  wb_val;      /* value written to dst in WB */
    int       exit_requested;
} MEM_WB_Latch;

typedef struct {
    uint64_t retired;          /* instructions that completed WB */
    uint64_t squashed;         /* wrong-path instructions flushed from IF/ID */
    uint64_t redirects;        /* EX flushes: mispredicted branches + jalr */
    uint64_t stalls;           /* cycles in which ID was held by the hazard unit */
    uint64_t forwards;         /* source operands taken from EX/MEM or MEM/WB */
    // Count forwarded source operands separately for the execute and writeback bypass paths.
    uint64_t fwd_ex, fwd_mem;
    uint64_t load_use_stalls;
    uint64_t branches;         /* conditional branches executed */
    uint64_t branch_mispredicts;
    uint64_t jal;              /* redirected in IF, never flushed */
    uint64_t jalr;             /* never predicted: always a redirect unless target == pc + 4 */
} PipelineStats;

/* Plug-ins chosen on the command line. */
typedef struct {
    const HazardDetector  *hazard;
    const BranchPredictor *predictor;
    // Sequential fetching reproduces historical pipeline timing without predicting conditional branches or jumps.
    int no_prediction;
} PipelineConfig;

typedef struct {
    IF_ID_Latch  if_id;
    ID_EX_Latch  id_ex;
    EX_MEM_Latch ex_mem;
    MEM_WB_Latch mem_wb;

    uint32_t fetch_pc;
    int      fetch_stopped;    /* an exit/fault is in flight: fetch nothing more */

    PipelineConfig cfg;
    BPState        bp;         /* the predictor's table */
    PipelineStats  stats;
} PipelineState;

void      pipeline_reset(PipelineState *p, uint32_t start_pc, const PipelineConfig *cfg);

/* Advances the pipeline by one clock cycle. */
CPUStatus pipeline_step(PipelineState *p, CPUState *cpu, Memory *mem);

/* IF, ID, EX, MEM, WB: what each stage will hold in the next cycle. */
int       pipeline_stage_views(const PipelineState *p, PipelineStageView *out, int max);

void      pipeline_format_stats(const PipelineState *p, const CPUState *cpu, char *buf, size_t n);

/* Wraps a PipelineState as a SimEngine for the UI. */
void      pipeline_make_engine(SimEngine *e, PipelineState *p, CPUState *cpu, Memory *mem);

#endif /* PIPELINE_H */
