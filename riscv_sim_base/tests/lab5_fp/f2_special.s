# Floating point variation 2 of 3: special values, exception flags and
# rounding modes.
#
# Each case clears fflags with fsflags, performs one operation, and reads
# the flags back with frflags, so every flag is attributed to exactly one
# operation. The expected flag value is in the comment: NV=16 DZ=8 OF=4
# UF=2 NX=1.
#
# Verified on the reference implementation:
#   forward  71 cycles, retired 66, stalls 1, forwards 12, CPI 1.076
#   single-cycle 66 cycles. prints 12. fcsr flags at exit: NV NX.
#   Measured flag registers, all as specified in the comments:
#     s1=8 (DZ)  s2=16 (NV)  s3=5 (OF|NX)  s4=3 (UF|NX)  s5=16  s6=0
#     s7=16  s8=16  s9=0  s10=16  a2=0 (feq quiet)  a4=16 (flt signals)
#     fclass: t3=0x080 (+inf)  t4=0x200 (qNaN)  t5=0x100 (sNaN)  a5=0x010 (+0)
#     rounding of 2.5: rne=2 rtz=2 rdn=2 rup=3 rmm=3
#
# RUN: forward --hazard forward
# RUN: single-cycle --single-cycle
# RARS-CHECK: s1 s2 s3 s4 s5 s6 s8 s9 s10 a1 a2 a3 a4 a5 a6 t1 t2 t3 t4 t5 t6
#
# KNOWN RARS 1.6 DEVIATIONS, so s7 and s11 are not compared with RARS
# (the golden files still check them):
#   s7  = the flags from sNaN + 1.0. IEEE 754 and the RISC-V F chapter
#         require NV for a signalling-NaN operand; RARS returns the sNaN
#         unchanged and raises nothing.
#   s11 = fcvt.w.s of 2.5 with rne. Ties go to EVEN, so 2; RARS gives 3.

.text
.globl main
main:
    la   gp, data
    flw  ft0, 0(gp)             # 1.0
    flw  ft1, 4(gp)             # 0.0
    flw  ft2, 8(gp)             # FLT_MAX   0x7f7fffff
    flw  ft3, 12(gp)            # FLT_MIN   0x00800000 (smallest normal)
    flw  ft4, 16(gp)            # +inf      0x7f800000
    flw  ft5, 20(gp)            # qNaN      0x7fc00000
    flw  ft6, 24(gp)            # sNaN      0x7fa00000
    flw  ft7, 28(gp)            # -1.0

    # ---- DZ: a finite non-zero divided by zero ----
    fsflags zero, zero
    fdiv.s fs0, ft0, ft1        # 1.0 / 0.0 -> +inf
    frflags s1                  # expect DZ = 8

    # ---- NV: zero divided by zero ----
    fsflags zero, zero
    fdiv.s fs1, ft1, ft1        # 0.0 / 0.0 -> canonical NaN
    frflags s2                  # expect NV = 16

    # ---- OF and NX: overflow to infinity ----
    fsflags zero, zero
    fadd.s fs2, ft2, ft2        # FLT_MAX + FLT_MAX -> +inf
    frflags s3                  # expect OF|NX = 5

    # ---- UF and NX: underflow to zero ----
    fsflags zero, zero
    fmul.s fs3, ft3, ft3        # FLT_MIN * FLT_MIN -> +0
    frflags s4                  # expect UF|NX = 3

    # ---- NV: infinity minus infinity ----
    fsflags zero, zero
    fsub.s fs4, ft4, ft4        # inf - inf -> canonical NaN
    frflags s5                  # expect NV = 16

    # ---- a quiet NaN propagates quietly ----
    fsflags zero, zero
    fadd.s fs5, ft5, ft0        # qNaN + 1.0 -> canonical NaN
    frflags s6                  # expect 0

    # ---- a signalling NaN does not ----
    fsflags zero, zero
    fadd.s fs6, ft6, ft0        # sNaN + 1.0 -> canonical NaN
    frflags s7                  # expect NV = 16

    # ---- NV: square root of a negative number ----
    fsflags zero, zero
    fsqrt.s fs7, ft7            # sqrt(-1.0) -> canonical NaN
    frflags s8                  # expect NV = 16

    # ---- min/max ignore a quiet NaN operand ----
    fsflags zero, zero
    fmin.s fs8, ft5, ft0        # min(qNaN, 1.0) -> 1.0
    frflags s9                  # expect 0
    fsflags zero, zero
    fmax.s fs9, ft6, ft0        # max(sNaN, 1.0) -> 1.0, but NV raised
    frflags s10                 # expect NV = 16

    # ---- comparisons: feq is quiet, flt signals ----
    fsflags zero, zero
    feq.s a1, ft5, ft5          # 0: a NaN is not equal to itself
    frflags a2                  # expect 0 (quiet compare)
    fsflags zero, zero
    flt.s a3, ft5, ft0          # 0
    frflags a4                  # expect NV = 16 (signalling compare)

    # ---- fclass on four special values ----
    fclass.s t3, ft4            # +inf      -> bit 7  = 128
    fclass.s t4, ft5            # qNaN      -> bit 9  = 512
    fclass.s t5, ft6            # sNaN      -> bit 8  = 256
    fclass.s a5, ft1            # +0        -> bit 4  = 16

    # ---- the five rounding modes on the same value ----
    la   t0, half
    flw  ft8, 0(t0)             # 2.5
    fcvt.w.s s11, ft8, rne      # 2  (ties to even)
    fcvt.w.s a6, ft8, rtz       # 2
    fcvt.w.s t6, ft8, rdn       # 2
    fcvt.w.s t1, ft8, rup       # 3
    fcvt.w.s t2, ft8, rmm       # 3  (ties away from zero)

    # ---- print the sum of the five rounded values: 2+2+2+3+3 = 12 ----
    add  a0, s11, a6
    add  a0, a0, t6
    add  a0, a0, t1
    add  a0, a0, t2
    addi a7, zero, 1
    ecall                       # prints 12

    addi a7, zero, 10
    ecall

.data
data:
    .float 1.0
    .float 0.0
    .word 0x7f7fffff            # FLT_MAX
    .word 0x00800000            # FLT_MIN, smallest normal
    .word 0x7f800000            # +inf
    .word 0x7fc00000            # canonical quiet NaN
    .word 0x7fa00000            # a signalling NaN
    .float -1.0
half:
    .float 2.5
