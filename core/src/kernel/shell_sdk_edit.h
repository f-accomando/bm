/*
 * Shell SDK: the text buffer of the Code tab. Pure model (no screen, no keyboard): the tests run on the PC.
 * One buffer of plain text, one cursor, lines that end with '\n'.
 */
#ifndef SHELL_SDK_EDIT_H
#define SHELL_SDK_EDIT_H

#include <stddef.h>

#define SDK_EDIT_MAX 32768              /* bytes of text */

void sdk_edit_reset(void);
size_t sdk_edit_length(void);
const char *sdk_edit_text(void);

/* Typed characters: 32 to 126 are inserted, '\n' and '\r' start a line, '\t' gives spaces up to the next
 * multiple of 4. Returns 1 when the text changed. */
int sdk_edit_type(int c);
int sdk_edit_backspace(void);
int sdk_edit_delete(void);

/* Cursor moves. Up and Down keep the column the cursor had (a short line does not lose it). */
void sdk_edit_left(void);
void sdk_edit_right(void);
void sdk_edit_up(void);
void sdk_edit_down(void);
void sdk_edit_home(void);
void sdk_edit_end(void);

/* The cursor: line and column, both from 0. */
void sdk_edit_pos(int *line, int *col);
int sdk_edit_line_count(void);
/* The text of a line (without its '\n'): returns the length, 0 and *s NULL after the last line. */
int sdk_edit_line(int line, const char **s);

#endif /* SHELL_SDK_EDIT_H */
