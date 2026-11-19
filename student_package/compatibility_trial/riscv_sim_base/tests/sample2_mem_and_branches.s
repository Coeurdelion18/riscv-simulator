# Exercises byte/halfword/word stores+loads (incl. sign vs zero extension)
# and a few branch/comparison instructions.
#
# Expected final state (check with `regs` in interactive mode, or diff the
# --batch dump against this):
#   a0 = 0xfffffff6   (-10, sign-extended lb of a byte holding 0xf6)
#   a1 = 0x000000f6   (zero-extended lbu of the same byte)
#   a2 = 1             (result of the blt/bge comparison chain below)

.text
.globl main
main:
    li   t0, buf            # t0 = address of buf

    addi t1, zero, -10      # t1 = -10
    sb   t1, 0(t0)          # store low byte of -10 (0xf6) at buf[0]

    lb   a0, 0(t0)          # sign-extended reload -> 0xfffffff6
    lbu  a1, 0(t0)          # zero-extended reload -> 0x000000f6

    addi t2, zero, 3
    addi t3, zero, 7
    addi a2, zero, 0        # a2 = 0 by default

    blt  t2, t3, set_true   # 3 < 7 is true -> a2 should end up 1
    j    after

set_true:
    addi a2, zero, 1

after:
    addi a7, zero, 10       # exit
    ecall

.data
buf:
    .byte 0
