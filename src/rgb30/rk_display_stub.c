/* Placeholder until the VOP2 + MIPI-DSI driver is in (rk_display.c). */
#ifdef PLAT_RK3566
#include "plat.h"

void plat_led(int green, int red)               { (void)green; (void)red; }
int  plat_display_init(uint32_t w, uint32_t h, uint32_t depth, uintptr_t addr)
{
    (void)w; (void)h; (void)depth; (void)addr;
    return -1;
}
void plat_display_show(uintptr_t addr)          { (void)addr; }
int  plat_display_wait_vsync(void)              { return 0; }
const char *plat_display_info(void)             { return "no display driver yet"; }
uint32_t plat_buttons(void)                     { return 0; }
void plat_sticks(int16_t axes[4])               { axes[0] = axes[1] = axes[2] = axes[3] = 0; }
#endif
