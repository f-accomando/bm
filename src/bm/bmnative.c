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
static const bmnative_mod *g_active_mod = NULL;

const char *bmnative_get_string(int32_t arg0)
{
    if (g_active_mod && g_active_mod->rodata) {
        uint32_t u = (uint32_t)arg0;
        if (u >= g_active_mod->ncode * 4u && u < g_active_mod->ncode * 4u + g_active_mod->nrodata)
            return (const char *)(g_active_mod->rodata + (u - g_active_mod->ncode * 4u));
    }
    return (const char *)(uintptr_t)arg0;
}

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
            if ((top4 != 0xCu && top4 != 0xDu && top4 != 0xEu) || cp != 10u) {
                *why = "coprocessor instruction not allowed";
                return BMN_ERR_BAD;
            }
            if ((top4 == 0xCu || top4 == 0xDu) && ((w >> 20) & 1u) == 0u && (((w >> 16) & 0xFu) == 15u)) {
                *why = "coprocessor store relative to pc not allowed";
                return BMN_ERR_BAD;
            }
        }
        /* Reject BKPT (0xE1200070 mask). */
        if ((w & 0x0FF000F0u) == 0x01200070u) {
            *why = "breakpoint instruction not allowed";
            return BMN_ERR_BAD;
        }
        /* Validate ARM Block Data Transfer: LDM / STM (bits 27..25 == 0b100). */
        if (((w >> 25) & 7u) == 4u) {
            const uint32_t rn = (w >> 16) & 0xFu;
            const uint32_t reglist = w & 0xFFFFu;
            const uint32_t s_bit = (w >> 22) & 1u;
            if (s_bit != 0u || reglist == 0u || rn == 15u) {
                *why = "block transfer not allowed";
                return BMN_ERR_BAD;
            }
            if ((w & 0x0FFF0000u) != 0x08BD0000u && (reglist & (1u << 15)) != 0u) {
                *why = "block transfer to pc not allowed";
                return BMN_ERR_BAD;
            }
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
    if ((pt >= BMN_I32 && pt <= BMN_ARR_F32) || pt == BMN_STR)
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
    const uint8_t *rodata_ptr = NULL;
    uint32_t rodata_len = 0;

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

        uint32_t nrodata = 0;
        size_t hdr_size = 12u;
        size_t expected_len_no_rodata = 12u + (size_t)nfn * 56u + (size_t)ncode * 4u + (size_t)nstruct * 308u;
        if (len > expected_len_no_rodata && len >= 16u) {
            nrodata = rd_u32(blob + 12);
            hdr_size = 16u;
        }
        const size_t expected_len = hdr_size + (size_t)nfn * 56u + (size_t)ncode * 4u + (size_t)nstruct * 308u + (size_t)nrodata;
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

        const uint8_t *p = blob + hdr_size;
        for (uint32_t i = 0; i < nfn; i++, p += 56) {
            bmnative_fn *f = &m->fn[i];
            memcpy(f->name, p, 31);
            f->name[31] = '\0';
            f->nparams = p[32];
            f->ret = p[33];
            if (f->nparams > BMN_MAX_ARGS)
                FAIL("too many parameters");
            if (f->ret > BMN_STR)
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
        rodata_ptr = (nrodata > 0) ? sp_ptr : NULL;
        rodata_len = nrodata;
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

    /* Allocate 32-byte cache-line-aligned executable RAM for the A32 words and .rodata segment. */
    const size_t code_bytes = (size_t)m->ncode * 4u;
    const size_t total_alloc_bytes = code_bytes + (size_t)rodata_len + 64u;
    m->code_Alloc = calloc(1, total_alloc_bytes);
    if (!m->code_Alloc) {
        bmnative_free(m);
        *why = "out of memory";
        return BMN_ERR_OOM;
    }
    uintptr_t aligned = ((uintptr_t)m->code_Alloc + 31u) & ~(uintptr_t)31u;
    m->code = (uint32_t *)aligned;
    for (uint32_t i = 0; i < m->ncode; i++)
        m->code[i] = rd_u32(code_ptr + i * 4u);
    if (rodata_len > 0 && rodata_ptr) {
        m->nrodata = rodata_len;
        m->rodata = (uint8_t *)(m->code + m->ncode);
        memcpy(m->rodata, rodata_ptr, rodata_len);
    }

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

static uint8_t g_sim_ram[65536];
static uint32_t g_last_insns = 0;

uint32_t bmnative_last_insns(void)
{
    return g_last_insns;
}

static int sim_mem_access(
    const bmnative_mod *m,
    const bmnative_buf *bufs,
    uint32_t *stack,
    uint32_t curr_pc,
    uint32_t rn,
    uint32_t addr,
    int is_ldr,
    int is_byte,
    uint32_t *val_io)
{
    if (!is_byte && (addr & 3u) != 0)
        return BMN_ERR_TRAP;
    if (rn == 15u) {
        if (is_ldr && !is_byte && (addr >> 2) < m->ncode) {
            *val_io = m->code[addr >> 2];
            return BMN_OK;
        }
        return BMN_ERR_TRAP;
    }
    (void)curr_pc;
    const uint32_t seg = addr >> 24;
    if (m->rodata && addr >= m->ncode * 4u && addr < m->ncode * 4u + m->nrodata) {
        const uint32_t ro_off = addr - m->ncode * 4u;
        if (is_byte) {
            if (!is_ldr) return BMN_ERR_TRAP;
            *val_io = m->rodata[ro_off];
        } else {
            if (!is_ldr || ro_off + 4u > m->nrodata) return BMN_ERR_TRAP;
            uint32_t val;
            memcpy(&val, m->rodata + ro_off, 4);
            *val_io = val;
        }
        return BMN_OK;
    }
    if (rn == 11u || rn == 13u || (seg == 0u && addr >= 0x4000u)) {
        if ((addr >> 2) >= 8192u)
            return BMN_ERR_TRAP;
        if (is_byte) {
            uint8_t *bstack = (uint8_t *)stack;
            if (is_ldr) *val_io = bstack[addr];
            else bstack[addr] = (uint8_t)(*val_io);
        } else {
            if (is_ldr) *val_io = stack[addr >> 2];
            else stack[addr >> 2] = *val_io;
        }
        return BMN_OK;
    }
    if (seg >= 1u && seg <= BMN_MAX_ARGS && bufs && bufs[seg - 1u].data) {
        const uint32_t byte_off = addr & 0x00FFFFFFu;
        if (is_byte) {
            if (byte_off >= bufs[seg - 1u].nwords * 4u)
                return BMN_ERR_TRAP;
            uint8_t *bdata = (uint8_t *)bufs[seg - 1u].data;
            if (is_ldr) *val_io = bdata[byte_off];
            else bdata[byte_off] = (uint8_t)(*val_io);
        } else {
            const uint32_t w_off = byte_off >> 2;
            if (w_off >= bufs[seg - 1u].nwords)
                return BMN_ERR_TRAP;
            if (is_ldr) *val_io = bufs[seg - 1u].data[w_off];
            else bufs[seg - 1u].data[w_off] = *val_io;
        }
        return BMN_OK;
    }
    const uint32_t m_off = addr & 0xFFFFu;
    if (is_byte) {
        if (is_ldr) *val_io = g_sim_ram[m_off];
        else g_sim_ram[m_off] = (uint8_t)(*val_io);
    } else if (is_ldr) {
        *val_io = (uint32_t)g_sim_ram[m_off] |
                  ((uint32_t)g_sim_ram[(m_off + 1u) & 0xFFFFu] << 8) |
                  ((uint32_t)g_sim_ram[(m_off + 2u) & 0xFFFFu] << 16) |
                  ((uint32_t)g_sim_ram[(m_off + 3u) & 0xFFFFu] << 24);
    } else {
        g_sim_ram[m_off] = (uint8_t)(*val_io & 0xFFu);
        g_sim_ram[(m_off + 1u) & 0xFFFFu] = (uint8_t)((*val_io >> 8) & 0xFFu);
        g_sim_ram[(m_off + 2u) & 0xFFFFu] = (uint8_t)((*val_io >> 16) & 0xFFu);
        g_sim_ram[(m_off + 3u) & 0xFFFFu] = (uint8_t)((*val_io >> 24) & 0xFFu);
    }
    return BMN_OK;
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

    float s[32];
    memset(s, 0, sizeof s);
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

        /* 4b. Generic LDM / STM (bits 27..25 == 0b100) */
        if (((w >> 25) & 7u) == 4u) {
            const uint32_t p_bit = (w >> 24) & 1u;
            const uint32_t u_bit = (w >> 23) & 1u;
            const uint32_t s_bit = (w >> 22) & 1u;
            const uint32_t w_bit = (w >> 21) & 1u;
            const uint32_t l_bit = (w >> 20) & 1u;
            const uint32_t rn    = (w >> 16) & 0xFu;
            const uint32_t mask  = w & 0xFFFFu;
            if (s_bit != 0u || rn == 15u || mask == 0u)
                return BMN_ERR_TRAP;
            uint32_t cnt = 0;
            for (int reg = 0; reg <= 15; reg++) {
                if (mask & (1u << reg))
                    cnt++;
            }
            uint32_t start_addr = 0, wb_addr = 0;
            if (u_bit && !p_bit) {
                start_addr = r[rn];
                wb_addr = r[rn] + cnt * 4u;
            } else if (u_bit && p_bit) {
                start_addr = r[rn] + 4u;
                wb_addr = r[rn] + cnt * 4u;
            } else if (!u_bit && p_bit) {
                start_addr = r[rn] - cnt * 4u;
                wb_addr = start_addr;
            } else {
                start_addr = r[rn] - cnt * 4u + 4u;
                wb_addr = r[rn] - cnt * 4u;
            }
            uint32_t ea = start_addr;
            for (int reg = 0; reg <= 15; reg++) {
                if (mask & (1u << reg)) {
                    uint32_t val = l_bit ? 0u : r[reg];
                    if (sim_mem_access(m, bufs, stack, curr_pc, rn, ea, (int)l_bit, 0, &val) != BMN_OK)
                        return BMN_ERR_TRAP;
                    if (l_bit)
                        r[reg] = val;
                    ea += 4u;
                }
            }
            if (w_bit && !(l_bit && (mask & (1u << rn))))
                r[rn] = wb_addr;
            continue;
        }

        /* 5. Single Data Transfer: LDR / STR / LDRB / STRB (imm12 or shifted register offset, P=1, W=0) */
        if (((w >> 26) & 3u) == 1u && ((w >> 24) & 1u) == 1u && ((w >> 21) & 1u) == 0u) {
            const int is_reg  = (int)((w >> 25) & 1u);
            if (!is_reg || ((w & 0x10u) == 0u)) {
                const int is_ldr  = (int)((w >> 20) & 1u);
                const int is_byte = (int)((w >> 22) & 1u);
                const int u_bit   = (int)((w >> 23) & 1u);
                const uint32_t rn = (w >> 16) & 0xFu;
                const uint32_t rd = (w >> 12) & 0xFu;
                uint32_t off = 0;
                if (!is_reg) {
                    off = w & 0xFFFu;
                } else {
                    const uint32_t rm = w & 0xFu;
                    const uint32_t val_m = (rm == 15u) ? (curr_pc + 8u) : r[rm];
                    const uint32_t shift_type = (w >> 5) & 3u;
                    const uint32_t shift_imm = (w >> 7) & 0x1Fu;
                    if (shift_imm == 0u) {
                        off = val_m;
                    } else if (shift_type == 0u) {
                        off = val_m << shift_imm;
                    } else if (shift_type == 1u) {
                        off = val_m >> shift_imm;
                    } else if (shift_type == 2u) {
                        off = (uint32_t)(((int32_t)val_m) >> shift_imm);
                    } else {
                        off = (val_m >> shift_imm) | (val_m << (32u - shift_imm));
                    }
                }
                const uint32_t base = (rn == 15u) ? (curr_pc + 8u) : r[rn];
                const uint32_t addr = u_bit ? (base + off) : (base - off);
                uint32_t val = is_ldr ? 0u : r[rd];
                if (sim_mem_access(m, bufs, stack, curr_pc, rn, addr, is_ldr, is_byte, &val) != BMN_OK)
                    return BMN_ERR_TRAP;
                if (is_ldr)
                    r[rd] = val;
                continue;
            }
        }

        /* 5c. Halfword Transfer: LDRH / STRH Rd, [Rn, #+/-imm8] (bits 27..25 == 0b000, bits 7..4 == 0b1011) */
        if ((w & 0x0E0000F0u) == 0x000000B0u) {
            const int is_ldr = (int)((w >> 20) & 1u);
            const int u_bit  = (int)((w >> 23) & 1u);
            const uint32_t rn = (w >> 16) & 0xFu;
            const uint32_t rd = (w >> 12) & 0xFu;
            const uint32_t imm8 = ((w >> 4) & 0xF0u) | (w & 0x0Fu);
            const uint32_t base = (rn == 15u) ? (curr_pc + 8u) : r[rn];
            const uint32_t addr = u_bit ? (base + imm8) : (base - imm8);
            const uint32_t m_off = addr & 0xFFFFu;
            if (is_ldr) {
                uint32_t val = (uint32_t)g_sim_ram[m_off] |
                               ((uint32_t)g_sim_ram[(m_off + 1) & 0xFFFFu] << 8);
                r[rd] = val;
            } else {
                g_sim_ram[m_off] = (uint8_t)(r[rd] & 0xFFu);
                g_sim_ram[(m_off + 1) & 0xFFFFu] = (uint8_t)((r[rd] >> 8) & 0xFFu);
            }
            continue;
        }

        /* 5b. VFPv2 single-precision load/store & push/pop (top4 == 0xC/0xD, cp == 10) */
        if ((((w >> 24) & 0xFu) == 0xCu || ((w >> 24) & 0xFu) == 0xDu) && ((w >> 8) & 0xFu) == 10u) {
            const uint32_t p_bit = (w >> 24) & 1u;
            const uint32_t u_bit = (w >> 23) & 1u;
            const uint32_t d_bit = (w >> 22) & 1u;
            const uint32_t w_bit = (w >> 21) & 1u;
            const uint32_t l_bit = (w >> 20) & 1u;
            const uint32_t rn    = (w >> 16) & 0xFu;
            const uint32_t sd0   = (((w >> 12) & 0xFu) << 1) | d_bit;
            const uint32_t imm8  = w & 0xFFu;
            if (p_bit == 1u && w_bit == 0u) {
                /* VLDR / VSTR Sd, [Rn, #+/-imm8*4] */
                const uint32_t base = (rn == 15u) ? (curr_pc + 8u) : r[rn];
                const uint32_t addr = u_bit ? (base + imm8 * 4u) : (base - imm8 * 4u);
                uint32_t val = l_bit ? 0u : f32_to_u32(s[sd0]);
                if (sim_mem_access(m, bufs, stack, curr_pc, rn, addr, (int)l_bit, 0, &val) != BMN_OK)
                    return BMN_ERR_TRAP;
                if (l_bit)
                    s[sd0] = u32_to_f32(val);
                continue;
            }
            /* VLDM / VSTM / VPOP / VPUSH (single-precision list) */
            if (rn == 15u || sd0 + imm8 > 32u)
                return BMN_ERR_TRAP;
            if (p_bit == 1u && u_bit == 0u && w_bit == 1u && l_bit == 0u) {
                /* VPUSH / VSTMDB Rn!, {Sd..} */
                uint32_t addr = r[rn] - imm8 * 4u;
                for (uint32_t k = 0; k < imm8; k++) {
                    uint32_t val = f32_to_u32(s[sd0 + k]);
                    if (sim_mem_access(m, bufs, stack, curr_pc, rn, addr + k * 4u, 0, 0, &val) != BMN_OK)
                        return BMN_ERR_TRAP;
                }
                r[rn] = addr;
                continue;
            }
            if (p_bit == 0u && u_bit == 1u && l_bit == 1u) {
                /* VPOP / VLDMIA Rn(!), {Sd..} */
                uint32_t addr = r[rn];
                for (uint32_t k = 0; k < imm8; k++) {
                    uint32_t val = 0u;
                    if (sim_mem_access(m, bufs, stack, curr_pc, rn, addr + k * 4u, 1, 0, &val) != BMN_OK)
                        return BMN_ERR_TRAP;
                    s[sd0 + k] = u32_to_f32(val);
                }
                if (w_bit)
                    r[rn] = addr + imm8 * 4u;
                continue;
            }
            return BMN_ERR_TRAP;
        }

        /* 5c. VFPv2 single-precision coprocessor 10 register & arithmetic instructions (top4 == 0xE, cp == 10) */
        if (((w >> 24) & 0xFu) == 0xEu && ((w >> 8) & 0xFu) == 10u) {
            const uint32_t op_mask = w & 0x0FFFFFFFu;
            if (op_mask == 0x0EF1FA10u) {
                flag_n = vfp_n;
                flag_z = vfp_z;
                flag_c = vfp_c;
                flag_v = vfp_v;
                continue;
            }
            const uint32_t sd = (((w >> 12) & 0xFu) << 1) | ((w >> 22) & 1u);
            const uint32_t sn = (((w >> 16) & 0xFu) << 1) | ((w >> 7) & 1u);
            const uint32_t sm = ((w & 0xFu) << 1) | ((w >> 5) & 1u);
            if ((w & 0x10u) != 0u) {
                /* VMOV Rt <-> Sn (bits[23:20]: 0 = Sn <- Rt, 1 = Rt <- Sn) */
                const uint32_t rt = (w >> 12) & 0xFu;
                const uint32_t opc = (w >> 20) & 0xFu;
                if (opc == 0u) {
                    s[sn] = u32_to_f32(r[rt]);
                    continue;
                }
                if (opc == 1u) {
                    r[rt] = f32_to_u32(s[sn]);
                    continue;
                }
                return BMN_ERR_TRAP;
            }
            const uint32_t pqr = (((w >> 23) & 1u) << 2) | (((w >> 21) & 1u) << 1) | ((w >> 20) & 1u);
            const uint32_t s_op = (w >> 6) & 1u;
            if (pqr == 0u) {
                if (s_op == 0u) s[sd] = s[sd] + s[sn] * s[sm];
                else            s[sd] = s[sd] - s[sn] * s[sm];
                continue;
            }
            if (pqr == 2u) {
                if (s_op == 0u) s[sd] = s[sn] * s[sm];
                else            s[sd] = -(s[sn] * s[sm]);
                continue;
            }
            if (pqr == 3u) {
                if (s_op == 0u) s[sd] = s[sn] + s[sm];
                else            s[sd] = s[sn] - s[sm];
                continue;
            }
            if (pqr == 4u && s_op == 0u) {
                s[sd] = (s[sm] != 0.0f) ? (s[sn] / s[sm]) : 0.0f;
                continue;
            }
            if (pqr == 7u && s_op == 1u) {
                const uint32_t opc2 = (w >> 16) & 0xFu;
                const uint32_t bit7 = (w >> 7) & 1u;
                if (opc2 == 0u && bit7 == 0u) { s[sd] = s[sm]; continue; }
                if (opc2 == 0u && bit7 == 1u) { s[sd] = fabsf(s[sm]); continue; }
                if (opc2 == 1u && bit7 == 0u) { s[sd] = -s[sm]; continue; }
                if (opc2 == 1u && bit7 == 1u) { s[sd] = sqrtf(s[sm]); continue; }
                if (opc2 == 4u && bit7 == 0u) {
                    const float a = s[sd], b = s[sm];
                    if (!(a < b) && !(a == b) && !(a > b)) {
                        vfp_n = 0; vfp_z = 0; vfp_c = 1; vfp_v = 1;
                    } else {
                        vfp_n = (a < b); vfp_z = (a == b); vfp_c = (a >= b); vfp_v = 0;
                    }
                    continue;
                }
                if (opc2 == 5u && bit7 == 0u) {
                    const float a = s[sd];
                    if (!(a < 0.0f) && !(a == 0.0f) && !(a > 0.0f)) {
                        vfp_n = 0; vfp_z = 0; vfp_c = 1; vfp_v = 1;
                    } else {
                        vfp_n = (a < 0.0f); vfp_z = (a == 0.0f); vfp_c = (a >= 0.0f); vfp_v = 0;
                    }
                    continue;
                }
                if (opc2 == 8u && bit7 == 1u)  { s[sd] = (float)(int32_t)f32_to_u32(s[sm]); continue; }
                if (opc2 == 13u && bit7 == 1u) { s[sd] = u32_to_f32((uint32_t)(int32_t)s[sm]); continue; }
            }
            return BMN_ERR_TRAP;
        }

        /* 6. MUL / MLA Rd, Rm, Rs [, Rn] (bits 27..22 == 0, bits 7..4 == 0b1001) */
        if ((w & 0x0FC000F0u) == 0x00000090u) {
            const int is_mla  = (int)((w >> 21) & 1u);
            const uint32_t rd = (w >> 16) & 0xFu;
            const uint32_t ra = (w >> 12) & 0xFu;
            const uint32_t rs = (w >> 8) & 0xFu;
            const uint32_t rm = w & 0xFu;
            const int s_bit   = (int)((w >> 20) & 1u);
            const uint32_t res = r[rm] * r[rs] + (is_mla ? r[ra] : 0u);
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
                uint32_t shift_imm = 0;
                if ((w & 0x10u) != 0) {
                    /* Shift by register */
                    const uint32_t rs = (w >> 8) & 0xFu;
                    shift_imm = r[rs] & 0xFFu;
                } else {
                    shift_imm = (w >> 7) & 0x1Fu;
                }
                if (shift_imm == 0) {
                    op2 = val_m;
                } else if (shift_imm >= 32u) {
                    if (shift_type == 0u) { /* LSL */
                        shift_c = (shift_imm == 32u) ? (int)(val_m & 1u) : 0;
                        op2 = 0;
                    } else if (shift_type == 1u) { /* LSR */
                        shift_c = (shift_imm == 32u) ? (int)((val_m >> 31) & 1u) : 0;
                        op2 = 0;
                    } else if (shift_type == 2u) { /* ASR */
                        int32_t sval = (int32_t)val_m;
                        shift_c = (sval < 0) ? 1 : 0;
                        op2 = (sval < 0) ? 0xFFFFFFFFu : 0u;
                    } else {                       /* ROR */
                        shift_imm &= 0x1Fu;
                        if (shift_imm == 0) {
                            op2 = val_m;
                        } else {
                            op2 = (val_m >> shift_imm) | (val_m << (32u - shift_imm));
                            shift_c = (int)((op2 >> 31) & 1u);
                        }
                    }
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

    g_last_insns = BMN_BUDGET - budget;
    *ret = (int32_t)r[0];
    return BMN_OK;
}
#endif

int bmnative_call_buf(const bmnative_mod *m, int fn, const int32_t *args, const bmnative_buf *bufs, int nargs, int32_t *ret)
{
    g_active_mod = m;
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

typedef struct {
    char *p;
    size_t pos;
    size_t cap;
} dis_buf_t;

static void db_ch(dis_buf_t *d, char c)
{
    if (d->pos + 1 < d->cap)
        d->p[d->pos] = c;
    d->pos++;
}

static void db_str(dis_buf_t *d, const char *s)
{
    while (*s)
        db_ch(d, *s++);
}

static void db_uint(dis_buf_t *d, uint32_t v)
{
    char tmp[12];
    int n = 0;
    if (v == 0) {
        db_ch(d, '0');
        return;
    }
    while (v > 0) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0)
        db_ch(d, tmp[--n]);
}

static void db_sint(dis_buf_t *d, int32_t v, int plus_sign)
{
    if (v < 0) {
        db_ch(d, '-');
        db_uint(d, (uint32_t)(-(int64_t)v));
    } else {
        if (plus_sign)
            db_ch(d, '+');
        db_uint(d, (uint32_t)v);
    }
}

static void db_hex(dis_buf_t *d, uint32_t v, int digits)
{
    static const char hd[] = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; i--)
        db_ch(d, hd[(v >> (i * 4)) & 0xFu]);
}

static void db_reg(dis_buf_t *d, uint32_t r)
{
    db_ch(d, 'r');
    db_uint(d, r);
}

static void db_sreg(dis_buf_t *d, uint32_t s)
{
    db_ch(d, 's');
    db_uint(d, s);
}

static void db_shift(dis_buf_t *d, uint32_t w)
{
    static const char *const sh[] = { "lsl", "lsr", "asr", "ror" };
    uint32_t stype = (w >> 5) & 3u;
    if ((w & 0x10u) == 0u) {
        uint32_t simm = (w >> 7) & 0x1Fu;
        if (simm == 0u && stype == 0u)
            return;
        db_str(d, ", ");
        if (simm == 0u && stype == 3u) {
            db_str(d, "rrx");
            return;
        }
        db_str(d, sh[stype]);
        db_str(d, " #");
        db_uint(d, simm == 0u ? 32u : simm);
    } else {
        db_str(d, ", ");
        db_str(d, sh[stype]);
        db_ch(d, ' ');
        db_reg(d, (w >> 8) & 0xFu);
    }
}

static uint32_t db_ror_imm(uint32_t w)
{
    uint32_t rot = ((w >> 8) & 0xFu) * 2u;
    uint32_t imm = w & 0xFFu;
    return rot ? ((imm >> rot) | (imm << (32u - rot))) : imm;
}

void bmnative_disasm_word(uint32_t w, char *buf, size_t cap)
{
    if (!buf || cap == 0)
        return;
    dis_buf_t d = { buf, 0, cap };
    static const char *const conds[] = {
        "eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
        "hi", "ls", "ge", "lt", "gt", "le", "", "nv"
    };
    const char *cc = conds[(w >> 28) & 0xFu];
    uint32_t top4 = (w >> 24) & 0xFu;

    if (((w >> 25) & 7u) == 5u) {
        int is_bl = (int)((w >> 24) & 1u);
        int32_t imm24 = (int32_t)(w & 0xFFFFFFu);
        if (imm24 & 0x800000) imm24 -= 0x1000000;
        int32_t byte_off = (imm24 + 2) * 4;
        db_str(&d, is_bl ? "bl" : "b");
        db_str(&d, cc);
        db_ch(&d, ' ');
        db_sint(&d, byte_off, 1);
    } else if ((w & 0x0FFFFFF0u) == 0x012FFF10u) {
        int rm = (int)(w & 0xFu);
        db_str(&d, "bx");
        db_str(&d, cc);
        db_ch(&d, ' ');
        if (rm == 14) db_str(&d, "lr");
        else db_reg(&d, (uint32_t)rm);
    } else if ((w & 0x0FFF0000u) == 0x092D0000u) {
        db_str(&d, "push"); db_str(&d, cc); db_str(&d, " {0x");
        db_hex(&d, w & 0xFFFFu, 4); db_ch(&d, '}');
    } else if ((w & 0x0FFF0000u) == 0x08BD0000u) {
        db_str(&d, "pop"); db_str(&d, cc); db_str(&d, " {0x");
        db_hex(&d, w & 0xFFFFu, 4); db_ch(&d, '}');
    } else if (((w >> 25) & 7u) == 4u) {
        const int is_ldm = (int)((w >> 20) & 1u);
        const uint32_t p_bit = (w >> 24) & 1u;
        const uint32_t u_bit = (w >> 23) & 1u;
        const uint32_t w_bit = (w >> 21) & 1u;
        const uint32_t rn    = (w >> 16) & 0xFu;
        const uint32_t mask  = w & 0xFFFFu;
        db_str(&d, is_ldm ? "ldm" : "stm");
        db_str(&d, u_bit ? (p_bit ? "ib" : "ia") : (p_bit ? "db" : "da"));
        db_str(&d, cc);
        db_ch(&d, ' ');
        db_reg(&d, rn);
        if (w_bit)
            db_ch(&d, '!');
        db_str(&d, ", {");
        int first = 1;
        for (uint32_t reg = 0; reg < 16; reg++) {
            if (mask & (1u << reg)) {
                if (!first)
                    db_str(&d, ", ");
                db_reg(&d, reg);
                first = 0;
            }
        }
        db_ch(&d, '}');
    } else if ((w & 0x0FC000F0u) == 0x00000090u) {
        uint32_t rd = (w >> 16) & 0xFu, rn = (w >> 12) & 0xFu, rs = (w >> 8) & 0xFu, rm = w & 0xFu;
        int is_mla = (int)((w >> 21) & 1u);
        db_str(&d, is_mla ? "mla" : "mul");
        db_str(&d, cc); db_ch(&d, ' ');
        db_reg(&d, rd); db_str(&d, ", "); db_reg(&d, rm); db_str(&d, ", "); db_reg(&d, rs);
        if (is_mla) { db_str(&d, ", "); db_reg(&d, rn); }
    } else if ((w & 0x0E0000F0u) == 0x000000B0u) {
        int is_ldr = (int)((w >> 20) & 1u);
        int is_up = (int)((w >> 23) & 1u);
        uint32_t rn = (w >> 16) & 0xFu;
        uint32_t rd = (w >> 12) & 0xFu;
        uint32_t imm8 = ((w >> 4) & 0xF0u) | (w & 0x0Fu);
        db_str(&d, is_ldr ? "ldrh" : "strh");
        db_str(&d, cc); db_ch(&d, ' ');
        db_reg(&d, rd); db_str(&d, ", ["); db_reg(&d, rn);
        if (imm8 != 0u || !is_up) {
            db_str(&d, ", #");
            if (!is_up) db_ch(&d, '-');
            db_uint(&d, imm8);
        }
        db_ch(&d, ']');
    } else if (((w >> 26) & 3u) == 0u) {
        uint32_t op = (w >> 21) & 0xFu;
        uint32_t rn = (w >> 16) & 0xFu;
        uint32_t rd = (w >> 12) & 0xFu;
        static const char *const ops[] = {
            "and", "eor", "sub", "rsb", "add", "adc", "sbc", "rsc",
            "tst", "teq", "cmp", "cmn", "orr", "mov", "bic", "mvn"
        };
        db_str(&d, ops[op]); db_str(&d, cc); db_ch(&d, ' ');
        if (op == 13u || op == 15u) {
            db_reg(&d, rd); db_str(&d, ", ");
        } else if (op >= 8u && op <= 11u) {
            db_reg(&d, rn); db_str(&d, ", ");
        } else {
            db_reg(&d, rd); db_str(&d, ", "); db_reg(&d, rn); db_str(&d, ", ");
        }
        if ((w >> 25) & 1u) {
            db_ch(&d, '#');
            db_uint(&d, db_ror_imm(w));
        } else {
            db_reg(&d, w & 0xFu);
            db_shift(&d, w);
        }
    } else if (((w >> 26) & 3u) == 1u) {
        int is_reg = (int)((w >> 25) & 1u);
        int is_up = (int)((w >> 23) & 1u);
        int is_byte = (int)((w >> 22) & 1u);
        int is_load = (int)((w >> 20) & 1u);
        uint32_t rn = (w >> 16) & 0xFu;
        uint32_t rd = (w >> 12) & 0xFu;
        db_str(&d, is_load ? "ldr" : "str");
        if (is_byte) db_ch(&d, 'b');
        db_str(&d, cc); db_ch(&d, ' ');
        db_reg(&d, rd); db_str(&d, ", ["); db_reg(&d, rn);
        if (!is_reg) {
            uint32_t imm12 = w & 0xFFFu;
            if (imm12 != 0u || !is_up) {
                db_str(&d, ", #");
                if (!is_up) db_ch(&d, '-');
                db_uint(&d, imm12);
            }
        } else {
            db_str(&d, ", ");
            if (!is_up) db_ch(&d, '-');
            db_reg(&d, w & 0xFu);
            db_shift(&d, w & ~0x10u);
        }
        db_ch(&d, ']');
    } else if ((top4 == 0xCu || top4 == 0xDu) && ((w >> 8) & 0xFu) == 10u) {
        uint32_t p = (w >> 24) & 1u, u = (w >> 23) & 1u, dv = (w >> 22) & 1u;
        uint32_t wb = (w >> 21) & 1u, l = (w >> 20) & 1u, rn = (w >> 16) & 0xFu;
        uint32_t sd = (((w >> 12) & 0xFu) << 1) | dv;
        uint32_t imm8 = w & 0xFFu;
        if (p == 1u && wb == 0u) {
            db_str(&d, l ? "vldr" : "vstr"); db_str(&d, cc); db_ch(&d, ' ');
            db_sreg(&d, sd); db_str(&d, ", ["); db_reg(&d, rn);
            if (imm8 != 0u || !u) {
                db_str(&d, ", #");
                if (!u) db_ch(&d, '-');
                db_uint(&d, imm8 * 4u);
            }
            db_ch(&d, ']');
        } else if (p == 1u && u == 0u && wb == 1u && l == 0u && rn == 13u && imm8 > 0u) {
            db_str(&d, "vpush"); db_str(&d, cc); db_str(&d, " {");
            db_sreg(&d, sd);
            if (imm8 > 1u) { db_ch(&d, '-'); db_sreg(&d, sd + imm8 - 1u); }
            db_ch(&d, '}');
        } else if (p == 0u && u == 1u && wb == 1u && l == 1u && rn == 13u && imm8 > 0u) {
            db_str(&d, "vpop"); db_str(&d, cc); db_str(&d, " {");
            db_sreg(&d, sd);
            if (imm8 > 1u) { db_ch(&d, '-'); db_sreg(&d, sd + imm8 - 1u); }
            db_ch(&d, '}');
        } else {
            db_str(&d, l ? "vldm" : "vstm"); db_str(&d, u ? "ia" : "db");
            db_str(&d, cc); db_ch(&d, ' ');
            db_reg(&d, rn); if (wb) db_ch(&d, '!');
            db_str(&d, ", {"); db_sreg(&d, sd);
            if (imm8 > 1u) { db_ch(&d, '-'); db_sreg(&d, sd + imm8 - 1u); }
            db_ch(&d, '}');
        }
    } else if (top4 == 0xEu && ((w >> 8) & 0xFu) == 10u) {
        uint32_t sd = (((w >> 12) & 0xFu) << 1) | ((w >> 22) & 1u);
        uint32_t sn = (((w >> 16) & 0xFu) << 1) | ((w >> 7) & 1u);
        uint32_t sm = ((w & 0xFu) << 1) | ((w >> 5) & 1u);
        if ((w & 0x0FFFFFFFu) == 0x0EF1FA10u) {
            db_str(&d, "vmrs"); db_str(&d, cc); db_str(&d, " apsr_nzcv, fpscr");
        } else if ((w & 0x10u) != 0u) {
            uint32_t rt = (w >> 12) & 0xFu;
            uint32_t opc = (w >> 20) & 0xFu;
            if (opc == 0u) {
                db_str(&d, "vmov"); db_str(&d, cc); db_ch(&d, ' ');
                db_sreg(&d, sn); db_str(&d, ", "); db_reg(&d, rt);
            } else if (opc == 1u) {
                db_str(&d, "vmov"); db_str(&d, cc); db_ch(&d, ' ');
                db_reg(&d, rt); db_str(&d, ", "); db_sreg(&d, sn);
            } else {
                db_str(&d, ".word 0x"); db_hex(&d, w, 8);
            }
        } else {
            uint32_t pqr = (((w >> 23) & 1u) << 2) | (((w >> 21) & 1u) << 1) | ((w >> 20) & 1u);
            uint32_t s_op = (w >> 6) & 1u;
            int decoded = 1;
            if (pqr == 0u || pqr == 2u || pqr == 3u || (pqr == 4u && s_op == 0u)) {
                const char *mn = pqr == 0u ? (s_op ? "vmls" : "vmla")
                               : pqr == 2u ? (s_op ? "vnmul" : "vmul")
                               : pqr == 3u ? (s_op ? "vsub" : "vadd")
                                           : "vdiv";
                db_str(&d, mn); db_str(&d, cc); db_str(&d, ".f32 ");
                db_sreg(&d, sd); db_str(&d, ", "); db_sreg(&d, sn); db_str(&d, ", "); db_sreg(&d, sm);
            } else if (pqr == 7u && s_op == 1u) {
                uint32_t opc2 = (w >> 16) & 0xFu;
                uint32_t bit7 = (w >> 7) & 1u;
                if (opc2 == 0u && bit7 == 0u) {
                    db_str(&d, "vmov"); db_str(&d, cc); db_str(&d, ".f32 ");
                    db_sreg(&d, sd); db_str(&d, ", "); db_sreg(&d, sm);
                } else if (opc2 == 0u && bit7 == 1u) {
                    db_str(&d, "vabs"); db_str(&d, cc); db_str(&d, ".f32 ");
                    db_sreg(&d, sd); db_str(&d, ", "); db_sreg(&d, sm);
                } else if (opc2 == 1u && bit7 == 0u) {
                    db_str(&d, "vneg"); db_str(&d, cc); db_str(&d, ".f32 ");
                    db_sreg(&d, sd); db_str(&d, ", "); db_sreg(&d, sm);
                } else if (opc2 == 1u && bit7 == 1u) {
                    db_str(&d, "vsqrt"); db_str(&d, cc); db_str(&d, ".f32 ");
                    db_sreg(&d, sd); db_str(&d, ", "); db_sreg(&d, sm);
                } else if (opc2 == 4u && bit7 == 0u) {
                    db_str(&d, "vcmp"); db_str(&d, cc); db_str(&d, ".f32 ");
                    db_sreg(&d, sd); db_str(&d, ", "); db_sreg(&d, sm);
                } else if (opc2 == 5u && bit7 == 0u) {
                    db_str(&d, "vcmpz"); db_str(&d, cc); db_str(&d, ".f32 ");
                    db_sreg(&d, sd);
                } else if (opc2 == 8u && bit7 == 1u) {
                    db_str(&d, "vcvt"); db_str(&d, cc); db_str(&d, ".f32.s32 ");
                    db_sreg(&d, sd); db_str(&d, ", "); db_sreg(&d, sm);
                } else if ((opc2 == 12u || opc2 == 13u) && bit7 == 1u) {
                    db_str(&d, "vcvt"); db_str(&d, cc); db_str(&d, ".s32.f32 ");
                    db_sreg(&d, sd); db_str(&d, ", "); db_sreg(&d, sm);
                } else {
                    decoded = 0;
                }
            } else {
                decoded = 0;
            }
            if (!decoded) {
                db_str(&d, ".word 0x");
                db_hex(&d, w, 8);
            }
        }
    } else {
        db_str(&d, ".word 0x");
        db_hex(&d, w, 8);
    }
    buf[d.pos < cap ? d.pos : cap - 1] = '\0';
}
