/*
 * Shell SDK: the Sounds tab of a NES project. Four pages (Instruments, SFX, Pattern, Song) over the music block
 * of the source (shell_sdk_music.h): the block is read when the code changes and written at each change here.
 */
#ifndef SHELL_SDK_SOUNDS_H
#define SHELL_SDK_SOUNDS_H

#include "shell_sdk_proj.h"

/* The SDK starts: the first page, nothing selected. */
void sdk_sounds_reset(void);
/* Call before the tab is drawn: reads the block again when the code changed. */
void sdk_sounds_sync(const sdk_proj_t *p);
/* The tab; msg (or NULL) is shown over the help. */
void sdk_sounds_draw(int cols, int rows, const char *msg);
/* Only the mark of what sounds now: while something plays, with no flicker. */
void sdk_sounds_playhead(void);
/* A key of the tab. Returns 1 when it used it. */
int sdk_sounds_key(int key, int mods);
/* 1 while a name is being typed: Esc and the other keys of the SDK belong to the tab. */
int sdk_sounds_typing(void);

#endif /* SHELL_SDK_SOUNDS_H */
