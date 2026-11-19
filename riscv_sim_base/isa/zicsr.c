#include "zicsr.h"
#include "isa_registry.h"
#include "cpu_state.h"
#include <stdio.h>

#define OP_SYSTEM 0x73
#define CSR_FFLAGS 0x001
#define CSR_FRM    0x002
#define CSR_FCSR   0x003

static int csr_read(const CPUState *c, uint32_t csr, uint32_t *v) {
    switch (csr) {
        case CSR_FFLAGS: *v = c->fcsr & 0x1Fu;  return 1;
        case CSR_FRM:    *v = (c->fcsr >> 5) & 0x7u; return 1;
        case CSR_FCSR:   *v = c->fcsr & 0xFFu;  return 1;
    }
    return 0;
}

static void csr_write(CPUState *c, uint32_t csr, uint32_t v) {
    switch (csr) {
        case CSR_FFLAGS: c->fcsr = (c->fcsr & ~0x1Fu) | (v & 0x1Fu);            break;
        case CSR_FRM:    c->fcsr = (c->fcsr & ~0xE0u) | ((v & 0x7u) << 5);      break;
        case CSR_FCSR:   c->fcsr = v & 0xFFu;                                   break;
    }
}

/* kind: 0 = swap (csrrw), 1 = set bits (csrrs), 2 = clear bits (csrrc) */
static void do_csr(CPUState *c, DecodedInstr *i, int kind, uint32_t src, int src_nonzero) {
    uint32_t csr = (uint32_t)i->imm & 0xFFF, old;
    if (!csr_read(c, csr, &old)) {                 /* unknown CSR -> illegal instruction */
        c->status = CPU_ILLEGAL_INSTRUCTION;
        c->fault_addr = c->pc;
        snprintf(c->fault_msg, sizeof c->fault_msg, "unknown CSR 0x%03x at pc=0x%08x", csr, c->pc);
        return;
    }
    /* csrrs/csrrc with a zero source do not write (so frflags never changes anything) */
    if (kind == 0)                    csr_write(c, csr, src);
    else if (kind == 1 && src_nonzero) csr_write(c, csr, old | src);
    else if (kind == 2 && src_nonzero) csr_write(c, csr, old & ~src);
    cpu_write_reg(c, (int)i->rd, old);
}

static void exec_csrrw (CPUState *c, Memory *m, DecodedInstr *i) { (void)m; do_csr(c, i, 0, cpu_read_reg(c,(int)i->rs1), 1); }
static void exec_csrrs (CPUState *c, Memory *m, DecodedInstr *i) { (void)m; do_csr(c, i, 1, cpu_read_reg(c,(int)i->rs1), i->rs1 != 0); }
static void exec_csrrc (CPUState *c, Memory *m, DecodedInstr *i) { (void)m; do_csr(c, i, 2, cpu_read_reg(c,(int)i->rs1), i->rs1 != 0); }
static void exec_csrrwi(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; do_csr(c, i, 0, i->rs1, 1); }
static void exec_csrrsi(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; do_csr(c, i, 1, i->rs1, i->rs1 != 0); }
static void exec_csrrci(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; do_csr(c, i, 2, i->rs1, i->rs1 != 0); }

void register_zicsr(void) {
    #define CSRREG(mn, f3, fn) \
        register_instruction_ex((InstructionHandler){mn, OP_SYSTEM, f3, -1, FMT_CSR, fn}, 0, -1)
    CSRREG("csrrw",  1, exec_csrrw);   CSRREG("csrrs",  2, exec_csrrs);   CSRREG("csrrc",  3, exec_csrrc);
    CSRREG("csrrwi", 5, exec_csrrwi);  CSRREG("csrrsi", 6, exec_csrrsi);  CSRREG("csrrci", 7, exec_csrrci);
}