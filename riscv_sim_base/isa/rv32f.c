#include "rv32f.h"
#include "isa_registry.h"
#include "cpu_state.h"
#include "memory.h"
#include <stdio.h>

#define SIGN_BIT    0x80000000u
#define OP_LOAD_FP  0x07
#define OP_STORE_FP 0x27
#define OP_FP       0x53

#define FREG(n) cpu_read_freg(c, (int)(n))
#define FFF (IF_RD_FP | IF_RS1_FP | IF_RS2_FP)

static void reg(const char *mn, uint32_t op, int f3, int f7, InstrFormat fmt,
                ExecuteFn fn, uint32_t flags, int rs2sel) {
    register_instruction_ex((InstructionHandler){mn, op, f3, f7, fmt, fn}, flags, rs2sel);
}

static void fp_mem_fault(CPUState *c, uint32_t addr, const char *name) {
    c->status = CPU_MEM_FAULT;
    c->fault_addr = addr;
    snprintf(c->fault_msg, sizeof(c->fault_msg), "%s: out-of-bounds access at 0x%08x", name, addr);
}

/* ---- loads and stores ---- */
static void exec_flw(CPUState *c, Memory *m, DecodedInstr *i) {
    uint32_t addr = cpu_read_reg(c, (int)i->rs1) + (uint32_t)i->imm;
    uint32_t v;
    if (memory_read_word(m, addr, &v) != MEM_OK) { fp_mem_fault(c, addr, "flw"); return; }
    cpu_write_freg(c, (int)i->rd, v);
}

static void exec_fsw(CPUState *c, Memory *m, DecodedInstr *i) {
    uint32_t addr = cpu_read_reg(c, (int)i->rs1) + (uint32_t)i->imm;
    if (memory_write_word(m, addr, FREG(i->rs2)) != MEM_OK) fp_mem_fault(c, addr, "fsw");
}

/* ---- sign injection ---- */
static void exec_fsgnj(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_freg(c, (int)i->rd, (FREG(i->rs1) & ~SIGN_BIT) | (FREG(i->rs2) & SIGN_BIT));
}
static void exec_fsgnjn(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_freg(c, (int)i->rd, (FREG(i->rs1) & ~SIGN_BIT) | (~FREG(i->rs2) & SIGN_BIT));
}
static void exec_fsgnjx(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_freg(c, (int)i->rd, FREG(i->rs1) ^ (FREG(i->rs2) & SIGN_BIT));
}

/* ---- moves between the register files (reinterpret, never convert) ---- */
static void exec_fmv_x_w(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, (int)i->rd, FREG(i->rs1));
}
static void exec_fmv_w_x(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_freg(c, (int)i->rd, cpu_read_reg(c, (int)i->rs1));
}

/* ---- classify ---- */
static uint32_t classify(uint32_t b) {
    int neg = (b & SIGN_BIT) != 0;
    uint32_t e = (b >> 23) & 0xFF, f = b & 0x007FFFFFu;
    if (e == 0xFF) {
        if (f == 0) return neg ? 0 : 7;          /* -inf / +inf */
        return (f & 0x00400000u) ? 9 : 8;        /* qNaN / sNaN */
    }
    if (e == 0) {
        if (f == 0) return neg ? 3 : 4;          /* -0 / +0 */
        return neg ? 2 : 5;                      /* subnormal */
    }
    return neg ? 1 : 6;                          /* normal */
}
static void exec_fclass(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, (int)i->rd, 1u << classify(FREG(i->rs1)));
}

void register_rv32f(void) {
    /* Task 1: bits only */
    reg("flw",      OP_LOAD_FP,  0x2, -1,   FMT_I_LOAD, exec_flw,    IF_RD_FP,  0);
    reg("fsw",      OP_STORE_FP, 0x2, -1,   FMT_S,      exec_fsw,    IF_RS2_FP, 0);
    reg("fsgnj.s",  OP_FP, 0x0, 0x10, FMT_R,  exec_fsgnj,  FFF, 0);
    reg("fsgnjn.s", OP_FP, 0x1, 0x10, FMT_R,  exec_fsgnjn, FFF, 0);
    reg("fsgnjx.s", OP_FP, 0x2, 0x10, FMT_R,  exec_fsgnjx, FFF, 0);
    reg("fmv.x.w",  OP_FP, 0x0, 0x70, FMT_R2, exec_fmv_x_w, IF_RS1_FP | IF_RS2_FIXED, 0);
    reg("fmv.w.x",  OP_FP, 0x0, 0x78, FMT_R2, exec_fmv_w_x, IF_RD_FP  | IF_RS2_FIXED, 0);
    reg("fclass.s", OP_FP, 0x1, 0x70, FMT_R2, exec_fclass,  IF_RS1_FP | IF_RS2_FIXED, 0);
}