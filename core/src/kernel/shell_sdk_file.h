/*
 * Shell SDK: the project files. A project is a bm container (the same bytes as a .bme, docs/PROJECT-LAYOUT.md)
 * with a project flag and only a LUA section. The LUA section holds the settings of the Project tab as one
 * "--sdk" line (sdk_proj_text), and after it the source code of the Code tab. The title is the project name.
 * A NES or SNES project is a .bmr in /bmr; a bm project is a .bme in /bme.
 */
#ifndef SHELL_SDK_FILE_H
#define SHELL_SDK_FILE_H

#include <stddef.h>

#include "shell_sdk_proj.h"

#define SDK_NAME_MAX 9              /* the stem of an 8.3 name, with its end */
#define SDK_FILE_MAX 13             /* "stem.ext" of an 8.3 name, with its end */
#define SDK_BMR_DIR "/bmr"          /* the folder of the NES and SNES projects */

/* The stem of a project name: lower case letters, digits, '_' and '-', at most 8, "project" when nothing is left.
 * "My Game.bmr" gives "mygame". */
void sdk_file_stem(const char *name, char out[SDK_NAME_MAX]);

/* The extension and the folder of a target: NES and SNES give "bmr" and "/bmr", bm gives "bme" and "/bme". */
const char *sdk_file_ext(int target);
const char *sdk_file_dir(int target);

/* The file name of a project: "mygame" for SDK_NES gives "mygame.bmr". */
void sdk_file_name(const char *name, int target, char out[SDK_FILE_MAX]);

/* The path of a project: "mygame" for SDK_SNES gives "/bmr/mygame.bmr". */
void sdk_file_path(const char *name, int target, char *out, size_t n);

/* 1 when the project exists on the card. */
int sdk_file_exists(const char *name, int target);

/* Writes the project: the settings of p and the source (len bytes). Returns 0, or -1 with the reason in err. */
int sdk_file_save(const sdk_proj_t *p, const char *name, const char *source, size_t len, char *err, size_t en);

/* Reads a project by its file name ("mygame.bmr"). The source is a block from malloc (the caller frees it; NULL
 * when the file has none). Returns 0, or -1 with the reason in err. */
int sdk_file_load(const char *file, sdk_proj_t *p, char **source, size_t *len, char *err, size_t en);

/* The file names ("stem.ext") of the projects in /bmr and in /bme (at most max): first the .bmr, then the .bme. */
int sdk_file_list(char names[][SDK_FILE_MAX], int max);

#endif /* SHELL_SDK_FILE_H */
