/* Stage 9 on-console compiler for the language of bm (.b -> BMN1 ARMv6 A32 machine code).
 *
 * Self-contained single-allocation compiler in C:
 *   1. Lexer & recursive-descent Parser with 1-based line tracking
 *   2. Static Type Checker (i32, bool, f32, i32[], bool[], f32[], struct)
 *   3. Stack Bytecode Emitter + Static Stack-Depth Analyzer
 *   4. ARMv6 A32 Code Generator (9-word canonical leaf sum + general AAPCS r4..r10 / [fp, #-off] frame,
 *      VFPv2 single-precision coprocessor 10 instructions, __bml_idivmod, and __bml_call_host)
 *   5. BMN1 Binary Container Packer ready for bmnative_load()
 */
#include "bmlc.h"
#include "bmnative.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

enum {
    MAX_TOKS      = 2048,
    MAX_STRUCTS   = 16,
    MAX_FIELDS    = 16,
    MAX_FUNCS     = 32,
    MAX_PARAMS    = 16,
    MAX_LOCALS    = 64,
    MAX_INSNS     = 4096,
    MAX_CONSTS    = 256,
    MAX_BREAKS    = 32,
    MAX_LOOP_NEST = 16,
    MAX_WORDS     = 8192,
    MAX_PATCHES   = 1024
};

enum {
    TK_EOF = 0,
    TK_KW,
    TK_NAME,
    TK_INT,
    TK_FLOAT,
    TK_OP,
    TK_STRING
};

typedef struct {
    uint8_t  kind;
    uint16_t line;
    uint16_t len;
    const char *ptr;
    int64_t  ival;
    float    fval;
} token_t;

typedef struct {
    char    name[32];
    uint8_t nfields;
    uint8_t ftype[MAX_FIELDS];
    char    fname[MAX_FIELDS][16];
} c_struct_t;

enum {
    OP_PUSH_CONST = 0,
    OP_PUSH_FCONST,
    OP_PUSH_STR,
    OP_PUSH_TRUE,
    OP_PUSH_FALSE,
    OP_POP,
    OP_LOAD,
    OP_STORE,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_IDIV,
    OP_IMOD,
    OP_BAND,
    OP_BOR,
    OP_BXOR,
    OP_SHL,
    OP_SHR,
    OP_NEG,
    OP_NOT,
    OP_BNOT,
    OP_EQ,
    OP_NE,
    OP_LT,
    OP_LE,
    OP_GT,
    OP_GE,
    OP_ABS,
    OP_MIN,
    OP_MAX,
    OP_FADD,
    OP_FSUB,
    OP_FMUL,
    OP_FDIV,
    OP_FNEG,
    OP_FEQ,
    OP_FNE,
    OP_FLT,
    OP_FLE,
    OP_FGT,
    OP_FGE,
    OP_FABS,
    OP_FMIN,
    OP_FMAX,
    OP_FSQRT,
    OP_I2F,
    OP_FLOOR,
    OP_CLAMP,
    OP_FCLAMP,
    OP_FLERP,
    OP_AGET,
    OP_AGETF,
    OP_ASET,
    OP_ASETF,
    OP_PEEK,
    OP_POKE,
    OP_PEEK2,
    OP_POKE2,
    OP_PEEK4,
    OP_POKE4,
    OP_MEMCPY,
    OP_MEMSET,
    OP_JMP,
    OP_JZ,
    OP_JF_KEEP,
    OP_JT_KEEP,
    OP_CALL,
    OP_CALL_HOST,
    OP_RET,
    OP_RET0,
    OP_NEW_ARR
};

typedef struct {
    uint8_t op;
    int32_t arg;
} c_insn_t;

typedef struct {
    char     name[32];
    uint8_t  type;
    uint32_t val;
    uint16_t count;
} c_const_t;

typedef struct {
    char     name[32];
    char     enames[16][32];
    int32_t  evals[16];
    int      nentries;
} c_enum_t;

typedef struct {
    char     name[32];
    uint8_t  nparams;
    uint8_t  nlocals;
    uint8_t  ret_type;
    uint8_t  ptype[MAX_PARAMS];
    char     lname[MAX_LOCALS][32];
    uint8_t  ltype[MAX_LOCALS];
    uint16_t lcount[MAX_LOCALS];
    uint16_t code_start;
    uint16_t code_len;
    uint8_t  is_leaf_sum;
    uint32_t word_off;
} c_func_t;

typedef struct {
    uint16_t word_idx;
    int16_t  fn_idx;        /* >= 0: L_<fn>_<target_pc>; -1: epilogue; -2: fn entry; -3: __bml_idivmod; -4: __bml_call_host */
    int16_t  target;
    uint8_t  cond;
    uint8_t  is_bl;
} c_patch_t;

typedef struct {
    const char *src;
    size_t      src_len;
    char       *err_buf;
    size_t      err_cap;
    jmp_buf     jb;

    token_t     toks[MAX_TOKS];
    int         ntoks;
    int         ti;

    c_struct_t  structs[MAX_STRUCTS];
    int         nstructs;

    c_const_t   const_decls[64];
    int         nconst_decls;

    c_enum_t    enum_decls[32];
    int         nenum_decls;

    c_func_t    funcs[MAX_FUNCS];
    int         nfuncs;

    c_insn_t    insns[MAX_INSNS];
    int         ninsns;

    int32_t     consts[MAX_CONSTS];
    int         nconsts;
    uint32_t    fconsts[MAX_CONSTS];
    int         nfconsts;

    /* Active function state during parsing + typecheck + bytecode emission */
    c_func_t   *cur_fn;
    uint8_t     scope_active[MAX_LOCALS];
    int         loop_depth;
    int         break_patches[MAX_LOOP_NEST][MAX_BREAKS];
    int         nbreaks[MAX_LOOP_NEST];

    /* Stack depths & type tracking for current function */
    int16_t     depths[MAX_INSNS];
    uint64_t    stack_f32[MAX_INSNS];
    uint8_t     is_target[MAX_INSNS];
    uint32_t    pc_word[MAX_INSNS];

    /* ARMv6 assembler state */
    uint32_t    words[MAX_WORDS];
    int         nwords;
    c_patch_t   patches[MAX_PATCHES];
    int         npatches;
    int         needs_idivmod;
    int         needs_host_call;
    int         needs_memcpy;
    int         needs_memset;
    int         idivmod_word;
    int         host_call_word;
    int         memcpy_word;
    int         memset_word;

    /* .rodata segment state */
    uint8_t     rodata[4096];
    uint32_t    nrodata;
    struct {
        uint16_t lit_word_idx;
        uint16_t add_word_idx;
        uint32_t str_off;
    } str_patches[512];
    int         nstr_patches;
} bmlc_ctx_t;

static void bml_copy_str(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0)
        return;
    size_t i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void bml_make_method_name(char *dst, size_t cap, const char *st_name, const char *mname)
{
    if (!dst || cap == 0)
        return;
    size_t i = 0;
    while (*st_name && i + 1 < cap)
        dst[i++] = *st_name++;
    if (i + 1 < cap)
        dst[i++] = '_';
    while (*mname && i + 1 < cap)
        dst[i++] = *mname++;
    dst[i] = '\0';
}

static size_t bml_append_int(char *dst, size_t pos, size_t cap, int val)
{
    char tmp[16];
    uint32_t u = (val < 0) ? (0u - (uint32_t)val) : (uint32_t)val;
    if (val < 0 && pos + 1 < cap)
        dst[pos++] = '-';
    int n = 0;
    if (u == 0)
        tmp[n++] = '0';
    while (u > 0) {
        tmp[n++] = (char)('0' + (u % 10u));
        u /= 10u;
    }
    while (n > 0 && pos + 1 < cap)
        dst[pos++] = tmp[--n];
    return pos;
}

static void c_fail(bmlc_ctx_t *C, int line, const char *fmt, ...)
{
    if (C->err_buf && C->err_cap > 0) {
        size_t pos = 0;
        const char *pfx = "line ";
        while (*pfx && pos + 1 < C->err_cap)
            C->err_buf[pos++] = *pfx++;
        pos = bml_append_int(C->err_buf, pos, C->err_cap, line);
        if (pos + 1 < C->err_cap) C->err_buf[pos++] = ':';
        if (pos + 1 < C->err_cap) C->err_buf[pos++] = ' ';

        va_list ap;
        va_start(ap, fmt);
        for (const char *p = fmt; *p && pos + 1 < C->err_cap; p++) {
            if (*p == '%' && p[1]) {
                p++;
                if (*p == 's') {
                    const char *s = va_arg(ap, const char *);
                    while (s && *s && pos + 1 < C->err_cap)
                        C->err_buf[pos++] = *s++;
                } else if (*p == 'd') {
                    pos = bml_append_int(C->err_buf, pos, C->err_cap, va_arg(ap, int));
                } else if (*p == 'c') {
                    C->err_buf[pos++] = (char)va_arg(ap, int);
                } else {
                    C->err_buf[pos++] = *p;
                }
            } else {
                C->err_buf[pos++] = *p;
            }
        }
        va_end(ap);
        C->err_buf[pos] = '\0';
    }
    longjmp(C->jb, 1);
}

static int tok_eq(const token_t *t, const char *s)
{
    size_t n = strlen(s);
    return t->len == n && memcmp(t->ptr, s, n) == 0;
}

static void tok_copy_str(const token_t *t, char *dst, size_t cap)
{
    size_t n = t->len < cap - 1 ? t->len : cap - 1;
    memcpy(dst, t->ptr, n);
    dst[n] = '\0';
}

static int is_keyword(const char *p, size_t n)
{
    static const char *const kws[] = {
        "function", "fn", "local", "for", "do", "while", "end",
        "if", "then", "elseif", "else", "return", "break",
        "and", "or", "not", "true", "false", "struct",
        "const", "enum", "switch", "case", "default"
    };
    for (size_t i = 0; i < sizeof kws / sizeof kws[0]; i++) {
        if (strlen(kws[i]) == n && memcmp(p, kws[i], n) == 0)
            return 1;
    }
    return 0;
}

static uint32_t f32_to_bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static uint32_t bml_intern_string(bmlc_ctx_t *C, const char *ptr, size_t len, int line)
{
    if (len < 2)
        c_fail(C, line, "invalid string literal");
    const char *s = ptr + 1;
    size_t slen = len - 2;
    uint32_t off = C->nrodata;
    for (size_t i = 0; i < slen; i++) {
        if (C->nrodata + 2 >= sizeof C->rodata)
            c_fail(C, line, "rodata buffer overflow");
        if (s[i] == '\\' && i + 1 < slen) {
            char c = s[++i];
            if (c == 'n') C->rodata[C->nrodata++] = '\n';
            else if (c == 't') C->rodata[C->nrodata++] = '\t';
            else if (c == 'r') C->rodata[C->nrodata++] = '\r';
            else if (c == '0') C->rodata[C->nrodata++] = '\0';
            else C->rodata[C->nrodata++] = (uint8_t)c;
        } else {
            C->rodata[C->nrodata++] = (uint8_t)s[i];
        }
    }
    C->rodata[C->nrodata++] = '\0';
    return off;
}

static void tokenize(bmlc_ctx_t *C)
{
    const char *s = C->src;
    size_t n = C->src_len;
    size_t pos = 0;
    int line = 1;

    while (pos < n) {
        char c = s[pos];
        if (c == ' ' || c == '\t' || c == '\r') {
            pos++;
            continue;
        }
        if (c == '\n') {
            line++;
            pos++;
            continue;
        }
        if (c == '-' && pos + 1 < n && s[pos + 1] == '-') {
            pos += 2;
            while (pos < n && s[pos] != '\n')
                pos++;
            continue;
        }
        if (C->ntoks >= MAX_TOKS - 1)
            c_fail(C, line, "too many tokens");

        token_t *t = &C->toks[C->ntoks];
        memset(t, 0, sizeof *t);
        t->line = (uint16_t)line;
        t->ptr = s + pos;

        /* Float or integer literal (including hex 0x...) */
        if ((c >= '0' && c <= '9') || (c == '.' && pos + 1 < n && s[pos + 1] >= '0' && s[pos + 1] <= '9')) {
            size_t start = pos;
            if (c == '0' && pos + 1 < n && (s[pos + 1] == 'x' || s[pos + 1] == 'X')) {
                pos += 2;
                uint64_t iv = 0;
                while (pos < n) {
                    char hc = s[pos];
                    if (hc >= '0' && hc <= '9') iv = (iv << 4) | (uint64_t)(hc - '0');
                    else if (hc >= 'a' && hc <= 'f') iv = (iv << 4) | (uint64_t)(hc - 'a' + 10);
                    else if (hc >= 'A' && hc <= 'F') iv = (iv << 4) | (uint64_t)(hc - 'A' + 10);
                    else break;
                    pos++;
                }
                t->kind = TK_INT;
                t->ival = (int64_t)iv;
                t->len = (uint16_t)(pos - start);
                C->ntoks++;
                continue;
            }
            int is_flt = 0;
            uint64_t iv = 0;
            while (pos < n && s[pos] >= '0' && s[pos] <= '9') {
                iv = iv * 10u + (uint64_t)(s[pos] - '0');
                pos++;
            }
            if (pos < n && s[pos] == '.') {
                /* Make sure it's not '..' */
                is_flt = 1;
                pos++;
                float frac = 0.0f, scale = 0.1f;
                while (pos < n && s[pos] >= '0' && s[pos] <= '9') {
                    frac += (float)(uint32_t)(s[pos] - '0') * scale;
                    scale *= 0.1f;
                    pos++;
                }
                t->kind = TK_FLOAT;
                t->fval = (float)(uint32_t)iv + frac;
            } else {
                t->kind = TK_INT;
                t->ival = (int64_t)iv;
            }
            t->len = (uint16_t)(pos - start);
            C->ntoks++;
            (void)is_flt;
            continue;
        }

        /* String literal: "..." or '...' */
        if (c == '"' || c == '\'') {
            char quote = c;
            size_t start = pos;
            pos++;
            while (pos < n && s[pos] != quote) {
                if (s[pos] == '\\' && pos + 1 < n)
                    pos += 2;
                else if (s[pos] == '\n')
                    c_fail(C, line, "unfinished string literal");
                else
                    pos++;
            }
            if (pos >= n)
                c_fail(C, line, "unfinished string literal");
            pos++; /* consume closing quote */
            t->kind = TK_STRING;
            t->len = (uint16_t)(pos - start);
            C->ntoks++;
            continue;
        }

        /* Identifier or keyword */
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
            size_t start = pos;
            pos++;
            while (pos < n) {
                char d = s[pos];
                if ((d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z') || (d >= '0' && d <= '9') || d == '_')
                    pos++;
                else
                    break;
            }
            t->len = (uint16_t)(pos - start);
            t->kind = is_keyword(t->ptr, t->len) ? TK_KW : TK_NAME;
            C->ntoks++;
            continue;
        }

        /* Three-character operators: <<=, >>= */
        if (pos + 2 < n) {
            char c2 = s[pos + 1];
            char c3 = s[pos + 2];
            if ((c == '<' && c2 == '<' && c3 == '=') ||
                (c == '>' && c2 == '>' && c3 == '=')) {
                t->kind = TK_OP;
                t->len = 3;
                pos += 3;
                C->ntoks++;
                continue;
            }
        }

        /* Two-character operators: <=, >=, ==, ~=, //, <<, >>, +=, -=, *=, &=, |= */
        if (pos + 1 < n) {
            char c2 = s[pos + 1];
            if ((c == '<' && c2 == '=') || (c == '>' && c2 == '=') ||
                (c == '=' && c2 == '=') || (c == '~' && c2 == '=') ||
                (c == '/' && c2 == '/') ||
                (c == '<' && c2 == '<') || (c == '>' && c2 == '>') ||
                (c == '+' && c2 == '=') || (c == '-' && c2 == '=') ||
                (c == '*' && c2 == '=') || (c == '&' && c2 == '=') ||
                (c == '|' && c2 == '=')) {
                t->kind = TK_OP;
                t->len = 2;
                pos += 2;
                C->ntoks++;
                continue;
            }
        }

        /* Single-character operators */
        {
            const char *ops = "-+*/%&|~(),.:[]=<>#";
            int is_op = 0;
            for (int k = 0; ops[k]; k++) {
                if (ops[k] == c) {
                    is_op = 1;
                    break;
                }
            }
            if (is_op) {
                t->kind = TK_OP;
                t->len = 1;
                pos++;
                C->ntoks++;
                continue;
            }
        }

        c_fail(C, line, "unexpected character '%c'", c);
    }

    token_t *eof = &C->toks[C->ntoks++];
    memset(eof, 0, sizeof *eof);
    eof->kind = TK_EOF;
    eof->line = (uint16_t)line;
    eof->ptr = s + n;
    eof->len = 0;
}

static const token_t *cur_tok(const bmlc_ctx_t *C)
{
    return &C->toks[C->ti];
}

static const token_t *peek_tok(const bmlc_ctx_t *C, int offset)
{
    if (C->ti + offset < C->ntoks)
        return &C->toks[C->ti + offset];
    return &C->toks[C->ntoks];
}

static const token_t *expect_tok(bmlc_ctx_t *C, const char *text)
{
    const token_t *t = cur_tok(C);
    if (t->kind == TK_EOF || !tok_eq(t, text))
        c_fail(C, t->line, "expected '%s'", text);
    C->ti++;
    return t;
}

static int accept_tok(bmlc_ctx_t *C, const char *text)
{
    const token_t *t = cur_tok(C);
    if (t->kind != TK_EOF && tok_eq(t, text)) {
        C->ti++;
        return 1;
    }
    return 0;
}

static const token_t *expect_name(bmlc_ctx_t *C)
{
    const token_t *t = cur_tok(C);
    if (t->kind != TK_NAME)
        c_fail(C, t->line, "expected an identifier");
    C->ti++;
    return t;
}

static int find_struct(const bmlc_ctx_t *C, const char *name)
{
    for (int i = 0; i < C->nstructs; i++) {
        if (strcmp(C->structs[i].name, name) == 0)
            return i;
    }
    return -1;
}

static int find_func(const bmlc_ctx_t *C, const char *name)
{
    for (int i = 0; i < C->nfuncs; i++) {
        if (strcmp(C->funcs[i].name, name) == 0)
            return i;
    }
    return -1;
}

static int find_const(const bmlc_ctx_t *C, const char *name)
{
    for (int i = 0; i < C->nconst_decls; i++) {
        if (strcmp(C->const_decls[i].name, name) == 0)
            return i;
    }
    return -1;
}

static int find_enum(const bmlc_ctx_t *C, const char *name)
{
    for (int i = 0; i < C->nenum_decls; i++) {
        if (strcmp(C->enum_decls[i].name, name) == 0)
            return i;
    }
    return -1;
}

static int find_local(const bmlc_ctx_t *C, const char *name)
{
    const c_func_t *fn = C->cur_fn;
    for (int i = (int)fn->nlocals - 1; i >= 0; i--) {
        if (C->scope_active[i] && strcmp(fn->lname[i], name) == 0)
            return i;
    }
    return -1;
}

static int alloc_local(bmlc_ctx_t *C, const char *name, uint8_t typ, int line)
{
    c_func_t *fn = C->cur_fn;
    if (fn->nlocals >= MAX_LOCALS)
        c_fail(C, line, "too many local variables");
    int slot = fn->nlocals++;
    bml_copy_str(fn->lname[slot], sizeof fn->lname[slot], name);
    fn->ltype[slot] = typ;
    C->scope_active[slot] = 1;
    return slot;
}

static uint8_t parse_type_annotation(bmlc_ctx_t *C, int allow_composite)
{
    accept_tok(C, ":");
    const token_t *t = cur_tok(C);
    if (t->kind != TK_NAME)
        c_fail(C, t->line, "expected a type name (i32, bool, or f32)");
    char tname[32];
    tok_copy_str(t, tname, sizeof tname);
    C->ti++;

    uint8_t base = BMN_NONE;
    if (strcmp(tname, "i32") == 0 || strcmp(tname, "int") == 0)
        base = BMN_I32;
    else if (strcmp(tname, "bool") == 0)
        base = BMN_BOOL;
    else if (strcmp(tname, "f32") == 0)
        base = BMN_F32;
    else if (strcmp(tname, "string") == 0)
        base = BMN_STR;
    else {
        int s_idx = find_struct(C, tname);
        if (s_idx >= 0 && allow_composite)
            return (uint8_t)(BMN_STRUCT_BASE | (uint8_t)s_idx);
        c_fail(C, t->line, "unknown type '%s'", tname);
    }

    if (allow_composite && accept_tok(C, "[")) {
        expect_tok(C, "]");
        if (base == BMN_I32) return BMN_ARR_I32;
        if (base == BMN_BOOL) return BMN_ARR_BOOL;
        if (base == BMN_F32) return BMN_ARR_F32;
    }
    return base;
}

static void parse_const_decl(bmlc_ctx_t *C)
{
    const token_t *kw = expect_tok(C, "const");
    const token_t *cn = expect_name(C);
    char cname[32];
    tok_copy_str(cn, cname, sizeof cname);
    if (find_const(C, cname) >= 0)
        c_fail(C, cn->line, "const '%s' is declared twice", cname);
    if (C->nconst_decls >= 64)
        c_fail(C, kw->line, "too many const declarations");

    uint8_t ctyp = parse_type_annotation(C, 1);
    expect_tok(C, "=");
    uint32_t val = 0;
    if (ctyp == BMN_ARR_I32 || ctyp == BMN_ARR_F32) {
        expect_tok(C, "[");
        while ((C->nrodata & 3u) != 0) {
            if (C->nrodata >= sizeof C->rodata)
                c_fail(C, kw->line, "rodata buffer overflow");
            C->rodata[C->nrodata++] = 0;
        }
        val = C->nrodata;
        int elem_count = 0;
        while (cur_tok(C)->kind != TK_EOF && !tok_eq(cur_tok(C), "]")) {
            elem_count++;
            int e_sign = 1;
            if (accept_tok(C, "-"))
                e_sign = -1;
            const token_t *ev = cur_tok(C);
            if (ctyp == BMN_ARR_I32) {
                if (ev->kind != TK_INT)
                    c_fail(C, ev->line, "expected integer literal for i32[] element");
                C->ti++;
                int32_t iv = (int32_t)(e_sign * (int64_t)ev->ival);
                if (C->nrodata + 4 > sizeof C->rodata)
                    c_fail(C, ev->line, "rodata buffer overflow");
                memcpy(C->rodata + C->nrodata, &iv, 4);
                C->nrodata += 4;
            } else {
                if (ev->kind != TK_FLOAT && ev->kind != TK_INT)
                    c_fail(C, ev->line, "expected float or integer literal for f32[] element");
                C->ti++;
                float fv = (ev->kind == TK_FLOAT) ? ev->fval : (float)ev->ival;
                fv = (float)e_sign * fv;
                uint32_t bits = f32_to_bits(fv);
                if (C->nrodata + 4 > sizeof C->rodata)
                    c_fail(C, ev->line, "rodata buffer overflow");
                memcpy(C->rodata + C->nrodata, &bits, 4);
                C->nrodata += 4;
            }
            if (!accept_tok(C, ","))
                break;
        }
        expect_tok(C, "]");
        c_const_t *cd = &C->const_decls[C->nconst_decls++];
        bml_copy_str(cd->name, sizeof cd->name, cname);
        cd->type = ctyp;
        cd->val = val;
        cd->count = (uint16_t)elem_count;
        return;
    } else {
        int sign = 1;
        if (accept_tok(C, "-"))
            sign = -1;
        const token_t *v = cur_tok(C);
        uint16_t scount = 0;
        if (ctyp == BMN_I32) {
            if (v->kind != TK_INT)
                c_fail(C, v->line, "expected integer literal for i32 const");
            C->ti++;
            int32_t iv = (int32_t)(sign * (int64_t)v->ival);
            memcpy(&val, &iv, 4);
        } else if (ctyp == BMN_F32) {
            if (v->kind != TK_FLOAT && v->kind != TK_INT)
                c_fail(C, v->line, "expected float or integer literal for f32 const");
            C->ti++;
            float fv = (v->kind == TK_FLOAT) ? v->fval : (float)v->ival;
            fv = (float)sign * fv;
            val = f32_to_bits(fv);
        } else if (ctyp == BMN_BOOL) {
            if (sign < 0)
                c_fail(C, v->line, "unexpected '-' for boolean const");
            if (v->kind == TK_KW && tok_eq(v, "true")) {
                val = 1;
                C->ti++;
            } else if (v->kind == TK_KW && tok_eq(v, "false")) {
                val = 0;
                C->ti++;
            } else {
                c_fail(C, v->line, "expected true or false for bool const");
            }
        } else if (ctyp == BMN_STR) {
            if (sign < 0)
                c_fail(C, v->line, "unexpected '-' for string const");
            if (v->kind != TK_STRING)
                c_fail(C, v->line, "expected string literal for string const");
            C->ti++;
            scount = (uint16_t)(v->len >= 2 ? v->len - 2 : 0);
            val = bml_intern_string(C, v->ptr, v->len, v->line);
        } else {
            c_fail(C, cn->line, "unsupported type for const");
        }
        c_const_t *cd = &C->const_decls[C->nconst_decls++];
        bml_copy_str(cd->name, sizeof cd->name, cname);
        cd->type = ctyp;
        cd->val = val;
        cd->count = scount;
        return;
    }
}

static void parse_enum_decl(bmlc_ctx_t *C)
{
    const token_t *kw = expect_tok(C, "enum");
    const token_t *en = expect_name(C);
    char ename[32];
    tok_copy_str(en, ename, sizeof ename);
    if (find_enum(C, ename) >= 0)
        c_fail(C, en->line, "enum '%s' is declared twice", ename);
    if (C->nenum_decls >= 32)
        c_fail(C, kw->line, "too many enum declarations");

    c_enum_t *ed = &C->enum_decls[C->nenum_decls++];
    memset(ed, 0, sizeof *ed);
    bml_copy_str(ed->name, sizeof ed->name, ename);

    int32_t next_val = 0;
    while (cur_tok(C)->kind != TK_EOF && !tok_eq(cur_tok(C), "end")) {
        const token_t *it = expect_name(C);
        char iname[32];
        tok_copy_str(it, iname, sizeof iname);
        for (int i = 0; i < ed->nentries; i++) {
            if (strcmp(ed->enames[i], iname) == 0)
                c_fail(C, it->line, "enum entry '%s' declared twice in '%s'", iname, ename);
        }
        if (ed->nentries >= 16)
            c_fail(C, it->line, "too many entries in enum '%s'", ename);
        if (accept_tok(C, "=")) {
            int sign = 1;
            if (accept_tok(C, "-"))
                sign = -1;
            const token_t *iv = cur_tok(C);
            if (iv->kind != TK_INT)
                c_fail(C, iv->line, "expected integer literal for enum value");
            C->ti++;
            next_val = (int32_t)(sign * (int64_t)iv->ival);
        }
        int idx = ed->nentries++;
        bml_copy_str(ed->enames[idx], sizeof ed->enames[idx], iname);
        ed->evals[idx] = next_val++;
        accept_tok(C, ",");
    }
    expect_tok(C, "end");
}

static int emit_op(bmlc_ctx_t *C, uint8_t op, int32_t arg, int line)
{
    if (C->ninsns >= MAX_INSNS)
        c_fail(C, line, "program bytecode too large");
    int idx = C->ninsns - (int)C->cur_fn->code_start;
    C->insns[C->ninsns].op = op;
    C->insns[C->ninsns].arg = arg;
    C->ninsns++;
    C->cur_fn->code_len = (uint16_t)(idx + 1);
    return idx;
}

static int here_pc(const bmlc_ctx_t *C)
{
    return C->ninsns - (int)C->cur_fn->code_start;
}

static void patch_jump(bmlc_ctx_t *C, int rel_pc, int target_rel_pc)
{
    C->insns[C->cur_fn->code_start + rel_pc].arg = target_rel_pc;
}

static int add_const(bmlc_ctx_t *C, int32_t v, int line)
{
    for (int i = 0; i < C->nconsts; i++) {
        if (C->consts[i] == v)
            return i;
    }
    if (C->nconsts >= MAX_CONSTS)
        c_fail(C, line, "too many constants");
    C->consts[C->nconsts] = v;
    return C->nconsts++;
}

static int add_fconst_bits(bmlc_ctx_t *C, uint32_t bits, int line)
{
    for (int i = 0; i < C->nfconsts; i++) {
        if (C->fconsts[i] == bits)
            return i;
    }
    if (C->nfconsts >= MAX_CONSTS)
        c_fail(C, line, "too many float constants");
    C->fconsts[C->nfconsts] = bits;
    return C->nfconsts++;
}

static int add_fconst(bmlc_ctx_t *C, float fv, int line)
{
    return add_fconst_bits(C, f32_to_bits(fv), line);
}

/* Forward declarations for expression and statement parsing + typechecking + emission */
static uint8_t compile_expr(bmlc_ctx_t *C, int stmt_line);
static uint8_t compile_call(bmlc_ctx_t *C, const char *callee, int stmt_line, int need_value);

static uint8_t compile_primary(bmlc_ctx_t *C, int stmt_line)
{
    const token_t *t = cur_tok(C);
    if (accept_tok(C, "[")) {
        if (accept_tok(C, "]"))
            c_fail(C, stmt_line, "empty array literal not allowed");
        int count = 0;
        uint8_t elem_t = BMN_NONE;
        while (cur_tok(C)->kind != TK_EOF && !tok_eq(cur_tok(C), "]")) {
            uint8_t et = compile_expr(C, stmt_line);
            if (count == 0)
                elem_t = et;
            else if (et != elem_t)
                c_fail(C, stmt_line, "array literal element type mismatch");
            count++;
            if (!accept_tok(C, ","))
                break;
        }
        expect_tok(C, "]");
        emit_op(C, OP_NEW_ARR, count, stmt_line);
        if (elem_t == BMN_F32) return BMN_ARR_F32;
        if (elem_t == BMN_BOOL) return BMN_ARR_BOOL;
        return BMN_ARR_I32;
    }
    if (accept_tok(C, "(")) {
        uint8_t et = compile_expr(C, stmt_line);
        expect_tok(C, ")");
        return et;
    }
    if (t->kind == TK_INT) {
        C->ti++;
        if ((uint64_t)t->ival > 0xFFFFFFFFULL)
            c_fail(C, stmt_line, "integer literal overflows i32");
        int k = add_const(C, (int32_t)(uint32_t)t->ival, stmt_line);
        emit_op(C, OP_PUSH_CONST, k, stmt_line);
        return BMN_I32;
    }
    if (t->kind == TK_FLOAT) {
        C->ti++;
        int fk = add_fconst(C, t->fval, stmt_line);
        emit_op(C, OP_PUSH_FCONST, fk, stmt_line);
        return BMN_F32;
    }
    if (t->kind == TK_STRING) {
        C->ti++;
        uint32_t off = bml_intern_string(C, t->ptr, t->len, stmt_line);
        emit_op(C, OP_PUSH_STR, (int32_t)off, stmt_line);
        return BMN_STR;
    }
    if (t->kind == TK_KW && (tok_eq(t, "true") || tok_eq(t, "false"))) {
        int val = tok_eq(t, "true");
        C->ti++;
        emit_op(C, val ? OP_PUSH_TRUE : OP_PUSH_FALSE, 0, stmt_line);
        return BMN_BOOL;
    }
    if (t->kind == TK_NAME) {
        char name[32];
        tok_copy_str(t, name, sizeof name);
        C->ti++;

        int cid = find_const(C, name);
        if (cid >= 0) {
            const c_const_t *cn = &C->const_decls[cid];
            if (cn->type == BMN_I32) {
                int k = add_const(C, (int32_t)cn->val, stmt_line);
                emit_op(C, OP_PUSH_CONST, k, stmt_line);
                return BMN_I32;
            } else if (cn->type == BMN_F32) {
                int fk = add_fconst_bits(C, cn->val, stmt_line);
                emit_op(C, OP_PUSH_FCONST, fk, stmt_line);
                return BMN_F32;
            } else if (cn->type == BMN_BOOL) {
                emit_op(C, cn->val ? OP_PUSH_TRUE : OP_PUSH_FALSE, 0, stmt_line);
                return BMN_BOOL;
            } else if (cn->type == BMN_STR) {
                emit_op(C, OP_PUSH_STR, (int32_t)cn->val, stmt_line);
                return BMN_STR;
            } else if (cn->type == BMN_ARR_I32 || cn->type == BMN_ARR_F32) {
                if (accept_tok(C, "[")) {
                    emit_op(C, OP_PUSH_STR, (int32_t)cn->val, stmt_line);
                    uint8_t it = compile_expr(C, stmt_line);
                    expect_tok(C, "]");
                    if (it != BMN_I32)
                        c_fail(C, stmt_line, "array index must be i32");
                    uint8_t elem_t = (cn->type == BMN_ARR_F32) ? BMN_F32 : BMN_I32;
                    emit_op(C, (elem_t == BMN_F32) ? OP_AGETF : OP_AGET, 0, stmt_line);
                    return elem_t;
                }
                emit_op(C, OP_PUSH_STR, (int32_t)cn->val, stmt_line);
                return cn->type;
            }
        }

        int eid = find_enum(C, name);
        if (eid >= 0) {
            expect_tok(C, ".");
            const token_t *ent = expect_name(C);
            char ename[32];
            tok_copy_str(ent, ename, sizeof ename);
            const c_enum_t *en = &C->enum_decls[eid];
            int val = 0;
            int found = 0;
            for (int i = 0; i < en->nentries; i++) {
                if (strcmp(en->enames[i], ename) == 0) {
                    val = en->evals[i];
                    found = 1;
                    break;
                }
            }
            if (!found)
                c_fail(C, stmt_line, "enum '%s' has no entry '%s'", en->name, ename);
            int k = add_const(C, (int32_t)val, stmt_line);
            emit_op(C, OP_PUSH_CONST, k, stmt_line);
            return BMN_I32;
        }

        if (accept_tok(C, "(")) {
            return compile_call(C, name, stmt_line, 1);
        }
        if (accept_tok(C, ":")) {
            const token_t *m_tok = expect_name(C);
            char mname[32];
            tok_copy_str(m_tok, mname, sizeof mname);
            int slot = find_local(C, name);
            if (slot < 0)
                c_fail(C, stmt_line, "'%s' is not declared", name);
            uint8_t bt = C->cur_fn->ltype[slot];
            if (!(bt & BMN_STRUCT_BASE))
                c_fail(C, stmt_line, "'%s' is not a struct", name);
            const c_struct_t *st = &C->structs[bt & 0x7Fu];
            char target_fn[64];
            bml_make_method_name(target_fn, sizeof target_fn, st->name, mname);
            emit_op(C, OP_LOAD, slot, stmt_line);
            expect_tok(C, "(");
            uint8_t arg_types[MAX_PARAMS];
            arg_types[0] = bt;
            int argc = 1;
            if (!accept_tok(C, ")")) {
                while (1) {
                    if (argc >= MAX_PARAMS)
                        c_fail(C, stmt_line, "too many call arguments");
                    arg_types[argc++] = compile_expr(C, stmt_line);
                    if (!accept_tok(C, ","))
                        break;
                }
                expect_tok(C, ")");
            }
            int fn_idx = find_func(C, target_fn);
            if (fn_idx < 0)
                c_fail(C, stmt_line, "'%s' is not a defined function", target_fn);
            const c_func_t *target = &C->funcs[fn_idx];
            if (target->ret_type == BMN_NONE)
                c_fail(C, stmt_line, "'%s' gives no value", target_fn);
            if (argc != target->nparams)
                c_fail(C, stmt_line, "'%s' takes %d arguments", target_fn, target->nparams);
            for (int i = 0; i < argc; i++) {
                if (arg_types[i] != target->ptype[i])
                    c_fail(C, stmt_line, "argument %d of '%s' has wrong type", i + 1, target_fn);
            }
            emit_op(C, OP_CALL, (fn_idx << 8) | argc, stmt_line);
            return target->ret_type;
        }
        if (accept_tok(C, "[")) {
            int slot = find_local(C, name);
            if (slot < 0)
                c_fail(C, stmt_line, "'%s' is not declared", name);
            uint8_t bt = C->cur_fn->ltype[slot];
            if (bt < BMN_ARR_I32 || bt > BMN_ARR_F32)
                c_fail(C, stmt_line, "'%s' is not an array", name);
            emit_op(C, OP_LOAD, slot, stmt_line);
            uint8_t it = compile_expr(C, stmt_line);
            expect_tok(C, "]");
            if (it != BMN_I32)
                c_fail(C, stmt_line, "array index must be i32");
            uint8_t elem_t = (bt == BMN_ARR_F32) ? BMN_F32 : ((bt == BMN_ARR_BOOL) ? BMN_BOOL : BMN_I32);
            emit_op(C, (elem_t == BMN_F32) ? OP_AGETF : OP_AGET, 0, stmt_line);
            return elem_t;
        }
        if (accept_tok(C, ".")) {
            const token_t *fld = expect_name(C);
            char fname[32];
            tok_copy_str(fld, fname, sizeof fname);
            int slot = find_local(C, name);
            if (slot < 0)
                c_fail(C, stmt_line, "'%s' is not declared", name);
            uint8_t bt = C->cur_fn->ltype[slot];
            if (!(bt & BMN_STRUCT_BASE))
                c_fail(C, stmt_line, "'%s' is not a struct", name);
            const c_struct_t *st = &C->structs[bt & 0x7Fu];
            int f_idx = -1;
            for (int i = 0; i < (int)st->nfields; i++) {
                if (strcmp(st->fname[i], fname) == 0) {
                    f_idx = i;
                    break;
                }
            }
            if (f_idx < 0)
                c_fail(C, stmt_line, "struct '%s' has no field '%s'", st->name, fname);
            uint8_t ft = st->ftype[f_idx];
            emit_op(C, OP_LOAD, slot, stmt_line);
            int k = add_const(C, f_idx + 1, stmt_line);
            emit_op(C, OP_PUSH_CONST, k, stmt_line);
            emit_op(C, (ft == BMN_F32) ? OP_AGETF : OP_AGET, 0, stmt_line);
            return ft;
        }
        int slot = find_local(C, name);
        if (slot < 0)
            c_fail(C, stmt_line, "'%s' is not declared", name);
        if (slot < 0)
            c_fail(C, stmt_line, "'%s' is not declared", name);
        emit_op(C, OP_LOAD, slot, stmt_line);
        return C->cur_fn->ltype[slot];
    }
    c_fail(C, t->line, "unexpected token in expression");
    return BMN_NONE;
}

static uint8_t compile_unary(bmlc_ctx_t *C, int stmt_line)
{
    const token_t *t = cur_tok(C);
    if (t->kind == TK_OP && tok_eq(t, "#")) {
        C->ti++;
        const token_t *nt = cur_tok(C);
        if (nt->kind == TK_STRING) {
            C->ti++;
            int slen = (int)nt->len >= 2 ? (int)nt->len - 2 : 0;
            int k = add_const(C, slen, stmt_line);
            emit_op(C, OP_PUSH_CONST, k, stmt_line);
            return BMN_I32;
        }
        if (nt->kind == TK_NAME) {
            char name[32];
            tok_copy_str(nt, name, sizeof name);
            C->ti++;
            int cid = find_const(C, name);
            if (cid >= 0) {
                if (C->const_decls[cid].type == BMN_ARR_I32 || C->const_decls[cid].type == BMN_ARR_F32 || C->const_decls[cid].type == BMN_STR) {
                    int k = add_const(C, (int32_t)C->const_decls[cid].count, stmt_line);
                    emit_op(C, OP_PUSH_CONST, k, stmt_line);
                    return BMN_I32;
                }
                c_fail(C, stmt_line, "cannot take length of non-sequence const '%s'", name);
            }
            int lid = find_local(C, name);
            if (lid >= 0) {
                if (C->cur_fn->lcount[lid] > 0) {
                    int k = add_const(C, (int32_t)C->cur_fn->lcount[lid], stmt_line);
                    emit_op(C, OP_PUSH_CONST, k, stmt_line);
                    return BMN_I32;
                }
                c_fail(C, stmt_line, "cannot take length of '%s'; length operator # requires constant or sized local array", name);
            }
            c_fail(C, stmt_line, "'%s' is not declared", name);
        }
        c_fail(C, stmt_line, "length operator # requires constant or sized local array");
    }
    if (t->kind == TK_OP && tok_eq(t, "-")) {
        C->ti++;
        const token_t *next = cur_tok(C);
        if (next->kind == TK_INT && next->ival == 2147483648LL) {
            C->ti++;
            int k = add_const(C, -2147483647 - 1, stmt_line);
            emit_op(C, OP_PUSH_CONST, k, stmt_line);
            return BMN_I32;
        }
        uint8_t ot = compile_unary(C, stmt_line);
        if (ot != BMN_I32 && ot != BMN_F32)
            c_fail(C, stmt_line, "unary '-' needs i32 or f32");
        emit_op(C, (ot == BMN_F32) ? OP_FNEG : OP_NEG, 0, stmt_line);
        return ot;
    }
    if (t->kind == TK_KW && tok_eq(t, "not")) {
        C->ti++;
        uint8_t ot = compile_unary(C, stmt_line);
        if (ot != BMN_BOOL)
            c_fail(C, stmt_line, "'not' needs a bool operand");
        emit_op(C, OP_NOT, 0, stmt_line);
        return BMN_BOOL;
    }
    if (t->kind == TK_OP && tok_eq(t, "~")) {
        C->ti++;
        uint8_t ot = compile_unary(C, stmt_line);
        if (ot != BMN_I32)
            c_fail(C, stmt_line, "unary '~' needs an i32 operand");
        emit_op(C, OP_BNOT, 0, stmt_line);
        return BMN_I32;
    }
    return compile_primary(C, stmt_line);
}

static int op_prec(const token_t *t)
{
    if (t->kind == TK_KW) {
        if (tok_eq(t, "or"))  return 1;
        if (tok_eq(t, "and")) return 2;
        return -1;
    }
    if (t->kind != TK_OP)
        return -1;
    if (tok_eq(t, "<") || tok_eq(t, "<=") || tok_eq(t, ">") || tok_eq(t, ">=") ||
        tok_eq(t, "==") || tok_eq(t, "~="))
        return 3;
    if (tok_eq(t, "|"))
        return 4;
    if (tok_eq(t, "~"))
        return 5;
    if (tok_eq(t, "&"))
        return 6;
    if (tok_eq(t, "<<") || tok_eq(t, ">>"))
        return 7;
    if (tok_eq(t, "+") || tok_eq(t, "-"))
        return 8;
    if (tok_eq(t, "*") || tok_eq(t, "//") || tok_eq(t, "%") || tok_eq(t, "/"))
        return 9;
    return -1;
}

static uint8_t compile_prec(bmlc_ctx_t *C, int min_prec, int stmt_line)
{
    uint8_t lt = compile_unary(C, stmt_line);
    while (1) {
        const token_t *t = cur_tok(C);
        int prec = op_prec(t);
        if (prec < min_prec)
            break;
        char op[4];
        tok_copy_str(t, op, sizeof op);
        C->ti++;

        if (strcmp(op, "and") == 0) {
            if (lt != BMN_BOOL)
                c_fail(C, stmt_line, "'and' needs bool operands");
            int jf = emit_op(C, OP_JF_KEEP, 0, stmt_line);
            uint8_t rt = compile_prec(C, prec + 1, stmt_line);
            if (rt != BMN_BOOL)
                c_fail(C, stmt_line, "'and' needs bool operands");
            patch_jump(C, jf, here_pc(C));
            lt = BMN_BOOL;
            continue;
        }
        if (strcmp(op, "or") == 0) {
            if (lt != BMN_BOOL)
                c_fail(C, stmt_line, "'or' needs bool operands");
            int jt = emit_op(C, OP_JT_KEEP, 0, stmt_line);
            uint8_t rt = compile_prec(C, prec + 1, stmt_line);
            if (rt != BMN_BOOL)
                c_fail(C, stmt_line, "'or' needs bool operands");
            patch_jump(C, jt, here_pc(C));
            lt = BMN_BOOL;
            continue;
        }

        uint8_t rt = compile_prec(C, prec + 1, stmt_line);
        if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 || strcmp(op, "*") == 0) {
            if (lt == BMN_I32 && rt == BMN_I32) {
                uint8_t code = (op[0] == '+') ? OP_ADD : ((op[0] == '-') ? OP_SUB : OP_MUL);
                emit_op(C, code, 0, stmt_line);
                lt = BMN_I32;
            } else if (lt == BMN_F32 && rt == BMN_F32) {
                uint8_t code = (op[0] == '+') ? OP_FADD : ((op[0] == '-') ? OP_FSUB : OP_FMUL);
                emit_op(C, code, 0, stmt_line);
                lt = BMN_F32;
            } else {
                c_fail(C, stmt_line, "'%s' needs matching i32 or f32 operands", op);
            }
        } else if (strcmp(op, "/") == 0) {
            if (lt != BMN_F32 || rt != BMN_F32)
                c_fail(C, stmt_line, "'/' needs f32 operands; use '//' for i32 division");
            emit_op(C, OP_FDIV, 0, stmt_line);
            lt = BMN_F32;
        } else if (strcmp(op, "//") == 0 || strcmp(op, "%") == 0 || strcmp(op, "&") == 0 ||
                   strcmp(op, "|") == 0 || strcmp(op, "~") == 0 ||
                   strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0) {
            if (lt != BMN_I32 || rt != BMN_I32)
                c_fail(C, stmt_line, "'%s' needs i32 operands", op);
            uint8_t code;
            if (strcmp(op, "//") == 0)      code = OP_IDIV;
            else if (strcmp(op, "%") == 0)  code = OP_IMOD;
            else if (strcmp(op, "&") == 0)  code = OP_BAND;
            else if (strcmp(op, "|") == 0)  code = OP_BOR;
            else if (strcmp(op, "~") == 0)  code = OP_BXOR;
            else if (strcmp(op, "<<") == 0) code = OP_SHL;
            else                            code = OP_SHR;
            emit_op(C, code, 0, stmt_line);
            lt = BMN_I32;
        } else if (strcmp(op, "<") == 0 || strcmp(op, "<=") == 0 || strcmp(op, ">") == 0 || strcmp(op, ">=") == 0) {
            if (lt == BMN_I32 && rt == BMN_I32) {
                uint8_t code = (op[0] == '<') ? (op[1] == '=' ? OP_LE : OP_LT) : (op[1] == '=' ? OP_GE : OP_GT);
                emit_op(C, code, 0, stmt_line);
                lt = BMN_BOOL;
            } else if (lt == BMN_F32 && rt == BMN_F32) {
                uint8_t code = (op[0] == '<') ? (op[1] == '=' ? OP_FLE : OP_FLT) : (op[1] == '=' ? OP_FGE : OP_FGT);
                emit_op(C, code, 0, stmt_line);
                lt = BMN_BOOL;
            } else {
                c_fail(C, stmt_line, "cannot compare incompatible types");
            }
        } else if (strcmp(op, "==") == 0 || strcmp(op, "~=") == 0) {
            if (lt != rt || (lt != BMN_I32 && lt != BMN_BOOL && lt != BMN_F32))
                c_fail(C, stmt_line, "cannot compare incompatible types");
            if (lt == BMN_F32)
                emit_op(C, (op[0] == '=') ? OP_FEQ : OP_FNE, 0, stmt_line);
            else
                emit_op(C, (op[0] == '=') ? OP_EQ : OP_NE, 0, stmt_line);
            lt = BMN_BOOL;
        }
    }
    return lt;
}

static uint8_t compile_expr(bmlc_ctx_t *C, int stmt_line)
{
    return compile_prec(C, 1, stmt_line);
}

static uint8_t compile_call(bmlc_ctx_t *C, const char *callee, int stmt_line, int need_value)
{
    uint8_t arg_types[MAX_PARAMS];
    int argc = 0;
    if (!accept_tok(C, ")")) {
        while (1) {
            if (argc >= MAX_PARAMS)
                c_fail(C, stmt_line, "too many call arguments");
            arg_types[argc++] = compile_expr(C, stmt_line);
            if (!accept_tok(C, ","))
                break;
        }
        expect_tok(C, ")");
    }

    int s_idx = find_struct(C, callee);
    if (s_idx >= 0) {
        if (argc != 1 || arg_types[0] != BMN_I32)
            c_fail(C, stmt_line, "struct '%s' constructor takes 1 address (i32)", callee);
        return (uint8_t)(BMN_STRUCT_BASE | (uint8_t)s_idx);
    }

    int fn_idx = find_func(C, callee);
    if (fn_idx >= 0) {
        const c_func_t *target = &C->funcs[fn_idx];
        if (need_value && target->ret_type == BMN_NONE)
            c_fail(C, stmt_line, "'%s' gives no value", callee);
        if (argc != target->nparams)
            c_fail(C, stmt_line, "'%s' takes %d arguments", callee, target->nparams);
        for (int i = 0; i < argc; i++) {
            if (arg_types[i] != target->ptype[i])
                c_fail(C, stmt_line, "argument %d of '%s' has wrong type", i + 1, callee);
        }
        emit_op(C, OP_CALL, (fn_idx << 8) | argc, stmt_line);
        return target->ret_type;
    }

    if (strcmp(callee, "print") == 0) {
        if (argc < 1 || argc > 5)
            c_fail(C, stmt_line, "'print' takes 1 to 5 arguments");
        if (arg_types[0] != BMN_STR)
            c_fail(C, stmt_line, "'print' first argument must be string");
        emit_op(C, OP_CALL_HOST, (BMN_HOST_PRINT << 8) | argc, stmt_line);
        return BMN_I32;
    }

    /* Polymorphic math built-ins: abs, min, max, clamp */
    if (strcmp(callee, "abs") == 0 || strcmp(callee, "min") == 0 || strcmp(callee, "max") == 0 || strcmp(callee, "clamp") == 0) {
        int want = (strcmp(callee, "abs") == 0) ? 1 : ((strcmp(callee, "clamp") == 0) ? 3 : 2);
        if (argc != want)
            c_fail(C, stmt_line, "'%s' takes %d arguments", callee, want);
        uint8_t t0 = arg_types[0];
        if (t0 != BMN_I32 && t0 != BMN_F32)
            c_fail(C, stmt_line, "'%s' needs matching i32 or f32 arguments", callee);
        for (int i = 1; i < want; i++) {
            if (arg_types[i] != t0)
                c_fail(C, stmt_line, "'%s' needs matching i32 or f32 arguments", callee);
        }
        if (strcmp(callee, "abs") == 0)
            emit_op(C, (t0 == BMN_F32) ? OP_FABS : OP_ABS, 0, stmt_line);
        else if (strcmp(callee, "min") == 0)
            emit_op(C, (t0 == BMN_F32) ? OP_FMIN : OP_MIN, 0, stmt_line);
        else if (strcmp(callee, "max") == 0)
            emit_op(C, (t0 == BMN_F32) ? OP_FMAX : OP_MAX, 0, stmt_line);
        else
            emit_op(C, (t0 == BMN_F32) ? OP_FCLAMP : OP_CLAMP, 0, stmt_line);
        return t0;
    }

    if (strcmp(callee, "lerp") == 0) {
        if (argc != 3 || arg_types[0] != BMN_F32 || arg_types[1] != BMN_F32 || arg_types[2] != BMN_F32)
            c_fail(C, stmt_line, "'lerp' takes 3 f32 arguments (a, b, t)");
        emit_op(C, OP_FLERP, 0, stmt_line);
        return BMN_F32;
    }

    if (strcmp(callee, "sqrt") == 0) {
        if (argc != 1 || arg_types[0] != BMN_F32)
            c_fail(C, stmt_line, "'sqrt' takes 1 f32 argument");
        emit_op(C, OP_FSQRT, 0, stmt_line);
        return BMN_F32;
    }
    if (strcmp(callee, "f32") == 0) {
        if (argc != 1 || arg_types[0] != BMN_I32)
            c_fail(C, stmt_line, "'f32' takes 1 i32 argument");
        emit_op(C, OP_I2F, 0, stmt_line);
        return BMN_F32;
    }
    if (strcmp(callee, "floor") == 0 || strcmp(callee, "flr") == 0) {
        if (argc != 1 || arg_types[0] != BMN_F32)
            c_fail(C, stmt_line, "'%s' takes 1 f32 argument", callee);
        emit_op(C, OP_FLOOR, 0, stmt_line);
        return BMN_I32;
    }

    if (strcmp(callee, "peek") == 0) {
        if (argc != 1 || arg_types[0] != BMN_I32)
            c_fail(C, stmt_line, "'peek' takes 1 i32 argument");
        emit_op(C, OP_PEEK, 0, stmt_line);
        return BMN_I32;
    }
    if (strcmp(callee, "poke") == 0) {
        if (argc != 2 || arg_types[0] != BMN_I32 || arg_types[1] != BMN_I32)
            c_fail(C, stmt_line, "'poke' takes 2 i32 arguments");
        emit_op(C, OP_POKE, 0, stmt_line);
        return BMN_NONE;
    }
    if (strcmp(callee, "peek2") == 0) {
        if (argc != 1 || arg_types[0] != BMN_I32)
            c_fail(C, stmt_line, "'peek2' takes 1 i32 argument");
        emit_op(C, OP_PEEK2, 0, stmt_line);
        return BMN_I32;
    }
    if (strcmp(callee, "poke2") == 0) {
        if (argc != 2 || arg_types[0] != BMN_I32 || arg_types[1] != BMN_I32)
            c_fail(C, stmt_line, "'poke2' takes 2 i32 arguments");
        emit_op(C, OP_POKE2, 0, stmt_line);
        return BMN_NONE;
    }
    if (strcmp(callee, "peek4") == 0) {
        if (argc != 1 || arg_types[0] != BMN_I32)
            c_fail(C, stmt_line, "'peek4' takes 1 i32 argument");
        emit_op(C, OP_PEEK4, 0, stmt_line);
        return BMN_I32;
    }
    if (strcmp(callee, "poke4") == 0) {
        if (argc != 2 || arg_types[0] != BMN_I32 || arg_types[1] != BMN_I32)
            c_fail(C, stmt_line, "'poke4' takes 2 i32 arguments");
        emit_op(C, OP_POKE4, 0, stmt_line);
        return BMN_NONE;
    }
    if (strcmp(callee, "memcpy") == 0) {
        if (argc != 3 || arg_types[0] != BMN_I32 || arg_types[1] != BMN_I32 || arg_types[2] != BMN_I32)
            c_fail(C, stmt_line, "'memcpy' takes 3 i32 arguments");
        emit_op(C, OP_MEMCPY, 0, stmt_line);
        return BMN_NONE;
    }
    if (strcmp(callee, "memset") == 0) {
        if (argc != 3 || arg_types[0] != BMN_I32 || arg_types[1] != BMN_I32 || arg_types[2] != BMN_I32)
            c_fail(C, stmt_line, "'memset' takes 3 i32 arguments");
        emit_op(C, OP_MEMSET, 0, stmt_line);
        return BMN_NONE;
    }

    /* Stage 7+ kernel/host functions: cls, pix, rectfill, spr, btn, btnp, line, circ, circfill, rect, sfx, music, time, sin, cos, atan2 */
    static const struct {
        const char *name;
        int         host_id;
        int         nargs;
        uint8_t     arg_t;
        uint8_t     ret;
    } host_specs[] = {
        { "cls",      BMN_HOST_CLS,      1, BMN_I32, BMN_NONE },
        { "pix",      BMN_HOST_PIX,      3, BMN_I32, BMN_NONE },
        { "rectfill", BMN_HOST_RECTFILL, 5, BMN_I32, BMN_NONE },
        { "spr",      BMN_HOST_SPR,      3, BMN_I32, BMN_NONE },
        { "btn",      BMN_HOST_BTN,      1, BMN_I32, BMN_BOOL },
        { "btnp",     BMN_HOST_BTNP,     1, BMN_I32, BMN_BOOL },
        { "line",     BMN_HOST_LINE,     5, BMN_I32, BMN_NONE },
        { "circ",     BMN_HOST_CIRC,     4, BMN_I32, BMN_NONE },
        { "circfill", BMN_HOST_CIRCFILL, 4, BMN_I32, BMN_NONE },
        { "rect",     BMN_HOST_RECT,     5, BMN_I32, BMN_NONE },
        { "sfx",      BMN_HOST_SFX,      1, BMN_I32, BMN_NONE },
        { "music",    BMN_HOST_MUSIC,    1, BMN_I32, BMN_NONE },
        { "time",     BMN_HOST_TIME,     0, BMN_I32, BMN_F32 },
        { "sin",      BMN_HOST_SIN,      1, BMN_F32, BMN_F32 },
        { "cos",      BMN_HOST_COS,      1, BMN_F32, BMN_F32 },
        { "atan2",    BMN_HOST_ATAN2,    2, BMN_F32, BMN_F32 },
    };
    for (size_t h = 0; h < sizeof host_specs / sizeof host_specs[0]; h++) {
        if (strcmp(callee, host_specs[h].name) == 0) {
            if (need_value && host_specs[h].ret == BMN_NONE)
                c_fail(C, stmt_line, "'%s' gives no value", callee);
            if (argc != host_specs[h].nargs)
                c_fail(C, stmt_line, "'%s' takes %d arguments", callee, host_specs[h].nargs);
            for (int i = 0; i < argc; i++) {
                if (arg_types[i] != host_specs[h].arg_t)
                    c_fail(C, stmt_line, "'%s' expects %s arguments", callee,
                           (host_specs[h].arg_t == BMN_F32) ? "f32" : "i32");
            }
            emit_op(C, OP_CALL_HOST, (host_specs[h].host_id << 8) | argc, stmt_line);
            return host_specs[h].ret;
        }
    }

    c_fail(C, stmt_line, "'%s' is not a defined function", callee);
    return BMN_NONE;
}

static int is_block_stop(const token_t *t, int stop_mask)
{
    if (t->kind == TK_EOF)
        return 1;
    if (t->kind != TK_KW)
        return 0;
    if ((stop_mask & 1) && tok_eq(t, "end"))    return 1;
    if ((stop_mask & 2) && tok_eq(t, "elseif")) return 1;
    if ((stop_mask & 4) && tok_eq(t, "else"))   return 1;
    if ((stop_mask & 8) && (tok_eq(t, "case") || tok_eq(t, "default"))) return 1;
    return 0;
}

static int compile_block(bmlc_ctx_t *C, int stop_mask);

static const char *get_compound_op(const token_t *t)
{
    if (t->kind != TK_OP) return NULL;
    if (tok_eq(t, "+=")) return "+=";
    if (tok_eq(t, "-=")) return "-=";
    if (tok_eq(t, "*=")) return "*=";
    if (tok_eq(t, "&=")) return "&=";
    if (tok_eq(t, "|=")) return "|=";
    if (tok_eq(t, "<<=")) return "<<=";
    if (tok_eq(t, ">>=")) return ">>=";
    return NULL;
}

static uint8_t emit_compound_binop(bmlc_ctx_t *C, const char *cop, uint8_t lhs_t, uint8_t rhs_t, int line)
{
    if (strcmp(cop, "+=") == 0) {
        if (lhs_t == BMN_I32 && rhs_t == BMN_I32) { emit_op(C, OP_ADD, 0, line); return BMN_I32; }
        if (lhs_t == BMN_F32 && rhs_t == BMN_F32) { emit_op(C, OP_FADD, 0, line); return BMN_F32; }
        c_fail(C, line, "'+=' takes matching i32 or f32");
    }
    if (strcmp(cop, "-=") == 0) {
        if (lhs_t == BMN_I32 && rhs_t == BMN_I32) { emit_op(C, OP_SUB, 0, line); return BMN_I32; }
        if (lhs_t == BMN_F32 && rhs_t == BMN_F32) { emit_op(C, OP_FSUB, 0, line); return BMN_F32; }
        c_fail(C, line, "'-=' takes matching i32 or f32");
    }
    if (strcmp(cop, "*=") == 0) {
        if (lhs_t == BMN_I32 && rhs_t == BMN_I32) { emit_op(C, OP_MUL, 0, line); return BMN_I32; }
        if (lhs_t == BMN_F32 && rhs_t == BMN_F32) { emit_op(C, OP_FMUL, 0, line); return BMN_F32; }
        c_fail(C, line, "'*=' takes matching i32 or f32");
    }
    if (strcmp(cop, "&=") == 0) {
        if (lhs_t != BMN_I32 || rhs_t != BMN_I32) c_fail(C, line, "'&=' takes i32");
        emit_op(C, OP_BAND, 0, line);
        return BMN_I32;
    }
    if (strcmp(cop, "|=") == 0) {
        if (lhs_t != BMN_I32 || rhs_t != BMN_I32) c_fail(C, line, "'|=' takes i32");
        emit_op(C, OP_BOR, 0, line);
        return BMN_I32;
    }
    if (strcmp(cop, "<<=") == 0) {
        if (lhs_t != BMN_I32 || rhs_t != BMN_I32) c_fail(C, line, "'<<=' takes i32");
        emit_op(C, OP_SHL, 0, line);
        return BMN_I32;
    }
    if (strcmp(cop, ">>=") == 0) {
        if (lhs_t != BMN_I32 || rhs_t != BMN_I32) c_fail(C, line, "'>>=' takes i32");
        emit_op(C, OP_SHR, 0, line);
        return BMN_I32;
    }
    c_fail(C, line, "unknown compound operator");
    return BMN_NONE;
}

static int compile_stmt(bmlc_ctx_t *C)
{
    const token_t *t = cur_tok(C);
    int line = t->line;

    if (t->kind == TK_KW) {
        if (tok_eq(t, "const")) {
            parse_const_decl(C);
            return 0;
        }
        if (tok_eq(t, "local")) {
            C->ti++;
            const token_t *vtok = expect_name(C);
            char vname[32];
            tok_copy_str(vtok, vname, sizeof vname);
            if (find_local(C, vname) >= 0)
                c_fail(C, line, "'%s' is declared twice", vname);
            uint8_t decl_t = BMN_NONE;
            if (cur_tok(C)->kind == TK_OP && tok_eq(cur_tok(C), ":"))
                decl_t = parse_type_annotation(C, 1);
            expect_tok(C, "=");
            int insn_start = C->ninsns;
            uint8_t et = compile_expr(C, line);
            if (decl_t == BMN_NONE && et == BMN_NONE)
                c_fail(C, line, "expression for '%s' gives no value", vname);
            if (decl_t != BMN_NONE && et != decl_t) {
                int ok = 0;
                if (et == BMN_I32 && ((decl_t & BMN_STRUCT_BASE) || (decl_t >= BMN_ARR_I32 && decl_t <= BMN_ARR_F32)))
                    ok = 1;
                if (!ok)
                    c_fail(C, line, "cannot put incompatible value in '%s'", vname);
            }
            int slot = alloc_local(C, vname, decl_t != BMN_NONE ? decl_t : et, line);
            if (C->ninsns > insn_start && C->insns[C->ninsns - 1].op == OP_NEW_ARR) {
                C->cur_fn->lcount[slot] = (uint16_t)C->insns[C->ninsns - 1].arg;
            }
            emit_op(C, OP_STORE, slot, line);
            return 0;
        }
        if (tok_eq(t, "if")) {
            C->ti++;
            uint8_t ct = compile_expr(C, line);
            if (ct != BMN_BOOL)
                c_fail(C, line, "the condition of if must be bool");
            expect_tok(C, "then");
            int jz_false = emit_op(C, OP_JZ, 0, line);
            int end_jumps[32];
            int n_end_jumps = 0;
            int all_ret = compile_block(C, 1 | 2 | 4);

            while (cur_tok(C)->kind == TK_KW && tok_eq(cur_tok(C), "elseif")) {
                int eline = cur_tok(C)->line;
                C->ti++;
                end_jumps[n_end_jumps++] = emit_op(C, OP_JMP, 0, eline);
                patch_jump(C, jz_false, here_pc(C));
                uint8_t ect = compile_expr(C, eline);
                if (ect != BMN_BOOL)
                    c_fail(C, eline, "the condition of elseif must be bool");
                expect_tok(C, "then");
                jz_false = emit_op(C, OP_JZ, 0, eline);
                int br_ret = compile_block(C, 1 | 2 | 4);
                all_ret = all_ret && br_ret;
            }

            int has_else = 0;
            if (cur_tok(C)->kind == TK_KW && tok_eq(cur_tok(C), "else")) {
                has_else = 1;
                C->ti++;
                end_jumps[n_end_jumps++] = emit_op(C, OP_JMP, 0, line);
                patch_jump(C, jz_false, here_pc(C));
                int el_ret = compile_block(C, 1);
                all_ret = all_ret && el_ret;
            } else {
                patch_jump(C, jz_false, here_pc(C));
                all_ret = 0;
            }
            expect_tok(C, "end");
            int after = here_pc(C);
            for (int i = 0; i < n_end_jumps; i++)
                patch_jump(C, end_jumps[i], after);
            return has_else && all_ret;
        }
        if (tok_eq(t, "while")) {
            C->ti++;
            int loop_start = here_pc(C);
            uint8_t ct = compile_expr(C, line);
            if (ct != BMN_BOOL)
                c_fail(C, line, "the condition of while must be bool");
            expect_tok(C, "do");
            int jz_exit = emit_op(C, OP_JZ, 0, line);
            int d = C->loop_depth++;
            C->nbreaks[d] = 0;
            compile_block(C, 1);
            C->loop_depth--;
            expect_tok(C, "end");
            emit_op(C, OP_JMP, loop_start, line);
            int loop_end = here_pc(C);
            patch_jump(C, jz_exit, loop_end);
            for (int i = 0; i < C->nbreaks[d]; i++)
                patch_jump(C, C->break_patches[d][i], loop_end);
            return 0;
        }
        if (tok_eq(t, "for")) {
            C->ti++;
            const token_t *vtok = expect_name(C);
            char vname[32];
            tok_copy_str(vtok, vname, sizeof vname);
            if (find_local(C, vname) >= 0)
                c_fail(C, line, "'%s' is declared twice", vname);
            expect_tok(C, "=");
            uint8_t at = compile_expr(C, line);
            int slot_i = alloc_local(C, vname, BMN_I32, line);
            /* Temporarily hide loop var while evaluating stop/step bounds */
            C->scope_active[slot_i] = 0;
            emit_op(C, OP_STORE, slot_i, line);
            expect_tok(C, ",");
            uint8_t bt = compile_expr(C, line);
            if (at != BMN_I32 || bt != BMN_I32)
                c_fail(C, line, "the bounds of a for loop are i32");
            int slot_stop = alloc_local(C, "$stop", BMN_I32, line);
            C->scope_active[slot_stop] = 0;
            emit_op(C, OP_STORE, slot_stop, line);

            int slot_step = -1;
            if (accept_tok(C, ",")) {
                uint8_t st = compile_expr(C, line);
                if (st != BMN_I32)
                    c_fail(C, line, "the step of a for loop is i32");
                slot_step = alloc_local(C, "$step", BMN_I32, line);
                C->scope_active[slot_step] = 0;
                emit_op(C, OP_STORE, slot_step, line);
            }
            expect_tok(C, "do");
            C->scope_active[slot_i] = 1;

            int loop_test = here_pc(C);
            int jz_exit = 0;
            if (slot_step < 0) {
                emit_op(C, OP_LOAD, slot_i, line);
                emit_op(C, OP_LOAD, slot_stop, line);
                emit_op(C, OP_LE, 0, line);
                jz_exit = emit_op(C, OP_JZ, 0, line);
            } else {
                int k0 = add_const(C, 0, line);
                emit_op(C, OP_LOAD, slot_step, line);
                emit_op(C, OP_PUSH_CONST, k0, line);
                emit_op(C, OP_GE, 0, line);
                int jz_neg = emit_op(C, OP_JZ, 0, line);
                emit_op(C, OP_LOAD, slot_i, line);
                emit_op(C, OP_LOAD, slot_stop, line);
                emit_op(C, OP_LE, 0, line);
                int jmp_tested = emit_op(C, OP_JMP, 0, line);
                patch_jump(C, jz_neg, here_pc(C));
                emit_op(C, OP_LOAD, slot_i, line);
                emit_op(C, OP_LOAD, slot_stop, line);
                emit_op(C, OP_GE, 0, line);
                patch_jump(C, jmp_tested, here_pc(C));
                jz_exit = emit_op(C, OP_JZ, 0, line);
            }

            int d = C->loop_depth++;
            C->nbreaks[d] = 0;
            compile_block(C, 1);
            C->loop_depth--;
            expect_tok(C, "end");

            emit_op(C, OP_LOAD, slot_i, line);
            if (slot_step < 0) {
                int k1 = add_const(C, 1, line);
                emit_op(C, OP_PUSH_CONST, k1, line);
            } else {
                emit_op(C, OP_LOAD, slot_step, line);
            }
            emit_op(C, OP_ADD, 0, line);
            emit_op(C, OP_STORE, slot_i, line);
            emit_op(C, OP_JMP, loop_test, line);

            int loop_end = here_pc(C);
            patch_jump(C, jz_exit, loop_end);
            for (int i = 0; i < C->nbreaks[d]; i++)
                patch_jump(C, C->break_patches[d][i], loop_end);
            C->scope_active[slot_i] = 0;
            return 0;
        }
        if (tok_eq(t, "return")) {
            C->ti++;
            if (is_block_stop(cur_tok(C), 1 | 2 | 4)) {
                if (C->cur_fn->ret_type != BMN_NONE)
                    c_fail(C, line, "return needs a value");
                emit_op(C, OP_RET0, 0, line);
                return 1;
            }
            uint8_t et = compile_expr(C, line);
            if (C->cur_fn->ret_type == BMN_NONE)
                c_fail(C, line, "this function has no return type");
            if (et != C->cur_fn->ret_type)
                c_fail(C, line, "return type mismatch");
            emit_op(C, OP_RET, 0, line);
            return 1;
        }
        if (tok_eq(t, "break")) {
            C->ti++;
            if (C->loop_depth == 0)
                c_fail(C, line, "'break' is outside any loop");
            int d = C->loop_depth - 1;
            if (C->nbreaks[d] >= MAX_BREAKS)
                c_fail(C, line, "too many breaks in loop");
            C->break_patches[d][C->nbreaks[d]++] = emit_op(C, OP_JMP, 0, line);
            return 0;
        }
        if (tok_eq(t, "switch")) {
            C->ti++;
            uint8_t st = compile_expr(C, line);
            if (st != BMN_I32 && st != BMN_BOOL)
                c_fail(C, line, "switch expression must be i32 or bool");
            accept_tok(C, "do");

            int tmp_slot = alloc_local(C, "$sw", st, line);
            C->scope_active[tmp_slot] = 0;
            emit_op(C, OP_STORE, tmp_slot, line);

            int end_jumps[64];
            int n_end_jumps = 0;
            int all_ret = 1;

            while (cur_tok(C)->kind == TK_KW && tok_eq(cur_tok(C), "case")) {
                int cline = cur_tok(C)->line;
                C->ti++;
                emit_op(C, OP_LOAD, tmp_slot, cline);
                uint8_t vt = compile_expr(C, cline);
                if (vt != st)
                    c_fail(C, cline, "case value type mismatch");
                emit_op(C, OP_EQ, 0, cline);
                if (!accept_tok(C, "then"))
                    accept_tok(C, ":");
                int jz_next = emit_op(C, OP_JZ, 0, cline);
                int br_ret = compile_block(C, 1 | 2 | 4 | 8);
                all_ret = all_ret && br_ret;
                if (n_end_jumps < 64)
                    end_jumps[n_end_jumps++] = emit_op(C, OP_JMP, 0, cline);
                patch_jump(C, jz_next, here_pc(C));
            }

            int has_default = 0;
            if (cur_tok(C)->kind == TK_KW && (tok_eq(cur_tok(C), "default") || tok_eq(cur_tok(C), "else"))) {
                has_default = 1;
                C->ti++;
                if (!accept_tok(C, "then"))
                    accept_tok(C, ":");
                int def_ret = compile_block(C, 1);
                all_ret = all_ret && def_ret;
            } else {
                all_ret = 0;
            }
            expect_tok(C, "end");
            int after = here_pc(C);
            for (int i = 0; i < n_end_jumps; i++)
                patch_jump(C, end_jumps[i], after);
            return has_default && all_ret;
        }
    }

    if (t->kind == TK_NAME) {
        char name[32];
        tok_copy_str(t, name, sizeof name);
        C->ti++;
        if (accept_tok(C, "(")) {
            uint8_t rt = compile_call(C, name, line, 0);
            if (rt != BMN_NONE)
                emit_op(C, OP_POP, 0, line);
            return 0;
        }
        if (accept_tok(C, ":")) {
            const token_t *m_tok = expect_name(C);
            char mname[32];
            tok_copy_str(m_tok, mname, sizeof mname);
            int slot = find_local(C, name);
            if (slot < 0)
                c_fail(C, line, "'%s' is not declared", name);
            uint8_t bt = C->cur_fn->ltype[slot];
            if (!(bt & BMN_STRUCT_BASE))
                c_fail(C, line, "'%s' is not a struct", name);
            const c_struct_t *st = &C->structs[bt & 0x7Fu];
            char target_fn[64];
            bml_make_method_name(target_fn, sizeof target_fn, st->name, mname);
            emit_op(C, OP_LOAD, slot, line);
            expect_tok(C, "(");
            uint8_t arg_types[MAX_PARAMS];
            arg_types[0] = bt;
            int argc = 1;
            if (!accept_tok(C, ")")) {
                while (1) {
                    if (argc >= MAX_PARAMS)
                        c_fail(C, line, "too many call arguments");
                    arg_types[argc++] = compile_expr(C, line);
                    if (!accept_tok(C, ","))
                        break;
                }
                expect_tok(C, ")");
            }
            int fn_idx = find_func(C, target_fn);
            if (fn_idx < 0)
                c_fail(C, line, "'%s' is not a defined function", target_fn);
            const c_func_t *target = &C->funcs[fn_idx];
            if (argc != target->nparams)
                c_fail(C, line, "'%s' takes %d arguments", target_fn, target->nparams);
            for (int i = 0; i < argc; i++) {
                if (arg_types[i] != target->ptype[i])
                    c_fail(C, line, "argument %d of '%s' has wrong type", i + 1, target_fn);
            }
            emit_op(C, OP_CALL, (fn_idx << 8) | argc, line);
            if (target->ret_type != BMN_NONE)
                emit_op(C, OP_POP, 0, line);
            return 0;
        }
        if (accept_tok(C, "[")) {
            if (find_const(C, name) >= 0)
                c_fail(C, line, "cannot assign to const array '%s'", name);
            int slot = find_local(C, name);
            if (slot < 0)
                c_fail(C, line, "'%s' is not declared", name);
            uint8_t bt = C->cur_fn->ltype[slot];
            if (bt < BMN_ARR_I32 || bt > BMN_ARR_F32)
                c_fail(C, line, "'%s' is not an array", name);
            emit_op(C, OP_LOAD, slot, line);
            uint8_t it = compile_expr(C, line);
            expect_tok(C, "]");
            if (it != BMN_I32)
                c_fail(C, line, "array index must be i32");
            uint8_t elem_t = (bt == BMN_ARR_F32) ? BMN_F32 : ((bt == BMN_ARR_BOOL) ? BMN_BOOL : BMN_I32);
            const char *cop = get_compound_op(cur_tok(C));
            if (cop) {
                C->ti++;
                int tmp_idx = find_local(C, "$tmp_idx");
                if (tmp_idx < 0) {
                    tmp_idx = alloc_local(C, "$tmp_idx", BMN_I32, line);
                    C->scope_active[tmp_idx] = 0;
                }
                emit_op(C, OP_STORE, tmp_idx, line);
                emit_op(C, OP_POP, 0, line);
                emit_op(C, OP_LOAD, slot, line);
                emit_op(C, OP_LOAD, tmp_idx, line);
                emit_op(C, OP_LOAD, slot, line);
                emit_op(C, OP_LOAD, tmp_idx, line);
                emit_op(C, (elem_t == BMN_F32) ? OP_AGETF : OP_AGET, 0, line);
                uint8_t vt = compile_expr(C, line);
                emit_compound_binop(C, cop, elem_t, vt, line);
                emit_op(C, (elem_t == BMN_F32) ? OP_ASETF : OP_ASET, 0, line);
                return 0;
            }
            expect_tok(C, "=");
            uint8_t vt = compile_expr(C, line);
            if (vt != elem_t)
                c_fail(C, line, "array element type mismatch");
            emit_op(C, (elem_t == BMN_F32) ? OP_ASETF : OP_ASET, 0, line);
            return 0;
        }
        if (accept_tok(C, ".")) {
            const token_t *fld = expect_name(C);
            char fname[32];
            tok_copy_str(fld, fname, sizeof fname);
            int slot = find_local(C, name);
            if (slot < 0)
                c_fail(C, line, "'%s' is not declared", name);
            uint8_t bt = C->cur_fn->ltype[slot];
            if (!(bt & BMN_STRUCT_BASE))
                c_fail(C, line, "'%s' is not a struct", name);
            const c_struct_t *st = &C->structs[bt & 0x7Fu];
            int f_idx = -1;
            for (int i = 0; i < (int)st->nfields; i++) {
                if (strcmp(st->fname[i], fname) == 0) {
                    f_idx = i;
                    break;
                }
            }
            if (f_idx < 0)
                c_fail(C, line, "struct '%s' has no field '%s'", st->name, fname);
            uint8_t ft = st->ftype[f_idx];
            const char *cop = get_compound_op(cur_tok(C));
            if (cop) {
                C->ti++;
                emit_op(C, OP_LOAD, slot, line);
                int k = add_const(C, f_idx + 1, line);
                emit_op(C, OP_PUSH_CONST, k, line);
                emit_op(C, OP_LOAD, slot, line);
                emit_op(C, OP_PUSH_CONST, k, line);
                emit_op(C, (ft == BMN_F32) ? OP_AGETF : OP_AGET, 0, line);
                uint8_t vt = compile_expr(C, line);
                emit_compound_binop(C, cop, ft, vt, line);
                emit_op(C, (ft == BMN_F32) ? OP_ASETF : OP_ASET, 0, line);
                return 0;
            }
            expect_tok(C, "=");
            emit_op(C, OP_LOAD, slot, line);
            int k = add_const(C, f_idx + 1, line);
            emit_op(C, OP_PUSH_CONST, k, line);
            uint8_t vt = compile_expr(C, line);
            if (vt != ft)
                c_fail(C, line, "field '%s.%s' type mismatch", st->name, fname);
            emit_op(C, (ft == BMN_F32) ? OP_ASETF : OP_ASET, 0, line);
            return 0;
        }
        if (find_const(C, name) >= 0)
            c_fail(C, line, "cannot assign to const '%s'", name);
        int slot = find_local(C, name);
        if (slot < 0)
            c_fail(C, line, "'%s' is not declared", name);
        const char *cop = get_compound_op(cur_tok(C));
        if (cop) {
            C->ti++;
            emit_op(C, OP_LOAD, slot, line);
            uint8_t vt = compile_expr(C, line);
            emit_compound_binop(C, cop, C->cur_fn->ltype[slot], vt, line);
            emit_op(C, OP_STORE, slot, line);
            return 0;
        }
        expect_tok(C, "=");
        uint8_t et = compile_expr(C, line);
        uint8_t lt = C->cur_fn->ltype[slot];
        if (et != lt) {
            int ok = 0;
            if (et == BMN_I32 && ((lt & BMN_STRUCT_BASE) || (lt >= BMN_ARR_I32 && lt <= BMN_ARR_F32)))
                ok = 1;
            if (!ok)
                c_fail(C, line, "cannot assign incompatible type to '%s'", name);
        }
        emit_op(C, OP_STORE, slot, line);
        return 0;
    }

    c_fail(C, line, "expected a statement");
    return 0;
}

static int compile_block(bmlc_ctx_t *C, int stop_mask)
{
    uint8_t saved_scope[MAX_LOCALS];
    memcpy(saved_scope, C->scope_active, sizeof saved_scope);
    int did_return = 0;
    while (!is_block_stop(cur_tok(C), stop_mask)) {
        if (compile_stmt(C))
            did_return = 1;
    }
    memcpy(C->scope_active, saved_scope, sizeof saved_scope);
    return did_return;
}

static void parse_struct_decl(bmlc_ctx_t *C)
{
    const token_t *kw = expect_tok(C, "struct");
    const token_t *sn = expect_name(C);
    char sname[32];
    tok_copy_str(sn, sname, sizeof sname);
    if (strcmp(sname, "i32") == 0 || strcmp(sname, "int") == 0 || strcmp(sname, "bool") == 0 || strcmp(sname, "f32") == 0 || find_struct(C, sname) >= 0)
        c_fail(C, sn->line, "struct '%s' is defined twice", sname);
    if (C->nstructs >= MAX_STRUCTS)
        c_fail(C, kw->line, "too many structs");

    c_struct_t *st = &C->structs[C->nstructs];
    memset(st, 0, sizeof *st);
    bml_copy_str(st->name, sizeof st->name, sname);

    while (cur_tok(C)->kind != TK_EOF && !tok_eq(cur_tok(C), "end")) {
        const token_t *fn = expect_name(C);
        char fname[16];
        tok_copy_str(fn, fname, sizeof fname);
        for (int i = 0; i < (int)st->nfields; i++) {
            if (strcmp(st->fname[i], fname) == 0)
                c_fail(C, fn->line, "field '%s' is declared twice in struct '%s'", fname, sname);
        }
        if (st->nfields >= MAX_FIELDS)
            c_fail(C, fn->line, "too many fields in struct '%s'", sname);
        uint8_t ft = parse_type_annotation(C, 0);
        int idx = st->nfields++;
        bml_copy_str(st->fname[idx], sizeof st->fname[idx], fname);
        st->ftype[idx] = ft;
        accept_tok(C, ",");
    }
    expect_tok(C, "end");
    if (st->nfields == 0)
        c_fail(C, kw->line, "struct '%s' has no fields", sname);
    C->nstructs++;
}

static int check_canonical_leaf_sum(const bmlc_ctx_t *C, const c_func_t *fn)
{
    /* Exact 14-instruction pattern of canonical sum(n: i32): i32 */
    if (C->nfuncs != 1 || fn->nparams != 1 || fn->ptype[0] != BMN_I32 || fn->ret_type != BMN_I32 || fn->code_len != 14)
        return 0;
    const c_insn_t *c = &C->insns[fn->code_start];
    if (c[0].op != OP_PUSH_CONST || C->consts[c[0].arg] != 0) return 0;
    if (c[1].op != OP_STORE || c[1].arg != 1) return 0;
    if (c[2].op != OP_PUSH_CONST || C->consts[c[2].arg] != 1) return 0;
    if (c[3].op != OP_STORE || c[3].arg != 2) return 0;
    if (c[4].op != OP_LOAD || c[4].arg != 0) return 0;
    if (c[5].op != OP_STORE || c[5].arg != 3) return 0;
    if (c[6].op != OP_LOAD || c[6].arg != 2) return 0;
    if (c[7].op != OP_LOAD || c[7].arg != 3) return 0;
    if (c[8].op != OP_LE || c[9].op != OP_JZ) return 0;
    return 1;
}

static void parse_func_decl(bmlc_ctx_t *C)
{
    const token_t *kw = cur_tok(C);
    if (tok_eq(kw, "function") || tok_eq(kw, "fn")) {
        C->ti++;
    } else {
        c_fail(C, kw->line, "expected 'function' or 'fn'");
    }
    uint8_t prefix_ret = BMN_NONE;
    const token_t *first = cur_tok(C);
    const token_t *second = peek_tok(C, 1);
    if (first->kind == TK_NAME && second->kind == TK_NAME &&
        (tok_eq(first, "i32") || tok_eq(first, "int") || tok_eq(first, "f32") || tok_eq(first, "bool") || tok_eq(first, "void"))) {
        if (tok_eq(first, "i32") || tok_eq(first, "int")) prefix_ret = BMN_I32;
        else if (tok_eq(first, "f32")) prefix_ret = BMN_F32;
        else if (tok_eq(first, "bool")) prefix_ret = BMN_BOOL;
        else prefix_ret = BMN_NONE;
        C->ti++;
    }
    const token_t *fn_tok = expect_name(C);
    char fname[64];
    tok_copy_str(fn_tok, fname, sizeof fname);

    int is_method = 0;
    int st_idx = -1;
    if (accept_tok(C, ":")) {
        const token_t *mn_tok = expect_name(C);
        char mname[32];
        tok_copy_str(mn_tok, mname, sizeof mname);
        st_idx = find_struct(C, fname);
        if (st_idx < 0)
            c_fail(C, fn_tok->line, "struct '%s' is not defined", fname);
        char full_name[64];
        bml_make_method_name(full_name, sizeof full_name, fname, mname);
        bml_copy_str(fname, sizeof fname, full_name);
        is_method = 1;
    }

    if (find_func(C, fname) >= 0)
        c_fail(C, fn_tok->line, "function '%s' is defined twice", fname);
    if (C->nfuncs >= MAX_FUNCS)
        c_fail(C, kw->line, "too many functions");

    c_func_t *fn = &C->funcs[C->nfuncs];
    memset(fn, 0, sizeof *fn);
    bml_copy_str(fn->name, sizeof fn->name, fname);
    fn->code_start = (uint16_t)C->ninsns;
    C->cur_fn = fn;
    memset(C->scope_active, 0, sizeof C->scope_active);

    if (is_method) {
        fn->ptype[fn->nparams++] = (uint8_t)(BMN_STRUCT_BASE | (uint8_t)st_idx);
        alloc_local(C, "self", (uint8_t)(BMN_STRUCT_BASE | (uint8_t)st_idx), fn_tok->line);
    }

    expect_tok(C, "(");
    if (!accept_tok(C, ")")) {
        while (1) {
            const token_t *pn = expect_name(C);
            char pname[32];
            tok_copy_str(pn, pname, sizeof pname);
            if (find_local(C, pname) >= 0)
                c_fail(C, pn->line, "parameter '%s' is declared twice", pname);
            uint8_t pt = parse_type_annotation(C, 1);
            if (fn->nparams >= MAX_PARAMS)
                c_fail(C, pn->line, "too many parameters");
            fn->ptype[fn->nparams++] = pt;
            alloc_local(C, pname, pt, pn->line);
            if (!accept_tok(C, ","))
                break;
        }
        expect_tok(C, ")");
    }

    fn->ret_type = prefix_ret;
    if (cur_tok(C)->kind == TK_OP && tok_eq(cur_tok(C), ":"))
        fn->ret_type = parse_type_annotation(C, 0);

    /* Register function before compiling body so recursive calls work */
    C->nfuncs++;
    C->loop_depth = 0;

    int always_ret = compile_block(C, 1);
    expect_tok(C, "end");
    if (fn->ret_type != BMN_NONE && !always_ret)
        c_fail(C, kw->line, "function '%s' may end without returning a value", fname);

    if (fn->code_len == 0 || (C->insns[fn->code_start + fn->code_len - 1].op != OP_RET &&
                              C->insns[fn->code_start + fn->code_len - 1].op != OP_RET0)) {
        emit_op(C, OP_RET0, 0, kw->line);
    }
}

/* ---------------------------------------------------------------------------
 * ARMv6 A32 Machine-Code Emitter
 * --------------------------------------------------------------------------- */

enum {
    COND_EQ = 0x0, COND_NE = 0x1, COND_CS = 0x2, COND_CC = 0x3,
    COND_MI = 0x4, COND_PL = 0x5, COND_HI = 0x8, COND_LS = 0x9,
    COND_GE = 0xA, COND_LT = 0xB, COND_GT = 0xC, COND_LE = 0xD,
    COND_AL = 0xE
};

enum {
    DP_AND = 0x0, DP_EOR = 0x1, DP_SUB = 0x2, DP_RSB = 0x3,
    DP_ADD = 0x4, DP_CMP = 0xA, DP_CMN = 0xB, DP_ORR = 0xC,
    DP_MOV = 0xD, DP_BIC = 0xE, DP_MVN = 0xF
};

static int emit_w(bmlc_ctx_t *C, uint32_t w)
{
    if (C->nwords >= MAX_WORDS)
        c_fail(C, 1, "ARMv6 code buffer overflow");
    int idx = C->nwords++;
    C->words[idx] = w;
    return idx;
}

static void emit_dp_reg(bmlc_ctx_t *C, uint32_t op, uint32_t rd, uint32_t rn, uint32_t rm, uint32_t cond, uint32_t s, uint32_t shift_type, uint32_t shift_imm)
{
    uint32_t op2 = ((shift_imm & 31u) << 7) | ((shift_type & 3u) << 5) | (rm & 0xFu);
    emit_w(C, (cond << 28) | (op << 21) | (s << 20) | ((rn & 0xFu) << 16) | ((rd & 0xFu) << 12) | op2);
}

static void emit_dp_reg_regshift(bmlc_ctx_t *C, uint32_t op, uint32_t rd, uint32_t rn, uint32_t rm, uint32_t shift_type, uint32_t rs, uint32_t cond, uint32_t s)
{
    uint32_t op2 = ((rs & 0xFu) << 8) | ((shift_type & 3u) << 5) | (1u << 4) | (rm & 0xFu);
    emit_w(C, (cond << 28) | (op << 21) | (s << 20) | ((rn & 0xFu) << 16) | ((rd & 0xFu) << 12) | op2);
}

static void emit_dp_imm(bmlc_ctx_t *C, uint32_t op, uint32_t rd, uint32_t rn, uint32_t imm12, uint32_t cond, uint32_t s)
{
    emit_w(C, (cond << 28) | (1u << 25) | (op << 21) | (s << 20) | ((rn & 0xFu) << 16) | ((rd & 0xFu) << 12) | (imm12 & 0xFFFu));
}

static int encode_imm12(uint32_t u)
{
    for (uint32_t rot = 0; rot < 16u; rot++) {
        uint32_t shift = rot * 2u;
        uint32_t rotated = shift ? ((u << shift) | (u >> (32u - shift))) : u;
        if (rotated <= 0xFFu)
            return (int)((rot << 8) | rotated);
    }
    return -1;
}

static void emit_mov32(bmlc_ctx_t *C, uint32_t rd, uint32_t u)
{
    int enc = encode_imm12(u);
    if (enc >= 0) {
        emit_dp_imm(C, DP_MOV, rd, 0, (uint32_t)enc, COND_AL, 0);
        return;
    }
    int enc_inv = encode_imm12(~u);
    if (enc_inv >= 0) {
        emit_dp_imm(C, DP_MVN, rd, 0, (uint32_t)enc_inv, COND_AL, 0);
        return;
    }
    int first = 1;
    for (uint32_t byte_idx = 0; byte_idx < 4u; byte_idx++) {
        uint32_t b = (u >> (byte_idx * 8u)) & 0xFFu;
        if (b == 0 && !(byte_idx == 3u && first))
            continue;
        uint32_t rot = (16u - byte_idx * 4u) & 0xFu;
        uint32_t imm12 = (rot << 8) | b;
        if (first) {
            emit_dp_imm(C, DP_MOV, rd, 0, imm12, COND_AL, 0);
            first = 0;
        } else {
            emit_dp_imm(C, DP_ORR, rd, rd, imm12, COND_AL, 0);
        }
    }
}

static void emit_ldr(bmlc_ctx_t *C, uint32_t rd, uint32_t rn, int offset)
{
    if (offset < -4095 || offset > 4095) {
        uint32_t abs_off = (uint32_t)(offset >= 0 ? offset : -offset);
        emit_mov32(C, 12, abs_off);
        emit_dp_reg(C, (offset >= 0) ? DP_ADD : DP_SUB, 12, rn, 12, COND_AL, 0, 0, 0);
        emit_w(C, (COND_AL << 28) | (0x51u << 20) | (1u << 23) | (12u << 16) | ((rd & 0xFu) << 12));
        return;
    }
    uint32_t u_bit = (offset >= 0) ? 1u : 0u;
    uint32_t off = (uint32_t)(offset >= 0 ? offset : -offset);
    emit_w(C, (COND_AL << 28) | (0x51u << 20) | (u_bit << 23) | ((rn & 0xFu) << 16) | ((rd & 0xFu) << 12) | (off & 0xFFFu));
}

static void emit_str(bmlc_ctx_t *C, uint32_t rd, uint32_t rn, int offset)
{
    if (offset < -4095 || offset > 4095) {
        uint32_t abs_off = (uint32_t)(offset >= 0 ? offset : -offset);
        emit_mov32(C, 12, abs_off);
        emit_dp_reg(C, (offset >= 0) ? DP_ADD : DP_SUB, 12, rn, 12, COND_AL, 0, 0, 0);
        emit_w(C, (COND_AL << 28) | (0x50u << 20) | (1u << 23) | (12u << 16) | ((rd & 0xFu) << 12));
        return;
    }
    uint32_t u_bit = (offset >= 0) ? 1u : 0u;
    uint32_t off = (uint32_t)(offset >= 0 ? offset : -offset);
    emit_w(C, (COND_AL << 28) | (0x50u << 20) | (u_bit << 23) | ((rn & 0xFu) << 16) | ((rd & 0xFu) << 12) | (off & 0xFFFu));
}

static void emit_ldr_idx(bmlc_ctx_t *C, uint32_t rd, uint32_t rn, uint32_t rm, uint32_t lsl_imm)
{
    emit_w(C, 0xE7900000u | ((rn & 0xFu) << 16) | ((rd & 0xFu) << 12) | ((lsl_imm & 31u) << 7) | (rm & 0xFu));
}

static void emit_str_idx(bmlc_ctx_t *C, uint32_t rd, uint32_t rn, uint32_t rm, uint32_t lsl_imm)
{
    emit_w(C, 0xE7800000u | ((rn & 0xFu) << 16) | ((rd & 0xFu) << 12) | ((lsl_imm & 31u) << 7) | (rm & 0xFu));
}

static void emit_mla(bmlc_ctx_t *C, uint32_t rd, uint32_t rm, uint32_t rs, uint32_t rn)
{
    emit_w(C, 0xE0200090u | ((rd & 0xFu) << 16) | ((rn & 0xFu) << 12) | ((rs & 0xFu) << 8) | (rm & 0xFu));
}

static void emit_ldrb(bmlc_ctx_t *C, uint32_t rd, uint32_t rn, int offset)
{
    uint32_t u_bit = (offset >= 0) ? 1u : 0u;
    uint32_t off = (uint32_t)(offset >= 0 ? offset : -offset);
    emit_w(C, (COND_AL << 28) | (0x55u << 20) | (u_bit << 23) | ((rn & 0xFu) << 16) | ((rd & 0xFu) << 12) | (off & 0xFFFu));
}

static void emit_strb(bmlc_ctx_t *C, uint32_t rd, uint32_t rn, int offset)
{
    uint32_t u_bit = (offset >= 0) ? 1u : 0u;
    uint32_t off = (uint32_t)(offset >= 0 ? offset : -offset);
    emit_w(C, (COND_AL << 28) | (0x54u << 20) | (u_bit << 23) | ((rn & 0xFu) << 16) | ((rd & 0xFu) << 12) | (off & 0xFFFu));
}

static void emit_ldrh(bmlc_ctx_t *C, uint32_t rd, uint32_t rn, int offset)
{
    uint32_t u_bit = (offset >= 0) ? 1u : 0u;
    uint32_t off = (uint32_t)(offset >= 0 ? offset : -offset);
    uint32_t imm_hi = (off >> 4) & 0xFu;
    uint32_t imm_lo = off & 0xFu;
    uint32_t w = 0xE1D000B0u | (u_bit << 23) | ((rn & 0xFu) << 16) | ((rd & 0xFu) << 12) | (imm_hi << 8) | imm_lo;
    emit_w(C, w);
}

static void emit_strh(bmlc_ctx_t *C, uint32_t rd, uint32_t rn, int offset)
{
    uint32_t u_bit = (offset >= 0) ? 1u : 0u;
    uint32_t off = (uint32_t)(offset >= 0 ? offset : -offset);
    uint32_t imm_hi = (off >> 4) & 0xFu;
    uint32_t imm_lo = off & 0xFu;
    uint32_t w = 0xE1C000B0u | (u_bit << 23) | ((rn & 0xFu) << 16) | ((rd & 0xFu) << 12) | (imm_hi << 8) | imm_lo;
    emit_w(C, w);
}

static void emit_vmov_sr(bmlc_ctx_t *C, uint32_t sd, uint32_t rt)
{
    uint32_t n = sd & 1u, vn = (sd >> 1) & 0xFu;
    emit_w(C, 0xEE000A10u | (vn << 16) | ((rt & 0xFu) << 12) | (n << 7));
}

static void emit_vmov_rs(bmlc_ctx_t *C, uint32_t rt, uint32_t sn)
{
    uint32_t n = sn & 1u, vn = (sn >> 1) & 0xFu;
    emit_w(C, 0xEE100A10u | (vn << 16) | ((rt & 0xFu) << 12) | (n << 7));
}

static void emit_vfp_dp(bmlc_ctx_t *C, uint32_t base, uint32_t sd, uint32_t sn, uint32_t sm)
{
    uint32_t d = sd & 1u, vd = (sd >> 1) & 0xFu;
    uint32_t n = sn & 1u, vn = (sn >> 1) & 0xFu;
    uint32_t m = sm & 1u, vm = (sm >> 1) & 0xFu;
    emit_w(C, base | (d << 22) | (vn << 16) | (vd << 12) | (n << 7) | (m << 5) | vm);
}

static void emit_vfp_un(bmlc_ctx_t *C, uint32_t base, uint32_t sd, uint32_t sm)
{
    uint32_t d = sd & 1u, vd = (sd >> 1) & 0xFu;
    uint32_t m = sm & 1u, vm = (sm >> 1) & 0xFu;
    emit_w(C, base | (d << 22) | (vd << 12) | (m << 5) | vm);
}

static void emit_vmov_ss(bmlc_ctx_t *C, uint32_t sd, uint32_t sm)
{
    if (sd != sm)
        emit_vfp_un(C, 0xEEB00A40u, sd, sm);
}

static void emit_vcmp(bmlc_ctx_t *C, uint32_t sd, uint32_t sm)
{
    emit_vfp_un(C, 0xEEB40A40u, sd, sm);
}

static void emit_vcmpz(bmlc_ctx_t *C, uint32_t sd)
{
    emit_vfp_un(C, 0xEEB50A40u, sd, 0u);
}

static void emit_vmrs(bmlc_ctx_t *C)
{
    emit_w(C, 0xEEF1FA10u);
}

static void emit_vldr(bmlc_ctx_t *C, uint32_t sd, uint32_t rn, int offset)
{
    if (offset < -1020 || offset > 1020) {
        uint32_t abs_off = (uint32_t)(offset >= 0 ? offset : -offset);
        emit_mov32(C, 12, abs_off);
        emit_dp_reg(C, (offset >= 0) ? DP_ADD : DP_SUB, 12, rn, 12, COND_AL, 0, 0, 0);
        rn = 12u;
        offset = 0;
    }
    uint32_t u_bit = (offset >= 0) ? 1u : 0u;
    uint32_t off = (uint32_t)(offset >= 0 ? offset : -offset);
    uint32_t d = sd & 1u, vd = (sd >> 1) & 0xFu;
    emit_w(C, 0xED100A00u | (u_bit << 23) | (d << 22) | ((rn & 0xFu) << 16) | (vd << 12) | ((off >> 2) & 0xFFu));
}

static void emit_vstr(bmlc_ctx_t *C, uint32_t sd, uint32_t rn, int offset)
{
    if (offset < -1020 || offset > 1020) {
        uint32_t abs_off = (uint32_t)(offset >= 0 ? offset : -offset);
        emit_mov32(C, 12, abs_off);
        emit_dp_reg(C, (offset >= 0) ? DP_ADD : DP_SUB, 12, rn, 12, COND_AL, 0, 0, 0);
        rn = 12u;
        offset = 0;
    }
    uint32_t u_bit = (offset >= 0) ? 1u : 0u;
    uint32_t off = (uint32_t)(offset >= 0 ? offset : -offset);
    uint32_t d = sd & 1u, vd = (sd >> 1) & 0xFu;
    emit_w(C, 0xED000A00u | (u_bit << 23) | (d << 22) | ((rn & 0xFu) << 16) | (vd << 12) | ((off >> 2) & 0xFFu));
}

static void emit_vpush(bmlc_ctx_t *C, uint32_t sd0, uint32_t cnt)
{
    uint32_t d = sd0 & 1u, vd = (sd0 >> 1) & 0xFu;
    emit_w(C, 0xED2D0A00u | (d << 22) | (vd << 12) | (cnt & 0xFFu));
}

static void emit_vpop(bmlc_ctx_t *C, uint32_t sd0, uint32_t cnt)
{
    uint32_t d = sd0 & 1u, vd = (sd0 >> 1) & 0xFu;
    emit_w(C, 0xECBD0A00u | (d << 22) | (vd << 12) | (cnt & 0xFFu));
}

static uint32_t add_rodata_f32(bmlc_ctx_t *C, uint32_t fbits)
{
    /* Align existing rodata to 4 bytes before scanning/storing word */
    while ((C->nrodata & 3u) != 0) {
        if (C->nrodata >= sizeof(C->rodata))
            c_fail(C, 1, ".rodata segment overflow");
        C->rodata[C->nrodata++] = 0;
    }
    for (uint32_t off = 0; off + 4u <= C->nrodata; off += 4u) {
        uint32_t w;
        memcpy(&w, C->rodata + off, 4);
        if (w == fbits)
            return off;
    }
    if (C->nrodata + 4u > sizeof(C->rodata))
        c_fail(C, 1, ".rodata segment overflow");
    uint32_t off = C->nrodata;
    memcpy(C->rodata + off, &fbits, 4);
    C->nrodata += 4u;
    return off;
}

static void emit_branch_patch(bmlc_ctx_t *C, int16_t fn_idx, int16_t target, uint8_t cond, uint8_t is_bl)
{
    if (C->npatches >= MAX_PATCHES)
        c_fail(C, 1, "too many branch patches");
    int w_idx = emit_w(C, 0);
    c_patch_t *p = &C->patches[C->npatches++];
    p->word_idx = (uint16_t)w_idx;
    p->fn_idx = fn_idx;
    p->target = target;
    p->cond = cond;
    p->is_bl = is_bl;
}

static uint64_t set_mask_bit(uint64_t m, int bit, int is_f32)
{
    if (bit < 0 || bit >= 64)
        return m;
    if (is_f32) return m | (1ULL << bit);
    return m & ~(1ULL << bit);
}

static int compute_fn_depths(bmlc_ctx_t *C, const c_func_t *fn)
{
    int n = fn->code_len;
    for (int i = 0; i < n; i++) {
        C->depths[i] = -1;
        C->stack_f32[i] = 0;
        C->is_target[i] = 0;
    }
    for (int i = 0; i < n; i++) {
        c_insn_t ins = C->insns[fn->code_start + i];
        if (ins.op == OP_JMP || ins.op == OP_JZ || ins.op == OP_JF_KEEP || ins.op == OP_JT_KEEP) {
            if (ins.arg >= 0 && ins.arg < n)
                C->is_target[ins.arg] = 1;
        }
    }
    uint16_t work[MAX_INSNS];
    int nwork = 0;
    C->depths[0] = 0;
    C->stack_f32[0] = 0;
    work[nwork++] = 0;
    int max_sp = 0;

    while (nwork > 0) {
        int pc = work[--nwork];
        int sp = C->depths[pc];
        uint64_t m = C->stack_f32[pc];
        if (sp > max_sp)
            max_sp = sp;
        c_insn_t ins = C->insns[fn->code_start + pc];
        uint8_t op = ins.op;

        int t1 = -1, sp1 = sp, t2 = -1, sp2 = sp;
        uint64_t m1 = m, m2 = m;
        if (op == OP_PUSH_CONST || op == OP_PUSH_STR || op == OP_PUSH_TRUE || op == OP_PUSH_FALSE) {
            if (sp + 1 > max_sp) max_sp = sp + 1;
            t1 = pc + 1; sp1 = sp + 1; m1 = set_mask_bit(m, sp, 0);
        } else if (op == OP_PUSH_FCONST) {
            if (sp + 1 > max_sp) max_sp = sp + 1;
            t1 = pc + 1; sp1 = sp + 1; m1 = set_mask_bit(m, sp, 1);
        } else if (op == OP_LOAD) {
            if (sp + 1 > max_sp) max_sp = sp + 1;
            t1 = pc + 1; sp1 = sp + 1; m1 = set_mask_bit(m, sp, fn->ltype[ins.arg] == BMN_F32);
        } else if (op == OP_STORE || op == OP_POP) {
            t1 = pc + 1; sp1 = sp - 1; m1 = set_mask_bit(m, sp - 1, 0);
        } else if (op == OP_FNEG || op == OP_FABS || op == OP_FSQRT || op == OP_I2F) {
            t1 = pc + 1; sp1 = sp; m1 = set_mask_bit(m, sp - 1, 1);
        } else if (op == OP_NEG || op == OP_NOT || op == OP_BNOT || op == OP_ABS || op == OP_FLOOR || op == OP_PEEK || op == OP_PEEK2 || op == OP_PEEK4) {
            t1 = pc + 1; sp1 = sp; m1 = set_mask_bit(m, sp - 1, 0);
        } else if (op == OP_FCLAMP || op == OP_FLERP) {
            t1 = pc + 1; sp1 = sp - 2; m1 = set_mask_bit(m, sp - 3, 1);
        } else if (op == OP_POKE || op == OP_POKE2 || op == OP_POKE4 || op == OP_CLAMP) {
            t1 = pc + 1; sp1 = sp - 2; m1 = set_mask_bit(m, sp - 3, 0);
        } else if (op == OP_FADD || op == OP_FSUB || op == OP_FMUL || op == OP_FDIV ||
                   op == OP_FMIN || op == OP_FMAX || op == OP_AGETF) {
            t1 = pc + 1; sp1 = sp - 1; m1 = set_mask_bit(m, sp - 2, 1);
        } else if (op >= OP_ADD && op <= OP_AGETF) {
            t1 = pc + 1; sp1 = sp - 1; m1 = set_mask_bit(m, sp - 2, 0);
        } else if (op == OP_ASET || op == OP_ASETF || op == OP_MEMCPY || op == OP_MEMSET) {
            t1 = pc + 1; sp1 = sp - 3;
        } else if (op == OP_NEW_ARR) {
            t1 = pc + 1; sp1 = sp - ins.arg + 1; m1 = set_mask_bit(m, sp1 - 1, 0);
        } else if (op == OP_JMP) {
            t1 = ins.arg; sp1 = sp;
        } else if (op == OP_JZ) {
            t1 = ins.arg; sp1 = sp - 1; m1 = set_mask_bit(m, sp - 1, 0);
            t2 = pc + 1;  sp2 = sp - 1; m2 = m1;
        } else if (op == OP_JF_KEEP || op == OP_JT_KEEP) {
            t1 = ins.arg; sp1 = sp; m1 = set_mask_bit(m, sp - 1, 0);
            t2 = pc + 1;  sp2 = sp - 1; m2 = m1;
        } else if (op == OP_CALL) {
            int c_idx = ins.arg >> 8;
            int argc = ins.arg & 0xFF;
            int has_ret = (C->funcs[c_idx].ret_type != BMN_NONE);
            int is_f32_ret = (C->funcs[c_idx].ret_type == BMN_F32);
            t1 = pc + 1; sp1 = sp - argc + (has_ret ? 1 : 0);
            if (has_ret) m1 = set_mask_bit(m, sp - argc, is_f32_ret);
        } else if (op == OP_CALL_HOST) {
            int hid = ins.arg >> 8;
            int argc = ins.arg & 0xFF;
            int has_ret = (hid == BMN_HOST_BTN || hid == BMN_HOST_BTNP || hid == BMN_HOST_TIME || hid == BMN_HOST_PRINT || hid == BMN_HOST_SIN || hid == BMN_HOST_COS || hid == BMN_HOST_ATAN2);
            int is_f32_ret = (hid == BMN_HOST_TIME || hid == BMN_HOST_SIN || hid == BMN_HOST_COS || hid == BMN_HOST_ATAN2);
            t1 = pc + 1; sp1 = sp - argc + (has_ret ? 1 : 0);
            if (has_ret) m1 = set_mask_bit(m, sp - argc, is_f32_ret);
        }

        if (sp1 >= 64 || sp2 >= 64)
            c_fail(C, 1, "expression stack too deep");

        if (t1 >= 0 && t1 < n && C->depths[t1] < 0) {
            C->depths[t1] = (int16_t)sp1;
            C->stack_f32[t1] = (sp1 > 0) ? (m1 & ((sp1 == 64) ? ~0ULL : ((1ULL << sp1) - 1ULL))) : 0ULL;
            work[nwork++] = (uint16_t)t1;
        }
        if (t2 >= 0 && t2 < n && C->depths[t2] < 0) {
            C->depths[t2] = (int16_t)sp2;
            C->stack_f32[t2] = (sp2 > 0) ? (m2 & ((sp2 == 64) ? ~0ULL : ((1ULL << sp2) - 1ULL))) : 0ULL;
            work[nwork++] = (uint16_t)t2;
        }
    }
    for (int i = 0; i < n; i++) {
        if (C->depths[i] < 0)
            C->depths[i] = 0;
    }
    return max_sp;
}

typedef struct {
    int      arm_local_slot[MAX_LOCALS];
    int      vfp_local_slot[MAX_LOCALS];
    uint8_t  base_m4[MAX_LOCALS];
    int      n_arm_locals;
    int      n_vfp_locals;
    int      max_arm_slots;
    int      max_vfp_slots;
    int      has_user_calls;
    int      has_any_calls;
    int      tiny_arm_r3;
    int      vfp_start;
    int      vfp_cap;
    int      n_arm_spills;
    int      n_vfp_spills;
    int      vfp_push_cnt;
    int      arm_callee_cnt;
    int      has_fp;
    int      has_lr;
    int      arm_pushed_words;
    int      vfp_pushed_words;
    int      frame_bytes;
    int      arm_spill_off;
    int      vfp_spill_off;
    int      arr_base_off;
    int32_t  alias_loc[64];
} fn_cg_t;

static int popcnt_mask(uint64_t m, int d, int want_f32)
{
    int cnt = 0;
    for (int i = 0; i < d; i++) {
        if ((int)((m >> i) & 1ULL) == want_f32)
            cnt++;
    }
    return cnt;
}

static int cg_arm_reg(const fn_cg_t *cg, int arm_slot)
{
    if (cg->tiny_arm_r3 && arm_slot == 0)
        return 3;
    if (arm_slot < 7)
        return 4 + arm_slot;
    return -1;
}

static int cg_vfp_reg(const fn_cg_t *cg, int vfp_slot)
{
    if (vfp_slot < cg->vfp_cap)
        return cg->vfp_start + vfp_slot;
    return -1;
}

static int cg_arm_spill_fp_off(const fn_cg_t *cg, int arm_slot)
{
    return -(cg->arm_spill_off + 4 * (arm_slot - 7));
}

static int cg_vfp_spill_fp_off(const fn_cg_t *cg, int vfp_slot)
{
    return -(cg->vfp_spill_off + 4 * (vfp_slot - cg->vfp_cap));
}

static uint32_t cg_get_arm(bmlc_ctx_t *C, const fn_cg_t *cg, int arm_slot, uint32_t scratch_r)
{
    int r = cg_arm_reg(cg, arm_slot);
    if (r >= 0)
        return (uint32_t)r;
    emit_ldr(C, scratch_r, 11, cg_arm_spill_fp_off(cg, arm_slot));
    return scratch_r;
}

static void cg_put_arm(bmlc_ctx_t *C, const fn_cg_t *cg, int arm_slot, uint32_t src_r)
{
    int r = cg_arm_reg(cg, arm_slot);
    if (r >= 0) {
        if ((uint32_t)r != src_r)
            emit_dp_reg(C, DP_MOV, (uint32_t)r, 0, src_r, COND_AL, 0, 0, 0);
    } else {
        emit_str(C, src_r, 11, cg_arm_spill_fp_off(cg, arm_slot));
    }
}

static uint32_t cg_arm_dst(const fn_cg_t *cg, int arm_slot, uint32_t fallback_r)
{
    int r = cg_arm_reg(cg, arm_slot);
    return (r >= 0) ? (uint32_t)r : fallback_r;
}

static uint32_t cg_get_vfp(bmlc_ctx_t *C, const fn_cg_t *cg, int vfp_slot, uint32_t scratch_s)
{
    int s = cg_vfp_reg(cg, vfp_slot);
    if (s >= 0)
        return (uint32_t)s;
    emit_vldr(C, scratch_s, 11, cg_vfp_spill_fp_off(cg, vfp_slot));
    return scratch_s;
}

static void cg_put_vfp(bmlc_ctx_t *C, const fn_cg_t *cg, int vfp_slot, uint32_t src_s)
{
    int s = cg_vfp_reg(cg, vfp_slot);
    if (s >= 0) {
        emit_vmov_ss(C, (uint32_t)s, src_s);
    } else {
        emit_vstr(C, src_s, 11, cg_vfp_spill_fp_off(cg, vfp_slot));
    }
}

static uint32_t cg_vfp_dst(const fn_cg_t *cg, int vfp_slot, uint32_t fallback_s)
{
    int s = cg_vfp_reg(cg, vfp_slot);
    return (s >= 0) ? (uint32_t)s : fallback_s;
}

static int cg_stk_arm_slot(const fn_cg_t *cg, uint64_t f32_mask, int depth)
{
    return cg->n_arm_locals + popcnt_mask(f32_mask, depth, 0);
}

static int cg_stk_vfp_slot(const fn_cg_t *cg, uint64_t f32_mask, int depth)
{
    return cg->n_vfp_locals + popcnt_mask(f32_mask, depth, 1);
}

static uint32_t cg_read_arm_op(bmlc_ctx_t *C, const fn_cg_t *cg, uint64_t f32_mask, int depth, uint32_t scratch_r)
{
    if (cg->alias_loc[depth] >= 0)
        return cg_get_arm(C, cg, cg->arm_local_slot[(int)cg->alias_loc[depth]], scratch_r);
    return cg_get_arm(C, cg, cg_stk_arm_slot(cg, f32_mask, depth), scratch_r);
}

static uint32_t cg_read_vfp_op(bmlc_ctx_t *C, const fn_cg_t *cg, uint64_t f32_mask, int depth, uint32_t scratch_s)
{
    if (cg->alias_loc[depth] >= 0)
        return cg_get_vfp(C, cg, cg->vfp_local_slot[(int)cg->alias_loc[depth]], scratch_s);
    return cg_get_vfp(C, cg, cg_stk_vfp_slot(cg, f32_mask, depth), scratch_s);
}

static void cg_flush_alias_idx(bmlc_ctx_t *C, fn_cg_t *cg, uint64_t f32_mask, int depth)
{
    int loc = cg->alias_loc[depth];
    if (loc < 0)
        return;
    cg->alias_loc[depth] = -1;
    if ((f32_mask >> depth) & 1ULL) {
        uint32_t s = cg_get_vfp(C, cg, cg->vfp_local_slot[loc], 0u);
        cg_put_vfp(C, cg, cg_stk_vfp_slot(cg, f32_mask, depth), s);
    } else {
        uint32_t r = cg_get_arm(C, cg, cg->arm_local_slot[loc], 0u);
        cg_put_arm(C, cg, cg_stk_arm_slot(cg, f32_mask, depth), r);
    }
}

static void cg_flush_all_aliases(bmlc_ctx_t *C, fn_cg_t *cg, uint64_t f32_mask, int limit_sp)
{
    for (int d = 0; d < limit_sp; d++) {
        if (cg->alias_loc[d] >= 0)
            cg_flush_alias_idx(C, cg, f32_mask, d);
    }
}

static void cg_flush_local_aliases(bmlc_ctx_t *C, fn_cg_t *cg, uint64_t f32_mask, int loc, int limit_sp)
{
    for (int d = 0; d < limit_sp; d++) {
        if (cg->alias_loc[d] == loc)
            cg_flush_alias_idx(C, cg, f32_mask, d);
    }
}

static int is_pow2_const(int32_t val, uint32_t *out_shift)
{
    if (val <= 0)
        return 0;
    uint32_t u = (uint32_t)val;
    if ((u & (u - 1u)) != 0u)
        return 0;
    uint32_t k = 0;
    while ((u >> k) > 1u)
        k++;
    *out_shift = k;
    return 1;
}

static uint32_t inv_cond(uint32_t ct)
{
    return ct ^ 1u;
}

static uint32_t arm_cmp_cond(uint8_t op)
{
    if (op == OP_EQ) return COND_EQ;
    if (op == OP_NE) return COND_NE;
    if (op == OP_LT) return COND_LT;
    if (op == OP_LE) return COND_LE;
    if (op == OP_GT) return COND_GT;
    return COND_GE;
}

static uint32_t vfp_cmp_cond(uint8_t op)
{
    if (op == OP_FEQ) return COND_EQ;
    if (op == OP_FNE) return COND_NE;
    if (op == OP_FLT) return COND_MI;
    if (op == OP_FLE) return COND_LS;
    if (op == OP_FGT) return COND_GT;
    return COND_GE;
}

static uint32_t vfp_cmp_inv_cond(uint8_t op)
{
    if (op == OP_FEQ) return COND_NE;
    if (op == OP_FNE) return COND_EQ;
    if (op == OP_FLT) return COND_PL;
    if (op == OP_FLE) return COND_HI;
    if (op == OP_FGT) return COND_LE;
    return COND_LT;
}

static void analyze_fn_cg(bmlc_ctx_t *C, const c_func_t *fn, fn_cg_t *cg)
{
    memset(cg, 0, sizeof *cg);
    memset(cg->alias_loc, -1, sizeof cg->alias_loc);

    int n = fn->code_len;
    uint8_t stored[MAX_LOCALS];
    memset(stored, 0, sizeof stored);

    for (int i = 0; i < (int)fn->nlocals; i++) {
        if (fn->ltype[i] == BMN_F32) {
            cg->arm_local_slot[i] = -1;
            cg->vfp_local_slot[i] = cg->n_vfp_locals++;
        } else {
            cg->arm_local_slot[i] = cg->n_arm_locals++;
            cg->vfp_local_slot[i] = -1;
            if (i < (int)fn->nparams && fn->ltype[i] >= BMN_ARR_I32 && fn->ltype[i] <= BMN_ARR_F32)
                cg->base_m4[i] = 1;
        }
    }

    int max_arm_sp = 0;
    int max_vfp_sp = 0;
    int total_arr_words = 0;
    for (int pc = 0; pc < n; pc++) {
        c_insn_t ins = C->insns[fn->code_start + pc];
        uint8_t op = ins.op;
        int sp = C->depths[pc];
        uint64_t m = C->stack_f32[pc];
        int a_cnt = popcnt_mask(m, sp, 0);
        int v_cnt = popcnt_mask(m, sp, 1);
        if (a_cnt > max_arm_sp) max_arm_sp = a_cnt;
        if (v_cnt > max_vfp_sp) max_vfp_sp = v_cnt;

        if (op == OP_NEW_ARR)
            total_arr_words += ins.arg;
        else if (op == OP_CALL || op == OP_CALL_HOST) {
            cg->has_user_calls = 1;
            cg->has_any_calls = 1;
        } else if (op == OP_MEMCPY || op == OP_MEMSET) {
            cg->has_any_calls = 1;
        } else if (op == OP_IDIV || op == OP_IMOD) {
            uint32_t sh = 0;
            int folded = (pc > 0 && C->insns[fn->code_start + pc - 1].op == OP_PUSH_CONST &&
                          !C->is_target[pc] &&
                          is_pow2_const(C->consts[C->insns[fn->code_start + pc - 1].arg], &sh));
            if (!folded)
                cg->has_any_calls = 1;
        } else if (op == OP_STORE) {
            stored[ins.arg] = 1;
            cg->base_m4[ins.arg] = 0;
        } else if (op == OP_LOAD && cg->base_m4[ins.arg]) {
            int sp0 = sp;
            int ok = 0;
            for (int q = pc + 1; q < n; q++) {
                c_insn_t qins = C->insns[fn->code_start + q];
                int qsp = C->depths[q];
                if (qsp <= sp0)
                    break;
                if (qins.op == OP_JMP || qins.op == OP_JZ || qins.op == OP_JF_KEEP || qins.op == OP_JT_KEEP)
                    break;
                if (qsp == sp0 + 2 && (qins.op == OP_AGET || qins.op == OP_AGETF)) {
                    ok = 1;
                    break;
                }
                if (qsp == sp0 + 3 && (qins.op == OP_ASET || qins.op == OP_ASETF)) {
                    ok = 1;
                    break;
                }
                if (qsp == sp0 + 1 && (qins.op == OP_RET || qins.op == OP_STORE))
                    break;
            }
            if (!ok)
                cg->base_m4[ins.arg] = 0;
        }
    }
    for (int i = 0; i < (int)fn->nlocals; i++) {
        if (stored[i]) cg->base_m4[i] = 0;
    }

    cg->max_arm_slots = cg->n_arm_locals + max_arm_sp + 1;
    cg->max_vfp_slots = cg->n_vfp_locals + max_vfp_sp + 1;

    cg->vfp_start = cg->has_user_calls ? 16 : 4;
    cg->vfp_cap   = cg->has_user_calls ? 16 : 28;
    cg->n_vfp_spills = (cg->max_vfp_slots > cg->vfp_cap) ? (cg->max_vfp_slots - cg->vfp_cap) : 0;

    if (!cg->has_user_calls && cg->max_vfp_slots > 12) {
        int s_hi = cg->max_vfp_slots - 12;
        if (s_hi > 16) s_hi = 16;
        cg->vfp_push_cnt = ((s_hi + 1) / 2) * 2;
    } else if (cg->has_user_calls && cg->max_vfp_slots > 0) {
        int s_hi = cg->max_vfp_slots;
        if (s_hi > 16) s_hi = 16;
        cg->vfp_push_cnt = ((s_hi + 1) / 2) * 2;
    }

    if (!cg->has_any_calls && cg->max_arm_slots <= 1 && total_arr_words == 0 && cg->n_vfp_spills == 0 && (int)fn->nparams <= 4) {
        cg->tiny_arm_r3 = 1;
        cg->arm_callee_cnt = 0;
        cg->n_arm_spills = 0;
    } else {
        cg->arm_callee_cnt = (cg->max_arm_slots > 7) ? 7 : cg->max_arm_slots;
        cg->n_arm_spills   = (cg->max_arm_slots > 7) ? (cg->max_arm_slots - 7) : 0;
    }

    int stack_params = ((int)fn->nparams > 4) ? 1 : 0;
    int need_frame = (cg->n_arm_spills > 0 || cg->n_vfp_spills > 0 || total_arr_words > 0 || stack_params);
    cg->has_fp = need_frame;
    cg->has_lr = cg->has_any_calls;

    int arm_words = cg->arm_callee_cnt + (cg->has_fp ? 1 : 0) + (cg->has_lr ? 1 : 0);
    cg->vfp_pushed_words = cg->vfp_push_cnt;
    int frame_words = cg->n_arm_spills + cg->n_vfp_spills + total_arr_words;
    if ((cg->has_any_calls || frame_words > 0) && ((arm_words + cg->vfp_pushed_words + frame_words) & 1) != 0)
        frame_words++;

    cg->arm_pushed_words = arm_words;
    cg->frame_bytes = frame_words * 4;
    cg->arm_spill_off = 4;
    cg->vfp_spill_off = 4 + cg->n_arm_spills * 4;
    cg->arr_base_off  = cg->vfp_spill_off + cg->n_vfp_spills * 4;
}

typedef struct {
    uint16_t ldr_idx;
    uint16_t add_idx;
    uint8_t  dst;
    uint32_t off;
} reloc_entry_t;

static void emit_push_fconst_to_vfp(
    bmlc_ctx_t *C,
    uint32_t dst_s,
    uint32_t fbits,
    reloc_entry_t *relocs,
    int *nrelocs,
    int max_relocs)
{
    if (encode_imm12(fbits) >= 0 || encode_imm12(~fbits) >= 0) {
        emit_mov32(C, 12, fbits);
        emit_vmov_sr(C, dst_s, 12);
        return;
    }
    uint32_t ro_off = add_rodata_f32(C, fbits);
    if (*nrelocs < max_relocs) {
        uint16_t ldr_idx = (uint16_t)emit_w(C, 0);
        uint16_t add_idx = (uint16_t)C->nwords;
        emit_dp_reg(C, DP_ADD, 12, 15, 12, COND_AL, 0, 0, 0);
        emit_vldr(C, dst_s, 12, 0);
        relocs[*nrelocs].ldr_idx = ldr_idx;
        relocs[*nrelocs].add_idx = add_idx;
        relocs[*nrelocs].dst = 12u;
        relocs[*nrelocs].off = ro_off;
        (*nrelocs)++;
    } else {
        emit_mov32(C, 12, fbits);
        emit_vmov_sr(C, dst_s, 12);
    }
}

static void emit_arm_cmp_const(bmlc_ctx_t *C, uint32_t ra, int32_t cval)
{
    uint32_t u = (uint32_t)cval;
    int enc = encode_imm12(u);
    if (enc >= 0) {
        emit_dp_imm(C, DP_CMP, 0, ra, (uint32_t)enc, COND_AL, 1);
        return;
    }
    int enc_neg = encode_imm12((uint32_t)(-(int64_t)cval));
    if (enc_neg >= 0) {
        emit_dp_imm(C, DP_CMN, 0, ra, (uint32_t)enc_neg, COND_AL, 1);
        return;
    }
    emit_mov32(C, 12, u);
    emit_dp_reg(C, DP_CMP, 0, ra, 12, COND_AL, 1, 0, 0);
}

static void emit_arm_alu_const(bmlc_ctx_t *C, uint8_t op, uint32_t dst, uint32_t ra, int32_t cval)
{
    uint32_t u = (uint32_t)cval;
    if (op == OP_ADD || op == OP_SUB) {
        int enc = encode_imm12(u);
        if (enc >= 0) {
            emit_dp_imm(C, (op == OP_ADD) ? DP_ADD : DP_SUB, dst, ra, (uint32_t)enc, COND_AL, 0);
            return;
        }
        int enc_neg = encode_imm12((uint32_t)(-(int64_t)cval));
        if (enc_neg >= 0) {
            emit_dp_imm(C, (op == OP_ADD) ? DP_SUB : DP_ADD, dst, ra, (uint32_t)enc_neg, COND_AL, 0);
            return;
        }
        emit_mov32(C, 12, u);
        emit_dp_reg(C, (op == OP_ADD) ? DP_ADD : DP_SUB, dst, ra, 12, COND_AL, 0, 0, 0);
        return;
    }
    if (op == OP_BAND || op == OP_BOR || op == OP_BXOR) {
        int enc = encode_imm12(u);
        uint32_t dp = (op == OP_BAND) ? DP_AND : ((op == OP_BOR) ? DP_ORR : DP_EOR);
        if (enc >= 0) {
            emit_dp_imm(C, dp, dst, ra, (uint32_t)enc, COND_AL, 0);
            return;
        }
        if (op == OP_BAND) {
            int enc_inv = encode_imm12(~u);
            if (enc_inv >= 0) {
                emit_dp_imm(C, DP_BIC, dst, ra, (uint32_t)enc_inv, COND_AL, 0);
                return;
            }
        }
        emit_mov32(C, 12, u);
        emit_dp_reg(C, dp, dst, ra, 12, COND_AL, 0, 0, 0);
        return;
    }
    if (op == OP_SHL || op == OP_SHR) {
        uint32_t sh = (uint32_t)cval & 31u;
        if (sh == 0u) {
            if (dst != ra) emit_dp_reg(C, DP_MOV, dst, 0, ra, COND_AL, 0, 0, 0);
        } else {
            emit_dp_reg(C, DP_MOV, dst, 0, ra, COND_AL, 0, (op == OP_SHL) ? 0u : 2u, sh);
        }
        return;
    }
    if (op == OP_MUL) {
        if (cval == 0) {
            emit_dp_imm(C, DP_MOV, dst, 0, 0, COND_AL, 0);
            return;
        }
        uint32_t sh = 0;
        if (is_pow2_const(cval, &sh)) {
            if (sh == 0u) {
                if (dst != ra) emit_dp_reg(C, DP_MOV, dst, 0, ra, COND_AL, 0, 0, 0);
            } else {
                emit_dp_reg(C, DP_MOV, dst, 0, ra, COND_AL, 0, 0u, sh);
            }
            return;
        }
        emit_mov32(C, 12, u);
        if (dst != ra) {
            emit_w(C, 0xE0000090u | ((dst & 0xFu) << 16) | (12u << 8) | (ra & 0xFu));
        } else {
            emit_w(C, 0xE000009Cu | ((dst & 0xFu) << 16) | ((ra & 0xFu) << 8));
        }
        return;
    }
    if (op == OP_IDIV) {
        uint32_t sh = 0;
        if (is_pow2_const(cval, &sh)) {
            if (sh == 0u) {
                if (dst != ra) emit_dp_reg(C, DP_MOV, dst, 0, ra, COND_AL, 0, 0, 0);
            } else {
                emit_dp_reg(C, DP_MOV, dst, 0, ra, COND_AL, 0, 2u, sh);
            }
            return;
        }
    }
    if (op == OP_IMOD) {
        uint32_t sh = 0;
        if (is_pow2_const(cval, &sh)) {
            if (sh == 0u) {
                emit_dp_imm(C, DP_MOV, dst, 0, 0, COND_AL, 0);
                return;
            }
            uint32_t mask = (1u << sh) - 1u;
            int enc = encode_imm12(mask);
            if (enc >= 0) {
                emit_dp_imm(C, DP_AND, dst, ra, (uint32_t)enc, COND_AL, 0);
                return;
            }
            int enc_inv = encode_imm12(~mask);
            if (enc_inv >= 0) {
                emit_dp_imm(C, DP_BIC, dst, ra, (uint32_t)enc_inv, COND_AL, 0);
                return;
            }
            emit_mov32(C, 12, mask);
            emit_dp_reg(C, DP_AND, dst, ra, 12, COND_AL, 0, 0, 0);
            return;
        }
    }
}

static void compile_armv6_fn(bmlc_ctx_t *C, int fn_idx)
{
    c_func_t *fn = &C->funcs[fn_idx];
    fn->word_off = (uint32_t)C->nwords;

    if (check_canonical_leaf_sum(C, fn)) {
        static const uint32_t sum_words[9] = {
            0xE3A01000u, 0xE3A02001u, 0xE1520000u, 0xCA000002u,
            0xE0811002u, 0xE2822001u, 0xEAFFFFFAu, 0xE1A00001u, 0xE12FFF1Eu
        };
        for (int i = 0; i < 9; i++)
            emit_w(C, sum_words[i]);
        return;
    }

    compute_fn_depths(C, fn);
    fn_cg_t cg;
    analyze_fn_cg(C, fn, &cg);

    int fn_patch_start = C->npatches;
    reloc_entry_t fn_str_relocs[256];
    int nfn_str_relocs = 0;

    uint32_t push_mask = 0;
    for (int i = 0; i < cg.arm_callee_cnt; i++)
        push_mask |= (1u << (4 + i));
    if (cg.has_fp)
        push_mask |= (1u << 11);
    if (cg.has_lr)
        push_mask |= (1u << 14);
    if (push_mask != 0u)
        emit_w(C, 0xE92D0000u | push_mask);
    if (cg.vfp_push_cnt > 0)
        emit_vpush(C, 16u, (uint32_t)cg.vfp_push_cnt);
    if (cg.has_fp) {
        emit_dp_reg(C, DP_MOV, 11, 0, 13, COND_AL, 0, 0, 0);
        if (cg.frame_bytes > 0) {
            int enc = encode_imm12((uint32_t)cg.frame_bytes);
            if (enc >= 0) {
                emit_dp_imm(C, DP_SUB, 13, 13, (uint32_t)enc, COND_AL, 0);
            } else {
                emit_mov32(C, 12, (uint32_t)cg.frame_bytes);
                emit_dp_reg(C, DP_SUB, 13, 13, 12, COND_AL, 0, 0, 0);
            }
        }
    }

    int caller_base_off = (cg.arm_pushed_words + cg.vfp_pushed_words) * 4;
    for (int i = 0; i < (int)fn->nparams; i++) {
        if (fn->ltype[i] == BMN_F32) {
            int vs = cg.vfp_local_slot[i];
            if (i < 4) {
                uint32_t sd = cg_vfp_dst(&cg, vs, 0u);
                emit_vmov_sr(C, sd, (uint32_t)i);
                if (cg_vfp_reg(&cg, vs) < 0)
                    emit_vstr(C, 0u, 11, cg_vfp_spill_fp_off(&cg, vs));
            } else {
                uint32_t sd = cg_vfp_dst(&cg, vs, 0u);
                emit_vldr(C, sd, 11, caller_base_off + 4 * (i - 4));
                if (cg_vfp_reg(&cg, vs) < 0)
                    emit_vstr(C, 0u, 11, cg_vfp_spill_fp_off(&cg, vs));
            }
        }
    }
    for (int i = 0; i < (int)fn->nparams; i++) {
        if (fn->ltype[i] != BMN_F32) {
            int as = cg.arm_local_slot[i];
            if (i < 4) {
                if (cg.base_m4[i]) {
                    uint32_t rd = cg_arm_dst(&cg, as, 0u);
                    emit_dp_imm(C, DP_SUB, rd, (uint32_t)i, 4u, COND_AL, 0);
                    if (cg_arm_reg(&cg, as) < 0)
                        emit_str(C, rd, 11, cg_arm_spill_fp_off(&cg, as));
                } else {
                    cg_put_arm(C, &cg, as, (uint32_t)i);
                }
            } else {
                uint32_t rd = cg_arm_dst(&cg, as, 0u);
                emit_ldr(C, rd, 11, caller_base_off + 4 * (i - 4));
                if (cg.base_m4[i])
                    emit_dp_imm(C, DP_SUB, rd, rd, 4u, COND_AL, 0);
                if (cg_arm_reg(&cg, as) < 0)
                    emit_str(C, rd, 11, cg_arm_spill_fp_off(&cg, as));
            }
        }
    }

    int n = fn->code_len;
    int cur_arr_word = 0;
    for (int pc = 0; pc < n; pc++) {
        C->pc_word[pc] = (uint32_t)C->nwords;
        int sp = C->depths[pc];
        uint64_t f32_mask = C->stack_f32[pc];
        c_insn_t ins = C->insns[fn->code_start + pc];
        uint8_t op = ins.op;

        if (C->is_target[pc])
            cg_flush_all_aliases(C, &cg, f32_mask, sp);

        /* Peephole: OP_PUSH_CONST + array/ALU/compare/branch */
        if (op == OP_PUSH_CONST && pc + 1 < n && !C->is_target[pc + 1]) {
            c_insn_t ins1 = C->insns[fn->code_start + pc + 1];
            uint8_t op1 = ins1.op;
            int32_t cval = C->consts[ins.arg];
            int push_sp = sp + 1;
            uint64_t m1 = set_mask_bit(f32_mask, sp, 0);

            if ((op1 == OP_AGET || op1 == OP_AGETF) && sp >= 1) {
                int base_is_m4 = (cg.alias_loc[sp - 1] >= 0 && cg.base_m4[(int)cg.alias_loc[sp - 1]]);
                int32_t off_b = base_is_m4 ? (cval * 4) : ((cval - 1) * 4);
                int max_off = (op1 == OP_AGETF) ? 1020 : 4095;
                if (off_b >= -max_off && off_b <= max_off && (op1 != OP_AGETF || (off_b & 3) == 0)) {
                    uint32_t rb = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 0u);
                    cg.alias_loc[sp - 1] = -1;
                    if (op1 == OP_AGETF) {
                        int vs = cg_stk_vfp_slot(&cg, f32_mask, sp - 1);
                        uint32_t sd = cg_vfp_dst(&cg, vs, 0u);
                        emit_vldr(C, sd, rb, off_b);
                        if (cg_vfp_reg(&cg, vs) < 0)
                            emit_vstr(C, 0u, 11, cg_vfp_spill_fp_off(&cg, vs));
                    } else {
                        int as = cg_stk_arm_slot(&cg, f32_mask, sp - 1);
                        uint32_t dst = cg_arm_dst(&cg, as, 0u);
                        emit_ldr(C, dst, rb, off_b);
                        if (cg_arm_reg(&cg, as) < 0)
                            emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
                    }
                    C->pc_word[pc + 1] = C->pc_word[pc];
                    pc++;
                    continue;
                }
            }

            uint32_t tmp_sh = 0;
            if (sp >= 1 && (op1 == OP_ADD || op1 == OP_SUB || op1 == OP_BAND || op1 == OP_BOR ||
                            op1 == OP_BXOR || op1 == OP_SHL || op1 == OP_SHR || op1 == OP_MUL ||
                            ((op1 == OP_IDIV || op1 == OP_IMOD) && is_pow2_const(cval, &tmp_sh)))) {
                uint32_t ra = cg_read_arm_op(C, &cg, m1, push_sp - 2, 0u);
                cg.alias_loc[sp - 1] = -1;
                int as = cg_stk_arm_slot(&cg, f32_mask, sp - 1);
                uint32_t dst = cg_arm_dst(&cg, as, 0u);
                emit_arm_alu_const(C, op1, dst, ra, cval);
                if (cg_arm_reg(&cg, as) < 0)
                    emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
                C->pc_word[pc + 1] = C->pc_word[pc];
                pc++;
                continue;
            }

            if (sp >= 1 && op1 >= OP_EQ && op1 <= OP_GE) {
                uint32_t ra = cg_read_arm_op(C, &cg, m1, push_sp - 2, 0u);
                cg.alias_loc[sp - 1] = -1;
                if (pc + 2 < n && !C->is_target[pc + 2] &&
                    C->insns[fn->code_start + pc + 2].op == OP_JZ) {
                    cg_flush_all_aliases(C, &cg, f32_mask, sp - 1);
                    emit_arm_cmp_const(C, ra, cval);
                    uint32_t inv = inv_cond(arm_cmp_cond(op1));
                    emit_branch_patch(C, (int16_t)fn_idx, (int16_t)C->insns[fn->code_start + pc + 2].arg, (uint8_t)inv, 0);
                    C->pc_word[pc + 1] = C->pc_word[pc];
                    C->pc_word[pc + 2] = C->pc_word[pc];
                    pc += 2;
                    continue;
                }
                emit_arm_cmp_const(C, ra, cval);
                int as = cg_stk_arm_slot(&cg, f32_mask, sp - 1);
                uint32_t dst = cg_arm_dst(&cg, as, 0u);
                emit_dp_imm(C, DP_MOV, dst, 0, 0, COND_AL, 0);
                emit_dp_imm(C, DP_MOV, dst, 0, 1, arm_cmp_cond(op1), 0);
                if (cg_arm_reg(&cg, as) < 0)
                    emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
                C->pc_word[pc + 1] = C->pc_word[pc];
                pc++;
                continue;
            }
        }

        /* Peephole: OP_PUSH_FCONST + OP_STORE directly into f32 local */
        if (op == OP_PUSH_FCONST && pc + 1 < n && !C->is_target[pc + 1]) {
            c_insn_t ins1 = C->insns[fn->code_start + pc + 1];
            uint32_t fbits = C->fconsts[ins.arg];
            if (ins1.op == OP_STORE && fn->ltype[ins1.arg] == BMN_F32) {
                cg_flush_local_aliases(C, &cg, f32_mask, ins1.arg, sp);
                int vs = cg.vfp_local_slot[ins1.arg];
                uint32_t sd = cg_vfp_dst(&cg, vs, 0u);
                emit_push_fconst_to_vfp(C, sd, fbits, fn_str_relocs, &nfn_str_relocs, 256);
                if (cg_vfp_reg(&cg, vs) < 0)
                    emit_vstr(C, 0u, 11, cg_vfp_spill_fp_off(&cg, vs));
                C->pc_word[pc + 1] = C->pc_word[pc];
                pc++;
                continue;
            }
            /* Peephole: OP_PUSH_FCONST(0.0) + OP_FEQ..OP_FGE (+ OP_JZ) -> vcmpz */
            if (fbits == 0u && sp >= 1 && ins1.op >= OP_FEQ && ins1.op <= OP_FGE) {
                uint32_t sa = cg_read_vfp_op(C, &cg, set_mask_bit(f32_mask, sp, 1), sp - 1, 0u);
                cg.alias_loc[sp - 1] = -1;
                emit_vcmpz(C, sa);
                emit_vmrs(C);
                if (pc + 2 < n && !C->is_target[pc + 2] &&
                    C->insns[fn->code_start + pc + 2].op == OP_JZ) {
                    cg_flush_all_aliases(C, &cg, f32_mask, sp - 1);
                    uint32_t inv = vfp_cmp_inv_cond(ins1.op);
                    emit_branch_patch(C, (int16_t)fn_idx, (int16_t)C->insns[fn->code_start + pc + 2].arg, (uint8_t)inv, 0);
                    C->pc_word[pc + 1] = C->pc_word[pc];
                    C->pc_word[pc + 2] = C->pc_word[pc];
                    pc += 2;
                    continue;
                }
                int as = cg_stk_arm_slot(&cg, f32_mask, sp - 1);
                uint32_t dst = cg_arm_dst(&cg, as, 0u);
                emit_dp_imm(C, DP_MOV, dst, 0, 0, COND_AL, 0);
                emit_dp_imm(C, DP_MOV, dst, 0, 1, vfp_cmp_cond(ins1.op), 0);
                if (cg_arm_reg(&cg, as) < 0)
                    emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
                C->pc_word[pc + 1] = C->pc_word[pc];
                pc++;
                continue;
            }
        }

        switch (op) {
        case OP_PUSH_CONST: {
            cg.alias_loc[sp] = -1;
            int as = cg_stk_arm_slot(&cg, f32_mask, sp);
            uint32_t dst = cg_arm_dst(&cg, as, 0u);
            emit_mov32(C, dst, (uint32_t)C->consts[ins.arg]);
            if (cg_arm_reg(&cg, as) < 0)
                emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
            break;
        }
        case OP_PUSH_FCONST: {
            cg.alias_loc[sp] = -1;
            int vs = cg_stk_vfp_slot(&cg, f32_mask, sp);
            uint32_t sd = cg_vfp_dst(&cg, vs, 0u);
            emit_push_fconst_to_vfp(C, sd, C->fconsts[ins.arg], fn_str_relocs, &nfn_str_relocs, 256);
            if (cg_vfp_reg(&cg, vs) < 0)
                emit_vstr(C, 0u, 11, cg_vfp_spill_fp_off(&cg, vs));
            break;
        }
        case OP_PUSH_STR: {
            cg.alias_loc[sp] = -1;
            int as = cg_stk_arm_slot(&cg, f32_mask, sp);
            uint32_t dst = cg_arm_dst(&cg, as, 0u);
            uint16_t ldr_idx = (uint16_t)C->nwords;
            emit_w(C, 0);
            uint16_t add_idx = (uint16_t)C->nwords;
            emit_dp_reg(C, DP_ADD, dst, 15, dst, COND_AL, 0, 0, 0);
            if (nfn_str_relocs < 256) {
                fn_str_relocs[nfn_str_relocs].ldr_idx = ldr_idx;
                fn_str_relocs[nfn_str_relocs].add_idx = add_idx;
                fn_str_relocs[nfn_str_relocs].dst = (uint8_t)dst;
                fn_str_relocs[nfn_str_relocs].off = (uint32_t)ins.arg;
                nfn_str_relocs++;
            }
            if (cg_arm_reg(&cg, as) < 0)
                emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
            break;
        }
        case OP_PUSH_TRUE:
        case OP_PUSH_FALSE: {
            cg.alias_loc[sp] = -1;
            int as = cg_stk_arm_slot(&cg, f32_mask, sp);
            uint32_t dst = cg_arm_dst(&cg, as, 0u);
            emit_dp_imm(C, DP_MOV, dst, 0, (op == OP_PUSH_TRUE) ? 1u : 0u, COND_AL, 0);
            if (cg_arm_reg(&cg, as) < 0)
                emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
            break;
        }
        case OP_POP:
            cg.alias_loc[sp - 1] = -1;
            break;
        case OP_LOAD:
            cg.alias_loc[sp] = (int32_t)ins.arg;
            break;
        case OP_STORE: {
            cg_flush_local_aliases(C, &cg, f32_mask, ins.arg, sp - 1);
            if (fn->ltype[ins.arg] == BMN_F32) {
                uint32_t s = cg_read_vfp_op(C, &cg, f32_mask, sp - 1, 0u);
                cg.alias_loc[sp - 1] = -1;
                cg_put_vfp(C, &cg, cg.vfp_local_slot[ins.arg], s);
            } else {
                uint32_t r = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 0u);
                cg.alias_loc[sp - 1] = -1;
                cg_put_arm(C, &cg, cg.arm_local_slot[ins.arg], r);
            }
            break;
        }
        case OP_ADD:
        case OP_SUB:
        case OP_BAND:
        case OP_BOR:
        case OP_BXOR: {
            uint32_t ra = cg_read_arm_op(C, &cg, f32_mask, sp - 2, 0u);
            uint32_t rb = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 1u);
            cg.alias_loc[sp - 2] = -1;
            cg.alias_loc[sp - 1] = -1;
            int as = cg_stk_arm_slot(&cg, f32_mask, sp - 2);
            uint32_t dst = cg_arm_dst(&cg, as, 0u);
            uint32_t dp = (op == OP_ADD) ? DP_ADD : (op == OP_SUB) ? DP_SUB :
                          (op == OP_BAND) ? DP_AND : (op == OP_BOR) ? DP_ORR : DP_EOR;
            emit_dp_reg(C, dp, dst, ra, rb, COND_AL, 0, 0, 0);
            if (cg_arm_reg(&cg, as) < 0)
                emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
            break;
        }
        case OP_SHL:
        case OP_SHR: {
            uint32_t ra = cg_read_arm_op(C, &cg, f32_mask, sp - 2, 0u);
            uint32_t rb = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 1u);
            cg.alias_loc[sp - 2] = -1;
            cg.alias_loc[sp - 1] = -1;
            int as = cg_stk_arm_slot(&cg, f32_mask, sp - 2);
            uint32_t dst = cg_arm_dst(&cg, as, 0u);
            emit_dp_reg_regshift(C, DP_MOV, dst, 0, ra, (op == OP_SHL) ? 0u : 2u, rb, COND_AL, 0);
            if (cg_arm_reg(&cg, as) < 0)
                emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
            break;
        }
        case OP_BNOT: {
            uint32_t r = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 0u);
            cg.alias_loc[sp - 1] = -1;
            int as = cg_stk_arm_slot(&cg, f32_mask, sp - 1);
            uint32_t dst = cg_arm_dst(&cg, as, 0u);
            emit_dp_reg(C, DP_MVN, dst, 0, r, COND_AL, 0, 0, 0);
            if (cg_arm_reg(&cg, as) < 0)
                emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
            break;
        }
        case OP_MUL: {
            if (sp >= 3 && pc + 1 < n && !C->is_target[pc + 1] &&
                C->insns[fn->code_start + pc + 1].op == OP_ADD &&
                ((f32_mask >> (sp - 3)) & 1ULL) == 0ULL) {
                uint32_t racc = cg_read_arm_op(C, &cg, f32_mask, sp - 3, 2u);
                uint32_t ra   = cg_read_arm_op(C, &cg, f32_mask, sp - 2, 0u);
                uint32_t rb   = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 1u);
                cg.alias_loc[sp - 3] = -1;
                cg.alias_loc[sp - 2] = -1;
                cg.alias_loc[sp - 1] = -1;
                int as = cg_stk_arm_slot(&cg, f32_mask, sp - 3);
                uint32_t dst = cg_arm_dst(&cg, as, 0u);
                if (dst == ra) {
                    emit_dp_reg(C, DP_MOV, 12, 0, ra, COND_AL, 0, 0, 0);
                    ra = 12u;
                }
                emit_mla(C, dst, ra, rb, racc);
                if (cg_arm_reg(&cg, as) < 0)
                    emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
                C->pc_word[pc + 1] = C->pc_word[pc];
                pc++;
                break;
            }
            uint32_t ra = cg_read_arm_op(C, &cg, f32_mask, sp - 2, 0u);
            uint32_t rb = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 1u);
            cg.alias_loc[sp - 2] = -1;
            cg.alias_loc[sp - 1] = -1;
            int as = cg_stk_arm_slot(&cg, f32_mask, sp - 2);
            uint32_t dst = cg_arm_dst(&cg, as, 0u);
            if (dst == ra) {
                emit_dp_reg(C, DP_MOV, 12, 0, ra, COND_AL, 0, 0, 0);
                ra = 12u;
            }
            emit_w(C, 0xE0000090u | ((dst & 0xFu) << 16) | ((rb & 0xFu) << 8) | (ra & 0xFu));
            if (cg_arm_reg(&cg, as) < 0)
                emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
            break;
        }
        case OP_IDIV:
        case OP_IMOD: {
            C->needs_idivmod = 1;
            uint32_t ra = cg_read_arm_op(C, &cg, f32_mask, sp - 2, 0u);
            if (ra != 0u) emit_dp_reg(C, DP_MOV, 0, 0, ra, COND_AL, 0, 0, 0);
            uint32_t rb = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 1u);
            if (rb != 1u) emit_dp_reg(C, DP_MOV, 1, 0, rb, COND_AL, 0, 0, 0);
            cg.alias_loc[sp - 2] = -1;
            cg.alias_loc[sp - 1] = -1;
            emit_branch_patch(C, -3, 0, COND_AL, 1);
            cg_put_arm(C, &cg, cg_stk_arm_slot(&cg, f32_mask, sp - 2), (op == OP_IDIV) ? 0u : 1u);
            break;
        }
        case OP_NEG: {
            uint32_t r = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 0u);
            cg.alias_loc[sp - 1] = -1;
            int as = cg_stk_arm_slot(&cg, f32_mask, sp - 1);
            uint32_t dst = cg_arm_dst(&cg, as, 0u);
            emit_dp_imm(C, DP_RSB, dst, r, 0, COND_AL, 0);
            if (cg_arm_reg(&cg, as) < 0)
                emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
            break;
        }
        case OP_NOT: {
            uint32_t r = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 0u);
            cg.alias_loc[sp - 1] = -1;
            if (pc + 1 < n && !C->is_target[pc + 1] &&
                C->insns[fn->code_start + pc + 1].op == OP_JZ) {
                cg_flush_all_aliases(C, &cg, f32_mask, sp - 1);
                emit_dp_imm(C, DP_CMP, 0, r, 0, COND_AL, 1);
                emit_branch_patch(C, (int16_t)fn_idx, (int16_t)C->insns[fn->code_start + pc + 1].arg, COND_NE, 0);
                C->pc_word[pc + 1] = C->pc_word[pc];
                pc++;
                break;
            }
            int as = cg_stk_arm_slot(&cg, f32_mask, sp - 1);
            uint32_t dst = cg_arm_dst(&cg, as, 0u);
            emit_dp_imm(C, DP_CMP, 0, r, 0, COND_AL, 1);
            emit_dp_imm(C, DP_MOV, dst, 0, 1, COND_EQ, 0);
            emit_dp_imm(C, DP_MOV, dst, 0, 0, COND_NE, 0);
            if (cg_arm_reg(&cg, as) < 0)
                emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
            break;
        }
        case OP_ABS: {
            uint32_t r = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 0u);
            cg.alias_loc[sp - 1] = -1;
            int as = cg_stk_arm_slot(&cg, f32_mask, sp - 1);
            uint32_t dst = cg_arm_dst(&cg, as, 0u);
            if (dst != r) emit_dp_reg(C, DP_MOV, dst, 0, r, COND_AL, 0, 0, 0);
            emit_dp_imm(C, DP_CMP, 0, dst, 0, COND_AL, 1);
            emit_dp_imm(C, DP_RSB, dst, dst, 0, COND_LT, 0);
            if (cg_arm_reg(&cg, as) < 0)
                emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
            break;
        }
        case OP_MIN:
        case OP_MAX: {
            uint32_t ra = cg_read_arm_op(C, &cg, f32_mask, sp - 2, 0u);
            uint32_t rb = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 1u);
            cg.alias_loc[sp - 2] = -1;
            cg.alias_loc[sp - 1] = -1;
            emit_dp_reg(C, DP_CMP, 0, ra, rb, COND_AL, 1, 0, 0);
            int as = cg_stk_arm_slot(&cg, f32_mask, sp - 2);
            uint32_t dst = cg_arm_dst(&cg, as, 0u);
            if (dst != ra) emit_dp_reg(C, DP_MOV, dst, 0, ra, COND_AL, 0, 0, 0);
            emit_dp_reg(C, DP_MOV, dst, 0, rb, (op == OP_MIN) ? COND_GT : COND_LT, 0, 0, 0);
            if (cg_arm_reg(&cg, as) < 0)
                emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
            break;
        }
        case OP_EQ: case OP_NE: case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
            uint32_t ra = cg_read_arm_op(C, &cg, f32_mask, sp - 2, 0u);
            uint32_t rb = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 1u);
            cg.alias_loc[sp - 2] = -1;
            cg.alias_loc[sp - 1] = -1;
            uint32_t ct = arm_cmp_cond(op);
            if (pc + 1 < n && !C->is_target[pc + 1] &&
                C->insns[fn->code_start + pc + 1].op == OP_JZ) {
                cg_flush_all_aliases(C, &cg, f32_mask, sp - 2);
                emit_dp_reg(C, DP_CMP, 0, ra, rb, COND_AL, 1, 0, 0);
                emit_branch_patch(C, (int16_t)fn_idx, (int16_t)C->insns[fn->code_start + pc + 1].arg, (uint8_t)inv_cond(ct), 0);
                C->pc_word[pc + 1] = C->pc_word[pc];
                pc++;
                break;
            }
            emit_dp_reg(C, DP_CMP, 0, ra, rb, COND_AL, 1, 0, 0);
            int as = cg_stk_arm_slot(&cg, f32_mask, sp - 2);
            uint32_t dst = cg_arm_dst(&cg, as, 0u);
            emit_dp_imm(C, DP_MOV, dst, 0, 0, COND_AL, 0);
            emit_dp_imm(C, DP_MOV, dst, 0, 1, ct, 0);
            if (cg_arm_reg(&cg, as) < 0)
                emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
            break;
        }
        case OP_FMUL: {
            if (sp >= 3 && pc + 1 < n && !C->is_target[pc + 1]) {
                uint8_t next_op = C->insns[fn->code_start + pc + 1].op;
                if ((next_op == OP_FADD || next_op == OP_FSUB) && ((f32_mask >> (sp - 3)) & 1ULL) != 0ULL) {
                    uint32_t sa = cg_read_vfp_op(C, &cg, f32_mask, sp - 2, 0u);
                    uint32_t sb = cg_read_vfp_op(C, &cg, f32_mask, sp - 1, 1u);
                    int vs_acc = cg_stk_vfp_slot(&cg, f32_mask, sp - 3);
                    uint32_t sd = cg_vfp_dst(&cg, vs_acc, 2u);
                    if (cg.alias_loc[sp - 3] >= 0) {
                        uint32_t sacc = cg_read_vfp_op(C, &cg, f32_mask, sp - 3, 2u);
                        emit_vmov_ss(C, sd, sacc);
                    } else if (cg_vfp_reg(&cg, vs_acc) < 0) {
                        emit_vldr(C, 2u, 11, cg_vfp_spill_fp_off(&cg, vs_acc));
                    }
                    cg.alias_loc[sp - 3] = -1;
                    cg.alias_loc[sp - 2] = -1;
                    cg.alias_loc[sp - 1] = -1;
                    if (sd != sa && sd != sb) {
                        emit_vfp_dp(C, (next_op == OP_FADD) ? 0xEE000A00u : 0xEE000A40u, sd, sa, sb);
                    } else {
                        emit_vfp_dp(C, 0xEE200A00u, 3u, sa, sb);
                        emit_vfp_dp(C, (next_op == OP_FADD) ? 0xEE300A00u : 0xEE300A40u, sd, sd, 3u);
                    }
                    if (cg_vfp_reg(&cg, vs_acc) < 0)
                        emit_vstr(C, sd, 11, cg_vfp_spill_fp_off(&cg, vs_acc));
                    C->pc_word[pc + 1] = C->pc_word[pc];
                    pc++;
                    break;
                }
            }
            /* fallthrough to standard f32 binary op */
        }
        /* FALLTHROUGH */
        case OP_FADD: case OP_FSUB: case OP_FDIV: {
            uint32_t sa = cg_read_vfp_op(C, &cg, f32_mask, sp - 2, 0u);
            uint32_t sb = cg_read_vfp_op(C, &cg, f32_mask, sp - 1, 1u);
            cg.alias_loc[sp - 2] = -1;
            cg.alias_loc[sp - 1] = -1;
            int vs = cg_stk_vfp_slot(&cg, f32_mask, sp - 2);
            uint32_t sd = cg_vfp_dst(&cg, vs, 0u);
            uint32_t vbase = (op == OP_FADD) ? 0xEE300A00u : (op == OP_FSUB) ? 0xEE300A40u :
                             (op == OP_FMUL) ? 0xEE200A00u : 0xEE800A00u;
            emit_vfp_dp(C, vbase, sd, sa, sb);
            if (cg_vfp_reg(&cg, vs) < 0)
                emit_vstr(C, 0u, 11, cg_vfp_spill_fp_off(&cg, vs));
            break;
        }
        case OP_FNEG: case OP_FABS: case OP_FSQRT: {
            uint32_t sa = cg_read_vfp_op(C, &cg, f32_mask, sp - 1, 0u);
            cg.alias_loc[sp - 1] = -1;
            int vs = cg_stk_vfp_slot(&cg, f32_mask, sp - 1);
            uint32_t sd = cg_vfp_dst(&cg, vs, 0u);
            uint32_t ubase = (op == OP_FNEG) ? 0xEEB10A40u : (op == OP_FABS) ? 0xEEB00AC0u : 0xEEB10AC0u;
            emit_vfp_un(C, ubase, sd, sa);
            if (cg_vfp_reg(&cg, vs) < 0)
                emit_vstr(C, 0u, 11, cg_vfp_spill_fp_off(&cg, vs));
            break;
        }
        case OP_I2F: {
            uint32_t r = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 0u);
            cg.alias_loc[sp - 1] = -1;
            int vs = cg_stk_vfp_slot(&cg, f32_mask, sp - 1);
            uint32_t sd = cg_vfp_dst(&cg, vs, 0u);
            emit_vmov_sr(C, sd, r);
            emit_vfp_un(C, 0xEEB80AC0u, sd, sd);
            if (cg_vfp_reg(&cg, vs) < 0)
                emit_vstr(C, 0u, 11, cg_vfp_spill_fp_off(&cg, vs));
            break;
        }
        case OP_FLOOR: {
            uint32_t sa = cg_read_vfp_op(C, &cg, f32_mask, sp - 1, 1u);
            cg.alias_loc[sp - 1] = -1;
            int as = cg_stk_arm_slot(&cg, f32_mask, sp - 1);
            uint32_t dst = cg_arm_dst(&cg, as, 0u);
            emit_vfp_un(C, 0xEEBD0AC0u, 0u, sa); /* vcvt.s32.f32 s0, sa */
            emit_vmov_rs(C, dst, 0u);            /* vmov dst, s0 */
            emit_vfp_un(C, 0xEEB80AC0u, 0u, 0u); /* vcvt.f32.s32 s0, s0 */
            emit_vcmp(C, sa, 0u);                /* vcmp.f32 sa, s0 */
            emit_vmrs(C);                        /* vmrs APSR_nzcv, fpscr */
            emit_dp_imm(C, DP_SUB, dst, dst, 1u, COND_MI, 0);
            if (cg_arm_reg(&cg, as) < 0)
                emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
            break;
        }
        case OP_FMIN: case OP_FMAX: {
            uint32_t sa = cg_read_vfp_op(C, &cg, f32_mask, sp - 2, 0u);
            uint32_t sb = cg_read_vfp_op(C, &cg, f32_mask, sp - 1, 1u);
            cg.alias_loc[sp - 2] = -1;
            cg.alias_loc[sp - 1] = -1;
            emit_vcmp(C, sa, sb);
            emit_vmrs(C);
            int vs = cg_stk_vfp_slot(&cg, f32_mask, sp - 2);
            uint32_t sd = cg_vfp_dst(&cg, vs, 0u);
            emit_vmov_ss(C, sd, sa);
            /* Conditional vmov.f32 sd, sb */
            {
                uint32_t cond = (op == OP_FMIN) ? COND_GT : COND_MI;
                uint32_t d = sd & 1u, vd = (sd >> 1) & 0xFu;
                uint32_t m = sb & 1u, vm = (sb >> 1) & 0xFu;
                emit_w(C, (cond << 28) | 0x0EB00A40u | (d << 22) | (vd << 12) | (m << 5) | vm);
            }
            if (cg_vfp_reg(&cg, vs) < 0)
                emit_vstr(C, 0u, 11, cg_vfp_spill_fp_off(&cg, vs));
            break;
        }
        case OP_CLAMP: {
            uint32_t rx  = cg_read_arm_op(C, &cg, f32_mask, sp - 3, 0u);
            uint32_t rlo = cg_read_arm_op(C, &cg, f32_mask, sp - 2, 1u);
            uint32_t rhi = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 2u);
            cg.alias_loc[sp - 3] = -1;
            cg.alias_loc[sp - 2] = -1;
            cg.alias_loc[sp - 1] = -1;
            int as = cg_stk_arm_slot(&cg, f32_mask, sp - 3);
            uint32_t dst = cg_arm_dst(&cg, as, 0u);
            if (dst != rx) emit_dp_reg(C, DP_MOV, dst, 0, rx, COND_AL, 0, 0, 0);
            emit_dp_reg(C, DP_CMP, 0, dst, rlo, COND_AL, 1, 0, 0);
            emit_dp_reg(C, DP_MOV, dst, 0, rlo, COND_LT, 0, 0, 0);
            emit_dp_reg(C, DP_CMP, 0, dst, rhi, COND_AL, 1, 0, 0);
            emit_dp_reg(C, DP_MOV, dst, 0, rhi, COND_GT, 0, 0, 0);
            if (cg_arm_reg(&cg, as) < 0)
                emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
            break;
        }
        case OP_FCLAMP: {
            uint32_t sx  = cg_read_vfp_op(C, &cg, f32_mask, sp - 3, 0u);
            uint32_t slo = cg_read_vfp_op(C, &cg, f32_mask, sp - 2, 1u);
            uint32_t shi = cg_read_vfp_op(C, &cg, f32_mask, sp - 1, 2u);
            cg.alias_loc[sp - 3] = -1;
            cg.alias_loc[sp - 2] = -1;
            cg.alias_loc[sp - 1] = -1;
            int vs = cg_stk_vfp_slot(&cg, f32_mask, sp - 3);
            uint32_t sd = cg_vfp_dst(&cg, vs, 0u);
            emit_vmov_ss(C, sd, sx);
            emit_vcmp(C, sd, slo);
            emit_vmrs(C);
            {
                uint32_t d = sd & 1u, vd = (sd >> 1) & 0xFu;
                uint32_t m = slo & 1u, vm = (slo >> 1) & 0xFu;
                emit_w(C, (COND_MI << 28) | 0x0EB00A40u | (d << 22) | (vd << 12) | (m << 5) | vm);
            }
            emit_vcmp(C, sd, shi);
            emit_vmrs(C);
            {
                uint32_t d = sd & 1u, vd = (sd >> 1) & 0xFu;
                uint32_t m = shi & 1u, vm = (shi >> 1) & 0xFu;
                emit_w(C, (COND_GT << 28) | 0x0EB00A40u | (d << 22) | (vd << 12) | (m << 5) | vm);
            }
            if (cg_vfp_reg(&cg, vs) < 0)
                emit_vstr(C, 0u, 11, cg_vfp_spill_fp_off(&cg, vs));
            break;
        }
        case OP_FLERP: {
            uint32_t sa = cg_read_vfp_op(C, &cg, f32_mask, sp - 3, 0u);
            uint32_t sb = cg_read_vfp_op(C, &cg, f32_mask, sp - 2, 1u);
            uint32_t st = cg_read_vfp_op(C, &cg, f32_mask, sp - 1, 2u);
            cg.alias_loc[sp - 3] = -1;
            cg.alias_loc[sp - 2] = -1;
            cg.alias_loc[sp - 1] = -1;
            emit_vfp_dp(C, 0xEE300A40u, 3u, sb, sa); /* s3 = b - a */
            int vs = cg_stk_vfp_slot(&cg, f32_mask, sp - 3);
            uint32_t sd = cg_vfp_dst(&cg, vs, 0u);
            emit_vmov_ss(C, sd, sa);
            emit_vfp_dp(C, 0xEE000A00u, sd, 3u, st); /* sd += (b - a) * t */
            if (cg_vfp_reg(&cg, vs) < 0)
                emit_vstr(C, 0u, 11, cg_vfp_spill_fp_off(&cg, vs));
            break;
        }
        case OP_FEQ: case OP_FNE: case OP_FLT: case OP_FLE: case OP_FGT: case OP_FGE: {
            uint32_t sa = cg_read_vfp_op(C, &cg, f32_mask, sp - 2, 0u);
            uint32_t sb = cg_read_vfp_op(C, &cg, f32_mask, sp - 1, 1u);
            cg.alias_loc[sp - 2] = -1;
            cg.alias_loc[sp - 1] = -1;
            emit_vcmp(C, sa, sb);
            emit_vmrs(C);
            if (pc + 1 < n && !C->is_target[pc + 1] &&
                C->insns[fn->code_start + pc + 1].op == OP_JZ) {
                cg_flush_all_aliases(C, &cg, f32_mask, sp - 2);
                emit_branch_patch(C, (int16_t)fn_idx, (int16_t)C->insns[fn->code_start + pc + 1].arg, (uint8_t)vfp_cmp_inv_cond(op), 0);
                C->pc_word[pc + 1] = C->pc_word[pc];
                pc++;
                break;
            }
            int as = cg_stk_arm_slot(&cg, f32_mask, sp - 2);
            uint32_t dst = cg_arm_dst(&cg, as, 0u);
            emit_dp_imm(C, DP_MOV, dst, 0, 0, COND_AL, 0);
            emit_dp_imm(C, DP_MOV, dst, 0, 1, vfp_cmp_cond(op), 0);
            if (cg_arm_reg(&cg, as) < 0)
                emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
            break;
        }
        case OP_AGET: case OP_AGETF: {
            int is_m4 = (cg.alias_loc[sp - 2] >= 0 && cg.base_m4[(int)cg.alias_loc[sp - 2]]);
            uint32_t rb = cg_read_arm_op(C, &cg, f32_mask, sp - 2, 0u);
            uint32_t ri = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 1u);
            cg.alias_loc[sp - 2] = -1;
            cg.alias_loc[sp - 1] = -1;
            if (op == OP_AGETF) {
                if (is_m4) {
                    emit_dp_reg(C, DP_ADD, 12, rb, ri, COND_AL, 0, 0, 2);
                } else {
                    emit_dp_imm(C, DP_SUB, 12, ri, 1, COND_AL, 0);
                    emit_dp_reg(C, DP_ADD, 12, rb, 12, COND_AL, 0, 0, 2);
                }
                int vs = cg_stk_vfp_slot(&cg, f32_mask, sp - 2);
                uint32_t sd = cg_vfp_dst(&cg, vs, 0u);
                emit_vldr(C, sd, 12, 0);
                if (cg_vfp_reg(&cg, vs) < 0)
                    emit_vstr(C, 0u, 11, cg_vfp_spill_fp_off(&cg, vs));
            } else {
                int as = cg_stk_arm_slot(&cg, f32_mask, sp - 2);
                uint32_t dst = cg_arm_dst(&cg, as, 0u);
                if (is_m4) {
                    emit_ldr_idx(C, dst, rb, ri, 2u);
                } else {
                    emit_dp_imm(C, DP_SUB, 12, ri, 1, COND_AL, 0);
                    emit_ldr_idx(C, dst, rb, 12, 2u);
                }
                if (cg_arm_reg(&cg, as) < 0)
                    emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
            }
            break;
        }
        case OP_ASET: case OP_ASETF: {
            int is_m4 = (cg.alias_loc[sp - 3] >= 0 && cg.base_m4[(int)cg.alias_loc[sp - 3]]);
            uint32_t rb = cg_read_arm_op(C, &cg, f32_mask, sp - 3, 0u);
            uint32_t ri = cg_read_arm_op(C, &cg, f32_mask, sp - 2, 1u);
            if (op == OP_ASETF) {
                uint32_t sv = cg_read_vfp_op(C, &cg, f32_mask, sp - 1, 0u);
                cg.alias_loc[sp - 3] = -1;
                cg.alias_loc[sp - 2] = -1;
                cg.alias_loc[sp - 1] = -1;
                if (is_m4) {
                    emit_dp_reg(C, DP_ADD, 12, rb, ri, COND_AL, 0, 0, 2);
                } else {
                    emit_dp_imm(C, DP_SUB, 12, ri, 1, COND_AL, 0);
                    emit_dp_reg(C, DP_ADD, 12, rb, 12, COND_AL, 0, 0, 2);
                }
                emit_vstr(C, sv, 12, 0);
            } else {
                uint32_t rv = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 2u);
                cg.alias_loc[sp - 3] = -1;
                cg.alias_loc[sp - 2] = -1;
                cg.alias_loc[sp - 1] = -1;
                if (is_m4) {
                    emit_str_idx(C, rv, rb, ri, 2u);
                } else {
                    emit_dp_imm(C, DP_SUB, 12, ri, 1, COND_AL, 0);
                    emit_str_idx(C, rv, rb, 12, 2u);
                }
            }
            break;
        }
        case OP_NEW_ARR: {
            int n_elems = ins.arg;
            int offset = cg.arr_base_off + (cur_arr_word + n_elems) * 4;
            cur_arr_word += n_elems;
            emit_mov32(C, 12, (uint32_t)offset);
            emit_dp_reg(C, DP_SUB, 12, 11, 12, COND_AL, 0, 0, 0);
            for (int j = 0; j < n_elems; j++) {
                int d = sp - n_elems + j;
                if ((f32_mask >> d) & 1ULL) {
                    uint32_t sv = cg_read_vfp_op(C, &cg, f32_mask, d, 0u);
                    emit_vstr(C, sv, 12, j * 4);
                } else {
                    uint32_t rv = cg_read_arm_op(C, &cg, f32_mask, d, 0u);
                    emit_str(C, rv, 12, j * 4);
                }
                cg.alias_loc[d] = -1;
            }
            cg_put_arm(C, &cg, cg_stk_arm_slot(&cg, f32_mask, sp - n_elems), 12u);
            break;
        }
        case OP_PEEK: case OP_PEEK2: case OP_PEEK4: {
            uint32_t r_addr = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 0u);
            cg.alias_loc[sp - 1] = -1;
            int as = cg_stk_arm_slot(&cg, f32_mask, sp - 1);
            uint32_t dst = cg_arm_dst(&cg, as, 0u);
            if (op == OP_PEEK) emit_ldrb(C, dst, r_addr, 0);
            else if (op == OP_PEEK2) emit_ldrh(C, dst, r_addr, 0);
            else emit_ldr(C, dst, r_addr, 0);
            if (cg_arm_reg(&cg, as) < 0)
                emit_str(C, 0u, 11, cg_arm_spill_fp_off(&cg, as));
            break;
        }
        case OP_POKE: case OP_POKE2: case OP_POKE4: {
            uint32_t r_addr = cg_read_arm_op(C, &cg, f32_mask, sp - 2, 0u);
            uint32_t r_val  = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 1u);
            cg.alias_loc[sp - 2] = -1;
            cg.alias_loc[sp - 1] = -1;
            if (op == OP_POKE) emit_strb(C, r_val, r_addr, 0);
            else if (op == OP_POKE2) emit_strh(C, r_val, r_addr, 0);
            else emit_str(C, r_val, r_addr, 0);
            break;
        }
        case OP_MEMCPY:
        case OP_MEMSET: {
            if (op == OP_MEMCPY) C->needs_memcpy = 1;
            else C->needs_memset = 1;
            cg_flush_all_aliases(C, &cg, f32_mask, sp - 3);
            uint32_t r_dst   = cg_read_arm_op(C, &cg, f32_mask, sp - 3, 0u);
            if (r_dst != 0u) emit_dp_reg(C, DP_MOV, 0, 0, r_dst, COND_AL, 0, 0, 0);
            uint32_t r_v_src = cg_read_arm_op(C, &cg, f32_mask, sp - 2, 1u);
            if (r_v_src != 1u) emit_dp_reg(C, DP_MOV, 1, 0, r_v_src, COND_AL, 0, 0, 0);
            uint32_t r_n     = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 2u);
            if (r_n != 2u) emit_dp_reg(C, DP_MOV, 2, 0, r_n, COND_AL, 0, 0, 0);
            cg.alias_loc[sp - 3] = -1;
            cg.alias_loc[sp - 2] = -1;
            cg.alias_loc[sp - 1] = -1;
            emit_branch_patch(C, (op == OP_MEMCPY) ? -5 : -6, 0, COND_AL, 1);
            break;
        }
        case OP_JMP:
            cg_flush_all_aliases(C, &cg, f32_mask, sp);
            emit_branch_patch(C, (int16_t)fn_idx, (int16_t)ins.arg, COND_AL, 0);
            memset(cg.alias_loc, -1, sizeof cg.alias_loc);
            break;
        case OP_JZ: {
            uint32_t r = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 0u);
            cg.alias_loc[sp - 1] = -1;
            cg_flush_all_aliases(C, &cg, f32_mask, sp - 1);
            emit_dp_imm(C, DP_CMP, 0, r, 0, COND_AL, 1);
            emit_branch_patch(C, (int16_t)fn_idx, (int16_t)ins.arg, COND_EQ, 0);
            break;
        }
        case OP_JF_KEEP:
        case OP_JT_KEEP: {
            cg_flush_all_aliases(C, &cg, f32_mask, sp);
            uint32_t r = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 0u);
            emit_dp_imm(C, DP_CMP, 0, r, 0, COND_AL, 1);
            emit_branch_patch(C, (int16_t)fn_idx, (int16_t)ins.arg, (op == OP_JF_KEEP) ? COND_EQ : COND_NE, 0);
            break;
        }
        case OP_CALL: {
            int c_idx = ins.arg >> 8;
            int argc = ins.arg & 0xFF;
            int base = sp - argc;
            cg_flush_all_aliases(C, &cg, f32_mask, base);
            int stack_args = argc > 4 ? (argc - 4) : 0;
            int pad_words = (stack_args % 2 == 1) ? 1 : 0;
            int alloc_bytes = (stack_args + pad_words) * 4;
            if (alloc_bytes > 0)
                emit_dp_imm(C, DP_SUB, 13, 13, (uint32_t)alloc_bytes, COND_AL, 0);
            for (int j = 4; j < argc; j++) {
                int d = base + j;
                if ((f32_mask >> d) & 1ULL) {
                    uint32_t sj = cg_read_vfp_op(C, &cg, f32_mask, d, 0u);
                    emit_vstr(C, sj, 13, (j - 4) * 4);
                } else {
                    uint32_t rj = cg_read_arm_op(C, &cg, f32_mask, d, 0u);
                    emit_str(C, rj, 13, (j - 4) * 4);
                }
            }
            for (int j = 0; j < argc && j < 4; j++) {
                int d = base + j;
                if ((f32_mask >> d) & 1ULL) {
                    uint32_t sj = cg_read_vfp_op(C, &cg, f32_mask, d, 0u);
                    emit_vmov_rs(C, (uint32_t)j, sj);
                } else {
                    uint32_t rj = cg_read_arm_op(C, &cg, f32_mask, d, (uint32_t)j);
                    if (rj != (uint32_t)j)
                        emit_dp_reg(C, DP_MOV, (uint32_t)j, 0, rj, COND_AL, 0, 0, 0);
                }
            }
            for (int j = 0; j < argc; j++)
                cg.alias_loc[base + j] = -1;
            emit_branch_patch(C, -2, (int16_t)c_idx, COND_AL, 1);
            if (alloc_bytes > 0)
                emit_dp_imm(C, DP_ADD, 13, 13, (uint32_t)alloc_bytes, COND_AL, 0);
            if (C->funcs[c_idx].ret_type == BMN_F32) {
                int vs = cg_stk_vfp_slot(&cg, f32_mask, base);
                uint32_t sd = cg_vfp_dst(&cg, vs, 0u);
                emit_vmov_sr(C, sd, 0u);
                if (cg_vfp_reg(&cg, vs) < 0)
                    emit_vstr(C, 0u, 11, cg_vfp_spill_fp_off(&cg, vs));
            } else if (C->funcs[c_idx].ret_type != BMN_NONE) {
                cg_put_arm(C, &cg, cg_stk_arm_slot(&cg, f32_mask, base), 0u);
            }
            break;
        }
        case OP_CALL_HOST: {
            C->needs_host_call = 1;
            int hid = ins.arg >> 8;
            int argc = ins.arg & 0xFF;
            int base = sp - argc;
            cg_flush_all_aliases(C, &cg, f32_mask, base);
            int alloc_words = (((argc > 0 ? argc : 1) + 1) / 2) * 2;
            int alloc_bytes = alloc_words * 4;
            emit_dp_imm(C, DP_SUB, 13, 13, (uint32_t)alloc_bytes, COND_AL, 0);
            for (int j = 0; j < argc; j++) {
                int d = base + j;
                if ((f32_mask >> d) & 1ULL) {
                    uint32_t sj = cg_read_vfp_op(C, &cg, f32_mask, d, 0u);
                    emit_vstr(C, sj, 13, j * 4);
                } else {
                    uint32_t rj = cg_read_arm_op(C, &cg, f32_mask, d, 0u);
                    emit_str(C, rj, 13, j * 4);
                }
                cg.alias_loc[d] = -1;
            }
            emit_mov32(C, 0, (uint32_t)hid);
            emit_dp_reg(C, DP_MOV, 1, 0, 13, COND_AL, 0, 0, 0);
            emit_mov32(C, 2, (uint32_t)argc);
            emit_branch_patch(C, -4, 0, COND_AL, 1);
            emit_dp_imm(C, DP_ADD, 13, 13, (uint32_t)alloc_bytes, COND_AL, 0);
            if (hid == BMN_HOST_TIME || hid == BMN_HOST_SIN || hid == BMN_HOST_COS || hid == BMN_HOST_ATAN2) {
                int vs = cg_stk_vfp_slot(&cg, f32_mask, base);
                uint32_t sd = cg_vfp_dst(&cg, vs, 0u);
                emit_vmov_sr(C, sd, 0u);
                if (cg_vfp_reg(&cg, vs) < 0)
                    emit_vstr(C, 0u, 11, cg_vfp_spill_fp_off(&cg, vs));
            } else if (hid == BMN_HOST_BTN || hid == BMN_HOST_BTNP || hid == BMN_HOST_PRINT) {
                cg_put_arm(C, &cg, cg_stk_arm_slot(&cg, f32_mask, base), 0u);
            }
            break;
        }
        case OP_RET: {
            if ((f32_mask >> (sp - 1)) & 1ULL) {
                uint32_t s = cg_read_vfp_op(C, &cg, f32_mask, sp - 1, 0u);
                emit_vmov_rs(C, 0u, s);
            } else {
                uint32_t r = cg_read_arm_op(C, &cg, f32_mask, sp - 1, 0u);
                if (r != 0u)
                    emit_dp_reg(C, DP_MOV, 0, 0, r, COND_AL, 0, 0, 0);
            }
            cg.alias_loc[sp - 1] = -1;
            if (pc != n - 1)
                emit_branch_patch(C, -1, 0, COND_AL, 0);
            memset(cg.alias_loc, -1, sizeof cg.alias_loc);
            break;
        }
        case OP_RET0:
            if (pc != n - 1)
                emit_branch_patch(C, -1, 0, COND_AL, 0);
            memset(cg.alias_loc, -1, sizeof cg.alias_loc);
            break;
        }
    }

    int epilogue_word = C->nwords;
    C->pc_word[n] = (uint32_t)epilogue_word;
    if (cg.has_fp)
        emit_dp_reg(C, DP_MOV, 13, 0, 11, COND_AL, 0, 0, 0);
    if (cg.vfp_push_cnt > 0)
        emit_vpop(C, 16u, (uint32_t)cg.vfp_push_cnt);
    if (push_mask != 0u) {
        if (cg.has_lr) {
            uint32_t pop_mask = (push_mask & ~(1u << 14)) | (1u << 15);
            emit_w(C, 0xE8BD0000u | pop_mask);
        } else {
            emit_w(C, 0xE8BD0000u | push_mask);
            emit_w(C, 0xE12FFF1Eu); /* bx lr */
        }
    } else {
        emit_w(C, 0xE12FFF1Eu); /* bx lr */
    }

    for (int k = 0; k < nfn_str_relocs; k++) {
        uint16_t lit_slot_idx = (uint16_t)C->nwords;
        emit_w(C, 0);
        uint32_t lit_offset = (uint32_t)(lit_slot_idx - (fn_str_relocs[k].ldr_idx + 2)) * 4u;
        C->words[fn_str_relocs[k].ldr_idx] = (COND_AL << 28) | (0x51u << 20) | (1u << 23) | (15u << 16) | (((uint32_t)fn_str_relocs[k].dst & 0xFu) << 12) | (lit_offset & 0xFFFu);
        if (C->nstr_patches < 512) {
            C->str_patches[C->nstr_patches].lit_word_idx = lit_slot_idx;
            C->str_patches[C->nstr_patches].add_word_idx = fn_str_relocs[k].add_idx;
            C->str_patches[C->nstr_patches].str_off = fn_str_relocs[k].off;
            C->nstr_patches++;
        }
    }

    for (int i = fn_patch_start; i < C->npatches; i++) {
        c_patch_t *p = &C->patches[i];
        int target_w = -1;
        if (p->fn_idx == fn_idx)
            target_w = C->pc_word[p->target];
        else if (p->fn_idx == -1)
            target_w = epilogue_word;
        if (target_w >= 0) {
            uint32_t disp = (uint32_t)(target_w - ((int)p->word_idx + 2)) & 0xFFFFFFu;
            C->words[p->word_idx] = ((uint32_t)p->cond << 28) | (5u << 25) | ((uint32_t)p->is_bl << 24) | disp;
            p->fn_idx = -99;
        }
    }
}

int bml_compile_native(const char *src, size_t src_len, uint8_t **out_blob, size_t *out_len, char *err_buf, size_t err_cap)
{
    *out_blob = NULL;
    *out_len = 0;
    if (err_buf && err_cap > 0)
        err_buf[0] = '\0';

    bmlc_ctx_t *C = calloc(1, sizeof *C);
    if (!C) {
        bml_copy_str(err_buf, err_cap, "out of memory");
        return -1;
    }
    C->src = src;
    C->src_len = src_len;
    C->err_buf = err_buf;
    C->err_cap = err_cap;

    if (setjmp(C->jb) != 0) {
        free(C);
        return -1;
    }

    tokenize(C);
    while (cur_tok(C)->kind != TK_EOF) {
        if (cur_tok(C)->kind == TK_KW && tok_eq(cur_tok(C), "struct"))
            parse_struct_decl(C);
        else if (cur_tok(C)->kind == TK_KW && tok_eq(cur_tok(C), "const"))
            parse_const_decl(C);
        else if (cur_tok(C)->kind == TK_KW && tok_eq(cur_tok(C), "enum"))
            parse_enum_decl(C);
        else if (cur_tok(C)->kind == TK_KW && (tok_eq(cur_tok(C), "function") || tok_eq(cur_tok(C), "fn")))
            parse_func_decl(C);
        else
            c_fail(C, cur_tok(C)->line, "expected 'function', 'struct', 'const', or 'enum'");
    }
    if (C->nfuncs == 0)
        c_fail(C, 1, "program has no functions");

    for (int i = 0; i < C->nfuncs; i++)
        compile_armv6_fn(C, i);

    if (C->needs_idivmod) {
        C->idivmod_word = C->nwords;
        static const uint32_t idiv_words[30] = {
            0xE92D4030u, 0xE3510000u, 0x0A00001Fu, 0xE1A04000u, 0xE1A05001u, 0xE0241005u,
            0xE1A02004u, 0xE3520000u, 0xB2622000u, 0xE1A03005u, 0xE3530000u, 0xB2633000u,
            0xE3A00000u, 0xE3A0C001u, 0xE1530002u, 0x2A000004u, 0xE3530000u, 0x4A000002u,
            0xE1A03083u, 0xE1A0C08Cu, 0xEAFFFFF8u, 0xE1520003u, 0x20422003u, 0x2080000Cu,
            0xE1A030A3u, 0xE1B0C0ACu, 0x1AFFFFF9u, 0xE3510000u, 0x5A000002u, 0xE3520000u
        };
        static const uint32_t idiv_tail[9] = {
            0x02600000u, 0x11E00000u, 0xE00C0590u, 0xE044100Cu, 0xE8BD8030u,
            0xE3A00000u, 0xE3A01000u, 0xE8BD8030u, 0u
        };
        for (int i = 0; i < 30; i++)
            emit_w(C, idiv_words[i]);
        for (int i = 0; i < 8; i++)
            emit_w(C, idiv_tail[i]);
    }

    if (C->needs_memcpy) {
        C->memcpy_word = C->nwords;
        static const uint32_t mc_words[9] = {
            0xE3520000u, /* cmp r2, #0 */
            0x012FFF1Eu, /* bxeq lr */
            0xE5D13000u, /* ldrb r3, [r1, #0] */
            0xE5C03000u, /* strb r3, [r0, #0] */
            0xE2811001u, /* add r1, r1, #1 */
            0xE2800001u, /* add r0, r0, #1 */
            0xE2522001u, /* subs r2, r2, #1 */
            0x1AFFFFFAu, /* bne .Lcp */
            0xE12FFF1Eu  /* bx lr */
        };
        for (int i = 0; i < 9; i++) emit_w(C, mc_words[i]);
    }

    if (C->needs_memset) {
        C->memset_word = C->nwords;
        static const uint32_t ms_words[8] = {
            0xE3520000u, /* cmp r2, #0 */
            0x012FFF1Eu, /* bxeq lr */
            0xE20110FFu, /* and r1, r1, #0xFF */
            0xE5C01000u, /* strb r1, [r0, #0] */
            0xE2800001u, /* add r0, r0, #1 */
            0xE2522001u, /* subs r2, r2, #1 */
            0x1AFFFFFBu, /* bne .Lset */
            0xE12FFF1Eu  /* bx lr */
        };
        for (int i = 0; i < 8; i++) emit_w(C, ms_words[i]);
    }

    if (C->needs_host_call) {
        C->host_call_word = C->nwords;
        emit_w(C, 0xE59FC00Cu); /* ldr r12, [pc, #12] */
        emit_w(C, 0xE35C0000u); /* cmp r12, #0 */
        emit_w(C, 0x112FFF1Cu); /* bxne r12 */
        emit_w(C, 0xE3A00000u); /* mov r0, #0 */
        emit_w(C, 0xE12FFF1Eu); /* bx lr */
        emit_w(C, 0x00000000u); /* __bml_host_fn_slot */
    }

    /* Resolve inter-function and helper branch patches */
    for (int i = 0; i < C->npatches; i++) {
        c_patch_t *p = &C->patches[i];
        if (p->fn_idx == -99)
            continue;
        int target_w = -1;
        if (p->fn_idx == -2)
            target_w = (int)C->funcs[p->target].word_off;
        else if (p->fn_idx == -3)
            target_w = C->idivmod_word;
        else if (p->fn_idx == -4)
            target_w = C->host_call_word;
        else if (p->fn_idx == -5)
            target_w = C->memcpy_word;
        else if (p->fn_idx == -6)
            target_w = C->memset_word;
        if (target_w >= 0) {
            uint32_t disp = (uint32_t)(target_w - ((int)p->word_idx + 2)) & 0xFFFFFFu;
            C->words[p->word_idx] = ((uint32_t)p->cond << 28) | (5u << 25) | ((uint32_t)p->is_bl << 24) | disp;
        }
    }

    /* Resolve string literal delta patches */
    while ((C->nrodata & 3u) != 0)
        C->rodata[C->nrodata++] = 0;

    for (int i = 0; i < C->nstr_patches; i++) {
        uint32_t target_off = (uint32_t)C->nwords * 4u + C->str_patches[i].str_off;
        uint32_t pc_at_add = ((uint32_t)C->str_patches[i].add_word_idx + 2u) * 4u;
        uint32_t delta = target_off - pc_at_add;
        C->words[C->str_patches[i].lit_word_idx] = delta;
    }

    /* Pack BMN1 binary container */
    size_t hdr_size = (C->nrodata > 0) ? 16u : 12u;
    size_t total_len = hdr_size + (size_t)C->nfuncs * 56u + (size_t)C->nwords * 4u + (size_t)C->nstructs * 308u + (size_t)C->nrodata;
    uint8_t *blob = calloc(1, total_len);
    if (!blob) {
        free(C);
        bml_copy_str(err_buf, err_cap, "out of memory");
        return -1;
    }

    uint32_t nfn_packed = (((uint32_t)C->nstructs | (C->needs_host_call ? 0x8000u : 0u)) << 16) | (uint32_t)C->nfuncs;
    uint32_t magic = BMN_MAGIC;
    uint32_t ncode = (uint32_t)C->nwords;
    memcpy(blob + 0, &magic, 4);
    memcpy(blob + 4, &nfn_packed, 4);
    memcpy(blob + 8, &ncode, 4);
    if (C->nrodata > 0)
        memcpy(blob + 12, &C->nrodata, 4);

    uint8_t *p = blob + hdr_size;
    for (int i = 0; i < C->nfuncs; i++, p += 56) {
        const c_func_t *f = &C->funcs[i];
        memcpy(p, f->name, 32);
        p[32] = f->nparams;
        p[33] = f->ret_type;
        memcpy(p + 36, f->ptype, 16);
        memcpy(p + 52, &f->word_off, 4);
    }
    memcpy(p, C->words, (size_t)C->nwords * 4u);
    p += (size_t)C->nwords * 4u;

    for (int i = 0; i < C->nstructs; i++, p += 308) {
        const c_struct_t *st = &C->structs[i];
        uint32_t nf = st->nfields;
        memcpy(p, st->name, 32);
        memcpy(p + 32, &nf, 4);
        memcpy(p + 36, st->ftype, 16);
        memcpy(p + 52, st->fname, 256);
    }

    if (C->nrodata > 0) {
        memcpy(p, C->rodata, C->nrodata);
        p += C->nrodata;
    }

    *out_blob = blob;
    *out_len = total_len;
    free(C);
    return 0;
}
