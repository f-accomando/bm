/*
 * Shell SDK: the text buffer of the Code tab. Pure model (no screen, no keyboard): the tests run on the PC.
 * One buffer of plain text, one cursor, one selection, one clipboard, lines that end with '\n'.
 */
#ifndef SHELL_SDK_EDIT_H
#define SHELL_SDK_EDIT_H

#include <stddef.h>

#define SDK_EDIT_MAX 32768              /* bytes of text */

void sdk_edit_reset(void);
size_t sdk_edit_length(void);
/* Replaces all the text (a project opens): a carriage return is dropped, other control codes become spaces, the text is cut
 * at SDK_EDIT_MAX. The cursor goes to the start and the revision counts it as a change. */
void sdk_edit_set(const char *text, size_t len);
/* A number that grows with each change of the text (not with moves): a caller sees an edit by comparing it. */
unsigned sdk_edit_revision(void);
/* The cursor to the start of a line (from 1; past the last line: the last line). */
void sdk_edit_goto_line(int line);
const char *sdk_edit_text(void);

/* Typed characters: 32 to 126 are inserted, '\n' and '\r' start a line, '\t' gives spaces up to the next
 * multiple of 4. The selected text goes first. Returns 1 when the text changed. */
int sdk_edit_type(int c);
/* Backspace and Delete remove the selected text, or one character. */
int sdk_edit_backspace(void);
int sdk_edit_delete(void);

/* Cursor moves. Up and Down keep the column the cursor had (a short line does not lose it). */
void sdk_edit_left(void);
void sdk_edit_right(void);
void sdk_edit_up(void);
void sdk_edit_down(void);
void sdk_edit_home(void);
void sdk_edit_end(void);

/* Word moves, as in bm Code: letters, digits and '_' make a word; at a line end they go to the next line.
 * The start and the end of the text, and a page of lines. */
void sdk_edit_word_left(void);
void sdk_edit_word_right(void);
void sdk_edit_doc_start(void);
void sdk_edit_doc_end(void);
void sdk_edit_page_up(int lines);
void sdk_edit_page_down(int lines);

/* Selection. sdk_edit_anchor(1) starts a selection at the cursor when there is none: call it before a move
 * with Shift held. sdk_edit_anchor(0) ends the selection: call it before a move without Shift. */
void sdk_edit_anchor(int on);
void sdk_edit_select_all(void);
/* The selected bytes [*from, *to). Returns 1, or 0 when nothing is selected. */
int sdk_edit_selection(size_t *from, size_t *to);
/* The offset of the first byte of a line. */
size_t sdk_edit_line_offset(int line);

/* The clipboard (the Code tab only: bm Code has its own). Copy and cut take the selection, or the whole
 * current line with its '\n' when nothing is selected. They return the bytes taken. */
int sdk_edit_copy(int cut);
/* Inserts the clipboard in place of the selection. Returns the bytes inserted (0 when empty or full). */
int sdk_edit_paste(void);
size_t sdk_edit_clip_length(void);
/* Copies the current line under itself, and the cursor goes to the copy. Returns 1 when the text changed. */
int sdk_edit_duplicate_line(void);

/* Undo and redo. Each change of the text is a step of the history (100 steps); typed characters, or Backspaces,
 * in a row with no move between them are one step. A new change ends the redo history. A step brings back the
 * text and the cursor. They return 1 when there was a step. */
int sdk_edit_undo(void);
int sdk_edit_redo(void);

/* The cursor: line and column, both from 0. */
void sdk_edit_pos(int *line, int *col);
int sdk_edit_line_count(void);
/* The text of a line (without its '\n'): returns the length, 0 and *s NULL after the last line. */
int sdk_edit_line(int line, const char **s);

#endif /* SHELL_SDK_EDIT_H */
