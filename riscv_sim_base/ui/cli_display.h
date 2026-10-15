#ifndef CLI_DISPLAY_H
#define CLI_DISPLAY_H

#include "cpu_state.h"
#include "memory.h"

void print_registers(const CPUState *cpu);
void print_status(const CPUState *cpu);
void print_memory_range(const Memory *mem, uint32_t base_addr, uint32_t length);

/* Interactive step-debugger: step / run / regs / mem <addr> <len> /
 * break <addr> / continue / quit. Returns when the user quits or the
 * program halts and the user chooses not to continue looking around. */
void run_repl(CPUState *cpu, Memory *mem);

/* Runs to completion (or until max_cycles is hit) with no interaction,
 * then prints the final register file, status, and cycle count. Intended
 * for scripted / autograder use. */
void run_batch(CPUState *cpu, Memory *mem, uint64_t max_cycles);

#endif /* CLI_DISPLAY_H */
