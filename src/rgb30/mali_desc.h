/*
 * M41: the Mali-G52's (Bifrost, "v7") descriptors for a draw: the vertex and
 * tiler jobs and what they point at. Every field's place is that of Mesa's
 * src/panfrost/genxml/v7.xml (MIT), the values those Mesa's pan_shader.h,
 * pan_blend.c and panvk fill in for a shader without depth, stencil or
 * multisampling. The packers write the bytes of one descriptor at p (the
 * caller zeroes it first). Pure functions, no hardware:
 * tests/rgb30/mali_desc_test.c checks them against bytes made from v7.xml
 * itself (tests/rgb30/v7pack.py).
 */
#ifndef RGB30_MALI_DESC_H
#define RGB30_MALI_DESC_H

#include <stdint.h>

/* v7.xml sizes (bytes) and the offsets of a Tiler Job */
enum {
    MD_JOB_VERTEX = 5, MD_JOB_TILER = 7,
    MD_TILER_JOB_SIZE = 256, MD_TJ_INVOCATION = 32, MD_TJ_PRIMITIVE = 40, MD_TJ_PRIM_SIZE = 64,
    MD_TJ_TILER = 72, MD_TJ_DRAW = 128,
    MD_VERTEX_JOB_SIZE = 192, MD_VJ_PARAMETERS = 40, MD_VJ_DRAW = 64,
    MD_DRAW_SIZE = 128, MD_RENDERER_STATE_SIZE = 64, MD_BLEND_SIZE = 16, MD_VIEWPORT_SIZE = 32,
    MD_TILER_HEAP_SIZE = 32, MD_TILER_CONTEXT_SIZE = 192, MD_ATTRIBUTE_SIZE = 8, MD_ATTR_BUFFER_SIZE = 16,
    MD_LOCAL_STORAGE_SIZE = 64,
};

/* Draw Mode */
enum { MD_TRIANGLES = 8, MD_TRIANGLE_STRIP = 10, MD_TRIANGLE_FAN = 12 };
/* Index Type */
enum { MD_INDEX_NONE = 0, MD_INDEX_U8 = 1, MD_INDEX_U16 = 2, MD_INDEX_U32 = 3 };
/* Pixel Kill */
enum { MD_KILL_FORCE_EARLY = 0, MD_KILL_STRONG_EARLY = 1, MD_KILL_WEAK_EARLY = 2, MD_KILL_FORCE_LATE = 3 };
/* Register File Format */
enum { MD_REG_F16 = 0, MD_REG_F32 = 1 };
/* Pixel Format = (format << 12) | component order */
#define MD_FMT_RGBA8_UNORM  (187u << 12)
#define MD_FMT_RGBA32F      (191u << 12)
#define MD_FMT_SNAP4        (94u << 12)
#define MD_ORDER_RGBA       0u
#define MD_ORDER_BGRA       4u

/* a little-endian bit field of size bits at bit `start` of the descriptor at p */
void md_bits(uint8_t *p, unsigned start, unsigned size, uint64_t v);

/* Primitive (24 bytes at a Tiler Job's offset 40) */
typedef struct {
    int mode;                   /* MD_TRIANGLES... */
    int index_type;             /* MD_INDEX_*; none: the vertices in order */
    uint32_t index_count;       /* the number of indices (vertices when none) */
    uint64_t indices;           /* address of the indices */
    int32_t base_vertex;
} md_primitive_t;
void md_primitive(uint8_t *p, const md_primitive_t *s);

/* Draw (128 bytes): the addresses of what the draw uses (0: none) */
typedef struct {
    int front_ccw, cull_front, cull_back;
    uint64_t position;          /* the positions the vertex job wrote (the tiler job reads them) */
    uint64_t uniform_buffers, push_uniforms;
    uint64_t state;             /* Renderer State */
    uint64_t attribute_buffers, attributes;
    uint64_t varying_buffers, varyings;
    uint64_t viewport;
    uint64_t thread_storage;    /* Local Storage */
} md_draw_t;
void md_draw(uint8_t *p, const md_draw_t *s);

/* Renderer State (64 bytes, 64-aligned), as pan_shader_prepare_rsd fills it */
typedef struct {
    int fragment;               /* else a vertex shader */
    uint64_t shader;            /* the shader's address */
    uint32_t attribute_count, varying_count, ubo_count;
    uint32_t work_regs;         /* <= 32: 32 per thread, else 64 */
    uint64_t preload_regs;      /* the compiler's preload bit mask (info.preload) */
    uint32_t push_count;        /* pushed words: the uniform count is half of it, rounded up */
    int pixel_kill, zs_update;  /* MD_KILL_*, fragment only */
    int wait6, wait7;           /* shader wait dependency 6 and 7 (info.bifrost.wait_6, wait_7), fragment only */
} md_renderer_state_t;
void md_renderer_state(uint8_t *p, const md_renderer_state_t *s);

/* Blend (16 bytes, 16-aligned) for a render target written as it is: mode Opaque, 4 components, the
 * colour converted from `reg_format` (MD_REG_*) to the memory format `mem_format` */
void md_blend_opaque(uint8_t *p, uint32_t rt, uint32_t mem_format, uint32_t reg_format);

/* Local Storage (thread storage, 64 bytes, 64-aligned): no scratch, no workgroup memory */
void md_local_storage(uint8_t *p);

/* Uniform Buffer (8 bytes): `entries` 16-byte entries at address (16-aligned) */
void md_uniform_buffer(uint8_t *p, uint64_t addr, uint32_t entries);

/* Invocation (8 bytes) of a graphics job over `vertices` vertices and one instance, as Mesa's
 * pan_pack_work_groups_compute(1, vertices, 1, 1, 1, 1, quirk_graphics) */
void md_invocation(uint8_t *p, uint32_t vertices);

/* Viewport (32 bytes, 32-aligned): in pixels, depth 0..1 */
void md_viewport(uint8_t *p, float minx, float miny, float maxx, float maxy, uint32_t sc_minx, uint32_t sc_miny,
                 uint32_t sc_maxx, uint32_t sc_maxy);

/* Tiler Heap (32 bytes, 64-aligned): `size` bytes (4096 multiple) from base; bottom = base, top = base + size */
void md_tiler_heap(uint8_t *p, uint64_t base, uint32_t size);

/* Tiler Context (192 bytes, 64-aligned): the frame w x h, the hierarchy levels mask, the heap descriptor's address */
void md_tiler_context(uint8_t *p, uint32_t w, uint32_t h, uint32_t hierarchy_mask, uint64_t heap);

/* Attribute (8 bytes) and Attribute Buffer (16 bytes, 32-aligned): a 1D buffer of vertices `stride` apart */
void md_attribute(uint8_t *p, uint32_t buffer, uint32_t format, int32_t offset, int offset_enable);
void md_attribute_buffer(uint8_t *p, uint64_t pointer, uint32_t stride, uint32_t size);

/* the Tiler Job's pieces at j (256 bytes, zeroed first): the header is the caller's (mali.c job_header),
 * p the Primitive, tiler the Tiler Context's address, d the Draw */
void md_tiler_job(uint8_t *j, const md_primitive_t *p, uint64_t tiler, const md_draw_t *d);

/* bytes between the draws' positions, uniform buffer descriptors and pushed words in a batch */
enum { MD_BATCH_MAX = 8, MD_BATCH_POS = 64, MD_BATCH_UBO = 8, MD_BATCH_PUSH = 32 };

/* n tiler jobs at jobs + i * stride; d is the draw 0: draw i gets position + i * MD_BATCH_POS, uniform_buffers +
 * i * MD_BATCH_UBO, push_uniforms + i * MD_BATCH_PUSH; the headers are the caller's; 0, or -1 (nothing written) if
 * n is 0 or over MD_BATCH_MAX */
int md_tiler_batch(uint8_t *jobs, uint32_t stride, uint32_t n, const md_primitive_t *p, uint64_t tiler,
                   const md_draw_t *d);

/* the Vertex job (a Compute Job layout, 192 bytes, zeroed first): the header is the caller's */
void md_vertex_job(uint8_t *j, uint32_t vertices, const md_draw_t *d);

#endif
