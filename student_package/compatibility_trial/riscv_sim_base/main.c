#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "cpu_state.h"
#include "memory.h"
#include "isa_registry.h"
#include "rv32i_base.h"
#include "rv32f.h"
#include "rv32m.h"
#include "assembler.h"
#include "cli_display.h"
#include "pipeline_state.h"

#define DEFAULT_MEM_SIZE   (1u << 20)   /* 1 MB */
#define DEFAULT_MAX_CYCLES 1000000ull

static int parse_unsigned(const char *text, uint64_t *value) {
    char *end;
    if (!*text || *text == '-') return 0;
    errno = 0;
    unsigned long long parsed = strtoull(text, &end, 0);
    if (errno == ERANGE || end == text || *end != '\0') return 0;
    *value = (uint64_t)parsed;
    return 1;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s <program.s> [options]\n"
        "  --batch              run the pipeline and dump final state\n"
        "  --single-cycle       use the reference engine with --batch\n"
        "  --predictor NAME     not-taken, btfn, 1bit, or 2bit (default)\n"
        "  --hazard NAME        none, stall, or forward (default)\n"
        "  --trace              print stages each cycle\n"
        "  --max-cycles N       cycle limit for --batch mode (default %llu)\n"
        "  --mem-size N         memory size in bytes (default %u)\n"
        "  --pipeline           alias for --batch --hazard stall\n"
        "  --pipeline-forward   alias for --batch --hazard forward\n",
        prog, (unsigned long long)DEFAULT_MAX_CYCLES, DEFAULT_MEM_SIZE);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *program_path = argv[1];
    int batch_mode = 0, single_cycle = 0, trace = 0;
    HazardMode hazard_mode = HAZARD_FORWARD;
    int hazard_set = 0;
    const BranchPredictor *predictor = NULL;
    uint64_t max_cycles = DEFAULT_MAX_CYCLES;
    uint32_t mem_size = DEFAULT_MEM_SIZE;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--batch") == 0) {
            batch_mode = 1;
        } else if (strcmp(argv[i], "--single-cycle") == 0) {
            single_cycle = 1;
        } else if (strcmp(argv[i], "--trace") == 0) {
            trace = 1;
        } else if (strcmp(argv[i], "--predictor") == 0) {
            if (i + 1 >= argc || !(predictor = bp_find(argv[++i]))) {
                fprintf(stderr, "--predictor requires not-taken, btfn, 1bit, or 2bit\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--hazard") == 0 ||
                   strcmp(argv[i], "--pipeline") == 0 ||
                   strcmp(argv[i], "--pipeline-forward") == 0) {
            HazardMode selected;
            if (strcmp(argv[i], "--hazard") == 0) {
                if (i + 1 >= argc ||
                    (strcmp(argv[i + 1], "forward") != 0 &&
                     strcmp(argv[i + 1], "stall") != 0 &&
                     strcmp(argv[i + 1], "none") != 0)) {
                    fprintf(stderr, "--hazard requires none, stall, or forward\n");
                    return 1;
                }
                ++i;
                selected = strcmp(argv[i], "forward") == 0 ? HAZARD_FORWARD :
                           strcmp(argv[i], "stall") == 0 ? HAZARD_STALL : HAZARD_NONE;
            } else {
                batch_mode = 1;
                selected = strcmp(argv[i], "--pipeline-forward") == 0 ? HAZARD_FORWARD : HAZARD_STALL;
            }
            if (hazard_set && hazard_mode != selected) {
                fprintf(stderr, "conflicting pipeline hazard modes\n");
                return 1;
            }
            hazard_mode = selected;
            hazard_set = 1;
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

    if (!batch_mode && (single_cycle || predictor || hazard_set || trace)) {
        fprintf(stderr, "execution options require --batch\n");
        return 1;
    }
    if (single_cycle && (predictor || hazard_set || trace)) {
        fprintf(stderr, "--predictor, --hazard, and --trace require pipeline batch mode\n");
        return 1;
    }

    /* 1. Bring up the ISA. Later labs add their own register_rv32m(),
     *    register_rv32f(), register_custom_ext(), etc. right here. */
    isa_registry_init();
    register_rv32i_base();
    register_rv32m();
    register_rv32f();

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

    /* 3. Reset the CPU at the program's entry point and run it. */
    CPUState cpu;
    cpu_reset(&cpu, entry_point);
    Pipeline p;

    SimEngine engine;
    if (single_cycle || !batch_mode) {
        single_cycle_engine_init(&engine, &cpu, &mem);
    } else {
        pipeline_init(&p, hazard_mode, predictor ? predictor : &bp_2bit);
        pipeline_engine_init(&engine, &p, &cpu, &mem);
    }
    if (batch_mode)
        run_batch(&engine, max_cycles, trace);
    else
        run_repl(&engine);

    memory_free(&mem);
    return 0;
}
