#ifndef PIPELINE_STATE_H
#define PIPELINE_STATE_H

#include <stdint.h>
#include "isa_registry.h"
#include "control.h"
#include "cpu_state.h"
#include "memory.h"
#include "branch_predictor.h"

#define PF_NONE 0
#define PF_ILLEGAL 1
#define PF_FETCH 2
#define PF_MEM 3

typedef struct {
    int valid;
    uint32_t pc;
    uint32_t raw;
    int fault;
    uint64_t load_use_cycles;
    uint64_t stall_cycles; /* Penalties belong to this instruction, not speculation. */
    uint32_t pred_next;
    int pred_taken;
} IFID;

typedef struct {
    int valid;
    uint32_t pc;
    int fault;
    DecodedInstr instr;
    const InstructionHandler* handler;
    ControlSignals ctrl;
    uint32_t rs1_val, rs2_val, rs3_val;
    uint64_t load_use_cycles;
    uint64_t stall_cycles;
    uint32_t pred_next;
    int pred_taken;
} IDEX;

typedef struct {
    int valid;
    uint32_t pc;
    int fault;
    DecodedInstr instr;
    const InstructionHandler* handler;
    ControlSignals ctrl;
    uint32_t alu_result;
    uint32_t store_val;
    uint32_t fault_addr;
    uint64_t load_use_cycles;
    uint64_t stall_cycles;
    int redirected, mispredicted;
    int fwd_ex, fwd_mem;
} EXMEM;

typedef struct {
    int valid;
    uint32_t pc;
    int fault;
    DecodedInstr instr;
    const InstructionHandler* handler;
    ControlSignals ctrl;
    uint32_t wb_val;
    uint32_t fault_addr;
    uint64_t load_use_cycles;
    uint64_t stall_cycles;
    int redirected, mispredicted;
    int fwd_ex, fwd_mem;
} MEMWB;

typedef struct {
    IFID if_id;
    IDEX id_ex;
    EXMEM ex_mem;
    MEMWB mem_wb;
    uint64_t cycles, retired, stalls, redirects, load_use_stalls, fwd_ex, fwd_mem;
    uint64_t branches, mispredicts, jal, jalr;
    const BranchPredictor *predictor;
    BPState bp_state;
    int forwarding;
    int trace;
} Pipeline;

void pipeline_init(Pipeline* p, int forwarding, const BranchPredictor *predictor);
CPUStatus pipeline_cycle(Pipeline* p, CPUState* cpu, Memory* mem);
void pipeline_run_batch(Pipeline* p, CPUState* cpu, Memory* mem, uint64_t max_cycles);

#endif
