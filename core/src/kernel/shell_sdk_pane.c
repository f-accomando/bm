/*
 * Shell SDK: the shared parts of the panes (see shell_sdk_pane.h). Original code.
 */
#include "shell_sdk_pane.h"

#include <stdio.h>
#include <string.h>

#include "gfx/console.h"

char sdk_note[48];

void sdk_at(int row, int col)
{
    char b[40];
    snprintf(b, sizeof b, "\x1b[%d;%dH", row, col);
    console_write(b);
}

void sdk_put_row(int row, int col, int width, const char *s, int n)
{
    char line[256];
    if (width > (int)sizeof line - 1)
        width = (int)sizeof line - 1;
    if (width < 1)
        return;
    snprintf(line, sizeof line, "%-*.*s", width, n < width ? n : width, s);
    sdk_at(row, col);
    console_write(line);
}

void sdk_title_bar(int row, int col, int width, const char *text, int active)
{
    char line[256];
    if (width > (int)sizeof line - 1)
        width = (int)sizeof line - 1;
    snprintf(line, sizeof line, " %-*.*s", width - 1, width - 1, text);
    sdk_at(row, col);
    console_write(active ? "\x1b[7m" : "\x1b[0m\x1b[96m");
    console_write(line);
    console_write("\x1b[0m");
}

void sdk_pane_status(int col, int width, int rows, const char *hint, int active)
{
    const int answer = active && sdk_note[0];
    const char *s = answer ? sdk_note : hint;
    console_write(answer ? "\x1b[93m" : "\x1b[96m");
    sdk_put_row(rows, col, width, s, (int)strlen(s));
    console_write("\x1b[0m");
}
