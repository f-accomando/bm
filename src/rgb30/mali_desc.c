/* The Mali-G52's descriptors for a draw (see mali_desc.h). Places from Mesa's v7.xml (MIT). */
#include "mali_desc.h"

void md_bits(uint8_t *p, unsigned start, unsigned size, uint64_t v)
{
    for (unsigned i = 0; i < size; i++) {
        const unsigned b = start + i;
        if (v >> i & 1)
            p[b >> 3] |= (uint8_t)(1u << (b & 7));
    }
}

#define W(p, word, start, size, v) md_bits((p), (word) * 32 + (start), (size), (v))
#define A(p, word, v)              md_bits((p), (word) * 32, 64, (v))

void md_primitive(uint8_t *p, const md_primitive_t *s)
{
    W(p, 0, 0, 8, (uint32_t)s->mode);
    W(p, 0, 8, 3, (uint32_t)s->index_type);
    W(p, 0, 15, 1, 1);                          /* first provoking vertex */
    W(p, 0, 16, 1, 1);                          /* low depth cull */
    W(p, 0, 17, 1, 1);                          /* high depth cull */
    W(p, 0, 26, 6, 6);                          /* job task split */
    W(p, 1, 0, 32, (uint32_t)s->base_vertex);
    W(p, 3, 0, 32, s->index_count - 1);
    A(p, 4, s->indices);
}

void md_draw(uint8_t *p, const md_draw_t *s)
{
    W(p, 0, 0, 1, 1);                           /* four components per vertex */
    W(p, 0, 1, 1, 1);                           /* 64-bit draw descriptor */
    W(p, 0, 5, 1, (uint32_t)(s->front_ccw != 0));
    W(p, 0, 6, 1, (uint32_t)(s->cull_front != 0));
    W(p, 0, 7, 1, (uint32_t)(s->cull_back != 0));
    /* instance size and instance primitive size: 1, "padded" 1 is stored as 0 */
    A(p, 4, s->position);
    A(p, 6, s->uniform_buffers);
    A(p, 12, s->push_uniforms);
    A(p, 14, s->state);
    A(p, 16, s->attribute_buffers);
    A(p, 18, s->attributes);
    A(p, 20, s->varying_buffers);
    A(p, 22, s->varyings);
    A(p, 24, s->viewport);
    A(p, 28, s->thread_storage);
}

void md_renderer_state(uint8_t *p, const md_renderer_state_t *s)
{
    A(p, 0, s->shader);                         /* Shader: address (words 0-1) */
    W(p, 3, 0, 16, s->attribute_count);
    W(p, 3, 16, 16, s->varying_count);
    /* Properties */
    W(p, 4, 0, 8, s->ubo_count);
    W(p, 4, 12, 2, s->work_regs <= 32 ? 2 : 0); /* shader register allocation: 32 or 64 per thread */
    if (s->fragment) {
        W(p, 4, 20, 1, 1);                      /* allow forward pixel to be killed */
        W(p, 4, 21, 2, (uint32_t)s->pixel_kill);
        W(p, 4, 23, 2, (uint32_t)s->zs_update);
        W(p, 4, 30, 1, (uint32_t)(s->wait6 != 0));
        W(p, 4, 31, 1, (uint32_t)(s->wait7 != 0));
    }
    /* Multisample, Misc: all samples, no depth test (function Always), the depth range as the shader gives it */
    W(p, 8, 0, 16, 0xffff);
    W(p, 8, 19, 1, 1);                          /* fixed-function depth range fixed */
    W(p, 8, 20, 1, 1);                          /* shader depth range fixed */
    W(p, 8, 24, 3, 7);                          /* depth function: always */
    W(p, 8, 28, 1, 1);                          /* fixed-function near discard */
    W(p, 8, 29, 1, 1);                          /* fixed-function far discard */
    W(p, 9, 21, 3, 7);                          /* Stencil Mask, Misc: alpha test function always */
    W(p, 10, 8, 8, 0xff);                       /* stencil compare masks */
    W(p, 11, 8, 8, 0xff);
    /* Preload: the registers the compiler asks for, as pan_make_preload maps them; the uniform count */
    if (s->fragment) {
        W(p, 12, 7, 1, 1);                      /* coverage */
        W(p, 12, 9, 1, (s->preload_regs >> 57) & 1);    /* primitive ID */
        W(p, 12, 10, 1, (s->preload_regs >> 58) & 1);   /* primitive flags */
        W(p, 12, 11, 1, (s->preload_regs >> 59) & 1);   /* fragment position */
        W(p, 12, 13, 1, (s->preload_regs >> 61) & 1);   /* sample mask / ID */
    } else {
        W(p, 12, 10, 1, (s->preload_regs >> 58) & 1);   /* position result address lo */
        W(p, 12, 11, 1, (s->preload_regs >> 59) & 1);   /* hi */
        W(p, 12, 13, 1, (s->preload_regs >> 61) & 1);   /* vertex ID */
        W(p, 12, 14, 1, (s->preload_regs >> 62) & 1);   /* instance ID */
    }
    W(p, 12, 15, 7, (s->push_count + 1) / 2);   /* uniform count */
}

void md_blend_opaque(uint8_t *p, uint32_t rt, uint32_t mem_format, uint32_t reg_format)
{
    W(p, 0, 9, 1, 1);                           /* enable */
    W(p, 0, 11, 1, 1);                          /* round to FB precision */
    /* equation: RGB and alpha = A Src, B Src, C Zero; all four colours written */
    W(p, 1, 0, 2, 2);                           /* RGB A: Src */
    W(p, 1, 4, 2, 2);                           /* RGB B: Src */
    W(p, 1, 8, 3, 1);                           /* RGB C: Zero */
    W(p, 1, 12, 2, 2);                          /* alpha A */
    W(p, 1, 16, 2, 2);                          /* alpha B */
    W(p, 1, 20, 3, 1);                          /* alpha C */
    W(p, 1, 28, 4, 0xf);                        /* colour mask */
    /* Internal: mode Opaque; fixed function: 4 components, the render target, the conversion */
    W(p, 2, 0, 2, 1);
    W(p, 2, 3, 2, 3);                           /* num comps - 1 */
    W(p, 2, 16, 4, rt);
    W(p, 3, 0, 22, mem_format);                 /* conversion: memory format */
    W(p, 3, 24, 3, reg_format);                 /* conversion: register format */
}

void md_local_storage(uint8_t *p)
{
    W(p, 1, 0, 5, 31);                          /* WLS instances: none (log2 of 0x80000000) */
}

void md_uniform_buffer(uint8_t *p, uint64_t addr, uint32_t entries)
{
    W(p, 0, 0, 12, entries - 1);
    md_bits(p, 12, 52, addr >> 4);
}

void md_invocation(uint8_t *p, uint32_t vertices)
{
    /* values {size 1, 1, 1, groups 1, vertices, 1}: only the vertex count takes bits */
    W(p, 0, 0, 32, vertices - 1);
    W(p, 1, 22, 6, 32);                         /* workgroups z shift 32 (the blob's, for non-instanced graphics) */
    W(p, 1, 28, 4, 2);                          /* thread group split: min efficient */
}

static void f32(uint8_t *p, unsigned word, float v)
{
    union { float f; uint32_t u; } c = { v };
    W(p, word, 0, 32, c.u);
}

void md_viewport(uint8_t *p, float minx, float miny, float maxx, float maxy, uint32_t sc_minx, uint32_t sc_miny,
                 uint32_t sc_maxx, uint32_t sc_maxy)
{
    f32(p, 0, minx);
    f32(p, 1, miny);
    f32(p, 2, maxx);
    f32(p, 3, maxy);
    f32(p, 4, 0.0f);
    f32(p, 5, 1.0f);
    W(p, 6, 0, 16, sc_minx);
    W(p, 6, 16, 16, sc_miny);
    W(p, 7, 0, 16, sc_maxx);
    W(p, 7, 16, 16, sc_maxy);
}

void md_tiler_heap(uint8_t *p, uint64_t base, uint32_t size)
{
    W(p, 1, 0, 32, size);
    A(p, 2, base);
    A(p, 4, base);                              /* bottom */
    A(p, 6, base + size);                       /* top */
}

void md_tiler_context(uint8_t *p, uint32_t w, uint32_t h, uint32_t hierarchy_mask, uint64_t heap)
{
    W(p, 2, 0, 13, hierarchy_mask);
    W(p, 3, 0, 16, w - 1);
    W(p, 3, 16, 16, h - 1);
    A(p, 6, heap);
}

void md_attribute(uint8_t *p, uint32_t buffer, uint32_t format, int32_t offset, int offset_enable)
{
    W(p, 0, 0, 9, buffer);
    W(p, 0, 9, 1, (uint32_t)(offset_enable != 0));
    W(p, 0, 10, 22, format);
    W(p, 1, 0, 32, (uint32_t)offset);
}

void md_attribute_buffer(uint8_t *p, uint64_t pointer, uint32_t stride, uint32_t size)
{
    W(p, 0, 0, 6, 1);                           /* type: 1D */
    md_bits(p, 6, 50, pointer >> 6);            /* pointer, shr(6) */
    W(p, 2, 0, 32, stride);
    W(p, 3, 0, 32, size);
    /* divisor 1: "padded" 1 is stored as 0 */
}

void md_tiler_job(uint8_t *j, const md_primitive_t *p, uint64_t tiler, const md_draw_t *d)
{
    md_invocation(j + MD_TJ_INVOCATION, p->index_count);
    md_primitive(j + MD_TJ_PRIMITIVE, p);
    A(j + MD_TJ_TILER, 0, tiler);
    md_draw(j + MD_TJ_DRAW, d);
}

void md_vertex_job(uint8_t *j, uint32_t vertices, const md_draw_t *d)
{
    md_invocation(j + 32, vertices);
    W(j + MD_VJ_PARAMETERS, 0, 26, 4, 5);       /* job task split */
    md_draw(j + MD_VJ_DRAW, d);
}
