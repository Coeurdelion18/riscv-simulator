# Data hazards variation 3 of 3: the same kernel, unpadded.
#
# Exactly the bubble sort from the pipeline lab's third variation, with
# every hand-inserted nop deleted. That is the comparison worth making:
# the padded version retires 658 instructions, this one retires far fewer
# and computes the same answer, because the hazard unit now does the
# waiting that the nops used to do.
#
# Run it under all three hazard units. Under 'none' the sort is wrong
# (its golden output records that on purpose); under 'stall' and
# 'forward' it must match the single-cycle oracle exactly.
#
# Input:  5 3 8 1 9 2 7 4
# Prints 1 then 9 then 39 (the sorted first, last and sum).
#
# Verified on the reference implementation:
#   forward  461 cycles, retired 321, stalls 36,  forwards 191, CPI 1.436
#   stall    749 cycles, retired 321, stalls 324, CPI 2.333
#   none      98 cycles, retired 75 -- WRONG: prints 510791112
#   single-cycle 321 cycles. forward and stall print 1939 and match the oracle.
#   The padded version of this kernel (pipeline lab, variation 3) retires 658
#   instructions in 762 cycles; dropping the nops halves the instruction count.
#
# RUN: forward --hazard forward
# RUN: stall --hazard stall
# RUN: none --hazard none
# RUN: single-cycle --single-cycle
# RARS-CHECK: s4 s5 s6
# RARS-RUNS: forward stall single-cycle

.text
.globl main
main:
    la   s0, arr
    addi s1, zero, 8            # element count

    # ---- bubble sort: outer counter i from n-1 down to 1 ----
    addi s2, s1, -1             # i = n - 1
outer:
    beq  s2, zero, sorted
    addi t0, zero, 0            # j = 0
inner:
    slli t1, t0, 2              # j * 4
    add  t2, s0, t1             # &arr[j]
    lw   t3, 0(t2)              # arr[j]
    lw   t4, 4(t2)              # arr[j+1]
    bge  t4, t3, no_swap        # already in order?
    sw   t4, 0(t2)              # swap
    sw   t3, 4(t2)
no_swap:
    addi t0, t0, 1
    blt  t0, s2, inner
    addi s2, s2, -1
    j    outer

sorted:
    # ---- first, last and sum of the sorted array ----
    lw   s4, 0(s0)              # 1
    addi t5, s1, -1
    slli t5, t5, 2
    add  t6, s0, t5
    lw   s5, 0(t6)              # 9

    addi s6, zero, 0            # sum
    addi t0, zero, 0
sum_loop:
    slli t1, t0, 2
    add  t2, s0, t1
    lw   t3, 0(t2)
    add  s6, s6, t3
    addi t0, t0, 1
    blt  t0, s1, sum_loop

    # ---- print the three results ----
    addi a7, zero, 1
    add  a0, s4, zero
    ecall
    add  a0, s5, zero
    ecall
    add  a0, s6, zero
    ecall

    addi a7, zero, 10
    ecall

.data
arr:
    .word 5
    .word 3
    .word 8
    .word 1
    .word 9
    .word 2
    .word 7
    .word 4
