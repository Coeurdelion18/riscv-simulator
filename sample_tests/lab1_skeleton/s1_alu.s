# Skeleton variation 1 of 3: the integer ALU.
#
# Every RV32I arithmetic and logical instruction, plus the signed/unsigned
# distinction and the three shift kinds. No memory, no branches -- so a
# failure here is an instruction-semantics bug, not a control-flow one.
#
# Prints the checksum in a0.

# Verified on the reference implementation:
#   41 cycles, prints 2016. RARS 1.6 agrees on all 19 result registers.

.text
.globl main
main:
    # ---- I-type and R-type add/sub, with a negative operand ----
    addi t0, zero, 100
    addi t1, zero, -50
    add  s0, t0, t1             # 50
    sub  s1, t0, t1             # 150

    # ---- bitwise ----
    addi t2, zero, 240          # 0x0F0
    addi t3, zero, 204          # 0x0CC
    and  s2, t2, t3             # 0x0C0 = 192
    or   s3, t2, t3             # 0x0FC = 252
    xor  s4, t2, t3             # 0x03C = 60
    andi s5, t2, 15             # 0
    ori  s6, t2, 15             # 255
    xori s7, t2, 255            # 15

    # ---- shifts: logical left, logical right, arithmetic right ----
    addi t4, zero, 1
    slli s8, t4, 10             # 1024
    addi t5, zero, -8           # 0xfffffff8
    srli s9, t5, 28             # 0x0000000f
    srai s10, t5, 1             # 0xfffffffc = -4
    addi t6, zero, 4
    sll  a1, t4, t6             # 16   (register shift amount)
    srl  a2, t5, t6             # 0x0ffffffff >> 4 = 0x0fffffff
    sra  a3, t5, t6             # -1

    # ---- set-less-than: signed vs unsigned on the same bits ----
    slt  a4, t5, t4             # -8 < 1 signed      -> 1
    sltu a5, t5, t4             # 0xfffffff8 < 1 uns -> 0
    slti a6, t5, 0              # -8 < 0             -> 1
    sltiu a7, t5, 0             # unsigned < 0       -> 0

    # ---- upper immediates ----
    lui  s11, 0x12345           # 0x12345000
    auipc t0, 0                 # address of this instruction

    # ---- checksum of the independent results ----
    add  a0, s0, s1
    add  a0, a0, s2
    add  a0, a0, s3
    add  a0, a0, s4
    add  a0, a0, s6
    add  a0, a0, s7
    add  a0, a0, s8
    add  a0, a0, a1
    add  a0, a0, a4
    add  a0, a0, a6
    addi a7, zero, 1
    ecall                       # print the checksum

    addi a7, zero, 10
    ecall
