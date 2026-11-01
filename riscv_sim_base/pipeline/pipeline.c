#include "pipeline_state.h"
#include "cpu_state.h"
#include "decoder.h"
#include "cli_display.h"
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

static int depends_on(const DecodedInstr *i, const ControlSignals *ctrl,
                      int valid, int fault, const DecodedInstr *producer,
                      const ControlSignals *producer_ctrl) {
    return valid && fault == PF_NONE && producer_ctrl->reg_write &&
           producer->rd != 0 &&
           ((ctrl->uses_rs1 && i->rs1 == producer->rd) ||
            (ctrl->uses_rs2 && i->rs2 == producer->rd));
}

void pipeline_init(Pipeline *p) {
    memset(p, 0, sizeof(*p));
}

void pipeline_run_batch(Pipeline *p, CPUState *cpu, Memory *mem, uint64_t max_cycles) {
    while (cpu->status == CPU_RUNNING && p->cycles < max_cycles)
        pipeline_cycle(p, cpu, mem);

    printf("\n");
    print_registers(cpu);
    print_status(cpu);
    printf("pipeline: cycles=%llu retired=%llu stalls=%llu flushes=%llu CPI=%.3f\n",
           (unsigned long long)p->cycles, (unsigned long long)p->retired,
           (unsigned long long)p->stalls, (unsigned long long)p->flushes,
           p->retired ? (double)p->cycles / (double)p->retired : 0.0);
    if (cpu->status == CPU_RUNNING)
        printf("  -> stopped: reached max cycle limit (%llu)\n",
               (unsigned long long)max_cycles);
}

CPUStatus pipeline_cycle(Pipeline *p, CPUState *cpu, Memory *mem) {
    if (cpu->status != CPU_RUNNING)
        return cpu->status;

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
        nx.stalls += p->mem_wb.stall_cycles;
        nx.flushes += p->mem_wb.redirected;
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

    /* EX: an invalid latch or fault must never execute stale controls. */
    if (p->id_ex.valid) {
        nx.ex_mem.valid = 1;
        nx.ex_mem.pc = p->id_ex.pc;
        nx.ex_mem.fault = p->id_ex.fault;
        nx.ex_mem.fault_addr = p->id_ex.pc;
        nx.ex_mem.instr = p->id_ex.instr;
        nx.ex_mem.handler = p->id_ex.handler;
        nx.ex_mem.ctrl = p->id_ex.ctrl;
        nx.ex_mem.store_val = p->id_ex.rs2_val;
        nx.ex_mem.stall_cycles = p->id_ex.stall_cycles;
        if (p->id_ex.fault == PF_NONE) {
            const DecodedInstr *i = &p->id_ex.instr;
            uint32_t a = p->id_ex.rs1_val, b = p->id_ex.rs2_val;
            if (p->id_ex.ctrl.is_branch) {
                switch (i->funct3) {
                    case 0: flush = a == b; break;
                    case 1: flush = a != b; break;
                    case 4: flush = (int32_t)a < (int32_t)b; break;
                    case 5: flush = (int32_t)a >= (int32_t)b; break;
                    case 6: flush = a < b; break;
                    case 7: flush = a >= b; break;
                }
                redirect_pc = p->id_ex.pc + i->imm;
            } else if (p->id_ex.ctrl.is_jump) {
                flush = 1;
                nx.ex_mem.alu_result = p->id_ex.pc + 4;
                redirect_pc = i->opcode == 0x6f ? p->id_ex.pc + i->imm :
                              (a + i->imm) & ~1u;
            } else {
                nx.ex_mem.alu_result = alu(i, p->id_ex.pc, a, b);
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
        if (p->if_id.fault != PF_NONE) {
            nx.id_ex.fault = p->if_id.fault;
        } else if (decode_instruction(p->if_id.raw, &d, &h) != DECODE_OK) {
            nx.id_ex.fault = PF_ILLEGAL;
        } else {
            ControlSignals ctrl = control_decode(&d, h);
            stall = depends_on(&d, &ctrl, p->id_ex.valid, p->id_ex.fault,
                               &p->id_ex.instr, &p->id_ex.ctrl) ||
                    depends_on(&d, &ctrl, p->ex_mem.valid, p->ex_mem.fault,
                               &p->ex_mem.instr, &p->ex_mem.ctrl) ||
                    depends_on(&d, &ctrl, p->mem_wb.valid, p->mem_wb.fault,
                               &p->mem_wb.instr, &p->mem_wb.ctrl);
            if (stall) {
                nx.id_ex = (IDEX){0};
                nx.if_id = p->if_id;
                nx.if_id.stall_cycles++;
            } else {
                nx.id_ex.instr = d;
                nx.id_ex.handler = h;
                nx.id_ex.ctrl = ctrl;
                nx.id_ex.rs1_val = cpu_read_reg(cpu, d.rs1);
                nx.id_ex.rs2_val = cpu_read_reg(cpu, d.rs2);
            }
        }
    }

    /* IF: freeze both the latch and fetch PC during a data stall. */
    if (!stall) {
        uint32_t fetch_pc = flush ? redirect_pc : cpu->pc;
        nx.if_id.valid = 1;
        nx.if_id.pc = fetch_pc;
        if (memory_read_word(mem, fetch_pc, &nx.if_id.raw) != MEM_OK)
            nx.if_id.fault = PF_FETCH;
        cpu->pc = fetch_pc + 4;
    }

commit:
    *p = nx;
    cpu->cycle_count = p->cycles;
    return cpu->status;
}
