/*
 * RGB30 (RK3566): serial console on UART2 (a DesignWare 8250, left at
 * 1500000 8N1 by U-Boot), PSCI through TF-A. The display, the LEDs and the
 * rest live in their own files (rk_*.c).
 */
#ifdef PLAT_RK3566
#include "plat.h"
#include "io.h"
#include "rk_pmic.h"
#include "rk_wlbt.h"
#include "rk_audio.h"

#define CRU             0xfdd20000u
#define CRU_GLB_SRST_FST 0xd4           /* the chip's first global soft reset (0xfdb9) */

#define UART2           0xfe660000u
#define UART_THR        0x00
#define UART_RBR        0x00
#define UART_LSR        0x14
#define LSR_DR          (1u << 0)
#define LSR_THRE        (1u << 5)

void plat_uart_init(void)
{
}

void plat_uart_putc(char c)
{
    /* bounded: with nothing on the pins the FIFO still drains */
    for (int i = 0; i < 100000 && !(readl(UART2 + UART_LSR) & LSR_THRE); i++)
        ;
    writel(UART2 + UART_THR, (uint8_t)c);
}

int plat_uart_getc(void)
{
    if (!(readl(UART2 + UART_LSR) & LSR_DR))
        return -1;
    return (int)(readl(UART2 + UART_RBR) & 0xff);
}

int plat_battery(int *mv, int *charge)
{
    *mv = rk817_battery_mv();
    int st = rk817_charge_state();
    *charge = st < 0 ? -1 : st == 4 ? 2 : (st >= 1 && st <= 3) ? 1 : 0;
    return *mv < 0 ? -1 : 0;
}

int plat_power_in(void)
{
    return rk817_plugged();                 /* SYS_STS bit 6: PLUG_IN_STS */
}

static void psci(uint32_t fn)
{
    register uint64_t x0 __asm__("x0") = fn;
    __asm__ volatile("smc #0" : "+r"(x0) :: "x1", "x2", "x3", "memory");
}

/* The sound, the screen, its backlight and the WiFi module off before the chip
 * restarts or the power goes: a restart of the chip alone keeps the PMIC's
 * rails and the PMU's GPIO as bm left them, and the next start should find
 * them as after power-on (the console used to stay on with a black screen
 * after an update's restart, 2026-10-05). The LEDs off too: if the screen
 * stays black, red on says bm started again (see docs/RGB30.md). */
static void quiet(void)
{
    rk_audio_off();                 /* the amplifier off first: no pop */
    plat_display_off();
    wlbt_power_off();
    plat_led(0, 0);
}

void plat_reset(void)
{
    quiet();
    psci(0x84000009u);          /* SYSTEM_RESET */
    /* still here: the firmware did not restart; the chip's own global
     * soft reset, as Linux's clock driver does */
    writel(CRU + CRU_GLB_SRST_FST, 0xfdb9u);
    for (;;)
        __asm__ volatile("wfe");
}

void plat_poweroff(void)
{
    quiet();
    rk817_power_off();          /* the PMIC cuts the power */
    psci(0x84000008u);          /* SYSTEM_OFF, if it did not */
    for (;;)
        __asm__ volatile("wfe");
}
#endif
