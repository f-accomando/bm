#include "sysinfo.h"
#include "arch/mmu.h"
#include "drivers/board.h"
#include "drivers/prop.h"
#include "lib/heap.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "lib/printf.h"
#include "fs/fat.h"
#include "kernel/version.h"

#include <string.h>

#include <malloc.h>

extern char __kernel_end[];

/* The mode the firmware started the kernel in (start.S): SVC on the
 * ARM1176, HYP on the Cortex-A53 of the Pi Zero 2 W. */
uint32_t boot_mode;

static const char *mode_name(uint32_t m)
{
    switch (m) {
    case 0x13: return "SVC";
    case 0x1A: return "HYP";
    case 0x16: return "MON";
    default:   return "?";
    }
}

const char *sysinfo_cpu(void)
{
    uint32_t midr;
    __asm__ volatile("mrc p15, 0, %0, c0, c0, 0" : "=r"(midr));
    switch (midr >> 4 & 0xFFF) {
    case 0xB76: return "ARM1176";
    case 0xC07: return "Cortex-A7";
    case 0xD03: return "Cortex-A53";
    default:    return "ARM";
    }
}

void sysinfo_print(void)
{
    uint32_t v[2];

    v[0] = 0;
    if (prop_query(PROP_GET_BOARD_REVISION, v, 1) == 0)
        kprintf("board revision : %08lx (Raspberry %s)\n", v[0], board()->name);

    v[0] = v[1] = 0;
    if (prop_query(PROP_GET_ARM_MEMORY, v, 2) == 0)
        kprintf("ARM memory     : %08lx + %lu MB\n", v[0], v[1] >> 20);

    v[0] = v[1] = 0;
    if (prop_query(PROP_GET_VC_MEMORY, v, 2) == 0)
        kprintf("GPU memory     : %08lx + %lu MB\n", v[0], v[1] >> 20);

    uint32_t midr;
    __asm__ volatile("mrc p15, 0, %0, c0, c0, 0" : "=r"(midr));
    kprintf("CPU            : %s (MIDR %08lx), started in %s mode\n", sysinfo_cpu(), midr,
            mode_name(boot_mode));
    kprintf("ARM clock      : %lu MHz\n", prop_clock_rate(CLOCK_ARM) / 1000000);
    kprintf("core clock     : %lu MHz\n", prop_clock_rate(CLOCK_CORE) / 1000000);
    kprintf("UART clock     : %lu Hz, %lu baud\n", uart_clock(), (uint32_t)UART_BAUD);

    v[0] = 0; v[1] = 0;
    if (prop_query(PROP_GET_TEMPERATURE, v, 2) == 0 && v[1])
        kprintf("SoC temp       : %lu.%lu C\n", v[1] / 1000, (v[1] % 1000) / 100);

    kprintf("MMU/caches     : %s\n", mmu_enabled() ? "on (I+D cache, branch prediction)" : "off");
    kprintf("kernel end     : %p\n", (void *)__kernel_end);
    sysinfo_print_heap();
    kprintf("uptime         : %lu ms\n", timer_ticks() / 1000);
}

void sysinfo_print_heap(void)
{
    struct mallinfo mi = mallinfo();
    uint32_t total = heap_end() - heap_start();
    uint32_t brk = heap_brk() - heap_start();
    kprintf("heap           : %p-%p, %lu MB, %lu KB reserved, %lu KB in use\n",
            (void *)heap_start(), (void *)heap_end(), total >> 20,
            brk >> 10, (uint32_t)mi.uordblks >> 10);
}

/* Boot summary: fits in a few lines of the 80x21 console. */
void sysinfo_print_short(void)
{
    uint32_t rev[1] = { 0 }, arm[2] = { 0, 0 }, vc[2] = { 0, 0 }, temp[2] = { 0, 0 };
    prop_query(PROP_GET_BOARD_REVISION, rev, 1);
    prop_query(PROP_GET_ARM_MEMORY, arm, 2);
    prop_query(PROP_GET_VC_MEMORY, vc, 2);
    prop_query(PROP_GET_TEMPERATURE, temp, 2);

    kprintf("board %lx, ARM %lu MHz, core %lu MHz, RAM %lu+%lu MB (ARM+GPU), %lu.%lu C\n",
            rev[0], prop_clock_rate(CLOCK_ARM) / 1000000,
            prop_clock_rate(CLOCK_CORE) / 1000000, arm[1] >> 20, vc[1] >> 20,
            temp[1] / 1000, (temp[1] % 1000) / 100);
    kprintf("%s from %s, MMU+caches %s, heap %lu MB, UART %lu Hz\n", sysinfo_cpu(),
            mode_name(boot_mode), mmu_enabled() ? "on" : "off",
            (uint32_t)((heap_end() - heap_start()) >> 20), uart_clock());
}

/* The main facts of the machine for the bm shell's box: label and value rows. Returns the rows. */
int sysinfo_summary(char label[][14], char value[][40], int max)
{
    uint32_t arm[2] = { 0, 0 }, vc[2] = { 0, 0 };
    prop_query(PROP_GET_ARM_MEMORY, arm, 2);
    prop_query(PROP_GET_VC_MEMORY, vc, 2);
    const uint32_t mhz_arm = prop_clock_rate(CLOCK_ARM) / 1000000;
    const uint32_t mhz_core = prop_clock_rate(CLOCK_CORE) / 1000000;
    const uint32_t mhz_v3d = prop_clock_rate(CLOCK_V3D) / 1000000;
    const uint32_t mhz_ram = prop_clock_rate(CLOCK_SDRAM) / 1000000;
    const board_t *b = board();
    const int wireless = b->wireless;
    const int zero2 = b->model == BOARD_ZERO_2W;

    char dev[28];                               /* "Pi Zero W rev 1.1" -> "Pi Zero W 1.1" */
    const char *rev = strstr(b->name, " rev ");
    if (rev)
        ksnprintf(dev, sizeof dev, "%.*s %s", (int)(rev - b->name), b->name, rev + 5);
    else
        ksnprintf(dev, sizeof dev, "%s", b->name);

    int n = 0;
#define ROW(l, ...) do { if (n < max) { ksnprintf(label[n], 14, "%s", l); ksnprintf(value[n], 40, __VA_ARGS__); n++; } } while (0)
    ROW("Device", "Raspberry %s", dev);
    ROW("CPU", "%s, %s", zero2 ? "Cortex-A53 x4" : "ARM1176JZF-S", BOARD_SOC);
    ROW("GPU", "VideoCore IV");
    ROW("RAM", "%lu MB (+ %lu MB GPU)", arm[1] >> 20, vc[1] >> 20);
    ROW("Kernel", "bm %s", bm_version);
    ROW("Storage", "%s", fat_describe());
    ROW("ARM clock", "%lu MHz", mhz_arm);
    ROW("Core clock", "%lu MHz (V3D %lu)", mhz_core, mhz_v3d);
    ROW("RAM clock", "%lu MHz", mhz_ram);
    ROW("WiFi", "%s", wireless ? "802.11n 2.4 GHz" : "none");
    ROW("Bluetooth", "%s", wireless ? "4.1 LE" : "none");
#undef ROW
    return n;
}
