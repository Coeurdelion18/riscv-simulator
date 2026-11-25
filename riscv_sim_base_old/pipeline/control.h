#ifndef CONTROL_H
#define CONTROL_H
#include "isa_registry.h"

typedef enum {
	WB_NONE = 0,
	WB_ALU,
	WB_MEM,
	WB_PC4
} WbSrc;


typedef struct {
	int reg_write;
	WbSrc wb_src;
	int mem_read;
	int mem_write;
	int is_branch;
	int is_jump;
	int is_system;
	int uses_rs1;
	int uses_rs2;
    int uses_rs3;
    uint32_t src1, src2, src3, dst;
} ControlSignals;


ControlSignals control_decode(const DecodedInstr* i, const InstructionHandler* h);


/* Zero denotes x0/no dependency. FP identities include writable f0 at 32. */
static inline uint32_t register_read(const CPUState *cpu, uint32_t reg) {
    return reg >= 32 ? cpu_read_freg(cpu, (int)reg - 32) : cpu_read_reg(cpu, (int)reg);
}
static inline void register_write(CPUState *cpu, uint32_t reg, uint32_t value) {
    if (reg >= 32) cpu_write_freg(cpu, (int)reg - 32, value);
    else cpu_write_reg(cpu, (int)reg, value);
}
#endif