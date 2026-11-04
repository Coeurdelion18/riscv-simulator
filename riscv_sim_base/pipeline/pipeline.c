#include "pipeline_state.h"
#include "cpu_state.h"
#include "decoder.h"
#include "cli_display.h"
#include "hazard.h"
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

// static int depends_on(const DecodedInstr *i, const ControlSignals *ctrl,
//                       int valid, int fault, const DecodedInstr *producer,
//                       const ControlSignals *producer_ctrl) {
//     return valid && fault == PF_NONE && producer_ctrl->reg_write &&
//            producer->rd != 0 &&
//            ((ctrl->uses_rs1 && i->rs1 == producer->rd) ||
//             (ctrl->uses_rs2 && i->rs2 == producer->rd));
// }

void pipeline_init(Pipeline *p, int forwarding) {
    memset(p, 0, sizeof(Pipeline));
    p->forwarding = forwarding;
}

void pipeline_run_batch(Pipeline *p, CPUState *cpu, Memory *mem, uint64_t max_cycles) {
    while (cpu->status == CPU_RUNNING &&
           (max_cycles == 0 || p->cycles < max_cycles))
        pipeline_cycle(p, cpu, mem);

    printf("\n");
    print_registers(cpu);
    print_status(cpu);
    printf("pipeline: mode=%s cycles=%llu retired=%llu stalls=%llu flushes=%llu "
           "CPI=%.3f fwd_ex=%llu fwd_mem=%llu load_use=%llu\n",
           p->forwarding ? "forward" : "stall",
           (unsigned long long)p->cycles,
           (unsigned long long)p->retired,
           (unsigned long long)p->stalls,
           (unsigned long long)p->flushes,
           (p->retired > 0) ? (double)p->cycles / (double)p->retired : 0.0,
           (unsigned long long)p->fwd_ex,
           (unsigned long long)p->fwd_mem,
           (unsigned long long)p->load_use_stalls);
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
        // nx.stalls += p->mem_wb.stall_cycles;
        // nx.flushes += p->mem_wb.redirected;
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
    /* =========================================================================
     * STAGE 3: EX (Execute) - Reads p->id_ex, Writes nx.ex_mem
     * ========================================================================= */
    Operands o = forward_operands(p, &p->id_ex);

    if(p->id_ex.valid){
        nx.fwd_ex  += o.fwd_ex;
        nx.fwd_mem += o.fwd_mem;
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

            if(taken){
                flush = 1;
                nx.flushes++;
                redirect_pc = p->id_ex.pc + p->id_ex.instr.imm;
            }
        } else if(p->id_ex.ctrl.is_jump){
            flush = 1;
            nx.flushes++;
            nx.ex_mem.alu_result = p->id_ex.pc + 4;

            if (p->id_ex.instr.opcode == 0x6F) {
                redirect_pc = p->id_ex.pc + p->id_ex.instr.imm;
            } else {
                redirect_pc = (o.a + p->id_ex.instr.imm) & ~1u;
            }
        } else {
            nx.ex_mem.alu_result = alu(&(p->id_ex.instr), p->id_ex.pc, o.a, o.b);
        }
    }
    // nx.ex_mem.redirected = flush;

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

            /* Hazard Check: Test against instructions in EX and MEM stages */
            // int ex_hazard = p->id_ex.valid && p->id_ex.ctrl.reg_write && (p->id_ex.instr.rd != 0) &&
            //                 ((ctrl.uses_rs1 && d.rs1 == p->id_ex.instr.rd) ||
            //                     (ctrl.uses_rs2 && d.rs2 == p->id_ex.instr.rd));

            // int mem_hazard = p->ex_mem.valid && p->ex_mem.ctrl.reg_write && (p->ex_mem.instr.rd != 0) &&
            //                 ((ctrl.uses_rs1 && d.rs1 == p->ex_mem.instr.rd) ||
            //                 (ctrl.uses_rs2 && d.rs2 == p->ex_mem.instr.rd));
            
            // if(ex_hazard || mem_hazard){
            //     stall = 1;
            //     p->stalls++;
            //     /* Inject bubble into ID/EX */
            //     nx.id_ex.valid = 0;
            //     /* Keep IF/ID state frozen */
            //     nx.if_id = p->if_id;

            HazardVerdict hz = hazard_check(p, &d, &ctrl);
            if(hz.stall){
                //1. bubble enters EX this cycle
                nx.id_ex.valid=0;
                //2. IF/ID is frozen: consumer is re-decoded next cycle
                nx.if_id = p->if_id;
                //3. local flag stall: IF must not fetch
                stall = 1;
                nx.stalls++; // Use nx to update the next state's counter
                if(hz.load_use) nx.load_use_stalls++;
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
        uint32_t raw_instr = 0;

        int fetch_status = memory_read_word(mem, fetch_pc, &raw_instr);

        nx.if_id.valid = 1;
        nx.if_id.pc = fetch_pc;

        if (fetch_status != MEM_OK){
            nx.if_id.fault = PF_FETCH;
            nx.if_id.raw = 0;
        } else {
            nx.if_id.fault = PF_NONE;
            nx.if_id.raw = raw_instr;
        }

        cpu->pc = fetch_pc+4;

     }

commit:
    *p = nx;
    cpu->cycle_count = p->cycles;
    return cpu->status;
}
