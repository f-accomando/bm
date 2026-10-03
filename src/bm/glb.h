/*
 * A glTF binary (.glb) becomes a model of the MESH section (the files of
 * the image-to-3D services, or any .glb): the meshes of the scene with
 * their node transforms, the positions welded, the faces wound as the
 * console shows them (clockwise from the side that shows: glTF's z
 * changes sign), the base colour texture decoded (PNG or JPEG) and
 * shrunk to the sprite sheet, the model framed (feet at y = 0, a given
 * height) and reduced (decimate.c). Plain C, kernel and PC; the same
 * conversion as tools/meshy2mesh.py.
 */
#ifndef GLB_H
#define GLB_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    float height;               /* the model's height in blocks (0: as it is) */
    int max_faces;              /* reduce to at most this many triangles (0: no) */
    int sheet;                  /* the texture's side on the sheet (256) */
} glb_opts_t;

typedef struct {
    uint8_t *record;            /* the model record (name16, counts, vertices, faces): textured if there is a texture */
    size_t record_len;
    uint8_t *flat;              /* the same faces with colours sampled from the texture; NULL without a texture */
    size_t flat_len;
    uint8_t *texture;           /* sheet x sheet RGBA, or NULL */
    int nv, nf;                 /* of the record */
    int textured;
} glb_model_t;

/* 0, or -1 with err. glb_model_free(out) after. */
int glb_to_model(const uint8_t *glb, size_t len, const char *name, const glb_opts_t *opts, glb_model_t *out,
                 char *err, size_t errlen);
void glb_model_free(glb_model_t *m);

#endif
