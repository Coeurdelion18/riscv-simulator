# Evaluation compatibility: verified proposal

The parent `riscv_sim_base/` has not been changed. All implementation trials,
builds and generated outputs were confined to `student_package/`.
The original `student_package/riscv_sim_base.zip` was used as the starting
point and has not been overwritten. Its C/header/Makefile contents matched
the parent simulator before the trial.

## 1. What the evaluator actually requires

These requirements come from `README.md`, `evaluate.py`, `ref_ui/cli_display.h`,
`ref_ui/cli_display.c`, `sample.s`, and `sample.txt` in this package.
The submission ZIP is our own submission, not supplied starter code.

### Submission layout and build

- The evaluator accepts a source directory or a ZIP. A ZIP should contain a
  Makefile at its root, or one project directory containing the Makefile.
  If there are multiple immediate subdirectories with Makefiles, root
  discovery fails. Deeper nesting is not searched.
- Recommended layout: `riscv_sim_base/Makefile`, `main.c`, `core/`, `isa/`,
  `assembler/`, `pipeline/`, `ui/`, `tests/`, and README.
- The archive must not contain paths that escape the extraction directory.
- It must build from source with plain `make`, without files outside the
  submitted project. Linux output must be an executable named `rvsim`.
- Python 3.8+, gcc and make are the stated evaluation tools. The evaluator
  itself uses Python's standard library only. The simulator uses standard
  C headers and libm, already linked by the Makefile.
- The evaluator copies the submission into a temporary build directory.
  It skips `.o`, `.d`, `.a`, `.so`, `rvsim`, `rvsim.exe`, `.git`, and
  `__MACOSX`. Prebuilt artifacts cannot satisfy the build requirement.
- The script removes the ENTIRE submitted `ui/` directory and replaces it
  with `ref_ui/`. Required engine code cannot live only in our UI.
- The build has a 180-second timeout. Each simulator run has a 10-second
  timeout and receives no keyboard input.

### Engine contract required by the reference UI

`ref_ui/cli_display.h` includes `cpu_state.h`, `memory.h`, and `sim_engine.h`.
Only `sim_engine.h` was missing. All referenced CPU/memory functions and
other quoted headers exist in the current simulator.

The UI establishes the following necessary contract:

- `SimEngine`: accessible `name`, `cpu`, and `mem` fields.
- `step(engine)`: executes one cycle of the selected engine.
- `stage_views(engine, views, capacity)`: fills stage descriptions and returns
  their count, within the supplied capacity.
- `format_stats(engine, buffer, capacity)`: optional callback producing bounded,
  null-terminated statistics text. The UI supplies 1024 bytes.
- `PipelineStageView`: fields `label`, `pc`, and `is_bubble`.
- `SIM_MAX_STAGES`: sufficient capacity for the five-stage pipeline.
- UI entry points: `run_repl(SimEngine *)` and
  `run_batch(SimEngine *, uint64_t max_cycles, int trace)`.

No definition or constructor for this interface is supplied. The four local
handouts do not name these types or this header. Their precise definitions
must therefore be implemented from the reference UI's observed usage. Our
adapter also uses a private-state pointer; that is our implementation choice.

### Execution, CLI, and output

- The script runs `rvsim program.s --batch` plus options from the first
  `# RUN:` header in the assembly source. An optional label is discarded;
  explicit `--batch` and `--trace` tokens are stripped from that header.
  Later RUN headers are ignored by this evaluator.
- Defaults: forwarding pipeline and two-bit predictor.
- Supported options: `--single-cycle`, `--hazard none|stall|forward`,
  `--predictor not-taken|btfn|1bit|2bit`, and `--max-cycles N`.
- Pipeline statistics must remain engine-owned and use the reference
  `name = value` form. Ref UI asks the engine to format them.
- Trace output is one stage-view entry per cycle on stdout in the reference
  UI. Program print syscalls can appear immediately before a trace entry
  on the same line, since those syscalls do not add a newline.
- The reference UI's cycle ceiling is absolute: it runs while
  `cpu->cycle_count < max_cycles`. A zero ceiling executes zero cycles.
- Interactive UI supports step, run, regs, pipe, stats, memory inspection,
  setting/clearing breakpoints, status, help and quit.

### What is graded

- Integer state: 31 registers x1-x31 plus PC, for 32 values. x0 is excluded.
- Pipeline counters: cycles, retired, stalls, branches, mispredicts, jal,
  jalr, redirects and squashed, when present in the expected dump.
- Single-cycle expected dumps contain only the cycles counter.
- Cycles are parsed from the `status = ... cycles = ...` line.
- Forwards, CPI and accuracy are not graded. FP registers, fcsr, memory
  contents and printed syscall text are not graded by this Lab 4 script.
- A different final status produces a note. Register/counter mismatches do
  not make the evaluator exit nonzero: inspect percentages and notes, not
  just the process exit code.
- A nonzero simulator exit, crash or timeout produces zero percentages.
- The announced final evaluation has five hidden programs. Their sources
  and expected dumps are not supplied; passing the sample cannot prove
  their exact counter expectations.

### Supplied sample's exact targets

- Printed sum: 121. Exit status: ECALL_EXIT. Final PC: 0x0000007c.
- 68 cycles, 53 retired, 5 stalls, 5 branches, 2 mispredicts, 1 jal, 1 jalr,
  3 redirects, 6 squashed. All integer registers must match sample.txt.
- Exercises forwarding, load-use stalls, loop prediction, calls/returns,
  multiplication, signed division/remainder and division by zero.

## 2. Proposed implementation, file by file

### New: core/sim_engine.h

Define the UI-independent engine contract listed above. Set SIM_MAX_STAGES
at five. Include only CPU, memory and standard size definitions. Provide the
single-cycle adapter initializer declaration. Keep this outside ui/ so the
evaluator cannot delete it when installing its reference UI.

### New: core/sim_engine.c

Implement the single-cycle adapter by calling the existing execute_cycle().
Expose a one-stage view and no pipeline-statistics callback. CPU ownership,
memory ownership, decoder and instruction handlers remain unchanged.

### Modify: pipeline/pipeline_state.h

Include the engine interface. Introduce named HAZARD_NONE, HAZARD_STALL and
HAZARD_FORWARD modes, replacing the boolean forwarding selector. Declare
the pipeline adapter initializer. Add a MEM/WB syscall-number field so the
system instruction's EX-stage inputs reach WB without being re-read later.
Existing stage contents, FP source metadata and fcsr snapshots are retained.

### Modify: pipeline/pipeline.c

1. Add adapters for one pipeline cycle, five stage views, and statistics.
   Respect stage-view capacity and use snprintf for bounded formatting.
2. Move the existing statistics text into the formatter. Keep all nine
   graded counters and their existing calculation; do not retune timings.
3. Retain pipeline_run_batch as a wrapper through the shared engine/UI,
   ensuring tracing is emitted once per cycle.
4. On exit or a committed fault, publish the retiring/faulting instruction's
   PC, rather than the speculative fetch address. This fixes the sample's
   former 0x8c-versus-0x7c mismatch without adding/removing cycles.
5. Carry the syscall argument and number through the pipeline and execute
   using those captured/forwarded inputs. With hazard handling disabled,
   dependency errors must remain visible rather than being silently healed
   by re-reading newer registers at WB. Invoke the syscall on a CPU snapshot
   so stale operands do not overwrite architectural registers.
6. Preserve existing branch prediction, redirects, precise halting,
   wrong-path suppression and committed-fcsr rollback.

### Modify: pipeline/hazard.c

Return no stalls for HAZARD_NONE and perform no forwarding. Retain the
current dependency-stall policy for HAZARD_STALL and current load-use /
operand-forwarding policy for HAZARD_FORWARD. This adds the missing CLI
mode without changing the safe modes' algorithms.

### Modify: main.c

Construct the appropriate engine and pass it to the shared run_batch or
run_repl functions. Keep existing defaults and aliases:

- `--batch`: forwarding + two-bit pipeline.
- `--batch --single-cycle`: single-cycle engine.
- `--pipeline`: batch + stall; `--pipeline-forward`: batch + forwarding.
- No batch option: existing single-cycle interactive debugger.

Accept hazard none, retain all four predictors, reject conflicting/invalid
execution options, and validate numeric limits for malformed, negative,
overflowing or out-of-range values. Preserve --mem-size. Document trace
stdout and the reference UI's zero-cycle limit behavior.

### Modify: ui/cli_display.h and ui/cli_display.c

Adopt the reference engine-based signatures and add its pipe/stats/stage
view facilities. Use the same generic cycle loop for both engines. Retain
our FP register and fcsr display in our own print_registers implementation.
The evaluator's integer-only UI can then replace ours without changing any
execution behavior. FP output is absent with the reference UI, but FP
execution remains available.

### Modify: Makefile

Compile and link core/sim_engine.c, add the new header dependency, and let
the existing source-derived clean rule remove its object. Preserve the
Linux rvsim target, Windows rvsim.exe target, compiler options and libm.
The Windows evaluation workaround described below is test infrastructure,
not a proposed simulator/build change.

### Modify: README.md

Document actual current CLI options, shared engine structure, three hazard
policies, trace stream, cycle limits, and the distinction between current
prediction-enabled execution and historical Lab 2/3 timings. Retain the
original skeleton documentation, clearly marked historical.

No changes are proposed to assembler, decoder, instruction registry,
RV32I/M/F handlers, FP helpers, CSR implementation, or predictor algorithms.
No changes are proposed to the evaluator, reference UI, sample inputs or
expected outputs.

## 3. Verification completed in the trial

### Submission evaluator

The unchanged evaluate.py ran against the final candidate source ZIP,
installed the unchanged reference UI, built from source and reported:

```
register state match= 100% ,counter match = 100%
RAW reg=32/32 cnt=9/9
```

See compatibility_trial/evaluator_result.txt.

This was run with native Windows gcc/make. A temporary make launcher copied
rvsim.exe to an extensionless rvsim after building, solely because the Linux
oriented evaluator checks that literal path. No evaluator logic, source
Makefile or simulator behavior was altered for that workaround. Linux/WSL
execution itself was not verified; no usable WSL distribution was listed.
The source retains its existing Linux build branch.

### Sample programs and golden files

- All 15 sample_tests programs produce their documented printed outputs
  under the current safe default.
- All 15 Lab 4 and all 8 Lab 5 golden dumps match the relevant current
  engine/predictor modes, including FP registers and fcsr in our UI.
- All 18 Lab 2/3 golden dumps match in a temporary historical verification
  build with IF prediction disabled. This includes the three deliberately
  wrong hazard-none goldens. The temporary build was removed afterward.
- These constitute all 41 supplied golden dumps, tested against their
  respective execution assumptions. They are not all the output of one
  modern default configuration.
- In the production configuration, 35/41 match all recorded counters and
  40/41 match architectural state. The six timing differences are the three
  Lab 2 pipeline cases and three h3_kernel pipeline modes. The remaining
  state difference is the deliberately incorrect h3_kernel.none result,
  whose dependency errors vary when prediction changes instruction timing.
  Do not change the required two-bit default or safe-mode logic to force
  historical no-prediction timings.

### Cross-engine and reference-UI matrix

18 existing programs: 15 sample_tests, two original tests, evaluator sample.

- 324 executions: each program in single-cycle plus both safe hazard modes
  crossed with all four predictors, using our UI and the reference UI.
  Full integer/FP/fcsr state matches the single-cycle oracle in our UI;
  reference UI agrees on integer state, PC, status and all graded counters.
- 144 additional executions: hazard none with all four predictors and both
  UIs. They terminate consistently, with zero dependency stalls/forwarding.
  Their intentionally unsafe results are not required to match single-cycle.
- 36 focused executions: illegal instruction, load fault, wrong-path illegal
  instruction and younger instruction after exit, in single-cycle and all
  eight safe pipeline configurations. Final state/status/PC match.
- Engine callback checks: capacity limits, stage bubbles, one-cycle stepping,
  and bounded statistics formatting.
- CLI checks: 13 invalid combinations/values, six cycle-limit cases (0, 1, 5
  in each engine), both old aliases, trace count/state consistency and an
  interactive debugger smoke test using each UI.
- make clean, make and make test pass in the final trial. make test covers
  the two original tests; the expanded checks above were run separately.

Some counters such as forwards are explicitly excluded from final grading,
but supplied golden counters were still compared in the golden checks.
This is regression evidence, not a substitute for the five unavailable
hidden programs or an independent RARS run.

## 4. Artifacts and approval boundary

- compatibility_trial/riscv_sim_base/: inspectable proposed source tree.
- compatibility_trial/riscv_sim_compatible.zip: source-only candidate ZIP.
- compatibility_trial/proposed_changes.patch: exact diff against parent.
- compatibility_trial/evaluator_result.txt: final official sample result.
- compatibility_trial/golden_results.json: modern-mode golden differences.
- compatibility_trial/historical_results.json: historical verification.
- compatibility_trial/matrix_summary.json: additional verification counts.
- compatibility_trial/parent_hashes.json: initial parent snapshot hashes.

Temporary custom fixtures, harness source and testing builds were removed.
Parent file hashes and the file list were checked after testing and match
the initial snapshot. The original submission ZIP and evaluation files
remain untouched.

After approval, apply only the ten listed source/documentation files (two
new and eight modified) to the parent, rebuild cleanly, rerun evaluation and
regressions, then create a fresh submission ZIP. Do not copy temporary
outputs, test adapters or build products into the parent source.
