#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

/* Error-code convention used across the whole codebase: no exceptions in C,
 * so every fallible function returns a status enum instead. Callers must
 * check it. Nothing in core/ or isa/ calls exit() -- only main.c/the CLI
 * decides whether a failure should end the program. */
typedef enum {
    MEM_OK = 0,
    MEM_ERR_OUT_OF_BOUNDS
} MemStatus;

typedef struct {
    uint8_t *data;
    uint32_t size;
} Memory;

int  memory_init(Memory *mem, uint32_t size_bytes); /* returns 0 on success, -1 on alloc failure */
void memory_free(Memory *mem);

/* All multi-byte accesses are little-endian, assembled/disassembled by hand
 * (rather than casting a uint32_t* onto the byte array) to avoid alignment
 * and strict-aliasing undefined behavior. */
MemStatus memory_read_byte (const Memory *mem, uint32_t addr, uint8_t  *out);
MemStatus memory_read_half (const Memory *mem, uint32_t addr, uint16_t *out);
MemStatus memory_read_word (const Memory *mem, uint32_t addr, uint32_t *out);

MemStatus memory_write_byte(Memory *mem, uint32_t addr, uint8_t  val);
MemStatus memory_write_half(Memory *mem, uint32_t addr, uint16_t val);
MemStatus memory_write_word(Memory *mem, uint32_t addr, uint32_t val);

#endif /* MEMORY_H */
