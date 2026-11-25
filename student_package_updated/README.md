# Lab 5 Self-Check Evaluator

This package lets you check, **before you submit**, that your simulator
will work with the automated final evaluation, and how closely it matches
the reference implementation.

The final evaluation uses **exactly the same script and procedure** as this
package. The only difference is that it runs **5 hidden test programs**
(`.s` + expected `.txt` pairs) instead of the one sample given here.
**2 of the 5 hidden tests use the F extension.**

## ⚠️ What changed since the previous (Lab 4) package

If you downloaded the earlier package, **delete it and use this one**: the
old `evaluate.py`, `ref_ui/` and `sample.*` no longer match the final
evaluation.

| | Old package (Lab 4) | **This package (Lab 5)** |
|---|---|---|
| Graded level | Lab 4 (RV32IM + pipeline) | **Lab 5 (RV32IMF + Zicsr + pipeline)** |
| `ref_ui/` | prints x registers only | **also prints `f0`–`f31` and `fcsr`** (see below) |
| Building | any Lab 4 tree builds | **your tree must provide the FP register file and `fcsr`** (see the checklist) |
| Registers graded | `x1`–`x31` + `pc` | `x1`–`x31` + `pc`, **plus `f0`–`f31` + `fcsr` in FP tests** |
| Register marks in final grading | all-or-nothing per test, over `x1`–`x31` + `pc` | all-or-nothing per test (unchanged), **over the registers selected by `# GRADE:`** |
| New test header | — | **`# GRADE: int` or `# GRADE: int+fp`** |
| `sample.s` | hazards, branches, M | **+ F extension (arithmetic, FMA, conversions, NaN, fcsr flags)** |
| New folder | — | **`lab5_skeleton/`**: the Lab 5 file structure with empty logic |
| Counters graded | unchanged | unchanged |

### The new register dump (from `ref_ui/`)

```
---------------------------- registers ----------------------------
x0 /zero=0x00000000  x8 /s0  =0x00000000  ...
...
------------------------- fp registers ----------------------------
f0 /ft0 =0x00000000  f8 /fs0 =0x3fc00000  f16/fa6 =0x00000000  f24/fs8 =0x7fc00000
...
f7 /ft7 =0x00000000  f15/fa5 =0x00000000  f23/fs7 =0x00000000  f31/ft11=0x00000000
fcsr =0x00000011  (frm=0  flags: NV NX)
pc = 0x000000c8
status = ECALL_EXIT   cycles = 85
```

FP registers are printed as raw IEEE 754 bit patterns (hex). For example,
`0x7fc00000` is the canonical NaN, which RISC-V requires for every NaN
result.

## Contents

| File | Purpose |
|------|---------|
| `evaluate.py` | The evaluator (Python 3.8+, standard library only) |
| `ref_ui/` | Reference `ui/` folder (Lab 5). It replaces yours during evaluation (see below) |
| `sample.s` | Sample test program (Lab 5 level: hazards, branches, `jal`/`jalr`, M and F extensions) |
| `sample.txt` | Expected output of `sample.s`, produced by the reference simulator |
| `lab5_skeleton/` | Complete Lab 5 directory/file structure. The logic you write is left empty (see below) |

## Requirements

Linux (or WSL on Windows) with `python3`, `gcc` and `make`. These are the
same tools you already use to build the simulator.

## Usage

```bash
python3 evaluate.py <your_source_folder> sample.s sample.txt
```

`<your_source_folder>` is the folder that contains your `Makefile`. You can
also pass the `.zip` file you plan to submit:

```bash
python3 evaluate.py my_submission.zip sample.s sample.txt
```

Add `--verbose` (or `-v`) to list every register and counter that does not match:

```bash
python3 evaluate.py ~/lab5 sample.s sample.txt --verbose
```

## What the evaluator does

1. **Builds from source.** It copies your code to a temporary folder,
   leaving your own folder untouched. It then deletes any `*.o`, `*.d` and
   `rvsim` files, replaces `ui/` with `ref_ui/`, and runs `make`.
   If that fails, it prints:
   ```
   Cannot build from source
     reason: make failed:
       ui/cli_display.c:25:53: error: 'CPUState' has no member named 'fcsr'
   ```
   (That particular error means your tree has no FP register file / `fcsr`
   yet; see the checklist.)
2. **Runs your simulator** headless on the test program:
   ```
   ./rvsim sample.s --batch <options from the RUN line>
   ```
   The options come from the `# RUN:` comment at the top of the `.s` file,
   e.g. `# RUN: --hazard forward --predictor 2bit`. Hidden tests may use any
   `--hazard` / `--predictor` combination, or `--single-cycle`.
3. **Compares** your final state with the expected `.txt` and prints:
   ```
   register state match= 100% ,counter match = 100%
   ```

### How the two percentages are computed

- **register state match**: the percentage of the graded registers that
  equal the expected values (shown so you can see how close you are).
  **In the final marking, registers are all-or-nothing per test:** a test
  scores register marks only if **every** graded register matches, so
  anything below 100% means 0 register marks for that test.
  **NEW:** which registers are graded is set by the `# GRADE:` line of the test:

  | `# GRADE:` | Graded registers | Count |
  |---|---|---|
  | `int` (default) | `x1`–`x31`, `pc` | 32 |
  | `int+fp` | `x1`–`x31`, `pc`, `f0`–`f31`, `fcsr` | 65 |

  FP registers are graded **only in FP tests** (`int+fp`). An integer-only
  test needs its 32 values right, and an FP test needs all 65 right
  (including `fcsr`). `x0` is never counted.
  `--verbose` prints `graded registers: ...` so you can see which set was used.
- **counter match** (unchanged, proportional in the final marking too): the percentage of these pipeline counters
  that equal the expected values:
  `cycles, retired, stalls, branches, mispredicts, jal, jalr, redirects, squashed`.
  - `forwards`, `CPI` and `accuracy` are **not** graded.
  - With `--single-cycle`, only `cycles` is compared.

If the final `status` differs from the expected one (e.g. you end in
`ILLEGAL_INSTRUCTION` instead of `ECALL_EXIT`), a `note:` line is printed
below the result.

If the simulator crashes, runs longer than **10 seconds**, or exits with an
error, the result is
`register state match= 0% ,counter match = 0%`, followed by the reason.
An instruction you have not implemented makes the program fail to
assemble, so that test scores 0%. The other tests are not affected:
F instructions appear only in the FP tests, and M instructions only in
the M test.

## The `lab5_skeleton/` folder

It is the **complete Lab 5 tree** (`core/ isa/ assembler/ ui/ pipeline/
main.c Makefile`). It builds warning-free and runs. Everything you are
not supposed to write is complete:

- `core/`: includes the **FP register file and `fcsr`** that the new UI needs
  (`cpu_read_freg`, `cpu_write_freg`, `cpu_freg_abi_name`, `cpu_frm`,
  `cpu_raise_fflags`, `FFLAG_*`, `FCSR_*`)
- `assembler/`: `.float`, FP/CSR pseudo-ops, optional rounding-mode operand
- `ui/`: identical to `ref_ui/`
- `isa/rv32i_base.c`, `main.c`, `Makefile`, all headers/interfaces
- the **registration tables** in `isa/rv32m.c`, `isa/rv32f.c`,
  `isa/zicsr.c`, so every instruction already assembles and decodes

The logic is left empty, marked `TODO(labN)`:

| File | What is empty |
|---|---|
| `pipeline/pipeline.c` | the five stage bodies (`stage_if/id/ex/mem/wb`), `forward_mux`, `predict_next` |
| `pipeline/control.c` | `control_from_instr()` |
| `pipeline/hazard_stall.c`, `hazard_forward.c` | the hazard / forwarding decisions |
| `pipeline/bp_static.c`, `bp_dynamic.c` | btfn, 1-bit and 2-bit predictors |
| `isa/rv32m.c` | the 8 M handlers |
| `isa/rv32f.c` | all F handlers |
| `isa/zicsr.c` | the 6 CSR handlers |

Out of the box, `./rvsim prog.s --single-cycle` runs RV32I programs
correctly, and pipeline mode prints the full Lab 5 dump (nothing retires
yet). You can either fill in the skeleton, or copy the parts you need
(especially `core/` and the FP parts of the assembler) into your own Lab 4
tree.

## Checklist: is my submission fit for automated evaluation?

- [ ] There is a `Makefile` at the top of the folder (or at the top of the
      single folder inside your zip), and a plain `make` builds an
      executable called **`rvsim`** in that same folder.
- [ ] It builds from a clean copy. Do not rely on `.o` files or a
      pre-built `rvsim`, because they are deleted before building. Do not
      rely on files outside your folder or on extra libraries.
- [ ] **NEW:** It builds with the **Lab 5 reference `ui/`**. Your `ui/` is
      replaced, so any changes you made there are lost. The reference `ui/`
      uses `core/sim_engine.h` and, new in Lab 5, `cpu_read_freg()`,
      `cpu_freg_abi_name()`, `cpu_frm()`, `cpu->fcsr` and `FFLAG_NV/DZ/OF/UF/NX`
      from `core/cpu_state.h`. A tree without these gets
      **"Cannot build from source" and 0 for every test**. `lab5_skeleton/core/`
      has them.
- [ ] The command-line options match the handout:
      `--batch`, `--single-cycle`, `--hazard none|stall|forward`,
      `--predictor not-taken|btfn|1bit|2bit`, `--max-cycles N`.
- [ ] `--batch` never waits for keyboard input and finishes well within 10 s.
- [ ] The statistics are printed **exactly in the reference format**, one
      `name = value` per line (this text is printed by `pipeline/pipeline.c`,
      not by `ui/`, so it is your responsibility. The skeleton has it):
      ```
      retired         = 69
      stalls          = 6
      branches        = 5
      mispredicts     = 2
      jal             = 1
      jalr            = 1
      redirects       = 3
      squashed        = 6
      ```
- [ ] `python3 evaluate.py <your folder> sample.s sample.txt` prints
      `register state match= 100% ,counter match = 100%`.

Getting 100% on the sample does **not** guarantee 100% on the hidden tests.
Write your own test programs too. Cross-check the register values with RARS
or by hand (RARS 1.6 deviates from the F spec for some NaN cases; the
spec wins), and check the cycle counts with `--batch --trace`.

## Writing your own test programs

Add a `# RUN:` line with the options to use and, for FP programs, a
`# GRADE:` line:

```asm
# RUN: --hazard stall --predictor btfn
# GRADE: int+fp
```

Without a `RUN` line, the simulator's defaults are used
(`--hazard forward --predictor 2bit`). Without a `GRADE` line, only the
integer registers are compared.
The evaluator needs an expected `.txt` to compare against, so for your own
programs you must work out the expected values yourself. To see the output
format, run `./rvsim prog.s --batch <options> > prog.txt`.

## Licence / tools

`sample.s` and `sample.txt` were produced with the course reference
simulator.
