# Pipeline variation 3 of 3: an applied kernel, for the CPI measurement.
#
# Bubble-sorts eight words in memory, then walks the sorted array to
# produce its first element, its last element and its sum. This is the
# program to quote when comparing the pipeline with the single-cycle core:
# it is dominated by loads, stores and branches, and every dependence has
# to be padded by hand, so the nops are retired instructions that are paid
# for in full.
#
# Input:  5 3 8 1 9 2 7 4
# Prints 1 then 9 then 39 (the sorted first, last and sum).
#
# Verified on the reference implementation:
#   pipeline     762 cycles, retired 658, redirects 50, squashed 100, CPI 1.158
#   single-cycle 658 cycles. Registers identical. Prints 1939.
#
# RUN: pipeline
# RUN: single-cycle --single-cycle
# RARS-CHECK: s4 s5 s6

.text
.globl main
main:
    la   s0, arr
    addi s1, zero, 8            # element count
    nop
    nop

    # ---- bubble sort: outer counter i from n-1 down to 1 ----
    addi s2, s1, -1             # i = n - 1
    nop
    nop
outer:
    beq  s2, zero, sorted
    addi t0, zero, 0            # j = 0
    nop
    nop
inner:
    slli t1, t0, 2              # j * 4
    nop
    nop
    add  t2, s0, t1             # &arr[j]
    nop
    nop
    lw   t3, 0(t2)              # arr[j]
    lw   t4, 4(t2)              # arr[j+1]
    nop
    nop
    bge  t4, t3, no_swap        # already in order?
    sw   t4, 0(t2)              # swap
    sw   t3, 4(t2)
no_swap:
    addi t0, t0, 1
    nop
    nop
    blt  t0, s2, inner
    addi s2, s2, -1
    nop
    nop
    j    outer

sorted:
    # ---- first, last and sum of the sorted array ----
    lw   s4, 0(s0)              # 1
    addi t5, s1, -1
    nop
    nop
    nop
    slli t5, t5, 2
    nop
    nop
    add  t6, s0, t5
    nop
    nop
    lw   s5, 0(t6)              # 9

    addi s6, zero, 0            # sum
    addi t0, zero, 0
    nop
    nop
sum_loop:
    slli t1, t0, 2
    nop
    nop
    add  t2, s0, t1
    nop
    nop
    lw   t3, 0(t2)
    nop
    nop
    add  s6, s6, t3
    addi t0, t0, 1
    nop
    nop
    blt  t0, s1, sum_loop

    # ---- print the three results ----
    addi a7, zero, 1
    add  a0, s4, zero
    nop
    nop
    ecall
    add  a0, s5, zero
    nop
    nop
    ecall
    add  a0, s6, zero
    nop
    nop
    ecall

    addi a7, zero, 10
    nop
    nop
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
