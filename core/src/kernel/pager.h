/*
 * Long text on the console, one screen at a time: arrows / PgUp / PgDn /
 * space (w and s from the serial port) scroll, q or Esc return. Text that
 * fits on one screen is just printed.
 */
#ifndef PAGER_H
#define PAGER_H

void pager_show(const char *text);

#endif
