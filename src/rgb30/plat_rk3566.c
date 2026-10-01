/*
 * RGB30 (RK3566): serial console on UART2 (a DesignWare 8250, left at
 * 1500000 8N1 by U-Boot), PSCI through TF-A. The display, the LEDs and the
 * rest live in their own files (rk_*.c).
 */
#ifdef PLAT_RK3566
#include "plat.h"
#include "io.h"

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

static void psci(uint32_t fn)
{
    register uint64_t x0 __asm__("x0") = fn;
    __asm__ volatile("smc #0" : "+r"(x0) :: "x1", "x2", "x3", "memory");
}

void plat_reset(void)
{
    psci(0x84000009u);          /* SYSTEM_RESET */
    for (;;)
        __asm__ volatile("wfe");
}

void plat_poweroff(void)
{
    psci(0x84000008u);          /* SYSTEM_OFF */
    for (;;)
        __asm__ volatile("wfe");
}
#endif
