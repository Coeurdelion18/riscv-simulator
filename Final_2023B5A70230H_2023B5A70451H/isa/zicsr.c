#include "zicsr.h"
#include "isa_registry.h"
#include "cpu_state.h"
#include <stdio.h>

#define OP_SYSTEM 0x73

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

// Select register replacement or bitwise update behavior using the decoded CSR operation.
static void do_csr(CPUState *c, DecodedInstr *i, int kind, uint32_t src, int src_nonzero) {
    uint32_t csr = (uint32_t)i->imm & 0xFFF, old;
    // Unsupported CSR numbers raise an illegal instruction fault before changing destination registers.
    if (!csr_read(c, csr, &old)) {
        c->status = CPU_ILLEGAL_INSTRUCTION;
        c->fault_addr = c->pc;
        snprintf(c->fault_msg, sizeof c->fault_msg, "unknown CSR 0x%03x at pc=0x%08x", csr, c->pc);
        return;
    }
    // Zero source register fields suppress CSR writes for set and clear operations.
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
    register_instruction((InstructionHandler){"csrrw",  OP_SYSTEM, 0x1, -1, FMT_CSR,  exec_csrrw});
    register_instruction((InstructionHandler){"csrrs",  OP_SYSTEM, 0x2, -1, FMT_CSR,  exec_csrrs});
    register_instruction((InstructionHandler){"csrrc",  OP_SYSTEM, 0x3, -1, FMT_CSR,  exec_csrrc});
    register_instruction((InstructionHandler){"csrrwi", OP_SYSTEM, 0x5, -1, FMT_CSRI, exec_csrrwi});
    register_instruction((InstructionHandler){"csrrsi", OP_SYSTEM, 0x6, -1, FMT_CSRI, exec_csrrsi});
    register_instruction((InstructionHandler){"csrrci", OP_SYSTEM, 0x7, -1, FMT_CSRI, exec_csrrci});
}
