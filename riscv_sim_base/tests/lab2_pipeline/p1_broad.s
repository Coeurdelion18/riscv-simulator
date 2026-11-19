# Pipeline variation 1 of 3: broad coverage.
#
# Exercises every part of the five-stage pipeline in one program: fill and
# drain timing, straight-line ALU work, all five load widths and three
# store widths through MEM, taken and not-taken branches, jal and jalr,
# and a loop. There is no hazard handling in this lab, so every consumer
# is placed at least three instructions after its producer.
#
# Verified on the reference implementation:
#   pipeline     113 cycles, retired 91, redirects 9, squashed 18, CPI 1.242
#   single-cycle  91 cycles. Registers identical. 91 + 4 + 2x9 = 113.
#
# RUN: pipeline
# RUN: single-cycle --single-cycle
# RARS-CHECK: s1 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11 a1 a2 a3
# (s0 holds a data address and is deliberately not compared with RARS:
#  RARS lays .data out at a different address than our flat memory.)

.text
.globl main
main:
    la   s0, buf
    addi s1, zero, 100
    addi s2, zero, -25
    nop
    nop
    nop

    # ---- ALU, every operand ready three instructions early ----
    add  s3, s1, s2             # 75
    sub  s4, s1, s2             # 125
    xor  s5, s1, s2             # 100 ^ -25
    nop
    slt  s6, s2, s1             # 1
    sltu s7, s2, s1             # 0  (-25 unsigned is huge)
    nop
    nop

    # ---- memory: word, halfword, byte, both extensions ----
    sw   s1, 0(s0)
    sh   s2, 4(s0)
    sb   s2, 8(s0)
    nop
    lw   s8, 0(s0)              # 100
    lh   s9, 4(s0)              # -25 sign-extended
    lhu  s10, 4(s0)             # 0x0000ffe7
    lbu  s11, 8(s0)             # 0x000000e7
    nop
    nop

    # ---- not-taken branch: costs nothing ----
    addi a1, zero, 0
    nop
    nop
    beq  s1, s2, wrong          # 100 == -25 ? no
    addi a1, a1, 1              # runs
    nop
    nop

    # ---- taken branch: two instructions squashed ----
    bne  s1, s2, cont
    addi a1, a1, 100            # squashed
    addi a1, a1, 100            # squashed
wrong:
    addi a1, a1, 100            # never reached
cont:
    addi a1, a1, 2              # a1 = 3
    nop
    nop

    # ---- a call and an explicit return ----
    addi a0, zero, 6
    nop
    nop
    jal  ra, double_it
    nop
    nop
    add  a2, a0, zero           # 12

    # ---- a counted loop. The limit is hoisted out of the body: inside
    # it, t1 would be only two instructions ahead of the branch. ----
    addi a3, zero, 0
    addi t0, zero, 0
    addi t1, zero, 7
    nop
    nop
loop:
    addi a3, a3, 3
    addi t0, t0, 1
    nop
    nop
    blt  t0, t1, loop           # seven iterations -> a3 = 21

    addi a7, zero, 10
    nop
    nop
    ecall

double_it:
    add  a0, a0, a0
    nop
    nop
    ret

.data
buf:
    .word 0
    .word 0
    .word 0
