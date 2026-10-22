#ifndef PIPELINE_STATE_H
#define PIPELINE_STATE_H

#include <stdint.h>
#include "isa_registry.h"
#include "control.h"
#include "cpu_state.h"
#include "memory.h"

#define PF_NONE 0
#define PF_ILLEGAL 1
#define PF_FETCH 2
#define PF_MEM 3

typedef struct {
    int valid;
    uint32_t pc;
    uint32_t raw;
    int fault;
} IFID;

typedef struct {
    int valid;
    uint32_t pc;
    int fault;
    DecodedInstr instr;
    const InstructionHandler* handler;
    ControlSignals ctrl;
    uint32_t rs1_val, rs2_val;
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
} MEMWB;

typedef struct {
    IFID if_id;
    IDEX id_ex;
    EXMEM ex_mem;
    MEMWB mem_wb;
    uint64_t cycles, retired, stalls, flushes;
} Pipeline;

void pipeline_init(Pipeline* p);
CPUStatus pipeline_cycle(Pipeline* p, CPUState* cpu, Memory* mem);
void pipeline_run_batch(Pipeline* p, CPUState* cpu, Memory* mem, uint64_t max_cycles);

#endif