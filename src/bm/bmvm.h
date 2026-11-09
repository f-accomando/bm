/* bmvm: a small register machine for the typed subset of Lua (scripts/bmtvm.py makes its code).
 * EXPERIMENT (claude/bm-vm-test): can a cartridge carry typed hot functions as data, run by the kernel, with no native code?
 * The code is checked when it is loaded (registers, jumps, constants, calls) and while it runs (array bounds, call depth,
 * stack, a budget of steps), because a cartridge is not trusted. */
#ifndef BMVM_H
#define BMVM_H

#include <stddef.h>
#include <stdint.h>

typedef union {
    int32_t i;          /* i32 and bool; an array is its number in the table of the call */
    double d;           /* f64 (f32 is computed as f64) */
} bmvm_val;

typedef struct {
    void *data;         /* int32_t or double */
    int32_t n;          /* elements */
    int32_t is_f64;
} bmvm_arr;

typedef struct {
    uint8_t op, a, b, c;
    int32_t k;
} bmvm_insn;

/* parameter and result types in a function header */
enum { BMVM_I32 = 0, BMVM_F64 = 1, BMVM_ARR_I32 = 2, BMVM_ARR_F64 = 3, BMVM_NONE = 4 };

typedef struct {
    char name[24];
    uint8_t nparams, nregs, ret, nconst;
    uint8_t ptype[16];
    uint32_t code, ncode;       /* first instruction and how many */
    uint32_t kfirst;            /* its constants K[kfirst ...] are copied into R[nparams ...] each time it is entered */
} bmvm_fn;

typedef struct bmvm_mod {
    int nfn, nk;
    uint32_t ncode;
    bmvm_fn *fn;
    bmvm_val *k;
    bmvm_insn *code;
} bmvm_mod;

/* the operations, in this order (scripts/bmtvm.py reads this list). I: i32, F: f64. a, b, c: registers; k: a number */
#define BMVM_OPS(X) \
    X(LK)   /* R[a] = K[k] */ \
    X(MOV)  /* R[a] = R[b] */ \
    X(ADDI) X(SUBI) X(MULI) X(IDIV) X(IMOD) X(NEGI) X(ANDI) /* R[a] = R[b] op R[c] (NEGI: R[b]) */ \
    X(ADDF) X(SUBF) X(MULF) X(DIVF) X(FFDIV) X(FFMOD) X(NEGF) \
    X(I2F)  /* R[a] = (f64) R[b] */ \
    X(LTI) X(LEI) X(EQI) X(NEI) X(LTF) X(LEF) X(EQF) X(NEF) /* R[a] = R[b] cmp R[c] (0 or 1) */ \
    X(NOT)  /* R[a] = !R[b] */ \
    X(FLOOR) X(SQRT) X(ABSI) X(ABSF) /* R[a] = f(R[b]) */ \
    X(MINI) X(MAXI) X(MINF) X(MAXF) /* R[a] = f(R[b], R[c]) */ \
    X(JMP)  /* pc = k */ \
    X(JF) X(JT) /* if R[a] is 0 (JF) or not 0 (JT): pc = k */ \
    X(JFLTI) X(JFLEI) X(JFEQI) X(JFNEI) X(JFLTF) X(JFLEF) X(JFEQF) X(JFNEF) /* if not (R[a] cmp R[b]): pc = k */ \
    X(FORI) /* R[a]++; if R[a] <= R[b]: pc = k */ \
    X(GETI) X(GETF) /* R[a] = array R[b] [R[c]] (the index counts from 1) */ \
    X(SETI) X(SETF) /* array R[a] [R[b]] = R[c] */ \
    X(CALL) /* function k, arguments in R[a], R[a+1], ...; the result goes to R[a] */ \
    X(RET)  /* the result is R[a]; RET0: none */ \
    X(RET0)

enum {
#define X(n) BMVM_OP_##n,
    BMVM_OPS(X)
#undef X
    BMVM_OP_COUNT
};

/* a module from its bytes; 0 if the bytes are right, else a negative error (the message in *why). The module must be freed. */
int bmvm_load(bmvm_mod **out, const uint8_t *blob, size_t len, const char **why);
void bmvm_free(bmvm_mod *m);
int bmvm_find(const bmvm_mod *m, const char *name);     /* the number of a function, or -1 */

/* runs function fn. args: one value for each parameter (a number as the header says; for an array parameter its number in
 * arrs). arrs: the arrays of the call. 0 if it ran; else a negative error (-1 bad call, -2 array out of range or of the
 * wrong kind, -3 stack or depth, -4 budget used up). The result (if any) goes into *ret. */
int bmvm_call(const bmvm_mod *m, int fn, const bmvm_val *args, int nargs, const bmvm_arr *arrs, int narrs, bmvm_val *ret,
              uint32_t budget);

#endif
