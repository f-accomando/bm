/*
 * Shell SDK: the animation mode of the Sprite tab (a): the frames of an animation side by side, painted, mirrored
 * and copied, its preview that moves, and its tiles put in the sheet (shell_sdk_anim.h).
 */
#ifndef SHELL_SDK_ANIMUI_H
#define SHELL_SDK_ANIMUI_H

/* A new project: nothing open. */
void sdk_animui_reset(void);
/* Call before the mode is drawn, after the sheet was read: kind is the SDK_CHR_* of the code. */
void sdk_animui_sync(int kind, int sheet_readable);
void sdk_animui_draw(int cols, int rows, const char *msg);
/* A key of the mode. Returns 0 when the mode closes (a). */
int sdk_animui_key(int key, int mods);
/* 1 while the preview moves: call sdk_animui_tick between the keys; it paints the preview. */
int sdk_animui_playing(void);
void sdk_animui_tick(void);
/* The legend of what has the keys (shell_sdk_keys.h). */
int sdk_animui_keys(void);
/* 1 when the open animation has changes that are not in the sheet. */
int sdk_animui_unsaved(void);
/* 1 while a name is typed: Esc belongs to the mode. */
int sdk_animui_typing(void);

#endif /* SHELL_SDK_ANIMUI_H */
