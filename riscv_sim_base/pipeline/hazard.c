/*This implements the baseline stalling policy. It checks all three forward latches (ID/EX, EX/MEM, and MEM/WB) to see if any of them produce a register that the current instruction in ID needs*/

#include "hazard.h"

/* valid latch + really writes a register + not x0 + same register number */
int produces(int valid, const ControlSignals *c, uint32_t rd, uint32_t rs) {
    return valid && c->reg_write && rd != 0 && rd == rs;
}

HazardVerdict hazard_check(const Pipeline *p, const DecodedInstr *d, const ControlSignals *c){
    (void)d;   /* sources now come from the unified numbers in c */
    HazardVerdict v = {0,0};
    int s[3] = { c->src1, c->src2, c->src3 };

    if(!p->forwarding){
        /* stall on any of the 3 latches, for any of the 3 sources */
        for(int k = 0; k < 3; k++){
            if( produces(p->id_ex.valid,  &p->id_ex.ctrl,  p->id_ex.ctrl.dst,  s[k]) ||
                produces(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.ctrl.dst, s[k]) ||
                produces(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.ctrl.dst, s[k]))
                v.stall = 1;
        }
        return v;
    }

    /*Task 3 here*/
    // This is for Load-Use hazard detection. A load's value does not exist till the end of MEM, but the next instruction
    // needs its operands at the start of EX. So the consumer may need to stall
    if(p->id_ex.valid && p->id_ex.ctrl.mem_read && p->id_ex.ctrl.dst != REG_NONE){
        for(int k = 0; k < 3; k++){
            if(s[k] == p->id_ex.ctrl.dst){
                v.stall = 1;
                v.load_use = 1;
            }
        }
    }
    return v;
}

Operands forward_operands(const Pipeline *p, const IDEX *d){
    Operands o = {d->rs1_val, d->rs2_val, 0, 0, d->rs3_val};

    if(!p->forwarding || d->fault != PF_NONE)
        return o;

    const int src[3] = { d->ctrl.src1, d->ctrl.src2, d->ctrl.src3 };
    uint32_t *val[3] = { &o.a, &o.b, &o.c };

    for(int k = 0; k < 3; k++){
        if(src[k] == REG_NONE) continue;
        if(produces(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.ctrl.dst, src[k])){
            *val[k] = p->ex_mem.alu_result;
            o.fwd_ex++;
        } else if(produces(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.ctrl.dst, src[k])){
            *val[k] = p->mem_wb.wb_val;
            o.fwd_mem++;
        }
    }
    return o;
}

// Operands forward_operands(const Pipeline *p, const IDEX *d){
//     Operands o = {d->rs1_val, d->rs2_val, 0, 0};

//     if(!p->forwarding || d->fault != PF_NONE){
//         // If forwarding is disabled or there is no fault, return the original operands without forwarding.
//         return o;
//     }

//     if(d->ctrl.uses_rs1 && d->instr.rs1 != 0){
//         if(produces(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.instr.rd, d->instr.rs1)){
//             o.a = p->ex_mem.alu_result;
//             o.fwd_ex++;
//         } else if(produces(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.instr.rd, d->instr.rs1)){
//             o.a = p->mem_wb.wb_val;
//             o.fwd_mem++;
//         }
//     }

//     if(d->ctrl.uses_rs2 && d->instr.rs2 != 0){
//         if(produces(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.instr.rd, d->instr.rs2)){
//             o.b = p->ex_mem.alu_result;
//             o.fwd_ex++;
//         } else if(produces(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.instr.rd, d->instr.rs2)){
//             o.b = p->mem_wb.wb_val;
//             o.fwd_mem++;
//         }
//     }

//     return o;

// }



