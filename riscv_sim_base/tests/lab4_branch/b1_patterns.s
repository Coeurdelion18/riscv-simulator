# Branch prediction variation 1 of 3: a catalogue of branch patterns.
#
# Twelve independent pattern blocks, each a loop or a forward branch
# with a stated outcome sequence. Between them they cover short loops
# (where a dynamic predictor never gets to warm up), long loops (where
# it does), forward branches that are always taken, forward branches
# that are never taken, and a run of alternating outcomes.
#
# Every block adds to the checksum in s0, so the printed value is the
# same under every predictor: prediction changes the cycle count, never
# the result. That is the first thing to check.
#
# Verified on the reference implementation:
#   372 lines. retired 1330, branches 328, single-cycle 1330 cycles.
#   prints 703233 under every predictor, and all four give identical registers.
#   not-taken  1856 cycles, 261 mispredicts, 20.4% accuracy, CPI 1.395
#   btfn       1432 cycles,  49 mispredicts, 85.1% accuracy, CPI 1.077
#   1bit       1506 cycles,  86 mispredicts, 73.8% accuracy, CPI 1.132
#   2bit       1476 cycles,  71 mispredicts, 78.4% accuracy, CPI 1.110
#
# RUN: not-taken --predictor not-taken
# RUN: btfn --predictor btfn
# RUN: 1bit --predictor 1bit
# RUN: 2bit --predictor 2bit
# RUN: single-cycle --single-cycle
# RARS-CHECK: s0 s1 s2

.text
.globl main
main:
    addi s0, zero, 0            # checksum
    addi s1, zero, 0            # count of loop iterations executed
    addi s2, zero, 0            # count of forward branches resolved

# ---- block 1: backward loop, 2 iterations. Outcome sequence:
#      T N
    addi t0, zero, 0
    addi t1, zero, 2
lp0:
    addi s0, s0, 3
    addi s1, s1, 1
    addi t0, t0, 1
    blt  t0, t1, lp0

# ---- block 2: backward loop, 3 iterations. Outcome sequence:
#      T T N
    addi t0, zero, 0
    addi t1, zero, 3
lp1:
    addi s0, s0, 3
    addi s1, s1, 1
    addi t0, t0, 1
    blt  t0, t1, lp1

# ---- block 3: backward loop, 4 iterations. Outcome sequence:
#      T T T N
    addi t0, zero, 0
    addi t1, zero, 4
lp2:
    addi s0, s0, 3
    addi s1, s1, 1
    addi t0, t0, 1
    blt  t0, t1, lp2

# ---- block 4: backward loop, 5 iterations. Outcome sequence:
#      T T T T N
    addi t0, zero, 0
    addi t1, zero, 5
lp3:
    addi s0, s0, 3
    addi s1, s1, 1
    addi t0, t0, 1
    blt  t0, t1, lp3

# ---- block 5: backward loop, 6 iterations. Outcome sequence:
#      T T T T T N
    addi t0, zero, 0
    addi t1, zero, 6
lp4:
    addi s0, s0, 3
    addi s1, s1, 1
    addi t0, t0, 1
    blt  t0, t1, lp4

# ---- block 6: backward loop, 7 iterations. Outcome sequence:
#      T T T T T T N
    addi t0, zero, 0
    addi t1, zero, 7
lp5:
    addi s0, s0, 3
    addi s1, s1, 1
    addi t0, t0, 1
    blt  t0, t1, lp5

# ---- block 7: backward loop, 8 iterations. Outcome sequence:
#      T T T T T T T N
    addi t0, zero, 0
    addi t1, zero, 8
lp6:
    addi s0, s0, 3
    addi s1, s1, 1
    addi t0, t0, 1
    blt  t0, t1, lp6

# ---- block 8: backward loop, 10 iterations. Outcome sequence:
#      T T T T T T T T T N
    addi t0, zero, 0
    addi t1, zero, 10
lp7:
    addi s0, s0, 3
    addi s1, s1, 1
    addi t0, t0, 1
    blt  t0, t1, lp7

# ---- block 9: backward loop, 12 iterations. Outcome sequence:
#      T T T T T T T T T T T N
    addi t0, zero, 0
    addi t1, zero, 12
lp8:
    addi s0, s0, 3
    addi s1, s1, 1
    addi t0, t0, 1
    blt  t0, t1, lp8

# ---- block 10: backward loop, 16 iterations. Outcome sequence:
#      T T T T T T T T T T T T T T T N
    addi t0, zero, 0
    addi t1, zero, 16
lp9:
    addi s0, s0, 3
    addi s1, s1, 1
    addi t0, t0, 1
    blt  t0, t1, lp9

# ---- block 11: backward loop, 20 iterations. Outcome sequence:
#      T T T T T T T T T T T T T T T T T T T N
    addi t0, zero, 0
    addi t1, zero, 20
lp10:
    addi s0, s0, 3
    addi s1, s1, 1
    addi t0, t0, 1
    blt  t0, t1, lp10

# ---- block 12: backward loop, 24 iterations. Outcome sequence:
#      T T T T T T T T T T T T T T T T T T T T T T T N
    addi t0, zero, 0
    addi t1, zero, 24
lp11:
    addi s0, s0, 3
    addi s1, s1, 1
    addi t0, t0, 1
    blt  t0, t1, lp11

# ---- block 9: a forward branch taken on all six executions.
#      Outcome sequence: T T T T T T ----
#      T T T T T T
    addi t2, zero, 0
    addi t3, zero, 6
fwd_t:
    addi t4, zero, 1
    beq  t4, t4, skip_t         # always equal: always taken
    addi s0, s0, 1000           # must never run
skip_t:
    addi s0, s0, 5
    addi s2, s2, 1
    addi t2, t2, 1
    blt  t2, t3, fwd_t

# ---- block 10: a forward branch not taken on all six executions.
#      Outcome sequence: N N N N N N ----
#      N N N N N N
    addi t2, zero, 0
    addi t3, zero, 6
fwd_n:
    addi t4, zero, 1
    addi t5, zero, 2
    beq  t4, t5, skip_n         # never equal: never taken
    addi s0, s0, 7              # always runs
skip_n:
    addi s2, s2, 1
    addi t2, t2, 1
    blt  t2, t3, fwd_n

# ---- block 11: a forward branch driven by the counter's parity.
#      Outcome sequence: T N T N T N T N -- 8 executions, 4 taken ----
    addi t2, zero, 0
    addi t3, zero, 8
alt:
    andi t4, t2, 1              # 0, 1, 0, 1, ...
    beq  t4, zero, alt_even
    addi s0, s0, 11             # odd iterations
    j    alt_next
alt_even:
    addi s0, s0, 13             # even iterations
alt_next:
    addi s2, s2, 1
    addi t2, t2, 1
    blt  t2, t3, alt

# ---- block 12: a 30-iteration loop. Outcome sequence: T x 29, then N ----
    addi t2, zero, 0
    addi t3, zero, 30
long_lp:
    addi s0, s0, 2
    addi s1, s1, 1
    addi t2, t2, 1
    blt  t2, t3, long_lp

# ---- block 13: nested loop, outer 4 x inner 3. The inner branch's
#      outcome sequence is (T T N) repeated four times: 12 executions,
#      8 of them taken, with a re-entry after every exit ----
    addi t2, zero, 0
    addi t3, zero, 4
nest_out:
    addi t4, zero, 0
    addi t5, zero, 3
nest_in:
    addi s0, s0, 1
    addi s1, s1, 1
    addi t4, t4, 1
    blt  t4, t5, nest_in
    addi t2, t2, 1
    blt  t2, t3, nest_out

# ---- block 14: a branch with period 4. Outcome sequence:
#      (N N N T) x 5  -- 20 executions, 5 of them taken ----
    addi t2, zero, 0
    addi t3, zero, 20
per4:
    andi t4, t2, 3              # 0,1,2,3 repeating
    addi t5, zero, 3
    beq  t4, t5, per4_skip      # taken once every four
    addi s0, s0, 2
per4_skip:
    addi s2, s2, 1
    addi t2, t2, 1
    blt  t2, t3, per4

# ---- block 15: nested 3 x 10. The inner branch's outcome sequence is
#      (T T T T T T T T T N) x 3 -- 30 executions, 27 of them taken ----
    addi t2, zero, 0
    addi t3, zero, 3
mostly_out:
    addi t4, zero, 0
    addi t5, zero, 10
mostly_in:
    addi s0, s0, 1
    addi s1, s1, 1
    addi t4, t4, 1
    blt  t4, t5, mostly_in
    addi t2, t2, 1
    blt  t2, t3, mostly_out

# ---- block 16: eight distinct forward branches, each executed once.
#      Outcome sequence: N, eight times, on eight different PCs ----
    addi t4, zero, 1
    addi t5, zero, 1
    bne  t4, t5, cold0         # never taken (equal)
    addi s0, s0, 1
cold0:
    addi s2, s2, 1
    addi t4, zero, 2
    addi t5, zero, 2
    bne  t4, t5, cold1         # never taken (equal)
    addi s0, s0, 1
cold1:
    addi s2, s2, 1
    addi t4, zero, 3
    addi t5, zero, 3
    bne  t4, t5, cold2         # never taken (equal)
    addi s0, s0, 1
cold2:
    addi s2, s2, 1
    addi t4, zero, 4
    addi t5, zero, 4
    bne  t4, t5, cold3         # never taken (equal)
    addi s0, s0, 1
cold3:
    addi s2, s2, 1
    addi t4, zero, 5
    addi t5, zero, 5
    bne  t4, t5, cold4         # never taken (equal)
    addi s0, s0, 1
cold4:
    addi s2, s2, 1
    addi t4, zero, 6
    addi t5, zero, 6
    bne  t4, t5, cold5         # never taken (equal)
    addi s0, s0, 1
cold5:
    addi s2, s2, 1
    addi t4, zero, 7
    addi t5, zero, 7
    bne  t4, t5, cold6         # never taken (equal)
    addi s0, s0, 1
cold6:
    addi s2, s2, 1
    addi t4, zero, 8
    addi t5, zero, 8
    bne  t4, t5, cold7         # never taken (equal)
    addi s0, s0, 1
cold7:
    addi s2, s2, 1

# ---- block 17: eight more loops, trip counts 9 down to 2, on eight
#      distinct branch PCs ----
    addi t2, zero, 0
    addi t3, zero, 9
desc0:
    addi s0, s0, 1
    addi s1, s1, 1
    addi t2, t2, 1
    blt  t2, t3, desc0
    addi t2, zero, 0
    addi t3, zero, 8
desc1:
    addi s0, s0, 1
    addi s1, s1, 1
    addi t2, t2, 1
    blt  t2, t3, desc1
    addi t2, zero, 0
    addi t3, zero, 7
desc2:
    addi s0, s0, 1
    addi s1, s1, 1
    addi t2, t2, 1
    blt  t2, t3, desc2
    addi t2, zero, 0
    addi t3, zero, 6
desc3:
    addi s0, s0, 1
    addi s1, s1, 1
    addi t2, t2, 1
    blt  t2, t3, desc3
    addi t2, zero, 0
    addi t3, zero, 5
desc4:
    addi s0, s0, 1
    addi s1, s1, 1
    addi t2, t2, 1
    blt  t2, t3, desc4
    addi t2, zero, 0
    addi t3, zero, 4
desc5:
    addi s0, s0, 1
    addi s1, s1, 1
    addi t2, t2, 1
    blt  t2, t3, desc5
    addi t2, zero, 0
    addi t3, zero, 3
desc6:
    addi s0, s0, 1
    addi s1, s1, 1
    addi t2, t2, 1
    blt  t2, t3, desc6
    addi t2, zero, 0
    addi t3, zero, 2
desc7:
    addi s0, s0, 1
    addi s1, s1, 1
    addi t2, t2, 1
    blt  t2, t3, desc7

    addi a7, zero, 1
    add  a0, s0, zero
    ecall                       # print the checksum
    addi a7, zero, 1
    add  a0, s1, zero
    ecall                       # print the iteration count
    addi a7, zero, 10
    ecall
