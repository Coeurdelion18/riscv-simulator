#include "cli_display.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

void print_registers(const CPUState *cpu) {
    printf("---------------------------- registers ----------------------------\n");
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 4; col++) {
            int idx = row + col * 8;
            printf("x%-2d/%-4s=0x%08x  ", idx, cpu_reg_abi_name(idx), cpu_read_reg(cpu, idx));
        }
        printf("\n");
    }
    printf("------------------------- fp registers ----------------------------\n");
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 4; ++col) {
            int idx = row + col * 8;
            printf("f%-2d/%-4s=0x%08x  ", idx, cpu_freg_abi_name(idx), cpu_read_freg(cpu, idx));
        }
        printf("\n");
    }
    printf("fcsr =0x%08x  (frm=%u  flags:", cpu->fcsr, cpu_frm(cpu));
    static const char *names[] = {"NX", "UF", "OF", "DZ", "NV"};
    for (int bit = 4; bit >= 0; --bit)
        if (cpu->fcsr & (1u << bit)) printf(" %s", names[bit]);
    printf(")\n");
    printf("pc = 0x%08x\n", cpu->pc);
}

void print_status(const CPUState *cpu) {
    printf("status = %s   cycles = %llu\n", cpu_status_str(cpu->status),
           (unsigned long long)cpu->cycle_count);
    if (cpu->status == CPU_ILLEGAL_INSTRUCTION || cpu->status == CPU_MEM_FAULT) {
        printf("  -> %s\n", cpu->fault_msg);
    }
}

void print_memory_range(const Memory *mem, uint32_t base_addr, uint32_t length) {
    printf("------------------------ memory 0x%08x --------------------------\n", base_addr);
    for (uint32_t off = 0; off < length; off += 16) {
        printf("0x%08x: ", base_addr + off);
        for (uint32_t j = 0; j < 16 && off + j < length; j++) {
            uint8_t b;
            if (memory_read_byte(mem, base_addr + off + j, &b) == MEM_OK) {
                printf("%02x ", b);
            } else {
                printf("?? ");
            }
        }
        printf("\n");
    }
}

void print_stage_line(const SimEngine *e) {
    PipelineStageView v[SIM_MAX_STAGES];
    int n = e->stage_views(e, v, SIM_MAX_STAGES);
    printf("[cycle %5llu] ", (unsigned long long)e->cpu->cycle_count + 1);
    for (int i = 0; i < n; i++) {
        if (v[i].is_bubble) printf("%s ----------", v[i].label);
        else                printf("%s 0x%08x", v[i].label, v[i].pc);
        if (i + 1 < n) printf(" | ");
    }
    printf("\n");
}

void print_engine_stats(const SimEngine *e) {
    if (!e->format_stats) return;
    char buf[1024];
    e->format_stats(e, buf, sizeof(buf));
    printf("%s", buf);
}

static void print_help(void) {
    printf("commands:\n"
           "  step [n]          execute n clock cycles (default 1)\n"
           "  run               execute until halt/fault/breakpoint\n"
           "  regs              show register file\n"
           "  pipe              show what each pipeline stage holds next cycle\n"
           "  stats             show engine statistics (CPI, ...)\n"
           "  mem <addr> <len>  show memory (hex/decimal address, length in bytes)\n"
           "  break <addr>      set a breakpoint at address\n"
           "  clearbreak <addr> remove a breakpoint\n"
           "  status            show CPU status\n"
           "  help              show this message\n"
           "  quit / exit       leave the debugger\n");
}

#define MAX_BREAKPOINTS 64

void run_repl(SimEngine *e) {
    CPUState *cpu = e->cpu;
    Memory *mem = e->mem;
    uint32_t breakpoints[MAX_BREAKPOINTS];
    int nbreak = 0;
    char line[256];

    printf("RISC-V simulator (%s) -- interactive mode ('help' for commands)\n", e->name);
    print_status(cpu);

    while (1) {
        printf("(sim) ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) { printf("\n"); break; }

        char cmd[32] = {0}, arg1[64] = {0}, arg2[64] = {0};
        int nargs = sscanf(line, "%31s %63s %63s", cmd, arg1, arg2);
        if (nargs <= 0) continue;

        if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            break;
        } else if (strcmp(cmd, "help") == 0) {
            print_help();
        } else if (strcmp(cmd, "regs") == 0) {
            print_registers(cpu);
        } else if (strcmp(cmd, "status") == 0) {
            print_status(cpu);
        } else if (strcmp(cmd, "pipe") == 0) {
            print_stage_line(e);
        } else if (strcmp(cmd, "stats") == 0) {
            print_engine_stats(e);
        } else if (strcmp(cmd, "mem") == 0) {
            if (nargs < 3) { printf("usage: mem <addr> <len>\n"); continue; }
            uint32_t addr = (uint32_t)strtoul(arg1, NULL, 0);
            uint32_t len = (uint32_t)strtoul(arg2, NULL, 0);
            print_memory_range(mem, addr, len);
        } else if (strcmp(cmd, "break") == 0) {
            if (nargs < 2) { printf("usage: break <addr>\n"); continue; }
            if (nbreak >= MAX_BREAKPOINTS) { printf("too many breakpoints\n"); continue; }
            breakpoints[nbreak++] = (uint32_t)strtoul(arg1, NULL, 0);
            printf("breakpoint set at 0x%08x\n", breakpoints[nbreak - 1]);
        } else if (strcmp(cmd, "clearbreak") == 0) {
            if (nargs < 2) { printf("usage: clearbreak <addr>\n"); continue; }
            uint32_t addr = (uint32_t)strtoul(arg1, NULL, 0);
            int found = 0;
            for (int i = 0; i < nbreak; i++) {
                if (breakpoints[i] == addr) {
                    breakpoints[i] = breakpoints[--nbreak];
                    found = 1;
                    break;
                }
            }
            printf(found ? "breakpoint at 0x%08x removed\n" : "no breakpoint at 0x%08x\n", addr);
        } else if (strcmp(cmd, "step") == 0) {
            long n = (nargs >= 2) ? strtol(arg1, NULL, 0) : 1;
            for (long i = 0; i < n; i++) {
                if (cpu->status != CPU_RUNNING) { printf("machine is not running (%s)\n", cpu_status_str(cpu->status)); break; }
                e->step(e);
            }
            print_status(cpu);
            print_stage_line(e);
        } else if (strcmp(cmd, "run") == 0) {
            while (cpu->status == CPU_RUNNING) {
                e->step(e);
                for (int i = 0; i < nbreak; i++) {
                    if (cpu->pc == breakpoints[i] && cpu->status == CPU_RUNNING) {
                        printf("hit breakpoint at 0x%08x\n", cpu->pc);
                        goto run_done;
                    }
                }
            }
        run_done:
            print_status(cpu);
        } else {
            printf("unknown command '%s' (try 'help')\n", cmd);
        }
    }
}

void run_batch(SimEngine *e, uint64_t max_cycles, int trace) {
    CPUState *cpu = e->cpu;
    while (cpu->status == CPU_RUNNING && cpu->cycle_count < max_cycles) {
        if (trace) print_stage_line(e);
        e->step(e);
    }
    printf("\n");
    print_registers(cpu);
    print_status(cpu);
    if (cpu->status == CPU_RUNNING) {
        printf("  -> stopped: reached max cycle limit (%llu)\n", (unsigned long long)max_cycles);
    }
    print_engine_stats(e);
}
