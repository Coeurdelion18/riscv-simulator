# Skeleton variation 2 of 3: memory.
#
# All five loads and all three stores, the sign- versus zero-extension
# pair at byte and halfword width, little-endian byte order observed by
# reading one word back as four bytes, and an array walk with a computed
# address. Byte order is the part students most often get wrong.
#
# Prints the sum of the array (36).

# Verified on the reference implementation:
#   87 cycles, prints 36. RARS 1.6 agrees on all result registers.

.text
.globl main
main:
    li   s0, buf

    # ---- store a known word, then read its four bytes back ----
    lui  t0, 0x12345            # t0 = 0x12345000
    addi t0, t0, 0x678          # t0 = 0x12345678
    sw   t0, 0(s0)
    lbu  a1, 0(s0)              # 0x78  (little-endian: low byte first)
    lbu  a2, 1(s0)              # 0x56
    lbu  a3, 2(s0)              # 0x34
    lbu  a4, 3(s0)              # 0x12

    # ---- sign vs zero extension at byte width ----
    addi t1, zero, -1           # 0xffffffff
    sb   t1, 4(s0)              # stores 0xff
    lb   a5, 4(s0)              # sign-extended -> 0xffffffff
    lbu  a6, 4(s0)              # zero-extended -> 0x000000ff

    # ---- sign vs zero extension at halfword width ----
    lui  t2, 0xFFFF8            # 0xffff8000
    sh   t2, 6(s0)              # stores 0x8000
    lh   s5, 6(s0)              # sign-extended -> 0xffff8000
    lhu  s1, 6(s0)              # zero-extended -> 0x00008000

    # ---- halfword store does not disturb its neighbour ----
    addi t3, zero, 0x7F0
    sh   t3, 8(s0)
    addi t4, zero, 0x123
    sh   t4, 10(s0)
    lw   s2, 8(s0)              # 0x012307f0

    # ---- array walk with a computed address ----
    li   s3, arr
    addi t5, zero, 0            # index
    addi s4, zero, 0            # running sum
loop:
    slli t6, t5, 2              # index * 4
    add  t6, s3, t6             # &arr[index]
    lw   t0, 0(t6)
    add  s4, s4, t0
    addi t5, t5, 1
    addi t1, zero, 8
    blt  t5, t1, loop           # eight elements

    add  a0, s4, zero           # 1+2+...+8 = 36
    addi a7, zero, 1
    ecall
    addi a7, zero, 10
    ecall

.data
buf:
    .word 0
    .word 0
    .word 0
    .word 0
arr:
    .word 1
    .word 2
    .word 3
    .word 4
    .word 5
    .word 6
    .word 7
    .word 8
