#include "assembler.h"
#include "isa_registry.h"
#include "cpu_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

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
    snprintf(st->items[st->count].name, sizeof(st->items[st->count].name), "%s", name);
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
    errno = 0;
    long long v = strtoll(token, &endptr, 0); /* base 0: handles "0x..", "-5", "10" */
    if (endptr != token && *endptr == '\0' && errno != ERANGE &&
        v >= INT32_MIN && v <= UINT32_MAX) {
        *out = (int32_t)(uint32_t)v;
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

/* Lab 5: an operand is either an x register or an f register, depending
 * on the instruction's IF_*_FP flags. */
static int parse_reg_class(const char *token, int is_fp, uint32_t *out, AsmError *err, int lineno) {
    if (!is_fp) return parse_register(token, out, err, lineno);
    int idx = cpu_freg_index_from_name(token);
    if (idx < 0) return set_err(err, lineno, ASM_ERR_BAD_OPERAND,
                                "'%s' is not a valid floating-point register name", token);
    *out = (uint32_t)idx;
    return ASM_OK;
}

/* Optional trailing rounding-mode operand: rne rtz rdn rup rmm dyn. */
static int parse_rm(const char *token, uint32_t *out, AsmError *err, int lineno) {
    static const char *names[] = {"rne", "rtz", "rdn", "rup", "rmm", NULL, NULL, "dyn"};
    for (uint32_t i = 0; i < 8; i++) {
        if (names[i] && strcmp(token, names[i]) == 0) { *out = i; return ASM_OK; }
    }
    return set_err(err, lineno, ASM_ERR_BAD_OPERAND,
                   "'%s' is not a rounding mode (rne, rtz, rdn, rup, rmm, dyn)", token);
}

/* CSR operand: a name (fflags, frm, fcsr) or a number. */
static int parse_csr(const char *token, int32_t *out, AsmError *err, int lineno) {
    int n = cpu_csr_number_from_name(token);
    if (n >= 0) { *out = n; return ASM_OK; }
    char *end;
    long v = strtol(token, &end, 0);
    if (*end == '\0' && end != token && v >= 0 && v <= 0xFFF) { *out = (int32_t)v; return ASM_OK; }
    return set_err(err, lineno, ASM_ERR_BAD_OPERAND, "'%s' is not a CSR name or number", token);
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

/* rs3/has_rm/rm were added in Lab 5 at the END, so the field order the
 * older initializers use ({rd, rs1, rs2, imm, ...}) is unchanged. has_rm = 0
 * means "no rounding-mode operand given" (encoded as dyn). */
typedef struct { uint32_t rd, rs1, rs2; int32_t imm; uint32_t rs3; int has_rm; uint32_t rm; } Fields;

static int parse_fields(const InstructionHandler *h, char *operand_str, SymbolTable *st, uint32_t addr,
                         Fields *f, AsmError *err, int lineno) {
    char buf[256];
    strncpy(buf, operand_str, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    char *tok[6];
    int n = split_commas(buf, tok, 6);
    memset(f, 0, sizeof(*f));

    InstrFormat fmt = h->format;
    uint32_t hf = isa_flags(h);
    int rd_fp  = (hf & IF_RD_FP)  != 0;
    int rs1_fp = (hf & IF_RS1_FP) != 0;
    int rs2_fp = (hf & IF_RS2_FP) != 0;
    int rs3_fp = (hf & IF_RS3_FP) != 0;
    int has_rm = (hf & IF_HAS_RM) != 0;

    /* Number of register/immediate operands before an optional ", rm". */
    int base = (fmt == FMT_R4) ? 4 : (fmt == FMT_R2) ? 2 : 3;
    if (has_rm && n == base + 1) {
        if (parse_rm(tok[base], &f->rm, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
        f->has_rm = 1;
        n--;
    }

    switch (fmt) {
        case FMT_R:
            if (n != 3) return set_err(err, lineno, ASM_ERR_SYNTAX,
                                       has_rm ? "expected 'rd, rs1, rs2[, rm]'" : "expected 'rd, rs1, rs2'");
            if (parse_reg_class(tok[0], rd_fp,  &f->rd, err, lineno)  != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_reg_class(tok[1], rs1_fp, &f->rs1, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_reg_class(tok[2], rs2_fp, &f->rs2, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return ASM_OK;

        case FMT_R2:
            if (n != 2) return set_err(err, lineno, ASM_ERR_SYNTAX,
                                       has_rm ? "expected 'rd, rs1[, rm]'" : "expected 'rd, rs1'");
            if (parse_reg_class(tok[0], rd_fp,  &f->rd, err, lineno)  != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_reg_class(tok[1], rs1_fp, &f->rs1, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return ASM_OK;

        case FMT_R4:
            if (n != 4) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rd, rs1, rs2, rs3[, rm]'");
            if (parse_reg_class(tok[0], rd_fp,  &f->rd, err, lineno)  != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_reg_class(tok[1], rs1_fp, &f->rs1, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_reg_class(tok[2], rs2_fp, &f->rs2, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_reg_class(tok[3], rs3_fp, &f->rs3, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return ASM_OK;

        case FMT_CSR:
            if (n != 3) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rd, csr, rs1'");
            if (parse_register(tok[0], &f->rd, err, lineno)  != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_csr(tok[1], &f->imm, err, lineno)      != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_register(tok[2], &f->rs1, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return ASM_OK;

        case FMT_CSRI: {
            if (n != 3) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rd, csr, uimm5'");
            if (parse_register(tok[0], &f->rd, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_csr(tok[1], &f->imm, err, lineno)     != ASM_OK) return ASM_ERR_BAD_OPERAND;
            int32_t u;
            int st_ok = parse_value(tok[2], st, addr, 0, &u, err, lineno);
            if (st_ok != ASM_OK) return st_ok;
            if (u < 0 || u > 31) return set_err(err, lineno, ASM_ERR_BAD_OPERAND, "CSR immediate must be 0..31");
            f->rs1 = (uint32_t)u; /* the uimm lives in the rs1 field */
            return ASM_OK;
        }

        case FMT_I:
        case FMT_I_SHAMT:
            if (n != 3) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rd, rs1, imm'");
            if (parse_register(tok[0], &f->rd, err, lineno)  != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_register(tok[1], &f->rs1, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return parse_value(tok[2], st, addr, 0, &f->imm, err, lineno);

        case FMT_I_LOAD:
        case FMT_I_JALR:
            if (n != 2) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rd, offset(rs1)'");
            if (parse_reg_class(tok[0], rd_fp, &f->rd, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return parse_offset_paren(tok[1], st, addr, &f->imm, &f->rs1, err, lineno);

        case FMT_I_SYS:
            return ASM_OK; /* no operands */

        case FMT_S:
            if (n != 2) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rs2, offset(rs1)'");
            if (parse_reg_class(tok[0], rs2_fp, &f->rs2, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
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

// Reject operands that cannot fit instruction fields instead of silently truncating values.
static int validate_fields(const InstructionHandler *h, Fields f, AsmError *err, int lineno) {
    switch (h->format) {
        case FMT_I:
        case FMT_I_LOAD:
        case FMT_I_JALR:
        case FMT_S:
            if (f.imm < -2048 || f.imm > 2047)
                return set_err(err, lineno, ASM_ERR_BAD_OPERAND, "immediate/offset must be -2048..2047");
            break;
        case FMT_I_SHAMT:
            if (f.imm < 0 || f.imm > 31)
                return set_err(err, lineno, ASM_ERR_BAD_OPERAND, "shift amount must be 0..31");
            break;
        case FMT_B:
            if (f.imm < -4096 || f.imm > 4094 || f.imm % 2 != 0)
                return set_err(err, lineno, ASM_ERR_BAD_OPERAND, "branch offset must be even and -4096..4094");
            break;
        case FMT_J:
            if (f.imm < -1048576 || f.imm > 1048574 || f.imm % 2 != 0)
                return set_err(err, lineno, ASM_ERR_BAD_OPERAND, "jump offset must be even and -1048576..1048574");
            break;
        case FMT_U:
            if (f.imm < -524288 || f.imm > 1048575)
                return set_err(err, lineno, ASM_ERR_BAD_OPERAND, "upper immediate must fit a 20-bit field");
            break;
        default:
            break;
    }
    return ASM_OK;
}

static uint32_t encode_instruction(const InstructionHandler *h, Fields f) {
    uint32_t opcode = h->opcode;
    uint32_t funct3 = (h->funct3 >= 0) ? (uint32_t)h->funct3 : 0;
    uint32_t funct7 = (h->funct7 >= 0) ? (uint32_t)h->funct7 : 0;
    uint32_t imm    = (uint32_t)f.imm;

    /* Lab 5: a rounding-mode funct3 defaults to dyn (use fcsr.frm). */
    if (isa_flags(h) & IF_HAS_RM) funct3 = f.has_rm ? (f.rm & 0x7) : RM_DYN;

    switch (h->format) {
        case FMT_R:
            return (funct7 << 25) | ((f.rs2 & 0x1F) << 20) | ((f.rs1 & 0x1F) << 15) |
                   (funct3 << 12) | ((f.rd & 0x1F) << 7) | opcode;

        case FMT_R2: /* the rs2 field is a fixed part of the opcode */
            return (funct7 << 25) | ((isa_rs2_sel(h) & 0x1F) << 20) | ((f.rs1 & 0x1F) << 15) |
                   (funct3 << 12) | ((f.rd & 0x1F) << 7) | opcode;

        case FMT_R4: /* bits 26:25 = fmt, 00 for single precision */
            return ((f.rs3 & 0x1F) << 27) | ((f.rs2 & 0x1F) << 20) | ((f.rs1 & 0x1F) << 15) |
                   (funct3 << 12) | ((f.rd & 0x1F) << 7) | opcode;

        case FMT_CSR:
        case FMT_CSRI:
            return ((imm & 0xFFF) << 20) | ((f.rs1 & 0x1F) << 15) |
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
/* Kept deliberately minimal. "li" (and its alias "la") always expands to exactly two real
 * instructions (lui + addi) regardless of the value's magnitude -- this
 * keeps its size fixed and known during pass 1, at the cost of occasionally
 * emitting a redundant lui for small constants. */

static int pseudo_instruction_size(const char *mnemonic) {
    if (strcmp(mnemonic, "li") == 0) return 8;
    if (strcmp(mnemonic, "la") == 0) return 8;
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
        Fields f = {0, 0, 0, 0, 0, 0, 0};
        memory_write_word(mem, addr, encode_instruction(h, f));
        return ASM_OK;
    }
    if (strcmp(mnemonic, "ret") == 0) {
        const InstructionHandler *h = isa_find_by_mnemonic("jalr");
        Fields f = {0, 1 /* ra */, 0, 0, 0, 0, 0};
        memory_write_word(mem, addr, encode_instruction(h, f));
        return ASM_OK;
    }
    if (strcmp(mnemonic, "mv") == 0) {
        if (n != 2) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'mv rd, rs'");
        const InstructionHandler *h = isa_find_by_mnemonic("addi");
        Fields f = {0, 0, 0, 0, 0, 0, 0};
        if (parse_register(tok[0], &f.rd, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
        if (parse_register(tok[1], &f.rs1, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
        memory_write_word(mem, addr, encode_instruction(h, f));
        return ASM_OK;
    }
    if (strcmp(mnemonic, "j") == 0) {
        if (n != 1) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'j label'");
        const InstructionHandler *h = isa_find_by_mnemonic("jal");
        Fields f = {0, 0, 0, 0, 0, 0, 0};
        int st_ok = parse_value(tok[0], st, addr, 1, &f.imm, err, lineno);
        if (st_ok != ASM_OK) return st_ok;
        st_ok = validate_fields(h, f, err, lineno);
        if (st_ok != ASM_OK) return st_ok;
        memory_write_word(mem, addr, encode_instruction(h, f));
        return ASM_OK;
    }
    /* "la rd, label" is the same lui+addi pair as "li". It exists so test
     * programs can also be assembled by RARS, which rejects "li rd, label". */
    if (strcmp(mnemonic, "li") == 0 || strcmp(mnemonic, "la") == 0) {
        if (n != 2) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected '%s rd, value'", mnemonic);
        uint32_t rd;
        if (parse_register(tok[0], &rd, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
        int32_t value;
        int st_ok = parse_value(tok[1], st, addr, 0, &value, err, lineno);
        if (st_ok != ASM_OK) return st_ok;

        uint32_t upper20; int32_t lower12;
        li_split(value, &upper20, &lower12);

        const InstructionHandler *hl = isa_find_by_mnemonic("lui");
        const InstructionHandler *ha = isa_find_by_mnemonic("addi");
        Fields fl = {rd, 0, 0, (int32_t)upper20, 0, 0, 0};
        Fields fa = {rd, rd, 0, lower12, 0, 0, 0};
        memory_write_word(mem, addr,     encode_instruction(hl, fl));
        memory_write_word(mem, addr + 4, encode_instruction(ha, fa));
        return ASM_OK;
    }
    return set_err(err, lineno, ASM_ERR_UNKNOWN_MNEMONIC, "unknown pseudo-instruction '%s'", mnemonic);
}

/* ------------------------------------------------------------------ */
/* Aliases (Lab 5): pseudo-instructions that are exactly ONE real      */
/* instruction with rewritten operands. They are expanded into the     */
/* real mnemonic + operand string and then assembled through the       */
/* normal parse_fields()/encode_instruction() path.                    */
/* ------------------------------------------------------------------ */

static const char *const g_alias_names[] = {
    "fmv.s", "fneg.s", "fabs.s",
    "frcsr", "fscsr", "frrm", "fsrm", "frflags", "fsflags", "fsrmi", "fsflagsi",
    "csrr", "csrw", "csrs", "csrc", "csrwi", "csrsi", "csrci",
    NULL
};

static int is_alias(const char *mnemonic) {
    for (int i = 0; g_alias_names[i]; i++)
        if (strcmp(mnemonic, g_alias_names[i]) == 0) return 1;
    return 0;
}

/* Writes the real mnemonic into `real` and its operands into `ops`. */
static int expand_alias(const char *mn, const char *operand_str, char *real, char *ops, size_t ops_size,
                        AsmError *err, int lineno) {
    char buf[256];
    strncpy(buf, operand_str, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    char *t[3];
    int n = split_commas(buf, t, 3);

#define ALIAS(r, ...) do { strcpy(real, r); snprintf(ops, ops_size, __VA_ARGS__); return ASM_OK; } while (0)
#define NEED(k, form) do { if (n != (k)) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected '%s %s'", mn, form); } while (0)

    /* fd, fs  ->  fsgnj*.s fd, fs, fs */
    if (strcmp(mn, "fmv.s") == 0)  { NEED(2, "fd, fs"); ALIAS("fsgnj.s",  "%s, %s, %s", t[0], t[1], t[1]); }
    if (strcmp(mn, "fneg.s") == 0) { NEED(2, "fd, fs"); ALIAS("fsgnjn.s", "%s, %s, %s", t[0], t[1], t[1]); }
    if (strcmp(mn, "fabs.s") == 0) { NEED(2, "fd, fs"); ALIAS("fsgnjx.s", "%s, %s, %s", t[0], t[1], t[1]); }

    /* read a CSR:  rd  ->  csrrs rd, csr, x0 */
    if (strcmp(mn, "frcsr") == 0)   { NEED(1, "rd"); ALIAS("csrrs", "%s, fcsr, x0", t[0]); }
    if (strcmp(mn, "frrm") == 0)    { NEED(1, "rd"); ALIAS("csrrs", "%s, frm, x0", t[0]); }
    if (strcmp(mn, "frflags") == 0) { NEED(1, "rd"); ALIAS("csrrs", "%s, fflags, x0", t[0]); }

    /* swap a CSR:  [rd,] rs  ->  csrrw rd|x0, csr, rs   (and the immediate forms) */
    {
        const char *csr = NULL, *op = "csrrw";
        if (strcmp(mn, "fscsr") == 0)    csr = "fcsr";
        if (strcmp(mn, "fsrm") == 0)     csr = "frm";
        if (strcmp(mn, "fsflags") == 0)  csr = "fflags";
        if (strcmp(mn, "fsrmi") == 0)    { csr = "frm";    op = "csrrwi"; }
        if (strcmp(mn, "fsflagsi") == 0) { csr = "fflags"; op = "csrrwi"; }
        if (csr) {
            if (n == 1) ALIAS(op, "x0, %s, %s", csr, t[0]);
            if (n == 2) ALIAS(op, "%s, %s, %s", t[0], csr, t[1]);
            return set_err(err, lineno, ASM_ERR_SYNTAX, "expected '%s [rd,] rs'", mn);
        }
    }

    /* generic CSR forms (standard operand order: csr first for writes) */
    if (strcmp(mn, "csrr") == 0)  { NEED(2, "rd, csr");   ALIAS("csrrs",  "%s, %s, x0", t[0], t[1]); }
    if (strcmp(mn, "csrw") == 0)  { NEED(2, "csr, rs");   ALIAS("csrrw",  "x0, %s, %s", t[0], t[1]); }
    if (strcmp(mn, "csrs") == 0)  { NEED(2, "csr, rs");   ALIAS("csrrs",  "x0, %s, %s", t[0], t[1]); }
    if (strcmp(mn, "csrc") == 0)  { NEED(2, "csr, rs");   ALIAS("csrrc",  "x0, %s, %s", t[0], t[1]); }
    if (strcmp(mn, "csrwi") == 0) { NEED(2, "csr, imm");  ALIAS("csrrwi", "x0, %s, %s", t[0], t[1]); }
    if (strcmp(mn, "csrsi") == 0) { NEED(2, "csr, imm");  ALIAS("csrrsi", "x0, %s, %s", t[0], t[1]); }
    if (strcmp(mn, "csrci") == 0) { NEED(2, "csr, imm");  ALIAS("csrrci", "x0, %s, %s", t[0], t[1]); }

#undef ALIAS
#undef NEED
    return set_err(err, lineno, ASM_ERR_UNKNOWN_MNEMONIC, "unknown alias '%s'", mn);
}

/* ------------------------------------------------------------------ */
/* Directives (.word / .half / .byte / .float / .align / .text / ...) */
/* ------------------------------------------------------------------ */

static int directive_element_size(const char *mnemonic) {
    if (strcmp(mnemonic, ".word") == 0) return 4;
    if (strcmp(mnemonic, ".float") == 0) return 4;   /* Lab 5: IEEE 754 single */
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
        snprintf(lines[count].text, sizeof(lines[count].text), "%s", raw);
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

        if (is_alias(mnemonic)) { addr += 4; continue; }

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
                if (strcmp(mnemonic, ".float") == 0) {
                    /* strtof rounds the decimal literal to nearest-even;
                     * memcpy (not a pointer cast) takes its bit pattern. */
                    char *endp;
                    float fv = strtof(tok[k], &endp);
                    if (endp == tok[k] || *endp != '\0') {
                        return (AsmStatus)set_err(err, lines[i].lineno, ASM_ERR_BAD_OPERAND,
                                                   "'%s' is not a floating-point literal", tok[k]);
                    }
                    uint32_t fb;
                    memcpy(&fb, &fv, sizeof fb);
                    v = (int32_t)fb;
                } else {
                    int st_ok = parse_value(tok[k], &st, addr, 0, &v, err, lines[i].lineno);
                    if (st_ok != ASM_OK) return (AsmStatus)st_ok;
                }
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

        if (is_alias(mnemonic)) {
            char real[32], real_ops[256];
            int al_ok = expand_alias(mnemonic, operands, real, real_ops, sizeof(real_ops), err, lines[i].lineno);
            if (al_ok != ASM_OK) return (AsmStatus)al_ok;
            strcpy(mnemonic, real);
            strcpy(operands, real_ops);
        }

        const InstructionHandler *h = isa_find_by_mnemonic(mnemonic);
        if (!h) {
            return (AsmStatus)set_err(err, lines[i].lineno, ASM_ERR_UNKNOWN_MNEMONIC,
                                       "unknown instruction or directive '%s'", mnemonic);
        }
        Fields f;
        int st_ok = parse_fields(h, operands, &st, addr, &f, err, lines[i].lineno);
        if (st_ok != ASM_OK) return (AsmStatus)st_ok;
        st_ok = validate_fields(h, f, err, lines[i].lineno);
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
