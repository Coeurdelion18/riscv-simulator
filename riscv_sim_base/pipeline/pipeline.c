#include "pipeline_state.h"
#include "cpu_state.h"

CPUStatus pipeline_cycle(Pipeline *p, CPUState *cpu, Memory *mem) {
    Pipeline nx = *p; /* nx = next state; p = current (old) state */
    /* ... every stage READS from p->..., WRITES into nx.... ... */
    // Implement WB
    // Reads p->mem_wb
    if(!p->mem_wb.valid) {
        // No work
    }
    else {
        if(p->mem_wb.fault != PF_NONE) {
            if(p->mem_wb.fault == PF_ILLEGAL) {
                cpu->status = CPU_ILLEGAL_INSTRUCTION;
            }
            else {
                cpu->status = CPU_MEM_FAULT;
            }
            cpu->fault_addr = mem_wb.fault_addr;
            snprintf(cpu->fault_msg, sizeof(cpu->fault_msg), "Pipeline Fault in MEM/WB at address %u", mem_wb.fault_addr);
            return cpu->status;
        }
        if (p->mem_wb.ctrl.reg_write) {
            cpu_write_reg(cpu, p->mem_wb.instr.rd, p->mem_wb.wb_val);
        }
        if (p->mem_wb.ctrl.is_system) {
            uint32_t temp_pc = cpu->pc;
            cpu->pc = p->mem_wb.pc;
            p->mem_wb.handler->execute(cpu, mem, &(p->mem_wb.instr));
            cpu->pc = temp_pc;
        }
        retired++;
        
        *p = nx; /* commit: one clock edge */
        return cpu->status;
    }
}