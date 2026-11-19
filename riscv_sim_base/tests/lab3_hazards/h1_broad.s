# Data hazards variation 1 of 3: broad coverage.
#
# Every shape of RAW dependence the hazard unit has to classify, with no
# padding anywhere -- which is the point: this program is wrong under
# --hazard none and right under stall and forward.
#
#   distance 1 (EX hazard)      -> forwarded from EX/MEM
#   distance 2 (MEM hazard)     -> forwarded from MEM/WB
#   distance 3 and 4            -> already in the register file
#   load then use               -> one stall, then MEM/WB forward
#   dependence into a store's data port, a branch's operands, a jalr
#   base and an ecall argument
#
# Verified on the reference implementation:
#   forward  52 cycles, retired 43, stalls 1,  forwards 21, CPI 1.209, prints 59
#   stall    84 cycles, retired 43, stalls 33, CPI 1.953, prints 59
#   none     45 cycles, retired 36 -- WRONG: the ecall misfires on stale a0/a7
#   single-cycle 43 cycles. forward and stall both match the oracle exactly.
#
# RUN: forward --hazard forward
# RUN: stall --hazard stall
# RUN: none --hazard none
# RUN: single-cycle --single-cycle
# RARS-CHECK: s0 s1 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11 a1 a2
# RARS-RUNS: forward stall single-cycle

.text
.globl main
main:
    # ---- the distance ladder ----
    addi t0, zero, 10
    addi s0, t0, 1              # distance 1  -> 11
    addi t1, zero, 20
    nop
    addi s1, t1, 1              # distance 2  -> 21
    addi t2, zero, 30
    nop
    nop
    addi s2, t2, 1              # distance 3  -> 31
    addi t3, zero, 40
    nop
    nop
    nop
    addi s3, t3, 1              # distance 4  -> 41

    # ---- a dependent chain: every link at distance 1 ----
    addi t4, zero, 1
    addi t4, t4, 2
    addi t4, t4, 4
    addi t4, t4, 8
    add  s4, t4, t4             # 15 + 15 = 30, both operands forwarded

    # ---- load then use, at distance 1 and at distance 2 ----
    la   gp, data
    lw   t5, 0(gp)              # 7
    addi s5, t5, 1              # LOAD-USE                -> 8
    lw   t6, 4(gp)              # 20
    nop
    addi s6, t6, 1              # distance 2: no stall     -> 21

    # ---- dependence into a store's data port, then read it back ----
    add  s7, s5, s6             # 29
    sw   s7, 8(gp)              # store data at distance 1
    lw   s8, 8(gp)              # 29

    # ---- dependence into a branch's operands ----
    addi a1, zero, 4
    addi a2, zero, 4
    beq  a1, a2, taken          # both operands still in the pipe
    addi s9, zero, 99           # squashed
taken:
    addi s9, zero, 5

    # ---- dependence into a jalr base ----
    la   t0, after
    jalr t1, 0(t0)              # base forwarded from distance 1
    addi s10, zero, 99          # squashed
after:
    addi s10, zero, 6

    # ---- dependence into an ecall argument ----
    add  s11, s4, s7            # 30 + 29 = 59
    addi a7, zero, 1
    add  a0, s11, zero          # a0 produced one instruction before ecall
    ecall                       # prints 59

    addi a7, zero, 10
    ecall

.data
data:
    .word 7
    .word 20
    .word 0
