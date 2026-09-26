/*
 * s32 machine, native implementation of the shared spec (spec/s32/s32-spec.md,
 * v0.1). Same memory map, formats and semantics as lua32; conformance is
 * checked against the vectors in spec/s32/conformance.
 */
#ifndef S32_H
#define S32_H

#include <stddef.h>
#include <stdint.h>

#define S32_MEM_SIZE        0x1000000u      /* 24-bit flat address space */
#define S32_ADDR_MASK       0xFFFFFFu

#define S32_WRAM_BASE       0x000000u
#define S32_WRAM_END        0x020000u
#define S32_VRAM_BASE       0x020000u
#define S32_TILEMAP_BASE    0x020000u
#define S32_TILEMAP_W       128
#define S32_TILEMAP_H       128
#define S32_TILEMAP_BYTES   (S32_TILEMAP_W * S32_TILEMAP_H * 2)
#define S32_DIR_BASE        0x028000u
#define S32_DIR_BYTES       (2048 * 4)
#define S32_POOL_BASE       0x02A000u
#define S32_POOL_BYTES      (512u * 1024u)
#define S32_GFX_BANK_BYTES  (S32_DIR_BYTES + S32_POOL_BYTES)
#define S32_OAM_BASE        0x0AA000u
#define S32_OAM_SLOTS       512
#define S32_CGRAM_BASE      0x0AB000u
#define S32_CGRAM_BYTES     (8 * 256 * 3)
#define S32_PORTS_BASE      0x0AC800u
#define S32_PORT_INPUT      (S32_PORTS_BASE + 0)
#define S32_PORT_STAGE      (S32_PORTS_BASE + 1)
#define S32_PORT_SCROLL_X   (S32_PORTS_BASE + 2)
#define S32_PORT_SCROLL_Y   (S32_PORTS_BASE + 3)
#define S32_PORT_GFX_BANK   (S32_PORTS_BASE + 5)
#define S32_PORT_INPUT2     (S32_PORTS_BASE + 0x10)     /* .. INPUT8 at +0x16 */
#define S32_APU_BASE        0x0AC900u
#define S32_APU_END         0x0AC980u

#define S32_LOAD_ADDR       0x001000u
#define S32_SP_INIT         0x01FFFE
#define S32_MAX_STEPS       200000u

#define S32_SCREEN_W        320
#define S32_SCREEN_H        224

#define S32_FLAG_Z  0x01
#define S32_FLAG_N  0x02
#define S32_FLAG_C  0x04
#define S32_FLAG_V  0x08

enum s32_status {
    S32_OK = 0,
    S32_CRASH_OPCODE,
    S32_CRASH_STACK_OVERFLOW,
    S32_CRASH_STACK_UNDERFLOW,
    S32_CRASH_STEP_LIMIT,
};

typedef struct {
    char title[65];
    char author[33];
    const uint8_t *code;
    uint32_t code_size;
    const uint8_t *stages;      /* stage_count * S32_TILEMAP_BYTES */
    uint32_t stage_count;
    const uint8_t *gfx;         /* gfx_count * S32_GFX_BANK_BYTES */
    uint32_t gfx_count;
    const uint8_t *cgram;       /* NULL if absent */
    uint8_t code_type;          /* reserved0[0]: 0 = s32 machine code, 1 = Lua */
    uint8_t screen_mode;        /* reserved0[1]: 0 = 320x224, 1 = 384x224 (16:9) */
} s32_cart_t;

typedef struct {
    uint8_t *mem;               /* S32_MEM_SIZE bytes */
    uint16_t a, x, y;
    uint8_t flags;
    uint32_t pc;
    int32_t sp;
    uint16_t scroll_x, scroll_y;
    const s32_cart_t *cart;
    uint32_t steps;             /* instructions executed by the last tick */
    uint32_t crash_pc;
} s32_machine_t;

/* Parses and validates a .cart image (magic, version, CRC, bounds). The
 * cart keeps pointers into `data`. Returns 0 or -1 with a message. */
int s32_cart_parse(const uint8_t *data, size_t len, s32_cart_t *cart,
                   char *err, size_t errlen);

/* Allocates the 16 MiB address space (caller may pass its own). */
int  s32_init(s32_machine_t *m, uint8_t *mem);
void s32_install(s32_machine_t *m, const s32_cart_t *cart);

/* One 60 Hz tick: writes the input ports, runs from S32_LOAD_ADDR to HALT. */
enum s32_status s32_tick(s32_machine_t *m, const uint8_t input[8]);
const char *s32_status_str(enum s32_status st);

/* Renders the frame as 0x00RRGGBB pixels, S32_SCREEN_W x S32_SCREEN_H,
 * `stride` pixels per row. */
void s32_render(const s32_machine_t *m, uint32_t *out, uint32_t stride);

#endif
