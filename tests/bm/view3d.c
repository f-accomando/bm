/* Host helper: renders a test 3D scene to a PPM so the rasterizer can be
 * inspected by eye. Usage: view3d out.ppm */
#include <stdio.h>
#include "bm/r3d.h"

static uint8_t glyphs[256 * 16];
static const font_t font = { 8, 16, glyphs };
static uint16_t fb[360 * 640];

int main(int argc, char **argv)
{
    g16_t g; r3d_t r; r3d_mesh_t sphere, cube;
    g16_target(&g, fb, 640, 640, 360, &font);
    r3d_init(&r, &g);
    r3d_mesh_sphere(&sphere, 12, 24, 0x3060FF, 0xFFFFFF);
    r3d_mesh_cube(&cube, 0xFF8020);
    g16_cls(&g, g16_rgb(20, 20, 40));
    r3d_zclear(&r);
    r3d_camera(&r, 0, 1.5f, -6, 0, -0.2f, 60);
    r3d_draw(&r, &sphere, (v3_t){ -1.2f, 0, 0 }, 0.3f, 0.5f, 0, 1.2f);
    r3d_draw(&r, &cube, (v3_t){ 1.3f, 0, 0.5f }, 0.5f, 0.7f, 0.2f, 1.0f);
    r3d_draw(&r, &cube, (v3_t){ 0.2f, -0.2f, -0.2f }, 0.1f, 0.3f, 0, 0.6f);   /* intersects: z-buffer */
    g16_tri(&g, 20, 340, 120, 250, 200, 350, g16_rgb(0, 255, 0));
    FILE *f = fopen(argc > 1 ? argv[1] : "view3d.ppm", "wb");
    fprintf(f, "P6 640 360 255\n");
    for (int i = 0; i < 640 * 360; i++) {
        uint32_t c = g16_to_rgb24(fb[i]);
        fputc(c >> 16, f); fputc(c >> 8 & 255, f); fputc(c & 255, f);
    }
    fclose(f);
    printf("tris in %u drawn %u pixels %u\n", r.tris_in, r.tris_drawn, r.pixels);
    return 0;
}
