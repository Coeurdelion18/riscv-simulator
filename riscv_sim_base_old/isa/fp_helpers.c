#include "fp_helpers.h"
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(float) == sizeof(uint32_t) && FLT_RADIX == 2 && FLT_MANT_DIG == 24,
               "RV32F helpers require IEEE-754 binary32 host floats");

float f_of(uint32_t b)    { float f; memcpy(&f, &b, sizeof f); return f; }
uint32_t bits_of(float f) { uint32_t b; memcpy(&b, &f, sizeof b); return b; }

int resolve_rm(CPUState *c, const DecodedInstr *i) {
    int rm = (int)i->funct3;
    if (rm == 7) rm = (int)cpu_frm(c);          /* dyn: use fcsr.frm */
    if (rm > 4) {                               /* 5, 6, or dyn with a bad frm */
        c->status = CPU_ILLEGAL_INSTRUCTION;
        c->fault_addr = c->pc;
        snprintf(c->fault_msg, sizeof c->fault_msg,
                 "illegal rounding mode %d at pc=0x%08x", rm, c->pc);
        return -1;
    }
    return rm;
}

int host_mode(int rm) {
    switch (rm) {
        case 1:  return FE_TOWARDZERO;
        case 2:  return FE_DOWNWARD;
        case 3:  return FE_UPWARD;
        default: return FE_TONEAREST;           /* 0 = rne; 4 = rmm starts from rne */
    }
}

uint32_t fflags_of(int ex) {
    uint32_t f = 0;
    if (ex & FE_INVALID)   f |= FFLAG_NV;
    if (ex & FE_DIVBYZERO) f |= FFLAG_DZ;
    if (ex & FE_OVERFLOW)  f |= FFLAG_OF;
    if (ex & FE_UNDERFLOW) f |= FFLAG_UF;
    if (ex & FE_INEXACT)   f |= FFLAG_NX;
    if (f & (FFLAG_OF | FFLAG_UF)) f |= FFLAG_NX;   /* overflow/underflow are always inexact */
    return f;
}

/* RMM fix-up. `r` is the round-to-nearest-EVEN result, `exact` the exact value
 * (only meaningful if it was computed without rounding). The two modes differ
 * only when `exact` sits exactly halfway between two adjacent floats: rne takes
 * the even neighbour, rmm the one with the larger magnitude. */
static float rmm_adjust(float r, double exact) {
    if (!isfinite(r) || !isfinite(exact)) return r;
    if ((double)r == exact) return r;                         /* representable: no tie */
    float n = nextafterf(r, exact > (double)r ? INFINITY : -INFINITY);
    if (!isfinite(n)) return r;
    double mid = ((double)r + (double)n) / 2.0;              /* exact in double */
    if (mid == exact && fabsf(n) > fabsf(r)) return n;
    return r;
}

uint32_t d2f(double d, int rm, uint32_t *flags) {
    fenv_t environment;
    fegetenv(&environment);
    int saved = fegetround();
    fesetround(host_mode(rm));
    feclearexcept(FE_ALL_EXCEPT);
    volatile float r = (float)d;
    int ex = fetestexcept(FE_ALL_EXCEPT);
    fesetround(saved);
    *flags |= fflags_of(ex);
    float out = r;
    if (rm == 4) out = rmm_adjust(out, d);
    fesetenv(&environment);
    return bits_of(out);
}

/* One operation on the host FPU in the requested mode, collecting host flags. */
static float host_op(ArithOp op, float a, float b, float c, int rm, int *ex) {
    int saved = fegetround();
    fesetround(host_mode(rm));
    feclearexcept(FE_ALL_EXCEPT);
    volatile float r = 0.0f;
    switch (op) {
        case AR_ADD:  r = a + b;      break;
        case AR_SUB:  r = a - b;      break;
        case AR_MUL:  r = a * b;      break;
        case AR_DIV:  r = a / b;      break;
        case AR_SQRT: r = sqrtf(a);   break;
        case AR_FMA:  r = fmaf(a, b, c); break;
    }
    *ex = fetestexcept(FE_ALL_EXCEPT);
    fesetround(saved);
    return r;
}

/* Computes the same operation in double. Returns 1 if the double result is
 * exact (no inexact flag), so it can be used for the RMM tie test. */
static int exact_in_double(ArithOp op, float a, float b, float c, double *d) {
    volatile double x;
    feclearexcept(FE_ALL_EXCEPT);
    switch (op) {
        case AR_ADD: x = (double)a + (double)b; break;
        case AR_SUB: x = (double)a - (double)b; break;
        case AR_MUL: x = (double)a * (double)b; break;
        case AR_FMA: x = (double)a * (double)b + (double)c; break;
        default:     return 0;      /* quotients and square roots are never exact ties */
    }
    *d = x;
    return !fetestexcept(FE_INEXACT);
}

uint32_t fp_arith(ArithOp op, uint32_t A, uint32_t B, uint32_t C, int rm, uint32_t *flags) {
    fenv_t environment;
    fegetenv(&environment);
    uint32_t fl = 0;
    int uses_b = (op != AR_SQRT), uses_c = (op == AR_FMA);

    /* RISC-V: any signalling-NaN operand raises NV. */
    if (is_snan_bits(A) || (uses_b && is_snan_bits(B)) || (uses_c && is_snan_bits(C)))
        fl |= FFLAG_NV;

    float a = f_of(A), b = f_of(B), c = f_of(C);

    /* RISC-V: inf * 0 is invalid even when the addend is a quiet NaN. */
    if (op == AR_FMA && ((isinf(a) && b == 0.0f) || (a == 0.0f && isinf(b))))
        fl |= FFLAG_NV;

    int ex = 0;
    float r = host_op(op, a, b, c, rm, &ex);
    fl |= fflags_of(ex);

    if (rm == 4) {
        double d;
        fesetround(FE_TONEAREST);
        if (exact_in_double(op, a, b, c, &d)) r = rmm_adjust(r, d);
    }

    *flags |= fl;
    fesetenv(&environment);
    return isnan(r) ? CANONICAL_NAN : bits_of(r);       /* canonicalise every NaN result */
}
