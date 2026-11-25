#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "cpu_state.h"
#include "memory.h"
#include "isa_registry.h"
#include "rv32i_base.h"
#include "rv32m.h"
#include "rv32f.h"
#include "zicsr.h"
#include "assembler.h"
#include "cli_display.h"
#include "sim_engine.h"
#include "pipeline.h"

#define DEFAULT_MEM_SIZE   (1u << 20)   /* 1 MB */
#define DEFAULT_MAX_CYCLES 1000000ull

static int parse_unsigned(const char *text, uint64_t *value) {
    if (!*text || !isdigit((unsigned char)*text)) return 0;
    char *end;
    errno = 0;
    unsigned long long n = strtoull(text, &end, 0);
    if (errno == ERANGE || end == text || *end) return 0;
    *value = (uint64_t)n;
    return 1;
}

// Windows builds provide both executable names required by the evaluator and terminal.
static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s <program.s> [options]\n"
        "  --batch              run to completion with no interaction, then dump state\n"
        "  --trace              (with --batch) print every cycle's stage occupancy\n"
        "  --single-cycle       use the original single-cycle core instead of the pipeline\n"
        "  --hazard NAME        pipeline hazard unit: %s (default forward)\n"
        "  --predictor NAME     branch predictor: %s (default 2bit)\n"
        "  --max-cycles N       cycle limit for --batch mode (default %llu)\n"
        "  --no-predict         sequential fetch for historical Lab 2/3 timing\n"
        "  --pipeline           alias for --batch --hazard stall\n"
        "  --pipeline-forward   alias for --batch --hazard forward\n"
        "  --mem-size N         memory size in bytes (default %u)\n",
        prog, hazard_names(), bp_names(), (unsigned long long)DEFAULT_MAX_CYCLES, DEFAULT_MEM_SIZE);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *program_path = argv[1];
    int batch_mode = 0;
    int trace = 0;
    int single_cycle = 0;
    PipelineConfig pcfg = { &hazard_forward, &bp_2bit, 0 };
    uint64_t max_cycles = DEFAULT_MAX_CYCLES;
    uint32_t mem_size = DEFAULT_MEM_SIZE;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--batch") == 0) {
            batch_mode = 1;
        } else if (strcmp(argv[i], "--trace") == 0) {
            trace = 1;
        } else if (strcmp(argv[i], "--single-cycle") == 0) {
            single_cycle = 1;
        } else if (strcmp(argv[i], "--no-predict") == 0) {
            pcfg.no_prediction = 1;
        } else if (strcmp(argv[i], "--pipeline") == 0 || strcmp(argv[i], "--pipeline-forward") == 0) {
            batch_mode = 1;
            pcfg.hazard = strcmp(argv[i], "--pipeline") == 0 ? &hazard_stall : &hazard_forward;
        } else if (strcmp(argv[i], "--hazard") == 0 && i + 1 < argc) {
            pcfg.hazard = hazard_find(argv[++i]);
            if (!pcfg.hazard) {
                fprintf(stderr, "unknown hazard unit '%s' (available: %s)\n", argv[i], hazard_names());
                return 1;
            }
        } else if (strcmp(argv[i], "--predictor") == 0 && i + 1 < argc) {
            pcfg.predictor = bp_find(argv[++i]);
            if (!pcfg.predictor) {
                fprintf(stderr, "unknown branch predictor '%s' (available: %s)\n", argv[i], bp_names());
                return 1;
            }
        } else if (strcmp(argv[i], "--max-cycles") == 0 && i + 1 < argc) {
            if (!parse_unsigned(argv[++i], &max_cycles)) {
                fprintf(stderr, "--max-cycles requires a nonnegative integer\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--mem-size") == 0 && i + 1 < argc) {
            uint64_t size;
            if (!parse_unsigned(argv[++i], &size) || size == 0 || size > UINT32_MAX) {
                fprintf(stderr, "--mem-size requires an integer between 1 and 4294967295\n");
                return 1;
            }
            mem_size = (uint32_t)size;
        } else {
            fprintf(stderr, "unrecognized option '%s'\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    /* 1. Bring up the ISA. Later labs add their own register_rv32m(),
     *    register_rv32f(), register_custom_ext(), etc. right here. */
    isa_registry_init();
    register_rv32i_base();
    register_rv32m();       /* Lab 4: integer multiply/divide */
    register_rv32f();       /* Lab 5: single-precision floating point */
    register_zicsr();       /* Lab 5: CSR access (fflags / frm / fcsr) */

    /* 2. Set up memory and assemble the program into it. */
    Memory mem;
    if (memory_init(&mem, mem_size) != 0) {
        fprintf(stderr, "error: could not allocate %u bytes of simulated memory\n", mem_size);
        return 1;
    }

    uint32_t entry_point = 0;
    AsmError err;
    AsmStatus asm_status = assemble_file(program_path, &mem, &entry_point, &err);
    if (asm_status != ASM_OK) {
        fprintf(stderr, "assembly error");
        if (err.line_number > 0) fprintf(stderr, " (line %d)", err.line_number);
        fprintf(stderr, ": %s\n", err.message);
        memory_free(&mem);
        return 1;
    }

    printf("assembled '%s' ok. entry point = 0x%08x\n", program_path, entry_point);

    /* 3. Reset the CPU at the program's entry point, pick the machine
     *    (5-stage pipeline by default), and run it. */
    CPUState cpu;
    cpu_reset(&cpu, entry_point);

    SimEngine engine;
    PipelineState pipe;
    if (single_cycle) {
        sim_engine_single_cycle(&engine, &cpu, &mem);
    } else {
        pipeline_reset(&pipe, entry_point, &pcfg);
        pipeline_make_engine(&engine, &pipe, &cpu, &mem);
    }

    if (batch_mode) {
        run_batch(&engine, max_cycles, trace);
    } else {
        run_repl(&engine);
    }

    memory_free(&mem);
    return 0;
}
