/*
 * A development tool built into the kernel (the Dev tab) and the games it
 * tries: one loop for the Pi (src/kernel/carts.c, the whole suite) and the
 * RGB30 (src/rgb30/ui.c, bm SDK (RGB30) alone), each with its own tools.
 */
#ifndef DEVTOOL_H
#define DEVTOOL_H

#include <stddef.h>
#include <stdint.h>
#include "drivers/fb.h"

/* A tool of the console: its name for cart_tool() ("sdk", "studio"...),
 * the name it is shown with ("SDK", "bm Studio"), its cartridge. */
typedef struct {
    const char *name, *what;
    const uint8_t *start, *end;
} devtool_t;

/* The tools this console has, and what it does after each run of a tool or
 * of a game it tries (NULL: nothing; the RGB30: wait until the buttons
 * that left are let go). */
typedef struct {
    const devtool_t *tools;
    size_t n;
    void (*after_run)(void);
} devtools_t;

/* Plays the tool (cart, cart_len, shown as `what`) as a tool: it saves
 * where it is told (bm_set_tool). When it asks to play a file (cart_run),
 * plays it, then opens the tool again on that file with cart_arg() =
 * {path, error, back = true, run}; when it asks for another tool of `set`
 * (cart_tool), that one opens on the file with cart_arg().from; a name the
 * set has not ends the session, as the tool closing does. `open`: a file to
 * start on, or NULL. Returns the `what` of the last tool. */
const char *devtool_session(framebuffer_t *fb, const devtools_t *set, const uint8_t *cart, size_t cart_len,
                            const char *what, const char *open);

#endif
