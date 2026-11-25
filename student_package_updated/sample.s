# Sample test for evaluate.py (Lab 5 level)
#
# Exercises: data hazards (forwarding + load-use stall), a loop branch,
# jal/jalr (function call), the M extension (mul, div, rem) and the
# F extension (flw/fsw, FP arithmetic, fused multiply-add, conversions,
# a canonical NaN, the fcsr exception flags and an FP load-use stall).
#
# The RUN line tells the evaluator which simulator options to use.
# The GRADE line tells it which registers are graded:
#   int      -> x1..x31 + pc                    (32 values)
#   int+fp   -> x1..x31 + pc + f0..f31 + fcsr   (65 values)
# RUN: --hazard forward --predictor 2bit
# GRADE: int+fp
# RARS-CHECK: s1 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11 a0 a1 a2 fs0 fs1 fs2 fs3 fs4 fs5 fs6 fs7 fs8 fcsr

.data
arr:    .word 5, -3, 12, 7, 100
fdata:  .float 1.5, -0.25
        .word 0

.text
.globl main
main:
    la   s0, arr            # s0 = &arr
    addi s1, x0, 5          # s1 = element count
    addi s2, x0, 0          # s2 = sum
    addi t0, x0, 0          # t0 = index
loop:
    slli t1, t0, 2
    add  t1, s0, t1
    lw   t2, 0(t1)          # load ...
    add  s2, s2, t2         # ... used immediately -> load-use stall
    addi t0, t0, 1
    blt  t0, s1, loop       # taken 4 times, falls through once

    mv   a0, s2             # a0 = 121
    jal  ra, square         # a0 = a0 * a0
    mv   s3, a0             # s3 = 14641

    addi t3, x0, 7
    div  s4, s3, t3         # 14641 / 7 = 2091
    rem  s5, s3, t3         # 14641 % 7 = 4
    addi t4, x0, -20
    div  s6, t4, t3         # -20 / 7 = -2 (rounds toward zero)
    rem  s7, t4, t3         # -20 % 7 = -6 (sign of dividend)
    div  s8, s3, x0         # divide by zero -> -1 (no trap)
    remu s9, s3, x0         # remu by zero -> dividend

    # ---------------- F extension
    la    t5, fdata
    flw   fs0, 0(t5)        # 1.5
    flw   fs1, 4(t5)        # -0.25
    fadd.s fs2, fs0, fs1    # 1.25        (FP load-use stall on fs1)
    fmul.s fs3, fs2, fs2    # 1.5625      (forwarded)
    fmadd.s fs4, fs0, fs2, fs1   # 1.5*1.25 + -0.25 = 1.625
    fsw   fs4, 8(t5)
    lw    s10, 8(t5)        # raw bits of 1.625 = 0x3fd00000
    fcvt.s.w fs5, s5        # 4.0  (int -> float)
    fdiv.s fs6, fs5, fs0    # 4 / 1.5 = 2.6666667 (inexact: NX)
    fcvt.w.s s11, fs6, rtz  # 2
    fsub.s fs7, fs6, fs6    # 0.0
    fdiv.s fs8, fs7, fs7    # 0/0 -> canonical NaN 0x7fc00000, NV
    frflags a1              # NV | NX = 0x11
    flt.s a2, fs1, fs0      # -0.25 < 1.5 -> 1

    addi a0, s2, 0
    addi a7, x0, 1          # print sum
    ecall
    addi a7, x0, 10         # exit
    ecall

square:
    mul  a0, a0, a0
    jalr x0, 0(ra)
