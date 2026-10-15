# RV32I Base Simulator

A single-cycle RV32I RISC-V simulator, assembler, and CLI debugger written in
portable C11, designed as the starting point for a semester of computer
architecture labs (pipelining, hazard detection, branch prediction, the M/F
extensions, and a custom-extension capstone).

Nothing in this handout needs to be modified by students. Every lab extends
the system by **adding new files** that plug into the interfaces described
below.

## Why it's built this way

The whole codebase hinges on one idea: instructions are not hardcoded into
the simulator. They're entries in a shared **ISA registry**
(`core/isa_registry.h`) — a table of `{mnemonic, opcode, funct3, funct7,
format, execute-function}` structs. The decoder looks instructions up in
this table by their bit pattern; the assembler looks the same table up by
mnemonic. Because both share one source of truth, adding an instruction
(e.g. `MUL` in the M-extension lab) means writing one new file that calls
`register_instruction(...)` — the core simulator, decoder, and assembler
never need to change.

This is also how "detect an instruction not in the library" works for free:
if the decoder can't find a matching entry, it reports `ILLEGAL_INSTRUCTION`
with the offending opcode and PC.

## Directory structure

```
riscv_sim_base/
├── core/                   # engine internals — do not modify in labs
│   ├── cpu_state.h/.c      # register file, PC, CPUStatus (RUNNING/HALTED/...)
│   ├── memory.h/.c         # flat byte-addressable memory, little-endian load/store
│   ├── isa_registry.h/.c   # the instruction dispatch table (the plugin mechanism)
│   ├── decoder.h/.c        # raw 32-bit word -> DecodedInstr, via the registry
│   └── executor.h/.c       # single-cycle fetch/decode/execute loop
│
├── isa/
│   └── rv32i_base.h/.c     # base RV32I instructions, registered at startup
│                           # (Lab 4 adds isa/rv32m.c, Lab 5 isa/rv32f.c, etc.)
│
├── assembler/
│   └── assembler.h/.c      # two-pass assembler; shares core/isa_registry.h
│                           # with the simulator, so new instructions are
│                           # automatically assemblable once registered
│
├── ui/
│   └── cli_display.h/.c    # register/memory/status display + step-debugger REPL
│
├── main.c                  # wiring: registers the ISA, assembles, runs
├── Makefile
├── tests/                  # example programs (also used by `make test`)
│   ├── sample1_sum_loop.s
│   └── sample2_mem_and_branches.s
└── README.md
```

### Where later labs' files go

The base system doesn't include a pipeline yet (Lab 1 builds it), so there's
no `pipeline/` directory in this handout. When it's introduced, the expected
shape is:

```
pipeline/
├── pipeline_state.h/.c         # IF/ID, ID/EX, EX/MEM, MEM/WB latches (Lab 1)
├── hazard_interface.h          # HazardDetector function-pointer contract
├── branch_predictor_interface.h# BranchPredictor function-pointer contract
├── hazard_naive.c              # default: always-stall (shipped with Lab 1)
├── hazard_forwarding.c         # Lab 2 deliverable
└── branch_predictor_*.c        # Lab 3 deliverable(s)
```
and the M/F/custom extensions each add one file under `isa/`:
```
isa/rv32m.c            # Lab 4: MUL/DIV family + overflow handling
isa/rv32f.c             # Lab 5: single-precision floating point
isa/custom_ext.c        # Lab 6: student-designed instruction(s)
```
Each of those is registered with one extra line in `main.c`
(`register_rv32m();`, etc.) — nothing else changes.

## Building

Requires `gcc` (or any C11 compiler) and `make`. No external dependencies.

```bash
cd riscv_sim_base
make
```

This produces a `rvsim` binary in the project root. `make clean` removes
build artifacts.

## Running

```bash
./rvsim <program.s>              # interactive step-debugger (default)
./rvsim <program.s> --batch      # run to completion, dump final state (for grading scripts)
./rvsim <program.s> --batch --max-cycles 500000
./rvsim <program.s> --mem-size 2097152   # override the default 1 MB memory
```

Try it on the provided sample programs:

```bash
./rvsim tests/sample1_sum_loop.s --batch
./rvsim tests/sample2_mem_and_branches.s --batch
```

or run both as a quick smoke test:

```bash
make test
```

### Interactive mode

```
$ ./rvsim tests/sample1_sum_loop.s
RISC-V single-cycle simulator -- interactive mode ('help' for commands)
status = RUNNING   cycles = 0
(sim) step 5
status = RUNNING   cycles = 5
(sim) regs
---------------------------- registers ----------------------------
...
(sim) mem 0x38 16
------------------------ memory 0x00000038 --------------------------
...
(sim) break 0x34
breakpoint set at 0x00000034
(sim) run
hit breakpoint at 0x00000034
(sim) quit
```

Commands: `step [n]`, `run`, `regs`, `mem <addr> <len>`, `break <addr>`,
`clearbreak <addr>`, `status`, `help`, `quit`/`exit`.

## Assembly language supported

**Base RV32I:** `add sub sll slt sltu xor srl sra or and` `addi slti sltiu
xori ori andi slli srli srai` `lb lh lw lbu lhu` `sb sh sw` `beq bne blt bge
bltu bgeu` `jal jalr lui auipc` `ecall`

**Pseudo-instructions:** `nop`, `ret`, `j label`, `mv rd, rs`, `li rd, imm`
(always expands to `lui`+`addi`, i.e. always 8 bytes, even for small
constants — keeps the assembler's address bookkeeping simple; `li` also
accepts a label, which loads that label's absolute address).

**Directives:** `.text`, `.data`, `.globl name` (section/visibility markers
— accepted but not enforced; there is a single flat address space), `.word
v1[, v2...]`, `.half v1[, v2...]`, `.byte v1[, v2...]` (each operand may be
a numeric literal or a label), `.align n` (pads the current address up to
the next multiple of `n`).

**Registers:** both `x0`–`x31` and ABI names (`zero ra sp gp tp t0-t6 s0-s11
a0-a7`, plus `fp` as an alias for `s0`).

**Syscall convention (`ecall`)**, deliberately minimal — extend freely:
- `a7 = 1`: print the signed integer in `a0` to stdout
- `a7 = 10`: exit (halts the machine with status `ECALL_EXIT`)
- anything else: ignored, with a warning printed to stderr

**Program entry point:** the assembler places code starting at address 0
and starts execution at the `main:` label if one exists, otherwise address
0.

## Error handling conventions (for anyone extending this code)

- No exceptions in C: every fallible function returns a status enum
  (`MemStatus`, `DecodeStatus`, `AsmStatus`, `CPUStatus`) instead. Check
  return values; don't call `exit()` from `core/`, `isa/`, or `assembler/` —
  only `main.c` and the CLI decide to terminate.
- Unknown instructions are a normal, expected outcome (not a crash): decode
  failure sets `CPUStatus = CPU_ILLEGAL_INSTRUCTION` with the offending
  opcode and PC recorded for diagnostics.
- Out-of-bounds memory accesses return `MEM_ERR_OUT_OF_BOUNDS` rather than
  segfaulting; the executor turns that into `CPU_MEM_FAULT`.
- Assembler errors (`AsmError`) always carry a source line number — keep
  this up whenever you add new directives or pseudo-instructions.

## A note on `valgrind`

If your course requires memcheck-clean submissions from students (recommended
starting with the pipelining lab, where manual memory management gets more
involved), a target is provided:

```bash
make memcheck PROG=tests/sample1_sum_loop.s
```

This requires `valgrind` to be installed locally (not available in every
sandboxed environment, including the one this code was authored in — it
wasn't possible to verify this target here, so please run it once locally
before relying on it for grading).

## Extending the ISA registry (what a lab actually looks like)

Adding an instruction is: write an `execute` function with the signature
`void fn(CPUState *cpu, Memory *mem, DecodedInstr *instr)`, then register it:

```c
static void exec_mul(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    int32_t a = (int32_t)cpu_read_reg(c, i->rs1);
    int32_t b = (int32_t)cpu_read_reg(c, i->rs2);
    cpu_write_reg(c, i->rd, (uint32_t)(a * b));
}

void register_rv32m(void) {
    /* opcode 0x33, funct3 0x0, funct7 0x01 is MUL's real RV32M encoding */
    register_instruction((InstructionHandler){"mul", 0x33, 0x0, 0x01, FMT_R, exec_mul});
}
```

Then in `main.c`, alongside `register_rv32i_base();`, add `register_rv32m();`.
The assembler picks up the new mnemonic automatically — no assembler changes
required.
