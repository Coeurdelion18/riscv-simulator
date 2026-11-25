#ifndef RV32F_H
#define RV32F_H

/* Registers the RV32F standard extension (single-precision floating point):
 * flw/fsw, arithmetic (fadd/fsub/fmul/fdiv/fsqrt, fmin/fmax, fused
 * multiply-add), sign injection, comparisons, conversions, moves and
 * fclass. Call after register_rv32i_base() in main.c. */
void register_rv32f(void);

#endif /* RV32F_H */
