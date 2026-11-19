#include "isa_registry.h"
#include <string.h>
#include <stdio.h>

static InstructionHandler g_registry[ISA_REGISTRY_MAX];
static uint32_t g_flags[ISA_REGISTRY_MAX];
static int32_t  g_rs2sel[ISA_REGISTRY_MAX];
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
    memset(g_flags, 0, sizeof(g_flags));
    memset(g_rs2sel, 0, sizeof(g_rs2sel));
}

// int register_instruction(InstructionHandler handler) {
//     if (g_count >= ISA_REGISTRY_MAX) return -1;
//     g_registry[g_count++] = handler;
//     return 0;
// }

int register_instruction_ex(InstructionHandler handler, uint32_t flags, int32_t rs2_sel) {
    if (g_count >= ISA_REGISTRY_MAX) return -1;
    g_registry[g_count] = handler;
    g_flags[g_count]    = flags;
    g_rs2sel[g_count]   = rs2_sel;
    g_count++;
    return 0;
}

/* The six-field form every base-ISA call uses: no flags, no fixed rs2. */
int register_instruction(InstructionHandler handler) {
    return register_instruction_ex(handler, 0, -1);
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

const InstructionHandler *isa_find_by_encoding(uint32_t opcode, uint32_t funct3,
                                               uint32_t funct7, uint32_t rs2) {
    for (int i = 0; i < g_count; i++) {
        InstructionHandler *h = &g_registry[i];
        if (h->opcode != opcode) continue;
        if (h->funct3 != -1 && (uint32_t)h->funct3 != funct3) continue;
        if (h->funct7 != -1 && (uint32_t)h->funct7 != funct7) continue;
        if ((g_flags[i] & IF_RS2_FIXED) && (uint32_t)g_rs2sel[i] != rs2) continue;
        return h;
    }
    return NULL;
}

uint32_t isa_flags_of(const InstructionHandler *h) {
    if (!h) return 0;
    long idx = h - g_registry;
    if (idx < 0 || idx >= g_count) return 0;
    return g_flags[idx];
}

int32_t isa_rs2_sel_of(const InstructionHandler *h) {
    if (!h) return -1;
    long idx = h - g_registry;
    if (idx < 0 || idx >= g_count) return -1;
    return g_rs2sel[idx];
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
