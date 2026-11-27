/*
 * Shell SDK: the legend of the keys (see shell_sdk_keys.h). Original code. A key that changes in a tab changes
 * here too.
 */
#include "shell_sdk_keys.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "gfx/console.h"
#include "shell_sdk_pane.h"

/* A line of a list: a key and what it does; with no `what`, a heading. A list ends with two NULLs. */
typedef struct { const char *keys, *what; } line_t;

static const line_t sdk[] = {
    { "Shell SDK", NULL },
    { "F1 - F5", "the tabs" },
    { "Esc", "the menu" },
    { "Ctrl+N O S", "new, open, save" },
    { "Ctrl+B R", "build, build and run" },
    { "F6 F7", "check, assembly (C)" },
    { "Ctrl+Q", "leave" },
    { NULL, NULL } };

static const line_t project[] = {
    { "Project", NULL },
    { "Up Down", "choose a row" },
    { "Left Right", "change the value" },
    { "Enter", "the next value" },
    { NULL, NULL } };

static const line_t editor[] = {
    { "Code: the editor", NULL },
    { "Arrows", "move (Shift selects)" },
    { "Ctrl+Left Right", "a word" },
    { "Home End", "start, end of line" },
    { "Ctrl+Alt+arrows", "line, text: ends" },
    { "Alt+Up Down", "a page" },
    { "Ctrl+C X V", "copy, cut, paste" },
    { "Ctrl+D", "double the line" },
    { "Ctrl+A", "select all" },
    { "Ctrl+Z", "undo" },
    { "Ctrl+Y", "redo (Ctrl+Shift+Z)" },
    { NULL, NULL } };

static const line_t with_rom[] = {
    { "With a ROM", NULL },
    { "Ctrl+T", "the other side" },
    { "Ctrl+E", "hide or show it" },
    { "Ctrl+W", "close it" },
    { NULL, NULL } };

static const line_t rom[] = {
    { "Code: the ROM side", NULL },
    { "Arrows", "move (Shift selects)" },
    { "Alt+Up Down", "a page" },
    { "Ctrl+A", "select all" },
    { "Ctrl+C", "copy" },
    { "a", "hex or ASM" },
    { "t", "hex or tiles" },
    { "+ -", "bank (ASM), tiles: row" },
    { "b", "tiles: bits a pixel" },
    { "o", "tiles: 8x8 or 8x16" },
    { ", .", "tiles: a byte" },
    { "Ctrl+Left Right", "tiles: a tile" },
    { NULL, NULL } };

static const line_t sheet[] = {
    { "Sprite: the sheet", NULL },
    { "Arrows", "move (Shift selects)" },
    { "Enter", "paint the tile" },
    { "", "(2x2 selected: 16x16)" },
    { "Ctrl+C X V", "copy, cut, paste" },
    { "Del", "empty the cells" },
    { "Ctrl+A", "select all" },
    { "i", "import the ROM's sheet" },
    { NULL, NULL } };

static const line_t canvas[] = {
    { "Sprite: the canvas", NULL },
    { "Arrows", "move (Shift selects)" },
    { "Space", "paint (hold and move)" },
    { "Backspace", "transparent (too)" },
    { "[ ]", "colour" },
    { "Ctrl+C X V", "copy, cut, paste" },
    { "Ctrl+A", "select all" },
    { "Enter", "back to the sheet" },
    { NULL, NULL } };

static const line_t tiles[] = {
    { "Sprite: the ROM", NULL },
    { "Arrows", "move (Shift selects)" },
    { "Ctrl+C", "copy the tiles" },
    { "Ctrl+A", "all that shows" },
    { "Alt+Up Down", "scroll a page" },
    { "Home", "back" },
    { ", .", "a byte" },
    { "Ctrl+Left Right", "a tile" },
    { "+ -", "tiles in a row" },
    { "o", "8x8 or 8x16" },
    { "b", "bits a pixel" },
    { "i", "import its sheet" },
    { NULL, NULL } };

static const line_t sprite[] = {
    { "Sprite tab", NULL },
    { "p", "palette" },
    { "s", "8x8 or 16x16" },
    { "v", "the code below" },
    { "a", "animations" },
    { "Ctrl+T", "the other side" },
    { "Ctrl+E", "ROM or canvas" },
    { NULL, NULL } };

static const line_t anim[] = {
    { "Animation: frames", NULL },
    { "Arrows", "pixel, next frame" },
    { "Shift+arrows", "select" },
    { "Space", "paint (hold and move)" },
    { "Backspace", "transparent (too)" },
    { "[ ]", "colour" },
    { "m / f", "mirror / flip" },
    { "Ctrl+C X V", "copy, cut, paste" },
    { "Ctrl+A", "select the frame" },
    { "d", "copy to the next frame" },
    { "Alt+Up Down", "the frame" },
    { NULL, NULL } };

static const line_t anim_set[] = {
    { "Animation", NULL },
    { "Up from top", "the settings" },
    { "- =", "change the setting" },
    { "- = on name", "other animation" },
    { "Enter / Del", "rename / delete" },
    { "n", "new animation" },
    { "w", "save in the sheet" },
    { "i", "import from the sheet" },
    { "p", "colours shown" },
    { "a", "back to the sprites" },
    { NULL, NULL } };

static const line_t place[] = {
    { "Animation: the sheet", NULL },
    { "Arrows", "the first tile" },
    { "Alt+Up Down", "a page of tiles" },
    { "Enter", "save or import here" },
    { "a / Backspace", "cancel" },
    { NULL, NULL } };

static const line_t map[] = {
    { "Map", NULL },
    { "", "not built yet" },
    { NULL, NULL } };

static const line_t no_sounds[] = {
    { "Sounds", NULL },
    { "", "for NES projects only" },
    { NULL, NULL } };

static const line_t sounds[] = {
    { "Sounds tab", NULL },
    { "Tab", "the next page" },
    { "Space", "play or stop" },
    { "q w e r t y u", "C D E F G A B" },
    { "i o p", "C D E, an octave up" },
    { "2 3 5 6 7 9 0", "the sharps" },
    { "z x", "octave down, up" },
    { "Ctrl+Z Y", "undo, redo" },
    { NULL, NULL } };

static const line_t instruments[] = {
    { "Instruments", NULL },
    { "Alt+Up Down", "the instrument" },
    { "a / Del", "new / delete (twice)" },
    { "Up Down", "the field" },
    { "- =", "value (_ + big step)" },
    { "Enter", "type the name" },
    { "On the volumes:", NULL },
    { "Left Right", "the frame" },
    { "- = / _ +", "volume by 1 / by 4" },
    { "d / Backspace", "add / remove a frame" },
    { "[ / ]", "loop / release here" },
    { NULL, NULL } };

static const line_t snes_instruments[] = {
    { "Instruments", NULL },
    { "Alt+Up Down", "the instrument" },
    { "a / Del", "new / delete (twice)" },
    { "Up Down", "the setting" },
    { "- =", "value (_ + big step)" },
    { "Enter", "name, or listen" },
    { "", "attack: larger, faster" },
    { "", "release 0: it holds" },
    { NULL, NULL } };

static const line_t sfx[] = {
    { "SFX", NULL },
    { "Alt+Up Down", "the effect" },
    { "a / Del", "new / delete (head)" },
    { "Up Down", "head or steps" },
    { "- =", "value, or the note" },
    { "_ +", "an octave" },
    { "Enter", "name; a note on/off" },
    { "1", "release (OFF)" },
    { "; '", "volume" },
    { "k l / n m", "effect / its amount" },
    { "Backspace", "remove the step" },
    { NULL, NULL } };

static const line_t pattern[] = {
    { "Pattern", NULL },
    { "Arrows", "track and step" },
    { "Up from track 1", "pattern, steps, speed" },
    { "Enter", "a note on/off" },
    { "1", "release (OFF)" },
    { "- =", "half tone (flat, sharp)" },
    { "_ +", "an octave" },
    { "[ ]", "instrument" },
    { "Backspace", "empty the step" },
    { "Alt+Up Down", "the pattern" },
    { "Ctrl+C V", "copy, paste it" },
    { NULL, NULL } };

static const line_t song[] = {
    { "Song", NULL },
    { "Alt+Up Down", "the song" },
    { "a / Del", "new / delete (head)" },
    { "Up Down", "head or order" },
    { "Enter", "name; add or open" },
    { "- =", "value, or pattern" },
    { "_ +", "pattern by 4" },
    { "i", "double the place" },
    { "Backspace", "remove the place" },
    { "[", "loop to here" },
    { NULL, NULL } };

/* The lists of each context: the left column, then the right one before the keys of the SDK. */
static const struct { const line_t *left, *right; } screens[SDK_KEYS_COUNT] = {
    [SDK_KEYS_PROJECT] = { project, NULL },
    [SDK_KEYS_EDITOR] = { editor, with_rom },
    [SDK_KEYS_ROM] = { rom, with_rom },
    [SDK_KEYS_SHEET] = { sheet, sprite },
    [SDK_KEYS_CANVAS] = { canvas, sprite },
    [SDK_KEYS_TILES] = { tiles, sprite },
    [SDK_KEYS_MAP] = { map, NULL },
    [SDK_KEYS_SOUND_INSTRUMENTS] = { instruments, sounds },
    [SDK_KEYS_SOUND_SFX] = { sfx, sounds },
    [SDK_KEYS_SOUND_PATTERN] = { pattern, sounds },
    [SDK_KEYS_SOUND_SONG] = { song, sounds },
    [SDK_KEYS_SOUND_NONE] = { no_sounds, NULL },
    [SDK_KEYS_ANIM] = { anim, anim_set },
    [SDK_KEYS_ANIM_PLACE] = { place, NULL },
    [SDK_KEYS_SOUND_SNES_INS] = { snes_instruments, sounds } };

/* A list from the row `row` of a column; the row after it, with an empty one. */
static int list(const line_t *l, int row, int col, int width, int rows)
{
    for (; l && (l->keys || l->what) && row <= rows; l++, row++) {
        sdk_at(row, col);
        if (!l->what) {
            console_write("\x1b[93m");
            sdk_put_row(row, col, width, l->keys, (int)strlen(l->keys));
        } else {
            console_write("\x1b[97m");
            sdk_put_row(row, col, 16, l->keys, (int)strlen(l->keys));
            console_write("\x1b[96m");
            sdk_put_row(row, col + 16, width - 16, l->what, (int)strlen(l->what));
        }
        console_write("\x1b[0m");
    }
    return row + 1;
}

void sdk_keys_draw(int what, int cols, int rows)
{
    if (what < 0 || what >= SDK_KEYS_COUNT)
        what = SDK_KEYS_PROJECT;
    console_cursor_mode(CONSOLE_CURSOR_HIDDEN);
    console_write("\x1b[0m\x1b[2J");
    sdk_title_bar(1, 1, cols, "Keys   (release F12 to go back)", 1);
    const int half = cols / 2, width = half - 2;
    list(screens[what].left, 3, 2, width, rows);
    int row = 3;
    if (screens[what].right)
        row = list(screens[what].right, row, half + 1, width, rows);
    list(sdk, row, half + 1, width, rows - 1);          /* the last cell of the screen stays: no scroll */
    sdk_at(1, 1);
}
