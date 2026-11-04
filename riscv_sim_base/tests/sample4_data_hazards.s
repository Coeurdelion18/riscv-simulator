# sample4_data_hazards.s -- one program per hazard class.
.text
.globl main
main:
# ---- Block A: EX/MEM hazards, three in a row ------------------------
addi t0, zero, 5 # t0 = 5
addi t1, t0, 10 # t0 at distance 1 -> t1 = 15
add t2, t1, t0 # t1 at 1, t0 at 2 -> t2 = 20
sub t3, t2, t1 # t2 at 1, t1 at 2 -> t3 = 5
# ---- Block B: MEM/WB hazards, and distance 3 (no hazard) ------------
addi t4, zero, 8 # t4 = 8
addi t5, zero, 9 # independent
add t6, t4, zero # t4 at distance 2 -> t6 = 8
addi s0, zero, 1 # independent
addi s1, zero, 2 # independent
add s2, s0, s1 # s0 at 3, s1 at 2 -> s2 = 3
# ---- Block C: x0 is never a dependency -----------------------------
addi zero, t0, 99 # dst = x0: nothing to depend on
add s3, zero, t1 # reads x0: no hazard -> s3 = 15
# ---- Block D: load-use ---------------------------------------------
lui  gp, 0
addi gp, gp, 0x78
lw s4, 0(gp) # gp at distance 1 -> s4 = 7
addi s5, s4, 1 # LOAD-USE -> s5 = 8
lw s6, 4(gp) # s6 = 20
add s7, s6, s5 # LOAD-USE on s6 -> s7 = 28
# ---- Block E: the store-data port ----------------------------------
add s8, s7, s5 # s8 = 36
sw s8, 8(gp) # s8 at distance 1, as src2
lw s9, 8(gp) # s9 = 36
# ---- Block F: a branch whose operands are still in the pipe --------
addi s10, zero, 4
addi s11, zero, 4
beq s10, s11, show # s11 at 1, s10 at 2; taken
addi s9, zero, 0 # squashed -- must never execute
show:
add a0, s9, zero # a0 = 36
addi a7, zero, 1
ecall # print a0
addi a7, zero, 10
ecall # exit
.data
data:
.word 7
.word 20
.word 0