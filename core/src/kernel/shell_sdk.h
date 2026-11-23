/*
 * Shell SDK: the IDE of the bm shell. It makes native cartridges and ROMs for NES, SNES and bm
 * from assembly, C, .b and Lua. The screen has five tabs, selected with F1 to F5:
 * Project, Code, Sprite, Map, Sounds. This is the skeleton: the tabs have no content yet.
 */
#ifndef SHELL_SDK_H
#define SHELL_SDK_H

enum {
    SDK_TAB_PROJECT,
    SDK_TAB_CODE,
    SDK_TAB_SPRITE,
    SDK_TAB_MAP,
    SDK_TAB_SOUNDS,
    SDK_TAB_COUNT
};

/* The name of a tab, or "" when the index is out of range. */
const char *shell_sdk_tab_name(int tab);

/* The tab that a key selects (F1 to F5 give 0 to 4), or -1 for any other key. */
int shell_sdk_tab_for_key(int key);

/* Opens the Shell SDK. Returns 0 when the user leaves it (Esc or Ctrl+Q). */
int shell_sdk_open(void);

#endif /* SHELL_SDK_H */
