#ifndef RV32M_H
#define RV32M_H

/* Registers the RV32M standard extension (integer multiply / divide):
 * mul, mulh, mulhsu, mulhu, div, divu, rem, remu. Call after
 * register_rv32i_base() in main.c. */
void register_rv32m(void);

#endif /* RV32M_H */
