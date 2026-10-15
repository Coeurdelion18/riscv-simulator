# RISC-V Simulator Codebase Manual

## A Guided Manual for Understanding the `riscv_sim_base` C Project

---

## 1. Introduction

This manual explains the architecture and implementation of the `riscv_sim_base` project. The project is a teaching-oriented **RV32I RISC-V processor simulator written in C**.

The purpose of the simulator is not to model individual hardware gates or an FPGA implementation. Instead, C data structures and functions are used to model the architectural state and behavior of a RISC-V processor.

The simulator provides:

- A CPU state containing registers and the program counter.
- A byte-addressable simulated memory.
- An assembler that converts RISC-V assembly into machine code.
- A decoder that extracts instruction fields from 32-bit instructions.
- An ISA registry that associates instruction encodings with their implementations.
- Instruction execution functions implementing the RV32I base ISA.
- A command-line interface/debugger.
- Example RISC-V assembly programs for testing.

The most important conceptual flow is:

```text
                    RISC-V Assembly
                           |
                           v
                     +-----------+
                     | Assembler |
                     +-----+-----+
                           |
                     Machine Code
                           |
                           v
                        Memory
                           |
                           v
                 +-------------------+
                 |   Execute Cycle   |
                 |                   |
                 |  1. Fetch         |
                 |  2. Decode        |
                 |  3. Execute       |
                 |  4. Update PC     |
                 +---------+---------+
                           |
                           v
                       CPU State
```

---

# 2. Project Structure

The project is organized into several logical components:

```text
riscv_sim_base/
|
├── main.c
├── Makefile
├── README.md
|
├── core/
│   ├── cpu_state.c/.h
│   ├── memory.c/.h
│   ├── isa_registry.c/.h
│   ├── decoder.c/.h
│   └── executor.c/.h
|
├── isa/
│   └── rv32i_base.c/.h
|
├── assembler/
│   └── assembler.c/.h
|
├── ui/
│   └── cli_display.c/.h
|
└── tests/
    ├── sample1_sum_loop.s
    └── sample2_mem_and_branches.s
```

The major responsibilities are:

| Component | Processor concept |
|---|---|
| `CPUState` | Processor architectural state |
| `Memory` | Main memory |
| `decoder.c` | Instruction decoding |
| `executor.c` | Fetch/decode/execute control |
| `rv32i_base.c` | RV32I instruction behavior |
| `isa_registry.c` | Instruction definition and dispatch |
| `assembler.c` | Assembly-to-machine-code conversion |
| `cli_display.c` | Interactive/debugging interface |
| `main.c` | Overall program control |

---

# 3. Overall Architecture

The simulator is deliberately modular.

A useful way to visualize the complete system is:

```text
                     Assembly Program
                            |
                            v
                     +-------------+
                     |  Assembler  |
                     +------+------+
                            |
                      32-bit words
                            |
                            v
                         Memory
                            |
                            |
                    +-------v-------+
                    |    Fetch      |
                    +-------+-------+
                            |
                            v
                    +---------------+
                    |     Decode    |
                    +-------+-------+
                            |
                            v
                     ISA Registry
                            |
                            v
                    Instruction Handler
                            |
                            v
                       Execute
                     /     |      \
                   ALU   Memory   Branch
                     \     |      /
                            |
                            v
                       Next PC
                            |
                            v
                           CPU
```

The important point is that the simulator separates:

1. **What an instruction looks like.**
2. **How it is encoded.**
3. **How it is decoded.**
4. **What it does when executed.**

This separation makes the code suitable for teaching and for later ISA extensions.

---

# 4. `main.c` — Starting the Simulator

The simulator starts from `main()`.

The major initialization steps are conceptually:

```c
isa_registry_init();
register_rv32i_base();
```

The ISA registry is initialized first, followed by registration of the RV32I instruction set.

Then memory is created:

```c
Memory mem;
memory_init(&mem, mem_size);
```

The assembly program is assembled:

```c
assemble_file(program_path, &mem, &entry_point, &err);
```

The CPU is initialized:

```c
CPUState cpu;
cpu_reset(&cpu, entry_point);
```

Finally, execution begins either in batch mode or through the command-line debugger.

The overall flow is:

```text
main()
 |
 +-- Initialize ISA
 |
 +-- Allocate memory
 |
 +-- Assemble assembly program
 |
 +-- Initialize CPU
 |
 +-- Execute program
       |
       +-- execute_cycle()
              |
              +-- Fetch
              +-- Decode
              +-- Execute
              +-- Update PC
```

---

# 5. CPU State

The `CPUState` structure represents the state of the simulated processor.

Conceptually it contains:

```c
typedef struct {
    uint32_t regs[NUM_REGS];
    uint32_t pc;
    uint32_t next_pc;
    CPUStatus status;
    uint64_t cycle_count;

    uint32_t fault_addr;
    char fault_msg[128];
} CPUState;
```

## 5.1 Register File

```c
uint32_t regs[32];
```

represents the 32 general-purpose RISC-V registers:

```text
x0
x1
x2
...
x31
```

Since this is RV32, each register is 32 bits wide.

---

## 5.2 Program Counter

```c
uint32_t pc;
```

is the address of the current instruction.

For example:

```text
PC = 0x00000010
```

means that the current instruction is stored starting at address `0x10`.

Since ordinary RV32I instructions are 32 bits:

```text
PC + 4
```

normally points to the next instruction.

---

# 6. Why `next_pc` Exists

The simulator maintains both:

```c
pc
next_pc
```

At the beginning of a cycle, the default next address is:

```c
cpu->next_pc = cpu->pc + 4;
```

For an ordinary instruction:

```text
PC = 0x100
next_pc = 0x104
```

After execution:

```text
PC = 0x104
```

For a taken branch:

```text
PC = 0x100
next_pc initially = 0x104

branch taken
    |
    v
next_pc = 0x080

PC = 0x080
```

This is a clean way to model control flow.

It is also useful when extending the simulator to pipelined execution because PC selection becomes explicit.

---

# 7. The Special RISC-V Register `x0`

RISC-V defines register `x0` as a constant zero.

The simulator enforces this centrally.

Reading register zero returns zero:

```c
uint32_t cpu_read_reg(const CPUState *cpu, int idx) {
    if (idx == 0) return 0;
    return cpu->regs[idx & 0x1F];
}
```

Writing register zero is ignored:

```c
void cpu_write_reg(CPUState *cpu, int idx, uint32_t value) {
    if (idx == 0) return;
    cpu->regs[idx & 0x1F] = value;
}
```

Therefore, even if an instruction tries:

```asm
addi zero, t0, 5
```

the write has no effect.

This is a good design because every instruction does not need to separately implement the `x0` rule.

---

# 8. Simulated Memory

The memory structure is conceptually:

```c
typedef struct {
    uint8_t *data;
    uint32_t size;
} Memory;
```

It consists of:

```text
Memory
 |
 +-- data -> byte array
 |
 +-- size -> memory capacity
```

The default memory size is:

```c
#define DEFAULT_MEM_SIZE (1u << 20)
```

which corresponds to:

```text
1,048,576 bytes = 1 MB
```

The memory is byte-addressable, as in a real processor.

---

# 9. Little-Endian Memory

A 32-bit word is reconstructed from four bytes.

Conceptually:

```c
*out = (uint32_t)mem->data[addr]
     | ((uint32_t)mem->data[addr + 1] << 8)
     | ((uint32_t)mem->data[addr + 2] << 16)
     | ((uint32_t)mem->data[addr + 3] << 24);
```

Suppose memory contains:

```text
Address       Value

0x100         78
0x101         56
0x102         34
0x103         12
```

The resulting 32-bit value is:

```text
0x12345678
```

This is little-endian representation.

The implementation explicitly constructs the word rather than simply casting the byte pointer to a `uint32_t *`. This avoids potential alignment and strict-aliasing issues in C.

---

# 10. The ISA Registry

One of the most important design elements is the ISA registry.

An instruction is represented using information similar to:

```c
typedef struct {
    const char *mnemonic;
    uint32_t opcode;
    int32_t funct3;
    int32_t funct7;
    InstrFormat format;
    ExecuteFn execute;
} InstructionHandler;
```

For example, an `add` definition contains:

```text
mnemonic = "add"
opcode    = 0x33
funct3    = 0
funct7    = 0x00
format    = R
execute   = exec_add
```

The registry therefore conceptually contains:

```text
+---------+--------+--------+--------+-----------+
|Mnemonic | Opcode | Funct3 | Funct7 | Execute   |
+---------+--------+--------+--------+-----------+
| add     | 0x33   | 0      | 0x00   | exec_add  |
| sub     | 0x33   | 0      | 0x20   | exec_sub  |
| sll     | 0x33   | 1      | 0x00   | exec_sll  |
| ...     | ...    | ...    | ...    | ...       |
| lw      | 0x03   | 2      | ...    | exec_lw   |
| sw      | 0x23   | 2      | ...    | exec_sw   |
| beq     | 0x63   | 0      | ...    | exec_beq  |
+---------+--------+--------+--------+-----------+
```

This is essentially a software instruction dispatch table.

---

# 11. Why the ISA Registry Is Important

A traditional implementation might use:

```c
switch (opcode) {
    case ...:
    case ...:
    case ...:
}
```

This project instead uses a table of instruction definitions and function pointers.

That means an instruction can be described as:

```text
Instruction
 |
 +-- Encoding
 |     +-- opcode
 |     +-- funct3
 |     +-- funct7
 |
 +-- Format
 |
 +-- Behavior
       |
       +-- Execute function
```

This makes ISA extensions easier.

For example, a future `mul` instruction can be added by implementing:

```c
static void exec_mul(...) {
    ...
}
```

and registering it with its encoding.

The rest of the simulator can remain largely unchanged.

---

# 12. Function Pointers and Dynamic Dispatch

The `execute` member is a function pointer.

For example:

```text
add  -> exec_add
sub  -> exec_sub
lw   -> exec_lw
sw   -> exec_sw
beq  -> exec_beq
```

When the decoder identifies an instruction, the simulator can simply call:

```c
handler->execute(cpu, mem, &instr);
```

This is effectively dynamic dispatch implemented in C.

The execution path becomes:

```text
32-bit instruction
        |
        v
     decoder
        |
        v
Instruction fields
        |
        v
  ISA registry lookup
        |
        v
  Function pointer
        |
        v
    exec_XXX()
```

---

# 13. The Execute Cycle

The central execution routine is:

```c
CPUStatus execute_cycle(CPUState *cpu, Memory *mem)
```

It can be understood as four conceptual stages.

## Stage 1 — Fetch

```c
memory_read_word(mem, cpu->pc, &raw);
```

The simulator reads the 32-bit instruction from memory.

If:

```text
PC = 0x20
```

then the bytes at:

```text
0x20
0x21
0x22
0x23
```

are combined into the instruction word.

---

## Stage 2 — Decode

The raw 32-bit instruction is passed to:

```c
decode_instruction(raw, &instr, &handler)
```

The decoder extracts fields such as:

```text
opcode
rd
rs1
rs2
funct3
funct7
immediate
```

For an R-type instruction:

```text
31       25 24    20 19    15 14  12 11     7 6      0
+----------+--------+--------+------+---------+--------+
| funct7   |  rs2   |  rs1   |funct3|   rd    | opcode |
+----------+--------+--------+------+---------+--------+
```

The C bit operations implement these field extractions.

---

## Stage 3 — Execute

Once the instruction is identified, the executor calls the registered function:

```c
handler->execute(cpu, mem, &instr);
```

For `add`, this calls:

```c
exec_add()
```

For `lw`:

```c
exec_lw()
```

For `beq`:

```c
exec_beq()
```

---

## Stage 4 — Update PC

At the start of the cycle:

```c
next_pc = pc + 4;
```

An instruction such as a branch or jump may modify `next_pc`.

At the end:

```c
pc = next_pc;
```

Thus the complete cycle is:

```text
FETCH
  |
  v
DECODE
  |
  v
EXECUTE
  |
  v
UPDATE PC
```

---

# 14. RV32I Instruction Implementation

The file `rv32i_base.c` implements the actual semantics of the RV32I instructions.

It contains groups such as:

```text
R-type arithmetic
    add sub sll slt sltu xor srl sra or and

I-type arithmetic
    addi slti sltiu xori ori andi
    slli srli srai

Loads
    lb lh lw lbu lhu

Stores
    sb sh sw

Branches
    beq bne blt bge bltu bgeu

Jumps
    jal jalr

Upper-immediate
    lui auipc

System
    ecall
```

Each instruction ultimately maps to a C function.

---

# 15. R-Type Arithmetic

Consider:

```asm
add t0, t1, t2
```

The implementation is conceptually:

```c
cpu_write_reg(c, i->rd,
    cpu_read_reg(c, i->rs1) +
    cpu_read_reg(c, i->rs2));
```

The mathematical operation is:

\[
R[rd] = R[rs1] + R[rs2]
\]

The hardware interpretation is:

```text
             +-----------+
t1 -------->|           |
            |    ALU    |------> t0
t2 -------->|     +     |
            +-----------+
```

The C implementation therefore corresponds closely to:

1. Read register `rs1`.
2. Read register `rs2`.
3. Perform ALU operation.
4. Write result to `rd`.

---

# 16. `sub`

The `sub` implementation is analogous:

\[
R[rd] = R[rs1] - R[rs2]
\]

The simulator does not explicitly check for integer overflow. The result naturally wraps to 32 bits because the register representation is `uint32_t`.

For example:

```text
0xFFFFFFFF + 1 = 0x00000000
```

under 32-bit modular arithmetic.

---

# 17. Shift Instructions

For `sll`, the simulator masks the shift amount:

```c
shift_amount = cpu_read_reg(c, i->rs2) & 0x1F;
```

Since:

```text
0x1F = 31
```

only the lower five bits remain.

Therefore:

```text
0 <= shift_amount <= 31
```

which is appropriate for a 32-bit register.

Conceptually:

```text
rs2
 |
 v
lower 5 bits
 |
 v
shift amount
 |
 v
barrel shifter
```

---

# 18. Signed and Unsigned Comparisons

The distinction between:

```asm
slt
```

and:

```asm
sltu
```

is important.

For `slt`, the register values are interpreted as `int32_t`.

For `sltu`, they are interpreted as `uint32_t`.

Consider:

```text
0xFFFFFFFF
```

As a signed value:

```text
-1
```

As an unsigned value:

```text
4294967295
```

Therefore:

```text
signed:   -1 < 1        -> true
unsigned: 4294967295 < 1 -> false
```

The C type used by the simulator directly models this ISA distinction.

---

# 19. `srl` versus `sra`

Logical right shift:

```asm
srl
```

treats the value as unsigned, so zeros are introduced from the left.

Arithmetic right shift:

```asm
sra
```

treats the value as signed, allowing the sign bit to propagate.

For example:

```text
11111111 10000000
```

after logical shifting:

```text
00001111 11111000
```

after arithmetic shifting:

```text
11111111 11111000
```

The simulator uses `uint32_t` and `int32_t` appropriately to reproduce this distinction.

---

# 20. I-Type Arithmetic

Consider:

```asm
addi t0, t1, 10
```

The semantic operation is:

\[
R[rd] = R[rs1] + SEXT(imm)
\]

The implementation uses the decoded immediate:

```c
cpu_write_reg(c, i->rd,
    cpu_read_reg(c, i->rs1) +
    (uint32_t)i->imm);
```

The important design point is that **sign extension has already been performed by the decoder**.

Thus:

```text
Decoder
 |
 +-- Extract immediate
 |
 +-- Sign extend
 |
 v
DecodedInstr.imm
 |
 v
Instruction handler
 |
 +-- Perform operation
```

This is analogous to a hardware processor where the decode stage prepares the immediate value for the execute stage.

---

# 21. Loads

Consider:

```asm
lw t0, 8(sp)
```

The address calculation is:

\[
address = R[sp] + SEXT(8)
\]

The simulator computes the address:

```c
uint32_t addr =
    cpu_read_reg(c, i->rs1) +
    (uint32_t)i->imm;
```

Then reads memory:

```c
memory_read_word(m, addr, &v);
```

Finally:

```c
cpu_write_reg(c, i->rd, v);
```

The corresponding datapath is:

```text
          rs1
           |
           v
         +---+
imm ---> | + |
         +---+
           |
           v
        Memory
           |
           v
          rd
```

This is the classic load datapath.

---

# 22. `lb` versus `lbu`

This is an important example of sign extension.

Suppose memory contains:

```text
0xFF
```

For:

```asm
lb t0, 0(sp)
```

the byte is interpreted as signed:

```text
0xFF = -1
```

and sign extended:

```text
11111111
        |
        v
11111111 11111111 11111111 11111111
```

giving:

```text
0xFFFFFFFF
```

For:

```asm
lbu t0, 0(sp)
```

the value is unsigned:

```text
255
```

giving:

```text
0x000000FF
```

Therefore:

```text
lb
 |
 +-- sign extend

lbu
 |
 +-- zero extend
```

---

# 23. Stores

For:

```asm
sw t0, 8(sp)
```

the address is:

\[
address = R[sp] + SEXT(8)
\]

The value in `t0` is then written to memory.

Conceptually:

```text
                 rs1
                  |
                  v
                +---+
          imm ->| + |
                +---+
                  |
                  v
                Memory <----- rs2
```

Unlike a load, a store has no destination register.

---

# 24. S-Type Immediate Encoding

RISC-V stores an S-type immediate in two separate fields:

```text
instruction[31:25] -> imm[11:5]
instruction[11:7]  -> imm[4:0]
```

The decoder reconstructs these pieces.

This initially appears unusual from a software perspective, but it is a consequence of RISC-V keeping register fields in consistent locations across instruction formats.

The encoding is:

```text
31      25 24    20 19    15 14  12 11       7 6      0
+---------+--------+--------+------+----------+--------+
| imm11:5 |  rs2   |  rs1   |funct3| imm4:0   | opcode |
+---------+--------+--------+------+----------+--------+
```

---

# 25. Branch Instructions

Consider:

```asm
beq t0, t2, done
```

The implementation conceptually does:

```c
if (cpu_read_reg(c, i->rs1) ==
    cpu_read_reg(c, i->rs2))
{
    c->next_pc = c->pc + (uint32_t)i->imm;
}
```

Notice that the function does not explicitly handle the not-taken case.

That is because the executor has already initialized:

```c
next_pc = pc + 4;
```

Therefore:

```text
                    execute_cycle
                         |
                         v
                  next_pc = PC + 4
                         |
                   +-----+-----+
                   |           |
              not taken      taken
                   |           |
                   v           v
                 PC+4      PC + offset
```

This is a clean separation of control logic.

---

# 26. Signed and Unsigned Branches

The same signed/unsigned distinction appears in:

```asm
blt
bltu
```

`blt` compares signed 32-bit values.

`bltu` compares unsigned 32-bit values.

Thus the C implementation explicitly casts register contents to the appropriate type.

This is an excellent example of how C's type system is being used to model ISA semantics.

---

# 27. `jal`

The `jal` instruction performs two operations:

\[
R[rd] = PC + 4
\]

and:

\[
PC = PC + offset
\]

The implementation therefore writes the return address to `rd` and modifies `next_pc`.

For:

```asm
jal ra, function
```

the usual behavior is:

```text
ra = PC + 4
PC = function
```

The conceptual datapath is:

```text
              PC
               |
        +------+------+
        |             |
       +4          +offset
        |             |
        v             v
       rd            PC
```

---

# 28. `jalr`

`jalr` provides indirect control flow.

The target is computed as:

\[
target = (R[rs1] + SEXT(imm)) \& \sim 1
\]

The return address is:

\[
R[rd] = PC+4
\]

and:

\[
next\_pc = target
\]

The masking:

```c
& ~1u
```

clears the least significant bit of the target as required by the RISC-V specification.

---

# 29. `lui`

`lui` loads an upper immediate into a register.

Conceptually:

\[
R[rd] = imm_{31:12} << 12
\]

The decoder constructs the immediate, and the execution function simply writes it into the destination register.

---

# 30. `auipc`

`auipc` adds an upper immediate to the current PC:

\[
R[rd] = PC + (imm_{31:12} << 12)
\]

This is useful for constructing PC-relative addresses.

---

# 31. `ecall`

The simulator provides a small simulator-specific system-call mechanism.

For example, a particular value in `a7` may indicate:

```text
print integer
```

while another value may indicate:

```text
exit
```

This should not be confused with the Linux RISC-V ABI.

It is simply a convenient mechanism that allows small assembly programs to print values and terminate.

For example, conceptually:

```asm
li a0, 42
li a7, 1
ecall
```

can be interpreted by the simulator as:

```text
print integer contained in a0
```

---

# 32. Instruction Registration

At the end of the RV32I implementation, instructions are registered with the ISA registry.

An entry conceptually looks like:

```c
register_instruction(
    (InstructionHandler){
        "add",
        OP_R,
        0x0,
        0x00,
        FMT_R,
        exec_add
    }
);
```

This contains six pieces of information:

```text
"add"       -> assembly mnemonic
OP_R        -> opcode
0x0         -> funct3
0x00        -> funct7
FMT_R       -> instruction format
exec_add    -> implementation
```

Therefore an instruction definition combines:

\[
\boxed{
\text{Encoding} + \text{Format} + \text{Behavior}
}
\]

---

# 33. Don't-Care Fields

Some instructions do not use all fields.

For example, an I-type arithmetic instruction may have no meaningful `funct7`.

The registry represents this with a value such as:

```text
funct7 = -1
```

This means the field is effectively "don't care" during matching.

For instructions such as `jal`, multiple fields may be irrelevant.

This allows the same registry mechanism to handle different instruction formats.

---

# 34. The Assembler

The assembler is responsible for converting source such as:

```asm
add t0, t1, t2
```

into a 32-bit machine instruction.

It uses the same ISA registry as the decoder.

This is an important architectural decision.

The relationship is:

```text
                     ISA Registry
                    /            \
                   /              \
                  v                v
             Assembler          Decoder
                 |                 |
                 v                 v
          Assembly -> Binary   Binary -> Fields
                 |                 |
                 +-------+---------+
                         |
                         v
                  Same ISA model
```

The registry therefore acts as a common source of instruction definitions.

---

# 35. Two-Pass Assembly

The assembler uses a two-pass approach because labels may refer to addresses that are not yet known.

Consider:

```asm
loop:
    addi t0, t0, 1
    beq  t0, t2, done
    ...
done:
```

During the first pass, the assembler determines addresses:

```text
loop -> 0x0c
done -> 0x1c
```

During the second pass, it can calculate:

```text
branch offset = address(done) - address(beq)
```

and encode the branch correctly.

Thus:

```text
Pass 1
 |
 +-- Determine instruction addresses
 +-- Build symbol/label table
 |
 v
Pass 2
 |
 +-- Resolve labels
 +-- Encode instructions
 |
 v
Machine code
```

---

# 36. Pseudo-Instruction `li`

`li` is not a base RV32I machine instruction.

For a sufficiently large immediate, the assembler can expand it into a sequence such as:

```asm
lui
addi
```

Conceptually:

```asm
li t0, immediate
```

becomes:

```asm
lui  t0, upper
addi t0, t0, lower
```

This is a good example of the distinction between:

```text
Assembly language
        |
        v
Pseudo-instructions
        |
        v
Real ISA instructions
        |
        v
Machine code
```

---

# 37. Complete Example: `add`

It is useful to trace a single instruction through the entire project.

Suppose the source contains:

```asm
add t0, t1, t2
```

## Step 1 — Assembler

The assembler looks up:

```text
add
```

in the ISA registry and obtains:

```text
opcode = 0x33
funct3 = 0
funct7 = 0x00
format = R
```

It encodes:

```text
funct7 | rs2 | rs1 | funct3 | rd | opcode
```

---

## Step 2 — Memory

The generated 32-bit instruction is stored in simulated memory.

---

## Step 3 — Fetch

The executor reads four bytes from memory at `PC`.

---

## Step 4 — Decode

The decoder extracts:

```text
opcode
rd
rs1
rs2
funct3
funct7
```

---

## Step 5 — Registry Lookup

The decoder finds:

```text
opcode = 0x33
funct3 = 0
funct7 = 0x00
```

which corresponds to:

```text
exec_add
```

---

## Step 6 — Execute

The handler performs:

\[
R[t0] = R[t1] + R[t2]
\]

---

## Step 7 — PC Update

Since this is not a branch or jump:

\[
PC \leftarrow PC + 4
\]

One complete instruction has now been simulated.

---

# 38. Tracing an Entire Program

Consider a simple loop:

```asm
main:
    addi t0, zero, 0
    addi t1, zero, 0
    addi t2, zero, 11

loop:
    addi t0, t0, 1
    beq  t0, t2, done
    add  t1, t1, t0
    j    loop

done:
    ...
```

The initial state is approximately:

```text
t0 = 0
t1 = 0
t2 = 0
```

After initialization:

```text
t0 = 0
t1 = 0
t2 = 11
```

The first iteration executes:

```text
t0 = 1
t1 = 1
```

The second:

```text
t0 = 2
t1 = 3
```

The third:

```text
t0 = 3
t1 = 6
```

Eventually:

```text
t0 = 10
t1 = 55
t2 = 11
```

Then:

```text
t0 = 11
```

and:

```text
beq t0, t2, done
```

becomes true.

The branch changes `next_pc` to the address of `done`.

This is a useful example for tracing the simulator because it exercises:

- Register reads.
- Register writes.
- Immediate decoding.
- ALU operations.
- Branch comparison.
- Branch immediate calculation.
- PC update.
- Jump execution.

---

# 39. The Three Layers Students Should Distinguish

The simulator is easiest to understand if the implementation is separated into three conceptual layers.

## Layer 1 — ISA Semantics

What does:

```asm
add t0, t1, t2
```

mean?

Answer:

\[
R[t0] = R[t1] + R[t2]
\]

---

## Layer 2 — Machine Encoding

How is that instruction represented as 32 bits?

For an R-type instruction:

```text
funct7 | rs2 | rs1 | funct3 | rd | opcode
```

---

## Layer 3 — Processor Execution

How does the simulated processor perform it?

```text
Read registers
      |
      v
     ALU
      |
      v
Write register
      |
      v
Update PC
```

These three questions are related but should not be mixed together.

---

# 40. Why the Code Is Good for an Architecture Course

This project provides a natural bridge from ISA-level programming to processor microarchitecture.

The current simulator essentially models:

```text
              Single-Cycle Processor

        +---------------------------+
        |                           |
        |   Fetch                   |
        |     |                     |
        |     v                     |
        |   Decode                  |
        |     |                     |
        |     v                     |
        |   Execute                 |
        |     |                     |
        |     v                     |
        |   Memory                  |
        |     |                     |
        |     v                     |
        |   Write Back              |
        |                           |
        +---------------------------+
```

This can later evolve into a pipelined processor.

For a five-stage pipeline:

```text
Cycle ->    1    2    3    4    5    6

I1          IF   ID   EX   MEM  WB
I2               IF   ID   EX   MEM  WB
I3                    IF   ID   EX   MEM  WB
I4                         IF   ID   EX   MEM
```

The current simulator therefore provides a useful starting point for implementing:

- Pipeline registers.
- Data hazards.
- Forwarding.
- Hazard detection.
- Branch hazards.
- Branch prediction.
- Stalls.
- Flushes.
- Multiple functional units.
- ISA extensions.
- Performance counters.

---

# 41. Potential Extension Path

A logical progression from the current codebase is:

```text
Current
  |
  v
Single-cycle RV32I
  |
  v
Add pipeline registers
  |
  v
5-stage pipeline
  |
  +--> Data forwarding
  |
  +--> Load-use hazard detection
  |
  +--> Branch handling
  |
  v
Branch prediction
  |
  +--> Static prediction
  |
  +--> One-bit predictor
  |
  +--> Two-bit predictor
  |
  +--> GShare
  |
  v
Performance analysis
```

Separately, the ISA registry allows:

```text
RV32I
  |
  +--> RV32M
  |
  +--> Custom instructions
  |
  +--> Floating point
  |
  +--> Vector operations
```

---

# 42. The Most Important Design Insight

The entire project can ultimately be reduced to this conceptual pipeline:

```text
                    program.s
                       |
                       v
                +--------------+
                |   Assembler  |
                +------+-------+
                       |
                       v
                 Machine Code
                       |
                       v
                    Memory
                       |
                       v
                    FETCH
                       |
                       v
                   32-bit word
                       |
                       v
                   DECODE
                       |
                       v
              Instruction Fields
                       |
                       v
                ISA Registry
                       |
                       v
                Execute Function
                       |
          +------------+------------+
          |            |            |
          v            v            v
         ALU         Memory       Branch
          |            |            |
          +------------+------------+
                       |
                       v
                    next_pc
                       |
                       v
                       PC
                       |
                       +-----------> Next Cycle
```

The key relationship is:

\[
\boxed{
\text{Assembly}
\rightarrow
\text{Encoding}
\rightarrow
\text{Decoding}
\rightarrow
\text{Execution}
\rightarrow
\text{Architectural State}
}
\]

That is essentially the complete lifecycle of a RISC-V instruction inside this simulator.

---

# 43. Recommended Way to Study the Code

For students, I would recommend following the project in this order:

### Stage 1 — Understand the state

Study:

```text
cpu_state.h
cpu_state.c
memory.h
memory.c
```

Questions to answer:

- What constitutes processor state?
- How are registers represented?
- How is `x0` enforced?
- How is memory represented?
- How are words loaded/stored?

### Stage 2 — Understand instruction representation

Study:

```text
isa_registry.h
isa_registry.c
```

Questions:

- What information uniquely identifies an instruction?
- Why are `opcode`, `funct3`, and `funct7` needed?
- How are function pointers used?

### Stage 3 — Understand decoding

Study:

```text
decoder.h
decoder.c
```

Questions:

- How are fields extracted?
- How are immediates reconstructed?
- How does signed extension work?
- How is the instruction matched to the ISA registry?

### Stage 4 — Understand execution

Study:

```text
executor.h
executor.c
```

Questions:

- How does fetch work?
- When is `next_pc` initialized?
- How does the handler get selected?
- When is the PC committed?

### Stage 5 — Understand RV32I

Study:

```text
rv32i_base.c
```

Implement or trace:

```text
add
addi
lw
sw
beq
jal
jalr
```

before looking at every remaining instruction.

### Stage 6 — Understand assembly

Finally study:

```text
assembler.c
```

and trace:

```text
label
   |
   v
address calculation
   |
   v
immediate encoding
   |
   v
machine instruction
```

---

# 44. A Particularly Useful Exercise

A good lab exercise would be to take:

```asm
add t0, t1, t2
```

and manually document every transformation:

```text
Assembly:

add t0, t1, t2

        |
        v

Fields:

opcode = ...
rd     = ...
rs1    = ...
rs2    = ...
funct3 = ...
funct7 = ...

        |
        v

32-bit encoding:

................................
................................
................................
................................

        |
        v

Memory:

[address] = instruction

        |
        v

Fetch

        |
        v

Decode

        |
        v

ISA Registry

        |
        v

exec_add()

        |
        v

R[t0] = R[t1] + R[t2]

        |
        v

PC = PC + 4
```

Doing the same exercise for:

```text
addi
lw
sw
beq
jal
jalr
```

will give students a very strong understanding of the complete RISC-V instruction lifecycle.

---

# 45. Final Summary

The project is best understood not as a collection of C files, but as a software implementation of a processor.

The major mapping is:

```text
C Code                       Processor Concept
-------------------------------------------------------
CPUState                     Architectural state
regs[]                       Register file
pc                           Program counter
next_pc                      Next-PC logic
Memory                       Main memory
memory_read_word()           Memory read
memory_write_word()          Memory write
decoder.c                    Instruction decoder
isa_registry.c               Instruction dispatch
Function pointers             Instruction control
rv32i_base.c                 ISA execution logic
assembler.c                  Assembly toolchain
execute_cycle()              Processor cycle
```

The most important execution path is:

```text
              Assembly
                 |
                 v
             Assembler
                 |
                 v
            Machine Code
                 |
                 v
               Memory
                 |
                 v
              Fetch
                 |
                 v
              Decode
                 |
                 v
          ISA Registry
                 |
                 v
             Execute
          /      |      \
        ALU    Memory   Branch
          \      |      /
                 v
              next_pc
                 |
                 v
                 PC
                 |
                 +------> Next instruction
```

Once this structure is understood, the rest of the project becomes much easier to read.

More importantly, it gives a direct conceptual path from **RISC-V assembly → instruction encoding → processor datapath → microarchitecture**, which makes this codebase a good foundation for progressively introducing pipelining, hazards, branch prediction, and other advanced computer architecture concepts.
