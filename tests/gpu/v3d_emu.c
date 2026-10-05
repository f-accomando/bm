/*
 * A V3D for the tests on the PC (M33): the functions of src/gpu/v3d.h
 * over a small emulator. It reads the control lists bm writes (only the
 * packets bm uses: anything else is an error), keeps the triangles of the
 * binning list with their state, and runs the rendering list tile by tile
 * on a 64x64 tile buffer (bytes a b c d, 24-bit depth): loads, the
 * triangles of the tile (the branch must go to the right tile list),
 * stores. The fragment shaders are recognised by their code (shaders.h)
 * and done in C. It checks what bm writes against what bm believes about
 * the V3D, not the V3D itself: that only the Pi can say.
 *
 * Two hidden choices the backend must find with its probe: which byte of
 * the tile buffer is red in BGR565 (emu_red_a) and whether the TMU swaps
 * bytes a and c of a texel (emu_tex_swap).
 *
 * Bus addresses: the memory of the backend comes from an arena
 * (test_aligned_alloc), bus = 0x40000000 + offset in the arena.
 */
#include "v3d_emu.h"
#include "gpu/shaders.h"
#include "gpu/v3d.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int emu_red_a = 1, emu_tex_swap = 0;
int emu_tformat = 0;                    /* the order of the 1 KiB subtiles of a T-format tile */
int emu_ms_load_one = 0;                /* MSAA: a colour load fills sample 0 only (else all 4) */
int emu_skip = 0;                       /* jobs only counted, not run (ARM instruction counts) */
int emu_no_threads = 0;                 /* M39: jobs with two-thread fragment shaders do not end */
int emu_hang_zclear = 0;                /* a job with fs_zclear does not end */
int emu_cw_flip = 0;                    /* GL: the V3D calls the other orientation clockwise */
int emu_clip = 0;                       /* GL clipping: 0 as GL (near plane, guard band), 1 none (the
                                         * flag ignored), 2 the near plane only with Z_MIN_MAX given */
int emu_vpm_words = 0;                  /* GL records: the attributes' VPM offsets and total size in
                                         * 32-bit words (else in bytes, as Mesa writes them) */
emu_stats_t emu_stats;
char emu_error[256];

/* ---------------------------------------------------------------- arena */

#define ARENA (160u << 20)                 /* up to 1920x1080: pages, the job, textures */
static uint8_t *arena;
static size_t arena_used;

void *test_aligned_alloc(size_t align, size_t size)
{
    if (!arena)
        arena = aligned_alloc(4096, ARENA);
    size_t at = (arena_used + align - 1) & ~(align - 1);
    if (at + size > ARENA)
        return NULL;
    arena_used = at + size;
    return arena + at;
}

void test_free(void *p)
{
    (void)p;                            /* the arena is not given back */
}

uint32_t v3d_bus(const void *p)
{
    const uint8_t *b = p;
    if (!arena || b < arena || b >= arena + ARENA) {
        fprintf(stderr, "v3d_emu: address outside the arena: %p\n", p);
        exit(2);
    }
    return 0x40000000u + (uint32_t)(b - arena);
}

static void *ptr(uint32_t bus)
{
    uint32_t off = bus - 0x40000000u;
    if (bus < 0x40000000u || off >= ARENA)
        return NULL;
    return arena + off;
}

/* ---------------------------------------------------------------- v3d.h */

static uint32_t overflow_bus, overflow_size;

int v3d_init(void) { return 0; }
const char *v3d_status(void) { return "emulated"; }
uint32_t v3d_ident(int i) { return i == 0 ? 0x02443356u : 0; }
void v3d_set_overflow(uint32_t bus, uint32_t size) { overflow_bus = bus; overflow_size = size; }

int emu_uncached = 0;

int v3d_uncached(void *p, uint32_t size, int on)
{
    (void)p;
    emu_uncached = on;                  /* nothing changes here: the PC has no such mapping */
    return (int)(size >> 20);
}

void v3d_dump(char *buf, size_t n)
{
    snprintf(buf, n, "emulator: %s\n", emu_error);
}

static int err(const char *fmt, unsigned a, unsigned b)
{
    snprintf(emu_error, sizeof emu_error, fmt, a, b);
    return -1;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static float rdf(const uint8_t *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

/* ---------------------------------------------------------------- QPU */

/* The QPU interpreter (M36): the vertex and coordinate shaders run as the
 * V3D runs them, 16 vertices at a time, so an error in their code shows on
 * the PC. The subset the shaders of bm use: the ALU ops, regfile A and B,
 * the accumulators, uniforms, the VPM (horizontal 32-bit rows), the SFU's
 * reciprocal, small and 32-bit immediates, the 16-bit packs of regfile A.
 * What the hardware does not allow is an error: reading a register of
 * regfile A or B the instruction before wrote, r4 within two instructions
 * of an SFU write, the VPM or uniforms in the last three instructions. */

#define QPU_VPM_ROWS 64

typedef struct {
    uint32_t ra[32][16], rb[32][16], acc[6][16];
    uint8_t z[16], n[16];
    const uint32_t *unif;
    int nunif, unif_at;
    uint32_t (*vpm)[16];
    int vr_addr, vr_left, vr_stride, vw_addr, vw_stride;
    int vr_count;                       /* rows read */
    int wrote_a, wrote_b;               /* regfile registers the last instruction wrote (-1 none) */
    int sfu_at;                         /* the instruction that wrote the SFU, -1 */
    int sfu_pending;
    uint32_t sfu_value[16];
} qpu_t;

static float qf(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint32_t qu(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

static uint32_t qpu_add_op(int op, uint32_t a, uint32_t b, int *bad)
{
    switch (op) {
    case 0: return 0;
    case 1: return qu(qf(a) + qf(b));
    case 2: return qu(qf(a) - qf(b));
    case 3: return qu(fminf(qf(a), qf(b)));
    case 4: return qu(fmaxf(qf(a), qf(b)));
    case 7: { float f = qf(a); return isnan(f) ? 0 : (uint32_t)(int32_t)f; }
    case 8: return qu((float)(int32_t)a);
    case 12: return a + b;
    case 13: return a - b;
    case 14: return a >> (b & 31);
    case 15: return (uint32_t)((int32_t)a >> (b & 31));
    case 17: return a << (b & 31);
    case 18: return (int32_t)a < (int32_t)b ? a : b;
    case 19: return (int32_t)a > (int32_t)b ? a : b;
    case 20: return a & b;
    case 21: return a | b;
    case 22: return a ^ b;
    case 23: return ~a;
    default: *bad = 1; return 0;
    }
}

static uint32_t qpu_mul_op(int op, uint32_t a, uint32_t b, int *bad)
{
    switch (op) {
    case 0: return 0;
    case 1: return qu(qf(a) * qf(b));
    case 2: return (a & 0xFFFFFF) * (b & 0xFFFFFF);
    case 4: {                           /* v8min: mov when a == b */
        uint32_t r = 0;
        for (int k = 0; k < 32; k += 8) {
            uint32_t x = a >> k & 255, y = b >> k & 255;
            r |= (x < y ? x : y) << k;
        }
        return r;
    }
    default: *bad = 1; return 0;
    }
}

static int qpu_float_add(int op) { return op >= 1 && op <= 7; }

/* source mux m of lane l */
static uint32_t qpu_src(qpu_t *q, int m, int l, const uint32_t *va, const uint32_t *vb)
{
    return m < 6 ? q->acc[m][l] : m == 6 ? va[l] : vb[l];
}

/* runs code until its thread end; 0, or -1 (emu_error says why) */
static int qpu_run(qpu_t *q, const uint32_t *code, int max)
{
    int end = -1;
    q->wrote_a = q->wrote_b = -1;
    q->sfu_at = -1;
    for (int pc = 0; pc < max; pc++) {
        const uint32_t lo = code[2 * pc], hi = code[2 * pc + 1];
        const int sig = hi >> 28, last3 = end >= 0;
        const int ws = hi >> 12 & 1, sf = hi >> 13 & 1, cond_add = hi >> 17 & 7, cond_mul = hi >> 14 & 7;
        const int waddr_add = hi >> 6 & 63, waddr_mul = hi & 63, pm = hi >> 24 & 1, pack = hi >> 20 & 15;
        uint32_t ra_[16], rb_[16], res_add[16], res_mul[16];
        int bad = 0;
        if (q->sfu_pending && pc == q->sfu_at + 3) {
            memcpy(q->acc[4], q->sfu_value, sizeof q->sfu_value);
            q->sfu_pending = 0;
        }
        if (sig == 15)
            return err("QPU: branch at %u (not emulated)", (unsigned)pc, 0);
        if (sig == 14) {                /* load immediate */
            if (hi >> 25 & 7)
                return err("QPU: packed load immediate at %u", (unsigned)pc, 0);
            for (int l = 0; l < 16; l++)
                res_add[l] = res_mul[l] = lo;
        } else {
            const int op_add = lo >> 24 & 31, op_mul = lo >> 29 & 7, raddr_a = lo >> 18 & 63,
                      raddr_b = lo >> 12 & 63;
            const int add_a = lo >> 9 & 7, add_b = lo >> 6 & 7, mul_a = lo >> 3 & 7, mul_b = lo & 7;
            if (hi >> 25 & 7)
                return err("QPU: unpack at %u (not emulated)", (unsigned)pc, 0);
            const int uses[4] = { op_add ? add_a : -1, op_add ? add_b : -1, op_mul ? mul_a : -1,
                                  op_mul ? mul_b : -1 };
            int use_a = 0, use_b = 0, use_r4 = 0;
            for (int k = 0; k < 4; k++) {
                use_a |= uses[k] == 6;
                use_b |= uses[k] == 7;
                use_r4 |= uses[k] == 4;
            }
            if (use_r4 && q->sfu_at >= 0 && pc - q->sfu_at < 3)
                return err("QPU: r4 read at %u, two instructions after the SFU", (unsigned)pc, 0);
            if (use_a && raddr_a < 32 && raddr_a == q->wrote_a)
                return err("QPU: ra%u read at %u right after its write", (unsigned)raddr_a, (unsigned)pc);
            if (use_b && sig != 13 && raddr_b < 32 && raddr_b == q->wrote_b)
                return err("QPU: rb%u read at %u right after its write", (unsigned)raddr_b, (unsigned)pc);
            int unif_read = (use_a && raddr_a == 32) || (use_b && sig != 13 && raddr_b == 32);
            int vpm_read = (use_a && raddr_a == 48) || (use_b && sig != 13 && raddr_b == 48);
            if ((unif_read || vpm_read) && last3)
                return err("QPU: uniform or VPM read at %u, in the last three instructions", (unsigned)pc, 0);
            uint32_t unif = 0, vrow[16] = { 0 };
            if (unif_read) {
                if (q->unif_at >= q->nunif)
                    return err("QPU: uniform %u read, only %u given", (unsigned)q->unif_at, (unsigned)q->nunif);
                unif = q->unif[q->unif_at++];
            }
            if (vpm_read) {
                if (q->vr_left <= 0 || q->vr_addr >= QPU_VPM_ROWS)
                    return err("QPU: VPM read at %u past its setup", (unsigned)pc, 0);
                memcpy(vrow, q->vpm[q->vr_addr], sizeof vrow);
                q->vr_addr += q->vr_stride;
                q->vr_left--;
                q->vr_count++;
            }
            for (int l = 0; l < 16; l++) {
                if (use_a)
                    ra_[l] = raddr_a < 32 ? q->ra[raddr_a][l] : raddr_a == 32 ? unif : raddr_a == 38 ? (uint32_t)l
                           : raddr_a == 39 ? 0 : raddr_a == 48 ? vrow[l] : 0xDEADBEEF;
                if (use_b) {
                    if (sig == 13) {
                        int s = raddr_b;
                        rb_[l] = s < 16 ? (uint32_t)s : s < 32 ? (uint32_t)(s - 32)
                               : s < 40 ? qu(ldexpf(1.0f, s - 32)) : s < 48 ? qu(ldexpf(1.0f, s - 48)) : 0;
                    } else {
                        rb_[l] = raddr_b < 32 ? q->rb[raddr_b][l] : raddr_b == 32 ? unif : raddr_b == 38 ? 0
                               : raddr_b == 39 ? 0 : raddr_b == 48 ? vrow[l] : 0xDEADBEEF;
                    }
                }
            }
            if ((use_a && raddr_a >= 32 && raddr_a != 32 && raddr_a != 38 && raddr_a != 39 && raddr_a != 48) ||
                (use_b && sig != 13 && raddr_b >= 32 && raddr_b != 32 && raddr_b != 38 && raddr_b != 39 &&
                 raddr_b != 48) || (use_b && sig == 13 && raddr_b >= 48))
                return err("QPU: read address %u/%u at an emulated shader", (unsigned)raddr_a, (unsigned)raddr_b);
            for (int l = 0; l < 16; l++) {
                res_add[l] = qpu_add_op(op_add, qpu_src(q, add_a, l, ra_, rb_), qpu_src(q, add_b, l, ra_, rb_), &bad);
                res_mul[l] = qpu_mul_op(op_mul, qpu_src(q, mul_a, l, ra_, rb_), qpu_src(q, mul_b, l, ra_, rb_), &bad);
            }
            if (bad)
                return err("QPU: op %u/%u not emulated", (unsigned)op_add, (unsigned)op_mul);
            if (pm || (pack && pack > 2))
                return err("QPU: pack %u (pm %u) not emulated", (unsigned)pack, (unsigned)pm);
            if (pack && ((!ws && qpu_float_add(op_add)) || (ws && op_mul == 1)))
                return err("QPU: 16-bit pack of a float at %u", (unsigned)pc, 0);
            if (sf) {
                const uint32_t *r = op_add ? res_add : res_mul;
                for (int l = 0; l < 16; l++) {
                    q->z[l] = r[l] == 0;
                    q->n[l] = r[l] >> 31;
                }
            }
            (void)cond_add;
        }
        /* writes: the add ALU to file A (B with ws), the mul ALU to B (A) */
        int new_a = -1, new_b = -1;
        for (int alu = 0; alu < 2; alu++) {
            const int waddr = alu ? waddr_mul : waddr_add, cond = alu ? cond_mul : cond_add;
            const int file_a = alu ? ws : !ws;
            const uint32_t *res = alu ? res_mul : res_add;
            if (cond == 0 || waddr == 39)
                continue;
            if (cond > 5 || (cond != 1 && waddr >= 36))
                return err("QPU: condition %u on write address %u (not emulated)", (unsigned)cond, (unsigned)waddr);
            int on[16];                 /* the lanes written: always, or as the flags say */
            for (int l = 0; l < 16; l++)
                on[l] = cond == 1 || (cond == 2 && q->z[l]) || (cond == 3 && !q->z[l]) || (cond == 4 && q->n[l]) ||
                        (cond == 5 && !q->n[l]);
            if (waddr < 32) {
                uint32_t (*reg)[16] = file_a ? q->ra : q->rb;
                for (int l = 0; l < 16; l++) {
                    if (!on[l])
                        continue;
                    if (file_a && pack == 1)
                        reg[waddr][l] = (reg[waddr][l] & 0xFFFF0000u) | (res[l] & 0xFFFF);
                    else if (file_a && pack == 2)
                        reg[waddr][l] = (reg[waddr][l] & 0xFFFF) | res[l] << 16;
                    else
                        reg[waddr][l] = res[l];
                }
                if (file_a) new_a = waddr; else new_b = waddr;
            } else if (waddr >= 32 && waddr <= 35) {
                for (int l = 0; l < 16; l++)
                    if (on[l])
                        q->acc[waddr - 32][l] = res[l];
            } else if (waddr == 48) {       /* VPM write */
                if (last3)
                    return err("QPU: VPM write at %u, in the last three instructions", (unsigned)pc, 0);
                if (q->vw_addr >= QPU_VPM_ROWS)
                    return err("QPU: VPM write past row %u", QPU_VPM_ROWS, 0);
                memcpy(q->vpm[q->vw_addr], res, sizeof q->vpm[0]);
                q->vw_addr += q->vw_stride;
            } else if (waddr == 49) {       /* VPM read (file A) or write (file B) setup */
                const uint32_t v = res[0];
                if ((v >> 30) || (v >> 8 & 3) != 2 || !(v >> 11 & 1))
                    return err("QPU: VPM setup %08x: generic horizontal 32-bit expected", v, 0);
                if (file_a) {
                    q->vr_addr = v & 255;
                    q->vr_stride = v >> 12 & 63;
                    q->vr_left = (v >> 20 & 15) ? (int)(v >> 20 & 15) : 16;
                } else {
                    q->vw_addr = v & 255;
                    q->vw_stride = v >> 12 & 63;
                }
            } else if (waddr == 52 || waddr == 54 || waddr == 55) {    /* SFU: r4, three instructions on */
                if (q->sfu_pending)
                    return err("QPU: two SFU writes at %u", (unsigned)pc, 0);
                for (int l = 0; l < 16; l++)
                    q->sfu_value[l] = qu(waddr == 52 ? 1.0f / qf(res[l]) : waddr == 54 ? exp2f(qf(res[l]))
                                         : log2f(qf(res[l])));
                q->sfu_pending = 1;
                q->sfu_at = pc;
            } else {
                return err("QPU: write address %u at %u (not emulated)", (unsigned)waddr, (unsigned)pc);
            }
        }
        q->wrote_a = new_a;
        q->wrote_b = new_b;
        if (sig == 3)
            end = pc;
        if (end >= 0 && pc == end + 2)
            return 0;
        if (sig != 1 && sig != 3 && sig != 13 && sig != 14)
            return err("QPU: signal %u at %u (not emulated)", (unsigned)sig, (unsigned)pc);
    }
    return err("QPU: no thread end in %u instructions", (unsigned)max, 0);
}

/* ---------------------------------------------------------------- binning */

enum { SH_COLOUR, SH_TEX, SH_TEX_ALPHA, SH_SCREEN, SH_TEX_RGB, SH_TEX_RGB_ALPHA, SH_ZCLEAR, SH_TEX_SCREEN,
       SH_TEX_RGB_SCREEN, SH_TEXT };

typedef struct { float x, y, z, iw, v[8]; } evert_t;

typedef struct {
    evert_t v[3];
    int shader, nvary;
    const uint32_t *params;
    int depth_func, z_update;
    int early;                          /* CONFIGURATION_BITS: bit 0 early z, bit 1 its updates */
    int oversample;                     /* CONFIGURATION_BITS: 1 = 4x (MSAA) */
    int clip[4];
} eprim_t;

/* the vertices of a GL batch, shaded by its vertex and coordinate shaders
 * (16 at a time, as the VCD fills the VPM), and their clip coordinates
 * (Xc Yc Zc Wc, the coordinate shader's first rows); 0 or -1 */
static int gl_vertices(const uint8_t *rec, int nattr, uint32_t first, uint32_t n, int nvary, evert_t *out,
                       float (*cc)[4])
{
    static uint32_t vpm[QPU_VPM_ROWS][16], vs_out[QPU_VPM_ROWS][16];
    static qpu_t q;
    const uint32_t *code[2] = { ptr(rd32(rec + 16)), ptr(rd32(rec + 28)) },
                   *unif[2] = { ptr(rd32(rec + 20)), ptr(rd32(rec + 32)) };
    const int sel[2] = { rec[14], rec[26] }, size[2] = { rec[15], rec[27] };
    if (!code[0] || !code[1] || !unif[0] || !unif[1])
        return err("GL record: shader code or uniforms outside memory", 0, 0);
    for (uint32_t b = 0; b < n; b += 16) {
        const uint32_t cnt = n - b < 16 ? n - b : 16;
        for (int s = 0; s < 2; s++) {           /* the vertex shader, then the coordinate shader */
            memset(vpm, 0, sizeof vpm);
            /* the total size and the offsets: bytes (Mesa: "byte offsets for
             * the start of the vertex attributes 0-7, and the total size"),
             * or words with emu_vpm_words */
            const int unit = emu_vpm_words ? 1 : 4;
            if (size[s] % unit || size[s] / unit > QPU_VPM_ROWS)
                return err("GL record: %u (units of %u bytes) of attributes", (unsigned)size[s], (unsigned)unit);
            const uint32_t rows = (uint32_t)size[s] / (uint32_t)unit;
            for (int a = 0; a < nattr; a++) {
                const uint8_t *at = rec + 36 + 8 * a;
                if (!(sel[s] >> a & 1))
                    continue;
                const uint32_t base = rd32(at), bytes = at[4] + 1u, stride = at[5];
                if (at[6 + s] % unit)
                    return err("GL attribute at VPM offset %u (units of %u bytes)", at[6 + s], (unsigned)unit);
                const uint32_t off = at[6 + s] / (uint32_t)unit;
                if (bytes % 4 || off + bytes / 4 > rows)
                    return err("GL attribute: %u bytes at word %u", bytes, off);
                for (uint32_t l = 0; l < cnt; l++) {
                    const uint8_t *src = ptr(base + (first + b + l) * stride);
                    if (!src || !ptr(base + (first + b + l) * stride + bytes - 1))
                        return err("GL attribute %u: vertex outside memory", (unsigned)a, 0);
                    for (uint32_t w = 0; w < bytes / 4; w++)
                        vpm[off + w][l] = rd32(src + 4 * w);
                }
            }
            memset(&q, 0, sizeof q);
            q.unif = unif[s];
            q.nunif = 256;
            q.vpm = vpm;
            if (qpu_run(&q, code[s], 512) != 0)
                return -1;
            /* as Broadcom's simulator (Mesa's emit_stub_vpm_read): every
             * attribute word loaded is read by the shader */
            if ((uint32_t)q.vr_count != rows)
                return err(s ? "GL: the coordinate shader read %u VPM rows, the record loads %u"
                             : "GL: the vertex shader read %u VPM rows, the record loads %u",
                           (unsigned)q.vr_count, rows);
            if (s == 0) {
                memcpy(vs_out, vpm, sizeof vpm);
                continue;
            }
            /* the coordinate shader places them as the vertex shader */
            for (uint32_t l = 0; l < cnt; l++) {
                for (int k = 0; k < 3; k++)
                    if (vpm[4 + k][l] != vs_out[k][l])
                        return err("GL: the coordinate shader's word %u differs from the vertex shader's (%08x)",
                                   (unsigned)k, vpm[4 + k][l]);
                for (int k = 0; k < 4; k++)
                    cc[b + l][k] = qf(vpm[k][l]);
            }
        }
        for (uint32_t l = 0; l < cnt; l++) {
            evert_t *v = &out[b + l];
            const uint32_t xy = vs_out[0][l];
            v->x = (int16_t)(xy & 0xFFFF) / 16.0f;
            v->y = (int16_t)(xy >> 16) / 16.0f;
            v->z = qf(vs_out[1][l]);
            v->iw = qf(vs_out[2][l]);
            for (int j = 0; j < nvary; j++)
                v->v[j] = qf(vs_out[3 + j][l]);
        }
    }
    emu_stats.glverts += n;
    return 0;
}

/* a polygon the CONFIGURATION_BITS keep (area: twice its signed area on
 * the screen): forward-facing ones are those clockwise on the screen (y
 * down), or the others with bit 2 clear */
static int area_kept(int cfg, float area)
{
    const int cw = (area > 0) ^ emu_cw_flip, forward = (cfg >> 2 & 1) ? cw : !cw;
    return forward ? cfg & 1 : cfg >> 1 & 1;
}

static int faces_kept(int cfg, const evert_t *v)
{
    return area_kept(cfg, (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[1].y - v[0].y) * (v[2].x - v[0].x));
}

/* GL clipping (the record's flag 4), as the PTB does it: a triangle that
 * crosses the near plane (Zc >= -Wc; with emu_clip 2 the plane of
 * Z_MIN_MAX instead, none without it) or reaches out of the guard band
 * (CLIP_GUARD pixels from the viewport's centre) is cut in clip space;
 * the new corners are placed by the clipper (Xc / Wc times its XY scale,
 * plus the viewport offset; Zc / Wc times its Z scale, plus its offset)
 * and their varyings blended in clip space (perspective-correct). */
#define CLIP_GUARD 2000.0f

typedef struct { float c[4], b[3]; } cvert_t;

typedef struct {
    float xs, ys, zs, zo;               /* CLIPPER_XY_SCALING (1/16 pixel), CLIPPER_Z_SCALING */
    int vx, vy;                         /* VIEWPORT_OFFSET (1/16 pixel) */
    int zplanes;                        /* Z_MIN_MAX given */
    float zmin, zmax;
} clipper_t;

/* the distance of c inside plane k (>= 0: kept) */
static float plane(const clipper_t *cl, int k, const float *c)
{
    const float gx = CLIP_GUARD * 16 / fabsf(cl->xs), gy = CLIP_GUARD * 16 / fabsf(cl->ys);
    switch (k) {
    case 0: return emu_clip == 2 ? c[2] * cl->zs + (cl->zo - cl->zmin) * c[3] : c[2] + c[3];  /* near */
    case 1: return emu_clip == 2 ? (cl->zmax - cl->zo) * c[3] - c[2] * cl->zs : c[3] - c[2];  /* far */
    case 2: return gx * c[3] - c[0];
    case 3: return gx * c[3] + c[0];
    case 4: return gy * c[3] - c[1];
    default: return gy * c[3] + c[1];
    }
}

/* the polygon of triangle t clipped (n corners, up to 9), or -1 if t needs
 * no clipping */
static int clip_tri(const clipper_t *cl, const float (*cc)[4], cvert_t *poly)
{
    const int k0 = emu_clip == 2 && !cl->zplanes ? 2 : 0;
    int need = 0;
    for (int k = k0; k < 6; k++)
        for (int i = 0; i < 3; i++)
            need |= plane(cl, k, cc[i]) < 0;
    if (!need)
        return -1;
    cvert_t buf[2][12];
    int n = 3;
    for (int i = 0; i < 3; i++) {
        memcpy(buf[0][i].c, cc[i], sizeof buf[0][i].c);
        for (int j = 0; j < 3; j++)
            buf[0][i].b[j] = i == j;
    }
    int cur = 0;
    for (int k = k0; k < 6 && n > 0; k++) {
        const cvert_t *in = buf[cur];
        cvert_t *out = buf[!cur];
        int m = 0;
        for (int i = 0; i < n; i++) {
            const cvert_t *a = &in[i], *b = &in[(i + 1) % n];
            const float da = plane(cl, k, a->c), db = plane(cl, k, b->c);
            if (da >= 0)
                out[m++] = *a;
            if ((da >= 0) != (db >= 0)) {
                const float t = da / (da - db);
                cvert_t *o = &out[m++];
                for (int j = 0; j < 4; j++)
                    o->c[j] = a->c[j] + t * (b->c[j] - a->c[j]);
                for (int j = 0; j < 3; j++)
                    o->b[j] = a->b[j] + t * (b->b[j] - a->b[j]);
            }
        }
        n = m;
        cur = !cur;
    }
    memcpy(poly, buf[cur], (size_t)n * sizeof *poly);
    return n;
}

/* a corner of a clipped polygon: one of the triangle's (as its shaders
 * placed it), or a new one */
static evert_t clip_corner(const clipper_t *cl, const cvert_t *c, const evert_t *t, int nvary)
{
    for (int i = 0; i < 3; i++)
        if (c->b[i] == 1.0f)
            return t[i];
    evert_t v;
    const float iw = 1.0f / c->c[3];
    v.x = floorf((c->c[0] * iw * cl->xs + (float)cl->vx) + 0.5f) / 16.0f;
    v.y = floorf((c->c[1] * iw * cl->ys + (float)cl->vy) + 0.5f) / 16.0f;
    v.z = c->c[2] * iw * cl->zs + cl->zo;
    v.iw = iw;
    for (int j = 0; j < nvary; j++)
        v.v[j] = c->b[0] * t[0].v[j] + c->b[1] * t[1].v[j] + c->b[2] * t[2].v[j];
    return v;
}

static eprim_t *prims;
static int nprims, cap;
static uint32_t bin_alloc, bin_tsda;
static int bin_tx, bin_ty, bin_ms;


static int shader_of(const uint8_t *code)
{
    if (!memcmp(code, fs_colour, sizeof fs_colour)) return SH_COLOUR;
    if (!memcmp(code, fs_tex_lit, sizeof fs_tex_lit)) return SH_TEX;
    if (!memcmp(code, fs_tex_lit_alpha, sizeof fs_tex_lit_alpha)) return SH_TEX_ALPHA;
    if (!memcmp(code, fs_colour_screen, sizeof fs_colour_screen)) return SH_SCREEN;
    if (!memcmp(code, fs_tex_rgb, sizeof fs_tex_rgb)) return SH_TEX_RGB;
    if (!memcmp(code, fs_tex_rgb_alpha, sizeof fs_tex_rgb_alpha)) return SH_TEX_RGB_ALPHA;
    if (!memcmp(code, fs_zclear, sizeof fs_zclear)) return SH_ZCLEAR;
    if (!memcmp(code, fs_tex_lit_screen, sizeof fs_tex_lit_screen)) return SH_TEX_SCREEN;
    if (!memcmp(code, fs_tex_rgb_screen, sizeof fs_tex_rgb_screen)) return SH_TEX_RGB_SCREEN;
    if (!memcmp(code, fs_text, sizeof fs_text)) return SH_TEXT;
    if (!memcmp(code, fs_tex_lit_t, sizeof fs_tex_lit_t)) return SH_TEX;       /* M39: two threads */
    if (!memcmp(code, fs_tex_rgb_t, sizeof fs_tex_rgb_t)) return SH_TEX_RGB;
    if (!memcmp(code, fs_colour_t, sizeof fs_colour_t)) return SH_COLOUR;      /* bm3d 6.3 */
    if (!memcmp(code, fs_colour_screen_t, sizeof fs_colour_screen_t)) return SH_SCREEN;
    if (!memcmp(code, fs_tex_lit_alpha_t, sizeof fs_tex_lit_alpha_t)) return SH_TEX_ALPHA;
    if (!memcmp(code, fs_tex_lit_screen_t, sizeof fs_tex_lit_screen_t)) return SH_TEX_SCREEN;
    if (!memcmp(code, fs_tex_rgb_alpha_t, sizeof fs_tex_rgb_alpha_t)) return SH_TEX_RGB_ALPHA;
    if (!memcmp(code, fs_tex_rgb_screen_t, sizeof fs_tex_rgb_screen_t)) return SH_TEX_RGB_SCREEN;
    return -1;
}

/* the fragment shaders with a thread switch (lthrsw) */
static int switches_threads(const uint8_t *code)
{
    static const struct { const uint32_t *code; size_t size; } t[] = {
        { fs_tex_lit_t, sizeof fs_tex_lit_t }, { fs_tex_rgb_t, sizeof fs_tex_rgb_t },
        { fs_colour_t, sizeof fs_colour_t }, { fs_colour_screen_t, sizeof fs_colour_screen_t },
        { fs_tex_lit_alpha_t, sizeof fs_tex_lit_alpha_t }, { fs_tex_lit_screen_t, sizeof fs_tex_lit_screen_t },
        { fs_tex_rgb_alpha_t, sizeof fs_tex_rgb_alpha_t }, { fs_tex_rgb_screen_t, sizeof fs_tex_rgb_screen_t },
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        if (!memcmp(code, t[i].code, t[i].size))
            return 1;
    return 0;
}

/* M39: a fragment shader with thread switches; a record that says it
 * single-threaded (flag 0) is an error, and emu_no_threads: a GPU on which
 * they do not work (the job does not end) */
static int threaded_check(const uint8_t *rec)
{
    const uint8_t *code = ptr(rd32(rec + 4));
    if (!switches_threads(code)) {
        /* no thread switch: the record must say single-threaded (a threaded
         * shader must execute LTHRSW once before it ends; the Pi hung with
         * these mixed with the two-thread ones, 2026-10-05) */
        if (!(rec[0] & 1))
            return err("a fragment shader without thread switches in a record that says it threaded", 0, 0);
        return 0;
    }
    if (rec[0] & 1)
        return err("a fragment shader with thread switches in a record that says it single-threaded", 0, 0);
    if (emu_no_threads)
        return err("two-thread fragment shader: the job does not end (emu_no_threads)", 0, 0);
    emu_stats.threaded++;
    return 0;
}

/* M35: the binner's semaphore (INCREMENT_SEMAPHORE at the end of the
 * binning list, WAIT_ON_SEMAPHORE in the rendering list). A job started
 * with v3d_start (async) must have both: on the V3D the two threads run
 * together and the rendering would read tile lists not yet written. */
static int sem_count, emu_async;

static int bin(uint32_t start, uint32_t end)
{
    const uint8_t *p = ptr(start), *e = ptr(end);
    if (!p || !e || e < p)
        return err("binning list %08x..%08x outside memory", start, end);
    nprims = 0;
    int cfg_seen = 0, started = 0, flushed = 0, depth_func = -1, z_update = 0, oversample = 0, faces = 3, early = 0;
    int clip[4] = { -1, 0, 0, 0 };
    const uint8_t *rec = NULL, *glrec = NULL;
    int glattr = 0, glclip = 0, xy_set = 0, z_set = 0;
    clipper_t cl;
    memset(&cl, 0, sizeof cl);
    static evert_t *glv;
    static float (*glc)[4];
    static uint32_t glcap;
    while (p < e) {
        uint8_t id = *p++;
        if (flushed && id != 1 && id != 0)
            return err("binning list: packet %u after FLUSH", id, 0);
        switch (id) {
        case 0: p = e; break;                                   /* HALT */
        case 1: break;                                          /* NOP */
        case 4: flushed = 1; break;                             /* FLUSH */
        case 7: sem_count++; break;                             /* INCREMENT_SEMAPHORE */
        case 112:                                               /* TILE_BINNING_MODE_CONFIG */
            bin_alloc = rd32(p); bin_tsda = rd32(p + 8);
            bin_tx = p[12]; bin_ty = p[13];
            if (!ptr(bin_alloc) || !ptr(bin_tsda) || (bin_alloc >> 28) != (bin_tsda >> 28))
                return err("binning memory %08x / tile state %08x", bin_alloc, bin_tsda);
            if ((p[14] & 0x04) == 0 || (p[14] >> 3 & 3) != 0 || (p[14] & 0x82))
                return err("binning flags %02x: auto tile state, first blocks of 32", p[14], 0);
            bin_ms = p[14] & 1;
            cfg_seen = 1;
            p += 15;
            break;
        case 6:                                                 /* START_TILE_BINNING */
            if (!cfg_seen)
                return err("START_TILE_BINNING before the binning configuration", 0, 0);
            started = 1;
            break;
        case 103:                                               /* VIEWPORT_OFFSET */
            cl.vx = (int16_t)rd16(p);
            cl.vy = (int16_t)rd16(p + 2);
            p += 4;
            break;
        case 104:                                               /* Z_MIN_MAX_CLIPPING_PLANES */
            cl.zmin = rdf(p);
            cl.zmax = rdf(p + 4);
            cl.zplanes = 1;
            p += 8;
            break;
        case 105:                                               /* CLIPPER_XY_SCALING */
            cl.xs = rdf(p);
            cl.ys = rdf(p + 4);
            xy_set = cl.xs != 0 && cl.ys != 0;
            p += 8;
            break;
        case 106:                                               /* CLIPPER_Z_SCALING */
            cl.zs = rdf(p);
            cl.zo = rdf(p + 4);
            z_set = 1;
            p += 8;
            break;
        case 102:                                               /* CLIP_WINDOW */
            clip[0] = rd16(p); clip[1] = rd16(p + 2); clip[2] = rd16(p + 4); clip[3] = rd16(p + 6);
            p += 8;
            break;
        case 96:                                                /* CONFIGURATION_BITS */
            if ((p[0] & 3) == 0)
                return err("configuration %02x: no faces", p[0], 0);
            faces = p[0] & 7;
            depth_func = rd16(p + 1) >> 4 & 7;
            z_update = rd16(p + 1) >> 7 & 1;
            early = rd16(p + 1) >> 8 & 3;
            oversample = p[0] >> 6 & 3;
            p += 3;
            break;
        case 65:                                                /* NV_SHADER_STATE */
            rec = ptr(rd32(p));
            glrec = NULL;
            if (!rec || (rd32(p) & 15))
                return err("shader record %08x not 16-byte aligned", rd32(p), 0);
            p += 4;
            break;
        case 64:                                                /* GL_SHADER_STATE */
            glrec = ptr(rd32(p) & ~15u);
            glattr = (rd32(p) & 7) ? (int)(rd32(p) & 7) : 8;
            rec = NULL;
            if (!glrec || (rd32(p) & 8))
                return err("GL shader record %08x (extended records not emulated)", rd32(p), 0);
            if (rd16(glrec) & ~5u)
                return err("GL record flags %04x: point size not emulated", rd16(glrec), 0);
            glclip = rd16(glrec) >> 2 & 1;
            p += 4;
            break;
        case 32:                                                /* INDEXED_PRIMITIVE_LIST (M39) */
        case 33: {                                              /* VERTEX_ARRAY_PRIMITIVES */
            const int indexed = id == 32;
            uint32_t n = rd32(p + 1), first = indexed ? 0 : rd32(p + 5), maxi = indexed ? rd32(p + 9) : 0;
            const uint16_t *ix = indexed ? ptr(rd32(p + 5)) : NULL;
            if (!started || (!rec && !glrec) || depth_func < 0 || clip[0] < 0)
                return err("primitives before the state (started %u, record %u)", started, rec || glrec);
            if ((p[0] & 15) != 4 || n % 3 || n > 65535 || (indexed && (p[0] >> 4) != 1) || (!indexed && p[0] != 4))
                return err("primitives: mode %02x, %u vertices", p[0], n);
            if (indexed && (!glrec || !ix || (rd32(p + 5) & 1) || maxi > 65535))
                return err("indexed primitives: GL only, 16-bit indices in memory (at %08x, max %u)", rd32(p + 5),
                           maxi);
            if (glrec) {
                const int sh = shader_of(ptr(rd32(glrec + 4))), nvary = glrec[3];
                if (threaded_check(glrec) != 0)
                    return -1;
                if (sh < 0 || nvary > 8)
                    return err("GL record: fragment shader at %08x, %u varyings", rd32(glrec + 4), nvary);
                const uint32_t nshade = indexed ? maxi + 1 : n;     /* the corners shaded */
                if (nshade + n > glcap) {
                    glcap = nshade + n;
                    glv = realloc(glv, glcap * sizeof *glv);
                    glc = realloc(glc, glcap * sizeof *glc);
                }
                if (gl_vertices(glrec, glattr, first, nshade, nvary, glv, glc) != 0)
                    return -1;
                if (indexed) {
                    /* each index's corner in a row after the shaded ones,
                     * then the triangles of the list as for an array */
                    for (uint32_t i = 0; i < n; i++) {
                        if (ix[i] > maxi)
                            return err("index %u above the maximum %u", ix[i], maxi);
                        glv[nshade + i] = glv[ix[i]];
                        memcpy(glc[nshade + i], glc[ix[i]], sizeof glc[0]);
                    }
                    memmove(glv, glv + nshade, n * sizeof *glv);
                    memmove(glc, glc + nshade, n * sizeof *glc);
                    emu_stats.glindexed += n;
                    /* a model of the VCD's cache of shaded corners (16, first in first out):
                     * the corners shaded again for this draw's order of indices (M39) */
                    uint16_t fifo[16];
                    int nf = 0, head = 0;
                    for (uint32_t i = 0; i < n; i++) {
                        int hit = 0;
                        for (int k = 0; k < nf && !hit; k++)
                            hit = fifo[k] == ix[i];
                        if (hit)
                            continue;
                        emu_stats.vmiss++;
                        if (nf < 16)
                            fifo[nf++] = ix[i];
                        else {
                            fifo[head] = ix[i];
                            head = (head + 1) % 16;
                        }
                    }
                }
                if (glclip && (!xy_set || !z_set))
                    return err("GL clipping without CLIPPER_XY_SCALING (%u) or CLIPPER_Z_SCALING (%u)", xy_set, z_set);
                for (uint32_t i = 0; i < n; i++) {
                    glv[i].x += cl.vx / 16.0f;
                    glv[i].y += cl.vy / 16.0f;
                }
                const int colour = sh == SH_COLOUR || sh == SH_SCREEN;
                const uint32_t *params = colour ? NULL : ptr(rd32(glrec + 8));
                for (uint32_t i = 0; i < n; i += 3) {
                    evert_t tri[9];
                    cvert_t poly[9];
                    int np = glclip && emu_clip != 1 ? clip_tri(&cl, (const float (*)[4])&glc[i], poly) : -1;
                    if (np < 0) {
                        if (!faces_kept(faces, &glv[i]))
                            continue;
                        memcpy(tri, &glv[i], 3 * sizeof *tri);
                        np = 3;
                    } else {
                        float area = 0;
                        for (int k = 0; k < np; k++)
                            tri[k] = clip_corner(&cl, &poly[k], &glv[i], nvary);
                        for (int k = 0; k < np; k++) {
                            const evert_t *a = &tri[k], *b = &tri[(k + 1) % np];
                            area += a->x * b->y - b->x * a->y;
                        }
                        if (np < 3 || !area_kept(faces, area))
                            continue;
                    }
                    for (int k = 1; k + 1 < np; k++) {
                        if (nprims == cap) {
                            cap = cap ? cap * 2 : 4096;
                            prims = realloc(prims, (size_t)cap * sizeof *prims);
                        }
                        eprim_t *pr = &prims[nprims++];
                        pr->v[0] = tri[0];
                        pr->v[1] = tri[k];
                        pr->v[2] = tri[k + 1];
                        pr->shader = sh;
                        pr->nvary = nvary;
                        pr->params = params;
                        pr->depth_func = depth_func;
                        pr->z_update = z_update;
                        pr->early = early;
                        pr->oversample = oversample;
                        memcpy(pr->clip, clip, sizeof clip);
                    }
                }
                emu_stats.prims += n / 3;
                emu_stats.batches++;
                p += indexed ? 13 : 9;
                break;
            }
            int sh = shader_of(ptr(rd32(rec + 4)));
            if (sh < 0)
                return err("unknown shader at %08x", rd32(rec + 4), 0);
            if (threaded_check(rec) != 0)
                return -1;
            const int colour = sh == SH_COLOUR || sh == SH_SCREEN || sh == SH_ZCLEAR,
                      rgb = sh == SH_TEX_RGB || sh == SH_TEX_RGB_ALPHA || sh == SH_TEX_RGB_SCREEN;
            int stride = rec[1], nvary = rec[3];
            if (sh == SH_ZCLEAR && emu_hang_zclear)
                return err("fs_zclear: the job does not end (emu_hang_zclear)", 0, 0);
            if (nvary != (rgb ? 8 : sh == SH_ZCLEAR ? 0 : sh == SH_TEXT ? 5 : 3) || stride != 12 + 4 * nvary ||
                rec[2] != (colour ? 0 : 2))
                return err("shader record: stride %u, varyings %u", stride, nvary);
            const uint8_t *vb = ptr(rd32(rec + 12));
            const uint32_t *params = colour ? NULL : ptr(rd32(rec + 8));
            if (!vb || (!colour && !params) || (rd32(rec + 12) & 3))
                return err("vertices %08x, uniforms %08x", rd32(rec + 12), rd32(rec + 8));
            for (uint32_t i = 0; i < n; i += 3) {
                if (faces != 3) {
                    evert_t t[3];
                    for (int k = 0; k < 3; k++) {
                        const uint8_t *v = vb + (size_t)(first + i + (uint32_t)k) * (size_t)stride;
                        t[k].x = ((int16_t)rd16(v) + cl.vx) / 16.0f;
                        t[k].y = ((int16_t)rd16(v + 2) + cl.vy) / 16.0f;
                    }
                    if (!faces_kept(faces, t))
                        continue;
                }
                if (nprims == cap) {
                    cap = cap ? cap * 2 : 4096;
                    prims = realloc(prims, (size_t)cap * sizeof *prims);
                }
                eprim_t *pr = &prims[nprims++];
                for (int k = 0; k < 3; k++) {
                    const uint8_t *v = vb + (size_t)(first + i + (uint32_t)k) * (size_t)stride;
                    pr->v[k] = (evert_t){ ((int16_t)rd16(v) + cl.vx) / 16.0f, ((int16_t)rd16(v + 2) + cl.vy) / 16.0f,
                                          rdf(v + 4), rdf(v + 8), { 0 } };
                    for (int j = 0; j < nvary; j++)
                        pr->v[k].v[j] = rdf(v + 12 + 4 * j);
                }
                pr->shader = sh;
                pr->nvary = nvary;
                pr->params = params;
                pr->depth_func = depth_func;
                pr->z_update = z_update;
                pr->early = early;
                pr->oversample = oversample;
                memcpy(pr->clip, clip, sizeof clip);
            }
            emu_stats.prims += n / 3;
            emu_stats.batches++;
            p += 9;
            break;
        }
        default:
            return err("binning list: unknown packet %u", id, 0);
        }
    }
    if (!flushed)
        return err("binning list without FLUSH", 0, 0);
    return 0;
}

/* ---------------------------------------------------------------- rendering */

/* tile buffer: bytes a b c d and depth; 64x64 pixels, or with MSAA 32x32
 * pixels of 4 samples (sample s of pixel (x, y) at (y * 32 + x) * 4 + s) */
static uint8_t tcol[64 * 64][4];
static uint32_t tz[64 * 64];
/* the early z test's own idea of the depth (as the FEP keeps it): set by
 * clears and depth loads, then written only by primitives with early z
 * updates; primitives with early z are thrown away when they are not
 * nearer than it, before their shader. So a depth the shader writes
 * farther (fs_zclear) leaves it nearer than the depth buffer: what comes
 * after with early z is lost (the Pi, 2026-10-05). */
static uint32_t tez[64 * 64];
static int ms, TS = 64, NS = 1;         /* the frame's mode, tile size, samples */
static uint32_t clear_col, clear_z;

static void tile_clear(void)
{
    for (int i = 0; i < 64 * 64; i++) {
        memcpy(tcol[i], &clear_col, 4);
        tz[i] = tez[i] = clear_z;
    }
}

static uint16_t to565(const uint8_t *c)
{
    const uint8_t *r = emu_red_a ? &c[0] : &c[2], *b = emu_red_a ? &c[2] : &c[0];
    return (uint16_t)((*r >> 3) << 11 | (c[1] >> 2) << 5 | (*b >> 3));
}

static void from565(uint16_t v, uint8_t *c)
{
    uint32_t r = v >> 11, g = v >> 5 & 63, b = v & 31;
    uint8_t r8 = (uint8_t)(r << 3 | r >> 2), g8 = (uint8_t)(g << 2 | g >> 4), b8 = (uint8_t)(b << 3 | b >> 2);
    c[0] = emu_red_a ? r8 : b8;
    c[1] = g8;
    c[2] = emu_red_a ? b8 : r8;
    c[3] = 255;
}

static uint8_t unit8(float f)
{
    return (uint8_t)(f <= 0 ? 0 : f >= 1 ? 255 : f * 255.0f + 0.5f);
}

static uint32_t t_index(int x, int y, int w);
static uint32_t t16_index(int x, int y, int w);

/* a texel of the texture at (x, y) as bytes a b c d: RGBA32R in rows,
 * RGBA8888 in T-format, or (M39) RGB565 in T-format, its top five bits in
 * byte c and its low five in byte a, alpha full; bytes a and c swapped with
 * emu_tex_swap */
static uint32_t texel_at(const uint32_t *tex, int type, int x, int y, int w)
{
    uint32_t t;
    if (type == 4) {
        const uint16_t v = ((const uint16_t *)tex)[t16_index(x, y, w)];
        const uint32_t hi = v >> 11, g = v >> 5 & 63, lo = v & 31;
        t = 0xFF000000u | (hi << 3 | hi >> 2) << 16 | (g << 2 | g >> 4) << 8 | (lo << 3 | lo >> 2);
    } else {
        t = type == 16 ? tex[y * w + x] : tex[t_index(x, y, w)];
    }
    if (emu_tex_swap)
        t = (t & 0xFF00FF00u) | (t >> 16 & 0xFF) | (t & 0xFF) << 16;
    return t;
}

/* v8muld: a * b / 255, as the QPU rounds it */
static uint8_t muld(uint32_t a, uint32_t b)
{
    const uint32_t x = a * b + 127;
    return (uint8_t)((x + 1 + (x >> 8)) >> 8);
}

static uint8_t adds(uint32_t a, uint32_t b)
{
    return (uint8_t)(a + b > 255 ? 255 : a + b);
}

static void shade(const eprim_t *pr, const float *va, uint8_t *out, int *discard, int px, int py)
{
    *discard = 0;
    if (pr->shader == SH_COLOUR || pr->shader == SH_SCREEN) {
        out[0] = unit8(va[0]); out[1] = unit8(va[1]); out[2] = unit8(va[2]); out[3] = 255;
        if (pr->shader == SH_SCREEN && ((px + py) & 1))
            *discard = 1;               /* only the pixels with x + y even */
        return;
    }
    uint32_t p0 = pr->params[0], p1 = pr->params[1];
    int w = (int)(p1 >> 8 & 2047), h = (int)(p1 >> 20 & 2047);
    if (!w) w = 2048;
    if (!h) h = 2048;
    const uint32_t *tex = ptr(p0 & ~0xFFFu);
    const int type = (int)(p0 >> 4 & 15) | (int)(p1 >> 31) << 4;
    uint8_t c[4];
    if (!(p1 >> 7 & 1) && !(p1 >> 4 & 7)) {
        /* M37: bilinear (MAGFILT and MINFILT linear): the four texels
         * around the point, clamped at the edges */
        const float fx = va[0] * (float)w - 0.5f, fy = va[1] * (float)h - 0.5f;
        const int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
        const float ax = fx - (float)x0, ay = fy - (float)y0;
        float acc[4] = { 0, 0, 0, 0 };
        for (int k = 0; k < 4; k++) {
            int x = x0 + (k & 1), y = y0 + (k >> 1);
            x = x < 0 ? 0 : x >= w ? w - 1 : x;
            y = y < 0 ? 0 : y >= h ? h - 1 : y;
            const uint32_t t = texel_at(tex, type, x, y, w);
            const float wk = ((k & 1) ? ax : 1 - ax) * ((k >> 1) ? ay : 1 - ay);
            for (int i = 0; i < 4; i++)
                acc[i] += wk * (float)(t >> (8 * i) & 255);
        }
        for (int i = 0; i < 4; i++)
            c[i] = (uint8_t)(acc[i] + 0.5f);
    } else {
        int tx = (int)floorf(va[0] * (float)w), ty = (int)floorf(va[1] * (float)h);
        tx = tx < 0 ? 0 : tx >= w ? w - 1 : tx;                 /* clamp */
        ty = ty < 0 ? 0 : ty >= h ? h - 1 : ty;
        const uint32_t t = texel_at(tex, type, tx, ty, w);
        c[0] = (uint8_t)t; c[1] = (uint8_t)(t >> 8); c[2] = (uint8_t)(t >> 16); c[3] = (uint8_t)(t >> 24);
    }
    if (pr->shader == SH_TEXT) {        /* M37: the colour where the glyph's texel is opaque */
        out[0] = unit8(va[2]); out[1] = unit8(va[3]); out[2] = unit8(va[4]); out[3] = 255;
        if (c[3] == 0)
            *discard = 1;
        return;
    }
    /* the textured screen-door shaders (M34): the even pixels, opaque texels */
    const int screen = pr->shader == SH_TEX_SCREEN || pr->shader == SH_TEX_RGB_SCREEN;
    if (screen && (((px + py) & 1) || c[3] == 0))
        *discard = 1;
    if (pr->shader == SH_TEX_RGB || pr->shader == SH_TEX_RGB_ALPHA || pr->shader == SH_TEX_RGB_SCREEN) {
        /* texel * light / 2, * 2, + fog: bytes a b c (light d = 1, fog d = 0) */
        for (int i = 0; i < 4; i++) {
            const uint8_t m = muld(c[i], i < 3 ? unit8(va[2 + i]) : 255);
            out[i] = adds(adds(m, m), i < 3 ? unit8(va[5 + i]) : 0);
        }
        if (pr->shader == SH_TEX_RGB_ALPHA && c[3] == 0)
            *discard = 1;
        return;
    }
    uint32_t k = unit8(va[2]);
    for (int i = 0; i < 4; i++) {
        const uint32_t x = c[i] * k + 127;
        out[i] = (uint8_t)((x + 1 + (x >> 8)) >> 8);              /* v8muld: x / 255 */
    }
    if (pr->shader == SH_TEX_ALPHA && c[3] == 0)
        *discard = 1;
}

/* the word of texel (x, y) in a T-format texture w wide, as the emulator
 * lays it out: 4 KiB tiles of 32x32 texels in rows, odd rows right to
 * left; in a tile four 1 KiB subtiles of 16x16, in a C (even rows of
 * tiles) or a reversed C (odd rows), or in raster order (emu_tformat 1:
 * the backend must learn either); in a subtile 4x4 utiles of 4x4 texels
 * in raster order. emu_tformat 2: rows, which the backend must refuse. */
static uint32_t t_index(int x, int y, int w)
{
    static const uint8_t even[2][2] = { { 0, 3 }, { 1, 2 } }, odd[2][2] = { { 2, 1 }, { 3, 0 } },
                         plain[2][2] = { { 0, 1 }, { 2, 3 } };        /* [sy][sx] */
    if (emu_tformat == 2)
        return (uint32_t)(y * w + x);   /* a TMU that reads it in rows: no T-format */
    const int tpr = (w + 31) / 32, ty = y / 32;
    int tx = x / 32;
    if (ty & 1)
        tx = tpr - 1 - tx;
    const int sx = x / 16 & 1, sy = y / 16 & 1;
    const int sub = emu_tformat ? plain[sy][sx] : (ty & 1 ? odd : even)[sy][sx];
    return (uint32_t)(ty * tpr + tx) * 1024u + (uint32_t)sub * 256u +
           (uint32_t)((y / 4 & 3) * 4 + (x / 4 & 3)) * 16u + (uint32_t)((y & 3) * 4 + (x & 3));
}

/* the 16-bit place of texel (x, y) in an RGB565 T-format texture (M39), as
 * the emulator lays it out: tiles of 4 KiB, 64 x 32 texels, in rows, odd
 * rows right to left; in a tile four 1 KiB subtiles of 32 x 16 in a C (or
 * a reversed C, or raster with emu_tformat 1); in a subtile 4 x 4 utiles of
 * 8 x 4 texels, raster */
static uint32_t t16_index(int x, int y, int w)
{
    static const uint8_t even[2][2] = { { 0, 3 }, { 1, 2 } }, odd[2][2] = { { 2, 1 }, { 3, 0 } },
                         plain[2][2] = { { 0, 1 }, { 2, 3 } };
    const int tpr = (w + 63) / 64, ty = y / 32;
    int tx = x / 64;
    if (ty & 1)
        tx = tpr - 1 - tx;
    const int sx = x / 32 & 1, sy = y / 16 & 1;
    const int sub = emu_tformat ? plain[sy][sx] : (ty & 1 ? odd : even)[sy][sx];
    return (uint32_t)(ty * tpr + tx) * 2048u + (uint32_t)sub * 512u +
           (uint32_t)((y / 4 & 3) * 4 + (x / 8 & 3)) * 32u + (uint32_t)((y & 3) * 8 + (x & 7));
}

static float edge(const evert_t *a, const evert_t *b, float x, float y)
{
    return (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
}

static void draw_tile(int tx, int ty, int fw, int fh)
{
    /* sample positions in a pixel: its centre, or MSAA's rotated grid */
    static const float one[1][2] = { { 0.5f, 0.5f } },
                       four[4][2] = { { 0.375f, 0.125f }, { 0.875f, 0.375f }, { 0.125f, 0.625f }, { 0.625f, 0.875f } };
    const float (*sp)[2] = ms ? four : one;
    for (int n = 0; n < nprims; n++) {
        const eprim_t *pr = &prims[n];
        const evert_t *v = pr->v;
        float area = edge(&v[0], &v[1], v[2].x, v[2].y);
        if (area == 0)
            continue;
        int x0 = tx * TS, y0 = ty * TS, x1 = x0 + TS, y1 = y0 + TS;
        if (x1 > fw) x1 = fw;
        if (y1 > fh) y1 = fh;
        /* only the pixels of the triangle's bounding box */
        float bx0 = fminf(v[0].x, fminf(v[1].x, v[2].x)), bx1 = fmaxf(v[0].x, fmaxf(v[1].x, v[2].x));
        float by0 = fminf(v[0].y, fminf(v[1].y, v[2].y)), by1 = fmaxf(v[0].y, fmaxf(v[1].y, v[2].y));
        if ((float)x0 < bx0 - 1) x0 = (int)floorf(bx0 - 1);
        if ((float)y0 < by0 - 1) y0 = (int)floorf(by0 - 1);
        if ((float)x1 > bx1 + 1) x1 = (int)ceilf(bx1 + 1);
        if ((float)y1 > by1 + 1) y1 = (int)ceilf(by1 + 1);
        if (x0 < pr->clip[0]) x0 = pr->clip[0];
        if (y0 < pr->clip[1]) y0 = pr->clip[1];
        if (x1 > pr->clip[0] + pr->clip[2]) x1 = pr->clip[0] + pr->clip[2];
        if (y1 > pr->clip[1] + pr->clip[3]) y1 = pr->clip[1] + pr->clip[3];
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++) {
                const int pix = ((y - ty * TS) * TS + (x - tx * TS)) * NS;
                uint8_t c[4];
                int shaded = 0, discard = 0;
                for (int s = 0; s < NS; s++) {
                    float px = x + sp[s][0], py = y + sp[s][1];
                    float w0 = edge(&v[1], &v[2], px, py) / area, w1 = edge(&v[2], &v[0], px, py) / area,
                          w2 = edge(&v[0], &v[1], px, py) / area;
                    if (w0 < 0 || w1 < 0 || w2 < 0)
                        continue;
                    float zs = w0 * v[0].z + w1 * v[1].z + w2 * v[2].z;
                    uint32_t zz = (uint32_t)(zs <= 0 ? 0 : zs >= 1 ? 0xFFFFFF : zs * 16777215.0f);
                    const int i = pix + s;
                    if (pr->early & 1) {        /* early z: less than, as the frame's direction */
                        if (!(zz < tez[i]))
                            continue;
                        if (pr->early & 2)
                            tez[i] = zz;
                    }
                    int pass = pr->depth_func == 7 || (pr->depth_func == 1 && zz < tz[i]) ||
                               (pr->depth_func == 3 && zz <= tz[i]);
                    if (!pass)
                        continue;
                    if (pr->shader == SH_ZCLEAR) {  /* fs_zclear: the colour loaded and written back */
                        if (pr->z_update)
                            tz[i] = zz;
                        continue;
                    }
                    if (!shaded) {              /* once a pixel, at its centre */
                        float cx = x + 0.5f, cy = y + 0.5f;
                        float c0 = edge(&v[1], &v[2], cx, cy) / area, c1 = edge(&v[2], &v[0], cx, cy) / area,
                              c2 = edge(&v[0], &v[1], cx, cy) / area;
                        float iw = c0 * v[0].iw + c1 * v[1].iw + c2 * v[2].iw;
                        float va[8];
                        for (int k = 0; k < pr->nvary; k++)
                            va[k] = (c0 * v[0].v[k] * v[0].iw + c1 * v[1].v[k] * v[1].iw + c2 * v[2].v[k] * v[2].iw) / iw;
                        shade(pr, va, c, &discard, x, y);
                        shaded = 1;
                        emu_stats.pixels++;
                    }
                    if (discard)
                        break;
                    memcpy(tcol[i], c, 4);
                    if (pr->z_update)
                        tz[i] = zz;
                }
            }
    }
}

/* a depth buffer in T-format as the emulator lays it out: 4 KiB tiles of
 * 32x32 pixels, a row of tiles after the other (the real order inside
 * differs, the size is the same); NULL if it leaves memory */
static uint32_t toff(int x, int y, int fw)
{
    const int tw = (fw + 31) / 32;
    return (uint32_t)(((y / 32) * tw + x / 32) * 1024 + (y % 32) * 32 + x % 32);
}

static uint32_t *zbuf_at(uint32_t a, int fw, int fh)
{
    const uint32_t size = (uint32_t)((fw + 31) / 32) * (uint32_t)((fh + 31) / 32) * 4096u;
    if ((a & 0xFFF) || !ptr(a) || !ptr(a + size - 1))
        return NULL;
    return ptr(a);
}

static int render(uint32_t start, uint32_t end, int have_bin)
{
    const uint8_t *p = ptr(start), *e = ptr(end);
    if (!p || !e || e < p)
        return err("rendering list %08x..%08x outside memory", start, end);
    uint16_t *fb = NULL;
    int fw = 0, fh = 0, tx = -1, ty = -1, load = 0, zload = 0, eof = 0, have_cfg = 0, tiles = 0;
    int loaded = 0;                     /* a load took place: a store before the next load */
    int tile_loaded = 0;                /* the colour was loaded for this tile */
    int waited = 0;                     /* WAIT_ON_SEMAPHORE seen */
    uint32_t load_addr = 0, zload_addr = 0;
    ms = 0, TS = 64, NS = 1;
    while (p < e) {
        uint8_t id = *p++;
        if (eof && id != 1)
            return err("rendering list: packet %u after the end of frame", id, 0);
        switch (id) {
        case 1: break;
        case 8:                                                 /* WAIT_ON_SEMAPHORE */
            if (sem_count <= 0)
                return err("WAIT_ON_SEMAPHORE with no INCREMENT_SEMAPHORE from the binner", 0, 0);
            sem_count--;
            waited = 1;
            break;
        case 114:                                               /* CLEAR_COLORS */
            clear_col = rd32(p); clear_z = rd32(p + 8) & 0xFFFFFF;
            p += 13;
            break;
        case 113: {                                             /* TILE_RENDERING_MODE_CONFIG */
            const uint16_t flags = rd16(p + 8);
            fb = ptr(rd32(p)); fw = rd16(p + 4); fh = rd16(p + 6);
            if (!fb || (rd32(p) & 15) || (flags >> 2 & 3) != 2 || (flags & 0xFFC2))
                return err("frame %08x, flags %04x (BGR565 expected)", rd32(p), flags);
            ms = flags & 1;             /* MSAA 4x, resolved (decimated 4x) at the store */
            emu_stats.msframes += (uint32_t)ms;
            if ((flags >> 4 & 3) != (unsigned)ms)
                return err("flags %04x: MSAA needs decimate 4x, and only MSAA", flags, 0);
            TS = ms ? 32 : 64;
            NS = ms ? 4 : 1;
            if (have_bin && (bin_ms != ms || bin_tx != (fw + TS - 1) / TS || bin_ty != (fh + TS - 1) / TS))
                return err("binning in tiles of %u, rendering in tiles of %u", bin_ms ? 32 : 64, (unsigned)TS);
            have_cfg = 1;
            tile_clear();
            p += 10;
            break;
        }
        case 29:                                                /* LOAD_TILE_BUFFER_GENERAL */
            if (load || zload || loaded)
                return err("load %04x: a load is pending (tile coordinates and a store first)", rd16(p), 0);
            if (rd16(p) == 0x0201) {
                load = 1; load_addr = rd32(p + 2);
                emu_stats.loads++;
            } else if (rd16(p) == 0x0012 && !ms) {
                zload = 1; zload_addr = rd32(p + 2);
            } else {
                return err("load %04x: colour (raster BGR565) or depth (T-format, no MSAA) expected", rd16(p), 0);
            }
            p += 6;
            break;
        case 115:                                               /* TILE_COORDINATES */
            tx = p[0]; ty = p[1];
            p += 2;
            if (load) {
                const uint16_t *src = ptr(load_addr);
                if (!src)
                    return err("load from %08x", load_addr, 0);
                for (int y = 0; y < TS; y++)
                    for (int x = 0; x < TS; x++) {
                        int X = tx * TS + x, Y = ty * TS + y;
                        if (X < fw && Y < fh)
                            for (int s = 0; s < NS; s++)
                                if (!(s && emu_ms_load_one))
                                    from565(src[Y * fw + X], tcol[(y * TS + x) * NS + s]);
                    }
                load = 0;
                loaded = 1;
                tile_loaded = 1;
            }
            if (zload) {
                const uint32_t *src = zbuf_at(zload_addr, fw, fh);
                if (!src)
                    return err("depth load from %08x", zload_addr, 0);
                for (int y = 0; y < 64; y++)
                    for (int x = 0; x < 64; x++) {
                        int X = tx * 64 + x, Y = ty * 64 + y;
                        if (X < fw && Y < fh)
                            tz[y * 64 + x] = tez[y * 64 + x] = src[toff(X, Y, fw)] >> 8;
                    }
                zload = 0;
                loaded = 1;
            }
            break;
        case 28: {                                              /* STORE_TILE_BUFFER_GENERAL */
            uint16_t bits = rd16(p);
            uint32_t a = rd32(p + 2);
            if (load || zload)
                return err("general store %04x with a load pending", bits, 0);
            if ((bits & 0x1FFF) == 0 && a == 0) {               /* a store of nothing */
            } else if ((bits & 0x1FFF) == 0x0012 && !(a & 15) && !ms) {   /* depth, T-format */
                uint32_t *dst = zbuf_at(a, fw, fh);
                if (!dst || tx < 0)
                    return err("depth store to %08x", a, 0);
                for (int y = 0; y < 64; y++)
                    for (int x = 0; x < 64; x++) {
                        int X = tx * 64 + x, Y = ty * 64 + y;
                        if (X < fw && Y < fh)
                            dst[toff(X, Y, fw)] = tz[y * 64 + x] << 8;
                    }
                emu_stats.zstores++;
            } else {
                return err("general store %04x %08x: nothing, or the depth in T-format (no MSAA)", bits, a);
            }
            for (int i = 0; i < 64 * 64; i++) {             /* the clears not turned off */
                if (!(bits & 1u << 13))
                    memcpy(tcol[i], &clear_col, 4);
                if (!(bits & 1u << 14))
                    tz[i] = tez[i] = clear_z;
            }
            loaded = 0;
            p += 6;
            break;
        }
        case 17: {                                              /* BRANCH_TO_SUB_LIST */
            if (emu_async && have_bin && !waited)
                return err("a started job reads the tile lists before waiting on the binner", 0, 0);
            uint32_t a = rd32(p);
            if (!have_bin || a != bin_alloc + (uint32_t)(ty * bin_tx + tx) * 32)
                return err("branch to %08x for tile %u", a, (unsigned)(ty * 100 + tx));
            for (int n = 0; n < nprims; n++) {
                if (prims[n].oversample != ms)
                    return err("a primitive rasterised %ux, the frame is %ux", prims[n].oversample ? 4 : 1,
                               ms ? 4 : 1);
                /* Mesa, HW-2905: after a full-resolution load with MSAA the
                 * early z tracking may hold the previous tile's values */
                if (ms && tile_loaded && (prims[n].early & 1))
                    return err("early z in an MSAA frame that loads its tiles (HW-2905)", 0, 0);
                if (ms && (prims[n].early & 1))
                    emu_stats.ms_early++;
            }
            draw_tile(tx, ty, fw, fh);
            p += 4;
            break;
        }
        case 24: case 25:                                       /* STORE_MS_TILE_BUFFER (EOF) */
            if (!have_cfg || tx < 0)
                return err("store before the configuration", 0, 0);
            for (int y = 0; y < TS; y++)
                for (int x = 0; x < TS; x++) {
                    int X = tx * TS + x, Y = ty * TS + y;
                    if (X >= fw || Y >= fh)
                        continue;
                    const uint8_t (*t)[4] = &tcol[(y * TS + x) * NS];
                    if (NS == 1) {
                        fb[Y * fw + X] = to565(t[0]);
                        continue;
                    }
                    uint8_t c[4];
                    for (int k = 0; k < 4; k++)         /* the samples' mean */
                        c[k] = (uint8_t)((t[0][k] + t[1][k] + t[2][k] + t[3][k] + 2) >> 2);
                    fb[Y * fw + X] = to565(c);
                }
            tile_clear();
            tiles++;
            loaded = 0;
            tile_loaded = 0;
            eof = id == 25;
            break;
        default:
            return err("rendering list: unknown packet %u", id, 0);
        }
    }
    if (!eof)
        return err("rendering list without end of frame", 0, 0);
    if (tiles != ((fw + TS - 1) / TS) * ((fh + TS - 1) / TS))
        return err("%u tiles stored, frame of %u", (unsigned)tiles, (unsigned)(((fw + TS - 1) / TS) * ((fh + TS - 1) / TS)));
    return 0;
}

static int async_busy;

int v3d_run(uint32_t bin_start, uint32_t bin_end, uint32_t rnd, uint32_t rnd_end, uint32_t timeout_us,
            uint32_t *bin_us, uint32_t *rnd_us)
{
    (void)timeout_us;
    if (async_busy && !emu_async)
        return err("v3d_run with a started job not waited for (the V3D runs one at a time)", 0, 0);
    if (bin_us) *bin_us = 1;
    if (rnd_us) *rnd_us = 1;
    emu_stats.jobs++;
    if (emu_skip)
        return 0;
    int have_bin = bin_end != bin_start;
    sem_count = 0;
    if (have_bin && bin(bin_start, bin_end) != 0)
        return -1;
    if (!have_bin)
        nprims = 0;
    const int r = render(rnd, rnd_end, have_bin);
    if (!r && sem_count != 0)
        return err("the binner's semaphore incremented and not waited on", 0, 0);
    return r;
}

/* M35: a started job; the emulator runs it when it is waited for (M39:
 * so a driver that wrote the memory the job reads, or drew on its page,
 * before waiting for it gets a wrong picture, as on the V3D).
 * emu_stats.async counts them. */
static uint32_t async_bin, async_bin_end, async_rnd, async_rnd_end;

int v3d_start(uint32_t bin_start, uint32_t bin_end, uint32_t rnd, uint32_t rnd_end)
{
    if (async_busy) {
        err("v3d_start with a job still running", 0, 0);
        return -1;
    }
    async_bin = bin_start;
    async_bin_end = bin_end;
    async_rnd = rnd;
    async_rnd_end = rnd_end;
    async_busy = 1;
    emu_stats.async++;
    return 0;
}

int v3d_busy(void)
{
    return async_busy;
}

int v3d_wait(uint32_t timeout_us, uint32_t *bin_us, uint32_t *rnd_us)
{
    (void)timeout_us;
    if (bin_us) *bin_us = 0;
    if (rnd_us) *rnd_us = 1;
    if (!async_busy)
        return 0;
    async_busy = 0;
    emu_async = 1;
    const int r = v3d_run(async_bin, async_bin_end, async_rnd, async_rnd_end, 0, NULL, NULL);
    emu_async = 0;
    return r;
}
