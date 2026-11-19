#ifndef CLI_DISPLAY_H
#define CLI_DISPLAY_H

#include "cpu_state.h"
#include "memory.h"
#include "sim_engine.h"

void print_registers(const CPUState *cpu);
void print_status(const CPUState *cpu);
void print_memory_range(const Memory *mem, uint32_t base_addr, uint32_t length);

/* One line: what every stage holds, e.g.
 *   IF 0x00000010 | ID 0x0000000c | EX ---------- | MEM 0x00000004 | WB 0x00000000 */
void print_stage_line(const SimEngine *e);

/* Engine statistics (CPI, stalls, ...), if the engine provides any. */
void print_engine_stats(const SimEngine *e);

/* Interactive step-debugger: step / run / regs / pipe / stats /
 * mem <addr> <len> / break <addr> / clearbreak <addr> / status / quit.
 * One "step" is one clock cycle of whichever engine is running. */
void run_repl(SimEngine *e);

/* Runs to completion (or until max_cycles is hit) with no interaction,
 * then prints the final register file, status, cycle count and engine
 * statistics. Intended for scripted / autograder use. If `trace` is
 * non-zero, prints one print_stage_line() per cycle before executing it. */
void run_batch(SimEngine *e, uint64_t max_cycles, int trace);

#endif /* CLI_DISPLAY_H */
