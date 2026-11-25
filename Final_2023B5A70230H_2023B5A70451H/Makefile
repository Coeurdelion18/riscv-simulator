CC      := gcc
# -frounding-math: the F extension changes the FPU rounding mode at run
# time (fesetround); this stops the compiler assuming round-to-nearest.
CFLAGS  := -std=c11 -Wall -Wextra -g -O0 -MMD -MP -frounding-math \
           -Icore -Iisa -Iassembler -Iui -Ipipeline
LDFLAGS := -lm

BIN := rvsim
ifeq ($(OS),Windows_NT)
BIN := rvsim.exe
PYTHON ?= python
else
PYTHON ?= python3
endif

SRCS := \
	core/cpu_state.c \
	core/memory.c \
	core/isa_registry.c \
	core/decoder.c \
	core/executor.c \
	core/sim_engine.c \
	pipeline/control.c \
	pipeline/pipeline.c \
	pipeline/hazard.c \
	pipeline/hazard_none.c \
	pipeline/hazard_stall.c \
	pipeline/hazard_forward.c \
	pipeline/branch_predictor.c \
	pipeline/bp_static.c \
	pipeline/bp_dynamic.c \
	isa/rv32i_base.c \
	isa/rv32m.c \
	isa/rv32f.c \
	isa/fp_helpers.c \
	isa/zicsr.c \
	assembler/assembler.c \
	ui/cli_display.c \
	main.c

OBJS := $(SRCS:.c=.o)
DEPS := $(OBJS:.o=.d)

.PHONY: all clean run memcheck test

all: $(BIN)
ifeq ($(OS),Windows_NT)
all: rvsim
rvsim: $(BIN)
	powershell.exe -NoProfile -Command "Copy-Item -LiteralPath '$(BIN)' -Destination 'rvsim' -Force"
endif

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Example: make run PROG=../sample.s
run: $(BIN)
	./$(BIN) $(PROG) --batch

# Requires valgrind installed. Example: make memcheck PROG=../sample.s
memcheck: $(BIN)
	valgrind --error-exitcode=1 --leak-check=full ./$(BIN) $(PROG) --batch

clean:
ifeq ($(OS),Windows_NT)
	powershell.exe -NoProfile -Command "& { foreach ($$artifact in @($(foreach file,$(OBJS) $(DEPS) rvsim rvsim.exe,'$(file)',)'')) { if ($$artifact -and (Test-Path -LiteralPath $$artifact)) { Remove-Item -LiteralPath $$artifact -Force -ErrorAction Stop } } }"
else
	rm -f $(OBJS) $(DEPS) rvsim rvsim.exe
endif

define RVSIM_CHECK
from pathlib import Path
import os, re, subprocess

exe = str(Path(os.environ["RVSIM_BIN"]).resolve())

def run(program, options, allow_limit=False):
    process = subprocess.run([exe, str(program.resolve()), "--batch", "--max-cycles", "20000"] + options,
                             capture_output=True, text=True, timeout=10)
    assert process.returncode == 0, (program, options, process.stderr)
    assert allow_limit or not re.search(r"^status\s*=\s*RUNNING", process.stdout, re.M), (program, options, "cycle limit")
    return process.stdout

def state(output):
    values = {kind + str(int(n)): int(v, 16) for kind, n, v in
              re.findall(r"\b([xf])(\d+)\s*/\s*\w+\s*=\s*(0x[0-9a-fA-F]+)", output)}
    values.update({k: int(v, 16) for k, v in re.findall(r"^\s*(pc|fcsr)\s*=\s*(0x[0-9a-fA-F]+)", output, re.M)})
    values["status"] = re.search(r"^status\s*=\s*(\w+)", output, re.M)[1]
    return values

def counters(output):
    values = {k: int(v) for k, v in re.findall(r"^\s*([a-z_]+)\s*=\s*(\d+)\s*$$", output, re.M)}
    values["cycles"] = int(re.search(r"status\s*=\s*\w+\s+cycles\s*=\s*(\d+)", output)[1])
    return values

programs = sorted(Path("tests").rglob("*.s"))
samples = Path("../sample_tests")
programs += sorted(samples.rglob("*.s")) if samples.is_dir() else []
for sample in (Path("../student_package_updated/sample.s"), Path("../sample.s")):
    if sample.is_file():
        programs.append(sample)
        break
old_sample = Path("../student_package/sample.s")
if old_sample.is_file():
    programs.append(old_sample)
safe = unsafe = 0
for program in programs:
    reference = run(program, ["--single-cycle"])
    oracle = state(reference)
    reference_cycles = counters(reference)["cycles"]
    for hazard in ("stall", "forward"):
        for predictor in ("not-taken", "btfn", "1bit", "2bit"):
            output = run(program, ["--hazard", hazard, "--predictor", predictor])
            assert state(output) == oracle, (program, hazard, predictor, "architectural state")
            c = counters(output)
            if oracle["status"] == "ECALL_EXIT":
                assert c["cycles"] == c["retired"] + c["stalls"] + 2*c["redirects"] + 4, (program, c)
                assert c["retired"] == reference_cycles, program
            safe += 1
    for predictor in ("not-taken", "btfn", "1bit", "2bit"):
        c = counters(run(program, ["--hazard", "none", "--predictor", predictor], allow_limit=True))
        assert c["stalls"] == 0 and c["forwards"] == 0, program
        unsafe += 1
    print("PASS", program)

goldens = 0
for golden in sorted(samples.rglob("*.out")) if samples.is_dir() else []:
    stem, mode = golden.stem.rsplit(".", 1)
    program = golden.parent.parent / (stem + ".s")
    if mode == "single-cycle":
        options = ["--single-cycle"]
    elif mode in ("not-taken", "btfn", "1bit", "2bit"):
        options = ["--predictor", mode]
    else:
        options = ["--hazard", "none" if mode == "pipeline" else mode]
    if mode != "single-cycle" and golden.parent.parent.name in ("lab2_pipeline", "lab3_hazards"):
        options += ["--no-predict"]
    actual = run(program, options)
    expected = golden.read_text()
    got = state(actual)
    assert all(got.get(k) == v for k, v in state(expected).items()), (golden, "state")
    got = counters(actual)
    assert all(got.get(k) == v for k, v in counters(expected).items()), (golden, "counters")
    goldens += 1

for sample in (Path("../student_package_updated/sample.s"), Path("../student_package/sample.s")):
    expected = sample.with_suffix(".txt")
    if sample.is_file() and expected.is_file():
        output = run(sample, [])
        got = state(output)
        assert all(got.get(k) == v for k, v in state(expected.read_text()).items()), "evaluator state"
        got = counters(output)
        assert all(got.get(k) == v for k, v in counters(expected.read_text()).items()), "evaluator counters"
        print("PASS evaluator sample:", sample)
print("PASS", len(programs), "programs;", safe, "safe pipeline configurations;", unsafe, "none-hazard configurations;", goldens, "goldens")
endef
export RVSIM_CHECK
export RVSIM_BIN = $(BIN)

test: $(BIN)
	$(PYTHON) -c "import os; exec(os.environ['RVSIM_CHECK'])"

-include $(DEPS)
