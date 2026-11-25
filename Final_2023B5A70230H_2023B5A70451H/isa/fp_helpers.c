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
    // Dynamic rounding instructions obtain their rounding mode from the current control register.
    if (rm == 7) rm = (int)cpu_frm(c);
    // Reserved rounding modes raise an illegal instruction fault before changing architectural state.
    if (rm > 4) {
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
        // Nearest even rounding also supplies the initial result for RMM tie correction.
        default: return FE_TONEAREST;
    }
}

uint32_t fflags_of(int ex) {
    uint32_t f = 0;
    if (ex & FE_INVALID)   f |= FFLAG_NV;
    if (ex & FE_DIVBYZERO) f |= FFLAG_DZ;
    if (ex & FE_OVERFLOW)  f |= FFLAG_OF;
    if (ex & FE_UNDERFLOW) f |= FFLAG_UF;
    if (ex & FE_INEXACT)   f |= FFLAG_NX;
    // Overflow and underflow exceptions also report inexactness when translating host exception flags.
    if (f & (FFLAG_OF | FFLAG_UF)) f |= FFLAG_NX;
    return f;
}

// Exact halfway results use the neighboring value with greater magnitude for RMM.
static float rmm_adjust(float r, double exact) {
    if (!isfinite(r) || !isfinite(exact)) return r;
    // Exactly representable values need no adjustment because neither rounding mode encounters ties.
    if ((double)r == exact) return r;
    float n = nextafterf(r, exact > (double)r ? INFINITY : -INFINITY);
    if (!isfinite(n)) return r;
    // Adjacent binary32 values have a midpoint representable exactly using double precision arithmetic.
    double mid = ((double)r + (double)n) / 2.0;
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

// Run one floating point operation using host rounding while collecting exception flags.
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

// Use exact double precision results to identify halfway cases requiring RMM correction.
static int exact_in_double(ArithOp op, float a, float b, float c, double *d) {
    volatile double x;
    feclearexcept(FE_ALL_EXCEPT);
    switch (op) {
        case AR_ADD: x = (double)a + (double)b; break;
        case AR_SUB: x = (double)a - (double)b; break;
        case AR_MUL: x = (double)a * (double)b; break;
        case AR_FMA: x = (double)a * (double)b + (double)c; break;
        // Quotients and square roots cannot require this exact halfway RMM adjustment step.
        default:     return 0;
    }
    *d = x;
    return !fetestexcept(FE_INEXACT);
}

uint32_t fp_arith(ArithOp op, uint32_t A, uint32_t B, uint32_t C, int rm, uint32_t *flags) {
    fenv_t environment;
    fegetenv(&environment);
    uint32_t fl = 0;
    int uses_b = (op != AR_SQRT), uses_c = (op == AR_FMA);

    // Signaling NaN operands raise invalid operation flags before arithmetic results are canonicalized.
    if (is_snan_bits(A) || (uses_b && is_snan_bits(B)) || (uses_c && is_snan_bits(C)))
        fl |= FFLAG_NV;

    float a = f_of(A), b = f_of(B), c = f_of(C);

    // An infinite operand multiplied by zero remains invalid despite quiet NaN addends.
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
    // Replace any arithmetic NaN result with the canonical encoding required by RISC-V.
    return isnan(r) ? CANONICAL_NAN : bits_of(r);
}
