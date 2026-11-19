# Floating point variation 1 of 3: broad coverage.
#
# One pass over every instruction group: flw/fsw, the five arithmetic
# operations, all four fused multiply-adds, min/max, the three sign
# injections, the three comparisons, both conversion directions, the two
# raw moves and fclass. Ordinary finite values only -- special values and
# flags are variation 2.
#
# Values: a = 2.5, b = -4.0, c = 0.5
#
# Verified on the reference implementation:
#   forward  46 cycles, retired 42, stalls 0, forwards 10, CPI 1.095
#   stall    61 cycles, retired 42, stalls 15, CPI 1.452
#   single-cycle 42 cycles. prints 11. fcsr flags: NX.
#   All three runs agree on every x register, every f register and fcsr.
#
# RUN: forward --hazard forward
# RUN: stall --hazard stall
# RUN: single-cycle --single-cycle
# RARS-CHECK: fs0 fs1 fs2 fs3 fs4 fs5 fs6 fs7 fs8 fs9 fs10 fs11 fa0 fa1 fa2 fa3 a1 a2 a3 a4 a6 s1 s2
#
# KNOWN RARS 1.6 DEVIATION, so a5 is not compared with RARS (the golden
# file still checks it): a5 = fcvt.w.s of 2.5 under the default rne mode.
# Round-to-nearest-EVEN gives 2; RARS 1.6 returns 3.

.text
.globl main
main:
    la   gp, data

    # ---- loads ----
    flw  ft0, 0(gp)             # 2.5
    flw  ft1, 4(gp)             # -4.0
    flw  ft2, 8(gp)             # 0.5

    # ---- the five arithmetic operations ----
    fadd.s fs0, ft0, ft1        # -1.5
    fsub.s fs1, ft0, ft1        #  6.5
    fmul.s fs2, ft0, ft1        # -10.0
    fdiv.s fs3, ft0, ft2        #  5.0
    fsqrt.s fs4, ft0            #  1.5811388

    # ---- the four fused multiply-adds, one rounding each ----
    fmadd.s  fs5, ft0, ft1, ft2 #  2.5 * -4.0 + 0.5  = -9.5
    fmsub.s  fs6, ft0, ft1, ft2 #  2.5 * -4.0 - 0.5  = -10.5
    fnmsub.s fs7, ft0, ft1, ft2 # -(2.5 * -4.0) + 0.5 = 10.5
    fnmadd.s fs8, ft0, ft1, ft2 # -(2.5 * -4.0) - 0.5 =  9.5

    # ---- min and max ----
    fmin.s fs9, ft0, ft1        # -4.0
    fmax.s fs10, ft0, ft1       #  2.5

    # ---- sign injection, and the three pseudo-ops built on it ----
    fsgnj.s  fs11, ft0, ft1     # magnitude 2.5, sign of -4.0 -> -2.5
    fsgnjn.s fa0, ft0, ft1      # sign negated              ->  2.5
    fsgnjx.s fa1, ft0, ft1      # signs XOR-ed              -> -2.5
    fabs.s   fa2, ft1           #  4.0
    fneg.s   fa3, ft0           # -2.5

    # ---- comparisons: results go to x registers ----
    feq.s a1, ft0, ft0          # 1
    flt.s a2, ft1, ft0          # 1   (-4.0 < 2.5)
    fle.s a3, ft0, ft1          # 0
    flt.s a4, ft0, ft0          # 0

    # ---- conversions, both directions ----
    fcvt.w.s  a5, ft0           # 2.5 -> 2   (round to nearest even)
    fcvt.w.s  a6, ft1           # -4.0 -> -4
    addi t0, zero, -7
    fcvt.s.w  ft3, t0           # -7.0
    fcvt.wu.s s1, ft2           # 0.5 -> 0

    # ---- raw moves between the register files ----
    fmv.x.w s2, ft0             # the bit pattern of 2.5 = 0x40200000
    fmv.w.x ft4, s2             # and back

    # ---- classify a normal and a negative normal ----
    fclass.s t1, ft0            # bit 6: +normal  -> 64
    fclass.s t2, ft1            # bit 1: -normal  -> 2

    # ---- store a result and read it back ----
    fsw  fs1, 12(gp)
    flw  ft5, 12(gp)            # 6.5

    # ---- print the integer part of fs1 + fs3 = 6.5 + 5.0 = 11.5 -> 11 ----
    fadd.s ft6, fs1, fs3
    fcvt.w.s a0, ft6, rtz
    addi a7, zero, 1
    ecall                       # prints 11

    addi a7, zero, 10
    ecall

.data
data:
    .float 2.5
    .float -4.0
    .float 0.5
    .word 0
