#include "rv32m.h"
#include "isa_registry.h"
#include <limits.h>

static void execute_m(CPUState *cpu, Memory *mem, DecodedInstr *instr) {
    (void)mem;
    uint32_t a = cpu_read_reg(cpu, instr->rs1);
    uint32_t b = cpu_read_reg(cpu, instr->rs2);
    int32_t sa = (int32_t)a, sb = (int32_t)b;
    uint32_t result = 0;
    switch (instr->funct3) {
        case 0: result = (uint32_t)((uint64_t)a * b); break;
        case 1: result = (uint32_t)((uint64_t)((int64_t)sa * sb) >> 32); break;
        case 2: result = (uint32_t)((uint64_t)((int64_t)sa * (int64_t)b) >> 32); break;
        case 3: result = (uint32_t)(((uint64_t)a * b) >> 32); break;
        case 4:
            result = b == 0 ? UINT32_MAX :
                (sa == INT32_MIN && sb == -1) ? a : (uint32_t)(sa / sb);
            break;
        case 5: result = b == 0 ? UINT32_MAX : a / b; break;
        case 6:
            result = b == 0 ? a :
                (sa == INT32_MIN && sb == -1) ? 0 : (uint32_t)(sa % sb);
            break;
        case 7: result = b == 0 ? a : a % b; break;
    }
    cpu_write_reg(cpu, instr->rd, result);
}

void register_rv32m(void) {
    static const char *names[] = {"mul","mulh","mulhsu","mulhu","div","divu","rem","remu"};
    for (int i = 0; i < 8; ++i)
        register_instruction((InstructionHandler){names[i], 0x33, i, 1, FMT_R, execute_m});
}
