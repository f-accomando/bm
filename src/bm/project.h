/*
 * Games and projects (2026-10-06, the user's choice). A .bm (and a .b16) is
 * a game: bm's tools read it, take its code and its assets, never change
 * it. What they change is a project, a .bme: the same container, with bit
 * 0 of the u16 at offset 18 set. A copy of a game makes a project ("Make an
 * editable copy", or the question of a tool that saves a game); a project
 * makes its game ("Build .bm"), which keeps the project's file name at
 * offset 104, so the next build replaces it, and only it.
 */
#ifndef BM_PROJECT_H
#define BM_PROJECT_H

#include <stddef.h>
#include <string.h>

#define BM_FLAG_PROJECT    1               /* u16 at 18: a project (.bme) */
#define BM_FLAG_B16        2               /* u16 at 18: the .b16 profile */
#define BM_FLAG_TYPE_SHIFT 2               /* u16 at 18, bits 2-3: the type of bm.info (game, app, utility) */
enum { BM_TYPE_GAME = 0, BM_TYPE_APP = 1, BM_TYPE_UTILITY = 2 };
/* The type in a header (at least 20 bytes); the reserved value 3 reads as a game. */
static inline int bm_type_of_head(const unsigned char *head)
{
    const int t = ((head[18] | head[19] << 8) >> BM_FLAG_TYPE_SHIFT) & 3;
    return t == 3 ? BM_TYPE_GAME : t;
}
static inline const char *bm_type_name(int type)
{
    return type == BM_TYPE_APP ? "app" : type == BM_TYPE_UTILITY ? "utility" : "game";
}
#define BM_BUILT_FROM      104             /* char[16]: the project a game was built from */
/* Sets the "built from" field of a header (at least 128 bytes) to `from`, or clears it (from NULL).
 * Only bytes 104..119 change: the compatibility block at 120..127 (bm.h) stays as it is. */
static inline void bm_set_built_from(unsigned char *head, const char *from)
{
    size_t n = 0;
    while (from && from[n] && n < 16)
        n++;
    memset(head + BM_BUILT_FROM, 0, 16);
    if (n)
        memcpy(head + BM_BUILT_FROM, from, n);
}

/* by the file's name: a project (.bme), a game (.bm, .b16) */
int bm_is_project(const char *path);
int bm_is_game(const char *path);

/* Projects (.bme) live in /bme. A project found next to its game (an old
 * SD card) is still read, and saved where it is. */
#define BM_PROJECTS_DIR "/bme"

/* The folder that holds the games: "/bm" on both consoles (the folder their menus read; /carts is not read). */
const char *bm_games_dir(void);

/* The project path of a name or a game path, 8.3: "/bm/Il mio gioco.bm"
 * -> "/bme/ilmiogio.bme". The one place that builds a project path in C. 0. */
int bm_project_path(const char *name, char *out, size_t n);

/* The name the editable copy of a game takes: in /bme, the game's name
 * with .bme, or with a digit at the end if that is taken ("/carts/
 * pong.bm" -> "/bme/pong.bme", "/bme/pong1.bme"...). 0, or -1
 * if there is no free one. */
int bm_copy_name(const char *game, char *out, size_t n);

/* Copies a game (or a project) into the project `to` (bm_copy_name): the
 * same bytes, marked as a project. 0, or -1 with the reason in err. */
int bm_make_copy(const char *game, const char *to, char *err, size_t en);

/* Builds the game of a project: the same bytes, marked as a game built
 * from it, in .BM: in the games folder (bm_games_dir) for a project in /bme,
 * next to it for an old project found next to its game. The game it built before is replaced; a game
 * of the same name that it did not build stays, and the new one takes a
 * digit ("PONG1.BM"). The game's path in out; 0, or -1 with the reason. */
int bm_build(const char *project, char *out, size_t n, char *err, size_t en);


#endif
