#include "sim_engine.h"
#include "executor.h"

static CPUStatus single_cycle_step(SimEngine *engine) {
    return execute_cycle(engine->cpu, engine->mem);
}

static int single_cycle_views(const SimEngine *engine, PipelineStageView *views, int capacity) {
    if (capacity < 1) return 0;
    views[0] = (PipelineStageView){"SC", engine->cpu->pc,
                                 engine->cpu->status != CPU_RUNNING};
    return 1;
}

void single_cycle_engine_init(SimEngine *engine, CPUState *cpu, Memory *mem) {
    *engine = (SimEngine){"single-cycle", cpu, mem, NULL,
                         single_cycle_step, single_cycle_views, NULL};
}
