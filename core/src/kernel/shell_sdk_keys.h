/*
 * Shell SDK: the legend of the keys, shown while F12 is held (the system's way to see the keys). No tab keeps a
 * list of keys on the screen. The left column is what has the keys now, the right one what works around it.
 */
#ifndef SHELL_SDK_KEYS_H
#define SHELL_SDK_KEYS_H

enum {
    SDK_KEYS_PROJECT, SDK_KEYS_EDITOR, SDK_KEYS_ROM, SDK_KEYS_SHEET, SDK_KEYS_CANVAS, SDK_KEYS_TILES,
    SDK_KEYS_MAP, SDK_KEYS_SOUND_INSTRUMENTS, SDK_KEYS_SOUND_SFX, SDK_KEYS_SOUND_PATTERN, SDK_KEYS_SOUND_SONG,
    SDK_KEYS_SOUND_NONE, SDK_KEYS_ANIM, SDK_KEYS_ANIM_PLACE, SDK_KEYS_SOUND_SNES_INS, SDK_KEYS_COUNT
};

/* Draws the legend of one of SDK_KEYS_* over the whole screen. */
void sdk_keys_draw(int what, int cols, int rows);

#endif /* SHELL_SDK_KEYS_H */
