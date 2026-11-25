/*
 * Shell SDK: a cursor and a selection in a text that is only read (the hex dump and the ASM listing of a ROM in
 * the Code tab). The lines come from a function, so a dump of megabytes is never in memory. Pure model (no
 * screen, no keyboard): the tests run on the PC.
 */
#ifndef SHELL_SDK_VIEW_H
#define SHELL_SDK_VIEW_H

#include <stddef.h>

#define SDK_VIEW_LINE 256               /* the most bytes of a line, with its end */

/* Writes line (from 0) into out, a string of at most n bytes with its end; returns its length. */
typedef int (*sdk_view_line_t)(void *ctx, int line, char *out, size_t n);

typedef struct {
    int lines;                          /* the lines of the text: the caller sets it */
    int line, col;                      /* the cursor, both from 0 */
    int want;                           /* the column Up and Down keep */
    int top, left;                      /* the first line and column shown */
    int sel;                            /* a selection runs from the anchor to the cursor */
    int aline, acol;
} sdk_view_t;

/* The cursor at the start, no selection. */
void sdk_view_reset(sdk_view_t *v, int lines);

enum { SDK_VIEW_LEFT, SDK_VIEW_RIGHT, SDK_VIEW_UP, SDK_VIEW_DOWN, SDK_VIEW_WORD_LEFT, SDK_VIEW_WORD_RIGHT,
       SDK_VIEW_HOME, SDK_VIEW_END, SDK_VIEW_PGUP, SDK_VIEW_PGDN, SDK_VIEW_DOC_START, SDK_VIEW_DOC_END };

/* A move, as in the editor. select (Shift is held) keeps the selection and grows it; else it ends. A page is
 * page lines, and the view moves with the cursor. */
void sdk_view_move(sdk_view_t *v, int move, int select, int page, sdk_view_line_t fn, void *ctx);
void sdk_view_select_all(sdk_view_t *v, sdk_view_line_t fn, void *ctx);

/* The selection in order: from (*l1, *c1) up to, not including, (*l2, *c2). Returns 0 when nothing is
 * selected. */
int sdk_view_selection(const sdk_view_t *v, int *l1, int *c1, int *l2, int *c2);

/* Copies the selection, or the line of the cursor with its '\n' when nothing is selected, into out (at most max
 * bytes, no end byte). Returns the bytes; *cut is 1 when there was more. */
size_t sdk_view_copy(const sdk_view_t *v, sdk_view_line_t fn, void *ctx, char *out, size_t max, int *cut);

/* Moves the view (top, left) so that a window of h lines and w columns shows the cursor. */
void sdk_view_scroll(sdk_view_t *v, int h, int w);

#endif /* SHELL_SDK_VIEW_H */
