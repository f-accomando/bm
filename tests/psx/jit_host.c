/* A shared library for tests/psx/jit_unicorn.py: the interpreter and the translator of the recompiler built for the PC.
 * Python runs the ARM code the translator makes in the Unicorn emulator and compares it with the interpreter. */
#include "psx_int.h"

#include <stdio.h>
#include <stdlib.h>

int psx_jit_dump(uint32_t pc, uint32_t *out, int max);
extern int psx_hooks;

uint8_t *h_ram(void) { return psx_ram; }
uint8_t *h_scratch(void) { return psx_scratch; }
void *h_cpu(void) { return &psx_cpu; }
int h_cpu_size(void) { return (int)sizeof(cpu_t); }
int h_off_r(void) { return (int)offsetof(cpu_t, r); }
int h_off_hi(void) { return (int)offsetof(cpu_t, hi); }
int h_off_lo(void) { return (int)offsetof(cpu_t, lo); }
int h_off_pc(void) { return (int)offsetof(cpu_t, pc); }
int h_off_npc(void) { return (int)offsetof(cpu_t, npc); }
int h_off_nbd(void) { return (int)offsetof(cpu_t, next_bd); }

void h_init(void)
{
    psx_cpu_reset();
    psx_hooks = 0;
    psx_jit_on = 0;
    psx_cpu.cop0[12] = 0x40000000u;
}

/* the interpreter from the state in the CPU until the PC is end (the last load lands) */
void h_interp(uint32_t end, int max_steps)
{
    psx_jit_on = 0;
    for (int i = 0; i < max_steps && psx_cpu.pc != end; i++) psx_cpu_run(1);
    if (psx_cpu.ld_r) { psx_cpu.r[psx_cpu.ld_r] = psx_cpu.ld_v; psx_cpu.ld_r = 0; }
}

int h_dump(uint32_t pc, uint32_t *out, int max) { return psx_jit_dump(pc, out, max); }
void h_step(void) { psx_jit_on = 0; psx_cpu_run(1); }

void psx_jit_make_trial(int t, uint32_t *prog, int *len_out, uint32_t *regs0, uint32_t *hi0, uint32_t *lo0, uint8_t *data0, uint8_t *scr0, gte_t *gte);
/* a trial of the self test (the generator of src/bm/psx_jit.c): the program (len + 140 words), the registers, HI, LO, the data and scratchpad bytes, the GTE registers */
void h_gen(int t, uint32_t *prog, int *len, uint32_t *regs0, uint32_t *hi, uint32_t *lo, uint8_t *data, uint8_t *scr, int32_t *gte)
{
    gte_t g;
    psx_jit_make_trial(t, prog, len, regs0, hi, lo, data, scr, &g);
    memcpy(gte, &g, sizeof g);
}
void h_set_gte(const int32_t *g) { memcpy(&psx_gte, g, sizeof psx_gte); }
void h_get_gte(int32_t *g) { memcpy(g, &psx_gte, sizeof psx_gte); }
