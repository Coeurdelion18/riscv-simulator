#include "isa_registry.h"
#include <string.h>
#include <stdio.h>

static InstructionHandler g_registry[ISA_REGISTRY_MAX];
static int g_count = 0;

/* NOTE ON PERFORMANCE: lookups below are linear scans. With the base ISA
 * (~40 entries) plus a few labs' worth of extensions this is perfectly fine
 * at teaching scale (a few hundred entries at most, executed at most a few
 * thousand times per run). If you want a real exercise in dispatch-table
 * design, replacing this with a hash table keyed on (opcode,funct3,funct7)
 * is a good optional challenge -- but it is NOT required for any lab. */

void isa_registry_init(void) {
    g_count = 0;
    memset(g_registry, 0, sizeof(g_registry));
}

int register_instruction(InstructionHandler handler) {
    if (g_count >= ISA_REGISTRY_MAX) return -1;
    g_registry[g_count++] = handler;
    return 0;
}

const InstructionHandler *isa_find_by_opcode(uint32_t opcode, uint32_t funct3, uint32_t funct7) {
    for (int i = 0; i < g_count; i++) {
        InstructionHandler *h = &g_registry[i];
        if (h->opcode != opcode) continue;
        if (h->funct3 != -1 && (uint32_t)h->funct3 != funct3) continue;
        if (h->funct7 != -1 && (uint32_t)h->funct7 != funct7) continue;
        return h;
    }
    return NULL;
}

const InstructionHandler *isa_find_by_mnemonic(const char *mnemonic) {
    const InstructionHandler *found = NULL;
    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_registry[i].mnemonic, mnemonic) == 0) {
            found = &g_registry[i]; /* keep scanning: last match wins, see header */
        }
    }
    return found;
}

int isa_registry_count(void) { return g_count; }

const InstructionHandler *isa_registry_get(int index) {
    if (index < 0 || index >= g_count) return NULL;
    return &g_registry[index];
}
