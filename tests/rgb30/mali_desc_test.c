/*
 * The Mali-G52's draw descriptors (src/rgb30/mali_desc.c) against the bytes
 * v7pack.py makes from Mesa's v7.xml (mali_desc_golden.h): same values,
 * byte for byte. Also the Tiler and Vertex jobs' layouts, and the shaders'
 * header (src/rgb30/mali_shaders.h).
 */
#include <stdio.h>
#include <string.h>

#include "rgb30/mali_desc.h"
#include "mali_desc_golden.h"
#include "rgb30/mali_shaders.h"

static int checks, failures;

static void same(const char *what, const uint8_t *got, const uint8_t *want, size_t n)
{
    checks++;
    if (memcmp(got, want, n) == 0)
        return;
    failures++;
    printf("FAIL %s:\n", what);
    for (size_t i = 0; i < n; i++)
        if (got[i] != want[i])
            printf("  byte %zu: got %02x, want %02x\n", i, got[i], want[i]);
}

static uint8_t b[MD_TILER_JOB_SIZE];

int main(void)
{
    md_primitive_t pr = { .mode = MD_TRIANGLES, .index_type = MD_INDEX_U16, .index_count = 12,
                          .indices = 0x123456780ull, .base_vertex = 5 };
    memset(b, 0, sizeof b);
    md_primitive(b, &pr);
    same("primitive", b, golden_primitive, sizeof golden_primitive);

    md_draw_t d = { .front_ccw = 1, .cull_back = 1, .position = 0x111100000040ull,
                    .uniform_buffers = 0x999900000240ull, .push_uniforms = 0xaaaa00000280ull,
                    .state = 0x222200000080ull, .attribute_buffers = 0x3333000000c0ull,
                    .attributes = 0x444400000100ull, .varying_buffers = 0x555500000140ull,
                    .varyings = 0x666600000180ull, .viewport = 0x7777000001c0ull,
                    .thread_storage = 0x888800000200ull };
    memset(b, 0, sizeof b);
    md_draw(b, &d);
    same("draw", b, golden_draw, sizeof golden_draw);

    md_renderer_state_t fs = { .fragment = 1, .shader = 0x987654300ull, .ubo_count = 1, .work_regs = 32,
                               .preload_regs = 1ull << 60, .push_count = 8, .pixel_kill = MD_KILL_WEAK_EARLY,
                               .zs_update = MD_KILL_WEAK_EARLY, .wait6 = 1, .wait7 = 1 };
    memset(b, 0, sizeof b);
    md_renderer_state(b, &fs);
    same("renderer state (fragment)", b, golden_renderer_state_fs, sizeof golden_renderer_state_fs);

    md_renderer_state_t vs = { .shader = 0x987654400ull, .attribute_count = 1, .varying_count = 1, .work_regs = 32,
                               .preload_regs = 3ull << 61 };
    memset(b, 0, sizeof b);
    md_renderer_state(b, &vs);
    same("renderer state (vertex)", b, golden_renderer_state_vs, sizeof golden_renderer_state_vs);

    memset(b, 0, sizeof b);
    md_blend_opaque(b, 0, MD_FMT_RGBA8_UNORM | MD_ORDER_BGRA, MD_REG_F32);
    same("blend", b, golden_blend, sizeof golden_blend);

    memset(b, 0, sizeof b);
    md_local_storage(b);
    same("local storage", b, golden_local_storage, sizeof golden_local_storage);

    memset(b, 0, sizeof b);
    md_uniform_buffer(b, 0x3c0004100ull, 2);
    same("uniform buffer", b, golden_uniform_buffer, sizeof golden_uniform_buffer);

    memset(b, 0, sizeof b);
    md_invocation(b, 3);
    same("invocation", b, golden_invocation, sizeof golden_invocation);

    memset(b, 0, sizeof b);
    md_viewport(b, 0.0f, 0.0f, 360.0f, 360.0f, 0, 0, 359, 359);
    same("viewport", b, golden_viewport, sizeof golden_viewport);

    memset(b, 0, sizeof b);
    md_tiler_heap(b, 0x3c0100000ull, 0x100000);
    same("tiler heap", b, golden_tiler_heap, sizeof golden_tiler_heap);

    memset(b, 0, sizeof b);
    md_tiler_context(b, 360, 360, 0x28, 0x3c0008000ull);
    same("tiler context", b, golden_tiler_context, sizeof golden_tiler_context);

    memset(b, 0, sizeof b);
    md_attribute(b, 3, 0x3ab5, -4, 1);
    same("attribute", b, golden_attribute, sizeof golden_attribute);

    memset(b, 0, sizeof b);
    md_attribute_buffer(b, 0x3c0123400ull, 16, 4800);
    same("attribute buffer", b, golden_attribute_buffer, sizeof golden_attribute_buffer);

    /* the tiler job: the pieces at their offsets, the header untouched; the invocation over the index count */
    uint8_t j[MD_TILER_JOB_SIZE];
    md_primitive_t tri = { .mode = MD_TRIANGLES, .index_count = 3 };
    memset(j, 0, sizeof j);
    md_tiler_job(j, &tri, 0x3c0004000ull, &d);
    uint8_t one[MD_DRAW_SIZE] = { 0 };
    md_draw(one, &d);
    same("tiler job: draw at 128", j + MD_TJ_DRAW, one, sizeof one);
    same("tiler job: invocation at 32", j + MD_TJ_INVOCATION, golden_invocation, sizeof golden_invocation);
    uint64_t tp;
    memcpy(&tp, j + MD_TJ_TILER, 8);
    checks++;
    if (tp != 0x3c0004000ull) {
        failures++;
        printf("FAIL tiler pointer %llx\n", (unsigned long long)tp);
    }
    uint8_t zero[32] = { 0 };
    same("tiler job: header stays zero", j, zero, sizeof zero);

    /* a batch: the draws' addresses summed by the index, 64 / 8 / 32 bytes apart */
    md_draw_t bd = d;
    bd.position = 0x3c0006000ull;
    bd.uniform_buffers = 0x3c0006500ull;
    bd.push_uniforms = 0x3c0006600ull;
    static uint8_t bj[4 * MD_TILER_JOB_SIZE];
    checks++;
    if (md_tiler_batch(bj, MD_TILER_JOB_SIZE, 4, &tri, 0x3c0004000ull, &bd) != 0) {
        failures++;
        printf("FAIL tiler batch: 4 jobs refused\n");
    }
    for (int i = 0; i < 4; i++) {
        md_draw_t di = bd;
        di.position += (uint64_t)i * 64;
        di.uniform_buffers += (uint64_t)i * 8;
        di.push_uniforms += (uint64_t)i * 32;
        uint8_t want[MD_TILER_JOB_SIZE];
        memset(want, 0, sizeof want);
        md_tiler_job(want, &tri, 0x3c0004000ull, &di);
        char name[32];
        snprintf(name, sizeof name, "tiler batch: job %d", i);
        same(name, bj + i * MD_TILER_JOB_SIZE, want, sizeof want);
    }
    checks++;
    if (md_tiler_batch(bj, MD_TILER_JOB_SIZE, 0, &tri, 0x3c0004000ull, &bd) != -1) {
        failures++;
        printf("FAIL tiler batch: n = 0 accepted\n");
    }
    checks++;
    if (md_tiler_batch(bj, MD_TILER_JOB_SIZE, 9, &tri, 0x3c0004000ull, &bd) != -1) {
        failures++;
        printf("FAIL tiler batch: n = 9 accepted\n");
    }

    /* the vertex job: invocation at 32, parameters at 40 (job task split 5 in bits 26-29), draw at 64 */
    uint8_t v[MD_VERTEX_JOB_SIZE];
    memset(v, 0, sizeof v);
    md_vertex_job(v, 3, &d);
    same("vertex job: invocation at 32", v + 32, golden_invocation, sizeof golden_invocation);
    same("vertex job: draw at 64", v + MD_VJ_DRAW, one, sizeof one);
    checks++;
    uint32_t par;
    memcpy(&par, v + MD_VJ_PARAMETERS, 4);
    if (par != 5u << 26) {
        failures++;
        printf("FAIL vertex job parameters %08x\n", par);
    }

    /* the shaders made by tools/mali/mkshader.c: whole 8-byte clauses, the fragment shader pushes its colour */
    checks++;
    if (sizeof BM_VS_bin % 8 || sizeof BM_FS_bin % 8 || BM_FS_PUSH_COUNT != 8 || BM_VS_PUSH_COUNT != 0 ||
        BM_FS_push[4][0] != 0 || BM_FS_push[4][1] != 0 || BM_FS_push[7][1] != 12) {
        failures++;
        printf("FAIL shaders: the headers' sizes or the colour's push words are not the expected ones\n");
    }

    printf("mali_desc_test: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
