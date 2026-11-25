#include "executor.h"
#include "decoder.h"
#include "isa_registry.h"
#include <stdio.h>

CPUStatus execute_cycle(CPUState *cpu, Memory *mem) {
    if (cpu->status != CPU_RUNNING) return cpu->status;

    uint32_t raw;
    if (memory_read_word(mem, cpu->pc, &raw) != MEM_OK) {
        cpu->status = CPU_MEM_FAULT;
        cpu->fault_addr = cpu->pc;
        snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                 "instruction fetch out of bounds at pc=0x%08x", cpu->pc);
        return cpu->status;
    }

    DecodedInstr instr;
    const InstructionHandler *handler;
    if (decode_instruction(raw, &instr, &handler) != DECODE_OK) {
        cpu->status = CPU_ILLEGAL_INSTRUCTION;
        cpu->fault_addr = cpu->pc;
        snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                 "unknown instruction 0x%08x at pc=0x%08x (no handler registered)",
                 raw, cpu->pc);
        return cpu->status;
    }

    /* Default: fall through to the next instruction. Branch/jump handlers
     * overwrite cpu->next_pc themselves when they're taken. */
    cpu->next_pc = cpu->pc + 4;

    handler->execute(cpu, mem, &instr);

    if (cpu->status == CPU_RUNNING) {
        cpu->pc = cpu->next_pc;
    }
    cpu->cycle_count++;

    return cpu->status;
}
