/*
 * Shell SDK: listening in the Sounds tab. A small NES program with the sound driver and the music of the tab is
 * built and run in the bm NES core, so what sounds here is what the cartridge will play.
 */
#ifndef SHELL_SDK_PLAY_H
#define SHELL_SDK_PLAY_H

#include <stddef.h>

#include "shell_sdk_music.h"

enum { SDK_PLAY_NOTHING, SDK_PLAY_PATTERN, SDK_PLAY_SONG, SDK_PLAY_SFX, SDK_PLAY_NOTE };

/* Plays the pattern, song or effect number index, or (SDK_PLAY_NOTE) the note with the instrument index. speed:
 * the frames a step of a pattern. Returns 0, or -1 with the reason in err. The program is built again only when
 * the music it needs has changed. */
int sdk_play_start(const sdk_music_t *m, int what, int index, int note, int speed, char *err, size_t en);
void sdk_play_stop(void);
/* SDK_PLAY_*: what sounds now. */
int sdk_play_what(void);
/* Runs the frames that are due: call it often while something sounds. Returns 1 when the row that sounds
 * changed, or the sound ended. */
int sdk_play_tick(void);
/* The place of the song and the row of the pattern that sound now. */
void sdk_play_position(int *order, int *row);
/* 0 when the console has a sound output. */
int sdk_play_silent(void);

#endif /* SHELL_SDK_PLAY_H */
