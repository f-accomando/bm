#ifndef MONITOR_H
#define MONITOR_H

/* The monitor: the bm shell (a menu, shell_menu.c, and the bm: prompt). It has no hotkeys. Never returns. */
void monitor_run(void) __attribute__((noreturn));

#endif
