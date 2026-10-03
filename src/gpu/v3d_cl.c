/* Control lists of the V3D: little-endian packets written byte by byte
 * (the lists have no alignment). Separate from v3d.c so that the tests on
 * the PC build them too. */
#include "v3d.h"

#include <string.h>

void v3d_cl_init(v3d_cl_t *cl, void *buf, size_t size)
{
    cl->p = cl->start = buf;
    cl->end = cl->start + size;
    cl->overflow = 0;
}

void v3d_cl_u8(v3d_cl_t *cl, uint8_t v)
{
    if (cl->p >= cl->end) {
        cl->overflow = 1;
        return;
    }
    *cl->p++ = v;
}

void v3d_cl_u16(v3d_cl_t *cl, uint16_t v)
{
    v3d_cl_u8(cl, (uint8_t)v);
    v3d_cl_u8(cl, (uint8_t)(v >> 8));
}

void v3d_cl_u32(v3d_cl_t *cl, uint32_t v)
{
    v3d_cl_u16(cl, (uint16_t)v);
    v3d_cl_u16(cl, (uint16_t)(v >> 16));
}

void v3d_cl_f32(v3d_cl_t *cl, float v)
{
    uint32_t u;
    memcpy(&u, &v, 4);
    v3d_cl_u32(cl, u);
}
