# Sums integers 1..10 using a loop, prints the result, then exits.
# Expected result: a0 (and the value stored to 'result') = 55

.text
.globl main
main:
    addi t0, zero, 0      # t0 = i = 0
    addi t1, zero, 0      # t1 = sum = 0
    addi t2, zero, 11     # t2 = loop bound (exclusive)

loop:
    addi t0, t0, 1        # i++
    beq  t0, t2, done     # if i == 11, exit loop
    add  t1, t1, t0       # sum += i
    j    loop

done:
    add  a0, t1, zero     # a0 = sum (for printing and as "return value")
    li   gp, result       # gp = address of 'result' (li accepts a label operand)
    sw   a0, 0(gp)        # store result to memory

    addi a7, zero, 1      # syscall 1 = print integer in a0
    ecall

    addi a7, zero, 10     # syscall 10 = exit
    ecall

.data
result:
    .word 0
