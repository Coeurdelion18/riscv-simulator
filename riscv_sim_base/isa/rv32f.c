#include "rv32f.h"
#include "fp_helpers.h"
#include "isa_registry.h"
#include "cpu_state.h"
#include "zicsr.h"
#include "memory.h"
#include <stdio.h>
#include <math.h>

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

static void do_arith(CPUState *c, DecodedInstr *i, ArithOp op, uint32_t A, uint32_t B, uint32_t C){
    int rm = resolve_rm(c, i);          /* funct3, or fcsr.frm for dyn */
    if (rm < 0) return;                 /* illegal mode: already faulted, nothing written */
    uint32_t flags = 0;
    uint32_t r = fp_arith(op, A, B, C, rm, &flags);
    cpu_raise_fflags(c, flags);         /* sticky OR into fcsr */
    cpu_write_freg(c, (int)i->rd, r);
}

static void do_cmp(CPUState *c, DecodedInstr *i, int kind) {
    uint32_t A = FREG(i->rs1), B = FREG(i->rs2);
    int r = 0;
    if (kind == 0) {
        if (is_snan_bits(A) || is_snan_bits(B)) cpu_raise_fflags(c, FFLAG_NV);  /* quiet compare */
        r = !is_nan_bits(A) && !is_nan_bits(B) && f_of(A) == f_of(B);
    } else {
        if (is_nan_bits(A) || is_nan_bits(B)) {          /* any NaN: NV, result 0 */
            cpu_raise_fflags(c, FFLAG_NV);
            cpu_write_reg(c, (int)i->rd, 0);
            return;
        }
        r = (kind == 1) ? (f_of(A) < f_of(B)) : (f_of(A) <= f_of(B));
    }
    cpu_write_reg(c, (int)i->rd, (uint32_t)r);
}
static void exec_feq(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; do_cmp(c, i, 0); }
static void exec_flt(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; do_cmp(c, i, 1); }
static void exec_fle(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; do_cmp(c, i, 2); }

static void do_minmax(CPUState *c, DecodedInstr *i, int is_max) {
    uint32_t A = FREG(i->rs1), B = FREG(i->rs2), r;
    if (is_snan_bits(A) || is_snan_bits(B)) cpu_raise_fflags(c, FFLAG_NV);   /* rule 1 */
    if (is_nan_bits(A) && is_nan_bits(B))      r = CANONICAL_NAN;            /* rule 2 */
    else if (is_nan_bits(A))                   r = B;                        /* rule 3: a NaN never wins */
    else if (is_nan_bits(B))                   r = A;
    else if (((A | B) & 0x7FFFFFFFu) == 0)     r = is_max ? (A & B) : (A | B);   /* rule 4: both zero */
    else {                                                                   /* rule 5 */
        float a = f_of(A), b = f_of(B);
        r = is_max ? (a > b ? A : B) : (a < b ? A : B);
    }
    cpu_write_freg(c, (int)i->rd, r);
}

static double round_to_int(double x, int rm) {
    if (isinf(x)) return x;
    switch (rm) {
        case 1:  return trunc(x);               /* rtz */
        case 2:  return floor(x);               /* rdn */
        case 3:  return ceil(x);                /* rup */
        case 4:  return round(x);               /* rmm: C's round() breaks ties away from zero */
        default: {                              /* rne: ties go to the even neighbour */
            double lo = floor(x), d = x - lo;
            if (d < 0.5) return lo;
            if (d > 0.5) return lo + 1.0;
            return (fmod(lo, 2.0) == 0.0) ? lo : lo + 1.0;
        }
    }
}

static void do_cvt_to_int(CPUState *c, DecodedInstr *i, int is_unsigned) {
    int rm = resolve_rm(c, i);
    if (rm < 0) return;                         /* illegal mode: nothing written */
    uint32_t A = FREG(i->rs1), res, fl = 0;

    if (is_nan_bits(A)) {
        res = is_unsigned ? 0xFFFFFFFFu : 0x7FFFFFFFu;
        fl = FFLAG_NV;
    } else {
        double x = (double)f_of(A);
        double r = round_to_int(x, rm);
        if (is_unsigned) {
            if (r >= 4294967296.0)  { res = 0xFFFFFFFFu; fl = FFLAG_NV; }
            else if (r < 0.0)       { res = 0;           fl = FFLAG_NV; }
            else                    { res = (uint32_t)r; if (r != x) fl = FFLAG_NX; }
        } else {
            if (r >= 2147483648.0)       { res = 0x7FFFFFFFu; fl = FFLAG_NV; }
            else if (r < -2147483648.0)  { res = 0x80000000u; fl = FFLAG_NV; }
            else                         { res = (uint32_t)(int32_t)r; if (r != x) fl = FFLAG_NX; }
        }
    }
    cpu_raise_fflags(c, fl);
    cpu_write_reg(c, (int)i->rd, res);
}
static void exec_fcvt_w_s (CPUState *c, Memory *m, DecodedInstr *i) { (void)m; do_cvt_to_int(c, i, 0); }
static void exec_fcvt_wu_s(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; do_cvt_to_int(c, i, 1); }

static void exec_fmin(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; do_minmax(c, i, 0); }
static void exec_fmax(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; do_minmax(c, i, 1); }

static void do_cvt_from_int(CPUState *c, DecodedInstr *i, int is_unsigned) {
    int rm = resolve_rm(c, i);
    if (rm < 0) return;
    uint32_t v = cpu_read_reg(c, (int)i->rs1), fl = 0;
    double d = is_unsigned ? (double)v : (double)(int32_t)v;
    uint32_t r = d2f(d, rm, &fl);
    cpu_raise_fflags(c, fl);
    cpu_write_freg(c, (int)i->rd, r);
}
static void exec_fcvt_s_w (CPUState *c, Memory *m, DecodedInstr *i) { (void)m; do_cvt_from_int(c, i, 0); }
static void exec_fcvt_s_wu(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; do_cvt_from_int(c, i, 1); }

static void exec_fadd(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_arith(c, i, AR_ADD, FREG(i->rs1), FREG(i->rs2), 0);
}

static void exec_fsub(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_arith(c, i, AR_SUB, FREG(i->rs1), FREG(i->rs2), 0);
}

static void exec_fmul(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_arith(c, i, AR_MUL, FREG(i->rs1), FREG(i->rs2), 0);
}
static void exec_fdiv(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_arith(c, i, AR_DIV, FREG(i->rs1), FREG(i->rs2), 0);
}
static void exec_fsqrt(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_arith(c, i, AR_SQRT, FREG(i->rs1), 0, 0);
}

static void exec_fmadd(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_arith(c, i, AR_FMA, FREG(i->rs1), FREG(i->rs2), FREG(i->rs3));
}
static void exec_fmsub(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_arith(c, i, AR_FMA, FREG(i->rs1), FREG(i->rs2), FREG(i->rs3) ^ SIGN_BIT);
}
static void exec_fnmsub(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_arith(c, i, AR_FMA, FREG(i->rs1) ^ SIGN_BIT, FREG(i->rs2), FREG(i->rs3));
}
static void exec_fnmadd(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_arith(c, i, AR_FMA, FREG(i->rs1) ^ SIGN_BIT, FREG(i->rs2), FREG(i->rs3) ^ SIGN_BIT);
}

void register_rv32f(void) {
    register_zicsr();
    /* Task 1: bits only */
    reg("flw",      OP_LOAD_FP,  0x2, -1,   FMT_I_LOAD, exec_flw,    IF_RD_FP,  0);
    reg("fsw",      OP_STORE_FP, 0x2, -1,   FMT_S,      exec_fsw,    IF_RS2_FP, 0);
    reg("fsgnj.s",  OP_FP, 0x0, 0x10, FMT_R,  exec_fsgnj,  FFF, 0);
    reg("fsgnjn.s", OP_FP, 0x1, 0x10, FMT_R,  exec_fsgnjn, FFF, 0);
    reg("fsgnjx.s", OP_FP, 0x2, 0x10, FMT_R,  exec_fsgnjx, FFF, 0);
    reg("fmv.x.w",  OP_FP, 0x0, 0x70, FMT_R2, exec_fmv_x_w, IF_RS1_FP | IF_RS2_FIXED, 0);
    reg("fmv.w.x",  OP_FP, 0x0, 0x78, FMT_R2, exec_fmv_w_x, IF_RD_FP  | IF_RS2_FIXED, 0);
    reg("fclass.s", OP_FP, 0x1, 0x70, FMT_R2, exec_fclass,  IF_RS1_FP | IF_RS2_FIXED, 0);
    reg("fadd.s",  OP_FP, -1, 0x00, FMT_R,  exec_fadd,  FFF | IF_HAS_RM, 0);
    reg("fsub.s",  OP_FP, -1, 0x04, FMT_R,  exec_fsub,  FFF | IF_HAS_RM, 0);
    reg("fmul.s",  OP_FP, -1, 0x08, FMT_R,  exec_fmul,  FFF | IF_HAS_RM, 0);
    reg("fdiv.s",  OP_FP, -1, 0x0C, FMT_R,  exec_fdiv,  FFF | IF_HAS_RM, 0);
    reg("fsqrt.s", OP_FP, -1, 0x2C, FMT_R2, exec_fsqrt, IF_RD_FP | IF_RS1_FP | IF_HAS_RM | IF_RS2_FIXED, 0);
    #define FFFF (FFF | IF_RS3_FP | IF_HAS_RM)
    reg("fmadd.s",  0x43, -1, -1, FMT_R4, exec_fmadd,  FFFF, 0);
    reg("fmsub.s",  0x47, -1, -1, FMT_R4, exec_fmsub,  FFFF, 0);
    reg("fnmsub.s", 0x4B, -1, -1, FMT_R4, exec_fnmsub, FFFF, 0);
    reg("fnmadd.s", 0x4F, -1, -1, FMT_R4, exec_fnmadd, FFFF, 0);
    reg("fle.s",  OP_FP, 0x0, 0x50, FMT_R, exec_fle,  IF_RS1_FP | IF_RS2_FP, 0);
    reg("flt.s",  OP_FP, 0x1, 0x50, FMT_R, exec_flt,  IF_RS1_FP | IF_RS2_FP, 0);
    reg("feq.s",  OP_FP, 0x2, 0x50, FMT_R, exec_feq,  IF_RS1_FP | IF_RS2_FP, 0);
    reg("fmin.s", OP_FP, 0x0, 0x14, FMT_R, exec_fmin, FFF, 0);
    reg("fmax.s", OP_FP, 0x1, 0x14, FMT_R, exec_fmax, FFF, 0);
    reg("fcvt.w.s",  OP_FP, -1, 0x60, FMT_R2, exec_fcvt_w_s,  IF_RS1_FP | IF_HAS_RM | IF_RS2_FIXED, 0);
    reg("fcvt.wu.s", OP_FP, -1, 0x60, FMT_R2, exec_fcvt_wu_s, IF_RS1_FP | IF_HAS_RM | IF_RS2_FIXED, 1);
    reg("fcvt.s.w",  OP_FP, -1, 0x68, FMT_R2, exec_fcvt_s_w,  IF_RD_FP  | IF_HAS_RM | IF_RS2_FIXED, 0);
    reg("fcvt.s.wu", OP_FP, -1, 0x68, FMT_R2, exec_fcvt_s_wu, IF_RD_FP  | IF_HAS_RM | IF_RS2_FIXED, 1);
}