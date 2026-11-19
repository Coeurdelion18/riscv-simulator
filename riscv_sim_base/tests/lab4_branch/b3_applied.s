# Branch prediction variation 3 of 3: an applied workload.
#
# Four kernels whose branches are data-dependent rather than patterned,
# which is what ordinary code looks like:
#
#   1. insertion sort of 16 words -- the inner 'shift while greater'
#      branch depends on the data, so its outcome sequence is not
#      periodic and no predictor can learn it exactly;
#   2. binary search for eight different keys in the sorted array --
#      three or four data-dependent branches per lookup;
#   3. recursive Fibonacci of 10 -- 177 calls and 177 returns, so this
#      section is dominated by jalr, which is never predicted and
#      always costs two cycles;
#   4. greatest common divisor by repeated subtraction, six pairs.
#
# Prints four checksums: the sorted sum, the sum of the eight search
# results, fib(10), and the sum of the six GCDs.
#
# Verified on the reference implementation:
#   322 lines. retired 3195, branches 510, jal 336, jalr 191, stalls 115.
#   single-cycle 3195 cycles. prints 697645586 (697, 64, 55, 86) under every
#   predictor, and all four give identical registers.
#   not-taken  3986 cycles, 145 mispredicts, 71.6% accuracy, CPI 1.248
#   btfn       3986 cycles, 145 mispredicts, 71.6% accuracy, CPI 1.248
#   1bit       3970 cycles, 137 mispredicts, 73.1% accuracy, CPI 1.243
#   2bit       3982 cycles, 143 mispredicts, 72.0% accuracy, CPI 1.246
#   
#   The predictors barely differ here because returns dominate: of 336
#   redirects, 191 are jalr, which this design never predicts. That is the
#   measurement that motivates a return-address stack.
#
# RUN: not-taken --predictor not-taken
# RUN: btfn --predictor btfn
# RUN: 1bit --predictor 1bit
# RUN: 2bit --predictor 2bit
# RUN: single-cycle --single-cycle
# RARS-CHECK: s4 s5 s6 s7

.text
.globl main
main:
    la   sp, stack_top
    la   s0, arr
    addi s1, zero, 16           # element count
    addi s4, zero, 0            # sorted sum
    addi s5, zero, 0            # search result sum
    addi s6, zero, 0            # fib result
    addi s7, zero, 0            # gcd sum

# ================ 1. insertion sort ================================
    addi t0, zero, 1            # i = 1
ins_outer:
    bge  t0, s1, ins_done
    slli t1, t0, 2
    add  t1, s0, t1
    lw   t2, 0(t1)              # key = arr[i]
    addi t3, t0, -1             # j = i - 1
ins_inner:
    blt  t3, zero, ins_place
    slli t4, t3, 2
    add  t4, s0, t4
    lw   t5, 0(t4)              # arr[j]
    bge  t2, t5, ins_place      # data-dependent: key >= arr[j] ?
    sw   t5, 4(t4)              # shift right
    addi t3, t3, -1
    j    ins_inner
ins_place:
    addi t3, t3, 1
    slli t4, t3, 2
    add  t4, s0, t4
    sw   t2, 0(t4)
    addi t0, t0, 1
    j    ins_outer
ins_done:

# ---- sum the sorted array ----
    addi t0, zero, 0
sum_loop:
    bge  t0, s1, sum_done
    slli t1, t0, 2
    add  t1, s0, t1
    lw   t2, 0(t1)
    add  s4, s4, t2
    addi t0, t0, 1
    j    sum_loop
sum_done:
    addi a7, zero, 1
    add  a0, s4, zero
    ecall                       # print the sorted sum

# ================ 2. binary search, eight keys =====================
    # ---- look up 3 ----
    addi a0, zero, 3
    jal  ra, bsearch
    add  s5, s5, a0             # -1 if absent, the index if present
    # ---- look up 11 ----
    addi a0, zero, 11
    jal  ra, bsearch
    add  s5, s5, a0             # -1 if absent, the index if present
    # ---- look up 25 ----
    addi a0, zero, 25
    jal  ra, bsearch
    add  s5, s5, a0             # -1 if absent, the index if present
    # ---- look up 40 ----
    addi a0, zero, 40
    jal  ra, bsearch
    add  s5, s5, a0             # -1 if absent, the index if present
    # ---- look up 57 ----
    addi a0, zero, 57
    jal  ra, bsearch
    add  s5, s5, a0             # -1 if absent, the index if present
    # ---- look up 64 ----
    addi a0, zero, 64
    jal  ra, bsearch
    add  s5, s5, a0             # -1 if absent, the index if present
    # ---- look up 72 ----
    addi a0, zero, 72
    jal  ra, bsearch
    add  s5, s5, a0             # -1 if absent, the index if present
    # ---- look up 99 ----
    addi a0, zero, 99
    jal  ra, bsearch
    add  s5, s5, a0             # -1 if absent, the index if present
    addi a7, zero, 1
    add  a0, s5, zero
    ecall                       # print the sum of the eight results

# ================ 3. recursive Fibonacci ===========================
    addi a0, zero, 10
    jal  ra, fib
    add  s6, a0, zero
    addi a7, zero, 1
    add  a0, s6, zero
    ecall                       # print fib(10) = 55

# ================ 4. greatest common divisor, six pairs ============
    # ---- gcd(48, 18) ----
    addi a0, zero, 48
    addi a1, zero, 18
    jal  ra, gcd
    add  s7, s7, a0
    # ---- gcd(100, 75) ----
    addi a0, zero, 100
    addi a1, zero, 75
    jal  ra, gcd
    add  s7, s7, a0
    # ---- gcd(81, 27) ----
    addi a0, zero, 81
    addi a1, zero, 27
    jal  ra, gcd
    add  s7, s7, a0
    # ---- gcd(270, 192) ----
    addi a0, zero, 270
    addi a1, zero, 192
    jal  ra, gcd
    add  s7, s7, a0
    # ---- gcd(17, 5) ----
    addi a0, zero, 17
    addi a1, zero, 5
    jal  ra, gcd
    add  s7, s7, a0
    # ---- gcd(1071, 462) ----
    addi a0, zero, 1071
    addi a1, zero, 462
    jal  ra, gcd
    add  s7, s7, a0
    addi a7, zero, 1
    add  a0, s7, zero
    ecall                       # print the sum of the six GCDs

    addi a7, zero, 10
    ecall

# ---- bsearch(key in a0) -> index, or -1 ----
bsearch:
    la   t0, arr
    addi t1, zero, 0            # lo
    addi t2, zero, 15           # hi
bs_loop:
    blt  t2, t1, bs_absent
    add  t3, t1, t2
    srli t3, t3, 1              # mid = (lo + hi) / 2
    slli t4, t3, 2
    add  t4, t0, t4
    lw   t5, 0(t4)
    beq  t5, a0, bs_found       # data-dependent
    blt  t5, a0, bs_right       # data-dependent
    addi t2, t3, -1
    j    bs_loop
bs_right:
    addi t1, t3, 1
    j    bs_loop
bs_found:
    add  a0, t3, zero
    ret
bs_absent:
    addi a0, zero, -1
    ret

# ---- fib(n in a0), recursive: 177 calls for n = 10 ----
fib:
    addi t0, zero, 2
    blt  a0, t0, fib_base       # n < 2 ?
    addi sp, sp, -12
    sw   ra, 0(sp)
    sw   s8, 4(sp)
    sw   s9, 8(sp)
    add  s8, a0, zero           # n
    addi a0, s8, -1
    jal  ra, fib                # fib(n-1)
    add  s9, a0, zero
    addi a0, s8, -2
    jal  ra, fib                # fib(n-2)
    add  a0, a0, s9
    lw   ra, 0(sp)
    lw   s8, 4(sp)
    lw   s9, 8(sp)
    addi sp, sp, 12
    ret
fib_base:
    ret                         # fib(0) = 0, fib(1) = 1

# ---- gcd(a0, a1) by repeated subtraction ----
gcd:
    beq  a1, zero, gcd_done
gcd_loop:
    beq  a0, a1, gcd_done
    blt  a0, a1, gcd_swap       # data-dependent
    sub  a0, a0, a1
    j    gcd_loop
gcd_swap:
    sub  a1, a1, a0
    j    gcd_loop
gcd_done:
    ret

.data
arr:
    .word 57
    .word 3
    .word 72
    .word 25
    .word 11
    .word 99
    .word 40
    .word 64
    .word 8
    .word 91
    .word 33
    .word 50
    .word 19
    .word 77
    .word 2
    .word 46
    .word 0
stack_space:
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
stack_top:
    .word 0
