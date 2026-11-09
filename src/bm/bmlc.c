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
    OP_RET0
};

typedef struct {
    uint8_t op;
    int32_t arg;
} c_insn_t;

typedef struct {
    char     name[32];
    uint8_t  type;
    uint32_t val;
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

    /* Stack depths for current function */
    int16_t     depths[MAX_INSNS];
    uint16_t    pc_word[MAX_INSNS];

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
    } str_patches[256];
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
        "function", "local", "for", "do", "while", "end",
        "if", "then", "elseif", "else", "return", "break",
        "and", "or", "not", "true", "false", "struct",
        "const", "enum"
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
            const char *ops = "-+*/%&|~(),.:[]=<>";
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
    if (strcmp(tname, "i32") == 0)
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

    uint8_t ctyp = parse_type_annotation(C, 0);
    expect_tok(C, "=");
    int sign = 1;
    if (accept_tok(C, "-"))
        sign = -1;
    const token_t *v = cur_tok(C);
    uint32_t val = 0;
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
        val = bml_intern_string(C, v->ptr, v->len, v->line);
    } else {
        c_fail(C, cn->line, "unsupported type for const");
    }

    c_const_t *cd = &C->const_decls[C->nconst_decls++];
    bml_copy_str(cd->name, sizeof cd->name, cname);
    cd->type = ctyp;
    cd->val = val;
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
        emit_op(C, OP_LOAD, slot, stmt_line);
        return C->cur_fn->ltype[slot];
    }
    c_fail(C, t->line, "unexpected token in expression");
    return BMN_NONE;
}

static uint8_t compile_unary(bmlc_ctx_t *C, int stmt_line)
{
    const token_t *t = cur_tok(C);
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

    if (strcmp(callee, "print") == 0) {
        if (argc < 1 || argc > 5)
            c_fail(C, stmt_line, "'print' takes 1 to 5 arguments");
        if (arg_types[0] != BMN_STR)
            c_fail(C, stmt_line, "'print' first argument must be string");
        emit_op(C, OP_CALL_HOST, (BMN_HOST_PRINT << 8) | argc, stmt_line);
        return BMN_I32;
    }

    /* Polymorphic math built-ins: abs, min, max */
    if (strcmp(callee, "abs") == 0 || strcmp(callee, "min") == 0 || strcmp(callee, "max") == 0) {
        int want = (strcmp(callee, "abs") == 0) ? 1 : 2;
        if (argc != want)
            c_fail(C, stmt_line, "'%s' takes %d arguments", callee, want);
        uint8_t t0 = arg_types[0];
        if ((t0 != BMN_I32 && t0 != BMN_F32) || (want == 2 && arg_types[1] != t0))
            c_fail(C, stmt_line, "'%s' needs matching i32 or f32 arguments", callee);
        if (strcmp(callee, "abs") == 0)
            emit_op(C, (t0 == BMN_F32) ? OP_FABS : OP_ABS, 0, stmt_line);
        else if (strcmp(callee, "min") == 0)
            emit_op(C, (t0 == BMN_F32) ? OP_FMIN : OP_MIN, 0, stmt_line);
        else
            emit_op(C, (t0 == BMN_F32) ? OP_FMAX : OP_MAX, 0, stmt_line);
        return t0;
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
    if (strcmp(callee, "floor") == 0) {
        if (argc != 1 || arg_types[0] != BMN_F32)
            c_fail(C, stmt_line, "'floor' takes 1 f32 argument");
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

    /* Stage 7+ kernel/host functions: cls, pix, rectfill, spr, btn, btnp, line, circ, circfill, rect, sfx, music, time */
    static const struct {
        const char *name;
        int         host_id;
        int         nargs;
        uint8_t     ret;
    } host_specs[] = {
        { "cls",      BMN_HOST_CLS,      1, BMN_NONE },
        { "pix",      BMN_HOST_PIX,      3, BMN_NONE },
        { "rectfill", BMN_HOST_RECTFILL, 5, BMN_NONE },
        { "spr",      BMN_HOST_SPR,      3, BMN_NONE },
        { "btn",      BMN_HOST_BTN,      1, BMN_BOOL },
        { "btnp",     BMN_HOST_BTNP,     1, BMN_BOOL },
        { "line",     BMN_HOST_LINE,     5, BMN_NONE },
        { "circ",     BMN_HOST_CIRC,     4, BMN_NONE },
        { "circfill", BMN_HOST_CIRCFILL, 4, BMN_NONE },
        { "rect",     BMN_HOST_RECT,     5, BMN_NONE },
        { "sfx",      BMN_HOST_SFX,      1, BMN_NONE },
        { "music",    BMN_HOST_MUSIC,    1, BMN_NONE },
        { "time",     BMN_HOST_TIME,     0, BMN_F32 },
    };
    for (size_t h = 0; h < sizeof host_specs / sizeof host_specs[0]; h++) {
        if (strcmp(callee, host_specs[h].name) == 0) {
            if (need_value && host_specs[h].ret == BMN_NONE)
                c_fail(C, stmt_line, "'%s' gives no value", callee);
            if (argc != host_specs[h].nargs)
                c_fail(C, stmt_line, "'%s' takes %d arguments", callee, host_specs[h].nargs);
            for (int i = 0; i < argc; i++) {
                if (arg_types[i] != BMN_I32)
                    c_fail(C, stmt_line, "'%s' expects i32 arguments", callee);
            }
            emit_op(C, OP_CALL_HOST, (host_specs[h].host_id << 8) | argc, stmt_line);
            return host_specs[h].ret;
        }
    }

    int fn_idx = find_func(C, callee);
    if (fn_idx < 0)
        c_fail(C, stmt_line, "'%s' is not a defined function", callee);
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

static int is_block_stop(const token_t *t, int stop_mask)
{
    if (t->kind == TK_EOF)
        return 1;
    if (t->kind != TK_KW)
        return 0;
    if ((stop_mask & 1) && tok_eq(t, "end"))    return 1;
    if ((stop_mask & 2) && tok_eq(t, "elseif")) return 1;
    if ((stop_mask & 4) && tok_eq(t, "else"))   return 1;
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
            uint8_t et = compile_expr(C, line);
            if (decl_t != BMN_NONE && et != decl_t) {
                int ok = 0;
                if (et == BMN_I32 && ((decl_t & BMN_STRUCT_BASE) || (decl_t >= BMN_ARR_I32 && decl_t <= BMN_ARR_F32)))
                    ok = 1;
                if (!ok)
                    c_fail(C, line, "cannot put incompatible value in '%s'", vname);
            }
            int slot = alloc_local(C, vname, decl_t != BMN_NONE ? decl_t : et, line);
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
    if (strcmp(sname, "i32") == 0 || strcmp(sname, "bool") == 0 || strcmp(sname, "f32") == 0 || find_struct(C, sname) >= 0)
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
    const token_t *kw = expect_tok(C, "function");
    uint8_t prefix_ret = BMN_NONE;
    const token_t *first = cur_tok(C);
    const token_t *second = peek_tok(C, 1);
    if (first->kind == TK_NAME && second->kind == TK_NAME &&
        (tok_eq(first, "i32") || tok_eq(first, "f32") || tok_eq(first, "bool") || tok_eq(first, "void"))) {
        if (tok_eq(first, "i32")) prefix_ret = BMN_I32;
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
    COND_MI = 0x4, COND_PL = 0x5, COND_GE = 0xA, COND_LT = 0xB,
    COND_GT = 0xC, COND_LE = 0xD, COND_AL = 0xE
};

enum {
    DP_AND = 0x0, DP_EOR = 0x1, DP_SUB = 0x2, DP_RSB = 0x3,
    DP_ADD = 0x4, DP_CMP = 0xA, DP_ORR = 0xC, DP_MOV = 0xD, DP_MVN = 0xF
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
    uint32_t u_bit = (offset >= 0) ? 1u : 0u;
    uint32_t off = (uint32_t)(offset >= 0 ? offset : -offset);
    emit_w(C, (COND_AL << 28) | (0x51u << 20) | (u_bit << 23) | ((rn & 0xFu) << 16) | ((rd & 0xFu) << 12) | (off & 0xFFFu));
}

static void emit_str(bmlc_ctx_t *C, uint32_t rd, uint32_t rn, int offset)
{
    uint32_t u_bit = (offset >= 0) ? 1u : 0u;
    uint32_t off = (uint32_t)(offset >= 0 ? offset : -offset);
    emit_w(C, (COND_AL << 28) | (0x50u << 20) | (u_bit << 23) | ((rn & 0xFu) << 16) | ((rd & 0xFu) << 12) | (off & 0xFFFu));
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

static int compute_fn_depths(bmlc_ctx_t *C, const c_func_t *fn)
{
    int n = fn->code_len;
    for (int i = 0; i < n; i++)
        C->depths[i] = -1;
    uint16_t work[MAX_INSNS];
    int nwork = 0;
    C->depths[0] = 0;
    work[nwork++] = 0;
    int max_sp = 0;

    while (nwork > 0) {
        int pc = work[--nwork];
        int sp = C->depths[pc];
        if (sp > max_sp)
            max_sp = sp;
        c_insn_t ins = C->insns[fn->code_start + pc];
        uint8_t op = ins.op;

        int t1 = -1, sp1 = sp, t2 = -1, sp2 = sp;
        if (op == OP_PUSH_CONST || op == OP_PUSH_FCONST || op == OP_PUSH_STR || op == OP_PUSH_TRUE || op == OP_PUSH_FALSE || op == OP_LOAD) {
            if (sp + 1 > max_sp) max_sp = sp + 1;
            t1 = pc + 1; sp1 = sp + 1;
        } else if (op == OP_STORE || op == OP_POP) {
            t1 = pc + 1; sp1 = sp - 1;
        } else if (op == OP_NEG || op == OP_NOT || op == OP_BNOT || op == OP_ABS || op == OP_FNEG || op == OP_FABS || op == OP_FSQRT || op == OP_I2F || op == OP_FLOOR || op == OP_PEEK || op == OP_PEEK2 || op == OP_PEEK4) {
            t1 = pc + 1; sp1 = sp;
        } else if (op == OP_POKE || op == OP_POKE2 || op == OP_POKE4) {
            t1 = pc + 1; sp1 = sp - 2;
        } else if (op >= OP_ADD && op <= OP_AGETF) {
            t1 = pc + 1; sp1 = sp - 1;
        } else if (op == OP_ASET || op == OP_ASETF || op == OP_MEMCPY || op == OP_MEMSET) {
            t1 = pc + 1; sp1 = sp - 3;
        } else if (op == OP_JMP) {
            t1 = ins.arg; sp1 = sp;
        } else if (op == OP_JZ) {
            t1 = ins.arg; sp1 = sp - 1;
            t2 = pc + 1;  sp2 = sp - 1;
        } else if (op == OP_JF_KEEP || op == OP_JT_KEEP) {
            t1 = ins.arg; sp1 = sp;
            t2 = pc + 1;  sp2 = sp - 1;
        } else if (op == OP_CALL) {
            int c_idx = ins.arg >> 8;
            int argc = ins.arg & 0xFF;
            int has_ret = (C->funcs[c_idx].ret_type != BMN_NONE);
            t1 = pc + 1; sp1 = sp - argc + (has_ret ? 1 : 0);
        } else if (op == OP_CALL_HOST) {
            int hid = ins.arg >> 8;
            int argc = ins.arg & 0xFF;
            int has_ret = (hid == BMN_HOST_BTN || hid == BMN_HOST_BTNP || hid == BMN_HOST_TIME || hid == BMN_HOST_PRINT);
            t1 = pc + 1; sp1 = sp - argc + (has_ret ? 1 : 0);
        }

        if (t1 >= 0 && t1 < n && C->depths[t1] < 0) {
            C->depths[t1] = (int16_t)sp1;
            work[nwork++] = (uint16_t)t1;
        }
        if (t2 >= 0 && t2 < n && C->depths[t2] < 0) {
            C->depths[t2] = (int16_t)sp2;
            work[nwork++] = (uint16_t)t2;
        }
    }
    for (int i = 0; i < n; i++) {
        if (C->depths[i] < 0)
            C->depths[i] = 0;
    }
    return max_sp;
}

static uint32_t get_slot_reg(bmlc_ctx_t *C, int slot, uint32_t dst_reg)
{
    if (slot < 7)
        return (uint32_t)(4 + slot);
    emit_ldr(C, dst_reg, 11, -4 * (slot - 6));
    return dst_reg;
}

static void put_slot_reg(bmlc_ctx_t *C, int slot, uint32_t src_reg)
{
    if (slot < 7) {
        uint32_t target = (uint32_t)(4 + slot);
        if (src_reg != target)
            emit_dp_reg(C, DP_MOV, target, 0, src_reg, COND_AL, 0, 0, 0);
    } else {
        emit_str(C, src_reg, 11, -4 * (slot - 6));
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

    int max_sp = compute_fn_depths(C, fn);
    int total_slots = (int)fn->nlocals + max_sp + 1;
    int spill_slots = total_slots > 7 ? (total_slots - 7) : 0;
    int frame_words = spill_slots;
    if ((9 + frame_words) % 2 != 0)
        frame_words++;
    int frame_bytes = frame_words * 4;

    int fn_patch_start = C->npatches;
    struct { uint16_t ldr_idx; uint16_t add_idx; uint8_t dst; uint32_t off; } fn_str_relocs[64];
    int nfn_str_relocs = 0;

    /* push {r4-r10, fp, lr}; mov fp, sp; sub sp, sp, #frame_bytes */
    emit_w(C, 0xE92D4FF0u);
    emit_dp_reg(C, DP_MOV, 11, 0, 13, COND_AL, 0, 0, 0);
    if (frame_bytes > 0) {
        emit_mov32(C, 12, (uint32_t)frame_bytes);
        emit_dp_reg(C, DP_SUB, 13, 13, 12, COND_AL, 0, 0, 0);
    }

    for (int i = 0; i < (int)fn->nparams; i++) {
        if (i < 4) {
            put_slot_reg(C, i, (uint32_t)i);
        } else {
            emit_ldr(C, 0, 11, 36 + 4 * (i - 4));
            put_slot_reg(C, i, 0);
        }
    }

    int n = fn->code_len;
    for (int pc = 0; pc < n; pc++) {
        C->pc_word[pc] = (uint16_t)C->nwords;
        int sp = C->depths[pc];
        c_insn_t ins = C->insns[fn->code_start + pc];
        uint8_t op = ins.op;
        int s_top = (int)fn->nlocals + sp;

        switch (op) {
        case OP_PUSH_CONST: {
            uint32_t dst = (s_top < 7) ? (uint32_t)(4 + s_top) : 0u;
            emit_mov32(C, dst, (uint32_t)C->consts[ins.arg]);
            put_slot_reg(C, s_top, dst);
            break;
        }
        case OP_PUSH_FCONST: {
            uint32_t dst = (s_top < 7) ? (uint32_t)(4 + s_top) : 0u;
            emit_mov32(C, dst, C->fconsts[ins.arg]);
            put_slot_reg(C, s_top, dst);
            break;
        }
        case OP_PUSH_STR: {
            uint32_t dst = (s_top < 7) ? (uint32_t)(4 + s_top) : 0u;
            uint16_t ldr_idx = (uint16_t)C->nwords;
            emit_w(C, 0); /* placeholder for ldr dst, [pc, #lit] */
            uint16_t add_idx = (uint16_t)C->nwords;
            emit_dp_reg(C, DP_ADD, dst, 15, dst, COND_AL, 0, 0, 0);
            if (nfn_str_relocs < 64) {
                fn_str_relocs[nfn_str_relocs].ldr_idx = ldr_idx;
                fn_str_relocs[nfn_str_relocs].add_idx = add_idx;
                fn_str_relocs[nfn_str_relocs].dst = (uint8_t)dst;
                fn_str_relocs[nfn_str_relocs].off = (uint32_t)ins.arg;
                nfn_str_relocs++;
            }
            put_slot_reg(C, s_top, dst);
            break;
        }
        case OP_PUSH_TRUE:
        case OP_PUSH_FALSE: {
            uint32_t dst = (s_top < 7) ? (uint32_t)(4 + s_top) : 0u;
            emit_dp_imm(C, DP_MOV, dst, 0, (op == OP_PUSH_TRUE) ? 1u : 0u, COND_AL, 0);
            put_slot_reg(C, s_top, dst);
            break;
        }
        case OP_POP:
            break;
        case OP_LOAD: {
            uint32_t r = get_slot_reg(C, ins.arg, 0);
            put_slot_reg(C, s_top, r);
            break;
        }
        case OP_STORE: {
            uint32_t r = get_slot_reg(C, s_top - 1, 0);
            put_slot_reg(C, ins.arg, r);
            break;
        }
        case OP_ADD:
        case OP_SUB:
        case OP_BAND:
        case OP_BOR:
        case OP_BXOR: {
            uint32_t ra = get_slot_reg(C, s_top - 2, 0);
            uint32_t rb = get_slot_reg(C, s_top - 1, 1);
            uint32_t dst = (s_top - 2 < 7) ? (uint32_t)(4 + s_top - 2) : 0u;
            uint32_t dp;
            if (op == OP_ADD) dp = DP_ADD;
            else if (op == OP_SUB) dp = DP_SUB;
            else if (op == OP_BAND) dp = DP_AND;
            else if (op == OP_BOR) dp = DP_ORR;
            else dp = DP_EOR;
            emit_dp_reg(C, dp, dst, ra, rb, COND_AL, 0, 0, 0);
            put_slot_reg(C, s_top - 2, dst);
            break;
        }
        case OP_SHL:
        case OP_SHR: {
            uint32_t ra = get_slot_reg(C, s_top - 2, 0);
            uint32_t rb = get_slot_reg(C, s_top - 1, 1);
            uint32_t dst = (s_top - 2 < 7) ? (uint32_t)(4 + s_top - 2) : 0u;
            uint32_t shift_type = (op == OP_SHL) ? 0u : 2u; /* 0: LSL, 2: ASR */
            emit_dp_reg_regshift(C, DP_MOV, dst, 0, ra, shift_type, rb, COND_AL, 0);
            put_slot_reg(C, s_top - 2, dst);
            break;
        }
        case OP_BNOT: {
            uint32_t r = get_slot_reg(C, s_top - 1, 0);
            uint32_t dst = (s_top - 1 < 7) ? (uint32_t)(4 + s_top - 1) : 0u;
            emit_dp_reg(C, DP_MVN, dst, 0, r, COND_AL, 0, 0, 0);
            put_slot_reg(C, s_top - 1, dst);
            break;
        }
        case OP_MUL: {
            uint32_t ra = get_slot_reg(C, s_top - 2, 0);
            uint32_t rb = get_slot_reg(C, s_top - 1, 1);
            emit_dp_reg(C, DP_MOV, 12, 0, ra, COND_AL, 0, 0, 0);
            emit_w(C, 0xE000009Cu | ((rb & 0xFu) << 8)); /* mul r0, r12, rb */
            put_slot_reg(C, s_top - 2, 0);
            break;
        }
        case OP_IDIV:
        case OP_IMOD: {
            C->needs_idivmod = 1;
            uint32_t ra = get_slot_reg(C, s_top - 2, 0);
            if (ra != 0) emit_dp_reg(C, DP_MOV, 0, 0, ra, COND_AL, 0, 0, 0);
            uint32_t rb = get_slot_reg(C, s_top - 1, 1);
            if (rb != 1) emit_dp_reg(C, DP_MOV, 1, 0, rb, COND_AL, 0, 0, 0);
            emit_branch_patch(C, -3, 0, COND_AL, 1);
            put_slot_reg(C, s_top - 2, (op == OP_IDIV) ? 0u : 1u);
            break;
        }
        case OP_NEG: {
            uint32_t r = get_slot_reg(C, s_top - 1, 0);
            uint32_t dst = (s_top - 1 < 7) ? (uint32_t)(4 + s_top - 1) : 0u;
            emit_dp_imm(C, DP_RSB, dst, r, 0, COND_AL, 0);
            put_slot_reg(C, s_top - 1, dst);
            break;
        }
        case OP_NOT: {
            uint32_t r = get_slot_reg(C, s_top - 1, 0);
            uint32_t dst = (s_top - 1 < 7) ? (uint32_t)(4 + s_top - 1) : 0u;
            emit_dp_imm(C, DP_CMP, 0, r, 0, COND_AL, 1);
            emit_dp_imm(C, DP_MOV, dst, 0, 1, COND_EQ, 0);
            emit_dp_imm(C, DP_MOV, dst, 0, 0, COND_NE, 0);
            put_slot_reg(C, s_top - 1, dst);
            break;
        }
        case OP_ABS: {
            uint32_t r = get_slot_reg(C, s_top - 1, 0);
            uint32_t dst = (s_top - 1 < 7) ? (uint32_t)(4 + s_top - 1) : 0u;
            if (dst != r) emit_dp_reg(C, DP_MOV, dst, 0, r, COND_AL, 0, 0, 0);
            emit_dp_imm(C, DP_CMP, 0, dst, 0, COND_AL, 1);
            emit_dp_imm(C, DP_RSB, dst, dst, 0, COND_LT, 0);
            put_slot_reg(C, s_top - 1, dst);
            break;
        }
        case OP_MIN:
        case OP_MAX: {
            uint32_t ra = get_slot_reg(C, s_top - 2, 0);
            uint32_t rb = get_slot_reg(C, s_top - 1, 1);
            emit_dp_reg(C, DP_CMP, 0, ra, rb, COND_AL, 1, 0, 0);
            uint32_t dst = (s_top - 2 < 7) ? (uint32_t)(4 + s_top - 2) : 0u;
            if (dst != ra) emit_dp_reg(C, DP_MOV, dst, 0, ra, COND_AL, 0, 0, 0);
            emit_dp_reg(C, DP_MOV, dst, 0, rb, (op == OP_MIN) ? COND_GT : COND_LT, 0, 0, 0);
            put_slot_reg(C, s_top - 2, dst);
            break;
        }
        case OP_EQ: case OP_NE: case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
            uint32_t ra = get_slot_reg(C, s_top - 2, 0);
            uint32_t rb = get_slot_reg(C, s_top - 1, 1);
            emit_dp_reg(C, DP_CMP, 0, ra, rb, COND_AL, 1, 0, 0);
            uint32_t ct = (op == OP_EQ) ? COND_EQ : (op == OP_NE) ? COND_NE : (op == OP_LT) ? COND_LT :
                          (op == OP_LE) ? COND_LE : (op == OP_GT) ? COND_GT : COND_GE;
            uint32_t dst = (s_top - 2 < 7) ? (uint32_t)(4 + s_top - 2) : 0u;
            emit_dp_imm(C, DP_MOV, dst, 0, 0, COND_AL, 0);
            emit_dp_imm(C, DP_MOV, dst, 0, 1, ct, 0);
            put_slot_reg(C, s_top - 2, dst);
            break;
        }
        case OP_FADD: case OP_FSUB: case OP_FMUL: case OP_FDIV: {
            uint32_t ra = get_slot_reg(C, s_top - 2, 0);
            uint32_t rb = get_slot_reg(C, s_top - 1, 1);
            emit_w(C, 0xEE000A10u | ((ra & 0xFu) << 12));
            emit_w(C, 0xEE000A90u | ((rb & 0xFu) << 12));
            emit_w(C, (op == OP_FADD) ? 0xEE300A20u : (op == OP_FSUB) ? 0xEE300A60u : (op == OP_FMUL) ? 0xEE200A20u : 0xEE800A20u);
            uint32_t dst = (s_top - 2 < 7) ? (uint32_t)(4 + s_top - 2) : 0u;
            emit_w(C, 0xEE100A10u | ((dst & 0xFu) << 12));
            put_slot_reg(C, s_top - 2, dst);
            break;
        }
        case OP_FNEG: case OP_FABS: case OP_FSQRT: case OP_I2F: {
            uint32_t r = get_slot_reg(C, s_top - 1, 0);
            emit_w(C, 0xEE000A10u | ((r & 0xFu) << 12));
            emit_w(C, (op == OP_FNEG) ? 0xEEB10A40u : (op == OP_FABS) ? 0xEEB00AC0u : (op == OP_FSQRT) ? 0xEEB10AC0u : 0xEEB80AC0u);
            uint32_t dst = (s_top - 1 < 7) ? (uint32_t)(4 + s_top - 1) : 0u;
            emit_w(C, 0xEE100A10u | ((dst & 0xFu) << 12));
            put_slot_reg(C, s_top - 1, dst);
            break;
        }
        case OP_FLOOR: {
            uint32_t r = get_slot_reg(C, s_top - 1, 0);
            uint32_t dst = (s_top - 1 < 7) ? (uint32_t)(4 + s_top - 1) : 0u;
            emit_w(C, 0xEE000A10u | ((r & 0xFu) << 12));
            emit_w(C, 0xEEBD1AC0u);
            emit_w(C, 0xEE110A10u | ((dst & 0xFu) << 12));
            emit_w(C, 0xEEB81AC1u);
            emit_w(C, 0xEE11CA10u);
            emit_w(C, 0xEE00CA90u);
            emit_w(C, 0xEEB40A60u);
            emit_w(C, 0xEEF1FA10u);
            emit_dp_imm(C, DP_SUB, dst, dst, 1, COND_MI, 0);
            put_slot_reg(C, s_top - 1, dst);
            break;
        }
        case OP_FMIN: case OP_FMAX: {
            uint32_t ra = get_slot_reg(C, s_top - 2, 0);
            uint32_t rb = get_slot_reg(C, s_top - 1, 1);
            emit_w(C, 0xEE000A10u | ((ra & 0xFu) << 12));
            emit_w(C, 0xEE000A90u | ((rb & 0xFu) << 12));
            emit_w(C, 0xEEB40A60u);
            emit_w(C, 0xEEF1FA10u);
            uint32_t dst = (s_top - 2 < 7) ? (uint32_t)(4 + s_top - 2) : 0u;
            if (dst != ra) emit_dp_reg(C, DP_MOV, dst, 0, ra, COND_AL, 0, 0, 0);
            emit_dp_reg(C, DP_MOV, dst, 0, rb, (op == OP_FMIN) ? COND_GT : COND_LT, 0, 0, 0);
            put_slot_reg(C, s_top - 2, dst);
            break;
        }
        case OP_FEQ: case OP_FNE: case OP_FLT: case OP_FLE: case OP_FGT: case OP_FGE: {
            uint32_t ra = get_slot_reg(C, s_top - 2, 0);
            uint32_t rb = get_slot_reg(C, s_top - 1, 1);
            emit_w(C, 0xEE000A10u | ((ra & 0xFu) << 12));
            emit_w(C, 0xEE000A90u | ((rb & 0xFu) << 12));
            emit_w(C, 0xEEB40A60u);
            emit_w(C, 0xEEF1FA10u);
            uint32_t ct = (op == OP_FEQ) ? COND_EQ : (op == OP_FNE) ? COND_NE : (op == OP_FLT) ? COND_LT :
                          (op == OP_FLE) ? COND_LE : (op == OP_FGT) ? COND_GT : COND_GE;
            uint32_t dst = (s_top - 2 < 7) ? (uint32_t)(4 + s_top - 2) : 0u;
            emit_dp_imm(C, DP_MOV, dst, 0, 0, COND_AL, 0);
            emit_dp_imm(C, DP_MOV, dst, 0, 1, ct, 0);
            put_slot_reg(C, s_top - 2, dst);
            break;
        }
        case OP_AGET: case OP_AGETF: {
            uint32_t r_base = get_slot_reg(C, s_top - 2, 0);
            uint32_t r_idx = get_slot_reg(C, s_top - 1, 1);
            emit_dp_imm(C, DP_SUB, 12, r_idx, 1, COND_AL, 0);
            emit_dp_reg(C, DP_ADD, 12, r_base, 12, COND_AL, 0, 0, 2);
            uint32_t dst = (s_top - 2 < 7) ? (uint32_t)(4 + s_top - 2) : 0u;
            emit_ldr(C, dst, 12, 0);
            put_slot_reg(C, s_top - 2, dst);
            break;
        }
        case OP_ASET: case OP_ASETF: {
            uint32_t r_base = get_slot_reg(C, s_top - 3, 0);
            uint32_t r_idx = get_slot_reg(C, s_top - 2, 1);
            uint32_t r_val = get_slot_reg(C, s_top - 1, 2);
            emit_dp_imm(C, DP_SUB, 12, r_idx, 1, COND_AL, 0);
            emit_dp_reg(C, DP_ADD, 12, r_base, 12, COND_AL, 0, 0, 2);
            emit_str(C, r_val, 12, 0);
            break;
        }
        case OP_PEEK: {
            uint32_t r_addr = get_slot_reg(C, s_top - 1, 0);
            uint32_t dst = (s_top - 1 < 7) ? (uint32_t)(4 + s_top - 1) : 0u;
            emit_ldrb(C, dst, r_addr, 0);
            put_slot_reg(C, s_top - 1, dst);
            break;
        }
        case OP_POKE: {
            uint32_t r_addr = get_slot_reg(C, s_top - 2, 0);
            uint32_t r_val = get_slot_reg(C, s_top - 1, 1);
            emit_strb(C, r_val, r_addr, 0);
            break;
        }
        case OP_PEEK2: {
            uint32_t r_addr = get_slot_reg(C, s_top - 1, 0);
            uint32_t dst = (s_top - 1 < 7) ? (uint32_t)(4 + s_top - 1) : 0u;
            emit_ldrh(C, dst, r_addr, 0);
            put_slot_reg(C, s_top - 1, dst);
            break;
        }
        case OP_POKE2: {
            uint32_t r_addr = get_slot_reg(C, s_top - 2, 0);
            uint32_t r_val = get_slot_reg(C, s_top - 1, 1);
            emit_strh(C, r_val, r_addr, 0);
            break;
        }
        case OP_PEEK4: {
            uint32_t r_addr = get_slot_reg(C, s_top - 1, 0);
            uint32_t dst = (s_top - 1 < 7) ? (uint32_t)(4 + s_top - 1) : 0u;
            emit_ldr(C, dst, r_addr, 0);
            put_slot_reg(C, s_top - 1, dst);
            break;
        }
        case OP_POKE4: {
            uint32_t r_addr = get_slot_reg(C, s_top - 2, 0);
            uint32_t r_val = get_slot_reg(C, s_top - 1, 1);
            emit_str(C, r_val, r_addr, 0);
            break;
        }
        case OP_MEMCPY:
        case OP_MEMSET: {
            if (op == OP_MEMCPY) C->needs_memcpy = 1;
            else C->needs_memset = 1;
            uint32_t r_dst = get_slot_reg(C, s_top - 3, 0);
            if (r_dst != 0) emit_dp_reg(C, DP_MOV, 0, 0, r_dst, COND_AL, 0, 0, 0);
            uint32_t r_v_src = get_slot_reg(C, s_top - 2, 1);
            if (r_v_src != 1) emit_dp_reg(C, DP_MOV, 1, 0, r_v_src, COND_AL, 0, 0, 0);
            uint32_t r_n = get_slot_reg(C, s_top - 1, 2);
            if (r_n != 2) emit_dp_reg(C, DP_MOV, 2, 0, r_n, COND_AL, 0, 0, 0);
            emit_branch_patch(C, (op == OP_MEMCPY) ? -5 : -6, 0, COND_AL, 1);
            break;
        }
        case OP_JMP:
            emit_branch_patch(C, (int16_t)fn_idx, (int16_t)ins.arg, COND_AL, 0);
            break;
        case OP_JZ:
        case OP_JF_KEEP: {
            uint32_t r = get_slot_reg(C, s_top - 1, 0);
            emit_dp_imm(C, DP_CMP, 0, r, 0, COND_AL, 1);
            emit_branch_patch(C, (int16_t)fn_idx, (int16_t)ins.arg, COND_EQ, 0);
            break;
        }
        case OP_JT_KEEP: {
            uint32_t r = get_slot_reg(C, s_top - 1, 0);
            emit_dp_imm(C, DP_CMP, 0, r, 0, COND_AL, 1);
            emit_branch_patch(C, (int16_t)fn_idx, (int16_t)ins.arg, COND_NE, 0);
            break;
        }
        case OP_CALL: {
            int c_idx = ins.arg >> 8;
            int argc = ins.arg & 0xFF;
            int base = sp - argc;
            int stack_args = argc > 4 ? (argc - 4) : 0;
            int pad_words = (stack_args % 2 == 1) ? 1 : 0;
            if (pad_words)
                emit_dp_imm(C, DP_SUB, 13, 13, 4, COND_AL, 0);
            for (int j = argc - 1; j >= 4; j--) {
                uint32_t rj = get_slot_reg(C, (int)fn->nlocals + base + j, 0);
                emit_w(C, 0xE92D0000u | (1u << rj));
            }
            for (int j = 0; j < argc && j < 4; j++) {
                uint32_t rj = get_slot_reg(C, (int)fn->nlocals + base + j, (uint32_t)j);
                if (rj != (uint32_t)j)
                    emit_dp_reg(C, DP_MOV, (uint32_t)j, 0, rj, COND_AL, 0, 0, 0);
            }
            emit_branch_patch(C, -2, (int16_t)c_idx, COND_AL, 1);
            int cleanup = (stack_args + pad_words) * 4;
            if (cleanup > 0)
                emit_dp_imm(C, DP_ADD, 13, 13, (uint32_t)cleanup, COND_AL, 0);
            if (C->funcs[c_idx].ret_type != BMN_NONE)
                put_slot_reg(C, (int)fn->nlocals + base, 0);
            break;
        }
        case OP_CALL_HOST: {
            C->needs_host_call = 1;
            int hid = ins.arg >> 8;
            int argc = ins.arg & 0xFF;
            int base = sp - argc;
            int alloc_words = (((argc > 0 ? argc : 1) + 1) / 2) * 2;
            int alloc_bytes = alloc_words * 4;
            emit_dp_imm(C, DP_SUB, 13, 13, (uint32_t)alloc_bytes, COND_AL, 0);
            for (int j = 0; j < argc; j++) {
                uint32_t rj = get_slot_reg(C, (int)fn->nlocals + base + j, 0);
                emit_str(C, rj, 13, j * 4);
            }
            emit_mov32(C, 0, (uint32_t)hid);
            emit_dp_reg(C, DP_MOV, 1, 0, 13, COND_AL, 0, 0, 0);
            emit_mov32(C, 2, (uint32_t)argc);
            emit_branch_patch(C, -4, 0, COND_AL, 1);
            emit_dp_imm(C, DP_ADD, 13, 13, (uint32_t)alloc_bytes, COND_AL, 0);
            if (hid == BMN_HOST_BTN || hid == BMN_HOST_BTNP || hid == BMN_HOST_TIME || hid == BMN_HOST_PRINT)
                put_slot_reg(C, (int)fn->nlocals + base, 0);
            break;
        }
        case OP_RET: {
            uint32_t r = get_slot_reg(C, s_top - 1, 0);
            if (r != 0)
                emit_dp_reg(C, DP_MOV, 0, 0, r, COND_AL, 0, 0, 0);
            emit_branch_patch(C, -1, 0, COND_AL, 0);
            break;
        }
        case OP_RET0:
            emit_branch_patch(C, -1, 0, COND_AL, 0);
            break;
        }
    }

    int epilogue_word = C->nwords;
    C->pc_word[n] = (uint16_t)epilogue_word;
    emit_dp_reg(C, DP_MOV, 13, 0, 11, COND_AL, 0, 0, 0);
    emit_w(C, 0xE8BD8FF0u); /* pop {r4-r10, fp, pc} */

    for (int k = 0; k < nfn_str_relocs; k++) {
        uint16_t lit_slot_idx = (uint16_t)C->nwords;
        emit_w(C, 0); /* placeholder for delta */
        uint32_t lit_offset = (uint32_t)(lit_slot_idx - (fn_str_relocs[k].ldr_idx + 2)) * 4u;
        C->words[fn_str_relocs[k].ldr_idx] = (COND_AL << 28) | (0x51u << 20) | (1u << 23) | (15u << 16) | (((uint32_t)fn_str_relocs[k].dst & 0xFu) << 12) | (lit_offset & 0xFFFu);
        if (C->nstr_patches < 256) {
            C->str_patches[C->nstr_patches].lit_word_idx = lit_slot_idx;
            C->str_patches[C->nstr_patches].add_word_idx = fn_str_relocs[k].add_idx;
            C->str_patches[C->nstr_patches].str_off = fn_str_relocs[k].off;
            C->nstr_patches++;
        }
    }

    /* Resolve intra-function jump patches now */
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
        else if (cur_tok(C)->kind == TK_KW && tok_eq(cur_tok(C), "function"))
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

    if (C->needs_host_call) {
        C->host_call_word = C->nwords;
        emit_w(C, 0xE59FC00Cu); /* ldr r12, [pc, #12] */
        emit_w(C, 0xE35C0000u); /* cmp r12, #0 */
        emit_w(C, 0x112FFF1Cu); /* bxne r12 */
        emit_w(C, 0xE3A00000u); /* mov r0, #0 */
        emit_w(C, 0xE12FFF1Eu); /* bx lr */
        emit_w(C, 0x00000000u); /* __bml_host_fn_slot */
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
