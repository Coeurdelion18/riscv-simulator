# Pipeline variation 2 of 3: the edge cases a working pipeline must get
# right and a plausible-looking one does not.
#
#   1. Squash completeness: a taken branch, a j and a jal each have TWO
#      instructions behind them with visible side effects -- a register
#      write and a memory store. None may happen. A pipeline that squashes
#      the ID slot but not the IF slot passes half of these.
#   2. Wrong-path faults stay silent: an illegal instruction word and an
#      out-of-bounds load sit in a squashed shadow. Faults are recorded in
#      the latch and only raised at WB, so neither may kill the program.
#   3. PC sourcing: auipc and jal must use the PC carried with them, not
#      the machine's fetch PC, which runs four instructions ahead.
#   4. Drain on exit: three instructions are still in flight when the exit
#      ecall reaches EX, and all three must still write back.
#
# Every marker register must end at 0 except where noted.
#
# Verified on the reference implementation:
#   pipeline      57 cycles, retired 43, redirects 5, squashed 10, CPI 1.326
#   single-cycle  43 cycles. Registers identical. 43 + 4 + 2x5 = 57.
#   Every marker register ends at 0; a5=11, a6=22, s11=33; memory stays 0.
#
# RUN: pipeline
# RUN: single-cycle --single-cycle

.text
.globl main
main:
    la   s0, slot
    addi t1, zero, 1
    addi t2, zero, 1
    nop
    nop

    # ---- 1. shadows of a taken branch: register and memory ----
    beq  t1, t2, l1             # taken
    addi s1, zero, 99           # IF slot: must not run
    sw   t1, 0(s0)              # ID slot: must not write memory
l1:
    nop
    nop

    # ---- shadow of an unconditional jump ----
    j    l2
    addi s2, zero, 99
    addi s3, zero, 99
l2:
    nop
    nop

    # ---- shadow of a jal (never returned to, so markers are safe) ----
    jal  t3, l3
    addi s4, zero, 99
    addi s5, zero, 99
l3:
    nop
    nop

    # ---- 2. wrong-path illegal words and a bad load ----
    lui  t4, 0x40000            # 0x40000000: outside a 1 MB memory
    nop
    nop
    beq  t1, t2, l4             # taken: the next two words are squashed
    .word 0x00000000            # not a legal instruction
    lw   t5, 0(t4)              # would be a MEM_FAULT if it ran
l4:
    nop
    nop

    # ---- 3. auipc and jal must use their own PC ----
here:
    auipc a1, 0                 # address of 'here'
    nop
    nop
    la    a2, here              # the same address, from the symbol table
    nop
    nop
    sub   s6, a1, a2            # must be 0

    jal   a3, landing           # a3 = address of the next instruction
retpt:
    addi  s7, zero, 99          # squashed by the jal
    addi  s8, zero, 99          # squashed by the jal
landing:
    la    a4, retpt
    nop
    nop
    sub   s9, a3, a4            # must be 0

    # ---- memory must still hold 0: no shadow store ever landed ----
    lw   s10, 0(s0)
    nop
    nop

    # ---- 4. three instructions in flight behind the exit ecall ----
    addi a7, zero, 10
    addi a5, zero, 11           # these three are in MEM/WB when the
    addi a6, zero, 22           # ecall runs in EX, and must still
    addi s11, zero, 33          # reach the register file
    ecall

.data
slot:
    .word 0
