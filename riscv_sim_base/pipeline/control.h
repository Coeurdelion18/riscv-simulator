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
} ControlSignals;


ControlSignals control_decode(const DecodedInstr* i, const InstructionHandler* h);

#endif