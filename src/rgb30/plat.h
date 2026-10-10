/*
 * The two machines the AArch64 kernel runs on: the RGB30 (RK3566) and,
 * for the tests, QEMU's virt machine (Cortex-A55, GICv3, PL011, ramfb).
 * Chosen at build time: rgb30.mk passes -DPLAT_RK3566 or -DPLAT_VIRT.
 */
#ifndef PLAT_H
#define PLAT_H

#include <stdint.h>

#if defined(PLAT_RK3566)
#define PLAT_NAME       "RGB30"
#define PLAT_RAM_START  0x00200000u     /* below: TF-A (BL31) */
#define PLAT_RAM_END    0x40000000u     /* 1 GiB of LPDDR4 */
#define PLAT_DEV_START  0xc0000000u     /* peripherals 0xfc000000-0xffffffff */
#define PLAT_DEV_END    0x100000000ull
#define PLAT_GICD       0xfd400000u
#define PLAT_GICR       0xfd460000u
#define PLAT_FB_START   0x3c000000u     /* display and GPU memory: top 64 MiB, uncached */
#define PLAT_HEAP_END   PLAT_FB_START
#elif defined(PLAT_VIRT)
#define PLAT_NAME       "QEMU virt"
#define PLAT_RAM_START  0x40000000u
#define PLAT_RAM_END    0x60000000u     /* -m 512M */
#define PLAT_DEV_START  0x00000000u
#define PLAT_DEV_END    0x40000000u
#define PLAT_GICD       0x08000000u
#define PLAT_GICR       0x080a0000u
#define PLAT_FB_START   0x5e000000u
#define PLAT_HEAP_END   0x50000000u     /* above: the SD image (sd_virt.c) */
#else
#error "PLAT_RK3566 or PLAT_VIRT"
#endif

#define IRQ_TIMER_PPI   30              /* EL1 physical timer (PPI 14) */

#define PLAT_FB_END     PLAT_RAM_END
/* M41: the last 4 MiB of it are the GPU's (page tables, jobs: mali.c) */
#define PLAT_GPU_START  (PLAT_FB_END - (4u << 20))

/* serial console: UART2 on the RGB30 (1500000 8N1, as U-Boot), the PL011 in QEMU */
void plat_uart_init(void);
void plat_uart_putc(char c);
int  plat_uart_getc(void);             /* -1: nothing waiting */

/* status LEDs (green, red): -1 leaves one as it is */
void plat_led(int green, int red);

/* The screen. A mode is an image in memory, w x h pixels, depth 32
 * (XRGB8888) or 16 (RGB565), pitch bytes per row, shown out_w x out_h big
 * (scaled by the display controller: nearest neighbour, or smooth) and
 * centred on the panel (720x720 on the RGB30); scale is how it was asked
 * (1..8 times, 0 as big as fits). QEMU shows the image 1:1.
 * plat_display_init() sets a mode; plat_display_show() makes the buffer at
 * `addr` the one shown, from the next frame. Returns 0 on success. */
#define PLAT_PANEL_W    720
#define PLAT_PANEL_H    720
typedef struct {
    uint32_t w, h, depth, pitch;
    uint32_t scale, out_w, out_h;
    int smooth;
} plat_mode_t;
int  plat_display_init(const plat_mode_t *m, uintptr_t addr);
void plat_display_show(uintptr_t addr);
/* waits for the start of the next frame (vertical blank); 0 if it cannot */
int  plat_display_wait_vsync(void);
/* diagnostics: a few lines on what the display driver did, and whether
 * something went wrong on the way (the red LED stays on) */
const char *plat_display_info(void);
int plat_display_problem(void);

/* buttons held now (PAD_* bits, rgb30/pad.h) and the analog sticks
 * (-32768..32767: left x, left y, right x, right y; negative: left, up) */
uint32_t plat_buttons(void);
void plat_sticks(int16_t axes[4]);

/* battery voltage in mV (-1: unknown) and charger state (-1 unknown,
 * 0 not charging, 1 charging, 2 full) */
int plat_battery(int *mv, int *charge);
/* the charger's cable: 1 in, 0 out, -1 unknown (one byte from the PMIC:
 * cheap enough to ask a few times a second, src/rgb30/battery.c) */
int plat_power_in(void);

/* the charge (0..100) of the RGB30's Li-ion cell from its voltage: a
 * table, linear in between, 0 at 3.45 V (the LED's low battery); on the
 * charger the voltage reads higher, an indication only */
static inline int battery_percent(int mv)
{
    static const short v[] = { 3450, 3610, 3690, 3730, 3770, 3800, 3840, 3870, 3950, 4020, 4110, 4180 };
    static const short pc[] = { 0, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
    if (mv <= v[0])
        return 0;
    for (int i = 1; i < (int)(sizeof v / sizeof v[0]); i++)
        if (mv < v[i])
            return pc[i - 1] + (pc[i] - pc[i - 1]) * (mv - v[i - 1]) / (v[i] - v[i - 1]);
    return 100;
}

/* PSCI through TF-A (SMC) on the RGB30, QEMU's PSCI (HVC) in the tests */
/* the display off (before a restart; the RGB30's panel and backlight) */
void plat_display_off(void);
void plat_reset(void) __attribute__((noreturn));
void plat_poweroff(void) __attribute__((noreturn));

#endif
