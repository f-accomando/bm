/* Native ARMv6 A32 loader and runner for the language of bm (Stages 5 and 6). */
#include "bmnative.h"

#include <stdlib.h>
#include <string.h>

#if defined(__arm__) && !defined(__linux__)
#include "arch/cache.h"
#endif

#if !defined(__arm__)
#include <math.h>
#endif

static bmnative_host_fn g_host_fn = NULL;

void bmnative_set_host_fn(bmnative_host_fn fn)
{
    g_host_fn = fn;
}

bmnative_host_fn bmnative_get_host_fn(void)
{
    return g_host_fn;
}

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void bmnative_sync_cache(const void *code, size_t bytes)
{
#if defined(__arm__) && !defined(__linux__)
    /* Bare-metal ARM (Pi Zero W ARM1176JZF-S):
     * 1. Clean D-cache lines by MVA over [code, code + bytes) and drain write buffer (DSB).
     * 2. Invalidate I-cache lines by MVA over [code, code + bytes).
     * 3. Flush Branch Target Cache (BTAC), then DSB + Prefetch Flush (ISB). */
    dcache_clean_range(code, (uint32_t)bytes);
    uint32_t a = (uint32_t)(uintptr_t)code & ~(CACHE_LINE - 1u);
    uint32_t end = (uint32_t)(uintptr_t)code + (uint32_t)bytes;
    for (; a < end; a += CACHE_LINE)
        __asm__ volatile("mcr p15, 0, %0, c7, c5, 1" : : "r"(a) : "memory");
    __asm__ volatile("mcr p15, 0, %0, c7, c5, 6" : : "r"(0) : "memory");
    arm_dsb();
    arm_isb();
#elif defined(__arm__) || defined(__aarch64__)
    __builtin___clear_cache((char *)code, (char *)code + bytes);
#else
    (void)code;
    (void)bytes;
#endif
}

void bmnative_free(bmnative_mod *m)
{
    if (!m)
        return;
    free(m->fn);
    free(m->structs);
    free(m->code_Alloc);
    free(m);
}

#define FAIL(msg) do { *why = (msg); bmnative_free(m); return BMN_ERR_BAD; } while (0)

static int check_armv6_words(const uint32_t *code, uint32_t ncode, const char **why)
{
    for (uint32_t i = 0; i < ncode; i++) {
        const uint32_t w = code[i];
        const uint32_t cond = (w >> 28) & 0xFu;
        if (cond == 0xFu) {
            *why = "unconditional extension instruction not allowed";
            return BMN_ERR_BAD;
        }
        /* Reject SVC/SWI (0b1111 in bits 27..24); allow only coprocessor 10 (VFPv2 single-precision). */
        const uint32_t top4 = (w >> 24) & 0xFu;
        if (top4 == 0xFu) {
            *why = "supervisor call instruction not allowed";
            return BMN_ERR_BAD;
        }
        if (top4 >= 0xCu) {
            const uint32_t cp = (w >> 8) & 0xFu;
            if (top4 != 0xEu || cp != 10u) {
                *why = "coprocessor instruction not allowed";
                return BMN_ERR_BAD;
            }
        }
        /* Reject BKPT (0xE1200070 mask). */
        if ((w & 0x0FF000F0u) == 0x01200070u) {
            *why = "breakpoint instruction not allowed";
            return BMN_ERR_BAD;
        }
        /* Check relative branch B / BL targets stay within [0, ncode). */
        if (((w >> 25) & 7u) == 5u) {
            int32_t imm24 = (int32_t)(w & 0xFFFFFFu);
            if (imm24 & 0x800000)
                imm24 -= 0x1000000;
            int64_t target = (int64_t)i + 2 + imm24;
            if (target < 0 || target >= (int64_t)ncode) {
                *why = "branch target out of module range";
                return BMN_ERR_BAD;
            }
        }
    }
    return BMN_OK;
}

static int valid_ptype(uint8_t pt, uint32_t nstruct)
{
    if (pt >= BMN_I32 && pt <= BMN_ARR_F32)
        return 1;
    if ((pt & BMN_STRUCT_BASE) && (uint32_t)(pt & 0x7Fu) < nstruct)
        return 1;
    return 0;
}

int bmnative_load(bmnative_mod **out, const uint8_t *blob, size_t len, const char **why)
{
    *out = NULL;
    *why = "";
    if (!blob || len < 4) {
        *why = "truncated native blob";
        return BMN_ERR_BAD;
    }

    bmnative_mod *m = calloc(1, sizeof *m);
    if (!m) {
        *why = "out of memory";
        return BMN_ERR_OOM;
    }

    const uint32_t first_word = rd_u32(blob);
    const uint8_t *code_ptr = NULL;

    if (first_word == BMN_MAGIC) {
        if (len < 12)
            FAIL("truncated BMN1 header");
        const uint32_t nfn_packed = rd_u32(blob + 4);
        const uint32_t nfn = nfn_packed & 0xFFFFu;
        const uint32_t nstruct_raw = (nfn_packed >> 16) & 0xFFFFu;
        const int has_host_stub = (int)((nstruct_raw >> 15) & 1u);
        const uint32_t nstruct = nstruct_raw & 0x7FFFu;
        const uint32_t ncode = rd_u32(blob + 8);
        if (nfn < 1 || nfn > BMN_MAX_FN)
            FAIL("bad function count");
        if (nstruct > BMN_MAX_STRUCT)
            FAIL("bad struct count");
        if (ncode < 1 || ncode > BMN_MAX_CODE || (has_host_stub && ncode < 6u))
            FAIL("bad code word count");
        const size_t expected_len = 12u + (size_t)nfn * 56u + (size_t)ncode * 4u + (size_t)nstruct * 308u;
        if (len != expected_len)
            FAIL("BMN1 blob size mismatch");

        m->nfn = (int)nfn;
        m->nstruct = (int)nstruct;
        m->has_host_stub = has_host_stub;
        m->ncode = ncode;
        m->fn = calloc(nfn, sizeof *m->fn);
        if (!m->fn) {
            bmnative_free(m);
            *why = "out of memory";
            return BMN_ERR_OOM;
        }
        if (nstruct > 0) {
            m->structs = calloc(nstruct, sizeof *m->structs);
            if (!m->structs) {
                bmnative_free(m);
                *why = "out of memory";
                return BMN_ERR_OOM;
            }
        }

        const uint8_t *p = blob + 12;
        for (uint32_t i = 0; i < nfn; i++, p += 56) {
            bmnative_fn *f = &m->fn[i];
            memcpy(f->name, p, 31);
            f->name[31] = '\0';
            f->nparams = p[32];
            f->ret = p[33];
            if (f->nparams > BMN_MAX_ARGS)
                FAIL("too many parameters");
            if (f->ret > BMN_F32)
                FAIL("invalid return type");
            memcpy(f->ptype, p + 36, BMN_MAX_ARGS);
            for (int j = 0; j < f->nparams; j++) {
                if (!valid_ptype(f->ptype[j], nstruct))
                    FAIL("invalid parameter type");
            }
            f->word_off = rd_u32(p + 52);
            if (f->word_off >= ncode)
                FAIL("function entry offset out of range");
        }
        code_ptr = p;
        const uint8_t *sp_ptr = code_ptr + (size_t)ncode * 4u;
        for (uint32_t i = 0; i < nstruct; i++, sp_ptr += 308) {
            bmnative_struct *st = &m->structs[i];
            memcpy(st->name, sp_ptr, 31);
            st->name[31] = '\0';
            st->nfields = rd_u32(sp_ptr + 32);
            if (st->nfields < 1 || st->nfields > BMN_MAX_FIELDS)
                FAIL("invalid struct field count");
            memcpy(st->ftype, sp_ptr + 36, BMN_MAX_FIELDS);
            for (uint32_t k = 0; k < st->nfields; k++) {
                if (st->ftype[k] < BMN_I32 || st->ftype[k] > BMN_F32)
                    FAIL("invalid struct field type");
                memcpy(st->fname[k], sp_ptr + 52 + k * 16u, 15);
                st->fname[k][15] = '\0';
            }
        }
    } else {
        /* Raw 32-bit ARMv6 A32 word blob (e.g. tools/bml/bml.py --armv6). */
        if ((len & 3u) != 0)
            FAIL("raw ARMv6 blob length must be a multiple of 4 bytes");
        const uint32_t ncode = (uint32_t)(len / 4u);
        if (ncode < 1 || ncode > BMN_MAX_CODE)
            FAIL("bad raw ARMv6 word count");
        if (((first_word >> 28) & 0xFu) != 0xEu)
            FAIL("raw ARMv6 blob does not start with an AL-condition instruction");

        m->nfn = 1;
        m->ncode = ncode;
        m->fn = calloc(1, sizeof *m->fn);
        if (!m->fn) {
            bmnative_free(m);
            *why = "out of memory";
            return BMN_ERR_OOM;
        }
        memcpy(m->fn[0].name, "main", 5);
        m->fn[0].nparams = BMN_VARARGS;
        m->fn[0].ret = BMN_I32;
        m->fn[0].word_off = 0;
        code_ptr = blob;
    }

    /* Allocate 32-byte cache-line-aligned executable RAM for the A32 words. */
    const size_t code_bytes = (size_t)m->ncode * 4u;
    m->code_Alloc = calloc(1, code_bytes + 64u);
    if (!m->code_Alloc) {
        bmnative_free(m);
        *why = "out of memory";
        return BMN_ERR_OOM;
    }
    uintptr_t aligned = ((uintptr_t)m->code_Alloc + 31u) & ~(uintptr_t)31u;
    m->code = (uint32_t *)aligned;
    for (uint32_t i = 0; i < m->ncode; i++)
        m->code[i] = rd_u32(code_ptr + i * 4u);

    if (check_armv6_words(m->code, m->ncode, why) != BMN_OK) {
        bmnative_free(m);
        return BMN_ERR_BAD;
    }

#if defined(__arm__)
    if (m->has_host_stub && g_host_fn)
        m->code[m->ncode - 1u] = (uint32_t)(uintptr_t)g_host_fn;
#endif

    /* Clean D-cache and invalidate I-cache + branch target cache before any call. */
    bmnative_sync_cache(m->code, code_bytes);

    *out = m;
    return BMN_OK;
}

int bmnative_find(const bmnative_mod *m, const char *name)
{
    if (!m || !name)
        return -1;
    for (int i = 0; i < m->nfn; i++) {
        if (strcmp(m->fn[i].name, name) == 0)
            return i;
    }
    if (m->nfn == 1 && m->fn[0].nparams == BMN_VARARGS)
        return 0;
    return -1;
}

#if defined(__arm__)
typedef int32_t (*bmnative_cfunc_t)(
    int32_t, int32_t, int32_t, int32_t,
    int32_t, int32_t, int32_t, int32_t,
    int32_t, int32_t, int32_t, int32_t,
    int32_t, int32_t, int32_t, int32_t);
#else
static int eval_cond(uint32_t cond, int n, int z, int c, int v)
{
    switch (cond) {
    case 0x0: return z;                 /* EQ */
    case 0x1: return !z;                /* NE */
    case 0x2: return c;                 /* CS / HS */
    case 0x3: return !c;                /* CC / LO */
    case 0x4: return n;                 /* MI */
    case 0x5: return !n;                /* PL */
    case 0x6: return v;                 /* VS */
    case 0x7: return !v;                /* VC */
    case 0x8: return c && !z;           /* HI */
    case 0x9: return !c || z;           /* LS */
    case 0xA: return n == v;            /* GE */
    case 0xB: return n != v;            /* LT */
    case 0xC: return !z && (n == v);    /* GT */
    case 0xD: return z || (n != v);     /* LE */
    case 0xE: return 1;                 /* AL */
    default:  return 0;
    }
}

static float u32_to_f32(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static uint32_t f32_to_u32(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

/* Executes the ARMv6 A32 machine words in m->code on non-ARM hosts (e.g. x86_64 bmhost-bin). */
static int sim_armv6_call(const bmnative_mod *m, uint32_t entry_word, const int32_t *a, const bmnative_buf *bufs, int32_t *ret)
{
    enum { STACK_WORDS = 8192 };
    uint32_t stack[STACK_WORDS];
    memset(stack, 0, sizeof stack);

    uint32_t r[16];
    memset(r, 0, sizeof r);
    r[0] = (uint32_t)a[0];
    r[1] = (uint32_t)a[1];
    r[2] = (uint32_t)a[2];
    r[3] = (uint32_t)a[3];

    float s[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    int vfp_n = 0, vfp_z = 0, vfp_c = 0, vfp_v = 0;

    /* Place stack arguments a[4..15] at the top of the virtual stack. */
    uint32_t sp_base = (STACK_WORDS - 32) * 4u;
    for (int i = 4; i < BMN_MAX_ARGS; i++)
        stack[(sp_base / 4u) + (uint32_t)(i - 4)] = (uint32_t)a[i];

    const uint32_t RET_SENTINEL = 0xFFFFFFFCu;
    r[13] = sp_base;
    r[14] = RET_SENTINEL;
    r[15] = entry_word * 4u;

    int flag_n = 0, flag_z = 0, flag_c = 0, flag_v = 0;
    uint32_t budget = BMN_BUDGET;

    while (r[15] != RET_SENTINEL) {
        if (budget == 0)
            return BMN_ERR_BUDGET;
        budget--;

        const uint32_t idx = r[15] >> 2;
        if ((r[15] & 3u) != 0 || idx >= m->ncode)
            return BMN_ERR_TRAP;

        /* Stage 7 host function call stub (__bml_call_host at word ncode - 6):
         * r0 = host_id, r1 = stack byte offset of args[0..nargs-1], r2 = nargs */
        if (m->has_host_stub && idx == m->ncode - 6u) {
            int32_t h_args[BMN_MAX_ARGS];
            const int32_t h_nargs = (int32_t)r[2];
            if (h_nargs < 0 || h_nargs > BMN_MAX_ARGS || (r[1] & 3u) != 0)
                return BMN_ERR_TRAP;
            for (int32_t j = 0; j < h_nargs; j++) {
                const uint32_t sw = (r[1] >> 2) + (uint32_t)j;
                if (sw >= STACK_WORDS)
                    return BMN_ERR_TRAP;
                h_args[j] = (int32_t)stack[sw];
            }
            r[0] = g_host_fn ? (uint32_t)g_host_fn((int32_t)r[0], h_args, h_nargs) : 0u;
            r[15] = r[14];
            continue;
        }

        const uint32_t w = m->code[idx];
        const uint32_t curr_pc = r[15];
        r[15] = curr_pc + 4u;

        const uint32_t cond = (w >> 28) & 0xFu;
        if (!eval_cond(cond, flag_n, flag_z, flag_c, flag_v))
            continue;

        /* 1. BX Rm (0x012FFF10) */
        if ((w & 0x0FFFFFF0u) == 0x012FFF10u) {
            r[15] = r[w & 0xFu];
            continue;
        }

        /* 2. B / BL (bits 27..25 == 0b101) */
        if (((w >> 25) & 7u) == 5u) {
            const int is_bl = (int)((w >> 24) & 1u);
            int32_t imm24 = (int32_t)(w & 0xFFFFFFu);
            if (imm24 & 0x800000)
                imm24 -= 0x1000000;
            if (is_bl)
                r[14] = curr_pc + 4u;
            r[15] = (uint32_t)((int32_t)(curr_pc + 8u) + (imm24 * 4));
            continue;
        }

        /* 3. PUSH {reglist} (STMDB sp!, {reglist}: 0x092D0000) */
        if ((w & 0x0FFF0000u) == 0x092D0000u) {
            const uint32_t mask = w & 0xFFFFu;
            for (int reg = 15; reg >= 0; reg--) {
                if (mask & (1u << reg)) {
                    r[13] -= 4u;
                    if ((r[13] & 3u) != 0 || (r[13] >> 2) >= STACK_WORDS)
                        return BMN_ERR_TRAP;
                    stack[r[13] >> 2] = r[reg];
                }
            }
            continue;
        }

        /* 4. POP {reglist} (LDMIA sp!, {reglist}: 0x08BD0000) */
        if ((w & 0x0FFF0000u) == 0x08BD0000u) {
            const uint32_t mask = w & 0xFFFFu;
            for (int reg = 0; reg <= 15; reg++) {
                if (mask & (1u << reg)) {
                    if ((r[13] & 3u) != 0 || (r[13] >> 2) >= STACK_WORDS)
                        return BMN_ERR_TRAP;
                    r[reg] = stack[r[13] >> 2];
                    r[13] += 4u;
                }
            }
            continue;
        }

        /* 5. Single Data Transfer: LDR / STR Rd, [Rn, #+/-imm12] (bits 27..24 == 0b0101) */
        if (((w >> 24) & 0xFu) == 0x5u && ((w >> 21) & 3u) == 0u) {
            const int is_ldr = (int)((w >> 20) & 1u);
            const int u_bit  = (int)((w >> 23) & 1u);
            const uint32_t rn = (w >> 16) & 0xFu;
            const uint32_t rd = (w >> 12) & 0xFu;
            const uint32_t imm12 = w & 0xFFFu;
            const uint32_t base = (rn == 15u) ? (curr_pc + 8u) : r[rn];
            const uint32_t addr = u_bit ? (base + imm12) : (base - imm12);
            if ((addr & 3u) != 0)
                return BMN_ERR_TRAP;
            const uint32_t seg = addr >> 24;
            if (seg == 0u) {
                if ((addr >> 2) >= STACK_WORDS)
                    return BMN_ERR_TRAP;
                if (is_ldr)
                    r[rd] = stack[addr >> 2];
                else
                    stack[addr >> 2] = r[rd];
            } else if (seg >= 1u && seg <= BMN_MAX_ARGS && bufs && bufs[seg - 1u].data) {
                const uint32_t w_off = (addr & 0x00FFFFFFu) >> 2;
                if (w_off >= bufs[seg - 1u].nwords)
                    return BMN_ERR_TRAP;
                if (is_ldr)
                    r[rd] = bufs[seg - 1u].data[w_off];
                else
                    bufs[seg - 1u].data[w_off] = r[rd];
            } else {
                return BMN_ERR_TRAP;
            }
            continue;
        }

        /* 5b. VFPv2 single-precision coprocessor 10 instructions (top4 == 0xE, cp == 10) */
        if (((w >> 24) & 0xFu) == 0xEu && ((w >> 8) & 0xFu) == 10u) {
            const uint32_t op_mask = w & 0x0FFFFFFFu;
            const uint32_t rt_mask = w & 0x0FFF0FFFu;
            const uint32_t rt = (w >> 12) & 0xFu;
            if (rt_mask == 0x0E000A10u) {
                s[0] = u32_to_f32(r[rt]);
                continue;
            }
            if (rt_mask == 0x0E000A90u) {
                s[1] = u32_to_f32(r[rt]);
                continue;
            }
            if (rt_mask == 0x0E100A10u) {
                r[rt] = f32_to_u32(s[0]);
                continue;
            }
            if (rt_mask == 0x0E110A10u) {
                r[rt] = f32_to_u32(s[2]);
                continue;
            }
            if (op_mask == 0x0E300A20u) { s[0] = s[0] + s[1]; continue; }
            if (op_mask == 0x0E300A60u) { s[0] = s[0] - s[1]; continue; }
            if (op_mask == 0x0E200A20u) { s[0] = s[0] * s[1]; continue; }
            if (op_mask == 0x0E800A20u) { s[0] = (s[1] != 0.0f) ? (s[0] / s[1]) : 0.0f; continue; }
            if (op_mask == 0x0EB10A40u) { s[0] = -s[0]; continue; }
            if (op_mask == 0x0EB00AC0u) { s[0] = fabsf(s[0]); continue; }
            if (op_mask == 0x0EB10AC0u) { s[0] = sqrtf(s[0]); continue; }
            if (op_mask == 0x0EB80AC0u) { s[0] = (float)(int32_t)f32_to_u32(s[0]); continue; }
            if (op_mask == 0x0EBD1AC0u) { s[2] = u32_to_f32((uint32_t)(int32_t)s[0]); continue; }
            if (op_mask == 0x0EB81AC1u) { s[2] = (float)(int32_t)f32_to_u32(s[2]); continue; }
            if (op_mask == 0x0EB40A60u) {
                vfp_n = (s[0] < s[1]);
                vfp_z = (s[0] == s[1]);
                vfp_c = (s[0] >= s[1]);
                vfp_v = 0;
                continue;
            }
            if (op_mask == 0x0EF1FA10u) {
                flag_n = vfp_n;
                flag_z = vfp_z;
                flag_c = vfp_c;
                flag_v = vfp_v;
                continue;
            }
            return BMN_ERR_TRAP;
        }

        /* 6. MUL Rd, Rm, Rs (bits 27..21 == 0, bits 7..4 == 0b1001) */
        if ((w & 0x0FE000F0u) == 0x00000090u) {
            const uint32_t rd = (w >> 16) & 0xFu;
            const uint32_t rs = (w >> 8) & 0xFu;
            const uint32_t rm = w & 0xFu;
            const int s_bit   = (int)((w >> 20) & 1u);
            const uint32_t res = r[rm] * r[rs];
            r[rd] = res;
            if (s_bit) {
                flag_z = (res == 0);
                flag_n = (int)((res >> 31) & 1u);
            }
            continue;
        }

        /* 7. Data Processing (bits 27..26 == 0b00) */
        if (((w >> 26) & 3u) == 0u) {
            const int is_imm = (int)((w >> 25) & 1u);
            const uint32_t opcode = (w >> 21) & 0xFu;
            const int s_bit = (int)((w >> 20) & 1u);
            const uint32_t rn = (w >> 16) & 0xFu;
            const uint32_t rd = (w >> 12) & 0xFu;
            const uint32_t op1 = (rn == 15u) ? (curr_pc + 8u) : r[rn];
            uint32_t op2 = 0;
            int shift_c = flag_c;

            if (is_imm) {
                const uint32_t imm8 = w & 0xFFu;
                const uint32_t rot = ((w >> 8) & 0xFu) * 2u;
                if (rot == 0) {
                    op2 = imm8;
                } else {
                    op2 = (imm8 >> rot) | (imm8 << (32u - rot));
                    shift_c = (int)((op2 >> 31) & 1u);
                }
            } else {
                const uint32_t rm = w & 0xFu;
                const uint32_t val_m = (rm == 15u) ? (curr_pc + 8u) : r[rm];
                const uint32_t shift_type = (w >> 5) & 3u;
                const uint32_t shift_imm = (w >> 7) & 0x1Fu;
                if (shift_imm == 0) {
                    op2 = val_m;
                } else if (shift_type == 0u) { /* LSL */
                    shift_c = (int)((val_m >> (32u - shift_imm)) & 1u);
                    op2 = val_m << shift_imm;
                } else if (shift_type == 1u) { /* LSR */
                    shift_c = (int)((val_m >> (shift_imm - 1u)) & 1u);
                    op2 = val_m >> shift_imm;
                } else if (shift_type == 2u) { /* ASR */
                    shift_c = (int)((((int32_t)val_m) >> (shift_imm - 1u)) & 1);
                    op2 = (uint32_t)(((int32_t)val_m) >> shift_imm);
                } else {                       /* ROR */
                    op2 = (val_m >> shift_imm) | (val_m << (32u - shift_imm));
                    shift_c = (int)((op2 >> 31) & 1u);
                }
            }

            uint32_t res = 0;
            int write_rd = 1;
            int arith_c = shift_c;
            int arith_v = flag_v;

            switch (opcode) {
            case 0x0: /* AND */
                res = op1 & op2;
                break;
            case 0x1: /* EOR */
                res = op1 ^ op2;
                break;
            case 0x2: /* SUB */
            case 0xA: /* CMP */
                res = op1 - op2;
                arith_c = (op1 >= op2);
                arith_v = (((op1 ^ op2) & (op1 ^ res)) >> 31) & 1;
                if (opcode == 0xAu)
                    write_rd = 0;
                break;
            case 0x3: /* RSB */
                res = op2 - op1;
                arith_c = (op2 >= op1);
                arith_v = (((op2 ^ op1) & (op2 ^ res)) >> 31) & 1;
                break;
            case 0x4: /* ADD */
            case 0xB: /* CMN */
                res = op1 + op2;
                arith_c = (res < op1);
                arith_v = ((~(op1 ^ op2) & (op1 ^ res)) >> 31) & 1;
                if (opcode == 0xBu)
                    write_rd = 0;
                break;
            case 0x8: /* TST */
                res = op1 & op2;
                write_rd = 0;
                break;
            case 0x9: /* TEQ */
                res = op1 ^ op2;
                write_rd = 0;
                break;
            case 0xC: /* ORR */
                res = op1 | op2;
                break;
            case 0xD: /* MOV */
                res = op2;
                break;
            case 0xE: /* BIC */
                res = op1 & ~op2;
                break;
            case 0xF: /* MVN */
                res = ~op2;
                break;
            default:
                return BMN_ERR_TRAP;
            }

            if (write_rd)
                r[rd] = res;
            if (s_bit) {
                flag_z = (res == 0);
                flag_n = (int)((res >> 31) & 1u);
                flag_c = arith_c;
                flag_v = arith_v;
            }
            continue;
        }

        return BMN_ERR_TRAP;
    }

    *ret = (int32_t)r[0];
    return BMN_OK;
}
#endif

int bmnative_call_buf(const bmnative_mod *m, int fn, const int32_t *args, const bmnative_buf *bufs, int nargs, int32_t *ret)
{
    if (!m || fn < 0 || fn >= m->nfn || !ret)
        return BMN_ERR_ARGS;
    const bmnative_fn *f = &m->fn[fn];
    if (f->nparams == BMN_VARARGS) {
        if (nargs < 0 || nargs > BMN_MAX_ARGS)
            return BMN_ERR_ARGS;
    } else if (nargs != f->nparams) {
        return BMN_ERR_ARGS;
    }

    int32_t a[BMN_MAX_ARGS];
    memset(a, 0, sizeof a);
    for (int i = 0; i < nargs; i++) {
        if (bufs && bufs[i].data) {
#if defined(__arm__)
            a[i] = (int32_t)(uintptr_t)bufs[i].data;
#else
            a[i] = (int32_t)((uint32_t)(i + 1) << 24);
#endif
        } else {
            a[i] = args ? args[i] : 0;
        }
    }

#if defined(__arm__)
    if (m->has_host_stub) {
        uint32_t want_fn = (uint32_t)(uintptr_t)g_host_fn;
        if (m->code[m->ncode - 1u] != want_fn) {
            m->code[m->ncode - 1u] = want_fn;
            bmnative_sync_cache(&m->code[m->ncode - 1u], 4u);
        }
    }
    bmnative_cfunc_t fp = (bmnative_cfunc_t)(uintptr_t)(&m->code[f->word_off]);
    *ret = fp(
        a[0],  a[1],  a[2],  a[3],
        a[4],  a[5],  a[6],  a[7],
        a[8],  a[9],  a[10], a[11],
        a[12], a[13], a[14], a[15]);
    return BMN_OK;
#else
    return sim_armv6_call(m, f->word_off, a, bufs, ret);
#endif
}

int bmnative_call(const bmnative_mod *m, int fn, const int32_t *args, int nargs, int32_t *ret)
{
    return bmnative_call_buf(m, fn, args, NULL, nargs, ret);
}
