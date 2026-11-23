/*
 * Shell SDK: the start file of a new project. A NES or SNES project starts from a program that builds as it is
 * and shows a blue screen: the start of the machine, a game loop, the pad, and marked places (;@sprites, ;@music,
 * ;@map) for the data of the Sprite, Sounds and Map tabs. The files are core/src/script/sdk_tpl_nes.s and
 * sdk_tpl_snes.s, built into the kernels. A bm project starts empty.
 */
#ifndef SHELL_SDK_TPL_H
#define SHELL_SDK_TPL_H

#include <stddef.h>

/* The text of the start file of a target (SDK_NES, SDK_SNES, SDK_BM). Not NUL-terminated: len is its length
 * (0 and "" for a bm project). */
const char *sdk_template(int target, size_t *len);

#endif /* SHELL_SDK_TPL_H */
