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

#define BM_FLAG_PROJECT    1               /* u16 at 18: a project (.bme) */
#define BM_BUILT_FROM      104             /* char[16]: the project a game was built from */

/* by the file's name: a project (.bme), a game (.bm, .b16) */
int bm_is_project(const char *path);
int bm_is_game(const char *path);

/* The name the editable copy of a game takes: next to it, the game's name
 * with .BME, or with a digit at the end if that is taken ("/carts/
 * VILLAGE.BM" -> "/carts/VILLAGE.BME", "/carts/VILLAGE1.BME"...). 0, or -1
 * if there is no free one. */
int bm_copy_name(const char *game, char *out, size_t n);

/* Copies a game (or a project) into the project `to` (bm_copy_name): the
 * same bytes, marked as a project. 0, or -1 with the reason in err. */
int bm_make_copy(const char *game, const char *to, char *err, size_t en);

/* Builds the game of a project: the same bytes, marked as a game built
 * from it, in .BM next to it. The game it built before is replaced; a game
 * of the same name that it did not build stays, and the new one takes a
 * digit ("PONG1.BM"). The game's path in out; 0, or -1 with the reason. */
int bm_build(const char *project, char *out, size_t n, char *err, size_t en);

#endif
