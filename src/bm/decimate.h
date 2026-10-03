/*
 * Fewer triangles for a 3D model (bm Studio's "reduce", tools/bmreduce.py,
 * meshy2mesh): quadric edge collapse (Garland and Heckbert) on a model of
 * the MESH section. Plain C, the same on the console and the PC.
 *
 * The vertices of the model are welded by position (and bone) first, so the
 * mesh is connected whatever wrote it; then the cheapest edge is collapsed
 * onto one of its ends, again and again, until the model has `target`
 * triangles or nothing more can go. A collapse never turns a face over.
 * Open borders, the lines where the colour changes and the seams of the
 * texture are held in place (a plane through the edge, across the face,
 * joins the quadric of its ends). The faces keep their colour; a textured
 * face keeps the texture corners it had, and the corner that moves takes
 * the corners of the faces that went (the texture stays continuous), or the
 * texture of the face stretched to the new position. A vertex keeps its
 * bone; an edge between two bones goes last.
 */
#ifndef DECIMATE_H
#define DECIMATE_H

#include <stddef.h>
#include <stdint.h>

/* A model as arrays (the MESH record, unpacked). */
typedef struct {
    int nv, nf;
    float *v;           /* nv x 3 */
    uint8_t *bone;      /* nv, or NULL (no skeleton) */
    uint16_t *f;        /* nf x 3 vertex indices */
    uint32_t *colour;   /* nf: 0xRRGGBB, or bit 31 set = textured */
    uint16_t *uv;       /* nf x 6 texture corners, sheet pixels x 8 */
} dec_mesh_t;

/* Reduces m in place to at most `target` triangles (fewer if the mesh
 * allows it, more if it cannot go further without turning faces over);
 * max_err > 0 stops before a collapse that costs more than that (the
 * quadric error, area x squared distance). The arrays are rewritten
 * compacted; nv and nf become the new counts. Returns the number of
 * triangles, or -1 without memory. */
int dec_reduce(dec_mesh_t *m, int target, float max_err);

/* The same on one model record of the MESH section (name16, u16 vertices,
 * u16 faces, u32 reserved, the vertices, the faces: as bm Studio's
 * encode_mesh writes it): `rec` of `len` bytes, `vb` the bone of each
 * vertex (nv bytes, or NULL). The reduced record goes to `out` (at least
 * len bytes: it is never longer) with its length in *outlen, the bones of
 * its vertices to vb_out (nv bytes, if vb was given). Returns the number of
 * triangles, -1 if the record is broken, -2 without memory. */
int bm_model_reduce(const uint8_t *rec, size_t len, const uint8_t *vb, int target, float max_err,
                    uint8_t *out, size_t *outlen, uint8_t *vb_out);

#endif
