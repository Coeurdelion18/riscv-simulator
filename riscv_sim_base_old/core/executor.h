#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "cpu_state.h"
#include "memory.h"

/* Runs exactly one fetch-decode-execute cycle (this is the single-cycle
 * base machine -- Lab 1 replaces this with a staged pipeline, but the
 * fetch/decode/dispatch pieces it reuses live in decoder.c/isa_registry.c
 * unchanged). Returns the resulting CPUStatus; the caller (the CLI) should
 * stop calling this once status != CPU_RUNNING. */
CPUStatus execute_cycle(CPUState *cpu, Memory *mem);

#endif /* EXECUTOR_H */
