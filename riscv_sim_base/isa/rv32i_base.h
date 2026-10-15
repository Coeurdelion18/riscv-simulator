#ifndef RV32I_BASE_H
#define RV32I_BASE_H

/* Registers every base RV32I instruction (arithmetic, logic, loads, stores,
 * branches, jumps, upper-immediate, and a minimal ecall) into the ISA
 * registry. Call this once at startup, before assembling or running any
 * program. Later labs add their own register_rv32m(), register_rv32f(),
 * register_custom_ext(), etc. alongside this call in main.c -- they never
 * need to modify this file. */
void register_rv32i_base(void);

#endif /* RV32I_BASE_H */
