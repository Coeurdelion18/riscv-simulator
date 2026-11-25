#ifndef SIM_ENGINE_H
#define SIM_ENGINE_H

/*
 * A SimEngine is "a machine the UI can clock". Lab 0 had exactly one
 * machine (the single-cycle executor), and the UI called execute_cycle()
 * directly. From Lab 1 on there are two -- the original single-cycle core
 * (kept as a correctness oracle, selected with --single-cycle) and the
 * 5-stage pipeline -- so the UI talks to whichever one main.c hands it
 * through this small function-pointer interface instead.
 *
 * Nothing in ui/ knows how a step is implemented; nothing in the machine
 * knows how it is displayed.
 */

#include <stddef.h>
#include "cpu_state.h"
#include "memory.h"

/* One row of "what's active right now" for the REPL "pipe" command / --trace.
 * The single-cycle machine reports a single "EXEC" entry; the pipeline
 * reports five (IF, ID, EX, MEM, WB). is_bubble = 1 means the stage holds
 * no real instruction this cycle (empty at start-up, squashed by a flush,
 * or -- from Lab 2 on -- a stall bubble). */
typedef struct {
    const char *label;
    uint32_t    pc;
    int         is_bubble;
} PipelineStageView;

#define SIM_MAX_STAGES 8

typedef struct SimEngine {
    const char *name;      /* shown in banners, e.g. "5-stage pipeline" */
    CPUState   *cpu;
    Memory     *mem;
    void       *ctx;       /* engine-private state (e.g. PipelineState*) */

    /* Advance the machine by exactly one clock cycle. */
    CPUStatus (*step)(struct SimEngine *e);

    /* Fill `out` with what each stage will work on in the NEXT cycle.
     * Returns the number of entries written (<= max). */
    int (*stage_views)(const struct SimEngine *e, PipelineStageView *out, int max);

    /* Optional (may be NULL): human-readable statistics, one item per
     * line, written into buf. Used by --batch and the REPL "stats" command. */
    void (*format_stats)(const struct SimEngine *e, char *buf, size_t buf_size);
} SimEngine;

/* Wraps the original Lab 0 single-cycle executor as a SimEngine. */
void sim_engine_single_cycle(SimEngine *e, CPUState *cpu, Memory *mem);

#endif /* SIM_ENGINE_H */
