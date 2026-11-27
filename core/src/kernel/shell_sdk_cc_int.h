/*
 * Shell SDK, bm C: what the reader (shell_sdk_cc_parse.c), the writer of the 6502 (shell_sdk_cc_gen.c) and the
 * runtime (shell_sdk_cc_rt.c) share. Original code.
 */
#ifndef SHELL_SDK_CC_INT_H
#define SHELL_SDK_CC_INT_H

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

#include "shell_sdk_cc.h"

/* ---- types ---- */

enum { CT_VOID, CT_U8, CT_S8, CT_U16, CT_S16 };

typedef struct {
    uint8_t base;               /* CT_* */
    uint8_t ptr;                /* how many '*' */
    int len;                    /* an array: its length (-1: from the list of values); 0: not an array */
} ctype_t;

/* ---- the tree ---- */

enum {
    /* expressions */
    N_NUM, N_NAME, N_STR, N_UNARY, N_BINARY, N_AND, N_OR, N_ASSIGN, N_PREINC, N_POSTINC, N_INDEX, N_CALL,
    N_DEREF, N_ADDR, N_CAST, N_COND, N_LIST, N_BLANK,
    /* statements */
    S_BLOCK, S_DECL, S_IF, S_WHILE, S_DO, S_FOR, S_BREAK, S_CONTINUE, S_RETURN, S_EXPR, S_ASM, S_EMPTY
};

/* operators of N_UNARY, N_BINARY, N_ASSIGN (0: a plain '='), N_PREINC and N_POSTINC */
enum {
    OP_NONE, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_AND, OP_OR, OP_XOR, OP_SHL, OP_SHR,
    OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE, OP_NEG, OP_NOT, OP_INV
};

typedef struct sym sym_t;

typedef struct node {
    uint8_t kind, op;
    uint8_t constant;           /* an expression whose value is known: value */
    uint8_t wrapped;            /* it was written between parentheses */
    int line;
    ctype_t type;
    struct node *a, *b, *c, *d; /* the parts */
    struct node *next;          /* the next of a list: statements, arguments, values */
    long value;
    const char *name;           /* N_NAME, N_CALL, S_DECL; the text of N_STR */
    sym_t *sym;
} node_t;

/* ---- names ---- */

enum { SY_VAR, SY_CONST, SY_ROM, SY_FUNC, SY_BUILTIN };

#define CC_PARAMS 8

struct sym {
    const char *name;
    uint8_t kind;
    uint8_t is_const;
    uint8_t param;              /* a parameter */
    ctype_t type;               /* of a function: what it gives back */
    long value;                 /* SY_CONST */
    int line;
    char label[96];             /* its name in the assembly */
    int address;                /* SY_VAR: where it lives */
    /* functions */
    int nparams;
    sym_t *params[CC_PARAMS];
    node_t *body;
    node_t *init;               /* a global's value or list */
    sym_t **calls;              /* the functions it calls */
    int ncalls, calls_max;
    int state;                  /* the search for recursion */
    int builtin;                /* SY_BUILTIN: which */
    sym_t *next;                /* in its scope */
    sym_t *all;                 /* every symbol, in the order of the source */
};

/* ---- the compiler ---- */

#define CC_DEPTH 120            /* how deep an expression or the statements may nest */

typedef struct cc_block cc_block_t;

typedef struct {
    const char *src;
    size_t len;
    jmp_buf fail;
    char *err;
    size_t en;
    cc_block_t *blocks;         /* the memory of the tree, freed at the end */
    sdk_cc_out_t *out;
    void *user;                 /* the writer's state (cc_constant and #define use its names) */
} cc_t;

/* the whole program: the declarations at the top, in their order */
typedef struct {
    node_t *decls;              /* S_DECL (variables), S_BLOCK with sym->kind SY_FUNC (functions) */
    node_t *tiles;              /* the values of tiles { }: N_NUM and N_BLANK */
} cc_program_t;

/* Stops the compile with a message for a line of the source. */
void cc_error(cc_t *cc, int line, const char *fmt, ...) __attribute__((format(printf, 3, 4), noreturn));
void *cc_alloc(cc_t *cc, size_t n);                 /* zeroed; freed with the compiler */
char *cc_strdup(cc_t *cc, const char *s, size_t n);

/* The reader: the text into the tree. #define gives constants to `define`. */
void cc_parse(cc_t *cc, cc_program_t *prog, void (*define)(cc_t *, const char *, node_t *, int));
/* The value of an expression of numbers and of #define names (lookup gives them); fails when it is not one. */
long cc_constant(cc_t *cc, node_t *n);

/* ---- the runtime ---- */

enum {
    RT_BASE = 1, RT_MUL = 2, RT_UDIV = 4, RT_SDIV = 8, RT_SHIFT = 16, RT_SPRITE = 32, RT_PALETTE = 64,
    RT_RANDOM = 128
};

const char *cc_runtime_head(void);                  /* the registers and the places of the runtime */
const char *cc_runtime_part(int part);              /* the routines of one RT_* */

#define CC_ZP_FIRST   0x20                          /* the zero page of the program */
#define CC_ZP_END     0x100
#define CC_RAM_FIRST  0x320                         /* after the shadow of the palette */
#define CC_RAM_END    0x800

#endif /* SHELL_SDK_CC_INT_H */
