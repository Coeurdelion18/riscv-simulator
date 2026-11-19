**The Evaluation Process:** 
Step 1: Code similarity checking using the code similarity checker "MOSS" 
Step 2: Automatic evaluation of assignment using "evaluator.py"
Step 3: Marks announcements
Step 4: Clarrification of the marks, if any, during the announced time slot in-person. 

**Code similarity will be checked across the groups using the following:**
https://github.com/abdelhalimyasser/node-moss

# Lab 4 Self-Check Evaluator

This package lets you check, **before you submit**, that your simulator
will work with the automated final evaluation, and how closely it matches
the reference implementation.

The final evaluation uses **exactly the same script and procedure** as this
package. The only difference is that it runs **5 hidden test programs**
(`.s` + expected `.txt` pairs) instead of the one sample given here.

## Contents

| File | Purpose |
|------|---------|
| `evaluate.py` | The evaluator (Python 3.8+, standard library only) |
| `ref_ui/` | Reference `ui/` folder. It replaces "cli_display.c" and cli_display.h" in the "ui" folder in your base simulator during evaluation (see below) |
| `sample.s` | Sample test program (Lab 4 level: hazards, branches, `jal`/`jalr`, M extension) |
| `sample.txt` | Expected output of `sample.s`, produced by the reference simulator |

## Requirements to run the "evaluate.py"

Linux (or WSL on Windows) with `python3`, `gcc` and `make`. 

## Usage

```bash
python3 evaluate.py <my_submission.zip> sample.s sample.txt --verbose
```
the switch "--verbose" displays all the registers and counters that does not match the entries in "sample.txt"   

## What the evaluator does

1. **Builds from source.** It copies your code to a temporary folder,
   leaving your own folder untouched. It then deletes any `*.o`, `*.d` and
   `rvsim` files, replaces `ui/` with `ref_ui/`, and runs `make`.
   If that fails, it prints:
   ```
   Cannot build from source
     reason: make failed:
       main.c:124:1: error: ...
   ```
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

- **register state match**: the percentage of the 32 values `x1`–`x31` and
  `pc` that equal the expected values. `x0` is not counted because it is
  always 0.
- **counter match**: the percentage of these pipeline counters that equal
  the expected values:
  `cycles, retired, stalls, branches, mispredicts, jal, jalr, redirects, squashed`.
  - `forwards`, `CPI` and `accuracy` are **not** graded.
  - With `--single-cycle`, only `cycles` is compared.

If the final `status` differs from the expected one (e.g. you end in
`ILLEGAL_INSTRUCTION` instead of `ECALL_EXIT`), a `note:` line is printed
below the result.

If the simulator crashes, runs longer than **10 seconds**, or exits with an
error (for example an assembly error because an instruction is not
implemented), the result is
`register state match= 0% ,counter match = 0%`, followed by the reason.

## Checklist: is my submission fit for automated evaluation?

- [ ] There is a `Makefile` at the top of the folder (or at the top of the
      single folder inside your zip), and a plain `make` builds an
      executable called **`rvsim`** in that same folder.
- [ ] It builds from a clean copy. Do not rely on `.o` files or a
      pre-built `rvsim`, because they are deleted before building. Do not
      rely on files outside your folder or on extra libraries.
- [ ] It builds with the **reference `ui/`**. Your `ui/` is replaced, so
      any changes you made there are lost. Keep the `SimEngine` interface
      (`core/sim_engine.h`) that `ui/` uses.
- [ ] The command-line options match the handout:
      `--batch`, `--single-cycle`, `--hazard none|stall|forward`,
      `--predictor not-taken|btfn|1bit|2bit`, `--max-cycles N`.
- [ ] `--batch` never waits for keyboard input and finishes well within 10 s.
- [ ] The statistics are printed **exactly in the reference format**, one
      `name = value` per line (this text is printed by `pipeline/pipeline.c`,
      not by `ui/`, so it is your responsibility):
      ```
      retired         = 53
      stalls          = 5
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
or by hand, and check the cycle counts with `--batch --trace`.

## Writing your own test programs

Add a `# RUN:` line with the options to use:

```asm
# RUN: --hazard stall --predictor btfn
```

Without one, the simulator's defaults are used (`--hazard forward --predictor 2bit`).
The evaluator needs a corresponding "sample.txt` to compare against, so for your own
programs you must work out the expected values yourself and properly place those in "sample.txt". 
