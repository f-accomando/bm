/* snes_gsu_off.h: the byte offsets of the fields of gsu_t (snes_gsu.c) that the assembly loop (snes_gsu_arm.S) uses. snes_gsu.c checks them with _Static_assert on 32-bit ARM. */
#ifndef SNES_GSU_OFF_H
#define SNES_GSU_OFF_H
#define G_R       0         /* uint32_t r[16]: R0-R15, each below 65536 */
#define G_SZ      64
#define G_FCY     68
#define G_FOV     72
#define G_JT      76
#define G_BUDGET  80
#define G_MUL     84
#define G_ROMCYC  88
#define G_RAMCYC  92
#define G_CVALID  96
#define G_RM      100
#define G_RBASE   104
#define G_IPN     108
#define G_INSNS   112
#define G_CBR     116       /* uint16_t */
#define G_RAMADDR 118       /* uint16_t */
#define G_ALT     120       /* uint8_t: alt, b, sreg, dreg */
#define G_B       121
#define G_SREG    122
#define G_DREG    123
#define G_CFGR    124       /* uint8_t */
#define G_ROMBR   125
#define G_DIRTY   126
#define G_PBR     127
#define G_RBOK    128       /* uint8_t: the ROM buffer is loaded and R14 was not written since */
#define G_CACHE   256       /* uint8_t cache[512], 16-aligned */
#endif
