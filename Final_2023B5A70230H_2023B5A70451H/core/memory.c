#include "memory.h"
#include <stdlib.h>
#include <string.h>

int memory_init(Memory *mem, uint32_t size_bytes) {
    mem->data = (uint8_t *)calloc(1, size_bytes);
    if (!mem->data) return -1;
    mem->size = size_bytes;
    return 0;
}

void memory_free(Memory *mem) {
    free(mem->data);
    mem->data = NULL;
    mem->size = 0;
}

MemStatus memory_read_byte(const Memory *mem, uint32_t addr, uint8_t *out) {
    if (addr >= mem->size) return MEM_ERR_OUT_OF_BOUNDS;
    *out = mem->data[addr];
    return MEM_OK;
}

MemStatus memory_read_half(const Memory *mem, uint32_t addr, uint16_t *out) {
    if ((uint64_t)addr + 1 >= mem->size) return MEM_ERR_OUT_OF_BOUNDS;
    *out = (uint16_t)mem->data[addr] | ((uint16_t)mem->data[addr + 1] << 8);
    return MEM_OK;
}

MemStatus memory_read_word(const Memory *mem, uint32_t addr, uint32_t *out) {
    if ((uint64_t)addr + 3 >= mem->size) return MEM_ERR_OUT_OF_BOUNDS;
    *out = (uint32_t)mem->data[addr]
         | ((uint32_t)mem->data[addr + 1] << 8)
         | ((uint32_t)mem->data[addr + 2] << 16)
         | ((uint32_t)mem->data[addr + 3] << 24);
    return MEM_OK;
}

MemStatus memory_write_byte(Memory *mem, uint32_t addr, uint8_t val) {
    if (addr >= mem->size) return MEM_ERR_OUT_OF_BOUNDS;
    mem->data[addr] = val;
    return MEM_OK;
}

MemStatus memory_write_half(Memory *mem, uint32_t addr, uint16_t val) {
    if ((uint64_t)addr + 1 >= mem->size) return MEM_ERR_OUT_OF_BOUNDS;
    mem->data[addr]     = (uint8_t)(val & 0xFF);
    mem->data[addr + 1] = (uint8_t)((val >> 8) & 0xFF);
    return MEM_OK;
}

MemStatus memory_write_word(Memory *mem, uint32_t addr, uint32_t val) {
    if ((uint64_t)addr + 3 >= mem->size) return MEM_ERR_OUT_OF_BOUNDS;
    mem->data[addr]     = (uint8_t)(val & 0xFF);
    mem->data[addr + 1] = (uint8_t)((val >> 8) & 0xFF);
    mem->data[addr + 2] = (uint8_t)((val >> 16) & 0xFF);
    mem->data[addr + 3] = (uint8_t)((val >> 24) & 0xFF);
    return MEM_OK;
}
