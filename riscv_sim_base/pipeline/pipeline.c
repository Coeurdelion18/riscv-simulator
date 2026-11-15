#include "pipeline_state.h"
#include "cpu_state.h"
#include "decoder.h"
#include "cli_display.h"
#include "hazard.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static uint32_t alu(const DecodedInstr *i, uint32_t pc, uint32_t a, uint32_t b) {
    uint32_t shamt = (uint32_t)i->imm & 0x1F;

    switch (i->opcode) {
        case 0x37: return i->imm;                                     /* LUI */
        case 0x17: return pc + i->imm;                                /* AUIPC */
        case 0x03:                                                    /* Loads */
        case 0x23: return a + i->imm;                                 /* Stores */
        case 0x13: /* OP-IMM */
            switch (i->funct3) {
                case 0x0: return a + i->imm;                          /* ADDI */
                case 0x2: return ((int32_t)a < (int32_t)i->imm) ? 1 : 0; /* SLTI */
                case 0x3: return ((uint32_t)a < (uint32_t)i->imm) ? 1 : 0;                /* SLTIU */
                case 0x4: return a ^ i->imm;                          /* XORI */
                case 0x6: return a | i->imm;                          /* ORI */
                case 0x7: return a & i->imm;                          /* ANDI */
                case 0x1: return a << shamt;                         /* SLLI */
                case 0x5:                                            /* SRLI / SRAI */
                    if (i->funct7 & 0x20)
                        return (uint32_t)(((int32_t)a) >> shamt);    /* SRAI */
                    return a >> shamt;                               /* SRLI */
                default: return 0;
            }
        case 0x33: /* OP (R-type) */
            switch (i->funct3) {
                case 0x0:                                            /* ADD / SUB */
                    if (i->funct7 & 0x20) return a - b;               /* SUB */
                    return a + b;                                    /* ADD */
                case 0x1: return a << (b & 0x1F);                   /* SLL */
                case 0x2: return ((int32_t)a < (int32_t)b) ? 1 : 0;  /* SLT */
                case 0x3: return (a < b) ? 1 : 0;                   /* SLTU */
                case 0x4: return a ^ b;                              /* XOR */
                case 0x5:                                            /* SRL / SRA */
                    if (i->funct7 & 0x20)
                        return (uint32_t)(((int32_t)a) >> (b & 0x1F));/* SRA */
                    return a >> (b & 0x1F);                          /* SRL */
                case 0x6: return a | b;                              /* OR */
                case 0x7: return a & b;                              /* AND */
                default: return 0;
            }
        default: return 0;
    }
}

void pipeline_init(Pipeline *p, int forwarding, const BranchPredictor *predictor) {
    memset(p, 0, sizeof(*p));
    p->forwarding = forwarding;
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

void pipeline_run_batch(Pipeline *p, CPUState *cpu, Memory *mem, uint64_t max_cycles) {
    while (cpu->status == CPU_RUNNING &&
           (max_cycles == 0 || p->cycles < max_cycles))
        pipeline_cycle(p, cpu, mem);

    printf("\n");
    print_registers(cpu);
    print_status(cpu);
    double cpi = p->retired ? (double)p->cycles / p->retired : 0.0;
    double accuracy = p->branches ?
        100.0 * (p->branches - p->mispredicts) / p->branches : 0.0;
    printf("pipeline        = 5-stage, hazard unit '%s', predictor '%s'\n"
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
           p->forwarding ? "forward" : "stall", p->predictor->name, p->retired,
           cpi, p->stalls, p->fwd_ex + p->fwd_mem, p->branches, p->mispredicts,
           accuracy, p->jal, p->jalr, p->redirects, 2 * p->redirects, p->load_use_stalls);
    if (cpu->status == CPU_RUNNING)
        printf("  -> stopped: reached max cycle limit (%llu)\n",
               (unsigned long long)max_cycles);
}

CPUStatus pipeline_cycle(Pipeline *p, CPUState *cpu, Memory *mem) {
    if (cpu->status != CPU_RUNNING)
        return cpu->status;

    if (p->trace)
        trace_cycle(p, cpu->pc);

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
            cpu->fault_addr = p->mem_wb.fault_addr;
            snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                     "pipeline fault %d at pc=0x%08x, address=0x%08x",
                     p->mem_wb.fault, p->mem_wb.pc, p->mem_wb.fault_addr);
            goto commit;
        }
        if (p->mem_wb.ctrl.reg_write)
            cpu_write_reg(cpu, p->mem_wb.instr.rd, p->mem_wb.wb_val);
        if (p->mem_wb.ctrl.is_system) {
            uint32_t fetch_pc = cpu->pc;
            DecodedInstr instr = p->mem_wb.instr;
            cpu->pc = p->mem_wb.pc;
            p->mem_wb.handler->execute(cpu, mem, &instr);
            cpu->pc = fetch_pc;
        }
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
        nx.mem_wb.pc = p->ex_mem.pc;
        nx.mem_wb.fault = p->ex_mem.fault;
        nx.mem_wb.instr = p->ex_mem.instr;
        nx.mem_wb.handler = p->ex_mem.handler;
        nx.mem_wb.ctrl = p->ex_mem.ctrl;
        nx.mem_wb.wb_val = p->ex_mem.alu_result;
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
        } else {
            nx.ex_mem.alu_result = alu(&(p->id_ex.instr), p->id_ex.pc, o.a, o.b);
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
            /* In forwarding mode, include the syscall ABI's implicit inputs
             * in dependency checks and forwarding statistics. The syscall
             * handler still runs at WB with the committed register file. */
            if (p->forwarding && ctrl.is_system) {
                d.rs1 = 10; /* a0: argument */
                d.rs2 = 17; /* a7: syscall number */
                ctrl.uses_rs1 = 1;
                ctrl.uses_rs2 = 1;
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
                nx.id_ex.rs1_val = cpu_read_reg(cpu, d.rs1);
                nx.id_ex.rs2_val = cpu_read_reg(cpu, d.rs2);
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
    *p = nx;
    cpu->cycle_count = p->cycles;
    return cpu->status;
}
