#ifndef RGB30_UI_H
#define RGB30_UI_H

#include "drivers/fb.h"

/* The menu (never returns). */
void ui_home(framebuffer_t *fb) __attribute__((noreturn));

/* Lua lines from the serial port until exit() or Ctrl-D (main.c). */
void ui_serial_repl(void);

#endif
