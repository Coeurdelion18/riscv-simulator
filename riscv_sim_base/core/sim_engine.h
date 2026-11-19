#ifndef SIM_ENGINE_H
#define SIM_ENGINE_H

#include <stddef.h>
#include "cpu_state.h"
#include "memory.h"

#define SIM_MAX_STAGES 5

typedef struct {
    const char *label;
    uint32_t pc;
    int is_bubble;
} PipelineStageView;

typedef struct SimEngine SimEngine;
struct SimEngine {
    const char *name;
    CPUState *cpu;
    Memory *mem;
    void *state;
    CPUStatus (*step)(SimEngine *engine);
    int (*stage_views)(const SimEngine *engine, PipelineStageView *views, int capacity);
    void (*format_stats)(const SimEngine *engine, char *buffer, size_t capacity);
};

void single_cycle_engine_init(SimEngine *engine, CPUState *cpu, Memory *mem);

#endif
