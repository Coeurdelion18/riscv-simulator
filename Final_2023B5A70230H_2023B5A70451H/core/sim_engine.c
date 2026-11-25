#include "sim_engine.h"
#include "executor.h"

static CPUStatus single_cycle_step(SimEngine *e) {
    return execute_cycle(e->cpu, e->mem);
}

static int single_cycle_views(const SimEngine *e, PipelineStageView *out, int max) {
    if (max < 1) return 0;
    out[0].label = "EXEC";
    out[0].pc = e->cpu->pc;
    out[0].is_bubble = (e->cpu->status != CPU_RUNNING);
    return 1;
}

void sim_engine_single_cycle(SimEngine *e, CPUState *cpu, Memory *mem) {
    e->name = "single-cycle";
    e->cpu = cpu;
    e->mem = mem;
    e->ctx = NULL;
    e->step = single_cycle_step;
    e->stage_views = single_cycle_views;
    e->format_stats = NULL; /* cycles == instructions; print_status already shows it */
}
