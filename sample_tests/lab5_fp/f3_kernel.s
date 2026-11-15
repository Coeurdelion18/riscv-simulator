# Floating point variation 3 of 3: an applied kernel.
#
# Three dependence-heavy FP computations, with no padding anywhere, so the
# difference between --hazard forward and --hazard stall is the largest in
# the whole course:
#
#   1. a 3x3 matrix times a 3-vector, with fmadd.s in the inner loop. Each
#      flw feeds an fmadd one instruction later, so every iteration pays a
#      load-use stall even with full forwarding.
#   2. the length of the result vector: three fmul/fadd then fsqrt, a
#      chain in which every instruction consumes the previous result.
#   3. Horner evaluation of 3x^3 - 2x^2 + x - 7 at x = 2.0, four fmadds
#      back to back on the same accumulator.
#
# Matrix [[1,2,3],[4,5,6],[7,8,10]] times [1,2,3] = [14, 32, 53]
# Length = sqrt(14^2 + 32^2 + 53^2) = sqrt(196+1024+2809) = sqrt(4029)
#        = 63.474... -> 63 truncated
# Horner at 2.0: 3*8 - 2*4 + 2 - 7 = 24 - 8 + 2 - 7 = 11
#
# Prints 14, 32, 53, then 63, then 11.
#
# Verified on the reference implementation:
#   forward  222 cycles, retired 197, stalls 9,   forwards 143, CPI 1.127
#   stall    449 cycles, retired 197, stalls 236, CPI 2.279
#   single-cycle 197 cycles. prints 1432536311 (14, 32, 53, 63, 11).
#   Forwarding removes 227 of 449 cycles -- 50.6%, the largest gap of any
#   program in this suite, because FP code is one long dependence chain.
#   Key results: s1=63 s2=64 s3=4029 s4=11 s5=0x41300000 (the bits of 11.0).
#
# RUN: forward --hazard forward
# RUN: stall --hazard stall
# RUN: single-cycle --single-cycle
# RARS-CHECK: fs0 fs1 fs2 fs3 fs4 s1 s2 s3 s4 s5

.text
.globl main
main:
    la   s0, mat
    la   s6, vec

    # ================ 1. matrix-vector product ================
    addi s7, zero, 0            # row index
    addi s8, zero, 3            # rows
mv_row:
    la   t0, zero_f
    flw  ft0, 0(t0)             # accumulator = 0.0
    addi t1, zero, 0            # column index
mv_col:
    slli t2, s7, 2              # row * 4
    slli t3, t2, 1
    add  t2, t2, t3             # row * 12  (three words per row)
    slli t3, t1, 2
    add  t2, t2, t3
    add  t2, s0, t2             # &mat[row][col]
    flw  ft1, 0(t2)
    slli t4, t1, 2
    add  t4, s6, t4             # &vec[col]
    flw  ft2, 0(t4)
    fmadd.s ft0, ft1, ft2, ft0  # acc += mat * vec  (load-use on ft2)
    addi t1, t1, 1
    blt  t1, s8, mv_col

    # store the row result and print its integer value
    slli t5, s7, 2
    la   t6, res
    add  t6, t6, t5
    fsw  ft0, 0(t6)
    fcvt.w.s a0, ft0, rtz
    addi a7, zero, 1
    ecall
    addi s7, s7, 1
    blt  s7, s8, mv_row

    # ================ 2. length of the result vector ================
    la   t0, res
    flw  fs0, 0(t0)             # 14.0
    flw  fs1, 4(t0)             # 32.0
    flw  fs2, 8(t0)             # 53.0
    fmul.s ft3, fs0, fs0        # a dependence chain with no slack
    fmadd.s ft3, fs1, fs1, ft3
    fmadd.s ft3, fs2, fs2, ft3
    fsqrt.s fs3, ft3            # sqrt(4029) = 63.474...
    fcvt.w.s a0, fs3, rtz
    addi a7, zero, 1
    ecall                       # prints 63
    fcvt.w.s s1, fs3, rtz       # 63
    fcvt.w.s s2, fs3, rup       # 64
    fcvt.w.s s3, ft3, rtz       # 4029

    # ================ 3. Horner: 3x^3 - 2x^2 + x - 7 at x = 2 ========
    la   t0, horner
    flw  ft4, 0(t0)             # x = 2.0
    flw  ft5, 4(t0)             # 3.0
    flw  ft6, 8(t0)             # -2.0
    flw  ft7, 12(t0)            # 1.0
    flw  ft8, 16(t0)            # -7.0
    fmadd.s ft9, ft5, ft4, ft6  # 3x - 2        = 4
    fmadd.s ft9, ft9, ft4, ft7  # (3x-2)x + 1   = 9
    fmadd.s fs4, ft9, ft4, ft8  # ((3x-2)x+1)x - 7 = 11
    fcvt.w.s a0, fs4, rtz
    addi a7, zero, 1
    ecall                       # prints 11
    fcvt.w.s s4, fs4, rtz       # 11
    fmv.x.w  s5, fs4            # the bit pattern of 11.0 = 0x41300000

    addi a7, zero, 10
    ecall

.data
zero_f:
    .float 0.0
mat:
    .float 1.0
    .float 2.0
    .float 3.0
    .float 4.0
    .float 5.0
    .float 6.0
    .float 7.0
    .float 8.0
    .float 10.0
vec:
    .float 1.0
    .float 2.0
    .float 3.0
res:
    .word 0
    .word 0
    .word 0
horner:
    .float 2.0
    .float 3.0
    .float -2.0
    .float 1.0
    .float -7.0
