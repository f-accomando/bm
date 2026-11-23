/*
 * Shell SDK: the project files. A project is a .bme in /bme (the same container as the projects of the
 * SNES SDK and the other tools, docs/PROJECT-LAYOUT.md): a cartridge with only a LUA section that holds
 * the settings of the Project tab as text (sdk_proj_text). The title is the project name.
 */
#ifndef SHELL_SDK_FILE_H
#define SHELL_SDK_FILE_H

#include <stddef.h>

#include "shell_sdk_proj.h"

#define SDK_NAME_MAX 9              /* the stem of an 8.3 name, with its end */

/* The stem of a project name: lower case letters, digits, '_' and '-', at most 8, "project" when nothing is left.
 * "My Game.bme" gives "mygame". */
void sdk_file_stem(const char *name, char out[SDK_NAME_MAX]);

/* The path of a project: "mygame" gives "/bme/mygame.bme". */
void sdk_file_path(const char *name, char *out, size_t n);

/* 1 when the project exists in /bme. */
int sdk_file_exists(const char *name);

/* Writes the project. Returns 0, or -1 with the reason in err. */
int sdk_file_save(const sdk_proj_t *p, const char *name, char *err, size_t en);

/* Reads the project of a name. Returns 0, or -1 with the reason in err. */
int sdk_file_load(const char *name, sdk_proj_t *p, char *err, size_t en);

/* The stems of the .bme files of /bme, sorted as the card gives them (at most max). */
int sdk_file_list(char names[][SDK_NAME_MAX], int max);

#endif /* SHELL_SDK_FILE_H */
