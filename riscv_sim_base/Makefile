CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -g -O0 \
           -Icore -Iisa -Iassembler -Iui
LDFLAGS :=

BIN := rvsim

SRCS := \
	core/cpu_state.c \
	core/memory.c \
	core/isa_registry.c \
	core/decoder.c \
	core/executor.c \
	isa/rv32i_base.c \
	assembler/assembler.c \
	ui/cli_display.c \
	main.c

OBJS := $(SRCS:.c=.o)

.PHONY: all clean run memcheck test

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Example: make run PROG=tests/sample1.s
run: $(BIN)
	./$(BIN) $(PROG) --batch

# Requires valgrind installed. Example: make memcheck PROG=tests/sample1.s
memcheck: $(BIN)
	valgrind --error-exitcode=1 --leak-check=full ./$(BIN) $(PROG) --batch

# Runs every .s file under tests/ in batch mode, just to smoke-test the build.
test: $(BIN)
	@for f in tests/*.s; do \
		echo "=== $$f ==="; \
		./$(BIN) "$$f" --batch || exit 1; \
		echo; \
	done

clean:
	rm -f $(OBJS) $(BIN)
