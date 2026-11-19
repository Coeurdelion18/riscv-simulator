#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_state.h"
#include "memory.h"
#include "isa_registry.h"
#include "rv32i_base.h"
#include "assembler.h"
#include "cli_display.h"
#include "pipeline_state.h"
#include "rv32f.h"

#define DEFAULT_MEM_SIZE   (1u << 20)   /* 1 MB */
#define DEFAULT_MAX_CYCLES 1000000ull

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s <program.s> [options]\n"
        "  --batch              run the pipeline and dump final state\n"
        "  --single-cycle       use the reference engine with --batch\n"
        "  --predictor NAME     not-taken, btfn, 1bit, or 2bit (default)\n"
        "  --hazard NAME        stall or forward (default)\n"
        "  --trace              print pipeline stages each cycle to stderr\n"
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
    int forwarding = -1;
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
            int selected;
            if (strcmp(argv[i], "--hazard") == 0) {
                if (i + 1 >= argc ||
                    (strcmp(argv[i + 1], "forward") != 0 &&
                     strcmp(argv[i + 1], "stall") != 0)) {
                    fprintf(stderr, "--hazard requires forward or stall\n");
                    return 1;
                }
                selected = strcmp(argv[++i], "forward") == 0;
            } else {
                batch_mode = 1;
                selected = strcmp(argv[i], "--pipeline-forward") == 0;
            }
            if (forwarding != -1 && forwarding != selected) {
                fprintf(stderr, "conflicting pipeline hazard modes\n");
                return 1;
            }
            forwarding = selected;
        } else if (strcmp(argv[i], "--max-cycles") == 0 && i + 1 < argc) {
            max_cycles = strtoull(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--mem-size") == 0 && i + 1 < argc) {
            mem_size = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else {
            fprintf(stderr, "unrecognized option '%s'\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (!batch_mode && (single_cycle || predictor || forwarding != -1 || trace)) {
        fprintf(stderr, "execution options require --batch\n");
        return 1;
    }
    if (single_cycle && (predictor || forwarding != -1 || trace)) {
        fprintf(stderr, "--predictor, --hazard, and --trace require pipeline batch mode\n");
        return 1;
    }

    /* 1. Bring up the ISA. Later labs add their own register_rv32m(),
     *    register_rv32f(), register_custom_ext(), etc. right here. */
    isa_registry_init();
    register_rv32i_base();
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

    if (single_cycle) {
        run_batch(&cpu, &mem, max_cycles);
    } else if (batch_mode) {
        pipeline_init(&p, forwarding != 0, predictor ? predictor : &bp_2bit);
        p.trace = trace;
        pipeline_run_batch(&p, &cpu, &mem, max_cycles);
    } else {
        run_repl(&cpu, &mem);
    }

    memory_free(&mem);
    return 0;
}
