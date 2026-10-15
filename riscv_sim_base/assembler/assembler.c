#include "assembler.h"
#include "isa_registry.h"
#include "cpu_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINES     8192
#define MAX_LINE_LEN  512
#define MAX_SYMBOLS   4096

typedef struct {
    char text[MAX_LINE_LEN];
    int  lineno;
} SrcLine;

typedef struct {
    char name[64];
    uint32_t address;
} Symbol;

typedef struct {
    Symbol items[MAX_SYMBOLS];
    int count;
} SymbolTable;

static int symtab_find(SymbolTable *st, const char *name) {
    for (int i = 0; i < st->count; i++)
        if (strcmp(st->items[i].name, name) == 0) return i;
    return -1;
}

static int symtab_add(SymbolTable *st, const char *name, uint32_t addr) {
    if (symtab_find(st, name) != -1) return -1; /* duplicate */
    if (st->count >= MAX_SYMBOLS) return -2;
    strncpy(st->items[st->count].name, name, sizeof(st->items[st->count].name) - 1);
    st->items[st->count].name[sizeof(st->items[st->count].name) - 1] = '\0';
    st->items[st->count].address = addr;
    st->count++;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Small string helpers                                               */
/* ------------------------------------------------------------------ */

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) { *end = '\0'; end--; }
    return s;
}

static void strip_comment(char *s) {
    for (char *p = s; *p; p++) {
        if (*p == '#') { *p = '\0'; break; }
        if (*p == '/' && *(p + 1) == '/') { *p = '\0'; break; }
    }
}

static void to_lower_inplace(char *s) {
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

/* Splits "a, b, c" into up to max_tokens trimmed tokens. Commas are never
 * expected to appear inside our operand syntax (offsets use "imm(reg)"
 * with no comma), so a plain split is sufficient. Returns token count. */
static int split_commas(char *s, char *tokens[], int max_tokens) {
    int n = 0;
    char *tok = strtok(s, ",");
    while (tok && n < max_tokens) {
        tokens[n++] = trim(tok);
        tok = strtok(NULL, ",");
    }
    return n;
}

/* label:            -> label="label", rest=""
 * label: mnemonic .. -> label="label", rest="mnemonic .."
 * mnemonic ..        -> label="",      rest="mnemonic .."
 */
static void tokenize_line(char *line, char *label_out, char *mnemonic_out, char *operand_out) {
    label_out[0] = mnemonic_out[0] = operand_out[0] = '\0';

    strip_comment(line);
    char *s = trim(line);
    if (*s == '\0') return;

    char *colon = strchr(s, ':');
    if (colon) {
        *colon = '\0';
        char *lbl = trim(s);
        strncpy(label_out, lbl, 63); label_out[63] = '\0';
        s = trim(colon + 1);
    }
    if (*s == '\0') return;

    char *space = s;
    while (*space && !isspace((unsigned char)*space)) space++;
    if (*space) {
        *space = '\0';
        strncpy(mnemonic_out, s, 31); mnemonic_out[31] = '\0';
        strncpy(operand_out, trim(space + 1), 255); operand_out[255] = '\0';
    } else {
        strncpy(mnemonic_out, s, 31); mnemonic_out[31] = '\0';
    }
    to_lower_inplace(mnemonic_out);
}

/* ------------------------------------------------------------------ */
/* Value / operand parsing                                            */
/* ------------------------------------------------------------------ */

static int is_pseudo_directive(const char *mnemonic) {
    return strcmp(mnemonic, ".text") == 0 || strcmp(mnemonic, ".data") == 0 ||
           strcmp(mnemonic, ".globl") == 0 || strcmp(mnemonic, ".global") == 0;
}

static int set_err(AsmError *err, int lineno, AsmStatus status, const char *fmt, ...);
/* forward-declared helper defined near the bottom; returns `status` so
 * call sites can `return set_err(...)` directly */

/* Parses a decimal/hex literal or a symbol reference. If pc_relative is
 * non-zero and the token resolves to a symbol, the result is
 * (symbol_address - addr) -- used for branch/jal targets. */
static int parse_value(const char *token, SymbolTable *st, uint32_t addr,
                        int pc_relative, int32_t *out, AsmError *err, int lineno) {
    if (!token || token[0] == '\0') { *out = 0; return ASM_OK; }

    char *endptr;
    long v = strtol(token, &endptr, 0); /* base 0: handles "0x..", "-5", "10" */
    if (*endptr == '\0') {
        *out = (int32_t)v;
        return ASM_OK;
    }

    int idx = symtab_find(st, token);
    if (idx == -1) {
        return set_err(err, lineno, ASM_ERR_UNDEFINED_SYMBOL, "undefined symbol '%s'", token);
    }
    uint32_t sym_addr = st->items[idx].address;
    *out = pc_relative ? (int32_t)(sym_addr - addr) : (int32_t)sym_addr;
    return ASM_OK;
}

static int parse_register(const char *token, uint32_t *out, AsmError *err, int lineno) {
    int idx = cpu_reg_index_from_name(token);
    if (idx < 0) return set_err(err, lineno, ASM_ERR_BAD_OPERAND, "'%s' is not a valid register name", token);
    *out = (uint32_t)idx;
    return ASM_OK;
}

/* Parses "imm(reg)" e.g. "0(sp)", "-4(t0)", "(a0)". */
static int parse_offset_paren(const char *token, SymbolTable *st, uint32_t addr,
                               int32_t *imm_out, uint32_t *reg_out, AsmError *err, int lineno) {
    char buf[128];
    strncpy(buf, token, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';

    char *open = strchr(buf, '(');
    char *close = strchr(buf, ')');
    if (!open || !close || close < open) {
        return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'offset(register)', got '%s'", token);
    }
    *close = '\0';
    *open = '\0';
    char *imm_part = trim(buf);
    char *reg_part = trim(open + 1);

    int st_ok = parse_value(imm_part, st, addr, 0, imm_out, err, lineno);
    if (st_ok != ASM_OK) return st_ok;
    return parse_register(reg_part, reg_out, err, lineno);
}

typedef struct { uint32_t rd, rs1, rs2; int32_t imm; } Fields;

static int parse_fields(InstrFormat fmt, char *operand_str, SymbolTable *st, uint32_t addr,
                         Fields *f, AsmError *err, int lineno) {
    char buf[256];
    strncpy(buf, operand_str, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    char *tok[4];
    int n = split_commas(buf, tok, 4);
    memset(f, 0, sizeof(*f));

    switch (fmt) {
        case FMT_R:
            if (n != 3) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rd, rs1, rs2'");
            if (parse_register(tok[0], &f->rd, err, lineno)  != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_register(tok[1], &f->rs1, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_register(tok[2], &f->rs2, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return ASM_OK;

        case FMT_I:
        case FMT_I_SHAMT:
            if (n != 3) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rd, rs1, imm'");
            if (parse_register(tok[0], &f->rd, err, lineno)  != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_register(tok[1], &f->rs1, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return parse_value(tok[2], st, addr, 0, &f->imm, err, lineno);

        case FMT_I_LOAD:
        case FMT_I_JALR:
            if (n != 2) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rd, offset(rs1)'");
            if (parse_register(tok[0], &f->rd, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return parse_offset_paren(tok[1], st, addr, &f->imm, &f->rs1, err, lineno);

        case FMT_I_SYS:
            return ASM_OK; /* no operands */

        case FMT_S:
            if (n != 2) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rs2, offset(rs1)'");
            if (parse_register(tok[0], &f->rs2, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return parse_offset_paren(tok[1], st, addr, &f->imm, &f->rs1, err, lineno);

        case FMT_B:
            if (n != 3) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rs1, rs2, label'");
            if (parse_register(tok[0], &f->rs1, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_register(tok[1], &f->rs2, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return parse_value(tok[2], st, addr, 1, &f->imm, err, lineno);

        case FMT_U:
            if (n != 2) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rd, imm'");
            if (parse_register(tok[0], &f->rd, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return parse_value(tok[1], st, addr, 0, &f->imm, err, lineno);

        case FMT_J:
            if (n != 2) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rd, label'");
            if (parse_register(tok[0], &f->rd, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return parse_value(tok[1], st, addr, 1, &f->imm, err, lineno);
    }
    return set_err(err, lineno, ASM_ERR_SYNTAX, "unhandled instruction format");
}

/* ------------------------------------------------------------------ */
/* Generic encoder -- mirrors decoder.c exactly, in reverse.          */
/* Works for ANY registered instruction (base or extension), since it */
/* only depends on the handler's opcode/funct3/funct7/format.         */
/* ------------------------------------------------------------------ */

static uint32_t encode_instruction(const InstructionHandler *h, Fields f) {
    uint32_t opcode = h->opcode;
    uint32_t funct3 = (h->funct3 >= 0) ? (uint32_t)h->funct3 : 0;
    uint32_t funct7 = (h->funct7 >= 0) ? (uint32_t)h->funct7 : 0;
    uint32_t imm    = (uint32_t)f.imm;

    switch (h->format) {
        case FMT_R:
            return (funct7 << 25) | ((f.rs2 & 0x1F) << 20) | ((f.rs1 & 0x1F) << 15) |
                   (funct3 << 12) | ((f.rd & 0x1F) << 7) | opcode;

        case FMT_I:
        case FMT_I_LOAD:
        case FMT_I_JALR:
            return ((imm & 0xFFF) << 20) | ((f.rs1 & 0x1F) << 15) |
                   (funct3 << 12) | ((f.rd & 0x1F) << 7) | opcode;

        case FMT_I_SHAMT:
            return (funct7 << 25) | ((imm & 0x1F) << 20) | ((f.rs1 & 0x1F) << 15) |
                   (funct3 << 12) | ((f.rd & 0x1F) << 7) | opcode;

        case FMT_I_SYS:
            return (funct3 << 12) | opcode; /* rd=0, rs1=0, imm=0 */

        case FMT_S: {
            uint32_t imm11_5 = (imm >> 5) & 0x7F;
            uint32_t imm4_0  = imm & 0x1F;
            return (imm11_5 << 25) | ((f.rs2 & 0x1F) << 20) | ((f.rs1 & 0x1F) << 15) |
                   (funct3 << 12) | (imm4_0 << 7) | opcode;
        }

        case FMT_B: {
            uint32_t b12 = (imm >> 12) & 0x1, b11 = (imm >> 11) & 0x1;
            uint32_t b10_5 = (imm >> 5) & 0x3F, b4_1 = (imm >> 1) & 0xF;
            return (b12 << 31) | (b10_5 << 25) | ((f.rs2 & 0x1F) << 20) | ((f.rs1 & 0x1F) << 15) |
                   (funct3 << 12) | (b4_1 << 8) | (b11 << 7) | opcode;
        }

        case FMT_U:
            return ((imm & 0xFFFFF) << 12) | ((f.rd & 0x1F) << 7) | opcode;

        case FMT_J: {
            uint32_t b20 = (imm >> 20) & 0x1, b19_12 = (imm >> 12) & 0xFF;
            uint32_t b11 = (imm >> 11) & 0x1, b10_1 = (imm >> 1) & 0x3FF;
            return (b20 << 31) | (b10_1 << 21) | (b11 << 20) | (b19_12 << 12) |
                   ((f.rd & 0x1F) << 7) | opcode;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Pseudo-instructions                                                */
/* ------------------------------------------------------------------ */
/* Kept deliberately minimal. "li" always expands to exactly two real
 * instructions (lui + addi) regardless of the value's magnitude -- this
 * keeps its size fixed and known during pass 1, at the cost of occasionally
 * emitting a redundant lui for small constants. */

static int pseudo_instruction_size(const char *mnemonic) {
    if (strcmp(mnemonic, "li") == 0) return 8;
    if (strcmp(mnemonic, "nop") == 0) return 4;
    if (strcmp(mnemonic, "ret") == 0) return 4;
    if (strcmp(mnemonic, "j") == 0) return 4;
    if (strcmp(mnemonic, "mv") == 0) return 4;
    return 0; /* not a pseudo-instruction */
}

static void li_split(int32_t value, uint32_t *upper20, int32_t *lower12) {
    int64_t v = value;
    int64_t upper = (v + 0x800) >> 12;
    int32_t lower = (int32_t)(v - (upper << 12));
    *upper20 = (uint32_t)(upper & 0xFFFFF);
    *lower12 = lower;
}

/* Encodes a pseudo-instruction into `mem` at `addr`, writing 4 or 8 bytes.
 * Returns ASM_OK or an error status via `err`. */
static int assemble_pseudo(const char *mnemonic, char *operand_str, SymbolTable *st,
                            uint32_t addr, Memory *mem, AsmError *err, int lineno) {
    char buf[256];
    strncpy(buf, operand_str, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    char *tok[3];
    int n = split_commas(buf, tok, 3);

    if (strcmp(mnemonic, "nop") == 0) {
        const InstructionHandler *h = isa_find_by_mnemonic("addi");
        Fields f = {0, 0, 0, 0};
        memory_write_word(mem, addr, encode_instruction(h, f));
        return ASM_OK;
    }
    if (strcmp(mnemonic, "ret") == 0) {
        const InstructionHandler *h = isa_find_by_mnemonic("jalr");
        Fields f = {0, 1 /* ra */, 0, 0};
        memory_write_word(mem, addr, encode_instruction(h, f));
        return ASM_OK;
    }
    if (strcmp(mnemonic, "mv") == 0) {
        if (n != 2) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'mv rd, rs'");
        const InstructionHandler *h = isa_find_by_mnemonic("addi");
        Fields f = {0, 0, 0, 0};
        if (parse_register(tok[0], &f.rd, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
        if (parse_register(tok[1], &f.rs1, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
        memory_write_word(mem, addr, encode_instruction(h, f));
        return ASM_OK;
    }
    if (strcmp(mnemonic, "j") == 0) {
        if (n != 1) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'j label'");
        const InstructionHandler *h = isa_find_by_mnemonic("jal");
        Fields f = {0, 0, 0, 0};
        int st_ok = parse_value(tok[0], st, addr, 1, &f.imm, err, lineno);
        if (st_ok != ASM_OK) return st_ok;
        memory_write_word(mem, addr, encode_instruction(h, f));
        return ASM_OK;
    }
    if (strcmp(mnemonic, "li") == 0) {
        if (n != 2) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'li rd, imm'");
        uint32_t rd;
        if (parse_register(tok[0], &rd, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
        int32_t value;
        int st_ok = parse_value(tok[1], st, addr, 0, &value, err, lineno);
        if (st_ok != ASM_OK) return st_ok;

        uint32_t upper20; int32_t lower12;
        li_split(value, &upper20, &lower12);

        const InstructionHandler *hl = isa_find_by_mnemonic("lui");
        const InstructionHandler *ha = isa_find_by_mnemonic("addi");
        Fields fl = {rd, 0, 0, (int32_t)upper20};
        Fields fa = {rd, rd, 0, lower12};
        memory_write_word(mem, addr,     encode_instruction(hl, fl));
        memory_write_word(mem, addr + 4, encode_instruction(ha, fa));
        return ASM_OK;
    }
    return set_err(err, lineno, ASM_ERR_UNKNOWN_MNEMONIC, "unknown pseudo-instruction '%s'", mnemonic);
}

/* ------------------------------------------------------------------ */
/* Directives (.word / .half / .byte / .align / .text / .data / .globl)*/
/* ------------------------------------------------------------------ */

static int directive_element_size(const char *mnemonic) {
    if (strcmp(mnemonic, ".word") == 0) return 4;
    if (strcmp(mnemonic, ".half") == 0) return 2;
    if (strcmp(mnemonic, ".byte") == 0) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Error helper                                                       */
/* ------------------------------------------------------------------ */

#include <stdarg.h>
static int set_err(AsmError *err, int lineno, AsmStatus status, const char *fmt, ...) {
    if (err) {
        err->line_number = lineno;
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err->message, sizeof(err->message), fmt, ap);
        va_end(ap);
    }
    return status;
}

/* ------------------------------------------------------------------ */
/* Main two-pass driver                                               */
/* ------------------------------------------------------------------ */

static int read_lines(const char *filename, SrcLine *lines, int *out_count, AsmError *err) {
    FILE *f = fopen(filename, "r");
    if (!f) return set_err(err, 0, ASM_ERR_IO, "could not open '%s'", filename);

    int count = 0;
    char raw[MAX_LINE_LEN];
    int lineno = 0;
    while (fgets(raw, sizeof(raw), f)) {
        lineno++;
        if (count >= MAX_LINES) {
            fclose(f);
            return set_err(err, lineno, ASM_ERR_TOO_MANY_LINES, "source file exceeds %d lines", MAX_LINES);
        }
        strncpy(lines[count].text, raw, MAX_LINE_LEN - 1);
        lines[count].text[MAX_LINE_LEN - 1] = '\0';
        lines[count].lineno = lineno;
        count++;
    }
    fclose(f);
    *out_count = count;
    return ASM_OK;
}

AsmStatus assemble_file(const char *filename, Memory *mem, uint32_t *out_entry_point, AsmError *err) {
    static SrcLine lines[MAX_LINES]; /* static: too large for the stack */
    static SymbolTable st;
    int nlines = 0;
    st.count = 0;

    int rc = read_lines(filename, lines, &nlines, err);
    if (rc != ASM_OK) return (AsmStatus)rc;

    /* ---------------- pass 1: build the symbol table ---------------- */
    uint32_t addr = 0;
    for (int i = 0; i < nlines; i++) {
        char work[MAX_LINE_LEN];
        strncpy(work, lines[i].text, sizeof(work) - 1); work[sizeof(work) - 1] = '\0';

        char label[64], mnemonic[32], operands[256];
        tokenize_line(work, label, mnemonic, operands);

        if (label[0]) {
            if (symtab_add(&st, label, addr) != 0) {
                return (AsmStatus)set_err(err, lines[i].lineno, ASM_ERR_DUPLICATE_SYMBOL,
                                           "duplicate label '%s'", label);
            }
        }
        if (!mnemonic[0]) continue;

        if (strcmp(mnemonic, ".align") == 0) {
            char *endptr; long n = strtol(operands, &endptr, 0);
            if (endptr == operands || n <= 0) {
                return (AsmStatus)set_err(err, lines[i].lineno, ASM_ERR_SYNTAX, ".align expects a positive integer");
            }
            if (addr % (uint32_t)n != 0) addr += (uint32_t)n - (addr % (uint32_t)n);
            continue;
        }
        if (is_pseudo_directive(mnemonic)) continue;

        int elem_size = directive_element_size(mnemonic);
        if (elem_size > 0) {
            char buf[256]; strncpy(buf, operands, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
            char *tok[64];
            int n = split_commas(buf, tok, 64);
            addr += (uint32_t)(elem_size * n);
            continue;
        }

        int pseudo_size = pseudo_instruction_size(mnemonic);
        if (pseudo_size > 0) { addr += (uint32_t)pseudo_size; continue; }

        if (isa_find_by_mnemonic(mnemonic)) { addr += 4; continue; }

        return (AsmStatus)set_err(err, lines[i].lineno, ASM_ERR_UNKNOWN_MNEMONIC,
                                   "unknown instruction or directive '%s'", mnemonic);
    }

    /* ---------------- pass 2: encode ---------------- */
    addr = 0;
    for (int i = 0; i < nlines; i++) {
        char work[MAX_LINE_LEN];
        strncpy(work, lines[i].text, sizeof(work) - 1); work[sizeof(work) - 1] = '\0';

        char label[64], mnemonic[32], operands[256];
        tokenize_line(work, label, mnemonic, operands);
        (void)label; /* already recorded in pass 1 */

        if (!mnemonic[0]) continue;

        if (strcmp(mnemonic, ".align") == 0) {
            long n = strtol(operands, NULL, 0);
            if (addr % (uint32_t)n != 0) addr += (uint32_t)n - (addr % (uint32_t)n);
            continue;
        }
        if (is_pseudo_directive(mnemonic)) continue;

        int elem_size = directive_element_size(mnemonic);
        if (elem_size > 0) {
            char buf[256]; strncpy(buf, operands, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
            char *tok[64];
            int n = split_commas(buf, tok, 64);
            for (int k = 0; k < n; k++) {
                int32_t v;
                int st_ok = parse_value(tok[k], &st, addr, 0, &v, err, lines[i].lineno);
                if (st_ok != ASM_OK) return (AsmStatus)st_ok;
                MemStatus ms;
                if (elem_size == 4) ms = memory_write_word(mem, addr, (uint32_t)v);
                else if (elem_size == 2) ms = memory_write_half(mem, addr, (uint16_t)v);
                else ms = memory_write_byte(mem, addr, (uint8_t)v);
                if (ms != MEM_OK) return (AsmStatus)set_err(err, lines[i].lineno, ASM_ERR_MEMORY,
                                                             "data directive writes past end of memory");
                addr += (uint32_t)elem_size;
            }
            continue;
        }

        int pseudo_size = pseudo_instruction_size(mnemonic);
        if (pseudo_size > 0) {
            int st_ok = assemble_pseudo(mnemonic, operands, &st, addr, mem, err, lines[i].lineno);
            if (st_ok != ASM_OK) return (AsmStatus)st_ok;
            addr += (uint32_t)pseudo_size;
            continue;
        }

        const InstructionHandler *h = isa_find_by_mnemonic(mnemonic);
        if (!h) {
            return (AsmStatus)set_err(err, lines[i].lineno, ASM_ERR_UNKNOWN_MNEMONIC,
                                       "unknown instruction or directive '%s'", mnemonic);
        }
        Fields f;
        int st_ok = parse_fields(h->format, operands, &st, addr, &f, err, lines[i].lineno);
        if (st_ok != ASM_OK) return (AsmStatus)st_ok;

        uint32_t word = encode_instruction(h, f);
        if (memory_write_word(mem, addr, word) != MEM_OK) {
            return (AsmStatus)set_err(err, lines[i].lineno, ASM_ERR_MEMORY, "program too large for memory");
        }
        addr += 4;
    }

    int main_idx = symtab_find(&st, "main");
    *out_entry_point = (main_idx >= 0) ? st.items[main_idx].address : 0;
    return ASM_OK;
}
