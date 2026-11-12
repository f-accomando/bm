/* game_run_arm.c: game_run.c on the ARM1176 in QEMU (semihosting reads the files from the current folder): a ROM race on the ARM CPU loop, the SIMD PPU and the tile cache, to compare its pictures with the host ones.
 * Build in the Makefile style (see SNES_ARM_CC); the arguments are the macros GR_ROM, GR_FW, GR_FRAMES, GR_SCRIPT, GR_OUT. */
#define main game_run_main
#include "game_run.c"
#undef main
int main(void)
{
    char *av[] = { "game_run", GR_ROM, GR_FW, GR_FRAMES, GR_SCRIPT, GR_OUT, 0 };
    return game_run_main(6, av);
}
