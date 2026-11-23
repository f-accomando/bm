/*
 * Shell SDK, Project tab: the pixel art of the platform icons. A letter is a palette entry, '.' is clear.
 */
#ifndef SHELL_SDK_ART_H
#define SHELL_SDK_ART_H

#define SDK_ART_SIZE  24
#define SDK_ART_COUNT 3         /* NES cartridge, SNES cartridge, SD card: the same order as SDK_NES, SDK_SNES, SDK_BM */

extern const char sdk_art[SDK_ART_COUNT][SDK_ART_SIZE][SDK_ART_SIZE + 1];

#endif /* SHELL_SDK_ART_H */
