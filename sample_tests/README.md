# Sample test suite — 15 programs across the 5 labs

Three variations per lab, for testing students' simulators. Every number in
every program header was measured on the reference implementation, and all
four lab harnesses pass with these installed (RARS 1.6 cross-checks
included).

```
lab1_skeleton/   s1_alu.s  s2_memory.s  s3_control.s          (no harness: base has none)
lab2_pipeline/   p1_broad.s  p2_edge.s  p3_kernel.s           + expected/  (6 goldens)
lab3_hazards/    h1_broad.s  h2_edge.s  h3_kernel.s           + expected/ (12 goldens)
lab4_branch/     b1_patterns.s  b2_adversarial.s  b3_applied.s + expected/ (15 goldens)
lab5_fp/         f1_broad.s  f2_special.s  f3_kernel.s        + expected/  (8 goldens)
```

The three variations in each lab follow the same scheme:

| Variation | Purpose |
|---|---|
| **1 — broad** | one pass over everything the lab adds |
| **2 — edge** | the cases a plausible-but-wrong implementation fails |
| **3 — kernel** | a realistic program, for the CPI / accuracy measurement |

## Installing

The base (skeleton) has no test harness, so run those three by hand. The
other four install into the matching lab and are picked up by its existing
`tests/run_tests.sh`:

```bash
# skeleton -> the root riscv_sim_base folder
./rvsim path/to/lab1_skeleton/s1_alu.s --batch

# the other four labs
cp lab2_pipeline/*.s      lab1/tests/  ; cp lab2_pipeline/expected/*  lab1/tests/expected/
cp lab3_hazards/*.s       lab2/tests/  ; cp lab3_hazards/expected/*   lab2/tests/expected/
cp lab4_branch/*.s        lab3/tests/  ; cp lab4_branch/expected/*    lab3/tests/expected/
cp lab5_fp/*.s            lab5/tests/  ; cp lab5_fp/expected/*        lab5/tests/expected/
cd lab1 && make && make test        # and likewise lab2, lab3, lab5
```

Check counts with these installed: **lab1 25, lab2 46, lab3 70, lab5 36** —
all passing.

Note the folder-name offset: these are named for the *lab sheet* number, and
the pipeline lab's code folder is `lab1`, the hazards lab's is `lab2`, the
branch lab's is `lab3`, and the floating-point lab's is `lab5`.

## Expected printed output

| Program | Prints |
|---|---|
| `s1_alu` | `2016` |
| `s2_memory` | `36` |
| `s3_control` | `755120` (7, then 55, then 120) |
| `p1_broad`, `p2_edge` | nothing — checked by registers |
| `p3_kernel` | `1939` (1, then 9, then 39) |
| `h1_broad` | `59` |
| `h2_edge` | `28` |
| `h3_kernel` | `1939` |
| `b1_patterns` | `703233` |
| `b2_adversarial` | `4078` |
| `b3_applied` | `697645586` (697, 64, 55, 86) |
| `f1_broad` | `11` |
| `f2_special` | `12` |
| `f3_kernel` | `1432536311` (14, 32, 53, 63, 11) |

## Three deliberate "wrong answer" goldens

`h1_broad`, `h2_edge` and `h3_kernel` each have a `--hazard none` golden
that records the **wrong** result on purpose, because that is what a
pipeline with no hazard handling produces. `h3_kernel` under `none` prints
`510791112` instead of `1939` — the sort collapses. Do not "fix" those.

## Writing more

- Labs 1 and 2 (skeleton, pipeline) have **no hazard handling**: put at
  least two independent instructions or `nop`s between a producer and its
  consumer, `ecall` included (it reads `a7` and `a0`). Two of these
  programs were first written one `nop` short and the oracle caught both.
- The root base has no `la`; it has `li`, which accepts labels. The lab
  assemblers have both. RARS rejects `li reg, label`, so the skeleton
  programs are RARS-checked through a copy with `li` → `la` substituted.
- Never put an address-holding register in `# RARS-CHECK:` — RARS lays
  `.data` out at a different address.
- `ble` is not a supported pseudo-op; use `bge` with the operands swapped.
