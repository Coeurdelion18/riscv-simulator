#ifndef FP_HELPERS_H
#define FP_HELPERS_H

#include <stdint.h>
#include "cpu_state.h"
#include "isa_registry.h"

#define CANONICAL_NAN 0x7fc00000u

typedef enum { AR_ADD, AR_SUB, AR_MUL, AR_DIV, AR_SQRT, AR_FMA } ArithOp;

// Read raw floating point bits through memcpy without violating strict aliasing rules.
float    f_of(uint32_t bits);
// Copy floating point representations into integer storage while preserving their original bits.
uint32_t bits_of(float f);
static inline int is_nan_bits(uint32_t b)  { return ((b >> 23) & 0xFF) == 0xFF && (b & 0x007FFFFFu) != 0; }
static inline int is_snan_bits(uint32_t b) { return is_nan_bits(b) && !(b & 0x00400000u); }

// Resolve static or dynamic rounding modes and reject reserved encodings before execution.
int      resolve_rm(CPUState *c, const DecodedInstr *i);
// Translate supported RISC-V rounding modes into corresponding constants for host arithmetic operations.
int      host_mode(int rm);
// Map host floating point exceptions onto the five architectural accrued exception flags.
uint32_t fflags_of(int host_excepts);

// Round exact double precision values into binary32 while accumulating architectural exception flags.
uint32_t d2f(double d, int rm, uint32_t *flags);

// Calculate arithmetic results from operand bits using selected rounding and accrued flags.
uint32_t fp_arith(ArithOp op, uint32_t A, uint32_t B, uint32_t C, int rm, uint32_t *flags);

#endif

