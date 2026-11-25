# RV32IMF simulator

Completed Labs 2-5 on the supplied updated starter: a single-cycle reference
engine and a five-stage pipeline, with hazard plugins, branch prediction,
RV32M, RV32F and the six FP CSR instructions.

## Build and run

```sh
make clean
make
make test
./rvsim program.s --batch
./rvsim program.s --batch --single-cycle
./rvsim program.s --batch --hazard stall --predictor btfn
./rvsim program.s --batch --hazard forward --predictor 2bit
./rvsim program.s --batch --trace --max-cycles 10000
```

On Windows `make` builds `rvsim.exe` and a matching extensionless `rvsim`,
so the unchanged evaluator can locate and run its expected executable name.
On Linux it builds the native `rvsim` binary. Linux requires gcc, make and
libm; tests use Python 3. Override `PYTHON` if needed.

The defaults are the forwarding pipeline and the two-bit predictor, as in
the updated starter. Without `--batch`, the selected engine opens the
interactive debugger; use `--single-cycle` to select the reference engine.
`step`, `run`, `regs`, `pipe`, `stats`, `mem`, `break`, `clearbreak`, `status`,
`help` and `quit` are supplied by the unchanged UI.

- `--hazard none|stall|forward`: dependency handling. None deliberately
  permits stale operands and can produce wrong results, faults or loops.
- `--predictor not-taken|btfn|1bit|2bit`: the four direction predictors.
  Conditional branches use a 64-entry table; jal targets are known in IF,
  and jalr resolves in EX. A wrong next-PC prediction costs two cycles.
- `--no-predict`: historical Lab 2/3 behavior, fetching sequentially even
  for jal. Use this when comparing their old golden files. It overrides
  the selected predictor during fetch and leaves the modern default intact.
- `--pipeline`: old alias for `--batch --hazard stall`.
- `--pipeline-forward`: old alias for `--batch --hazard forward`.
- `--max-cycles N`: absolute cycle limit (default 1000000); zero runs zero
  cycles. Malformed, negative and overflowing limits are rejected.
- `--mem-size N`: simulated memory bytes (default 1048576).
- `--trace`: print stage occupancy before each batch cycle, to stdout.

## Structure

`core/` supplies the CPU, memory, registry, decoder, single-cycle executor
and SimEngine interface. `assembler/` implements the supplied assembler,
including FP operands, rounding modes, .float and CSR pseudoinstructions.
`ui/` remains identical to the updated evaluator's reference UI.

`pipeline/control.c` derives unified source/destination registers from the
registered instruction formats and operand flags. x0 is not a dependency;
f0 is a writable register. `pipeline/pipeline.c` implements the stages
using the supplied snapshot and reverse stage order. Faults and exits drain
to WB; older memory faults suppress younger EX work. fcsr is read/written
in EX in program order, so it needs no extra interlock or rollback.

`hazard_none.c`, `hazard_stall.c` and `hazard_forward.c` implement the
provided plugin contract. Forwarding prioritizes EX/MEM over MEM/WB and
stalls exactly once on load-use. All three source operands participate.
`bp_static.c` and `bp_dynamic.c` implement static, last-outcome and
saturating-counter predictors through the supplied interface.

`isa/rv32m.c` implements all eight integer M instructions. `isa/rv32f.c`
implements the 26 required FP operations plus the supplied move aliases.
`isa/fp_helpers.c/.h` carry over the previously verified host-FPU rounding,
exception, canonical-NaN and RMM helpers; no such helper files were included
in this starter. `isa/zicsr.c` implements fflags, frm and fcsr access.

Changes to otherwise supplied support code are limited to portable 32-bit
literal parsing and bounded string copies in the assembler, rejection of
unsupported R4 formats/system encodings in the decoder, CLI validation and
legacy options in main, and build/test portability in the Makefile.

## Verification

`make test` always checks the two bundled original programs. In this
repository it also discovers all 15 sibling sample_tests programs and the updated
evaluator sample (plus the earlier sample, if it is present). It checks single-cycle against stall/forward
under all four predictors, full integer/FP/fcsr state, cycle identities,
zero stalls/forwarding in bounded unsafe-mode runs, all 41 sample golden
files, and the published evaluator sample values and counters.

The Lab 2/3 goldens use --no-predict; Lab 4/5 use their specified current
modes. The three hazard-none goldens intentionally contain wrong answers.
A cycle limit in an arbitrary dependency-heavy none-hazard run is allowed;
its architectural result is not asserted to match the reference engine.

The updated evaluator replaces the entire ui/ directory and grades 65
state values for int+fp tests: x1-x31, PC, f0-f31 and fcsr. Its statistics
labels remain unchanged; extra forwarding-path/load-use counters follow.

To evaluate a source directory or source-only ZIP:

```sh
python3 student_package_updated/evaluate.py riscv_sim_base_updated \
  student_package_updated/sample.s student_package_updated/sample.txt --verbose
python3 student_package_updated/evaluate.py \
  student_package_updated/riscv_sim_base_updated.zip \
  student_package_updated/sample.s student_package_updated/sample.txt --verbose
```

Use `python` on Windows when that is the installed Python command. The
final five hidden programs and independent RARS runs are not included in
these checks; passing the published sample cannot prove hidden-test results.
