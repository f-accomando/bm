/* bmvm: see bmvm.h. EXPERIMENT (claude/bm-vm-test). */
#include "bmvm.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BMVM_STACK 4096         /* values */
#define BMVM_DEPTH 32           /* nested calls */
#define BMVM_MAGIC 0x31564D42u  /* "BMV1" */
#define FN_BYTES 56

static uint32_t rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* which fields of an instruction are registers: bit 0 a, bit 1 b, bit 2 c; 8: k is a constant, 16: k is a jump, 32: k is a function */
static const uint8_t op_use[BMVM_OP_COUNT] = {
    [BMVM_OP_LK] = 1 | 8, [BMVM_OP_MOV] = 3,
    [BMVM_OP_ADDI] = 7, [BMVM_OP_SUBI] = 7, [BMVM_OP_MULI] = 7, [BMVM_OP_IDIV] = 7, [BMVM_OP_IMOD] = 7, [BMVM_OP_NEGI] = 3,
    [BMVM_OP_ANDI] = 7,
    [BMVM_OP_ADDF] = 7, [BMVM_OP_SUBF] = 7, [BMVM_OP_MULF] = 7, [BMVM_OP_DIVF] = 7, [BMVM_OP_FFDIV] = 7, [BMVM_OP_FFMOD] = 7,
    [BMVM_OP_NEGF] = 3, [BMVM_OP_I2F] = 3,
    [BMVM_OP_LTI] = 7, [BMVM_OP_LEI] = 7, [BMVM_OP_EQI] = 7, [BMVM_OP_NEI] = 7,
    [BMVM_OP_LTF] = 7, [BMVM_OP_LEF] = 7, [BMVM_OP_EQF] = 7, [BMVM_OP_NEF] = 7,
    [BMVM_OP_NOT] = 3, [BMVM_OP_FLOOR] = 3, [BMVM_OP_SQRT] = 3, [BMVM_OP_ABSI] = 3, [BMVM_OP_ABSF] = 3,
    [BMVM_OP_MINI] = 7, [BMVM_OP_MAXI] = 7, [BMVM_OP_MINF] = 7, [BMVM_OP_MAXF] = 7,
    [BMVM_OP_JMP] = 16, [BMVM_OP_JF] = 1 | 16, [BMVM_OP_JT] = 1 | 16, [BMVM_OP_FORI] = 3 | 16,
    [BMVM_OP_JFLTI] = 3 | 16, [BMVM_OP_JFLEI] = 3 | 16, [BMVM_OP_JFEQI] = 3 | 16, [BMVM_OP_JFNEI] = 3 | 16,
    [BMVM_OP_JFLTF] = 3 | 16, [BMVM_OP_JFLEF] = 3 | 16, [BMVM_OP_JFEQF] = 3 | 16, [BMVM_OP_JFNEF] = 3 | 16,
    [BMVM_OP_GETI] = 7, [BMVM_OP_GETF] = 7, [BMVM_OP_SETI] = 7, [BMVM_OP_SETF] = 7,
    [BMVM_OP_CALL] = 1 | 32, [BMVM_OP_RET] = 1, [BMVM_OP_RET0] = 0,
};

void bmvm_free(bmvm_mod *m)
{
    if (!m)
        return;
    free(m->fn);
    free(m->k);
    free(m->code);
    free(m);
}

int bmvm_load(bmvm_mod **out, const uint8_t *blob, size_t len, const char **why)
{
    const char *w = "";
    bmvm_mod *m = NULL;
    *out = NULL;
    if (len < 12 || rd32(blob) != BMVM_MAGIC) {
        w = "not a bmvm module";
        goto bad;
    }
    const int nfn = rd16(blob + 4), nk = rd16(blob + 6);
    const uint32_t ncode = rd32(blob + 8);
    if (nfn < 1 || nfn > 64 || ncode > 100000 || (size_t)nk > 65535 ||
        len != 12 + (size_t)nfn * FN_BYTES + (size_t)nk * 8 + (size_t)ncode * 8) {
        w = "bad sizes";
        goto bad;
    }
    m = calloc(1, sizeof *m);
    if (!m) {
        w = "no memory";
        goto bad;
    }
    m->nfn = nfn; m->nk = nk; m->ncode = ncode;
    m->fn = calloc((size_t)nfn, sizeof *m->fn);
    m->k = calloc((size_t)nk + 1, sizeof *m->k);
    m->code = calloc((size_t)ncode + 1, sizeof *m->code);
    if (!m->fn || !m->k || !m->code) {
        w = "no memory";
        goto bad;
    }
    const uint8_t *p = blob + 12;
    for (int i = 0; i < nfn; i++, p += FN_BYTES) {
        bmvm_fn *f = &m->fn[i];
        memcpy(f->name, p, 23);
        f->name[23] = 0;
        f->nparams = p[24]; f->nregs = p[25]; f->ret = p[26]; f->nconst = p[27];
        memcpy(f->ptype, p + 28, 16);
        f->code = rd32(p + 44); f->ncode = rd32(p + 48); f->kfirst = rd32(p + 52);
        if (f->nparams > 16 || f->nregs < f->nparams || f->nregs == 0 || f->ret > BMVM_NONE ||
            (uint64_t)f->code + f->ncode > ncode || f->ncode == 0 || (uint64_t)f->kfirst + f->nconst > (uint64_t)nk ||
            (unsigned)f->nparams + f->nconst > f->nregs) {
            w = "bad function header";
            goto bad;
        }
        for (int j = 0; j < f->nparams; j++)
            if (f->ptype[j] > BMVM_ARR_F64) {
                w = "bad parameter type";
                goto bad;
            }
    }
    for (int i = 0; i < nk; i++, p += 8) {
        uint32_t lo = rd32(p), hi = rd32(p + 4);
        uint64_t bits = (uint64_t)hi << 32 | lo;
        memcpy(&m->k[i], &bits, 8);             /* a double, or an i32 in the low 32 bits (little endian) */
    }
    for (uint32_t i = 0; i < ncode; i++, p += 8) {
        bmvm_insn *c = &m->code[i];
        c->op = p[0]; c->a = p[1]; c->b = p[2]; c->c = p[3];
        c->k = (int32_t)rd32(p + 4);
    }
    /* the checks: every instruction of every function */
    for (int i = 0; i < nfn; i++) {
        const bmvm_fn *f = &m->fn[i];
        for (uint32_t j = f->code; j < f->code + f->ncode; j++) {
            const bmvm_insn *c = &m->code[j];
            if (c->op >= BMVM_OP_COUNT) {
                w = "unknown operation";
                goto bad;
            }
            const uint8_t u = op_use[c->op];
            if (((u & 1) && c->a >= f->nregs) || ((u & 2) && c->b >= f->nregs) || ((u & 4) && c->c >= f->nregs)) {
                w = "register out of range";
                goto bad;
            }
            if ((u & 8) && (c->k < 0 || c->k >= nk)) {
                w = "constant out of range";
                goto bad;
            }
            if ((u & 16) && (c->k < (int32_t)f->code || c->k >= (int32_t)(f->code + f->ncode))) {
                w = "jump out of the function";
                goto bad;
            }
            /* only JMP and FORI may go back (they use the budget); a conditional jump goes forward, so that a loop cannot run
             * without the budget */
            if ((u & 16) && c->op != BMVM_OP_JMP && c->op != BMVM_OP_FORI && c->k <= (int32_t)j) {
                w = "conditional jump backward";
                goto bad;
            }
            if ((u & 32) && (c->k < 0 || c->k >= nfn)) {
                w = "call of an unknown function";
                goto bad;
            }
            if (c->op == BMVM_OP_CALL && c->a + m->fn[c->k].nregs > 255) {
                w = "call frame too big";
                goto bad;
            }
        }
        const bmvm_insn *last = &m->code[f->code + f->ncode - 1];
        if (last->op != BMVM_OP_RET && last->op != BMVM_OP_RET0 && last->op != BMVM_OP_JMP) {
            w = "function does not end with a return";
            goto bad;
        }
    }
    *out = m;
    return 0;
bad:
    bmvm_free(m);
    if (why)
        *why = w;
    return -1;
}

int bmvm_find(const bmvm_mod *m, const char *name)
{
    for (int i = 0; i < m->nfn; i++)
        if (strcmp(m->fn[i].name, name) == 0)
            return i;
    return -1;
}

static int32_t idiv32(int32_t a, int32_t b)
{
    if (b == 0)
        return 0;
    if (b == -1)
        return (int32_t)(0u - (uint32_t)a);
    int32_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q--;
    return q;
}

static int32_t imod32(int32_t a, int32_t b)
{
    if (b == 0 || b == -1)
        return 0;
    int32_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0)))
        r += b;
    return r;
}

#ifdef BMVM_COUNT
uint64_t bmvm_steps;           /* instructions run, for the tests */
#endif

int bmvm_call(const bmvm_mod *m, int fn, const bmvm_val *args, int nargs, const bmvm_arr *arrs, int narrs, bmvm_val *ret,
              uint32_t budget)
{
    static bmvm_val stack[BMVM_STACK];
    struct { const bmvm_insn *pc; bmvm_val *base; } fr[BMVM_DEPTH];
    int depth = 0;

    if (fn < 0 || fn >= m->nfn)
        return -1;
    const bmvm_fn *f = &m->fn[fn];
    if (nargs != f->nparams)
        return -1;
    for (int i = 0; i < nargs; i++) {
        stack[i] = args[i];
        if (f->ptype[i] >= BMVM_ARR_I32) {
            if ((uint32_t)args[i].i >= (uint32_t)narrs || arrs[args[i].i].is_f64 != (f->ptype[i] == BMVM_ARR_F64))
                return -2;
        }
    }
    if (f->nconst)
        memcpy(stack + f->nparams, m->k + f->kfirst, (size_t)f->nconst * sizeof(bmvm_val));
    bmvm_val *R = stack;
    bmvm_val *const stack_end = stack + BMVM_STACK;
    const bmvm_insn *const code = m->code;
    const bmvm_insn *pc = code + f->code;
    const bmvm_insn *ins;

    static const void *const labels[BMVM_OP_COUNT] = {
#define X(n) &&L_##n,
        BMVM_OPS(X)
#undef X
    };
#ifdef BMVM_COUNT
#define NEXT() do { ins = pc++; bmvm_steps++; goto *labels[ins->op]; } while (0)
#else
#define NEXT() do { ins = pc++; goto *labels[ins->op]; } while (0)
#endif
#define RA R[ins->a]
#define RB R[ins->b]
#define RC R[ins->c]
    NEXT();

L_LK:   RA = m->k[ins->k]; NEXT();
L_MOV:  RA = RB; NEXT();
L_ADDI: RA.i = (int32_t)((uint32_t)RB.i + (uint32_t)RC.i); NEXT();
L_SUBI: RA.i = (int32_t)((uint32_t)RB.i - (uint32_t)RC.i); NEXT();
L_MULI: RA.i = (int32_t)((uint32_t)RB.i * (uint32_t)RC.i); NEXT();
L_IDIV: RA.i = idiv32(RB.i, RC.i); NEXT();
L_IMOD: RA.i = imod32(RB.i, RC.i); NEXT();
L_NEGI: RA.i = (int32_t)(0u - (uint32_t)RB.i); NEXT();
L_ANDI: RA.i = RB.i & RC.i; NEXT();
L_ADDF: RA.d = RB.d + RC.d; NEXT();
L_SUBF: RA.d = RB.d - RC.d; NEXT();
L_MULF: RA.d = RB.d * RC.d; NEXT();
L_DIVF: RA.d = RB.d / RC.d; NEXT();
L_FFDIV: RA.d = floor(RB.d / RC.d); NEXT();
L_FFMOD: RA.d = RB.d - floor(RB.d / RC.d) * RC.d; NEXT();
L_NEGF: RA.d = -RB.d; NEXT();
L_I2F:  RA.d = (double)RB.i; NEXT();
L_LTI:  RA.i = RB.i < RC.i; NEXT();
L_LEI:  RA.i = RB.i <= RC.i; NEXT();
L_EQI:  RA.i = RB.i == RC.i; NEXT();
L_NEI:  RA.i = RB.i != RC.i; NEXT();
L_LTF:  RA.i = RB.d < RC.d; NEXT();
L_LEF:  RA.i = RB.d <= RC.d; NEXT();
L_EQF:  RA.i = RB.d == RC.d; NEXT();
L_NEF:  RA.i = RB.d != RC.d; NEXT();
L_NOT:  RA.i = !RB.i; NEXT();
L_FLOOR: RA.i = (int32_t)floor(RB.d); NEXT();
L_SQRT: RA.d = sqrt(RB.d); NEXT();
L_ABSI: RA.i = RB.i < 0 ? (int32_t)(0u - (uint32_t)RB.i) : RB.i; NEXT();
L_ABSF: RA.d = fabs(RB.d); NEXT();
L_MINI: RA.i = RB.i < RC.i ? RB.i : RC.i; NEXT();
L_MAXI: RA.i = RB.i > RC.i ? RB.i : RC.i; NEXT();
L_MINF: RA.d = RB.d < RC.d ? RB.d : RC.d; NEXT();
L_MAXF: RA.d = RB.d > RC.d ? RB.d : RC.d; NEXT();
L_JMP:  if (--budget == 0) return -4; pc = code + ins->k; NEXT();
L_JF:   if (!RA.i) pc = code + ins->k; NEXT();
L_JT:   if (RA.i) pc = code + ins->k; NEXT();
L_JFLTI: if (!(RA.i < RB.i)) pc = code + ins->k; NEXT();
L_JFLEI: if (!(RA.i <= RB.i)) pc = code + ins->k; NEXT();
L_JFEQI: if (!(RA.i == RB.i)) pc = code + ins->k; NEXT();
L_JFNEI: if (!(RA.i != RB.i)) pc = code + ins->k; NEXT();
L_JFLTF: if (!(RA.d < RB.d)) pc = code + ins->k; NEXT();
L_JFLEF: if (!(RA.d <= RB.d)) pc = code + ins->k; NEXT();
L_JFEQF: if (!(RA.d == RB.d)) pc = code + ins->k; NEXT();
L_JFNEF: if (!(RA.d != RB.d)) pc = code + ins->k; NEXT();
L_FORI: RA.i++; if (RA.i <= RB.i) { if (--budget == 0) return -4; pc = code + ins->k; } NEXT();
L_GETI: {
    const uint32_t h = (uint32_t)RB.i;
    if (h >= (uint32_t)narrs || arrs[h].is_f64)
        return -2;
    const uint32_t ix = (uint32_t)RC.i - 1u;
    if (ix >= (uint32_t)arrs[h].n)
        return -2;
    RA.i = ((const int32_t *)arrs[h].data)[ix];
    NEXT();
}
L_GETF: {
    const uint32_t h = (uint32_t)RB.i;
    if (h >= (uint32_t)narrs || !arrs[h].is_f64)
        return -2;
    const uint32_t ix = (uint32_t)RC.i - 1u;
    if (ix >= (uint32_t)arrs[h].n)
        return -2;
    RA.d = ((const double *)arrs[h].data)[ix];
    NEXT();
}
L_SETI: {
    const uint32_t h = (uint32_t)RA.i;
    if (h >= (uint32_t)narrs || arrs[h].is_f64)
        return -2;
    const uint32_t ix = (uint32_t)RB.i - 1u;
    if (ix >= (uint32_t)arrs[h].n)
        return -2;
    ((int32_t *)arrs[h].data)[ix] = RC.i;
    NEXT();
}
L_SETF: {
    const uint32_t h = (uint32_t)RA.i;
    if (h >= (uint32_t)narrs || !arrs[h].is_f64)
        return -2;
    const uint32_t ix = (uint32_t)RB.i - 1u;
    if (ix >= (uint32_t)arrs[h].n)
        return -2;
    ((double *)arrs[h].data)[ix] = RC.d;
    NEXT();
}
L_CALL: {
    const bmvm_fn *cf = &m->fn[ins->k];
    bmvm_val *nb = R + ins->a;
    if (depth >= BMVM_DEPTH || nb + cf->nregs > stack_end || --budget == 0)
        return depth >= BMVM_DEPTH || nb + cf->nregs > stack_end ? -3 : -4;
    fr[depth].pc = pc;
    fr[depth].base = R;
    depth++;
    R = nb;
    if (cf->nconst)
        memcpy(R + cf->nparams, m->k + cf->kfirst, (size_t)cf->nconst * sizeof(bmvm_val));
    pc = code + cf->code;
    NEXT();
}
L_RET:
    if (depth == 0) {
        if (ret)
            *ret = RA;
        return 0;
    }
    R[0] = RA;
    depth--;
    pc = fr[depth].pc;
    R = fr[depth].base;
    NEXT();
L_RET0:
    if (depth == 0)
        return 0;
    depth--;
    pc = fr[depth].pc;
    R = fr[depth].base;
    NEXT();
}
