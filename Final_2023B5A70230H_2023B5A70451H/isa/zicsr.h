#ifndef ZICSR_H
#define ZICSR_H

/* Registers the Zicsr instructions (csrrw, csrrs, csrrc, csrrwi, csrrsi,
 * csrrci). Only the floating-point CSRs exist in this simulator:
 *   0x001 fflags   accrued exception flags (fcsr bits 4:0)
 *   0x002 frm      dynamic rounding mode   (fcsr bits 7:5)
 *   0x003 fcsr     both
 * Any other CSR number makes the instruction illegal. */
void register_zicsr(void);

#endif /* ZICSR_H */
