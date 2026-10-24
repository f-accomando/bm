/*
 * Native bm cartridge format (.bm), version 1. Little endian.
 *
 *   0   char[8]  magic "BMCART" and two zero bytes ("BM33CART" in files
 *                made before the project was renamed: still read)
 *   8   u16      version (1)
 *   10  u16      header size (128)
 *   12  u16      width  (640, 480, 320, 256, 360 or 720)
 *   14  u16      height (360, 270, 180, 256, 360 or 720: 640x360, 480x270, 320x180, 256x256, 360x360, 720x720;
 *                a square one is shown in the middle of a 480x270 screen (256x256),
 *                a 640x360 one (360x360, the .b16's) or a 1280x720 one (720x720); whole on a square panel)
 *   16  u8       pixel format (1 = RGB565; 2 = XRGB8888, reserved)
 *   17  u8       section count
 *   18  u16      flags: bit 0 a project (.bme), bit 1 a .b16 edition (src/bm/project.h), bits 2-3 the
 *                type (game, app, utility), bit 4 BM_FLAG_COMPAT: the compatibility block at 120 is
 *                present; the other bits are kept as they are by every writer. The kind (project,
 *                .b16) is told by the file's extension
 *   20  u32      CRC-32 of everything after the header
 *   24  char[48] title
 *   72  char[32] author
 *   104 char[16] built from: the 8.3 name of the project a game was built from (src/bm/project.h); 0 if none
 *   120 u8[8]    compatibility block, valid only when flag bit 4 is set (else all 0 = an old file):
 *                120 targets (BM_TARGET_*), 121 input (BM_IN_*), 122 aspect (BM_ASPECT_*),
 *                123 needs (BM_NEED_*), 124 u16 min RAM in MB (0 = none), 126 min spec revision,
 *                127 min screen width in units of 16 pixels. Not covered by the CRC. See bm_compat_t.
 *   128 section table: count x { u32 type, u32 offset, u32 size, u32 reserved }
 *
 * Section types:
 *   1 LUA    main script, UTF-8 source
 *   2 SHEET  u16 w, u16 h, then w*h RGBA8888 pixels (alpha < 128 = transparent)
 *   3 MAP    u16 w, u16 h, then w*h u16 cells (sprite index, 0 = empty)
 *   4 COVER  u16 w, u16 h, then w*h RGBA8888: the picture printed on the
 *            cartridge in the menu (mkbm.py makes it 88x88); first in the
 *            file, so the menu can read it without the rest
 *   5 SHEET8 the sheet with at most 256 colours, much smaller for big
 *            sprites: u16 w, u16 h, u16 colours (1..256), u16 reserved (0),
 *            the palette (colours x RGBA8888, alpha < 128 = transparent), then
 *            the w*h palette indices, row by row, as runs: a byte t < 128 is
 *            followed by t+1 indices; t >= 128 by one index, repeated t-126
 *            times. A cartridge has SHEET or SHEET8, not both.
 *   6 AUDIO  the sound bank: sounds, sound effects, patterns and songs
 *            (format in src/audio/player.h), played by sfx() and music()
 *   8 MESH   3D models (made with bm Studio, or packed by
 *            mkbm.py --models): u16 models (1..256), u16 texture inset
 *            (1/256 of a sheet pixel: the loader moves the texture corners
 *            of each face that far inwards, so the next tile of the sheet
 *            never shows along the edges), u32 reserved (0), then per model:
 *              char[16] name (UTF-8, zero padded; unique in the section),
 *              u16 vertices (1..65535; 4096 before 2026-10-05, bm3d 5.0), u16 faces
 *              (1..65535; 16384 before), u32 flags (bit 0
 *              "lit": the light is baked in the faces, see below; else 0),
 *              vertices x { f32 x, y, z }: y up, like mesh();
 *              faces x { u16 a, b, c: 0-based vertex indices, clockwise
 *                        seen from the side that shows (as for mesh());
 *                        u16 reserved; u32 colour: 0xRRGGBB, or bit 31 set =
 *                        textured with the sprite sheet; bits 24..30 the
 *                        material (emissive, glossy, screen-door, flat,
 *                        level of detail: r3d.h; 0 = plain); u16 u0, v0, u1,
 *                        v1, u2, v2: texture corners in sheet pixels x 8; in a
 *                        "lit" model, on a face that is not textured, the
 *                        light baked at each corner instead: u = R | G << 8,
 *                        v = B (128 = the colour as it is, up to 255 brighter):
 *                        such a face is drawn with that light, smoothly, and
 *                        no light of the scene (sun, sky) but the lamps; a
 *                        textured face of a "lit" model has its light (one
 *                        for the whole face) in the u16 reserved: RGB 5-6-5,
 *                        where the top value is twice the texture as it is }.
 *   9 ANIM   skeletons and animations of MESH models (made with bm
 *            Animator): u16 rigs, u16 reserved, u32 reserved, then per
 *            rig:
 *              char[16] model name (the model it moves), u16 bones (1..64),
 *              u16 clips (0..255), u16 vertices (the model's), u16 reserved,
 *              bones x { char[16] name; i16 parent (-1, or an earlier bone);
 *                        u16 reserved; f32 head[3], tail[3] (at rest) },
 *              vertices x u8: the bone each vertex of the model follows,
 *              zero padded to a multiple of 4,
 *              clips x { char[16] name; u16 keys (1..1024); u8 mode (0
 *                        linear, 1 smooth, 2 step, 3 ease-in, 4 ease-out); u8 flags (bit 0: loop);
 *                        f32 length (seconds); keys x { f32 time (rising,
 *                        0..length); bones x { f32 q[4] (x, y, z, w: the
 *                        turn, relative to the parent), f32 t[3] (a move) } } }.
 *            A bone turns around its head: M = M_parent * T(head + t) * R(q)
 *            * T(-head) moves the vertices at rest (see runtime.c animate()).
 *   (6 and 7 were MESH and ANIM in the first files of bm Studio: a type 6
 *   section that is not a sound bank ("BMAU") is still read as MESH, a type
 *   7 one as ANIM; they are written as 8 and 9.)
 *   10 INFO  about the file and its parts, UTF-8 text of at most 16 KB:
 *            lines "key: value" (name, desc, author, license as an SPDX id,
 *            version, tags separated by commas, origin = the SHA-256 of the
 *            resource file it came from, date YYYY-MM-DD), then a block for
 *            each part that has its own, opened by a line "[type name]"
 *            (type: model, sprite, sound, sfx, song, map, palette). Unknown
 *            keys are kept.
 *   11 SPRITES named zones of the sheet: u16 zones (1..1024), u16 reserved,
 *            then per zone: char[16] name (UTF-8, zero padded; unique in the
 *            section), u16 x, y, w, h (sheet pixels, w and h >= 1), u8
 *            frames (1..16: the next ones are the w x h boxes to the right
 *            of the first, on the same row), u8 fps (0: still), u16 reserved.
 *   12 LAYERS the map's other layers (R11, 2026-10-04; the MAP section is
 *            layer 1, so a kernel of before draws that one): u16 w, u16 h
 *            (the MAP's), u16 layers (2..8, layer 1 counted), u16 reserved
 *            (0), then layers x char[16] name (UTF-8, zero padded, not empty,
 *            unique; layer 1 first), then the cells of layers 2..n, each w*h
 *            u16 as in MAP, row by row. Drawn in their order: 1 at the back.
 *   13 FLAGS 8 flags for each 8x8 cell of the sheet (fget/fset, R11): u16
 *            cells per row and u16 rows of the sheet they were written for,
 *            then per_row*rows bytes, row by row (cell n of that sheet is
 *            byte n); trailing rows of zeros may be left out. Read by the
 *            cell's place, so a sheet grown since keeps them.
 *   14 BOXES the boxes of the frames of the SPRITES zones (hitboxes,
 *            hurtboxes; 2026-10-04): u16 boxes (1..4096), u16 reserved (0),
 *            then per box: char[16] zone name (a zone of SPRITES), u8 frame
 *            (1..16; 0: every frame of the zone), u8 kind (0 hurt, 1 hit, 2
 *            body, 3..255 the game's own), i16 x, i16 y (from the frame's
 *            top-left corner, pixels: it may stick out), u16 w, u16 h (>= 1),
 *            u16 reserved (0). In a cartridge broken BOXES are left out.
 * Graphics are stored independently of the screen format and converted when
 * the cartridge is loaded, so the same file works if 32-bit output is added.
 *
 * Resource files (docs/RISORSE.md): one resource out of a cartridge, in the
 * same container with another magic and never a LUA section. The header is
 * the one above but for:
 *   0   char[8]  magic "BMRES" and three zero bytes
 *   12  u16      kind: 1 models (.bmm), 2 image (.bmi), 3 sounds (.bms),
 *                4 map (.bmt), 5 palette (.bmc), 6 kit (.bmk)
 *   14  u16      reserved (0)
 *   16  u8       reserved (0)
 *   24  char[48] the name shown (the file name is 8.3)
 * Sections of each kind (INFO in all; the others are ignored and kept):
 *   models   MESH, ANIM, SHEET or SHEET8 (only the cells the textures use)
 *   image    SHEET or SHEET8, SPRITES, BOXES
 *   sounds   AUDIO
 *   map      MAP, LAYERS, FLAGS, SHEET or SHEET8 (only the tiles the map
 *            uses)
 *   palette  SHEET8 of N x 1 pixels: its palette is the palette
 *   kit      any of MESH, ANIM, SHEET or SHEET8, SPRITES, BOXES, AUDIO,
 *            MAP, LAYERS, FLAGS
 * (an image may also hold FLAGS: the flags of its tiles)
 */
#ifndef BM_H
#define BM_H

/* The revision of the specification this kernel implements (docs/spec/specs.toml): it grows when a format, the API,
 * the system keys or the config keys gain something a cartridge can depend on. A cartridge says what it needs with
 * the INFO key "spec: N" (bm.info); an older kernel then refuses it with a message instead of failing half way.
 * Revision 1: everything of 2026-10-11 (container v1, sections 1-14, resource files, projects, the .b16 marker).
 * Revision 2 (2026-10-13): 720x720 cartridges, shot(), the INFO key "spec", tool names reserved against games.
 * Revision 3 (2026-10-17): the cartridge type in bits 2-3 of the flags (bm.info "type"), the /bme folder for projects.
 * Revision 4 (2026-10-22): the web API for cartridges (web_get, web_poll, web_info, web_body, web_cancel, image_load, file_write), the folder bm/web. */
#define BM_SPEC_REV 4

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
#define BM_SEC_INFO        10
#define BM_SEC_SPRITES     11
#define BM_SEC_LAYERS      12
#define BM_SEC_FLAGS       13
#define BM_SEC_BOXES       14
#define BM_SEC_OLD_ANIM    7               /* the first files of bm Studio (see above) */
#define BM_LAYERS_MAX      8               /* map layers, the MAP section counted */
#define BM_LAYER_NAME      16              /* bytes of a layer's name in LAYERS */
#define BM_INFO_MAX        (16 * 1024)     /* bytes of an INFO section */
#define BM_SPRITES_MAX     1024            /* zones of a SPRITES section */
#define BM_SPRITE_SIZE     28              /* bytes of a zone */
#define BM_FRAMES_MAX      16
#define BM_BOXES_MAX       4096            /* boxes of a BOXES section */
#define BM_BOX_SIZE        28              /* bytes of a box */

/* the kinds of resource file (offset 12 of a "BMRES" header) */
enum { BM_RES_CART, BM_RES_MODEL, BM_RES_IMAGE, BM_RES_SOUND, BM_RES_MAP, BM_RES_PALETTE, BM_RES_KIT,
       BM_RES_KINDS };
#define BM_SHEET_MAX       4096            /* width and height of a sheet */
/* COVER: the picture in the menu, RGBA, up to 512x512; square since
 * 2026-10-04 (mkbm.py writes 88x88, the menu's card), 128x80 before: the
 * menu fits any shape (menu_load_cover) */
#define BM_COVER_SIZE      88
#define BM_MODEL_NAME      16              /* bytes of a model name in MESH */
#define BM_MODEL_VERTS     65535           /* bm3d 5.0: 4096 and 16384 before */
#define BM_MODEL_FACES     65535
#define BM_MODELS_MAX      256
#define BM_MESH_FACE       24              /* bytes of a face in MESH */
#define BM_BONES_MAX       64
#define BM_CLIPS_MAX       255
#define BM_KEYS_MAX        1024
#define BM_BONE_SIZE       44              /* bytes of a bone in ANIM */
#define BM_POSE_SIZE       28              /* bytes of one bone of a key */

/* The compatibility block of the header (bytes 120..127, flag bit 4). docs/BM-FORMAT.md. */
#define BM_FLAG_COMPAT      16              /* u16 at 18: the block at 120 is present */
#define BM_COMPAT_AT        120             /* offset of the block in the header */
#define BM_TARGET_PI        1               /* targets: the Pi Zero W (kernel.img) */
#define BM_TARGET_RGB30     2               /* targets: the RGB30 (kernel8.img) */
#define BM_TARGET_ALL       (BM_TARGET_PI | BM_TARGET_RGB30)
#define BM_IN_PAD           1               /* input: playable with a pad alone */
#define BM_IN_KEYBOARD      2               /* input: playable with a keyboard */
#define BM_IN_TEXT          4               /* input: needs real text entry */
#define BM_IN_MOUSE         8               /* input: uses mouse() as the main control */
#define BM_IN_MOUSE_OPT     16              /* input: uses the mouse, works without it */
#define BM_ASPECT_WIDE      1               /* aspect: made for 16:9 */
#define BM_ASPECT_SQUARE    2               /* aspect: made for 1:1 */
#define BM_ASPECT_ANY       4               /* aspect: elastic layout */
#define BM_ASPECT_SAFE      8               /* aspect: uses safe_area() */
#define BM_NEED_3D          1               /* needs: 3D */
#define BM_NEED_AUDIO       2               /* needs: audio */
#define BM_NEED_NET         4               /* needs: network */
#define BM_NEED_MOUSE_HW    8               /* needs: a mouse device */
typedef struct {
    uint8_t declared;               /* 1 if flag bit 4 is set: the fields below are the file's own */
    uint8_t targets;                /* BM_TARGET_* bits */
    uint8_t input;                  /* BM_IN_* bits (0 = not declared) */
    uint8_t aspect;                 /* BM_ASPECT_* bits (0 = not declared) */
    uint8_t needs;                  /* BM_NEED_* bits */
    uint16_t min_ram_mib;           /* 0 = none */
    uint8_t min_spec;               /* 0 = none */
    uint8_t min_w16;                /* minimum screen width / 16, 0 = none */
} bm_compat_t;
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
    uint8_t kind;                   /* BM_RES_CART, or the kind of a resource file */
    const uint8_t *info;            /* INFO section (text), or NULL */
    uint32_t info_size;
    const uint8_t *sprites;         /* SPRITES section, or NULL */
    uint16_t zones;                 /* zones in it */
    const uint8_t *boxes;           /* BOXES section (frames' hit and hurt boxes), or NULL */
    uint16_t nboxes;                /* boxes in it */
    const uint8_t *layers;          /* LAYERS section, or NULL (then the map has one layer) */
    uint8_t nlayers;                /* the map's layers: 0 without a map, else 1..8 */
    const uint8_t *flags;           /* FLAGS: the bytes, row by row, or NULL */
    uint16_t flags_per_row, flags_rows;
    bm_compat_t compat;             /* the header's compatibility block (old rules for an old file) */
} bm_cart_t;

/* A zone of the SPRITES section. */
typedef struct {
    char name[BM_MODEL_NAME + 1];
    uint16_t x, y, w, h;            /* the first frame, sheet pixels */
    uint8_t frames, fps;            /* the next frames: w x h boxes to the right */
    uint8_t slices;                 /* 0 for a plain zone; 2..255 for a stack zone (w x h slices to the right) */
    uint8_t vertical;               /* 1 for a stack zone whose slices lie one under the other (stackv) */
} bm_zone_t;

/* A box of the BOXES section. */
enum { BM_BOX_HURT, BM_BOX_HIT, BM_BOX_BODY };
typedef struct {
    char zone[BM_MODEL_NAME + 1];
    uint8_t frame;                  /* 1..16, 0: every frame */
    uint8_t kind;                   /* BM_BOX_*, or the game's own (3..255) */
    int16_t x, y;                   /* from the frame's top-left corner */
    uint16_t w, h;
} bm_box_t;

/* One model of a MESH section (bm_parse has already checked it). */
#define BM_MODEL_LIT       1u              /* flags of a model: its light is baked */

typedef struct {
    char name[BM_MODEL_NAME + 1];
    uint16_t nverts, nfaces;
    uint32_t flags;                 /* BM_MODEL_LIT, or 0 */
    const uint8_t *verts;           /* nverts x 3 little-endian f32 */
    const uint8_t *faces;           /* nfaces x BM_MESH_FACE bytes */
} bm_model_t;

int bm_parse(const uint8_t *data, size_t len, bm_cart_t *c, char *err, size_t errlen);
/* Called every 64 KB of the CRC check, if set (the loading screen, the
 * menu's fibers); nonzero stops the parse (it fails: "stopped"). */
extern int (*bm_parse_tick)(void);
/* bm_parse for a cartridge or a resource file ("BMRES": no code, the
 * sections of its kind; c->kind says which). */
int bm_parse_any(const uint8_t *data, size_t len, bm_cart_t *c, char *err, size_t errlen);
/* 1 if these first 8 bytes are the magic of a resource file. */
int bm_is_res(const void *head8);
/* The COVER from the first bytes of a file (it comes first, so the menu
 * reads only those; no CRC: the whole file is checked when it is played).
 * 1 with the picture, 0 if more bytes are needed (*need says how many:
 * read that much and ask again), -1 if there is no cover or it is broken. */
int bm_cover_peek(const uint8_t *d, size_t len, uint32_t *need, const uint8_t **rgba, int *w, int *h);
/* Zone i (0-based) of a parsed file's SPRITES: 0, or -1 if there is none. */
int bm_zone(const bm_cart_t *c, int i, bm_zone_t *z);
/* Box i (0-based) of a parsed file's BOXES: 0, or -1 if there is none. */
int bm_box(const bm_cart_t *c, int i, bm_box_t *b);
/* Checks a SPRITES section: number of zones (1..1024), or -1 if broken. */
int bm_sprites_check(const uint8_t *p, uint32_t size);
/* Checks a BOXES section: number of boxes (1..4096), or -1 if broken. */
int bm_boxes_check(const uint8_t *p, uint32_t size);
/* A value of INFO: `key` in the block of the part "[type name]", else on
 * the file's own lines (type NULL: only those). 1 if found, the value in
 * out (cut to n - 1 bytes). */
int bm_info_get(const bm_cart_t *c, const char *type, const char *name, const char *key, char *out, size_t n);

/* Layer i (0-based, < c->nlayers) of a parsed file's map: its name (layer 0
 * without LAYERS: "main") and its w*h little-endian u16 cells. 0, or -1. */
int bm_layer(const bm_cart_t *c, int i, char name[BM_LAYER_NAME + 1], const uint8_t **cells);
/* The flags of the sheet cell at column col, row row (cells of 8x8 pixels)
 * of a parsed file: 0 without FLAGS or outside them. */
uint8_t bm_cell_flags(const bm_cart_t *c, int col, int row);

/* Unpacks a SHEET8 section: set(x, y, rgba) for every pixel. Returns 0, or
 * -1 if the data is broken (bm_parse has already checked it). */
int bm_sheet8_unpack(const bm_cart_t *c, void (*set)(void *ctx, int x, int y, const uint8_t rgba[4]),
                      void *ctx);

/* Packs a SHEET8 section: w x h palette indices `idx` (row by row), the
 * palette as `ncol` RGBA8888 colours (1..256). The runs are those of
 * mkbm.py's encoder (scripts/mkbm.py sheet8), so both give the same bytes. Returns a
 * malloc'd section (*outlen bytes), or NULL without memory. */
uint8_t *bm_sheet8_pack(int w, int h, const uint8_t *pal_rgba, int ncol, const uint8_t *idx, size_t *outlen);

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
/* The light baked at the corners of face f of a "lit" model: R G B for
 * each corner (128 = 1; a textured face: the same at the three). */
void bm_model_face_light(const bm_model_t *m, int f, uint8_t rgb[9]);

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

/* Reads the block from a header (at least 128 bytes). An old file (bit 4 clear) gives declared = 0 and
 * the old rules: targets = Pi for a .bm, Pi + RGB30 for a .b16 (flag bit 1); the other fields are 0.
 * A set bit 4 with targets 0 is invalid and reads as the old rules too. */
void bm_compat_read(const uint8_t *head, bm_compat_t *out);
/* Writes `in` into a header (at least 128 bytes): the bytes 120..127 and flag bit 4. A `declared` of 0 clears both. */
void bm_compat_write(uint8_t *head, const bm_compat_t *in);
/* 1 if a cartridge with these targets may run on the kernel `kernel` (BM_TARGET_PI or BM_TARGET_RGB30). */
static inline int bm_compat_runs_on(const bm_compat_t *c, unsigned kernel) { return (c->targets & kernel) != 0; }

/* The compatibility policy: what a kernel does with a cartridge (docs/spec/compat-policy.toml; the C table
 * src/bm/compat_table.h is made from it by scripts/mkcompat.py). Both kernels, the menu and the Market, ask here. */
enum { BM_EDITION_BM, BM_EDITION_B16, BM_EDITION_PROJECT, BM_EDITION_ANY };     /* flag bit 0 = project, bit 1 = b16 */
enum { BM_ACT_SHOW, BM_ACT_GREY, BM_ACT_HIDE };
#define BM_REASON_MAX 31                /* characters of a reason: "Not here: " and the reason fit the 41 of the RGB30 line */
typedef struct {
    uint8_t edition;                    /* BM_EDITION_* of the header flags (the extension is not used) */
    uint8_t declared;                   /* the header has the compatibility block */
    uint8_t targets;                    /* BM_TARGET_* bits, with the old rules for an old file */
    uint8_t uses_3d;                    /* the file has a MESH section */
    uint16_t w, h;                      /* the size in the header */
    uint32_t audio_bytes;               /* bytes of the AUDIO sections */
    uint32_t size_bytes;                /* the whole file */
} bm_compat_facts_t;
/* The facts of a file from its first bytes: the header and as much of the section table as `n` holds (a
 * table cut short counts only the sections it shows). `file_size` is the size of the whole file. n < 128: all 0.
 * `name` (may be NULL) is the file name: as everywhere in the kernel, the extension (.bm, .b16, .bme) tells the
 * edition; without a known extension the header flags do (an old .b16 may lack the flag bit). */
void bm_compat_facts(const uint8_t *head, size_t n, uint32_t file_size, const char *name, bm_compat_facts_t *f);
/* The facts of a Market entry, which has no header yet: the edition from its file name, the size, nothing declared. */
void bm_compat_facts_named(int b16, uint32_t file_size, bm_compat_facts_t *f);
/* What `kernel` (BM_TARGET_PI or BM_TARGET_RGB30) does with a file: BM_ACT_*. market = 1 asks for the Market list.
 * *reason (may be NULL) is the short text of the rule that matched (at most BM_REASON_MAX characters), *rule its
 * number from 1 in the table (0: the kernel's default row). An unknown kernel shows. */
int bm_compat_classify(const bm_compat_facts_t *f, unsigned kernel, int market, const char **reason, int *rule);
/* The number of rules of the table (a host test compares it with the toml). */
int bm_compat_rule_count(void);

/* A cartridge with new code, title, author and resolution, and every other
 * section of `old` copied as it is (sheet, map, cover, and the sections this
 * kernel does not know, in their order). old == NULL: a new cartridge with
 * only the code. `width` is the resolution: 320 (320x180), 256 (256x256),
 * 360 (360x360), 480 (480x270), 720 (720x720), anything else 640x360. `old` must have passed bm_parse. Returns a malloc'd file
 * (*outlen bytes; the caller frees it), or NULL without memory. */
uint8_t *bm_rewrite(const uint8_t *old, size_t oldlen, const char *lua, size_t lua_len,
                    const char *title, const char *author, int width, size_t *outlen);

/* A section for bm_rewrite_with: data NULL takes the sections of that type
 * away. */
typedef struct {
    uint32_t type;
    const uint8_t *data;
    uint32_t size;
} bm_put_t;

/* bm_rewrite, with the sections of `put` in place of those of their type
 * (or at the end, if the file has none); lua NULL keeps the code as it is.
 * Putting MESH or ANIM also takes away those of the first bm Studio files
 * (a type 6 that is not a sound bank, a type 7). A SHEET or SHEET8 put is
 * the sheet: it takes the place of the file's sheet, whichever of the two
 * it was. */
uint8_t *bm_rewrite_with(const uint8_t *old, size_t oldlen, const char *lua, size_t lua_len,
                         const char *title, const char *author, int width, const bm_put_t *put, int nput,
                         size_t *outlen);

/* A resource file of `kind` (BM_RES_*) from sections, in the order given: the same bytes as scripts/bmres.py.
 * name (48 bytes) and author (32) are cut on a character; NULL for none. NULL if a section has no data, a
 * kind is wrong or memory is short. The checks of the sections are the caller's (bm_parse_any on the result). */
uint8_t *bm_res_build(unsigned kind, const char *name, const char *author, const bm_put_t *sec, int n,
                      size_t *outlen);

#endif
