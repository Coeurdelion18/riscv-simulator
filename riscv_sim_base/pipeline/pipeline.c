#include "pipeline_state.h"
#include "cpu_state.h"
#include "decoder.h"
#include <stdio.h>
#include <string.h>

static uint32_t alu(const DecodedInstr *i, uint32_t pc, uint32_t a, uint32_t b) {
    uint32_t shamt = b & 0x1F;

    switch (i->opcode) {
        case 0x37: return i->imm;                                     /* LUI */
        case 0x17: return pc + i->imm;                                /* AUIPC */
        case 0x03:                                                    /* Loads */
        case 0x23: return a + i->imm;                                 /* Stores */
        case 0x13: /* OP-IMM */
            switch (i->funct3) {
                case 0x0: return a + i->imm;                          /* ADDI */
                case 0x2: return ((int32_t)a < (int32_t)i->imm) ? 1 : 0; /* SLTI */
                case 0x3: return (a < i->imm) ? 1 : 0;                /* SLTIU */
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

void pipeline_init(Pipeline *p) {
    memset(p, 0, sizeof(Pipeline));
}

void pipeline_run_batch(Pipeline *p, CPUState *cpu, Memory *mem, uint64_t max_cycles) {
    /* 
     * Loop until the CPU hits a halting status (e.g., ECALL_EXIT, 
     * CPU_ILLEGAL_INSTRUCTION, CPU_MEM_FAULT) or we hit max_cycles.
     * (Assuming CPU_OK or 0 represents the normal running state).
     */
    while (max_cycles == 0 || p->cycles < max_cycles) {
        CPUStatus status = pipeline_cycle(p, cpu, mem);
        
        /* If the pipeline returns anything other than the standard running state, halt */
        if (status != 0) { /* Typically CPU_OK == 0 in these base simulators */
            break;
        }
    }
}

CPUStatus pipeline_cycle(Pipeline *p, CPUState *cpu, Memory *mem) {
    Pipeline nx = *p; /* nx = next state; p = current (old) state */
    nx.cycles++;

    int flush = 0;
    int stall = 0;
    uint32_t redirect_pc = 0;

    nx.if_id.valid = 0;
    nx.id_ex.valid = 0;
    nx.ex_mem.valid = 0;
    nx.mem_wb.valid = 0;

    /* ... every stage READS from p->..., WRITES into nx.... ... */
    if(!p->mem_wb.valid) {
        // No work
    }
    else {
        if(p->mem_wb.fault != PF_NONE) {
            if(p->mem_wb.fault == PF_ILLEGAL) {
                cpu->status = CPU_ILLEGAL_INSTRUCTION;
            }
            else {
                cpu->status = CPU_MEM_FAULT;
            }
            cpu->fault_addr = p->mem_wb.fault_addr;
            snprintf(cpu->fault_msg, sizeof(cpu->fault_msg), "Pipeline Fault in MEM/WB at address %u", p->mem_wb.fault_addr);
            return cpu->status;
        }
        if (p->mem_wb.ctrl.reg_write) {
            cpu_write_reg(cpu, p->mem_wb.instr.rd, p->mem_wb.wb_val);
        }
        if (p->mem_wb.ctrl.is_system) {
            uint32_t temp_pc = cpu->pc;
            cpu->pc = p->mem_wb.pc;
            p->mem_wb.handler->execute(cpu, mem, &(p->mem_wb.instr));
            cpu->pc = temp_pc;
        }
        p->retired++;
    }
    
    /* ----------------------------------------------------------------------
     * 4. MEM - Memory Access (Reads p->ex_mem, writes nx.mem_wb)
     * ----------------------------------------------------------------------*/

    if(p->ex_mem.valid){
        nx.mem_wb.pc = p->ex_mem.pc;
        nx.mem_wb.fault = p->ex_mem.fault;
        nx.mem_wb.instr = p->ex_mem.instr;
        nx.mem_wb.handler = p->ex_mem.handler;
        nx.mem_wb.ctrl = p->ex_mem.ctrl;
        nx.mem_wb.wb_val = p->ex_mem.alu_result;
        nx.mem_wb.fault_addr = p->ex_mem.fault_addr;

        if(p->ex_mem.fault == PF_NONE){
            if(p->ex_mem.ctrl.mem_read){
                uint32_t val = 0;
                int status = MEM_OK;

                switch(p->ex_mem.instr.funct3){
                    case 1: status = memory_read_byte(mem, p->ex_mem.alu_result, (int8_t*)&val); break; //LB
                    case 2: status = memory_read_half(mem, p->ex_mem.alu_result, (int16_t*)&val); break; //LH
                    case 3: status = memory_read_word(mem, p->ex_mem.alu_result, &val); break; //LW
                    case 4: 
                        status = memory_read_byte(mem, p->ex_mem.alu_result, (int8_t*)&val); 
                        val &= 0xFF;
                        break; //LBU
                    case 5: 
                        status = memory_read_half(mem, p->ex_mem.alu_result, (int16_t*)&val); 
                        val &= 0xFFFF;
                        break; 
                }

                if (status != MEM_OK){
                    nx.mem_wb.fault = PF_MEM;
                    nx.mem_wb.fault_addr = p->ex_mem.alu_result;
                } else {
                    nx.mem_wb.wb_val = val;
                }
            }
            else if(p->ex_mem.ctrl.mem_write){
                int status = MEM_OK;

                switch(p->ex_mem.instr.funct3){
                    case 0: 
                        status = memory_write_byte(mem, p->ex_mem.alu_result, p->ex_mem.store_val); 
                        
                        break;
                    case 1: status = memory_write_half(mem, p->ex_mem.alu_result, (int16_t)p->ex_mem.store_val); break;
                    case 3: status = memory_write_word(mem, p->ex_mem.alu_result, p->ex_mem.store_val); break;
                }

                if(status != MEM_OK){
                    nx.mem_wb.fault = PF_MEM;
                    nx.mem_wb.fault_addr = p->ex_mem.alu_result;
                }
            }
        }
    }
    /* =========================================================================
     * STAGE 3: EX (Execute) - Reads p->id_ex, Writes nx.ex_mem
     * ========================================================================= */

    if(p->id_ex.valid){
        nx.ex_mem.valid = 1;
        nx.ex_mem.pc = p->id_ex.pc;
        nx.ex_mem.fault = p->id_ex.fault;
        nx.ex_mem.instr = p->id_ex.instr;
        nx.ex_mem.handler = p->id_ex.handler;
        nx.ex_mem.ctrl = p->id_ex.ctrl;
        nx.ex_mem.store_val = p->id_ex.rs2_val;
    }

    if(p->id_ex.fault == PF_NONE){
        if(p->id_ex.ctrl.is_branch){
            int taken = 0;
            uint32_t r1 = p->id_ex.rs1_val;
            uint32_t r2 = p->id_ex.rs2_val;

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
                redirect_pc = p->id_ex.pc + p->id_ex.instr.imm;
            }
        } else if(p->id_ex.ctrl.is_jump){
            flush = 1;
            p->flushes++;
            nx.ex_mem.alu_result = p->id_ex.pc + 4;

            if (p->id_ex.instr.opcode == 0x6F) {
                redirect_pc = p->id_ex.pc + p->id_ex.instr.imm;
            } else {
                redirect_pc = (p->id_ex.rs1_val + p->id_ex.instr.imm) & ~1u;
            }
        } else {
            nx.ex_mem.alu_result = alu(&(p->id_ex.instr), p->id_ex.pc, p->id_ex.rs1_val, p->id_ex.rs2_val);
        }
    }

    /* =========================================================================
     * STAGE 2: ID (Decode & Hazard Check) - Reads p->if_id, Writes nx.id_ex
     * ========================================================================= */

    if (p->if_id.valid && !flush) {
        DecodedInstr d;
        const InstructionHandler *h;
        DecodeStatus ds = decode_instruction(p->if_id.raw, &d, &h);
        
        if(ds == DECODE_UNKNOWN_INSTRUCTION){
            nx.id_ex.valid = 1;
            nx.id_ex.pc = p->if_id.pc;
            nx.id_ex.fault = PF_ILLEGAL;
        } else {
            ControlSignals ctrl = control_decode(&d, h);

            /* Hazard Check: Test against instructions in EX and MEM stages */
            int ex_hazard = p->id_ex.valid && p->id_ex.ctrl.reg_write && (p->id_ex.instr.rd != 0) &&
                            ((ctrl.uses_rs1 && d.rs1 == p->id_ex.instr.rd) ||
                                (ctrl.uses_rs2 && d.rs2 == p->id_ex.instr.rd));

            int mem_hazard = p->ex_mem.valid && p->ex_mem.ctrl.reg_write && (p->ex_mem.instr.rd != 0) &&
                            ((ctrl.uses_rs1 && d.rs1 == p->ex_mem.instr.rd) ||
                            (ctrl.uses_rs2 && d.rs2 == p->ex_mem.instr.rd));
            
            if(ex_hazard || mem_hazard){
                stall = 1;
                p->stalls++;
                /* Inject bubble into ID/EX */
                nx.id_ex.valid = 0;
                /* Keep IF/ID state frozen */
                nx.if_id = p->if_id;
            } else {
                nx.id_ex.valid = 1;
                nx.id_ex.pc = p->if_id.pc;
                nx.id_ex.fault = p->if_id.fault;
                nx.id_ex.instr = d;
                nx.id_ex.handler = h;
                nx.id_ex.ctrl = ctrl;
                nx.id_ex.rs1_val = cpu_read_reg(cpu, d.rs1);
                nx.id_ex.rs2_val = cpu_read_reg(cpu, d.rs2);
            }
        }
    }

    /* =========================================================================
     * STAGE 1: IF (Instruction Fetch) - Writes nx.if_id
     * ========================================================================= */

     if(stall){
        /* IF is frozen during stalls: keep cpu->pc unchanged */
     } else {
        uint32_t fetch_pc = flush ? redirect_pc : cpu->pc;
        uint32_t raw_instr = 0;

        int fetch_status = memory_read_word(mem, fetch_pc, &raw_instr);

        nx.if_id.valid = 1;
        nx.if_id.pc = fetch_pc;

        if(fetch_status != MEM_OK){
            nx.if_id.fault = PF_FETCH;
            nx.if_id.raw = 0;
        } else {
            nx.if_id.fault = PF_NONE;
            nx.if_id.raw = 1;
        }

        cpu->pc = fetch_pc+4;

     }


    *p = nx; /* commit: one clock edge */
    return cpu->status;
}