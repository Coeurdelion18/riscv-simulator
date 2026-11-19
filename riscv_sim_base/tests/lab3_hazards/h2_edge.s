# Data hazards variation 2 of 3: the edge cases.
#
#   1. Two writes to one register in flight at once: the YOUNGER producer
#      (in MEM, reached through EX/MEM) must win. A unit that tests MEM/WB
#      first delivers the older value and s0 comes out 2 instead of 3.
#   2. Things that look like dependences and are not: a write to x0, reads
#      of x0, and lui/jal whose unused operand fields hold junk. A correct
#      unit reports ZERO stalls for this whole block.
#   3. Load-use in all four consumer positions: ALU operand, branch
#      operand, store data, and address base (pointer chasing).
#   4. A load whose value is consumed at distance 2 -- which must NOT
#      stall, because MEM/WB forwarding covers it.
#
# Verified on the reference implementation:
#   forward  40 cycles, retired 30, stalls 4,  forwards 11, CPI 1.333, prints 28
#   stall    53 cycles, retired 30, stalls 17, CPI 1.767, prints 28
#   none     37 cycles -- WRONG: prints 1
#   single-cycle 30 cycles. The 4 stalls under forwarding are exactly the
#   four load-use positions; the distance-2 load adds none.
#   Key results: s0=3 s5=8 s6=1 s7=20 s8=7 s9=22.
#
# RUN: forward --hazard forward
# RUN: stall --hazard stall
# RUN: none --hazard none
# RUN: single-cycle --single-cycle
# RARS-CHECK: s0 s1 s2 s3 s4 s5 s6 s7 s8 s9
# RARS-RUNS: forward stall single-cycle

.text
.globl main
main:
    la   gp, data

    # ---- 1. the newer producer must win ----
    addi t0, zero, 1
    addi t0, zero, 2            # older when the reader is in EX
    addi t0, zero, 3            # younger
    add  s0, t0, zero           # must be 3

    # ---- 2. false dependences: no stall may be reported for any of this
    addi zero, t0, 99           # destination x0: discarded
    add  s1, zero, zero         # reads x0 twice, right behind an x0 write
    addi zero, zero, 7
    add  s2, zero, zero         # still nothing to wait for
    lui  s3, 0x12345            # U-type: no source registers at all
    lui  s4, 0x6789a

    # ---- 3a. load -> ALU operand ----
    lw   t1, 0(gp)              # 7
    addi s5, t1, 1              # LOAD-USE -> 8

    # ---- 3b. load -> branch operand ----
    lw   t2, 12(gp)             # 0
    beq  t2, zero, l1           # LOAD-USE into the comparator
    addi s6, zero, 99           # squashed
l1:
    addi s6, zero, 1

    # ---- 3c. load -> store data (no MEM-to-MEM path: stall + forward) ----
    lw   t3, 4(gp)              # 20
    sw   t3, 8(gp)              # LOAD-USE at the store-data port
    lw   s7, 8(gp)              # read it back: 20

    # ---- 3d. load -> address base (pointer chasing) ----
    lw   t4, 16(gp)             # the address of 'data'
    lw   s8, 0(t4)              # LOAD-USE into the address -> 7

    # ---- 4. a load consumed at distance 2: must not stall ----
    lw   t5, 4(gp)
    nop
    addi s9, t5, 2              # 22

    addi a7, zero, 1
    add  a0, s5, s7             # 8 + 20 = 28
    ecall                       # prints 28

    addi a7, zero, 10
    ecall

.data
data:
    .word 7
    .word 20
    .word 0
    .word 0
    .word data
