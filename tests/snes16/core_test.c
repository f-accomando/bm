/*
 * core_test.c: standalone host tests for core/src/bm/snes16.c + snes_ppu.c + snes_dsp.c.
 * Links with ZERO dependency on snes_cpu.c, snes_bus.c, or snes_spc.c.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmnative.h"
#include "snes16.h"

static uint32_t dummy_clk(void) { static uint32_t t; return t += 100; }

int main(void)
{
    int checks = 0;
    s16_set_clock(dummy_clk);
    s16_reset(224);

    /* 1. Mode 1 BG1 tile + CGRAM + HDMA sky gradient */
    s16_cgram_set(0, 0x0000);
    s16_cgram_set(1, 0x001F); /* pure red in RGB555 (r=31 -> rgb565 0xF800) */
    s16_cgram_set(128 + 1, 0x03E0); /* pure green sprite pixel (g=31 -> rgb565 0x07E0) */

    uint8_t solid_tile[64];
    memset(solid_tile, 1, sizeof solid_tile);
    s16_tile_write(0x0020, 4, solid_tile); /* BG1 tile 1 (keep tile 0 transparent) */
    s16_tile_write(0x8000, 4, solid_tile); /* OBJ tile 0 */

    s16_bg(0, 0x4000, 0x0000, 0);
    s16_obj_cfg(0, 0x8000, 0x2000);
    s16_map_set(0, 0, 0, 1); /* tile 1 at (0,0) */
    s16_mode(1, 1 | 16, 0, 15);
    s16_colormath(0x02, 0x20, 0x0000); /* backdrop fixed-colour gradient */
    s16_hdma_grad(0, 223, 0x0000, 0x7C00); /* black -> pure blue (b=31 -> rgb565 0x001F) */

    /* OAM sprite 0 at (16, 16) using tile 0, pal 0, prio 2 */
    s16_oam_set(0, 16, 16, 0, (2 << 4));
    s16_render();

    int lines = 0;
    const uint16_t *pic = s16_picture(&lines);
    assert(lines == 224);
    checks++;
    /* (0, 0) has BG1 tile 0 filled with colour 1 (pure red -> 0xF800) */
    assert(pic[0] == 0xF800);
    checks++;
    /* (16, 16) has OAM sprite 0 (pure green -> 0x07E0) */
    assert(pic[16 * 256 + 16] == 0x07E0);
    checks++;
    /* (200, 223) is backdrop at bottom scanline -> HDMA blue (0x001F) */
    assert(pic[223 * 256 + 200] == 0x001F);
    checks++;

    /* 2. Mid-frame Mode 7 split at line 120 */
    uint8_t m7px[64];
    memset(m7px, 2, sizeof m7px);
    s16_cgram_set(2, 0x7FFF); /* white */
    s16_m7_tile(1, m7px);
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++)
            s16_m7_map(x, y, 1);
    s16_hdma_mode_band(0, 119, 1, 1 | 16, 0);
    s16_hdma_mode_band(120, 223, 7, 1, 0);
    s16_hdma_m7_persp(120, 223, 512, 512, 0, 160, 112);
    s16_render();
    assert(pic[150 * 256 + 100] == 0xFFFF);
    checks++;

    /* 3. S-DSP PCM16 -> BRR encode & audio mix without SPC700 */
    int16_t pcm[128];
    for (int i = 0; i < 128; i++)
        pcm[i] = (i & 16) ? 12000 : -12000;
    int brr_bytes = s16_dsp_upload_pcm16(0, 0x0400, pcm, 128, 0);
    assert(brr_bytes == 8 * 9);
    checks++;
    s16_dsp_play(0, 0, 0x1000, 0xFF, 0xE0, 100, 100);
    s16_dsp_step(534);
    float mix[1600];
    memset(mix, 0, sizeof mix);
    int nonzero = snes_audio_mix(mix, 800, 1.0f);
    assert(nonzero == 1);
    checks++;

    /* 4. Fast C host dispatcher */
    int32_t args[4] = { 1, 20, 0, 15 };
    s16_host_call(BMN_HOST_S16_MODE, args, 4);
    s16_host_call(BMN_HOST_S16_RENDER, NULL, 0);
    snes16_stats_t st;
    s16_get_stats(&st);
    assert(st.us_ppu > 0 && st.us_dsp > 0);
    checks++;

    printf("snes16 core_test: %d checks passed\n", checks);
    return 0;
}
