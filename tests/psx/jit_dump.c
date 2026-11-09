/* Prints the ARM code the recompiler makes for a few MIPS words (the translator is built for the PC, nothing runs):
 *   jit_dump WORD WORD ...     the words are placed at 0x80010000; the block that starts there is printed as hex words,
 * one per line. tests/psx/jit_dump.py disassembles them. */
#include "psx_int.h"

#include <stdio.h>
#include <stdlib.h>

int psx_jit_dump(uint32_t pc, uint32_t *out, int max);

int main(int argc, char **argv)
{
    psx_cpu.cop0[12] = 0x40000000u;              /* the GTE is on */
    uint32_t w[64];
    int n = 0;
    for (int i = 1; i < argc && n < 62; i++) w[n++] = (uint32_t)strtoul(argv[i], NULL, 16);
    w[n++] = 0; w[n++] = 0;
    memcpy(psx_ram + 0x10000, w, (size_t)n * 4);
    uint32_t out[1024];
    const int m = psx_jit_dump(0x80010000u, out, 1024);
    for (int i = 0; i < m; i++) printf("%08x\n", (unsigned)out[i]);
    return 0;
}
