# Branch prediction variation 2 of 3: patterns built to defeat each
# predictor, and a table-aliasing experiment.
#
#   Section A  alternating branch, outcome sequence (T N) x 10.
#              One branch's own history cannot represent this, so both
#              dynamic predictors are wrong on nearly every execution
#              while btfn is right on half of them.
#   Section B  nested 6 x 3 loop. Inner outcome sequence (T T N) x 6:
#              an exit followed immediately by a re-entry, twelve
#              times, which is where hysteresis earns its keep.
#   Section C  TABLE ALIASING. Two branches inside one loop body, one
#              never taken and one always taken, placed exactly 256
#              bytes apart. The 64-entry table is indexed by pc[7:2],
#              so a separation of 256 bytes puts both branches in the
#              SAME entry and they train each other out of it.
#   Section D  the same two behaviours on adjacent branches, as a
#              reference point for Section C.
#
# Every section only adds to the checksum in s0, so the printed value
# is identical under every predictor.
#
# Verified on the reference implementation:
#   346 lines. retired 4739, branches 364, single-cycle 4739 cycles.
#   prints 4078 under every predictor; all four give identical registers.
#   not-taken  5287 cycles, 272 mispredicts, 25.3% accuracy, CPI 1.116
#   btfn       5007 cycles, 132 mispredicts, 63.7% accuracy, CPI 1.057
#   1bit       5033 cycles, 145 mispredicts, 60.2% accuracy, CPI 1.062
#   2bit       4985 cycles, 121 mispredicts, 66.8% accuracy, CPI 1.052
#   
#   The aliasing claim was checked against a control build in which the two
#   Section C branches sit 260 bytes apart (a different table entry) and the
#   Section E pair 516 bytes apart. Measured mispredicts, aliasing vs control:
#     not-taken 272 vs 272    btfn 132 vs 132   (no table: unaffected)
#     1bit      145 vs 108    2bit 121 vs 103   (the collision costs them)
#   Branch PCs in this build: Section C 0x6c and 0x16c (both table entry 27),
#   Section E 0x1b8 and 0x3b8 (both entry 46).
#
# RUN: not-taken --predictor not-taken
# RUN: btfn --predictor btfn
# RUN: 1bit --predictor 1bit
# RUN: 2bit --predictor 2bit
# RUN: single-cycle --single-cycle
# RARS-CHECK: s0 s1 s2 s3

.text
.globl main
main:
    addi s0, zero, 0            # checksum
    addi s1, zero, 0            # section A executions
    addi s2, zero, 0            # section C executions
    addi s3, zero, 0            # section D executions

# ================ Section A: alternating (T N) x 10 ================
    addi t0, zero, 0
    addi t1, zero, 20
secA:
    andi t2, t0, 1              # 0,1,0,1,...
    beq  t2, zero, a_even       # taken on even, not taken on odd
    addi s0, s0, 3
    j    a_next
a_even:
    addi s0, s0, 5
a_next:
    addi s1, s1, 1
    addi t0, t0, 1
    blt  t0, t1, secA

# ================ Section B: nested 6 x 3, inner (T T N) x 6 ========
    addi t0, zero, 0
    addi t1, zero, 6
secB_out:
    addi t2, zero, 0
    addi t3, zero, 3
secB_in:
    addi s0, s0, 1
    addi t2, t2, 1
    blt  t2, t3, secB_in
    addi t0, t0, 1
    blt  t0, t1, secB_out

# ================ Section C: the aliasing pair =====================
#   branch X (never taken) and branch Y (always taken) execute 20
#   times each, alternating in time: X Y X Y ...
    addi t0, zero, 0
    addi t1, zero, 20
secC:
    addi t4, zero, 1
    addi t5, zero, 2
    beq  t4, t5, c_x_skip       # branch X: 1 == 2 is false -> NEVER taken
    addi s0, s0, 1              # always runs
c_x_skip:
    # ---- 62 filler instructions, so branch Y lands 256 bytes after
    #      branch X. They do real work: each one bumps the checksum. ----
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    beq  t4, t4, c_y_skip       # branch Y: 1 == 1 -> ALWAYS taken
    addi s0, s0, 1000           # must never run
c_y_skip:
    addi s2, s2, 1
    addi t0, t0, 1
    blt  t0, t1, secC

# ================ Section D: the same two behaviours, adjacent ======
    addi t0, zero, 0
    addi t1, zero, 20
secD:
    addi t4, zero, 1
    addi t5, zero, 2
    beq  t4, t5, d_x_skip       # never taken
    addi s0, s0, 1
d_x_skip:
    beq  t4, t4, d_y_skip       # always taken
    addi s0, s0, 1000           # must never run
d_y_skip:
    addi s3, s3, 1
    addi t0, t0, 1
    blt  t0, t1, secD

# ================ Section E: aliasing with matching behaviour =======
#   Another pair, 512 bytes apart (also a multiple of 256, so also
#   the same entry), but this time BOTH branches are
#   always taken. They share one table entry and train it the same
#   way, so the collision costs nothing: aliasing only hurts when the
#   colliding branches disagree.
    addi t0, zero, 0
    addi t1, zero, 20
secE:
    addi t4, zero, 1
    beq  t4, t4, e_x_skip       # always taken
    addi s0, s0, 1000           # must never run
e_x_skip:
    # ---- 126 filler instructions, so branch Y2 lands 512 bytes after
    #      branch X2, in the same table entry ----
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    addi s0, s0, 1
    beq  t4, t4, e_y_skip       # also always taken
    addi s0, s0, 1000           # must never run
e_y_skip:
    addi t0, t0, 1
    blt  t0, t1, secE

# ================ Section F: alternating, 60 executions ============
#   Outcome sequence (T N) x 30. The longest run of the pattern no
#   predictor in this lab can learn.
    addi t0, zero, 0
    addi t1, zero, 60
secF:
    andi t2, t0, 1
    beq  t2, zero, f_even
    addi s0, s0, 2
    j    f_next
f_even:
    addi s0, s0, 4
f_next:
    addi t0, t0, 1
    blt  t0, t1, secF

    addi a7, zero, 1
    add  a0, s0, zero
    ecall                       # print the checksum
    addi a7, zero, 10
    ecall
