/*
 * psx_jit.c: a dynamic recompiler from the R3000A to ARMv6 (ARM state), for the Pi Zero W. Original code.
 *
 * A block starts at a PC in RAM or in the BIOS and runs to a branch (with its delay slot), to an instruction that
 * must stay with the interpreter and ends the block (SYSCALL, BREAK, MTC0, RFE), or to 48 instructions. The MIPS
 * registers stay in the CPU state (r11 points to it): an instruction is a few ARM instructions that load its
 * registers, work, and store the result. Loads and stores of RAM are inline (a range check, then the access); every
 * other address, a misaligned one, a store into a page that has compiled code, and every instruction the compiler
 * does not know (the GTE, COP0, LWL/LWR, ADD/SUB that trap, ...) call back into the interpreter's code. So what
 * the compiler does not do is still right.
 *
 * The delay slots are exact: a branch reads its registers before the delay slot runs, and a loaded value lands after
 * the next instruction (it is held in r6 or r7 and stored after the next instruction is translated). Two deviations:
 * a value loaded in a delay slot or by LWL/LWR/MFC0/MFC2/CFC2 lands at once.
 *
 * ARM registers: r11 CPU state, r10 RAM, r9 page flags, r8 the return address, r4 the scratchpad, r5 branch condition or target,
 * r6 and r7 a load in flight, r0-r3 and r12 scratch.
 *
 * Code pages: RAM is split in 256 byte pages. A page with compiled code has a flag; a store into it goes the slow way
 * and bumps the page's generation, which makes the blocks of the page stale (they are compiled again when the PC comes
 * back). A page that goes stale too often is left to the interpreter.
 *
 * PSX_JIT_HOST: the translator builds on the PC (nothing runs): tests/psx/jit_dump.c prints what it makes.
 */
#include "psx_int.h"

#include <stddef.h>
#include <stdio.h>

#if defined(PSX_JIT_HOST) || (defined(__arm__) && !defined(__thumb__) && defined(__ARM_ARCH) && __ARM_ARCH == 6)
#define JIT_ARM 1
#endif

uint8_t psx_jit_pageflags[0x200000 / 256 + 1];   /* 1 for a 256 byte page of RAM that has compiled code (the stores of the CPU look at it) */

#ifndef JIT_ARM
/* no recompiler on this target: the interpreter does everything */
void psx_jit_reset(void) {}
int psx_jit_take_stale(void) { return 0; }
void psx_jit_ram_written(uint32_t off, uint32_t len) { (void)off; (void)len; }
void psx_jit_sr_changed(void) {}
int psx_jit_run_block(void) { return 0; }
int psx_jit_available(void) { return 0; }
int psx_jit_selftest(int trials, char *msg, int n) { (void)trials; snprintf(msg, (size_t)n, "no recompiler on this target"); return -1; }
#else

#define ARENA_SIZE (1536 * 1024)
#define TABLE_BITS 12
#define PAGE_SHIFT 8
#define NPAGES (0x200000 >> PAGE_SHIFT)
#define MAX_INSTR 48
#define BIOS_PAGE NPAGES                   /* the dummy page of blocks in the BIOS */

static uint8_t arena[ARENA_SIZE] __attribute__((aligned(32)));
static uint32_t arena_used;
static uint16_t page_gen[NPAGES + 1];
static uint8_t page_inv[NPAGES + 1], page_bad[NPAGES + 1];
static int stale_cur, cur_p0 = -1, cur_p1 = -1;
static int isolated;

typedef struct { uint32_t pc; void *code; uint16_t p0, p1, g0, g1; } block_t;
static block_t table[1 << TABLE_BITS];

#ifndef PSX_JIT_HOST
extern uint32_t psx_jit_enter(void *code, cpu_t *c);
__asm__(
    ".text\n.arm\n.global psx_jit_enter\n.type psx_jit_enter, %function\npsx_jit_enter:\n"
    "  push {r3-r11, lr}\n"
    "  mov r11, r1\n"
    "  ldr r10, =psx_ram\n"
    "  ldr r9, =psx_jit_pageflags\n"
    "  ldr r4, =psx_scratch\n"
    "  blx r0\n"
    "  pop {r3-r11, pc}\n"
    "  .ltorg\n");
#endif

int psx_jit_available(void) { return 1; }

/* ------------------------------------------------------------------ the code buffer */
static uint32_t *cp;                       /* where the next instruction goes */
static uint32_t *cend;

static inline void emit(uint32_t w) { if (cp < cend) *cp++ = w; }

enum { AL = 14, EQ = 0, NE = 1, HS = 2, LO = 3, GE = 10, LT = 11, GT = 12, LE = 13 };
enum { OP_AND = 0, OP_EOR = 1, OP_SUB = 2, OP_RSB = 3, OP_ADD = 4, OP_TST = 8, OP_CMP = 10, OP_ORR = 12, OP_MOV = 13, OP_BIC = 14, OP_MVN = 15 };

static int enc_imm(uint32_t v)             /* the 12 bit operand of a data instruction, or -1 */
{
    for (int rot = 0; rot < 16; rot++) {
        const uint32_t r = rot ? ((v << (2 * rot)) | (v >> (32 - 2 * rot))) : v;
        if (r < 256) return (rot << 8) | (int)r;
    }
    return -1;
}

static void dp_imm(int cond, int op, int rd, int rn, int imm12, int s) { emit((uint32_t)cond << 28 | 1u << 25 | (uint32_t)op << 21 | (uint32_t)s << 20 | (uint32_t)rn << 16 | (uint32_t)rd << 12 | (uint32_t)imm12); }
static void dp_reg(int cond, int op, int rd, int rn, int rm, int shift_type, int shamt, int s) { emit((uint32_t)cond << 28 | (uint32_t)op << 21 | (uint32_t)s << 20 | (uint32_t)rn << 16 | (uint32_t)rd << 12 | (uint32_t)shamt << 7 | (uint32_t)shift_type << 5 | (uint32_t)rm); }
static void dp_regshift(int op, int rd, int rn, int rm, int shift_type, int rs) { emit(14u << 28 | (uint32_t)op << 21 | (uint32_t)rn << 16 | (uint32_t)rd << 12 | (uint32_t)rs << 8 | 1u << 4 | (uint32_t)shift_type << 5 | (uint32_t)rm); }

static void mov_reg(int rd, int rm) { dp_reg(AL, OP_MOV, rd, 0, rm, 0, 0, 0); }

static void const_c(int cond, int rd, uint32_t v)
{
    int e;
    if ((e = enc_imm(v)) >= 0) { dp_imm(cond, OP_MOV, rd, 0, e, 0); return; }
    if ((e = enc_imm(~v)) >= 0) { dp_imm(cond, OP_MVN, rd, 0, e, 0); return; }
    int first = 1;
    for (int sh = 0; sh < 32; sh += 8) {
        const uint32_t byte = (v >> sh) & 0xFF;
        if (!byte) continue;
        const int rot = sh ? (32 - sh) / 2 : 0;
        dp_imm(cond, first ? OP_MOV : OP_ORR, rd, first ? 0 : rd, (rot << 8) | (int)byte, 0);
        first = 0;
    }
}
static void const_(int rd, uint32_t v) { const_c(AL, rd, v); }

static void add_const(int rd, int rn, int32_t v)            /* rd = rn + v */
{
    int e;
    if (v == 0) { if (rd != rn) mov_reg(rd, rn); return; }
    if (v > 0 && (e = enc_imm((uint32_t)v)) >= 0) { dp_imm(AL, OP_ADD, rd, rn, e, 0); return; }
    if (v < 0 && (e = enc_imm((uint32_t)-v)) >= 0) { dp_imm(AL, OP_SUB, rd, rn, e, 0); return; }
    const_(12, (uint32_t)v);
    dp_reg(AL, OP_ADD, rd, rn, 12, 0, 0, 0);
}

static void ldr_off(int cond, int rd, int rn, int off) { emit((uint32_t)cond << 28 | 0x05900000u | (uint32_t)rn << 16 | (uint32_t)rd << 12 | (uint32_t)off); }
static void str_off(int cond, int rd, int rn, int off) { emit((uint32_t)cond << 28 | 0x05800000u | (uint32_t)rn << 16 | (uint32_t)rd << 12 | (uint32_t)off); }

#define OFF_R(i) ((int)offsetof(cpu_t, r) + 4 * (i))
#define OFF_HI ((int)offsetof(cpu_t, hi))
#define OFF_LO ((int)offsetof(cpu_t, lo))
#define OFF_PC ((int)offsetof(cpu_t, pc))
#define OFF_NPC ((int)offsetof(cpu_t, npc))
#define OFF_NBD ((int)offsetof(cpu_t, next_bd))
#define OFF_EXC ((int)offsetof(cpu_t, jit_exc))

static void ld_mips(int arm, int m) { if (m) ldr_off(AL, arm, 11, OFF_R(m)); else dp_imm(AL, OP_MOV, arm, 0, 0, 0); }
static void st_mips(int m, int arm) { if (m) str_off(AL, arm, 11, OFF_R(m)); }

static void branch_fwd(int cond, uint32_t **patch) { *patch = cp; emit((uint32_t)cond << 28 | 0x0A000000u); }
static void patch_here(uint32_t *patch)                       /* the branch at patch goes to the next instruction */
{
    if (patch >= cend) return;
    const int32_t off = (int32_t)(cp - patch) - 2;
    *patch |= (uint32_t)off & 0x00FFFFFFu;
}
static void bx_reg(int rm) { emit(14u << 28 | 0x012FFF10u | (uint32_t)rm); }
#ifdef PSX_JIT_HOST
void psx_jit_div(uint32_t n, uint32_t d);
void psx_jit_divu(uint32_t n, uint32_t d);
/* on the PC the helper addresses are 0xF0000000 + 4 * number: tests/psx/jit_unicorn.py answers a call to them */
static uint32_t helper_addr(const void *fn)
{
    static const void *const list[] = { (const void *)psx_jit_exec, (const void *)psx_jit_read, (const void *)psx_jit_write, (const void *)psx_jit_div,
                                        (const void *)psx_jit_divu, (const void *)psx_gte_get_data, (const void *)psx_gte_set_data,
                                        (const void *)psx_gte_get_ctl, (const void *)psx_gte_set_ctl, (const void *)psx_gte_command };
    for (uint32_t i = 0; i < sizeof list / sizeof list[0]; i++) if (list[i] == fn) return 0xF0000000u + 4u * i;
    return 0xF0000F00u;
}
#define HELPER(fn) helper_addr(fn)
#else
#define HELPER(fn) ((uint32_t)(uintptr_t)(fn))
#endif
static void call_c(const void *fn) { const_(12, HELPER(fn)); emit(14u << 28 | 0x012FFF30u | 12u); }

/* ------------------------------------------------------------------ one block */
typedef struct {
    int n;                          /* MIPS instructions so far (the cycles) */
    int pend_reg, pend_arm;         /* a load in flight: the MIPS register and the ARM register that holds it; 0 if none */
    int next_arm;                   /* the ARM register the next load will use */
} gen_t;

static inline uint32_t phys_of(uint32_t pc) { return pc & ((pc >> 29) == 4 ? 0x7FFFFFFFu : (pc >> 29) == 5 ? 0x1FFFFFFFu : 0xFFFFFFFFu); }

static inline uint32_t fetch(uint32_t pc)
{
    const uint32_t p = phys_of(pc);
    if (p < 0x00800000u) return *(const uint32_t *)(psx_ram + (p & 0x1FFFFFu));
    return *(const uint32_t *)(psx_bios + (p & 0x7FFFFu));
}

static int pc_ok(uint32_t pc)                                  /* code the compiler may read: RAM or the BIOS */
{
    if (pc & 3) return 0;
    const uint32_t p = phys_of(pc);
    return p < 0x00800000u || (p >= 0x1FC00000u && p < 0x1FC80000u);
}

static void flush_pending(gen_t *g, int written)               /* the load in flight lands (unless the next instruction wrote the register) */
{
    if (g->pend_reg && g->pend_reg != written) st_mips(g->pend_reg, g->pend_arm);
    g->pend_reg = 0;
}

static void exit_const(const gen_t *g0, uint32_t next_pc)      /* the block ends: the PC is a constant (the generator state is not changed) */
{
    gen_t tmp = *g0, *g = &tmp;
    flush_pending(g, 0);
    const_(2, next_pc);
    str_off(AL, 2, 11, OFF_PC);
    add_const(3, 2, 4);
    str_off(AL, 3, 11, OFF_NPC);
    dp_imm(AL, OP_MOV, 0, 0, 0, 0);
    str_off(AL, 0, 11, OFF_NBD);
    const_(0, (uint32_t)g->n * PSX_CPI);
    bx_reg(8);
}

static void exit_cycles(const gen_t *g)                        /* leave at once: the state is already in the CPU (an exception) */
{
    const_(0, (uint32_t)g->n * PSX_CPI);
    bx_reg(8);
}

/* after a helper call that returns nonzero (r0) when the block must stop */
static void exit_if_r0(const gen_t *g)
{
    uint32_t *p;
    dp_imm(AL, OP_CMP, 0, 0, 0, 1);
    branch_fwd(EQ, &p);
    exit_cycles(g);
    patch_here(p);
}

static int is_branch(uint32_t i)
{
    const uint32_t op = i >> 26;
    if (op == 2 || op == 3 || (op >= 4 && op <= 7) || op == 1) return 1;
    return op == 0 && ((i & 63) == 8 || (i & 63) == 9);
}

static int is_terminator(uint32_t i)                           /* runs by the interpreter and ends the block */
{
    const uint32_t op = i >> 26, fn = i & 63;
    if (op == 0 && (fn == 0x0C || fn == 0x0D)) return 1;       /* SYSCALL, BREAK */
    if (op == 0x10 && (((i >> 21) & 31) == 4 || ((i >> 21) & 31) == 0x10)) return 1;   /* MTC0, RFE */
    return 0;
}

static void emit_load_op(int kind, int dst, int base, int idx)     /* ldr dst, [base, idx] for lw, lh, lhu, lb, lbu */
{
    const uint32_t o = kind == 0 ? 0x07900000u : kind == 4 ? 0x07D00000u : kind == 3 ? 0x019000D0u : kind == 2 ? 0x019000B0u : 0x019000F0u;
    emit(14u << 28 | o | (uint32_t)base << 16 | (uint32_t)dst << 12 | (uint32_t)idx);
}

static void emit_store_op(int kind, int src, int base, int idx)    /* str src, [base, idx] for sw, sh, sb */
{
    const uint32_t o = kind == 0 ? 0x07800000u : kind == 1 ? 0x018000B0u : 0x07C00000u;
    emit(14u << 28 | o | (uint32_t)base << 16 | (uint32_t)src << 12 | (uint32_t)idx);
}

/* a load: RAM and the scratchpad inline, the rest by a helper. gte: LWC2 (the value goes to the GTE register rt) */
static int gen_load(gen_t *g, uint32_t i, uint32_t pc, int bd, int gte)
{
    const uint32_t op = i >> 26;
    const int rs = (int)((i >> 21) & 31), rt = (int)((i >> 16) & 31);
    const int32_t imm = (int16_t)(i & 0xFFFF);
    static const uint8_t kind_of[6] = { 3, 1, 0, 0, 4, 2 };    /* 0x20 lb, 0x21 lh, 0x22 -, 0x23 lw, 0x24 lbu, 0x25 lhu */
    const int kind = gte ? 0 : kind_of[op - 0x20];
    const int size = (kind == 0) ? 4 : (kind == 1 || kind == 2) ? 2 : 1;
    const int dst = gte ? 2 : g->next_arm;
    uint32_t *slow1 = NULL, *hi, *slow2, *done1, *done2, *exc;
    ld_mips(0, rs);
    add_const(0, 0, imm);
    if (size > 1) { dp_imm(AL, OP_TST, 0, 0, size - 1, 1); branch_fwd(NE, &slow1); }
    dp_imm(AL, OP_BIC, 1, 0, enc_imm(0xE0000000u), 0);
    dp_imm(AL, OP_CMP, 0, 1, enc_imm(0x800000u), 1);
    branch_fwd(HS, &hi);
    dp_imm(AL, OP_BIC, 1, 1, enc_imm(0x600000u), 0);
    emit_load_op(kind, dst, 10, 1);
    branch_fwd(AL, &done1);
    patch_here(hi);                                             /* the scratchpad: 0x1F800000 to 0x1F8003FF */
    dp_imm(AL, OP_SUB, 3, 1, enc_imm(0x1F800000u), 0);
    dp_imm(AL, OP_CMP, 0, 3, enc_imm(0x400u), 1);
    branch_fwd(HS, &slow2);
    emit_load_op(kind, dst, 4, 3);
    branch_fwd(AL, &done2);
    if (slow1) patch_here(slow1);
    patch_here(slow2);
    /* the slow way: r0 is the address */
    dp_imm(AL, OP_MOV, 1, 0, kind | (bd ? 0x10 : 0), 0);
    const_(2, pc);
    call_c((const void *)psx_jit_read);
    mov_reg(dst, 0);
    ldr_off(AL, 1, 11, OFF_EXC);
    dp_imm(AL, OP_CMP, 0, 1, 0, 1);
    branch_fwd(EQ, &exc);
    exit_cycles(g);
    patch_here(exc);
    patch_here(done1);
    patch_here(done2);
    if (gte) {                                                  /* LWC2: the value (r2) goes to the GTE register */
        mov_reg(1, 2);
        const_(0, (uint32_t)rt);
        call_c((const void *)psx_gte_set_data);
        flush_pending(g, 0);
        return 1;
    }
    /* the load in flight from the instruction before lands now (even into the same register: a load does not cancel an earlier load); this one goes in flight */
    flush_pending(g, 0);
    if (rt) { g->pend_reg = rt; g->pend_arm = dst; g->next_arm = dst == 6 ? 7 : 6; }
    return 1;
}

/* a store: RAM and the scratchpad inline. gte: SWC2 (the value comes from the GTE register rt) */
static int gen_store(gen_t *g, uint32_t i, uint32_t pc, int bd, int gte)
{
    const uint32_t op = i >> 26;
    const int rs = (int)((i >> 21) & 31), rt = (int)((i >> 16) & 31);
    const int32_t imm = (int16_t)(i & 0xFFFF);
    const int kind = (gte || op == 0x2B) ? 0 : op == 0x29 ? 1 : 2;      /* sw, sh, sb */
    const int size = kind == 0 ? 4 : kind == 1 ? 2 : 1;
    uint32_t *slow1 = NULL, *hi, *slow2, *slow3, *done1, *done2, *cont;
    if (gte) { const_(0, (uint32_t)rt); call_c((const void *)psx_gte_get_data); mov_reg(2, 0); }
    ld_mips(0, rs);
    add_const(0, 0, imm);
    if (!gte) ld_mips(2, rt);
    if (size > 1) { dp_imm(AL, OP_TST, 0, 0, size - 1, 1); branch_fwd(NE, &slow1); }
    dp_imm(AL, OP_BIC, 1, 0, enc_imm(0xE0000000u), 0);
    dp_imm(AL, OP_CMP, 0, 1, enc_imm(0x800000u), 1);
    branch_fwd(HS, &hi);
    dp_imm(AL, OP_BIC, 1, 1, enc_imm(0x600000u), 0);
    emit(14u << 28 | 0x07D00000u | 9u << 16 | 3u << 12 | ((uint32_t)PAGE_SHIFT << 7) | (1u << 5) | 1u);   /* ldrb r3, [r9, r1, lsr #PAGE_SHIFT] */
    dp_imm(AL, OP_CMP, 0, 3, 0, 1);
    branch_fwd(NE, &slow3);                                     /* a page with compiled code: the slow way drops it */
    emit_store_op(kind, 2, 10, 1);
    branch_fwd(AL, &done1);
    patch_here(hi);                                             /* the scratchpad */
    dp_imm(AL, OP_SUB, 3, 1, enc_imm(0x1F800000u), 0);
    dp_imm(AL, OP_CMP, 0, 3, enc_imm(0x400u), 1);
    branch_fwd(HS, &slow2);
    emit_store_op(kind, 2, 4, 3);
    branch_fwd(AL, &done2);
    if (slow1) patch_here(slow1);
    patch_here(slow2);
    patch_here(slow3);
    /* the slow way: r0 = address, r1 = value, r2 = kind, r3 = pc */
    mov_reg(1, 2);
    dp_imm(AL, OP_MOV, 2, 0, kind | (bd ? 0x10 : 0), 0);
    const_(3, pc);
    call_c((const void *)psx_jit_write);
    if (!bd) {
        /* nonzero: an exception (the PC is set) or the block's own code changed (go on at the next instruction) */
        uint32_t *p;
        dp_imm(AL, OP_CMP, 0, 0, 0, 1);
        branch_fwd(EQ, &p);
        ldr_off(AL, 1, 11, OFF_EXC);
        dp_imm(AL, OP_CMP, 0, 1, 0, 1);
        branch_fwd(NE, &cont);
        exit_const(g, pc + 4);
        patch_here(cont);
        exit_cycles(g);
        patch_here(p);
    } else {
        ldr_off(AL, 1, 11, OFF_EXC);
        dp_imm(AL, OP_CMP, 0, 1, 0, 1);
        branch_fwd(EQ, &cont);
        exit_cycles(g);
        patch_here(cont);
    }
    patch_here(done1);
    patch_here(done2);
    flush_pending(g, 0);
    return 1;
}

/* MFC2, CFC2, MTC2, CTC2 and the GTE commands: direct calls (no decoding by the interpreter). 0 if it is not one of them */
static int gen_cop2(gen_t *g, uint32_t i)
{
    const int sub = (int)((i >> 21) & 31), rt = (int)((i >> 16) & 31), rd = (int)((i >> 11) & 31);
    if (i & (1u << 25)) {                                       /* a GTE command */
        const_(0, i & 0x1FFFFFFu);
        call_c((const void *)psx_gte_command);
        flush_pending(g, 0);
        return 1;
    }
    if (sub == 0 || sub == 2) {                                 /* MFC2, CFC2: a load that lands after the next instruction */
        const int dst = g->next_arm;
        const_(0, (uint32_t)rd);
        call_c(sub == 0 ? (const void *)psx_gte_get_data : (const void *)psx_gte_get_ctl);
        mov_reg(dst, 0);
        flush_pending(g, 0);
        if (rt) { g->pend_reg = rt; g->pend_arm = dst; g->next_arm = dst == 6 ? 7 : 6; }
        return 1;
    }
    if (sub == 4 || sub == 6) {                                 /* MTC2, CTC2 */
        ld_mips(1, rt);
        const_(0, (uint32_t)rd);
        call_c(sub == 4 ? (const void *)psx_gte_set_data : (const void *)psx_gte_set_ctl);
        flush_pending(g, 0);
        return 1;
    }
    return 0;
}

/* ADD, ADDI, SUB trap on overflow: the sum is made inline; on overflow the interpreter raises the exception */
static void gen_trap_add(gen_t *g, uint32_t i, uint32_t pc, int bd)
{
    const uint32_t op = i >> 26;
    const int rs = (int)((i >> 21) & 31), rt = (int)((i >> 16) & 31), rd = (int)((i >> 11) & 31);
    const int dest = op == 8 ? rt : rd;
    const int32_t simm = (int16_t)(i & 0xFFFF);
    uint32_t *ov, *done;
    ld_mips(0, rs);
    if (op == 8) {
        const int e = enc_imm((uint32_t)(simm < 0 ? -simm : simm));
        if (simm >= 0 && e >= 0) dp_imm(AL, OP_ADD, 0, 0, e, 1);
        else if (simm < 0 && e >= 0) dp_imm(AL, OP_SUB, 0, 0, e, 1);
        else { const_(1, (uint32_t)simm); dp_reg(AL, OP_ADD, 0, 0, 1, 0, 0, 1); }
    } else {
        ld_mips(1, rt);
        dp_reg(AL, (i & 63) == 0x20 ? OP_ADD : OP_SUB, 0, 0, 1, 0, 0, 1);
    }
    branch_fwd(6, &ov);                                         /* VS: a signed overflow */
    st_mips(dest, 0);
    branch_fwd(AL, &done);
    patch_here(ov);
    const_(0, i);
    const_(1, pc);
    dp_imm(AL, OP_MOV, 2, 0, bd ? 1 : 0, 0);
    call_c((const void *)psx_jit_exec);
    exit_cycles(g);
    patch_here(done);
    flush_pending(g, dest);
}

/* an instruction the compiler leaves to the interpreter. A load in flight lands after it (it reads the old value), except when
 * the instruction itself loads into the same register (LWL and LWR then merge with the value in flight: it lands first) */
static int gen_fallback(gen_t *g, uint32_t i, uint32_t pc, int bd)
{
    const uint32_t op = i >> 26;
    const int rt = (int)((i >> 16) & 31);
    const int loads = op == 0x22 || op == 0x26 || (op == 0x10 && ((i >> 21) & 31) == 0);
    if (loads && g->pend_reg == rt) flush_pending(g, 0);
    const_(0, i);
    const_(1, pc);
    dp_imm(AL, OP_MOV, 2, 0, bd ? 1 : 0, 0);
    call_c((const void *)psx_jit_exec);
    exit_if_r0(g);
    flush_pending(g, loads ? rt : 0);
    return 1;
}

void psx_jit_div(uint32_t n_, uint32_t d_)
{
    const int32_t n = (int32_t)n_, d = (int32_t)d_;
    if (d == 0) { C.hi = (uint32_t)n; C.lo = n < 0 ? 1u : 0xFFFFFFFFu; }
    else if (n == (int32_t)0x80000000 && d == -1) { C.hi = 0; C.lo = 0x80000000u; }
    else { C.lo = (uint32_t)(n / d); C.hi = (uint32_t)(n % d); }
}

void psx_jit_divu(uint32_t n, uint32_t d)
{
    if (d == 0) { C.hi = n; C.lo = 0xFFFFFFFFu; }
    else { C.lo = n / d; C.hi = n % d; }
}

/* the simple instructions: returns the MIPS register it wrote (0x100 if none), or -1 if it is not one of them */
static int gen_alu(gen_t *g, uint32_t i)
{
    (void)g;
    const uint32_t op = i >> 26, fn = i & 63;
    const int rs = (int)((i >> 21) & 31), rt = (int)((i >> 16) & 31), rd = (int)((i >> 11) & 31), sh = (int)((i >> 6) & 31);
    const int32_t simm = (int16_t)(i & 0xFFFF);
    const uint32_t uimm = i & 0xFFFF;
    int w = 0;
    if (op == 0) {
        switch (fn) {
        case 0x00: ld_mips(0, rt); if (sh) dp_reg(AL, OP_MOV, 0, 0, 0, 0, sh, 0); w = rd; st_mips(rd, 0); break;                              /* sll */
        case 0x02: ld_mips(0, rt); if (sh) dp_reg(AL, OP_MOV, 0, 0, 0, 1, sh, 0); w = rd; st_mips(rd, 0); break;                              /* srl (in ARM a shift of 0 bits means 32) */
        case 0x03: ld_mips(0, rt); if (sh) dp_reg(AL, OP_MOV, 0, 0, 0, 2, sh, 0); w = rd; st_mips(rd, 0); break;                              /* sra */
        case 0x04: ld_mips(0, rt); ld_mips(1, rs); dp_imm(AL, OP_AND, 1, 1, 31, 0); dp_regshift(OP_MOV, 0, 0, 0, 0, 1); w = rd; st_mips(rd, 0); break;
        case 0x06: ld_mips(0, rt); ld_mips(1, rs); dp_imm(AL, OP_AND, 1, 1, 31, 0); dp_regshift(OP_MOV, 0, 0, 0, 1, 1); w = rd; st_mips(rd, 0); break;
        case 0x07: ld_mips(0, rt); ld_mips(1, rs); dp_imm(AL, OP_AND, 1, 1, 31, 0); dp_regshift(OP_MOV, 0, 0, 0, 2, 1); w = rd; st_mips(rd, 0); break;
        case 0x10: ldr_off(AL, 0, 11, OFF_HI); w = rd; st_mips(rd, 0); break;
        case 0x11: ld_mips(0, rs); str_off(AL, 0, 11, OFF_HI); break;
        case 0x12: ldr_off(AL, 0, 11, OFF_LO); w = rd; st_mips(rd, 0); break;
        case 0x13: ld_mips(0, rs); str_off(AL, 0, 11, OFF_LO); break;
        case 0x18: case 0x19:                                                                                                               /* mult, multu */
            ld_mips(0, rs); ld_mips(1, rt);
            emit(14u << 28 | (fn == 0x18 ? 0x00C00090u : 0x00800090u) | 3u << 16 | 2u << 12 | 1u << 8 | 0u);                                  /* smull/umull r2, r3, r0, r1 */
            str_off(AL, 2, 11, OFF_LO); str_off(AL, 3, 11, OFF_HI);
            break;
        case 0x1A: case 0x1B:                                                                                                               /* div, divu */
            ld_mips(0, rs); ld_mips(1, rt);
            call_c(fn == 0x1A ? (const void *)psx_jit_div : (const void *)psx_jit_divu);
            break;
        case 0x21: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
            ld_mips(0, rs); ld_mips(1, rt);
            if (fn == 0x21) dp_reg(AL, OP_ADD, 0, 0, 1, 0, 0, 0);
            else if (fn == 0x23) dp_reg(AL, OP_SUB, 0, 0, 1, 0, 0, 0);
            else if (fn == 0x24) dp_reg(AL, OP_AND, 0, 0, 1, 0, 0, 0);
            else if (fn == 0x25) dp_reg(AL, OP_ORR, 0, 0, 1, 0, 0, 0);
            else if (fn == 0x26) dp_reg(AL, OP_EOR, 0, 0, 1, 0, 0, 0);
            else { dp_reg(AL, OP_ORR, 0, 0, 1, 0, 0, 0); dp_reg(AL, OP_MVN, 0, 0, 0, 0, 0, 0); }                                           /* nor */
            w = rd; st_mips(rd, 0);
            break;
        case 0x2A: case 0x2B:
            ld_mips(0, rs); ld_mips(1, rt);
            dp_imm(AL, OP_MOV, 2, 0, 0, 0);
            dp_reg(AL, OP_CMP, 0, 0, 1, 0, 0, 1);
            dp_imm(fn == 0x2A ? LT : LO, OP_MOV, 2, 0, 1, 0);
            w = rd; st_mips(rd, 2);
            break;
        default: return -1;
        }
        return w ? w : 0x100;
    }
    switch (op) {
    case 0x09: ld_mips(0, rs); add_const(0, 0, simm); w = rt; st_mips(rt, 0); break;                                                         /* addiu */
    case 0x0A: case 0x0B:                                                                                                                    /* slti, sltiu */
        ld_mips(0, rs); const_(1, (uint32_t)simm);
        dp_imm(AL, OP_MOV, 2, 0, 0, 0);
        dp_reg(AL, OP_CMP, 0, 0, 1, 0, 0, 1);
        dp_imm(op == 0x0A ? LT : LO, OP_MOV, 2, 0, 1, 0);
        w = rt; st_mips(rt, 2);
        break;
    case 0x0C: case 0x0D: case 0x0E: {                                                                                                       /* andi, ori, xori */
        const int o = op == 0x0C ? OP_AND : op == 0x0D ? OP_ORR : OP_EOR;
        ld_mips(0, rs);
        const int e = enc_imm(uimm);
        if (e >= 0) dp_imm(AL, o, 0, 0, e, 0);
        else { const_(1, uimm); dp_reg(AL, o, 0, 0, 1, 0, 0, 0); }
        w = rt; st_mips(rt, 0);
        break;
    }
    case 0x0F: const_(0, uimm << 16); w = rt; st_mips(rt, 0); break;                                                                          /* lui */
    default: return -1;
    }
    return w ? w : 0x100;
}

static int cond_for(uint32_t i, int *link)                    /* the ARM condition of a conditional branch, after cmp r0, r1 / cmp r0, #0 */
{
    *link = 0;
    switch (i >> 26) {
    case 4: return EQ;
    case 5: return NE;
    case 6: return LE;
    case 7: return GT;
    default: {
        const int rt = (int)((i >> 16) & 31);
        *link = (rt & 0x1E) == 0x10;
        return (rt & 1) ? GE : LT;
    }
    }
}

static void gen_one(gen_t *g, uint32_t i, uint32_t pc, int bd)  /* a plain instruction (not a branch) */
{
    const uint32_t op = i >> 26;
    const int cu2 = (C.cop0[12] & 0x40000000u) != 0;             /* (checked when the block is made) */
    if (op >= 0x20 && op <= 0x25 && op != 0x22) { gen_load(g, i, pc, bd, 0); return; }
    if (op == 0x28 || op == 0x29 || op == 0x2B) { gen_store(g, i, pc, bd, 0); return; }
    if (cu2 && op == 0x32) { gen_load(g, i, pc, bd, 1); return; }
    if (cu2 && op == 0x3A) { gen_store(g, i, pc, bd, 1); return; }
    if (cu2 && op == 0x12 && gen_cop2(g, i)) return;
    if (op == 8 || (op == 0 && ((i & 63) == 0x20 || (i & 63) == 0x22))) { gen_trap_add(g, i, pc, bd); return; }
    const int r = gen_alu(g, i);
    if (r < 0) gen_fallback(g, i, pc, bd);                      /* (a fallback that is a load: its value landed at once) */
    else flush_pending(g, r == 0x100 ? 0 : r);
}

/* translate from pc0; returns the block's code or NULL. *p0 and *p1: the pages of its code */
static void *compile(uint32_t pc0, int *p0, int *p1)
{
    if (!pc_ok(pc0)) return NULL;
    if (arena_used + 8192 > ARENA_SIZE) return NULL;
    uint32_t *start = (uint32_t *)(arena + arena_used);
    cp = start;
    cend = (uint32_t *)(arena + ARENA_SIZE);
    gen_t g = { 0, 0, 0, 6 };
    emit(14u << 28 | 0x01A00000u | 8u << 12 | 14u);              /* mov r8, lr */
    uint32_t pc = pc0, last_addr = pc0;
    for (;;) {
        if (g.n >= MAX_INSTR || !pc_ok(pc)) { exit_const(&g, pc); break; }
        const uint32_t i = fetch(pc);
        if (is_branch(i)) {
            if (!pc_ok(pc + 4)) { exit_const(&g, pc); break; }
            const uint32_t d = fetch(pc + 4);
            if (is_branch(d) || is_terminator(d)) {             /* a delay slot that is not plain: the interpreter takes the pair */
                if (g.n == 0) { cp = start; return NULL; }
                exit_const(&g, pc);
                break;
            }
            const uint32_t op = i >> 26, fn = i & 63;
            const int rs = (int)((i >> 21) & 31), rt = (int)((i >> 16) & 31), rd = (int)((i >> 11) & 31);
            g.n++;
            last_addr = pc + 4;
            /* 1. the condition or the target, from the registers before the delay slot; the link */
            uint32_t target = 0;
            int kind, cond = AL, wl = 0;                        /* kind: 0 conditional, 1 constant jump, 2 register jump */
            if (op == 2 || op == 3) { kind = 1; target = ((pc + 4) & 0xF0000000u) | ((i & 0x03FFFFFFu) << 2); }
            else if (op == 0) { kind = 2; }
            else { kind = 0; target = pc + 4 + ((uint32_t)(int32_t)(int16_t)(i & 0xFFFF) << 2); }
            if (kind == 2) {
                ld_mips(5, rs);
                if (fn == 9) { const_(0, pc + 8); st_mips(rd, 0); wl = rd; }
            } else if (kind == 1) {
                if (op == 3) { const_(0, pc + 8); st_mips(31, 0); wl = 31; }
            } else {
                int link;
                cond = cond_for(i, &link);
                ld_mips(0, rs);
                if (op == 4 || op == 5) { ld_mips(1, rt); dp_reg(AL, OP_CMP, 0, 0, 1, 0, 0, 1); }
                else dp_imm(AL, OP_CMP, 0, 0, 0, 1);
                dp_imm(AL, OP_MOV, 5, 0, 0, 0);
                dp_imm(cond, OP_MOV, 5, 0, 1, 0);
                if (link) { const_(0, pc + 8); st_mips(31, 0); wl = 31; }
            }
            flush_pending(&g, wl);                              /* a load before the branch lands before its delay slot */
            /* 2. the delay slot */
            g.n++;
            gen_one(&g, d, pc + 4, 1);
            flush_pending(&g, 0);
            /* 3. leave with the new PC */
            if (kind == 1) {
                exit_const(&g, target);
            } else {
                if (kind == 2) {
                    str_off(AL, 5, 11, OFF_PC);
                    add_const(3, 5, 4);
                } else {
                    dp_imm(AL, OP_CMP, 0, 5, 0, 1);
                    const_c(AL, 2, pc + 8);
                    const_c(NE, 2, target);
                    str_off(AL, 2, 11, OFF_PC);
                    add_const(3, 2, 4);
                }
                str_off(AL, 3, 11, OFF_NPC);
                dp_imm(AL, OP_MOV, 0, 0, 0, 0);
                str_off(AL, 0, 11, OFF_NBD);
                const_(0, (uint32_t)g.n * PSX_CPI);
                bx_reg(8);
            }
            break;
        }
        g.n++;
        last_addr = pc;
        if (is_terminator(i)) {
            gen_fallback(&g, i, pc, 0);
            exit_const(&g, pc + 4);
            break;
        }
        gen_one(&g, i, pc, 0);
        pc += 4;
    }
    if (cp >= cend) return NULL;
    const uint32_t bytes = (uint32_t)((uint8_t *)cp - (uint8_t *)start);
    arena_used += (bytes + 31) & ~31u;
    /* the pages of the code: at most two of 256 bytes (a block is at most 49 instructions) */
    {
        const uint32_t m0 = phys_of(pc0), m1 = phys_of(last_addr);
        if (m0 < 0x00800000u) {
            *p0 = (int)((m0 & 0x1FFFFFu) >> PAGE_SHIFT);
            *p1 = m1 < 0x00800000u ? (int)((m1 & 0x1FFFFFu) >> PAGE_SHIFT) : *p0;
            if (*p1 < *p0 || *p1 > *p0 + 1) *p1 = *p0;
        } else {
            *p0 = *p1 = BIOS_PAGE;
        }
    }
#ifndef PSX_JIT_HOST
    /* make the code visible to the instruction fetch */
    for (uint32_t a = (uint32_t)(uintptr_t)start & ~31u; a < (uint32_t)(uintptr_t)start + bytes; a += 32)
        __asm__ volatile("mcr p15, 0, %0, c7, c10, 1" : : "r"(a) : "memory");     /* clean the data cache line */
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 4" : : "r"(0) : "memory");          /* drain the write buffer */
    for (uint32_t a = (uint32_t)(uintptr_t)start & ~31u; a < (uint32_t)(uintptr_t)start + bytes; a += 32)
        __asm__ volatile("mcr p15, 0, %0, c7, c5, 1" : : "r"(a) : "memory");       /* invalidate the instruction cache line */
    __asm__ volatile("mcr p15, 0, %0, c7, c5, 6" : : "r"(0) : "memory");           /* flush the branch predictor */
    __asm__ volatile("mcr p15, 0, %0, c7, c5, 4" : : "r"(0) : "memory");           /* flush the prefetch buffer */
#endif
    return start;
}

/* ------------------------------------------------------------------ the dispatcher and the page flags */
static void psx_jit_flush_code(void)
{
    arena_used = 0;
    memset(table, 0, sizeof table);
    memset(psx_jit_pageflags, isolated ? 0xFF : 0, sizeof psx_jit_pageflags);
    for (int i = 0; i <= NPAGES; i++) page_gen[i]++;
}

void psx_jit_reset(void)
{
    arena_used = 0;
    memset(table, 0, sizeof table);
    memset(psx_jit_pageflags, 0, sizeof psx_jit_pageflags);
    memset(page_gen, 0, sizeof page_gen);
    memset(page_inv, 0, sizeof page_inv);
    memset(page_bad, 0, sizeof page_bad);
    stale_cur = 0; cur_p0 = cur_p1 = -1;
    isolated = 0;
}

int psx_jit_take_stale(void) { const int s = stale_cur; stale_cur = 0; return s; }

static void page_written(int p)
{
    if (p < 0 || p >= NPAGES || !psx_jit_pageflags[p]) return;
    psx_jit_pageflags[p] = isolated ? 0xFF : 0;
    page_gen[p]++;
    if (++page_inv[p] > 40) page_bad[p] = 1;
    if (p == cur_p0 || p == cur_p1) stale_cur = 1;
}

void psx_jit_ram_written(uint32_t off, uint32_t len)
{
    if (len == 0) return;
    const uint32_t a = off >> PAGE_SHIFT, b = (off + len - 1) >> PAGE_SHIFT;
    for (uint32_t p = a; p <= b && p < NPAGES; p++) page_written((int)p);
}

void psx_jit_sr_changed(void)
{
    const int now = (C.cop0[12] & 0x10000u) != 0;
    if (now == isolated) return;
    isolated = now;
    if (now) memset(psx_jit_pageflags, 0xFF, NPAGES);                  /* every store goes the slow way: the helper drops it */
    else psx_jit_flush_code();                                         /* (the blocks' own flags are set again when they are compiled) */
}

/* ------------------------------------------------------------------ programs for the self test */
static uint32_t rs_;
static uint32_t rnd32(void) { rs_ = rs_ * 1664525u + 1013904223u; return rs_; }
static int rndn(int n) { return (int)((rnd32() >> 9) % (uint32_t)n); }
static const int pool[11] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12 };
static int rreg(void) { return pool[rndn(11)]; }
static int rsrc(void) { const int k = rndn(10); return k == 0 ? 0 : k == 1 ? 31 : rreg(); }
#define R_(op, rs, rt, rd, sh, fn) (((uint32_t)(op) << 26) | ((uint32_t)(rs) << 21) | ((uint32_t)(rt) << 16) | ((uint32_t)(rd) << 11) | ((uint32_t)(sh) << 6) | (uint32_t)(fn))
#define I_(op, rs, rt, imm) (((uint32_t)(op) << 26) | ((uint32_t)(rs) << 21) | ((uint32_t)(rt) << 16) | ((uint32_t)(imm) & 0xFFFF))
#define T_BASE 0x80010000u
#define T_DATA 0x80020000u

static uint32_t gen_simple(void)
{
    const int k = rndn(48);
    const int rs = rsrc(), rt = rsrc(), rd = rndn(12) == 0 ? 0 : rreg(), sh = rndn(32);
    const int dt = rndn(12) == 0 ? 0 : rreg();                  /* a destination register */
    const uint32_t imm = rnd32() & 0xFFFF;
    const int bo = (int)(rnd32() & 0xFF) - 0x80;                /* a byte offset from the base register */
    switch (k) {
    case 0: return R_(0, 0, rt, rd, sh, 0x00);
    case 1: return R_(0, 0, rt, rd, sh, 0x02);
    case 2: return R_(0, 0, rt, rd, sh, 0x03);
    case 3: return R_(0, rs, rt, rd, 0, 0x04);
    case 4: return R_(0, rs, rt, rd, 0, 0x06);
    case 5: return R_(0, rs, rt, rd, 0, 0x07);
    case 6: return R_(0, 0, 0, rd, 0, 0x10);
    case 7: return R_(0, rs, 0, 0, 0, 0x11);
    case 8: return R_(0, 0, 0, rd, 0, 0x12);
    case 9: return R_(0, rs, 0, 0, 0, 0x13);
    case 10: return R_(0, rs, rt, 0, 0, 0x18);
    case 11: return R_(0, rs, rt, 0, 0, 0x19);
    case 12: return R_(0, rs, rt, 0, 0, 0x1A);
    case 13: return R_(0, rs, rt, 0, 0, 0x1B);
    case 14: return R_(0, rs, rt, rd, 0, 0x21);
    case 15: return R_(0, rs, rt, rd, 0, 0x23);
    case 16: return R_(0, rs, rt, rd, 0, 0x24);
    case 17: return R_(0, rs, rt, rd, 0, 0x25);
    case 18: return R_(0, rs, rt, rd, 0, 0x26);
    case 19: return R_(0, rs, rt, rd, 0, 0x27);
    case 20: return R_(0, rs, rt, rd, 0, 0x2A);
    case 21: return R_(0, rs, rt, rd, 0, 0x2B);
    case 22: return I_(0x09, rs, dt, imm);
    case 23: return I_(0x0A, rs, dt, imm);
    case 24: return I_(0x0B, rs, dt, imm);
    case 25: return I_(0x0C + rndn(3), rs, dt, imm);
    case 26: return I_(0x0F, 0, dt, imm);
    case 27: { const int o = bo & ~3; return I_(0x23, 29, dt, 0x200 + o); }                  /* lw */
    case 28: { const int o = bo & ~1; return I_(rndn(2) ? 0x21 : 0x25, 29, dt, 0x200 + o); } /* lh, lhu */
    case 29: return I_(rndn(2) ? 0x20 : 0x24, 29, dt, 0x200 + bo);                           /* lb, lbu */
    case 30: { const int o = bo & ~3; return I_(0x2B, 29, rt, 0x200 + o); }                  /* sw */
    case 31: { const int o = bo & ~1; return I_(0x29, 29, rt, 0x200 + o); }                  /* sh */
    case 32: return I_(0x28, 29, rt, 0x200 + bo);                                            /* sb */
    case 33: return I_(rndn(2) ? 0x2A : 0x2E, 29, rt, 0x200 + bo);                           /* swl swr (lwl and lwr come as a pair in build_program) */
    case 34: return I_(0x12, 0, dt, rndn(32) << 11) | (0u << 21);                          /* mfc2 */
    case 35: return (0x12u << 26) | (2u << 21) | ((uint32_t)dt << 16) | ((uint32_t)rndn(32) << 11);   /* cfc2 */
    case 36: return (0x12u << 26) | (4u << 21) | ((uint32_t)rt << 16) | ((uint32_t)rndn(32) << 11);   /* mtc2 */
    case 37: return (0x12u << 26) | (6u << 21) | ((uint32_t)rt << 16) | ((uint32_t)rndn(32) << 11);   /* ctc2 */
    case 38: { static const uint8_t cmd[8] = { 0x01, 0x06, 0x0C, 0x12, 0x13, 0x28, 0x2D, 0x30 }; return 0x4A000000u | ((uint32_t)rndn(2) << 19) | ((uint32_t)rndn(2) << 10) | cmd[rndn(8)]; }   /* a GTE command */
    case 39: { const int o = bo & ~3; return I_(0x32, 29, rndn(32), 0x200 + o); }            /* lwc2 */
    case 40: { const int o = bo & ~3; return I_(0x3A, 29, rndn(32), 0x200 + o); }            /* swc2 */
    case 41: return I_(0x08, rs, dt, imm & 0xFF);                                            /* addi with a small immediate (no overflow unless rs is huge) */
    case 42: return R_(0, 0, rt, rd, 0, 0x20);                                               /* add from zero: never overflows */
    case 43: { const int o = bo & ~3; return I_(0x23, 27, dt, o); }                          /* lw from the scratchpad */
    case 44: { const int o = bo & ~3; return I_(0x2B, 27, rt, o); }                          /* sw to the scratchpad */
    case 45: return I_(rndn(2) ? 0x21 : 0x25, 27, dt, bo & ~1);                              /* lh lhu */
    case 46: return I_(rndn(2) ? 0x28 : 0x29, 27, rt, bo & ~1);                              /* sb sh */
    default: return R_(0, rs, 0, rd, 0, 0x22);                                               /* sub of zero: never overflows */
    }
}

static uint32_t gen_slot(void)                                  /* a delay slot instruction: not a load (a load there lands at once in the recompiler) */
{
    for (;;) {
        const uint32_t w = gen_simple();
        const uint32_t op = w >> 26, sub = (w >> 21) & 31;
        if (op != 0x20 && op != 0x21 && op != 0x23 && op != 0x24 && op != 0x25 && !(op == 0x12 && (sub == 0 || sub == 2))) return w;
    }
}

static int build_program(uint32_t *prog, int len)               /* fills prog[0..len+1]; the end is at index len */
{
    int k = 0;
    while (k < len - 6) {
        const int t = rndn(14);
        if (t < 9) {
            if (rndn(12) == 0) {                                 /* the usual unaligned load: lwl, lwr, then a nop (a value that lwl or lwr loads lands at once in the recompiler) */
                const int d = rreg(), bo = (int)(rnd32() & 0x7F);
                prog[k++] = I_(0x22, 29, d, 0x200 + bo + 3);
                prog[k++] = I_(0x26, 29, d, 0x200 + bo);
                prog[k++] = 0;
                continue;
            }
            prog[k++] = gen_simple();
            continue;
        }
        const int span = len - (k + 2);
        const int tgt = k + 2 + rndn(span > 7 ? 7 : span + 1);  /* a forward target, at most the end */
        if (t < 12) {                                           /* a conditional branch with a delay slot */
            static const uint8_t ops[8] = { 4, 5, 6, 7, 1, 1, 1, 1 };
            static const uint8_t rtv[4] = { 0, 1, 16, 17 };
            const int o = ops[rndn(8)];
            const int rt = o == 1 ? rtv[rndn(4)] : rsrc();
            prog[k] = I_(o, rsrc(), rt, tgt - (k + 1));
            prog[k + 1] = gen_slot();
            k += 2;
        } else if (t == 12) {                                   /* j or jal */
            prog[k] = ((rndn(2) ? 2u : 3u) << 26) | (((T_BASE + 4u * (uint32_t)tgt) & 0x0FFFFFFFu) >> 2);
            prog[k + 1] = gen_slot();
            k += 2;
        } else {                                                /* jr or jalr through r13 */
            const int room = len - (k + 4);
            const int tg2 = k + 4 + rndn(room > 5 ? 5 : room + 1);
            const uint32_t a = T_BASE + 4u * (uint32_t)tg2;
            prog[k] = I_(0x0F, 0, 13, a >> 16);
            prog[k + 1] = I_(0x0D, 13, 13, a & 0xFFFF);
            prog[k + 2] = rndn(2) ? R_(0, 13, 0, 0, 0, 0x08) : R_(0, 13, 0, 31, 0, 0x09);
            prog[k + 3] = gen_slot();
            k += 4;
        }
    }
    while (k < len) prog[k++] = 0;                              /* nops up to the end */
    for (int i = 0; i < 140; i++)                               /* past the end: j end, nop, j end, nop ...: a jump that overshoots comes back */
        prog[len + i] = (i & 1) ? 0 : 0x08000000u | (((T_BASE + 4u * (uint32_t)len) & 0x0FFFFFFFu) >> 2);
    return len;
}


static gte_t gte0;                                              /* the GTE state every pass starts from */

/* one trial of the self test: the program, the start state, from the number of the trial (the same for the Pi and the PC test) */
void psx_jit_make_trial(int t, uint32_t *prog, int *len_out, uint32_t *regs0, uint32_t *hi0, uint32_t *lo0, uint8_t *data0, uint8_t *scr0, gte_t *gte)
{
    rs_ = 0x1234567u + (uint32_t)t * 7919u;
    const int len = 24 + rndn(40);
    build_program(prog, len);
    for (int i = 0; i < 0x400; i++) data0[i] = (uint8_t)rnd32();
    for (int i = 0; i < 0x400; i++) scr0[i] = (uint8_t)rnd32();
    *hi0 = rnd32(); *lo0 = rnd32();
    for (int i = 0; i < 32; i++) regs0[i] = rnd32();
    for (int i = 0; i < 32; i++) { gte->d[i] = (int32_t)rnd32(); gte->c[i] = (int32_t)rnd32(); }
    regs0[0] = 0; regs0[13] = T_BASE + 4u * (uint32_t)len;      /* r13: the jump register; if a branch skips the lui/ori that set it, the jump goes to the end */
    regs0[27] = 0x1F800200u;                                    /* the scratchpad base of the generated loads and stores */
    regs0[29] = T_DATA;                                         /* the loads and stores use 0x200 +- 0x80 from it */
    *len_out = len;
}

#ifdef PSX_JIT_HOST
/* the translator only: a block's ARM words for the PC (tests/psx/jit_dump.c) */
int psx_jit_dump(uint32_t pc, uint32_t *out, int max)
{
    psx_jit_flush_code();
    int p0, p1;
    uint32_t *code = (uint32_t *)compile(pc, &p0, &p1);
    if (!code) return 0;
    const int n = (int)(cp - code);
    for (int k = 0; k < n && k < max; k++) out[k] = code[k];
    return n;
}
int psx_jit_run_block(void) { return 0; }
int psx_jit_selftest(int trials, char *msg, int n) { (void)trials; snprintf(msg, (size_t)n, "translator only"); return -1; }
#else
int psx_jit_run_block(void)
{
    const uint32_t pc = C.pc;
    block_t *b = &table[(pc >> 2) & ((1u << TABLE_BITS) - 1)];
    if (b->pc != pc || (b->code && (b->g0 != page_gen[b->p0] || b->g1 != page_gen[b->p1]))) {
        int p0 = 0, p1 = 0;
        const uint32_t pp = phys_of(pc);
        if (pp < 0x00800000u && page_bad[(pp & 0x1FFFFFu) >> PAGE_SHIFT]) { b->pc = pc; b->code = NULL; b->p0 = b->p1 = 0; b->g0 = b->g1 = 0; return 0; }
        void *code = compile(pc, &p0, &p1);
        if (!code && arena_used + 8192 > ARENA_SIZE) {                    /* the arena is full: start again */
            psx_jit_flush_code();
            b = &table[(pc >> 2) & ((1u << TABLE_BITS) - 1)];
            code = compile(pc, &p0, &p1);
        }
        b->pc = pc;
        b->code = code;
        b->p0 = (uint16_t)p0; b->p1 = (uint16_t)p1;
        b->g0 = page_gen[p0]; b->g1 = page_gen[p1];
        if (code && p0 < NPAGES) { psx_jit_pageflags[p0] = 1; psx_jit_pageflags[p1] = 1; }
    }
    if (!b->code) return 0;
    cur_p0 = b->p0; cur_p1 = b->p1;
    stale_cur = 0;
    C.jit_exc = 0;
    const uint32_t used = psx_jit_enter(b->code, &C);
    cur_p0 = cur_p1 = -1;
    return (int)used;
}

/* ------------------------------------------------------------------ the self test */
/* Random programs run twice from the same start, by the interpreter and by compiled code; the registers, HI, LO and the data
 * area must come out the same. It uses the CPU state and a part of RAM: call it with no game running. */
/* runs prog (len instructions and the end) from the given start by the interpreter and by compiled code; 0 if they agree.
 * what: the first difference (1..31 a register, 32 hi, 33 lo, 34 data) and the two values */
extern int psx_hooks;
static int run_pair(const uint32_t *prog, int len, const uint32_t *regs0, uint32_t hi0, uint32_t lo0, const uint8_t *data0, const uint8_t *scr0,
                    int *what, uint32_t *va, uint32_t *vb)
{
    gte_t gte_a;
    uint32_t regs_a[32], hi_a = 0, lo_a = 0;
    uint8_t data_a[0x400], scr_a[0x400];
    const uint32_t end = T_BASE + 4u * (uint32_t)len;
    memcpy(psx_ram + (T_BASE & 0x1FFFFF), prog, (size_t)(len + 140) * 4);
    for (int pass = 0; pass < 2; pass++) {
        memcpy(psx_ram + (T_DATA & 0x1FFFFF), data0, 0x400);
        memcpy(psx_scratch, scr0, 0x400);
        memcpy(C.r, regs0, 32 * sizeof(uint32_t));
        C.hi = hi0; C.lo = lo0;
        G = gte0;
        C.pc = T_BASE; C.npc = T_BASE + 4; C.next_bd = 0; C.in_bd = 0; C.ld_r = 0; C.ld_v = 0; C.jit_exc = 0;
        psx_jit_flush_code();
        psx_jit_on = pass;                                      /* 0: the interpreter, 1: compiled code */
        int steps = 0;
        for (; C.pc != end && steps < 3000; steps++) psx_cpu_run(1);
        if (pass == 0 && C.pc != end) return -1;                 /* the program does not end (a jr back whose lui was skipped): not a test */
        if (C.ld_r) { C.r[C.ld_r] = C.ld_v; C.ld_r = 0; }       /* the interpreter stops with the last load not landed yet */
        if (pass == 0) {
            memcpy(regs_a, C.r, sizeof regs_a); hi_a = C.hi; lo_a = C.lo; gte_a = G;
            memcpy(data_a, psx_ram + (T_DATA & 0x1FFFFF), sizeof data_a);
            memcpy(scr_a, psx_scratch, sizeof scr_a);
        }
    }
    for (int i = 1; i < 32; i++) if (C.r[i] != regs_a[i]) { *what = i; *va = regs_a[i]; *vb = C.r[i]; return 1; }
    if (C.hi != hi_a) { *what = 32; *va = hi_a; *vb = C.hi; return 1; }
    if (C.lo != lo_a) { *what = 33; *va = lo_a; *vb = C.lo; return 1; }
    if (memcmp(&G, &gte_a, sizeof G) != 0) { *what = 35; *va = 0; *vb = 0; for (int i = 0; i < 32; i++) { if (G.d[i] != gte_a.d[i]) { *what = 40 + i; *va = (uint32_t)gte_a.d[i]; *vb = (uint32_t)G.d[i]; return 1; } if (G.c[i] != gte_a.c[i]) { *what = 80 + i; *va = (uint32_t)gte_a.c[i]; *vb = (uint32_t)G.c[i]; return 1; } } return 1; }
    for (int i = 0; i < 0x400; i++) {
        const uint8_t b = psx_ram[(T_DATA & 0x1FFFFF) + (uint32_t)i];
        if (b != data_a[i]) { *what = 34; *va = (uint32_t)i << 8 | data_a[i]; *vb = (uint32_t)i << 8 | b; return 1; }
        if (psx_scratch[i] != scr_a[i]) { *what = 34; *va = (uint32_t)(0x400 + i) << 8 | scr_a[i]; *vb = (uint32_t)(0x400 + i) << 8 | psx_scratch[i]; return 1; }
    }
    return 0;
}

int psx_jit_selftest(int trials, char *msg, int n)
{
    int bad = 0;
    msg[0] = 0;
    const int hooks_saved = psx_hooks, on_saved = psx_jit_on;
    psx_hooks = 0;
    C.cop0[12] = 0x40000000u;
    for (int t = 0; t < trials; t++) {
        uint32_t prog[64 + 144];
        int len;
        uint8_t data0[0x400], scr0[0x400];
        uint32_t regs0[32], hi0, lo0;
        psx_jit_make_trial(t, prog, &len, regs0, &hi0, &lo0, data0, scr0, &gte0);
        int what = 0;
        uint32_t va = 0, vb = 0;
        const int rp = run_pair(prog, len, regs0, hi0, lo0, data0, scr0, &what, &va, &vb);
        if (rp <= 0) continue;
        if (!bad) {
            /* the shortest prefix that still differs: the instructions after it are nops */
            uint32_t cut[64 + 144];
            int minl = len;
            for (int l = 1; l <= len; l++) {
                memcpy(cut, prog, sizeof cut);
                for (int k = l; k < len; k++) cut[k] = 0;
                int w2 = 0;
                uint32_t a2 = 0, b2 = 0;
                if (run_pair(cut, len, regs0, hi0, lo0, data0, scr0, &w2, &a2, &b2) > 0) { minl = l; what = w2; va = a2; vb = b2; break; }
            }
            int k = snprintf(msg, (size_t)n, "t%d prefix %d %s%d %08x/%08x:", t, minl, what >= 40 ? "gte" : what == 34 ? "mem" : what == 32 ? "hi" : what == 33 ? "lo" : "r", what < 32 ? what : what >= 40 ? what - 40 : 0, (unsigned)va, (unsigned)vb);
            for (int j = minl - 4 < 0 ? 0 : minl - 4; j < minl && k < n - 10; j++) k += snprintf(msg + k, (size_t)(n - k), " %08x", (unsigned)prog[j]);
        }
        bad++;
    }
    psx_hooks = hooks_saved;
    psx_jit_on = on_saved;
    return bad;
}
#endif

#endif
