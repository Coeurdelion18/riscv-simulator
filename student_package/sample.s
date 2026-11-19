# Sample test for evaluate.py 
#
# Exercises: data hazards (forwarding + load-use stall), a loop branch,
# jal/jalr (function call), and the M extension (mul, div, rem).
#
# The line below tells the evaluator which simulator options to use.
# RUN: --hazard forward --predictor 2bit

.data
arr:    .word 5, -3, 12, 7, 100

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

    addi a0, s2, 0
    addi a7, x0, 1          # print sum
    ecall
    addi a7, x0, 10         # exit
    ecall

square:
    mul  a0, a0, a0
    jalr x0, 0(ra)
