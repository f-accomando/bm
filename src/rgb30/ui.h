#ifndef RGB30_UI_H
#define RGB30_UI_H

#include "drivers/fb.h"

/* The menu (never returns). */
void ui_home(framebuffer_t *fb) __attribute__((noreturn));

/* Lua lines from the serial port until exit() or Ctrl-D (main.c). */
void ui_serial_repl(void);

/* Pages of the Dev tab that Settings opens too (kernel/settings.c): the
 * input test, the log since boot, the screen modes with their test image. */
void ui_input_test(void);
void ui_show_log(void);
void ui_screen_modes(void);

#endif
