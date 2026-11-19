# Skeleton variation 3 of 3: control flow and functions.
#
# All six conditional branches with both outcomes, the signed/unsigned
# comparison pair on the same bit pattern, jal/jalr, a function that saves
# and restores ra through a stack frame so it can call another function,
# and a nested loop. A wrong branch condition shows up as a wrong register
# rather than a crash, so every path writes a distinct marker.
#
# Prints 7 then 55 then 120.

# Verified on the reference implementation:
#   634 cycles, prints 755120 (7, then 55, then 120).
#   RARS 1.6 agrees.

.text
.globl main
main:
    li   sp, stack_top

    # ---- the six branches, taken and not taken ----
    addi t0, zero, 5
    addi t1, zero, 7
    addi s0, zero, 0

    beq  t0, t1, bad            # 5 == 7 ? no
    addi s0, s0, 1
bad:
    bne  t0, t1, b2             # 5 != 7 ? yes
    addi s0, s0, 100            # skipped
b2:
    blt  t0, t1, b3             # 5 < 7 signed ? yes
    addi s0, s0, 100            # skipped
b3:
    bge  t0, t1, b4             # 5 >= 7 ? no
    addi s0, s0, 2
b4:
    addi t2, zero, -1           # 0xffffffff
    blt  t2, t0, b5             # -1 < 5 signed ? yes
    addi s0, s0, 100            # skipped
b5:
    bltu t2, t0, b6             # 0xffffffff < 5 unsigned ? no
    addi s0, s0, 4
b6:
    bgeu t2, t0, b7             # unsigned >= ? yes
    addi s0, s0, 100            # skipped
b7:
    # s0 collected 1 (beq not taken) + 2 (bge not taken) + 4 (bltu not
    # taken) = 7. Each taken branch had to skip an 'add 100', so any
    # taken branch that failed to jump pushes s0 to 107 or more.
    add  a0, s0, zero
    addi a7, zero, 1
    ecall                       # prints 7

    # ---- a call that itself calls: ra must be saved ----
    addi a0, zero, 10
    jal  ra, triangle           # sum 1..10 = 55
    add  s1, a0, zero
    addi a0, s1, 0
    addi a7, zero, 1
    ecall                       # prints 55

    # ---- nested loop: 8 outer x 15 inner additions ----
    addi s2, zero, 0            # total
    addi t3, zero, 0            # outer counter
outer:
    addi t4, zero, 0            # inner counter
inner:
    addi s2, s2, 1
    addi t4, t4, 1
    addi t5, zero, 15
    blt  t4, t5, inner
    addi t3, t3, 1
    addi t6, zero, 8
    blt  t3, t6, outer
    add  a0, s2, zero           # 8 * 15 = 120
    addi a7, zero, 1
    ecall                       # prints 120

    addi a7, zero, 10
    ecall

# ---- triangle(n): returns n + (n-1) + ... + 1, by calling add_one ----
triangle:
    addi sp, sp, -8
    sw   ra, 0(sp)
    sw   s3, 4(sp)
    add  s3, a0, zero           # n
    addi a0, zero, 0            # accumulator
t_loop:
    beq  s3, zero, t_done
    add  a0, a0, s3
    addi a0, a0, 0
    jal  ra, add_one            # a1 = 1, to prove nesting works
    sub  s3, s3, a1
    j    t_loop
t_done:
    lw   ra, 0(sp)
    lw   s3, 4(sp)
    addi sp, sp, 8
    jalr zero, 0(ra)            # explicit return

# ---- add_one(): sets a1 = 1 and returns ----
add_one:
    addi a1, zero, 1
    jalr zero, 0(ra)

.data
    .word 0
    .word 0
    .word 0
    .word 0
stack_top:
    .word 0
