#include "pipeline_state.h"
#include "cpu_state.h"
#include "decoder.h"
#include "cli_display.h"
#include "hazard.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

void pipeline_init(Pipeline *p, HazardMode hazard_mode, const BranchPredictor *predictor) {
    memset(p, 0, sizeof(*p));
    p->hazard_mode = hazard_mode;
    p->predictor = predictor;
    predictor->reset(&p->bp_state);
}

static void trace_slot(const char *stage, int valid, uint32_t pc) {
    if (valid)
        fprintf(stderr, " %s 0x%08" PRIx32, stage, pc);
    else
        fprintf(stderr, " %s ----------", stage);
}

static void trace_cycle(const Pipeline *p, uint32_t fetch_pc) {
    fprintf(stderr, "[cycle %" PRIu64 "]", p->cycles + 1);
    trace_slot("IF", 1, fetch_pc);
    trace_slot("| ID", p->if_id.valid, p->if_id.pc);
    trace_slot("| EX", p->id_ex.valid, p->id_ex.pc);
    trace_slot("| MEM", p->ex_mem.valid, p->ex_mem.pc);
    trace_slot("| WB", p->mem_wb.valid, p->mem_wb.pc);
    fputc('\n', stderr);
}

static CPUStatus pipeline_engine_step(SimEngine *engine) {
    return pipeline_cycle(engine->state, engine->cpu, engine->mem);
}

static int pipeline_engine_views(const SimEngine *engine, PipelineStageView *views, int capacity) {
    const Pipeline *p = engine->state;
    PipelineStageView stages[SIM_MAX_STAGES] = {
        {"IF", engine->cpu->pc, engine->cpu->status != CPU_RUNNING},
        {"ID", p->if_id.pc, !p->if_id.valid},
        {"EX", p->id_ex.pc, !p->id_ex.valid},
        {"MEM", p->ex_mem.pc, !p->ex_mem.valid},
        {"WB", p->mem_wb.pc, !p->mem_wb.valid}
    };
    int count = capacity < SIM_MAX_STAGES ? capacity : SIM_MAX_STAGES;
    if (count < 0) count = 0;
    for (int i = 0; i < count; ++i) views[i] = stages[i];
    return count;
}

static void pipeline_engine_stats(const SimEngine *engine, char *buffer, size_t capacity) {
    const Pipeline *p = engine->state;
    double cpi = p->retired ? (double)p->cycles / p->retired : 0.0;
    double accuracy = p->branches ?
        100.0 * (p->branches - p->mispredicts) / p->branches : 100.0;
    snprintf(buffer, capacity, "pipeline        = 5-stage, hazard unit '%s', predictor '%s'\n"
           "retired         = %" PRIu64 "\n"
           "CPI             = %.3f\n"
           "stalls          = %" PRIu64 "\n"
           "forwards        = %" PRIu64 "\n"
           "branches        = %" PRIu64 "\n"
           "mispredicts     = %" PRIu64 "\n"
           "accuracy        = %.1f%%\n"
           "jal             = %" PRIu64 "\n"
           "jalr            = %" PRIu64 "\n"
           "redirects       = %" PRIu64 "\n"
           "squashed        = %" PRIu64 "\n"
           "load_use_stalls = %" PRIu64 "\n",
           p->hazard_mode == HAZARD_FORWARD ? "forward" :
           p->hazard_mode == HAZARD_STALL ? "stall" : "none", p->predictor->name, p->retired,
           cpi, p->stalls, p->fwd_ex + p->fwd_mem, p->branches, p->mispredicts,
           accuracy, p->jal, p->jalr, p->redirects, 2 * p->redirects, p->load_use_stalls);
}

void pipeline_engine_init(SimEngine *engine, Pipeline *p, CPUState *cpu, Memory *mem) {
    *engine = (SimEngine){"5-stage pipeline", cpu, mem, p, pipeline_engine_step,
                         pipeline_engine_views, pipeline_engine_stats};
}

void pipeline_run_batch(Pipeline *p, CPUState *cpu, Memory *mem, uint64_t max_cycles) {
    SimEngine engine;
    pipeline_engine_init(&engine, p, cpu, mem);
    int trace = p->trace;
    p->trace = 0; /* The shared UI owns tracing during a batch run. */
    run_batch(&engine, max_cycles, trace);
    p->trace = trace;
}

CPUStatus pipeline_cycle(Pipeline *p, CPUState *cpu, Memory *mem) {
    if (cpu->status != CPU_RUNNING)
        return cpu->status;

    if (p->trace)
        trace_cycle(p, cpu->pc);

    if (p->cycles == 0) p->fcsr_committed = cpu->fcsr;
    Pipeline nx = *p;
    nx.cycles++;
    // Clear complete outgoing latches so bubbles cannot retain live controls
    nx.if_id = (IFID){0};
    nx.id_ex = (IDEX){0};
    nx.ex_mem = (EXMEM){0};
    nx.mem_wb = (MEMWB){0};
    int flush = 0, stall = 0;
    uint32_t redirect_pc = 0;

    if (p->mem_wb.valid) {
        if (p->mem_wb.fault != PF_NONE) {
            cpu->status = p->mem_wb.fault == PF_ILLEGAL ?
                          CPU_ILLEGAL_INSTRUCTION : CPU_MEM_FAULT;
            cpu->pc = p->mem_wb.pc;
            cpu->fault_addr = p->mem_wb.fault_addr;
            snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                     "pipeline fault %d at pc=0x%08x, address=0x%08x",
                     p->mem_wb.fault, p->mem_wb.pc, p->mem_wb.fault_addr);
            goto commit;
        }
        if (p->mem_wb.ctrl.reg_write)
            register_write(cpu, p->mem_wb.ctrl.dst, p->mem_wb.wb_val);
        if (p->mem_wb.ctrl.is_system) {
            uint32_t fetch_pc = cpu->pc;
            DecodedInstr instr = p->mem_wb.instr;
            CPUState syscall = *cpu;
            syscall.pc = p->mem_wb.pc;
            cpu_write_reg(&syscall, 10, p->mem_wb.wb_val);
            cpu_write_reg(&syscall, 17, p->mem_wb.system_number);
            p->mem_wb.handler->execute(&syscall, mem, &instr);
            cpu->status = syscall.status;
            cpu->pc = cpu->status == CPU_RUNNING ? fetch_pc : p->mem_wb.pc;
        }
        nx.fcsr_committed = p->mem_wb.fcsr_after;
        nx.retired++;
        nx.stalls += p->mem_wb.stall_cycles;
        nx.load_use_stalls += p->mem_wb.load_use_cycles;
        nx.fwd_ex += p->mem_wb.fwd_ex;
        nx.fwd_mem += p->mem_wb.fwd_mem;
        nx.branches += p->mem_wb.ctrl.is_branch;
        nx.mispredicts += p->mem_wb.mispredicted;
        nx.redirects += p->mem_wb.redirected;
        if (p->mem_wb.ctrl.is_jump) {
            if (p->mem_wb.instr.opcode == 0x6f)
                nx.jal++;
            else
                nx.jalr++;
        }
        if (cpu->status != CPU_RUNNING)
            goto commit;
    }

    if (p->ex_mem.valid) {
        nx.mem_wb.valid = 1;
        nx.mem_wb.fcsr_after = p->ex_mem.fcsr_after;
        nx.mem_wb.pc = p->ex_mem.pc;
        nx.mem_wb.fault = p->ex_mem.fault;
        nx.mem_wb.instr = p->ex_mem.instr;
        nx.mem_wb.handler = p->ex_mem.handler;
        nx.mem_wb.ctrl = p->ex_mem.ctrl;
        nx.mem_wb.wb_val = p->ex_mem.alu_result;
        nx.mem_wb.system_number = p->ex_mem.store_val;
        nx.mem_wb.fault_addr = p->ex_mem.fault_addr;
        nx.mem_wb.load_use_cycles = p->ex_mem.load_use_cycles;
        nx.mem_wb.fwd_ex = p->ex_mem.fwd_ex;
        nx.mem_wb.fwd_mem = p->ex_mem.fwd_mem;
        nx.mem_wb.mispredicted = p->ex_mem.mispredicted;
        nx.mem_wb.stall_cycles = p->ex_mem.stall_cycles;
        nx.mem_wb.redirected = p->ex_mem.redirected;
        if (p->ex_mem.fault == PF_NONE) {
            uint32_t addr = p->ex_mem.alu_result;
            MemStatus status = MEM_OK;
            if (p->ex_mem.ctrl.mem_read) {
                uint8_t byte = 0;
                uint16_t half = 0;
                uint32_t val = 0;
                switch (p->ex_mem.instr.funct3) {
                    case 0: status = memory_read_byte(mem, addr, &byte);
                            val = (uint32_t)(int32_t)(int8_t)byte; break;
                    case 1: status = memory_read_half(mem, addr, &half);
                            val = (uint32_t)(int32_t)(int16_t)half; break;
                    case 2: status = memory_read_word(mem, addr, &val); break;
                    case 4: status = memory_read_byte(mem, addr, &byte);
                            val = byte; break;
                    case 5: status = memory_read_half(mem, addr, &half);
                            val = half; break;
                }
                nx.mem_wb.wb_val = val;
            } else if (p->ex_mem.ctrl.mem_write) {
                uint32_t val = p->ex_mem.store_val;
                switch (p->ex_mem.instr.funct3) {
                    case 0: status = memory_write_byte(mem, addr, (uint8_t)val); break;
                    case 1: status = memory_write_half(mem, addr, (uint16_t)val); break;
                    case 2: status = memory_write_word(mem, addr, val); break;
                }
            }
            if (status != MEM_OK) {
                nx.mem_wb.fault = PF_MEM;
                nx.mem_wb.fault_addr = addr;
            }
        }
    }
    /* =========================================================================
     * STAGE 3: EX (Execute) - Reads p->id_ex, Writes nx.ex_mem
     * ========================================================================= */
    Operands o = forward_operands(p, &p->id_ex);

    if(p->id_ex.valid){
        nx.ex_mem.fwd_ex = o.fwd_ex;
        nx.ex_mem.fwd_mem = o.fwd_mem;
        nx.ex_mem.stall_cycles = p->id_ex.stall_cycles;
        nx.ex_mem.load_use_cycles = p->id_ex.load_use_cycles;
        nx.ex_mem.valid = 1;
        nx.ex_mem.fcsr_after = cpu->fcsr;
        nx.ex_mem.pc = p->id_ex.pc;
        nx.ex_mem.fault = p->id_ex.fault;
        nx.ex_mem.fault_addr = p->id_ex.pc;
        nx.ex_mem.instr = p->id_ex.instr;
        nx.ex_mem.handler = p->id_ex.handler;
        nx.ex_mem.ctrl = p->id_ex.ctrl;
        nx.ex_mem.store_val = o.b;
    }

    if(p->id_ex.valid && p->id_ex.fault  == PF_NONE){
        if(p->id_ex.ctrl.is_branch){
            int taken = 0;
            uint32_t r1 = o.a;
            uint32_t r2 = o.b;

            switch(p->id_ex.instr.funct3){
                case 0: taken = (r1 == r2); break;
                case 1: taken = (r1 != r2); break;
                case 4: taken = ((int32_t)r1 < (int32_t)r2); break;
                case 5: taken = ((int32_t)r1 >= (int32_t)r2); break;
                case 6: taken = (r1 < r2); break;
                case 7: taken = (r1 >= r2); break;
            }

            uint32_t pc = p->id_ex.pc;
            uint32_t target = pc + (uint32_t)p->id_ex.instr.imm;
            p->predictor->update(&nx.bp_state, pc, target, taken);
            nx.ex_mem.mispredicted = taken != p->id_ex.pred_taken;
            redirect_pc = taken ? target : pc + 4;
            flush = redirect_pc != p->id_ex.pred_next;
        } else if(p->id_ex.ctrl.is_jump){
            nx.ex_mem.alu_result = p->id_ex.pc + 4;
            if (p->id_ex.instr.opcode == 0x6f)
                redirect_pc = p->id_ex.pc + (uint32_t)p->id_ex.instr.imm;
            else
                redirect_pc = (o.a + (uint32_t)p->id_ex.instr.imm) & ~1u;
            flush = redirect_pc != p->id_ex.pred_next;
        } else if (p->id_ex.ctrl.mem_read || p->id_ex.ctrl.mem_write) {
            nx.ex_mem.alu_result = o.a + (uint32_t)p->id_ex.instr.imm;
        } else if (p->id_ex.ctrl.is_system) {
            nx.ex_mem.alu_result = o.a;
        } else {
            /* Handlers see only their latched/forwarded register operands.
             * Memory instructions and syscalls are handled in MEM and WB. */
            CPUState scratch = *cpu;
            DecodedInstr instr = p->id_ex.instr;
            register_write(&scratch, p->id_ex.ctrl.src1, o.a);
            register_write(&scratch, p->id_ex.ctrl.src2, o.b);
            register_write(&scratch, p->id_ex.ctrl.src3, o.c);
            scratch.pc = p->id_ex.pc;
            p->id_ex.handler->execute(&scratch, mem, &instr);
            if (scratch.status != CPU_RUNNING) {
                nx.ex_mem.fault = scratch.status == CPU_ILLEGAL_INSTRUCTION ? PF_ILLEGAL : PF_MEM;
                nx.ex_mem.fault_addr = scratch.fault_addr;
            } else {
                nx.ex_mem.alu_result = register_read(&scratch, p->id_ex.ctrl.dst);
                cpu->fcsr = scratch.fcsr;
                nx.ex_mem.fcsr_after = scratch.fcsr;
            }
        }
    }
    nx.ex_mem.redirected = flush;

    /* ID: preserve fetch faults before attempting to decode their raw word. */
    if (p->if_id.valid && !flush) {
        DecodedInstr d = {0};
        const InstructionHandler *h = NULL;
        nx.id_ex.valid = 1;
        nx.id_ex.pc = p->if_id.pc;
        nx.id_ex.stall_cycles = p->if_id.stall_cycles;
        nx.id_ex.load_use_cycles = p->if_id.load_use_cycles;
        nx.id_ex.pred_next = p->if_id.pred_next;
        nx.id_ex.pred_taken = p->if_id.pred_taken;
        if (p->if_id.fault != PF_NONE) {
            nx.id_ex.fault = p->if_id.fault;
        } else if (decode_instruction(p->if_id.raw, &d, &h) != DECODE_OK) {
            nx.id_ex.fault = PF_ILLEGAL;
        } else {
            ControlSignals ctrl = control_decode(&d, h);
            /* Include the syscall ABI's implicit inputs
             * in dependency checks and forwarding statistics. The syscall
             * handler still runs at WB with the committed register file. */
            if (ctrl.is_system) {
                d.rs1 = 10; /* a0: argument */
                d.rs2 = 17; /* a7: syscall number */
                ctrl.uses_rs1 = 1;
                ctrl.uses_rs2 = 1;
                ctrl.src1 = 10;
                ctrl.src2 = 17;
            }
            HazardVerdict hz = hazard_check(p, &d, &ctrl);
            if(hz.stall){
                //1. bubble enters EX this cycle
                nx.id_ex.valid=0;
                //2. IF/ID is frozen: consumer is re-decoded next cycle
                nx.if_id = p->if_id;
                //3. local flag stall: IF must not fetch
                stall = 1;
                nx.if_id.stall_cycles++;
                nx.if_id.load_use_cycles += hz.load_use;
            } else {
                nx.id_ex.instr = d;
                nx.id_ex.handler = h;
                nx.id_ex.ctrl = ctrl;
                nx.id_ex.rs1_val = register_read(cpu, ctrl.src1);
                nx.id_ex.rs2_val = register_read(cpu, ctrl.src2);
                nx.id_ex.rs3_val = register_read(cpu, ctrl.src3);
            }
        }
    }

    /* Recovery discards ID and the current IF slot. Fetch resumes next cycle. */
    if (flush) {
        cpu->pc = redirect_pc;
    } else if (!stall) {
        uint32_t fetch_pc = cpu->pc;
        uint32_t raw_instr;
        nx.if_id.valid = 1;
        nx.if_id.pc = fetch_pc;
        nx.if_id.pred_next = fetch_pc + 4;
        if (memory_read_word(mem, fetch_pc, &raw_instr) != MEM_OK) {
            nx.if_id.fault = PF_FETCH;
        } else {
            nx.if_id.raw = raw_instr;
            uint32_t opcode = raw_instr & 0x7f;
            if (opcode == 0x63 || opcode == 0x6f) {
                DecodedInstr decoded;
                const InstructionHandler *handler;
                if (decode_instruction(raw_instr, &decoded, &handler) == DECODE_OK) {
                    uint32_t target = fetch_pc + (uint32_t)decoded.imm;
                    if (opcode == 0x6f) {
                        nx.if_id.pred_next = target;
                    } else {
                        nx.if_id.pred_taken = p->predictor->predict(&nx.bp_state, fetch_pc, target);
                        if (nx.if_id.pred_taken)
                            nx.if_id.pred_next = target;
                    }
                }
            }
        }
        cpu->pc = nx.if_id.pred_next;
    }

commit:
    if (cpu->status != CPU_RUNNING)
        cpu->fcsr = nx.fcsr_committed;
    *p = nx;
    cpu->cycle_count = p->cycles;
    return cpu->status;
}
