#ifndef CONTROL_H
#define CONTROL_H
#define REG_NONE   0     /* x0, unused operand, or no destination */
#define FREG_BASE  32    /* f0..f31 -> 32..63; x1..x31 -> 1..31 */
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
	int uses_rs3;          /* fused multiply-add reads a third source */
	int src1, src2, src3;  /* unified register numbers, REG_NONE if unused */
	int dst;               /* unified destination, REG_NONE if none */
	int use_handler;
} ControlSignals;


ControlSignals control_decode(const DecodedInstr* i, const InstructionHandler* h);

#endif