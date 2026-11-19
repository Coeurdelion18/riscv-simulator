CC      := gcc
CFLAGS  := -std=c11 -frounding-math -Wall -Wextra -g -O0 \
           -Icore -Iisa -Iassembler -Iui -Ipipeline
LDFLAGS := -lm

BIN := rvsim
ifeq ($(OS),Windows_NT)
BIN := rvsim.exe
endif
PYTHON ?= python

SRCS := \
	core/cpu_state.c \
	core/memory.c \
	core/isa_registry.c \
	core/decoder.c \
	core/executor.c \
	isa/rv32i_base.c \
	isa/rv32m.c \
	isa/rv32f.c \
	isa/fp_helpers.c \
	isa/zicsr.c \
	assembler/assembler.c \
	ui/cli_display.c \
	pipeline/control.c \
	pipeline/pipeline.c \
	pipeline/hazard.c \
	pipeline/branch_predictor.c \
	pipeline/bp_static.c \
	pipeline/bp_dynamic.c \
	main.c

OBJS := $(SRCS:.c=.o)

.PHONY: all clean run memcheck test

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(OBJS): core/cpu_state.h core/isa_registry.h pipeline/control.h

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

main.o pipeline/pipeline.o pipeline/hazard.o: pipeline/pipeline_state.h pipeline/branch_predictor.h
pipeline/branch_predictor.o pipeline/bp_static.o pipeline/bp_dynamic.o: pipeline/branch_predictor.h

# Example: make run PROG=tests/sample1.s
run: $(BIN)
	./$(BIN) $(PROG) --batch --single-cycle

# Requires valgrind installed. Example: make memcheck PROG=tests/sample1.s
memcheck: $(BIN)
	valgrind --error-exitcode=1 --leak-check=full ./$(BIN) $(PROG) --batch --single-cycle

# Keep the regression command self-contained, without generated test scripts.
define RVSIM_CHECK
from pathlib import Path
import subprocess
import os

executable = str(Path(os.environ["RVSIM_BIN"]).resolve())
def run(program, options):
    output = subprocess.check_output(
        [executable, str(program), "--batch", "--max-cycles", "20000"] + options,
        text=True, timeout=30)
    if "status = RUNNING" in output:
        raise AssertionError((program, "cycle limit reached"))
    return output

def state(output):
    registers = [line for line in output.splitlines()
                 if ((line[:1] in ("x", "f") and line[1:2].isdigit())
                     or line.startswith("fcsr"))]
    return registers, output.split("status = ")[1].split()[0]

for program in sorted(Path("tests").rglob("*.s")):
    if program.name == "fp_data.s":
        continue
    reference = state(run(program, ["--single-cycle"]))
    for hazard in ("forward", "stall"):
        assert state(run(program, ["--hazard", hazard])) == reference, (program, hazard)
    print("PASS", program)
endef
export RVSIM_CHECK
export RVSIM_BIN = $(BIN)

test: $(BIN)
	$(PYTHON) -c "import os; exec(os.environ['RVSIM_CHECK'])"

clean:
ifeq ($(OS),Windows_NT)
	powershell.exe -NoProfile -Command "& { foreach ($$artifact in @($(foreach file,$(OBJS) rvsim rvsim.exe,'$(file)',)'')) { if ($$artifact -and (Test-Path -LiteralPath $$artifact)) { Remove-Item -LiteralPath $$artifact -Force -ErrorAction Stop } } }"
else
	rm -f $(OBJS) $(BIN) $(BIN).exe
endif
