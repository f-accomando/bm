/*
 * QEMU virt (tests): PL011 serial port, ramfb for the screen (configured
 * through fw_cfg), PSCI over HVC. Run by tests/rgb30/qemu_test.py with
 *   -M virt,gic-version=3 -cpu cortex-a55 -m 512M -device ramfb
 */
#ifdef PLAT_VIRT
#include "plat.h"
#include "io.h"
#include "lib/printf.h"
#include "kernel/config.h"

#include <stdlib.h>
#include <string.h>

#define UART        0x09000000u
#define UART_DR     (UART + 0x00)
#define UART_FR     (UART + 0x18)
#define FR_TXFF     (1u << 5)
#define FR_RXFE     (1u << 4)

#define FWCFG       0x09020000u
#define FWCFG_DATA  (FWCFG + 0x00)
#define FWCFG_SEL   (FWCFG + 0x08)
#define FWCFG_DMA   (FWCFG + 0x10)
#define FW_CFG_FILE_DIR 0x19

void plat_uart_init(void)
{
}

void plat_uart_putc(char c)
{
    while (readl(UART_FR) & FR_TXFF)
        ;
    writel(UART_DR, (uint8_t)c);
}

int plat_uart_getc(void)
{
    if (readl(UART_FR) & FR_RXFE)
        return -1;
    return (int)(readl(UART_DR) & 0xff);
}

void plat_led(int green, int red)
{
    (void)green; (void)red;
}

/* no buttons: the tests press them through the serial port (pad.c) */
uint32_t plat_buttons(void)
{
    return 0;
}

void plat_sticks(int16_t axes[4])
{
    axes[0] = axes[1] = axes[2] = axes[3] = 0;
}

/* --- fw_cfg / ramfb --- */

static void fwcfg_select(uint16_t key)
{
    *(volatile uint16_t *)(uintptr_t)FWCFG_SEL = __builtin_bswap16(key);
}

static void fwcfg_read(void *buf, uint32_t len)
{
    uint8_t *p = buf;
    while (len--)
        *p++ = *(volatile uint8_t *)(uintptr_t)FWCFG_DATA;
}

static int ramfb_key = -1;

static int find_ramfb(void)
{
    uint32_t n;
    fwcfg_select(FW_CFG_FILE_DIR);
    fwcfg_read(&n, 4);
    n = __builtin_bswap32(n);
    for (uint32_t i = 0; i < n && i < 256; i++) {
        struct { uint32_t size; uint16_t select, reserved; char name[56]; } f;
        fwcfg_read(&f, sizeof f);
        if (strcmp(f.name, "etc/ramfb") == 0)
            return __builtin_bswap16(f.select);
    }
    return -1;
}

struct __attribute__((packed)) ramfb_cfg {
    uint64_t addr;
    uint32_t fourcc, flags, width, height, stride;
};

static plat_mode_t mode;
static char info[96];

static int ramfb_write(uintptr_t addr)
{
    static volatile struct ramfb_cfg cfg __attribute__((aligned(16)));
    static volatile struct { uint32_t control, length; uint64_t address; } dma __attribute__((aligned(16)));
    if (ramfb_key < 0)
        return -1;
    /* always XRGB8888: QEMU's ramfb has no RGB565 (plat_display_show
     * converts the 16-bit pages) */
    cfg.addr = __builtin_bswap64(addr);
    cfg.fourcc = __builtin_bswap32(0x34325258u);    /* XR24 */
    cfg.flags = 0;
    cfg.width = __builtin_bswap32(mode.w);
    cfg.height = __builtin_bswap32(mode.h);
    cfg.stride = __builtin_bswap32(mode.depth == 16 ? mode.w * 4 : mode.pitch ? mode.pitch : mode.w * 4);
    dma.control = __builtin_bswap32(((uint32_t)ramfb_key << 16) | 0x08 /* select */ | 0x10 /* write */);
    dma.length = __builtin_bswap32(sizeof cfg);
    dma.address = __builtin_bswap64((uintptr_t)&cfg);
    __asm__ volatile("dsb sy" ::: "memory");
    *(volatile uint64_t *)(uintptr_t)FWCFG_DMA = __builtin_bswap64((uintptr_t)&dma);
    for (int i = 0; i < 1000000; i++) {
        uint32_t c = __builtin_bswap32(dma.control);
        if (c == 0)
            return 0;
        if (c & 1)
            return -2;
    }
    return -3;
}

/* a 16-bit page as XRGB8888, for ramfb */
static uint32_t *wide;

static uintptr_t widen(uintptr_t addr)
{
    for (uint32_t y = 0; y < mode.h; y++) {
        const uint16_t *s = (const uint16_t *)(addr + (uintptr_t)y * mode.pitch);
        uint32_t *d = wide + (size_t)y * mode.w;
        for (uint32_t x = 0; x < mode.w; x++) {
            uint32_t c = s[x];
            uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
            d[x] = (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
        }
    }
    return (uintptr_t)wide;
}

int plat_display_init(const plat_mode_t *m, uintptr_t addr)
{
    if (ramfb_key < 0)
        ramfb_key = find_ramfb();
    mode = *m;                          /* shown 1:1: ramfb does not scale */
    if (m->depth == 16) {
        free(wide);
        wide = malloc((size_t)m->w * m->h * 4);
        if (!wide)
            return -1;
        addr = widen(addr);
    }
    int r = ramfb_write(addr);
    ksnprintf(info, sizeof info, "ramfb %lux%lu %lu bpp (fw_cfg key %d): %s",
              m->w, m->h, m->depth, ramfb_key, r == 0 ? "ok" : "error");
    return r;
}

void plat_display_show(uintptr_t addr)
{
    ramfb_write(mode.depth == 16 && wide ? widen(addr) : addr);
}

int plat_display_wait_vsync(void)
{
    return 0;
}

const char *plat_display_info(void)
{
    return info;
}

int plat_display_problem(void)
{
    return 0;
}

/* no battery in QEMU; for the tests bm/config.txt can give one:
 * test_battery=mV[,charger state] */
int plat_battery(int *mv, int *charge)
{
    const char *t = config_get("test_battery");
    *mv = -1;
    *charge = -1;
    if (!t)
        return -1;
    *mv = (int)strtol(t, NULL, 10);
    const char *c = strchr(t, ',');
    *charge = c ? (int)strtol(c + 1, NULL, 10) : 0;
    return 0;
}

/* the cable: in when test_battery gives a charger state (1 charging, 2 full) */
int plat_power_in(void)
{
    int mv, charge;
    if (plat_battery(&mv, &charge) != 0)
        return -1;
    return charge >= 1;
}

/* --- PSCI --- */

static void psci(uint32_t fn)
{
    register uint64_t x0 __asm__("x0") = fn;
    __asm__ volatile("hvc #0" : "+r"(x0) :: "x1", "x2", "x3", "memory");
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

#ifdef PLAT_VIRT
#include "lib/printf.h"
/* no Bluetooth chip in QEMU's virt machine (its H5 and Realtek code are
 * tested on the PC against a simulated chip: tests/bt/h5_test.c) */
#include "bt/btuart.h"
void btuart_init(uint32_t baud)                 { (void)baud; }
void btuart_set_baud(uint32_t baud)             { (void)baud; }
void btuart_set_flow(int on)                    { (void)on; }
void btuart_write(const void *buf, uint32_t n)  { (void)buf; (void)n; }
int  btuart_read(uint32_t timeout_us)           { (void)timeout_us; return -1; }
int  btuart_ready(void)                         { return 0; }
void btuart_drain(void)                         { }
unsigned btuart_overruns(void)                  { return 0; }

int rk_bt_bringup(uint32_t *baud_out)
{
    (void)baud_out;
    kprintf("bt: no Bluetooth controller in QEMU\n");
    return -1;
}
#endif

#ifdef PLAT_VIRT
/* no WiFi chip in QEMU's virt machine */
#include "wifi/wifi.h"
static const unsigned char no_mac[6];
int wifi_probe(void)
{
    kprintf("wifi: no WiFi chip in QEMU\n");
    return -1;
}
int wifi_start(void)                        { return wifi_probe(); }
int wifi_scan(void)                         { return -1; }
int wifi_connect(void)                      { return -1; }
int wifi_connect_saved(void)                { return -1; }
int wifi_linked(void)                       { return 0; }
int wifi_up(void)                           { return 0; }
void wifi_leave(void)                       { }
const unsigned char *wifi_mac(void)         { return no_mac; }
void wifi_poll(void)                        { }
int wifi_recv(void *buf, int max)           { (void)buf; (void)max; return 0; }
int wifi_send(const void *eth, int len)     { (void)eth; (void)len; return -1; }
#endif
