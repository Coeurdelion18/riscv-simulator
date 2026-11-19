CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -g -O0 -frounding-math \
           -Icore -Iisa -Iassembler -Iui -Ipipeline
LDFLAGS :=

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
	isa/rv32f.c \
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

test: $(BIN)
	$(PYTHON) pipeline/tests/verify_branch.py ./$(BIN)

clean:
ifeq ($(OS),Windows_NT)
	powershell.exe -NoProfile -Command "& { foreach ($$artifact in @($(foreach file,$(OBJS) rvsim rvsim.exe,'$(file)',)'')) { if ($$artifact -and (Test-Path -LiteralPath $$artifact)) { Remove-Item -LiteralPath $$artifact -Force -ErrorAction Stop } } }"
else
	rm -f $(OBJS) $(BIN) $(BIN).exe
endif
