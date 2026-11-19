#ifndef FP_HELPERS_H
#define FP_HELPERS_H

#include <stdint.h>
#include "cpu_state.h"
#include "isa_registry.h"

#define CANONICAL_NAN 0x7fc00000u

typedef enum { AR_ADD, AR_SUB, AR_MUL, AR_DIV, AR_SQRT, AR_FMA } ArithOp;

float    f_of(uint32_t bits);                 /* bits -> float, via memcpy */
uint32_t bits_of(float f);                    /* float -> bits, via memcpy */
static inline int is_nan_bits(uint32_t b)  { return ((b >> 23) & 0xFF) == 0xFF && (b & 0x007FFFFFu) != 0; }
static inline int is_snan_bits(uint32_t b) { return is_nan_bits(b) && !(b & 0x00400000u); }

/* funct3 or fcsr.frm -> 0..4, or -1 after raising ILLEGAL_INSTRUCTION */
int      resolve_rm(CPUState *c, const DecodedInstr *i);
int      host_mode(int rm);                   /* RISC-V mode -> <fenv.h> constant */
uint32_t fflags_of(int host_excepts);         /* fetestexcept bits -> NV DZ OF UF NX */

/* Rounds an exact double to float in mode rm (including RMM); ORs flags into *flags. */
uint32_t d2f(double d, int rm, uint32_t *flags);

/* op(A,B,C) on bit patterns in mode rm. Returns the result bits, ORs flags into *flags. */
uint32_t fp_arith(ArithOp op, uint32_t A, uint32_t B, uint32_t C, int rm, uint32_t *flags);

#endif

