/*
 * Shell SDK: what the panes of the Code and Sprite tabs share: rows of text at a place of the console, the
 * title bar, and the answer to the last key.
 */
#ifndef SHELL_SDK_PANE_H
#define SHELL_SDK_PANE_H

/* The answer to a key ("Copied 12 bytes"), shown by the pane that has the keys until the next key. */
extern char sdk_note[64];

/* The console cursor to a cell (ANSI row and column, from 1). */
void sdk_at(int row, int col);
/* One row of a pane: the first n bytes of s, cut to width cells and padded with spaces. */
void sdk_put_row(int row, int col, int width, const char *s, int n);
/* The head of a pane, in the swapped colours when the pane has the keys. */
void sdk_title_bar(int row, int col, int width, const char *text, int active);
/* The last row of a pane: the answer to the last key when the pane is active and there is one, else the hint. */
void sdk_pane_status(int col, int width, int rows, const char *hint, int active);

#endif /* SHELL_SDK_PANE_H */
