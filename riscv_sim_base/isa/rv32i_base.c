#include "rv32i_base.h"
#include "isa_registry.h"
#include <stdio.h>
#include <stdint.h>

/* ---- opcode constants (bits [6:0]) ---- */
#define OP_R      0x33
#define OP_IARITH 0x13
#define OP_LOAD   0x03
#define OP_STORE  0x23
#define OP_BRANCH 0x63
#define OP_JALR   0x67
#define OP_JAL    0x6F
#define OP_LUI    0x37
#define OP_AUIPC  0x17
#define OP_SYSTEM 0x73

/* =======================  R-type  ======================= */

static void exec_add(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, i->rd, cpu_read_reg(c, i->rs1) + cpu_read_reg(c, i->rs2));
}
static void exec_sub(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, i->rd, cpu_read_reg(c, i->rs1) - cpu_read_reg(c, i->rs2));
}
static void exec_sll(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, i->rd, cpu_read_reg(c, i->rs1) << (cpu_read_reg(c, i->rs2) & 0x1F));
}
static void exec_slt(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    int32_t a = (int32_t)cpu_read_reg(c, i->rs1), b = (int32_t)cpu_read_reg(c, i->rs2);
    cpu_write_reg(c, i->rd, (a < b) ? 1 : 0);
}
static void exec_sltu(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    uint32_t a = cpu_read_reg(c, i->rs1), b = cpu_read_reg(c, i->rs2);
    cpu_write_reg(c, i->rd, (a < b) ? 1 : 0);
}
static void exec_xor(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, i->rd, cpu_read_reg(c, i->rs1) ^ cpu_read_reg(c, i->rs2));
}
static void exec_srl(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, i->rd, cpu_read_reg(c, i->rs1) >> (cpu_read_reg(c, i->rs2) & 0x1F));
}
static void exec_sra(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    int32_t a = (int32_t)cpu_read_reg(c, i->rs1);
    cpu_write_reg(c, i->rd, (uint32_t)(a >> (cpu_read_reg(c, i->rs2) & 0x1F)));
}
static void exec_or(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, i->rd, cpu_read_reg(c, i->rs1) | cpu_read_reg(c, i->rs2));
}
static void exec_and(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, i->rd, cpu_read_reg(c, i->rs1) & cpu_read_reg(c, i->rs2));
}

/* =======================  I-type arithmetic  ======================= */

static void exec_addi(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, i->rd, cpu_read_reg(c, i->rs1) + (uint32_t)i->imm);
}
static void exec_slti(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    int32_t a = (int32_t)cpu_read_reg(c, i->rs1);
    cpu_write_reg(c, i->rd, (a < i->imm) ? 1 : 0);
}
static void exec_sltiu(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    uint32_t a = cpu_read_reg(c, i->rs1);
    cpu_write_reg(c, i->rd, (a < (uint32_t)i->imm) ? 1 : 0);
}
static void exec_xori(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, i->rd, cpu_read_reg(c, i->rs1) ^ (uint32_t)i->imm);
}
static void exec_ori(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, i->rd, cpu_read_reg(c, i->rs1) | (uint32_t)i->imm);
}
static void exec_andi(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, i->rd, cpu_read_reg(c, i->rs1) & (uint32_t)i->imm);
}
static void exec_slli(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, i->rd, cpu_read_reg(c, i->rs1) << (i->imm & 0x1F));
}
static void exec_srli(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, i->rd, cpu_read_reg(c, i->rs1) >> (i->imm & 0x1F));
}
static void exec_srai(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    int32_t a = (int32_t)cpu_read_reg(c, i->rs1);
    cpu_write_reg(c, i->rd, (uint32_t)(a >> (i->imm & 0x1F)));
}

/* =======================  Loads  ======================= */

static void mem_fault(CPUState *c, uint32_t addr, const char *what) {
    c->status = CPU_MEM_FAULT;
    c->fault_addr = addr;
    snprintf(c->fault_msg, sizeof(c->fault_msg), "%s out of bounds at address 0x%08x", what, addr);
}

static void exec_lb(CPUState *c, Memory *m, DecodedInstr *i) {
    uint32_t addr = cpu_read_reg(c, i->rs1) + (uint32_t)i->imm;
    uint8_t v;
    if (memory_read_byte(m, addr, &v) != MEM_OK) { mem_fault(c, addr, "lb"); return; }
    cpu_write_reg(c, i->rd, (uint32_t)(int32_t)(int8_t)v);
}
static void exec_lh(CPUState *c, Memory *m, DecodedInstr *i) {
    uint32_t addr = cpu_read_reg(c, i->rs1) + (uint32_t)i->imm;
    uint16_t v;
    if (memory_read_half(m, addr, &v) != MEM_OK) { mem_fault(c, addr, "lh"); return; }
    cpu_write_reg(c, i->rd, (uint32_t)(int32_t)(int16_t)v);
}
static void exec_lw(CPUState *c, Memory *m, DecodedInstr *i) {
    uint32_t addr = cpu_read_reg(c, i->rs1) + (uint32_t)i->imm;
    uint32_t v;
    if (memory_read_word(m, addr, &v) != MEM_OK) { mem_fault(c, addr, "lw"); return; }
    cpu_write_reg(c, i->rd, v);
}
static void exec_lbu(CPUState *c, Memory *m, DecodedInstr *i) {
    uint32_t addr = cpu_read_reg(c, i->rs1) + (uint32_t)i->imm;
    uint8_t v;
    if (memory_read_byte(m, addr, &v) != MEM_OK) { mem_fault(c, addr, "lbu"); return; }
    cpu_write_reg(c, i->rd, (uint32_t)v);
}
static void exec_lhu(CPUState *c, Memory *m, DecodedInstr *i) {
    uint32_t addr = cpu_read_reg(c, i->rs1) + (uint32_t)i->imm;
    uint16_t v;
    if (memory_read_half(m, addr, &v) != MEM_OK) { mem_fault(c, addr, "lhu"); return; }
    cpu_write_reg(c, i->rd, (uint32_t)v);
}

/* =======================  Stores  ======================= */

static void exec_sb(CPUState *c, Memory *m, DecodedInstr *i) {
    uint32_t addr = cpu_read_reg(c, i->rs1) + (uint32_t)i->imm;
    if (memory_write_byte(m, addr, (uint8_t)cpu_read_reg(c, i->rs2)) != MEM_OK) mem_fault(c, addr, "sb");
}
static void exec_sh(CPUState *c, Memory *m, DecodedInstr *i) {
    uint32_t addr = cpu_read_reg(c, i->rs1) + (uint32_t)i->imm;
    if (memory_write_half(m, addr, (uint16_t)cpu_read_reg(c, i->rs2)) != MEM_OK) mem_fault(c, addr, "sh");
}
static void exec_sw(CPUState *c, Memory *m, DecodedInstr *i) {
    uint32_t addr = cpu_read_reg(c, i->rs1) + (uint32_t)i->imm;
    if (memory_write_word(m, addr, cpu_read_reg(c, i->rs2)) != MEM_OK) mem_fault(c, addr, "sw");
}

/* =======================  Branches  ======================= */
/* Each branch handler only overwrites next_pc when taken; execute_cycle()
 * already defaulted next_pc to pc+4, so "not taken" needs no extra code. */

static void exec_beq(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    if (cpu_read_reg(c, i->rs1) == cpu_read_reg(c, i->rs2)) c->next_pc = c->pc + (uint32_t)i->imm;
}
static void exec_bne(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    if (cpu_read_reg(c, i->rs1) != cpu_read_reg(c, i->rs2)) c->next_pc = c->pc + (uint32_t)i->imm;
}
static void exec_blt(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    if ((int32_t)cpu_read_reg(c, i->rs1) < (int32_t)cpu_read_reg(c, i->rs2)) c->next_pc = c->pc + (uint32_t)i->imm;
}
static void exec_bge(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    if ((int32_t)cpu_read_reg(c, i->rs1) >= (int32_t)cpu_read_reg(c, i->rs2)) c->next_pc = c->pc + (uint32_t)i->imm;
}
static void exec_bltu(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    if (cpu_read_reg(c, i->rs1) < cpu_read_reg(c, i->rs2)) c->next_pc = c->pc + (uint32_t)i->imm;
}
static void exec_bgeu(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    if (cpu_read_reg(c, i->rs1) >= cpu_read_reg(c, i->rs2)) c->next_pc = c->pc + (uint32_t)i->imm;
}

/* =======================  Jumps / upper immediate  ======================= */

static void exec_jal(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, i->rd, c->pc + 4);
    c->next_pc = c->pc + (uint32_t)i->imm;
}
static void exec_jalr(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    uint32_t target = (cpu_read_reg(c, i->rs1) + (uint32_t)i->imm) & ~1u; /* LSB cleared per spec */
    cpu_write_reg(c, i->rd, c->pc + 4);
    c->next_pc = target;
}
static void exec_lui(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, i->rd, (uint32_t)i->imm);
}
static void exec_auipc(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, i->rd, c->pc + (uint32_t)i->imm);
}

/* =======================  System (minimal) ======================= */
/* Syscall convention (deliberately tiny, extend freely in later labs):
 *   a7 = 1   -> print the signed integer in a0 to stdout
 *   a7 = 10  -> exit (halts the machine cleanly)
 *   anything else -> ignored (prints a warning to stderr, execution continues) */

static void exec_ecall(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; (void)i;
    uint32_t which = cpu_read_reg(c, 17); /* a7 */
    switch (which) {
        case 1:
            printf("%d", (int32_t)cpu_read_reg(c, 10)); /* a0 */
            fflush(stdout);
            break;
        case 10:
            c->status = CPU_ECALL_EXIT;
            break;
        default:
            fprintf(stderr, "[warning] unhandled ecall a7=%u at pc=0x%08x (ignored)\n", which, c->pc);
            break;
    }
}

/* =======================  Registration  ======================= */

void register_rv32i_base(void) {
    /* R-type */
    register_instruction((InstructionHandler){"add",  OP_R, 0x0, 0x00, FMT_R, exec_add});
    register_instruction((InstructionHandler){"sub",  OP_R, 0x0, 0x20, FMT_R, exec_sub});
    register_instruction((InstructionHandler){"sll",  OP_R, 0x1, 0x00, FMT_R, exec_sll});
    register_instruction((InstructionHandler){"slt",  OP_R, 0x2, 0x00, FMT_R, exec_slt});
    register_instruction((InstructionHandler){"sltu", OP_R, 0x3, 0x00, FMT_R, exec_sltu});
    register_instruction((InstructionHandler){"xor",  OP_R, 0x4, 0x00, FMT_R, exec_xor});
    register_instruction((InstructionHandler){"srl",  OP_R, 0x5, 0x00, FMT_R, exec_srl});
    register_instruction((InstructionHandler){"sra",  OP_R, 0x5, 0x20, FMT_R, exec_sra});
    register_instruction((InstructionHandler){"or",   OP_R, 0x6, 0x00, FMT_R, exec_or});
    register_instruction((InstructionHandler){"and",  OP_R, 0x7, 0x00, FMT_R, exec_and});

    /* I-type arithmetic */
    register_instruction((InstructionHandler){"addi",  OP_IARITH, 0x0, -1,   FMT_I,       exec_addi});
    register_instruction((InstructionHandler){"slti",  OP_IARITH, 0x2, -1,   FMT_I,       exec_slti});
    register_instruction((InstructionHandler){"sltiu", OP_IARITH, 0x3, -1,   FMT_I,       exec_sltiu});
    register_instruction((InstructionHandler){"xori",  OP_IARITH, 0x4, -1,   FMT_I,       exec_xori});
    register_instruction((InstructionHandler){"ori",   OP_IARITH, 0x6, -1,   FMT_I,       exec_ori});
    register_instruction((InstructionHandler){"andi",  OP_IARITH, 0x7, -1,   FMT_I,       exec_andi});
    register_instruction((InstructionHandler){"slli",  OP_IARITH, 0x1, 0x00, FMT_I_SHAMT, exec_slli});
    register_instruction((InstructionHandler){"srli",  OP_IARITH, 0x5, 0x00, FMT_I_SHAMT, exec_srli});
    register_instruction((InstructionHandler){"srai",  OP_IARITH, 0x5, 0x20, FMT_I_SHAMT, exec_srai});

    /* Loads */
    register_instruction((InstructionHandler){"lb",  OP_LOAD, 0x0, -1, FMT_I_LOAD, exec_lb});
    register_instruction((InstructionHandler){"lh",  OP_LOAD, 0x1, -1, FMT_I_LOAD, exec_lh});
    register_instruction((InstructionHandler){"lw",  OP_LOAD, 0x2, -1, FMT_I_LOAD, exec_lw});
    register_instruction((InstructionHandler){"lbu", OP_LOAD, 0x4, -1, FMT_I_LOAD, exec_lbu});
    register_instruction((InstructionHandler){"lhu", OP_LOAD, 0x5, -1, FMT_I_LOAD, exec_lhu});

    /* Stores */
    register_instruction((InstructionHandler){"sb", OP_STORE, 0x0, -1, FMT_S, exec_sb});
    register_instruction((InstructionHandler){"sh", OP_STORE, 0x1, -1, FMT_S, exec_sh});
    register_instruction((InstructionHandler){"sw", OP_STORE, 0x2, -1, FMT_S, exec_sw});

    /* Branches */
    register_instruction((InstructionHandler){"beq",  OP_BRANCH, 0x0, -1, FMT_B, exec_beq});
    register_instruction((InstructionHandler){"bne",  OP_BRANCH, 0x1, -1, FMT_B, exec_bne});
    register_instruction((InstructionHandler){"blt",  OP_BRANCH, 0x4, -1, FMT_B, exec_blt});
    register_instruction((InstructionHandler){"bge",  OP_BRANCH, 0x5, -1, FMT_B, exec_bge});
    register_instruction((InstructionHandler){"bltu", OP_BRANCH, 0x6, -1, FMT_B, exec_bltu});
    register_instruction((InstructionHandler){"bgeu", OP_BRANCH, 0x7, -1, FMT_B, exec_bgeu});

    /* Jumps / upper immediate */
    register_instruction((InstructionHandler){"jal",   OP_JAL,   -1,  -1, FMT_J,      exec_jal});
    register_instruction((InstructionHandler){"jalr",  OP_JALR,  0x0, -1, FMT_I_JALR, exec_jalr});
    register_instruction((InstructionHandler){"lui",   OP_LUI,   -1,  -1, FMT_U,      exec_lui});
    register_instruction((InstructionHandler){"auipc", OP_AUIPC, -1,  -1, FMT_U,      exec_auipc});

    /* System */
    register_instruction((InstructionHandler){"ecall", OP_SYSTEM, 0x0, -1, FMT_I_SYS, exec_ecall});
}
