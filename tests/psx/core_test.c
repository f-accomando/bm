/* The tests of the C core of bm PSX (src/bm/psx_*.c): the R3000A on small hand-assembled programs (branch and load
 * delay slots, multiply, divide, LWL/LWR, interrupts), the BIOS calls answered without a BIOS, the GTE, the GPU
 * (fill, flat triangle, textured rectangle, VRAM transfers), the DMA, the CD-ROM controller and the SPU.
 * Run: make test-psx. */
#include "psx_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, count;
#define CHECK(name, cond) do { count++; if (!(cond)) { fails++; printf("FAIL %s\n", name); } } while (0)

/* a mini assembler */
#define R_(op, rs, rt, rd, sh, fn) (((uint32_t)(op) << 26) | ((uint32_t)(rs) << 21) | ((uint32_t)(rt) << 16) | ((uint32_t)(rd) << 11) | ((uint32_t)(sh) << 6) | (uint32_t)(fn))
#define I_(op, rs, rt, imm) (((uint32_t)(op) << 26) | ((uint32_t)(rs) << 21) | ((uint32_t)(rt) << 16) | ((uint32_t)(imm) & 0xFFFF))
#define NOP 0u
#define LUI(rt, i) I_(15, 0, rt, i)
#define ORI(rt, rs, i) I_(13, rs, rt, i)
#define ADDIU(rt, rs, i) I_(9, rs, rt, i)
#define ADDU(rd, rs, rt) R_(0, rs, rt, rd, 0, 33)
#define SW(rt, off, rs) I_(43, rs, rt, off)
#define LW(rt, off, rs) I_(35, rs, rt, off)
#define LWR(rt, off, rs) I_(0x26, rs, rt, off)
#define BNE(rs, rt, off) I_(5, rs, rt, off)
#define BEQ(rs, rt, off) I_(4, rs, rt, off)
#define MULT(rs, rt) R_(0, rs, rt, 0, 0, 24)
#define DIV(rs, rt) R_(0, rs, rt, 0, 0, 26)
#define MFLO(rd) R_(0, 0, 0, rd, 0, 18)
#define MFHI(rd) R_(0, 0, 0, rd, 0, 16)
#define JR(rs) R_(0, rs, 0, 0, 0, 8)
#define JALR(rd, rs) R_(0, rs, 0, rd, 0, 9)
#define J_SELF(idx) (0x08000000u | ((((0x80010000u + (idx) * 4u) & 0x0FFFFFFFu) >> 2)))
#define MFC0(rt, rd) I_(0x10, 0, rt, (rd) << 11)
#define MTC0(rt, rd) I_(0x10, 4, rt, (rd) << 11)
#define RFE 0x42000010u
#define MFC2(rt, rd) I_(0x12, 0, rt, (rd) << 11)
#define MTC2(rt, rd) I_(0x12, 4, rt, (rd) << 11)
#define CTC2(rt, rd) I_(0x12, 6, rt, (rd) << 11)
#define GTE(op) (0x4A000000u | (op))

static uint8_t exe[0x800 + 4096];
static int load(const uint32_t *w, int n)
{
    memset(exe, 0, sizeof exe);
    memcpy(exe, "PS-X EXE", 8);
    const uint32_t pc0 = 0x80010000u, tsz = (uint32_t)n * 4;
    memcpy(exe + 0x10, &pc0, 4);
    memcpy(exe + 0x18, &pc0, 4);
    memcpy(exe + 0x1C, &tsz, 4);
    memcpy(exe + 0x800, w, tsz);
    return psx_load_exe(exe, 0x800 + tsz, NULL);
}
static uint32_t mem(uint32_t a) { return psx_peek32(a); }
static void frames(int n) { for (int i = 0; i < n; i++) psx_frame(0, 0, 0); }

static void cpu_tests(void)
{
    const uint32_t p[] = {
        LUI(3, 0x8000), ADDIU(1, 0, 10), ADDIU(2, 0, 0),
        ADDU(2, 2, 1), ADDIU(1, 1, -1), BNE(1, 0, -3), NOP,                    /* 3..6: sum 10..1 */
        SW(2, 0x1000, 3),                                                       /* 7 */
        ADDIU(5, 0, 1), BEQ(0, 0, 2), ADDIU(5, 5, 1), ADDIU(5, 5, 100),         /* 8..11: delay slot runs, 11 is skipped */
        SW(5, 0x1004, 3),                                                       /* 12 */
        ADDIU(4, 0, 7), ADDIU(8, 0, 5), SW(8, 0x1008, 3), LW(4, 0x1008, 3),     /* 13..16 */
        ADDU(6, 4, 0), ADDU(7, 4, 0), SW(6, 0x100C, 3), SW(7, 0x1010, 3),       /* 17..20: the load delay */
        ADDIU(10, 0, -7), ADDIU(11, 0, -3), MULT(10, 11), MFLO(12), SW(12, 0x1014, 3),   /* 21..25 */
        ADDIU(13, 0, 100), ADDIU(14, 0, 7), DIV(13, 14), MFLO(15), MFHI(16), SW(15, 0x1018, 3), SW(16, 0x101C, 3),   /* 26..32 */
        LUI(17, 0x1122), ORI(17, 17, 0x3344), SW(17, 0x1020, 3), LUI(18, 0), LWR(18, 0x1021, 3), NOP, SW(18, 0x1024, 3),   /* 33..39 */
        J_SELF(40), NOP,
    };
    CHECK("load program", load(p, (int)(sizeof p / 4)) == 0);
    frames(1);
    CHECK("loop sum", mem(0x1000) == 55);
    CHECK("branch delay slot", mem(0x1004) == 2);
    CHECK("load delay: old value", mem(0x100C) == 7);
    CHECK("load delay: new value", mem(0x1010) == 5);
    CHECK("mult", mem(0x1014) == 21);
    CHECK("div quotient", mem(0x1018) == 14);
    CHECK("div remainder", mem(0x101C) == 2);
    CHECK("lwr", mem(0x1024) == 0x00112233u);
}

static void hle_tests(void)
{
    static const char fmt[] = "n=%d x=%04x s=%s\n";
    static const char str[] = "ok";
    uint32_t p[128];
    int n = 0;
    p[n++] = ADDIU(9, 0, 0x3D); p[n++] = ADDIU(4, 0, 'H'); p[n++] = ORI(8, 0, 0xB0); p[n++] = JALR(31, 8); p[n++] = NOP;
    p[n++] = ADDIU(4, 0, 'i'); p[n++] = JALR(31, 8); p[n++] = NOP;
    p[n++] = ADDIU(9, 0, 0x3F);
    p[n++] = LUI(4, 0x8001); p[n++] = ORI(4, 4, 0x0100);                       /* the format at 0x80010100 */
    p[n++] = ADDIU(5, 0, -12); p[n++] = ORI(6, 0, 0xBEEF);
    p[n++] = LUI(7, 0x8001); p[n++] = ORI(7, 7, 0x0120);
    p[n++] = ORI(8, 0, 0xA0); p[n++] = JALR(31, 8); p[n++] = NOP;
    { const uint32_t at = (uint32_t)n; p[n++] = J_SELF(at); p[n++] = NOP; }
    while (n < 128) p[n++] = 0;
    memcpy((char *)p + 0x100, fmt, sizeof fmt);
    memcpy((char *)p + 0x120, str, sizeof str);
    CHECK("load hle", load(p, 128) == 0);
    frames(1);
    CHECK("putchar", strncmp(psx_tty(), "Hi", 2) == 0);
    CHECK("printf", strstr(psx_tty(), "n=-12 x=beef s=ok\n") != NULL);
}

static void irq_tests(void)
{
    const uint32_t handler[] = {
        MFC0(26, 14), LUI(27, 0x8000), SW(1, 0x1200, 27), LW(1, 0x1204, 27), NOP, ADDIU(1, 1, 1), SW(1, 0x1204, 27),
        LW(1, 0x1200, 27), LUI(27, 0x1F80), SW(0, 0x1070, 27), JR(26), RFE,
    };
    const uint32_t p[] = {
        LUI(1, 0x4000), ORI(1, 1, 0x0401), MTC0(1, 12),
        LUI(2, 0x1F80), ADDIU(3, 0, 1), SW(3, 0x1074, 2),
        J_SELF(6), NOP,
    };
    CHECK("load irq", load(p, (int)(sizeof p / 4)) == 0);
    /* the vector at 0x80 jumps to the handler (the BIOS call vectors A0, B0 and C0 are answered by the core, so the handler
     * must not sit on them) */
    const uint32_t vec[2] = { 0x08000000u | (0x1400u >> 2), NOP };
    memcpy(psx_ram + 0x80, vec, sizeof vec);
    memcpy(psx_ram + 0x1400, handler, sizeof handler);
    frames(3);
    if (mem(0x1204) != 3) printf("  irq count %u, pc %08x, istat %x, sr %08x\n", mem(0x1204), C.pc, I_STAT, C.cop0[12]);
    CHECK("vblank interrupts", mem(0x1204) == 3);
}

static uint32_t gte_prog(const uint32_t *w, int n, uint32_t outaddr)
{
    CHECK("load gte", load(w, n) == 0);
    frames(1);
    return mem(outaddr);
}

static void gte_tests(void)
{
    const uint32_t p[] = {
        LUI(3, 0x8000),
        ORI(1, 0, 0x1000), CTC2(1, 0), CTC2(0, 1), CTC2(1, 2), CTC2(0, 3), CTC2(1, 4),   /* identity rotation */
        ORI(1, 0, 0x200), CTC2(1, 26),                                                    /* H */
        ORI(1, 0, 100), LUI(2, 0xFFCE), ORI(2, 2, 0), ADDU(1, 1, 2), MTC2(1, 0),           /* V0 = (100, -50) */
        ORI(1, 0, 0x400), MTC2(1, 1),                                                      /* VZ0 = 1024 */
        GTE(0x02080001),                                                                  /* RTPS sf=1 */
        MFC2(4, 14), NOP, SW(4, 0x1000, 3), MFC2(5, 25), NOP, SW(5, 0x1004, 3), MFC2(6, 19), NOP, SW(6, 0x1008, 3),
        MTC2(0, 15), ORI(1, 0, 10), MTC2(1, 15), LUI(1, 10), MTC2(1, 15),                 /* SXYP: (0,0) (10,0) (0,10) */
        GTE(0x00000006),                                                                  /* NCLIP */
        MFC2(7, 24), NOP, SW(7, 0x100C, 3),
        J_SELF(35), NOP,
    };
    (void)gte_prog;
    CHECK("load gte", load(p, (int)(sizeof p / 4)) == 0);
    frames(1);
    const uint32_t sxy = mem(0x1000);
    const int sx = (int16_t)(sxy & 0xFFFF), sy = (int16_t)(sxy >> 16);
    if (sx < 49 || sx > 50) printf("  sxy %08x mac1 %d sz3 %u pc %08x flag %08x\n", sxy, (int)mem(0x1004), mem(0x1008), C.pc, G.c[31]);
    CHECK("rtps x", sx >= 49 && sx <= 50);
    CHECK("rtps y", sy >= -26 && sy <= -25);
    CHECK("rtps mac1", (int32_t)mem(0x1004) == 100);
    CHECK("rtps sz3", mem(0x1008) == 1024);
    CHECK("nclip", mem(0x100C) == 100);
}

static void gpu_tests(void)
{
    psx_reset();
    psx_gp1_write(0x00000000);
    psx_gp0_write(0xE1000000); psx_gp0_write(0xE3000000); psx_gp0_write(0xE403FFFF >> 0 & 0xE4FFFFFF); psx_gp0_write(0xE4000000 | (511 << 10) | 1023);
    psx_gp0_write(0x020000FF); psx_gp0_write((0u << 16) | 0u); psx_gp0_write((16u << 16) | 32u);    /* fill red 32 x 16 at 0,0 */
    const uint16_t *v = psx_vram();
    CHECK("fill inside", v[5 * 1024 + 5] == 0x001F);
    CHECK("fill edge", v[15 * 1024 + 31] == 0x001F && v[16 * 1024 + 0] == 0 && v[0 * 1024 + 32] == 0);
    psx_gp0_write(0x2000FF00); psx_gp0_write((10u << 16) | 100u); psx_gp0_write((10u << 16) | 140u); psx_gp0_write((50u << 16) | 100u);   /* flat green triangle */
    CHECK("triangle inside", v[20 * 1024 + 110] == 0x03E0);
    CHECK("triangle outside", v[40 * 1024 + 139] == 0 && v[9 * 1024 + 110] == 0);
    /* CPU to VRAM, then a textured raw rectangle */
    psx_gp0_write(0xA0000000); psx_gp0_write((0u << 16) | 512u); psx_gp0_write((4u << 16) | 4u);
    for (int i = 0; i < 8; i++) psx_gp0_write(0x03E003E0u);
    CHECK("cpu to vram", v[3 * 1024 + 515] == 0x03E0);
    psx_gp0_write(0xE1000108);                                                       /* texture page x 8 (512), 15 bit */
    psx_gp0_write(0x65808080); psx_gp0_write((100u << 16) | 200u); psx_gp0_write(0x00000000); psx_gp0_write((4u << 16) | 4u);
    CHECK("textured rect", v[100 * 1024 + 200] == 0x03E0 && v[103 * 1024 + 203] == 0x03E0 && v[104 * 1024 + 204] == 0);
    /* VRAM to CPU */
    psx_gp0_write(0xC0000000); psx_gp0_write((0u << 16) | 512u); psx_gp0_write((1u << 16) | 2u);
    CHECK("vram to cpu", psx_gpu_read() == 0x03E003E0u);
    /* gouraud shaded: the middle of a red to blue gradient across a quad */
    psx_gp0_write(0xE1000000);
    psx_gp0_write(0x380000FF); psx_gp0_write((200u << 16) | 300u); psx_gp0_write(0x00FF0000); psx_gp0_write((200u << 16) | 364u);
    psx_gp0_write(0x000000FF); psx_gp0_write((264u << 16) | 300u); psx_gp0_write(0x00FF0000); psx_gp0_write((264u << 16) | 364u);
    const uint16_t mid = v[232 * 1024 + 332];
    CHECK("gouraud mix", (mid & 31) > 4 && (mid >> 10) > 4);
}

static void dma_tests(void)
{
    psx_reset();
    psx_bus_write(0x1F8010F0, 0x0FFFFFFF, 4);                                     /* DPCR: every channel on */
    psx_bus_write(0x1F8010E0, 0x2000, 4); psx_bus_write(0x1F8010E4, 4, 4); psx_bus_write(0x1F8010E8, 0x11000002, 4);
    CHECK("otc first", mem(0x2000) == 0x1FFC);
    CHECK("otc last", mem(0x1FF4) == 0x00FFFFFFu);
    const uint32_t list[] = { 0x03000000u | 0x3010u, 0x0200FF00u, (4u << 16) | 4u, (8u << 16) | 8u };
    memcpy(psx_ram + 0x3000, list, sizeof list);
    const uint32_t end = 0x00FFFFFFu;
    memcpy(psx_ram + 0x3010, &end, 4);
    psx_gp1_write(0x00000000);
    psx_gp0_write(0xE3000000); psx_gp0_write(0xE4000000 | (511 << 10) | 1023);
    psx_bus_write(0x1F8010A0, 0x3000, 4); psx_bus_write(0x1F8010A4, 0, 4); psx_bus_write(0x1F8010A8, 0x01000401, 4);
    CHECK("gpu linked list", psx_vram()[5 * 1024 + 6] == 0x03E0);
}

static void cd_tests(void)
{
    psx_reset();
    psx_set_disc(NULL);
    psx_cd_write(0, 1); psx_cd_write(2, 0x1F);
    psx_cd_write(0, 0); psx_cd_write(1, 0x1A);
    psx_cd_run(100000);
    psx_cd_write(0, 1);
    CHECK("getid int3", (psx_cd_read(3) & 7) == 3);
    psx_cd_write(3, 0x1F);
    psx_cd_run(100000);
    CHECK("getid no disc int5", (psx_cd_read(3) & 7) == 5);
}

static uint32_t fake_n;
static int fake_read(void *u, uint32_t lba, uint8_t *out)
{
    (void)u;
    fake_n++;
    memset(out, 0, 2352);
    for (int i = 24; i < 24 + 2048; i++) out[i] = (uint8_t)(lba + 1);
    return 0;
}

static void cd_disc_tests(void)
{
    psx_reset();
    psx_disc_t d = { fake_read, NULL, 100, 1 };
    psx_set_disc(&d);
    psx_cd_write(0, 1); psx_cd_write(2, 0x1F);
    psx_cd_write(0, 0); psx_cd_write(2, 0x00); psx_cd_write(2, 0x02); psx_cd_write(2, 0x00); psx_cd_write(1, 0x02);      /* Setloc 00:02:00 = lba 0 */
    psx_cd_run(60000);
    psx_cd_write(0, 1); psx_cd_write(3, 0x1F);
    psx_cd_write(0, 0); psx_cd_write(1, 0x06);                                                        /* ReadN */
    psx_cd_run(60000);
    psx_cd_write(0, 1); psx_cd_write(3, 0x1F);
    psx_cd_run(200000);
    psx_cd_write(0, 1);
    CHECK("readn int1", (psx_cd_read(3) & 7) == 1);
    psx_cd_write(0, 0); psx_cd_write(3, 0x80);
    uint8_t buf[2048];
    for (int i = 0; i < 2048; i++) buf[i] = (uint8_t)psx_cd_read(2);
    CHECK("sector data", buf[0] == 1 && buf[2047] == 1);
    psx_cd_write(0, 1); psx_cd_write(3, 0x1F);
    psx_cd_run(500000);
    psx_cd_write(0, 1);
    CHECK("second sector", (psx_cd_read(3) & 7) == 1);
    psx_cd_write(0, 0); psx_cd_write(3, 0x80);
    CHECK("sector 2 data", psx_cd_read(2) == 2);
}

static void spu_tests(void)
{
    psx_reset();
    psx_spu_write(0x1DA6, 0x200);
    for (int i = 0; i < 8; i++) psx_spu_write(0x1DA8, i == 0 ? 0x0800 | 0x0700 : 0x7777);      /* shift 8, flags: loop start, end, repeat */
    psx_spu_write(0x1DAA, 0xC000);
    psx_spu_write(0x1D80, 0x3FFF); psx_spu_write(0x1D82, 0x3FFF);
    psx_spu_write(0x1C00, 0x3FFF); psx_spu_write(0x1C02, 0x3FFF); psx_spu_write(0x1C04, 0x1000); psx_spu_write(0x1C06, 0x200);
    psx_spu_write(0x1C08, 0x000F); psx_spu_write(0x1C0A, 0x0000);
    psx_spu_write(0x1D88, 1);
    psx_spu_run(768 * 200);
    float out[128] = { 0 };
    psx_audio_mix(out, 64, 1.0f);
    int nz = 0;
    for (int i = 0; i < 128; i++) if (out[i] != 0.0f) nz++;
    CHECK("spu voice makes sound", nz > 10);
}

int main(void)
{
    cpu_tests();
    hle_tests();
    irq_tests();
    gte_tests();
    gpu_tests();
    dma_tests();
    cd_tests();
    cd_disc_tests();
    spu_tests();
    printf("%d checks, %d failed\n", count, fails);
    return fails ? 1 : 0;
}
