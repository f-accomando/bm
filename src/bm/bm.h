/*
 * Native bm cartridge format (.bm), version 1. Little endian.
 *
 *   0   char[8]  magic "BMCART" and two zero bytes ("BM33CART" in files
 *                made before the project was renamed: still read)
 *   8   u16      version (1)
 *   10  u16      header size (128)
 *   12  u16      width  (640 or 320)
 *   14  u16      height (360 or 180)
 *   16  u8       pixel format (1 = RGB565; 2 = XRGB8888, reserved)
 *   17  u8       section count
 *   18  u16      reserved (0)
 *   20  u32      CRC-32 of everything after the header
 *   24  char[48] title
 *   72  char[32] author
 *   104 ...      reserved (0) up to 128
 *   128 section table: count x { u32 type, u32 offset, u32 size, u32 reserved }
 *
 * Section types:
 *   1 LUA    main script, UTF-8 source
 *   2 SHEET  u16 w, u16 h, then w*h RGBA8888 pixels (alpha < 128 = transparent)
 *   3 MAP    u16 w, u16 h, then w*h u16 cells (sprite index, 0 = empty)
 *   4 COVER  u16 w, u16 h, then w*h RGBA8888: the picture printed on the
 *            cartridge in the menu (mkbm.py makes it 128x80); first in the
 *            file, so the menu can read it without the rest
 *   5 SHEET8 the sheet with at most 256 colours, much smaller for big
 *            sprites: u16 w, u16 h, u16 colours (1..256), u16 reserved (0),
 *            the palette (colours x RGBA8888, alpha < 128 = transparent), then
 *            the w*h palette indices, row by row, as runs: a byte t < 128 is
 *            followed by t+1 indices; t >= 128 by one index, repeated t-126
 *            times. A cartridge has SHEET or SHEET8, not both.
 *   6 AUDIO  the sound bank: sounds, sound effects, patterns and songs
 *            (format in src/audio/player.h), played by sfx() and music()
 *   8 MESH   3D models (made with bm Studio, sdk/studio, or packed by
 *            mkbm.py --models): u16 models (1..256), u16 texture inset
 *            (1/256 of a sheet pixel: the loader moves the texture corners
 *            of each face that far inwards, so the next tile of the sheet
 *            never shows along the edges), u32 reserved (0), then per model:
 *              char[16] name (UTF-8, zero padded; unique in the section),
 *              u16 vertices (1..4096), u16 faces (1..16384), u32 reserved,
 *              vertices x { f32 x, y, z }: y up, like mesh();
 *              faces x { u16 a, b, c: 0-based vertex indices, clockwise
 *                        seen from the side that shows (as for mesh());
 *                        u16 reserved; u32 colour: 0xRRGGBB, or bit 31 set =
 *                        textured with the sprite sheet; u16 u0, v0, u1, v1,
 *                        u2, v2: texture corners in sheet pixels x 8 }.
 *   9 ANIM   skeletons and animations of MESH models (made with bm Animator,
 *            sdk/animator): u16 rigs, u16 reserved, u32 reserved, then per
 *            rig:
 *              char[16] model name (the model it moves), u16 bones (1..64),
 *              u16 clips (0..255), u16 vertices (the model's), u16 reserved,
 *              bones x { char[16] name; i16 parent (-1, or an earlier bone);
 *                        u16 reserved; f32 head[3], tail[3] (at rest) },
 *              vertices x u8: the bone each vertex of the model follows,
 *              zero padded to a multiple of 4,
 *              clips x { char[16] name; u16 keys (1..1024); u8 mode (0
 *                        linear, 1 smooth, 2 step); u8 flags (bit 0: loop);
 *                        f32 length (seconds); keys x { f32 time (rising,
 *                        0..length); bones x { f32 q[4] (x, y, z, w: the
 *                        turn, relative to the parent), f32 t[3] (a move) } } }.
 *            A bone turns around its head: M = M_parent * T(head + t) * R(q)
 *            * T(-head) moves the vertices at rest (see runtime.c animate()).
 *   (6 and 7 were MESH and ANIM in the first files of bm Studio: a type 6
 *   section that is not a sound bank ("BMAU") is still read as MESH, a type
 *   7 one as ANIM; they are written as 8 and 9.)
 * Graphics are stored independently of the screen format and converted when
 * the cartridge is loaded, so the same file works if 32-bit output is added.
 */
#ifndef BM_H
#define BM_H

#include <stddef.h>
#include <stdint.h>

#define BM_HEADER_SIZE     128
#define BM_FMT_RGB565      1
#define BM_FMT_XRGB8888    2

#define BM_SEC_LUA         1
#define BM_SEC_SHEET       2
#define BM_SEC_MAP         3
#define BM_SEC_COVER       4
#define BM_SEC_SHEET8      5
#define BM_SEC_AUDIO       6
#define BM_SEC_MESH        8
#define BM_SEC_ANIM        9
#define BM_SEC_OLD_ANIM    7               /* the first files of bm Studio (see above) */
#define BM_SHEET_MAX       4096            /* width and height of a sheet */
#define BM_COVER_W         128
#define BM_COVER_H         80
#define BM_MODEL_NAME      16              /* bytes of a model name in MESH */
#define BM_MODEL_VERTS     4096
#define BM_MODEL_FACES     16384
#define BM_MODELS_MAX      256
#define BM_MESH_FACE       24              /* bytes of a face in MESH */
#define BM_BONES_MAX       64
#define BM_CLIPS_MAX       255
#define BM_KEYS_MAX        1024
#define BM_BONE_SIZE       44              /* bytes of a bone in ANIM */
#define BM_POSE_SIZE       28              /* bytes of one bone of a key */

typedef struct {
    char title[49];
    char author[33];
    uint16_t width, height;
    uint8_t pixel_format;
    const char *lua;
    uint32_t lua_size;
    const uint8_t *sheet_rgba;
    const uint8_t *sheet8;          /* SHEET8 section (from its header), or NULL */
    uint32_t sheet8_size;
    uint16_t sheet_w, sheet_h;
    const uint8_t *map_cells;       /* little-endian u16 cells */
    uint16_t map_w, map_h;
    const uint8_t *cover_rgba;      /* NULL if the cartridge has no cover */
    uint16_t cover_w, cover_h;
    const uint8_t *audio;           /* AUDIO section, or NULL */
    uint32_t audio_size;
    const uint8_t *mesh;            /* MESH section, or NULL */
    uint32_t mesh_size;
    uint16_t models;                /* models in it */
    const uint8_t *anim;            /* ANIM section, or NULL */
    uint32_t anim_size;
} bm_cart_t;

/* One model of a MESH section (bm_parse has already checked it). */
typedef struct {
    char name[BM_MODEL_NAME + 1];
    uint16_t nverts, nfaces;
    const uint8_t *verts;           /* nverts x 3 little-endian f32 */
    const uint8_t *faces;           /* nfaces x BM_MESH_FACE bytes */
} bm_model_t;

int bm_parse(const uint8_t *data, size_t len, bm_cart_t *c, char *err, size_t errlen);

/* Unpacks a SHEET8 section: set(x, y, rgba) for every pixel. Returns 0, or
 * -1 if the data is broken (bm_parse has already checked it). */
int bm_sheet8_unpack(const bm_cart_t *c, void (*set)(void *ctx, int x, int y, const uint8_t rgba[4]),
                      void *ctx);

/* Checks a MESH section: the number of models, or -1 if it is broken. */
int bm_mesh_check(const uint8_t *mesh, uint32_t size);
/* Model i (0-based) of a checked MESH section: 0, or -1 if there is none. */
int bm_mesh_model(const uint8_t *mesh, uint32_t size, int i, bm_model_t *m);
/* The texture inset of a MESH section, in sheet pixels. */
float bm_mesh_inset(const uint8_t *mesh);
/* Vertex i of a model, and face f: vertex indices, colour, texture corners
 * in sheet pixels. */
void bm_model_vertex(const bm_model_t *m, int i, float xyz[3]);
void bm_model_face(const bm_model_t *m, int f, uint16_t idx[3], uint32_t *colour, float uv[6]);

/* One skeleton of an ANIM section, and one of its animations. */
typedef struct {
    char model[BM_MODEL_NAME + 1];
    uint16_t nbones, nclips, nverts;
    const uint8_t *bones;           /* nbones x BM_BONE_SIZE */
    const uint8_t *vbones;          /* nverts bytes */
    const uint8_t *clips;           /* the clips, one after the other */
    uint32_t size;                  /* bytes of the whole rig */
} bm_rig_t;

typedef struct {
    char name[BM_MODEL_NAME + 1];
    uint16_t nkeys;
    uint8_t mode, loop;
    float length;
    const uint8_t *keys;            /* nkeys x (4 + nbones x BM_POSE_SIZE) */
} bm_clip_t;

/* Checks an ANIM section: the number of rigs, or -1 if it is broken. */
int bm_anim_check(const uint8_t *anim, uint32_t size);
/* The rig of the model called `model` in a checked ANIM section: 0, or -1. */
int bm_anim_rig(const uint8_t *anim, uint32_t size, const char *model, bm_rig_t *r);
/* Reads a rig from its own bytes (the r.size bytes at r.bones - 24). */
int bm_rig_read(const uint8_t *p, uint32_t size, bm_rig_t *r);
void bm_rig_bone(const bm_rig_t *r, int i, char name[BM_MODEL_NAME + 1], int *parent, float head[3],
                 float tail[3]);
/* Clip i of a rig: 0, or -1 if there is none. */
int bm_rig_clip(const bm_rig_t *r, int i, bm_clip_t *c);
/* Key k of a clip: its time, and the turn q and move t of each bone. */
float bm_clip_key(const bm_clip_t *c, int nbones, int k, float (*q)[4], float (*t)[3]);

/* 1 if these first 8 bytes are the magic of a .bm cartridge. */
int bm_is_cart(const void *head8);

/* A cartridge with new code, title, author and resolution, and every other
 * section of `old` copied as it is (sheet, map, cover, and the sections this
 * kernel does not know, in their order). old == NULL: a new cartridge with
 * only the code. `old` must have passed bm_parse. Returns a malloc'd file
 * (*outlen bytes; the caller frees it), or NULL without memory. */
uint8_t *bm_rewrite(const uint8_t *old, size_t oldlen, const char *lua, size_t lua_len,
                    const char *title, const char *author, int width, size_t *outlen);

#endif
