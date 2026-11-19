main:
    li       a0, data
    flw      fa0, 0(a0)        # 1.5   = 0x3fc00000
    flw      fa1, 4(a0)        # -2.25 = 0xc0100000
    fsgnj.s  fa2, fa0, fa1     # -1.5
    fsgnjn.s fa3, fa0, fa1     # +1.5
    fsgnjx.s fa4, fa0, fa1     # -1.5
    fneg.s   fa5, fa0          # -1.5
    fabs.s   fa6, fa1          # 2.25
    fmv.s    fa7, fa0          # 1.5
    fmv.x.w  s1, fa1           # s1 = 0xc0100000
    fmv.w.x  ft0, s1           # f0 = 0xc0100000
    fclass.s s2, fa0           # +normal
    fclass.s s3, fa1           # -normal
    fsw      fa6, 8(a0)
    lw       s4, 8(a0)         # s4 = 0x40100000
    li       a7, 10
    ecall
data:
    .float 1.5, -2.25
    .word 0